#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "raylib.h"
#include "raygui.h"

#include "android_input.h"
#include "android_permission.h"
#include "app.h"
#include "client.h"
#include "discovery.h"
#include "net.h"
#include "protocol.h"
#include "qr_scanner.h"

enum {
    // The phone layout is this many layout pixels wide, whatever the screen's resolution.
    LAYOUT_WIDTH_PIXELS = 420,
    // A full link, as the QR code holds it, or an address typed without the prefix.
    ADDRESS_TEXT_SIZE = PROTOCOL_LINK_SIZE,
    PATH_SIZE = 512,
    MESSAGE_SIZE = 160,
    INFO_SIZE = 64,
    MARGIN_PIXELS = 16,
    LINE_PIXELS = 28,
    BUTTON_HEIGHT_PIXELS = 44,
};

typedef enum Screen {
    SCREEN_CONNECT,
    SCREEN_SCAN,
    SCREEN_CALENDAR,
} Screen;

typedef enum ConnectResult {
    CONNECT_NONE,
    CONNECT_CHOSEN,     // The address to use is ready
    CONNECT_SCAN,       // Scan the desktop's QR code
    CONNECT_CANCELLED,  // Back to the calendar with the desktop chosen before
} ConnectResult;

// The screen for choosing the desktop app: by its QR code, by a desktop found on the network,
// or by typing its address.
typedef struct ConnectScreen {
    Discovery discovery;
    char address[ADDRESS_TEXT_SIZE];
    char message[MESSAGE_SIZE];  // Why the address cannot be used, or empty
    bool editing;                // The address box has the keyboard
    bool can_cancel;             // A desktop was chosen before, so Back returns to it
    bool open;
} ConnectScreen;

// The screen that reads the desktop's QR code with the camera.
typedef struct ScanScreen {
    QrScanner scanner;
    char message[MESSAGE_SIZE];  // A code that is not the desktop app's, or a camera problem
    bool permission_asked;
} ScanScreen;

// Everything the phone app runs. The client and scanner hold large buffers, so this lives in
// static storage rather than on the stack.
typedef struct Phone {
    Client client;
    ConnectScreen connect;
    ScanScreen scan;
    AndroidInput input;
    App *app;
    float scale;
    Screen screen;
    char address[ADDRESS_TEXT_SIZE];  // The desktop in use, as saved
} Phone;

static void phone_frame(Phone *phone);
static void desktop_choose(Client *client, App *app, const char *address);
static void settings_path(char *path, size_t path_size);
static bool settings_load(char *address, size_t address_size);
static void settings_save(const char *address);
static void screen_begin(float scale);
static void screen_end(void);
static void connect_screen_open(ConnectScreen *screen, const char *address, bool can_cancel);
static void connect_screen_close(ConnectScreen *screen);
static ConnectResult connect_screen_frame(ConnectScreen *screen, float scale);
static ConnectResult connect_screen_draw_choices(ConnectScreen *screen, float width, float *y);
static ConnectResult connect_screen_draw_address(ConnectScreen *screen, float width, float y);
static ConnectResult scan_screen_frame(ScanScreen *scan, float scale, char *address,
                                       size_t address_size);
static bool scan_screen_read(ScanScreen *scan, char *address, size_t address_size);

// The Android app: the calendar of a desktop app on the same local network. raylib's Android
// platform code calls this from android_main.
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // Full screen, at the display's own resolution.
    InitWindow(0, 0, "Calendar");
    SetTargetFPS(60);
    static Phone phone;
    android_input_install(&phone.input);

    int shorter_side = GetScreenWidth() < GetScreenHeight() ? GetScreenWidth() : GetScreenHeight();
    phone.scale = (float)shorter_side / LAYOUT_WIDTH_PIXELS;
    client_init(&phone.client);
    Backend backend = client_backend(&phone.client);
    AppConfig config = { .font_directory = "", .scale = phone.scale, .touch = true,
                         .server_button = true };
    phone.app = app_create(&backend, &config);
    phone.screen = SCREEN_CONNECT;
    if (settings_load(phone.address, sizeof(phone.address))) {
        desktop_choose(&phone.client, phone.app, phone.address);
        phone.screen = SCREEN_CALENDAR;
    } else {
        connect_screen_open(&phone.connect, "", false);
    }

    // The one intentionally unbounded loop: it runs until Android closes the app.
    while (!WindowShouldClose()) {
        phone_frame(&phone);
    }

    qr_scanner_stop(&phone.scan.scanner);
    connect_screen_close(&phone.connect);
    app_destroy(phone.app);
    client_deinit(&phone.client);
    CloseWindow();
    return 0;
}

