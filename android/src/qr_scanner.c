#include "qr_scanner.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImage.h>

#include "quirc.h"

enum {
    // Two images, so that the camera can fill one while the app reads the other.
    QR_SCANNER_IMAGE_COUNT = 2,
    CAMERA_ID_SIZE = 64,
};

static bool qr_scanner_find_camera(QR_Scanner *scanner, char *camera_id, size_t camera_id_size);
static bool qr_scanner_open_session(QR_Scanner *scanner);
static bool qr_scanner_copy_frame(QR_Scanner *scanner, AImage *image);
static bool qr_scanner_fail(QR_Scanner *scanner, const char *message);
static void on_device_disconnected(void *context, ACameraDevice *device);
static void on_device_error(void *context, ACameraDevice *device, int error);
static void on_session_state(void *context, ACameraCaptureSession *session);

bool qr_scanner_start(QR_Scanner *scanner) {
    assert(!scanner->running);
    scanner->error[0] = '\0';
    scanner->has_frame = false;
    scanner->manager = ACameraManager_create();
    char camera_id[CAMERA_ID_SIZE];
    if (scanner->manager == NULL || !qr_scanner_find_camera(scanner, camera_id,
                                                             sizeof(camera_id))) {
        return qr_scanner_fail(scanner, "This phone has no camera the app can use.");
    }
    // The callbacks report a camera taken away by the system; scanning then just stops seeing
    // new frames, and the user can cancel.
    scanner->device_callbacks = (ACameraDevice_StateCallbacks){
        .context = scanner,
        .onDisconnected = on_device_disconnected,
        .onError = on_device_error,
    };
    if (ACameraManager_openCamera(scanner->manager, camera_id, &scanner->device_callbacks,
                                  &scanner->device) != ACAMERA_OK) {
        return qr_scanner_fail(scanner, "The camera could not be opened.");
    }
    if (!qr_scanner_open_session(scanner)) {
        return qr_scanner_fail(scanner, "The camera could not be started.");
    }

    // quirc allocates its buffers once here, not per frame.
    scanner->decoder = quirc_new();
    if (scanner->decoder == NULL ||
        quirc_resize(scanner->decoder, QR_SCANNER_WIDTH, QR_SCANNER_HEIGHT) < 0) {
        return qr_scanner_fail(scanner, "Not enough memory to read QR codes.");
    }
    memset(scanner->luminance, 0, sizeof(scanner->luminance));
    Image image = { .data = scanner->luminance, .width = QR_SCANNER_WIDTH,
                    .height = QR_SCANNER_HEIGHT, .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    scanner->preview = LoadTextureFromImage(image);
    scanner->running = true;
    return true;
}

// Releases everything in the reverse order of qr_scanner_start; also after a start that failed
// halfway, so every step checks what exists.
void qr_scanner_stop(QR_Scanner *scanner) {
    if (scanner->session != NULL) {
        ACameraCaptureSession_stopRepeating(scanner->session);
        ACameraCaptureSession_close(scanner->session);
        scanner->session = NULL;
    }
    if (scanner->request != NULL) {
        ACaptureRequest_free(scanner->request);
        scanner->request = NULL;
    }
    if (scanner->target != NULL) {
        ACameraOutputTarget_free(scanner->target);
        scanner->target = NULL;
    }
    if (scanner->outputs != NULL) {
        ACaptureSessionOutputContainer_free(scanner->outputs);
        scanner->outputs = NULL;
    }
    if (scanner->output != NULL) {
        ACaptureSessionOutput_free(scanner->output);
        scanner->output = NULL;
    }
    if (scanner->device != NULL) {
        ACameraDevice_close(scanner->device);
        scanner->device = NULL;
    }
    if (scanner->reader != NULL) {
        AImageReader_delete(scanner->reader);
        scanner->reader = NULL;
    }
    if (scanner->manager != NULL) {
        ACameraManager_delete(scanner->manager);
        scanner->manager = NULL;
    }
    if (scanner->decoder != NULL) {
        quirc_destroy(scanner->decoder);
        scanner->decoder = NULL;
    }
    if (scanner->running) {
        UnloadTexture(scanner->preview);
    }
    scanner->running = false;
    scanner->has_frame = false;
}

bool qr_scanner_poll(QR_Scanner *scanner, char *text, size_t text_size) {
    assert(text_size > 0);
    if (!scanner->running) {
        return false;
    }
    AImage *image = NULL;
    if (AImageReader_acquireLatestImage(scanner->reader, &image) != AMEDIA_OK || image == NULL) {
        return false; // No new frame yet.
    }
    bool copied = qr_scanner_copy_frame(scanner, image);
    AImage_delete(image);
    if (!copied) {
        return false;
    }
    UpdateTexture(scanner->preview, scanner->luminance);
    scanner->has_frame = true;

    int width = 0;
    int height = 0;
    uint8_t *buffer = quirc_begin(scanner->decoder, &width, &height);
    assert(width == QR_SCANNER_WIDTH && height == QR_SCANNER_HEIGHT);
    memcpy(buffer, scanner->luminance, sizeof(scanner->luminance));
    quirc_end(scanner->decoder);
    // Bounded by quirc, which finds a handful of codes at most in one frame.
    int count = quirc_count(scanner->decoder);
    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(scanner->decoder, i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
            continue;
        }
        size_t length = (size_t)data.payload_len;
        if (length >= text_size) {
            continue; // Far longer than any code the desktop app makes.
        }
        memcpy(text, data.payload, length);
        text[length] = '\0';
        return true;
    }
    return false;
}

