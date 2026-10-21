#include <zeman/devices_view.h>

#include <assert.h>
#include <stdio.h>
#include <time.h>

enum {
    MARGIN_PIXELS = 40,
    LINE_PIXELS = 28,
    // Large enough that a phone camera reads the code from an arm's length off the screen.
    QR_MODULE_PIXELS = 8,
    // The blank border QR codes need around them, in modules.
    QR_QUIET_ZONE_MODULES = 4,
    // Room for a sentence with the longest server error in it.
    TEXT_SIZE = SERVER_ERROR_SIZE + 64,
};

static float devices_view_draw_qr(const Devices_View *view, float x, float y);
static void devices_view_draw_clients(const Server *server, const App *app, float x, float y);

void devices_view_init(Devices_View *view, const Server *server) {
    view->qr_ready = false;
    if (!server->listening) {
        return;
    }
    protocol_link_format(view->link, sizeof(view->link), server->address, server->port);
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(DEVICES_VIEW_QR_VERSION_MAX)];
    // Medium error correction, which survives glare and a partly hidden code on a screen.
    view->qr_ready = qrcodegen_encodeText(view->link, scratch, view->qr, qrcodegen_Ecc_MEDIUM,
                                          qrcodegen_VERSION_MIN, DEVICES_VIEW_QR_VERSION_MAX,
                                          qrcodegen_Mask_AUTO, true);
    // The link is short and fixed in form, so it always fits.
    assert(view->qr_ready);
}

void devices_view_draw(const Devices_View *view, const Server *server, const App *app,
                       Rectangle area) {
    const float x = area.x + MARGIN_PIXELS;
    float y = area.y + MARGIN_PIXELS;
    app_draw_text(app, APP_TEXT_HEADING, "Connected Devices", x, y);
    y += 2 * LINE_PIXELS;

    char text[TEXT_SIZE] = "";
    if (!server->listening || !view->qr_ready) {
        snprintf(text, sizeof(text), "Phone access is off: %s", server->error);
        app_draw_text(app, APP_TEXT_NORMAL, text, x, y);
        return;
    }
    app_draw_text(app, APP_TEXT_NORMAL, "Scan this code with the Zeman app on your phone:",
                  x, y);
    y += LINE_PIXELS + 8;
    float qr_size = devices_view_draw_qr(view, x, y);

    // Beside the code: the address to type instead, and who is connected.
    const float side_x = x + qr_size + MARGIN_PIXELS;
    float side_y = y;
    app_draw_text(app, APP_TEXT_DIM, "Or type this address in the app:", side_x, side_y);
    side_y += LINE_PIXELS;
    if (server->port == PROTOCOL_PORT_DEFAULT) {
        snprintf(text, sizeof(text), "%s", server->address);
    } else {
        snprintf(text, sizeof(text), "%s:%u", server->address, (unsigned)server->port);
    }
    app_draw_text(app, APP_TEXT_HEADING, text, side_x, side_y);
    side_y += 2 * LINE_PIXELS;
    app_draw_text(app, APP_TEXT_DIM, "The phone must be on the same Wi-Fi network.", side_x,
                  side_y);
    side_y += 2 * LINE_PIXELS;
    devices_view_draw_clients(server, app, side_x, side_y);
}

/**
 * Draws the code on white with its quiet zone, and returns its width in pixels.
 */
static float devices_view_draw_qr(const Devices_View *view, float x, float y) {
    const int32_t modules = qrcodegen_getSize(view->qr);
    const float size = (float)((modules + 2 * QR_QUIET_ZONE_MODULES) * QR_MODULE_PIXELS);
    DrawRectangleRec((Rectangle){ x, y, size, size }, WHITE);
    const float origin_x = x + QR_QUIET_ZONE_MODULES * QR_MODULE_PIXELS;
    const float origin_y = y + QR_QUIET_ZONE_MODULES * QR_MODULE_PIXELS;
    for (int32_t row = 0; row < modules; row++) {
        for (int32_t column = 0; column < modules; column++) {
            if (qrcodegen_getModule(view->qr, column, row)) {
                DrawRectangleRec((Rectangle){ origin_x + (float)(column * QR_MODULE_PIXELS),
                                              origin_y + (float)(row * QR_MODULE_PIXELS),
                                              QR_MODULE_PIXELS, QR_MODULE_PIXELS },
                                 BLACK);
            }
        }
    }
    return size;
}

static void devices_view_draw_clients(const Server *server, const App *app, float x, float y) {
    app_draw_text(app, APP_TEXT_NORMAL, "Connected now", x, y);
    y += LINE_PIXELS + 4;
    int32_t shown = 0;
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        const Server_Client *client = &server->clients[i];
        if (!client->active) {
            continue;
        }
        time_t connected_at = (time_t)client->connected_at;
        const struct tm *local = localtime(&connected_at);
        char since[16] = "?";
        if (local != NULL) {
            strftime(since, sizeof(since), "%H:%M", local);
        }
        char text[TEXT_SIZE] = "";
        snprintf(text, sizeof(text), "%s, since %s", client->address, since);
        app_draw_text(app, APP_TEXT_NORMAL, text, x, y);
        y += LINE_PIXELS;
        shown++;
    }
    if (shown == 0) {
        app_draw_text(app, APP_TEXT_DIM, "No phone is connected.", x, y);
    }
}