// Draws the current screen and moves between screens: connection, QR scan and calendar.
static void phone_frame(Phone *phone) {
    bool typing = false;
    ConnectResult result = CONNECT_NONE;
    char chosen[ADDRESS_TEXT_SIZE] = "";
    if (phone->screen == SCREEN_CONNECT) {
        result = connect_screen_frame(&phone->connect, phone->scale);
        typing = phone->connect.editing;
        snprintf(chosen, sizeof(chosen), "%s", phone->connect.address);
    } else if (phone->screen == SCREEN_SCAN) {
        result = scan_screen_frame(&phone->scan, phone->scale, chosen, sizeof(chosen));
    } else {
        BeginDrawing();
        app_frame(phone->app, (Rectangle){ 0, 0, (float)GetScreenWidth(),
                                           (float)GetScreenHeight() });
        EndDrawing();
        typing = app_text_input_active(phone->app);
        if (app_take_server_request(phone->app)) {
            connect_screen_open(&phone->connect, phone->address, true);
            phone->screen = SCREEN_CONNECT;
        }
    }

    if (result == CONNECT_SCAN) {
        phone->scan.message[0] = '\0';
        phone->scan.permission_asked = false;
        phone->screen = SCREEN_SCAN;
    } else if (result == CONNECT_CHOSEN) {
        snprintf(phone->address, sizeof(phone->address), "%s", chosen);
        desktop_choose(&phone->client, phone->app, phone->address);
        settings_save(phone->address);
    }
    // A cancelled scan goes back to the connection screen, which stays open meanwhile.
    if (phone->screen == SCREEN_SCAN && result == CONNECT_CANCELLED) {
        phone->screen = SCREEN_CONNECT;
    } else if (result == CONNECT_CHOSEN || result == CONNECT_CANCELLED) {
        qr_scanner_stop(&phone->scan.scanner);
        connect_screen_close(&phone->connect);
        phone->screen = SCREEN_CALENDAR;
    }
    android_input_keyboard_set_visible(&phone->input, typing);
}

// address is a link, "address" or "address:port", already checked by protocol_link_parse.
static void desktop_choose(Client *client, App *app, const char *address) {
    char host[CLIENT_HOST_SIZE];
    uint16_t port = 0;
    bool parsed = protocol_link_parse(address, host, sizeof(host), &port);
    assert(parsed);
    (void)parsed; // Unused when assertions are compiled out.
    client_set_server(client, host, port);
    char info[INFO_SIZE];
    snprintf(info, sizeof(info), "Desktop %s", host);
    app_set_info(app, info);
}

static void settings_path(char *path, size_t path_size) {
    int written = snprintf(path, path_size, "%s/desktop_address.txt",
                           android_input_data_directory());
    assert(written > 0 && (size_t)written < path_size);
}

