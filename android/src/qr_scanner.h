#ifndef QR_SCANNER_H
#define QR_SCANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#include "raylib.h"

enum {
    // Frames small enough to decode every frame, large enough for a code on a screen across a
    // desk. Every Android camera offers this size.
    QR_SCANNER_WIDTH = 640,
    QR_SCANNER_HEIGHT = 480,
    QR_SCANNER_ERROR_SIZE = 160,
};

struct quirc;

// Reads QR codes through the back camera, with Android's native camera API and the quirc
// decoder, and shows what the camera sees. Only the brightness of each frame is used: it is all
// a QR code needs, and it is the first plane of the camera's YUV images.
typedef struct QR_Scanner {
    uint8_t luminance[QR_SCANNER_WIDTH * QR_SCANNER_HEIGHT];
    ACameraDevice_StateCallbacks device_callbacks;  // Kept here, as the device holds them
    ACameraCaptureSession_stateCallbacks session_callbacks;
    ACameraManager *manager;
    ACameraDevice *device;
    AImageReader *reader;
    ACaptureSessionOutputContainer *outputs;
    ACaptureSessionOutput *output;
    ACameraOutputTarget *target;
    ACaptureRequest *request;
    ACameraCaptureSession *session;
    struct quirc *decoder;
    Texture2D preview;
    int32_t sensor_orientation;  // Degrees clockwise that frames turn to be upright
    bool running;
    bool has_frame;  // A frame has arrived, so the preview shows something
    char error[QR_SCANNER_ERROR_SIZE];
} QR_Scanner;

// Needs the camera permission and the window. Returns false, with error set, when there is no
// usable camera.
bool qr_scanner_start(QR_Scanner *scanner);
void qr_scanner_stop(QR_Scanner *scanner);
// Takes the newest frame, if one arrived, and looks for a code in it. Returns true when one was
// read; text then holds its content.
bool qr_scanner_poll(QR_Scanner *scanner, char *text, size_t text_size);
// Draws the newest frame upright, as large as fits in area.
void qr_scanner_draw(const QR_Scanner *scanner, Rectangle area);

#endif