void qr_scanner_draw(const QR_Scanner *scanner, Rectangle area) {
    DrawRectangleRec(area, BLACK);
    if (!scanner->running || !scanner->has_frame) {
        return;
    }
    // A frame turned by a quarter swaps its width and height on the screen.
    bool sideways = scanner->sensor_orientation == 90 || scanner->sensor_orientation == 270;
    float shown_width = sideways ? QR_SCANNER_HEIGHT : QR_SCANNER_WIDTH;
    float shown_height = sideways ? QR_SCANNER_WIDTH : QR_SCANNER_HEIGHT;
    float fit = area.width / shown_width < area.height / shown_height
                    ? area.width / shown_width
                    : area.height / shown_height;
    float width = QR_SCANNER_WIDTH * fit;
    float height = QR_SCANNER_HEIGHT * fit;
    Rectangle source = { 0, 0, QR_SCANNER_WIDTH, QR_SCANNER_HEIGHT };
    Rectangle target = { area.x + area.width / 2, area.y + area.height / 2, width, height };
    DrawTexturePro(scanner->preview, source, target, (Vector2){ width / 2, height / 2 },
                   (float)scanner->sensor_orientation, WHITE);
}

// The first back-facing camera, or else the first camera; and how its frames are turned.
static bool qr_scanner_find_camera(QR_Scanner *scanner, char *camera_id, size_t camera_id_size) {
    ACameraIdList *ids = NULL;
    if (ACameraManager_getCameraIdList(scanner->manager, &ids) != ACAMERA_OK || ids == NULL) {
        return false;
    }
    int chosen = -1;
    scanner->sensor_orientation = 90;
    for (int i = 0; i < ids->numCameras; i++) {
        ACameraMetadata *metadata = NULL;
        if (ACameraManager_getCameraCharacteristics(scanner->manager, ids->cameraIds[i],
                                                    &metadata) != ACAMERA_OK) {
            continue;
        }
        ACameraMetadata_const_entry facing = { 0 };
        ACameraMetadata_const_entry orientation = { 0 };
        bool back = ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &facing) ==
                        ACAMERA_OK && facing.data.u8[0] == ACAMERA_LENS_FACING_BACK;
        if (chosen < 0 || back) {
            chosen = i;
            if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION,
                                              &orientation) == ACAMERA_OK) {
                scanner->sensor_orientation = orientation.data.i32[0];
            }
        }
        ACameraMetadata_free(metadata);
        if (back) {
            break;
        }
    }
    bool found = chosen >= 0;
    if (found) {
        snprintf(camera_id, camera_id_size, "%s", ids->cameraIds[chosen]);
    }
    ACameraManager_deleteCameraIdList(ids);
    return found;
}