// Returns false when nothing usable was saved, so that the app asks for the desktop.
static bool settings_load(char *address, size_t address_size) {
    char path[PATH_SIZE];
    settings_path(path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    bool ok = fgets(address, (int)address_size, file) != NULL;
    fclose(file);
    if (!ok) {
        return false;
    }
    address[strcspn(address, "\r\n")] = '\0';
    char host[CLIENT_HOST_SIZE];
    uint16_t port = 0;
    return protocol_link_parse(address, host, sizeof(host), &port);
}

static void settings_save(const char *address) {
    // Some Android versions do not create the app's data folder until something asks for it.
    mkdir(android_input_data_directory(), 0770);
    char path[PATH_SIZE];
    settings_path(path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return; // The phone then asks for the desktop again at the next start.
    }
    fprintf(file, "%s\n", address);
    fclose(file);
}

// The connection and scan screens use the calendar's layout pixels: drawing and touches are
// scaled the same way, as app_frame does for the calendar.
static void screen_begin(float scale) {
    SetMouseScale(1.0f / scale, 1.0f / scale);
    BeginDrawing();
    ClearBackground(GetColor((unsigned int)GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    BeginMode2D((Camera2D){ .offset = { 0, 0 }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
}

static void screen_end(void) {
    EndMode2D();
    EndDrawing();
    SetMouseScale(1.0f, 1.0f);
}

static void connect_screen_open(ConnectScreen *screen, const char *address, bool can_cancel) {
    assert(!screen->open);
    // Searching the network starts at once; without it, the code or the address still work.
    bool searching = discovery_start(&screen->discovery);
    (void)searching;
    snprintf(screen->address, sizeof(screen->address), "%s", address);
    screen->message[0] = '\0';
    screen->editing = false;
    screen->can_cancel = can_cancel;
    screen->open = true;
}

static void connect_screen_close(ConnectScreen *screen) {
    if (!screen->open) {
        return;
    }
    discovery_stop(&screen->discovery);
    screen->editing = false;
    screen->open = false;
}

static ConnectResult connect_screen_frame(ConnectScreen *screen, float scale) {
    assert(screen->open);
    discovery_poll(&screen->discovery);
    if (screen->can_cancel && IsKeyPressed(KEY_BACK)) {
        return CONNECT_CANCELLED;
    }
    screen_begin(scale);
    const float width = (float)GetScreenWidth() / scale;
    float y = 32;
    ConnectResult result = connect_screen_draw_choices(screen, width, &y);
    ConnectResult typed = connect_screen_draw_address(screen, width, y);
    screen_end();
    return result != CONNECT_NONE ? result : typed;
}

// The recommended way first, the QR code; then a desktop found on this network, if any.
static ConnectResult connect_screen_draw_choices(ConnectScreen *screen, float width, float *y) {
    const float x = MARGIN_PIXELS;
    const float content_width = width - 2 * MARGIN_PIXELS;
    ConnectResult result = CONNECT_NONE;
    GuiLabel((Rectangle){ x, *y, content_width, LINE_PIXELS }, "Connect to the desktop calendar");
    *y += 2 * LINE_PIXELS;
    if (GuiButton((Rectangle){ x, *y, content_width, BUTTON_HEIGHT_PIXELS },
                  "Scan QR code (recommended)")) {
        screen->editing = false;
        result = CONNECT_SCAN;
    }
    *y += BUTTON_HEIGHT_PIXELS + 8;
    GuiLabel((Rectangle){ x, *y, content_width, LINE_PIXELS }, "In the desktop app, open");
    GuiLabel((Rectangle){ x, *y + LINE_PIXELS, content_width, LINE_PIXELS },
             "Connected Devices and scan its code.");
    *y += 3 * LINE_PIXELS;

    const Discovery *discovery = &screen->discovery;
    if (discovery->found) {
        char text[MESSAGE_SIZE];
        snprintf(text, sizeof(text), "Found a desktop at %s.", discovery->found_address);
        GuiLabel((Rectangle){ x, *y, content_width - 110, LINE_PIXELS }, text);
        if (GuiButton((Rectangle){ x + content_width - 100, *y - 6, 100, 40 }, "Use it")) {
            protocol_link_format(screen->address, sizeof(screen->address),
                                 discovery->found_address, discovery->found_port);
            result = CONNECT_CHOSEN;
        }
        *y += 2 * LINE_PIXELS;
    }
    return result;
}

// Typing the address, as shown in the desktop's status bar and Connected Devices view.
static ConnectResult connect_screen_draw_address(ConnectScreen *screen, float width, float y) {
    const float x = MARGIN_PIXELS;
    const float content_width = width - 2 * MARGIN_PIXELS;
    ConnectResult result = CONNECT_NONE;
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS }, "Or type its address, such as");
    GuiLabel((Rectangle){ x, y + LINE_PIXELS, content_width, LINE_PIXELS }, "192.168.1.20:");
    y += 2 * LINE_PIXELS + 8;
    if (GuiTextBox((Rectangle){ x, y, content_width, 40 }, screen->address,
                   sizeof(screen->address), screen->editing)) {
        screen->editing = !screen->editing;
    }
    y += 56;
    const float button_width = (content_width - 12) / 2;
    if (screen->can_cancel) {
        if (GuiButton((Rectangle){ x, y, button_width, 40 }, "Cancel")) {
            result = CONNECT_CANCELLED;
        }
    }
    if (GuiButton((Rectangle){ x + button_width + 12, y, button_width, 40 }, "Connect")) {
        char host[CLIENT_HOST_SIZE];
        uint16_t port = 0;
        screen->editing = false;
        if (!protocol_link_parse(screen->address, host, sizeof(host), &port)) {
            snprintf(screen->message, sizeof(screen->message), "Type an address such as "
                                                               "192.168.1.20.");
        } else if (!net_address_is_local(host)) {
            snprintf(screen->message, sizeof(screen->message), "That is not a local address.");
        } else {
            result = CONNECT_CHOSEN;
        }
    }
    y += 56;
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS }, screen->message);
    y += LINE_PIXELS + 8;
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS }, "Both must be on the same Wi-Fi.");
    return result;
}

