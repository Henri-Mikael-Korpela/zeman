#ifndef DEVICES_VIEW_H
#define DEVICES_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "app.h"
#include "protocol.h"
#include "qrcodegen.h"
#include "server.h"

enum {
    // The link is at most PROTOCOL_LINK_SIZE bytes, which version 4 holds even at error
    // correction level Q; the limit only sizes the buffer.
    DEVICES_VIEW_QR_VERSION_MAX = 6,
};

// The Connected Devices view: a QR code the phone scans to connect, the address to type
// instead, and the phones connected now.
typedef struct DevicesView {
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(DEVICES_VIEW_QR_VERSION_MAX)];
    char link[PROTOCOL_LINK_SIZE];
    bool qr_ready;  // False when the server is off, so there is nothing to connect to
} DevicesView;

// Makes the QR code for the server's address. Call after server_start.
void devices_view_init(DevicesView *view, const Server *server);
void devices_view_draw(const DevicesView *view, const Server *server, const App *app,
                       Rectangle area);

#endif