// Frames go to an image reader, which the app reads at its own pace; preview frames with
// continuous autofocus, so that a code held close is sharp.
static bool qr_scanner_open_session(QR_Scanner *scanner) {
    if (AImageReader_new(QR_SCANNER_WIDTH, QR_SCANNER_HEIGHT, AIMAGE_FORMAT_YUV_420_888,
                         QR_SCANNER_IMAGE_COUNT, &scanner->reader) != AMEDIA_OK) {
        return false;
    }
    ANativeWindow *window = NULL;
    if (AImageReader_getWindow(scanner->reader, &window) != AMEDIA_OK) {
        return false;
    }
    if (ACaptureSessionOutputContainer_create(&scanner->outputs) != ACAMERA_OK ||
        ACaptureSessionOutput_create(window, &scanner->output) != ACAMERA_OK ||
        ACaptureSessionOutputContainer_add(scanner->outputs, scanner->output) != ACAMERA_OK ||
        ACameraOutputTarget_create(window, &scanner->target) != ACAMERA_OK) {
        return false;
    }
    if (ACameraDevice_createCaptureRequest(scanner->device, TEMPLATE_PREVIEW,
                                           &scanner->request) != ACAMERA_OK ||
        ACaptureRequest_addTarget(scanner->request, scanner->target) != ACAMERA_OK) {
        return false;
    }
    uint8_t focus = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
    ACaptureRequest_setEntry_u8(scanner->request, ACAMERA_CONTROL_AF_MODE, 1, &focus);
    scanner->session_callbacks = (ACameraCaptureSession_stateCallbacks){
        .context = scanner,
        .onClosed = on_session_state,
        .onReady = on_session_state,
        .onActive = on_session_state,
    };
    if (ACameraDevice_createCaptureSession(scanner->device, scanner->outputs,
                                           &scanner->session_callbacks,
                                           &scanner->session) != ACAMERA_OK) {
        return false;
    }
    return ACameraCaptureSession_setRepeatingRequest(scanner->session, NULL, 1,
                                                     &scanner->request, NULL) == ACAMERA_OK;
}

// Copies the brightness plane row by row, because its rows may be padded past the width.
static bool qr_scanner_copy_frame(QR_Scanner *scanner, AImage *image) {
    int32_t width = 0;
    int32_t height = 0;
    int32_t row_stride = 0;
    uint8_t *data = NULL;
    int length = 0;
    if (AImage_getWidth(image, &width) != AMEDIA_OK ||
        AImage_getHeight(image, &height) != AMEDIA_OK ||
        AImage_getPlaneRowStride(image, 0, &row_stride) != AMEDIA_OK ||
        AImage_getPlaneData(image, 0, &data, &length) != AMEDIA_OK) {
        return false;
    }
    if (width != QR_SCANNER_WIDTH || height != QR_SCANNER_HEIGHT || row_stride < width) {
        return false;
    }
    if ((int64_t)length < (int64_t)(height - 1) * row_stride + width) {
        return false;
    }
    for (int32_t row = 0; row < height; row++) {
        memcpy(scanner->luminance + row * QR_SCANNER_WIDTH, data + row * row_stride,
               (size_t)width);
    }
    return true;
}

static bool qr_scanner_fail(QR_Scanner *scanner, const char *message) {
    snprintf(scanner->error, sizeof(scanner->error), "%s", message);
    qr_scanner_stop(scanner);
    return false;
}

static void on_device_disconnected(void *context, ACameraDevice *device) {
    (void)context;
    (void)device;
}

static void on_device_error(void *context, ACameraDevice *device, int error) {
    (void)context;
    (void)device;
    (void)error;
}

static void on_session_state(void *context, ACameraCaptureSession *session) {
    (void)context;
    (void)session;
}