// Asks for the camera if needed, then shows the camera and reads codes until one is the
// desktop's. Returns CONNECT_CHOSEN with address filled in, or CONNECT_CANCELLED.
static ConnectResult scan_screen_frame(ScanScreen *scan, float scale, char *address,
                                       size_t address_size) {
    QrScanner *scanner = &scan->scanner;
    if (IsKeyPressed(KEY_BACK)) {
        qr_scanner_stop(scanner);
        return CONNECT_CANCELLED;
    }
    bool permitted = scanner->running || android_camera_permitted();
    if (!permitted && !scan->permission_asked) {
        android_camera_permission_request();
        scan->permission_asked = true;
    }
    if (permitted && !scanner->running && scan->message[0] == '\0') {
        if (!qr_scanner_start(scanner)) {
            snprintf(scan->message, sizeof(scan->message), "%s", scanner->error);
        }
    }
    if (scan_screen_read(scan, address, address_size)) {
        qr_scanner_stop(scanner);
        return CONNECT_CHOSEN;
    }

    screen_begin(scale);
    const float width = (float)GetScreenWidth() / scale;
    const float height = (float)GetScreenHeight() / scale;
    const float x = MARGIN_PIXELS;
    const float content_width = width - 2 * MARGIN_PIXELS;
    float y = 32;
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS }, "Scan the desktop's QR code");
    y += LINE_PIXELS + 8;
    const float preview_height = height - y - 4 * LINE_PIXELS - BUTTON_HEIGHT_PIXELS;
    qr_scanner_draw(scanner, (Rectangle){ x, y, content_width, preview_height });
    y += preview_height + 8;
    const char *hint = !permitted ? "Allow the camera to scan the code."
                                  : "Point the camera at the code.";
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS },
             scan->message[0] != '\0' ? scan->message : hint);
    y += 2 * LINE_PIXELS;
    bool cancelled = GuiButton((Rectangle){ x, y, content_width, BUTTON_HEIGHT_PIXELS }, "Cancel");
    screen_end();
    if (cancelled) {
        qr_scanner_stop(scanner);
        return CONNECT_CANCELLED;
    }
    return CONNECT_NONE;
}

// Reads the newest frame; a code that is not a local desktop link is reported and ignored.
static bool scan_screen_read(ScanScreen *scan, char *address, size_t address_size) {
    char text[PROTOCOL_LINK_SIZE * 4];
    if (!qr_scanner_poll(&scan->scanner, text, sizeof(text))) {
        return false;
    }
    char host[CLIENT_HOST_SIZE];
    uint16_t port = 0;
    bool is_link = strncmp(text, PROTOCOL_LINK_PREFIX, strlen(PROTOCOL_LINK_PREFIX)) == 0;
    if (!is_link || !protocol_link_parse(text, host, sizeof(host), &port)) {
        snprintf(scan->message, sizeof(scan->message), "That code is not the desktop app's.");
        return false;
    }
    if (!net_address_is_local(host)) {
        snprintf(scan->message, sizeof(scan->message), "That code is not for this network.");
        return false;
    }
    snprintf(address, address_size, "%s", text);
    return true;
}
