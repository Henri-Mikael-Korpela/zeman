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
    // "Desktop " and the longest host.
    INFO_SIZE = CLIENT_HOST_SIZE + 16,
    MARGIN_PIXELS = 16,
    LINE_PIXELS = 28,
    BUTTON_HEIGHT_PIXELS = 44,
    // A strip along the left edge, always shown: its handle says that the navigation can be
    // pulled out from there, and touches on it never reach the view beside it. It is also the
    // target for starting a drag, so it is wide enough for a thumb: about 8 mm on a typical
    // phone, where 14 pixels (3 mm) was too hard to hit.
    GUTTER_WIDTH_PIXELS = 32,
    // Wide enough for "Connection settings" and an address, narrow enough that the view still
    // shows beside it on a 420 pixel wide layout.
    DRAWER_WIDTH_PIXELS = 280,
    DRAWER_ITEMS_TOP_PIXELS = 72,
    DRAWER_ITEM_HEIGHT_PIXELS = 52,
    // A press that moves this far drags the drawer instead of tapping, because a finger wobbles
    // by a few pixels during a tap.
    DRAWER_DRAG_THRESHOLD_PIXELS = 8,
};

// Share of the way the drawer moves each frame, so that it opens or closes in five frames: a
// twelfth of a second at 60 frames per second, quick but still seen to slide.
static const float DRAWER_STEP = 0.2f;
// Darkening of the view behind the open drawer, so that the drawer reads as on top of it.
static const float DRAWER_SCRIM_ALPHA = 0.4f;

// The views in the navigation drawer, in its order.
typedef enum View {
    VIEW_CALENDAR,
    VIEW_CONNECTION,
    VIEW_COUNT,
} View;

static const char *const view_names[VIEW_COUNT] = { "Calendar", "Connection settings" };

typedef enum ConnectResult {
    CONNECT_NONE,
    CONNECT_CHOSEN,     // The address to use is ready
    CONNECT_SCAN,       // Scan the desktop's QR code
    CONNECT_CANCELLED,  // The scan was given up
} ConnectResult;

// The connection settings view: choosing the desktop app by its QR code, by a desktop found on
// the network, or by typing its address. Choosing the same desktop again reconnects to it.
typedef struct ConnectScreen {
    Discovery discovery;
    char address[ADDRESS_TEXT_SIZE];
    char message[MESSAGE_SIZE];  // Why the address cannot be used, or empty
    bool editing;                // The address box has the keyboard
    bool open;
} ConnectScreen;

// The screen that reads the desktop's QR code with the camera.
typedef struct ScanScreen {
    QrScanner scanner;
    char message[MESSAGE_SIZE];  // A code that is not the desktop app's, or a camera problem
    bool permission_asked;
} ScanScreen;

// The navigation, pulled out from the left edge or opened by tapping the edge's handle.
typedef struct Drawer {
    float open_fraction;   // How far out it is drawn, from 0 (closed) to 1 (open)
    float drag_x;          // Pointer x where the press began, in layout pixels
    float drag_fraction;   // open_fraction when the press began
    bool pressed;          // A press on the edge or on the open drawer is held
    bool dragging;         // That press moved far enough to drag instead of tap
    bool open;             // Where the drawer slides to when not dragged
} Drawer;

// Everything the phone app runs. The client and scanner hold large buffers, so this lives in
// static storage rather than on the stack.
typedef struct Phone {
    Client client;
    ConnectScreen connect;
    ScanScreen scan;
    AndroidInput input;
    Drawer drawer;
    App *app;
    float scale;
    View view;
    bool scanning;                    // The QR scan covers the connection settings
    char address[ADDRESS_TEXT_SIZE];  // The desktop in use, as saved, or empty
} Phone;

static void phone_frame(Phone *phone);
static void phone_frame_scan(Phone *phone);
static void phone_view_set(Phone *phone, View view);
static void phone_desktop_set(Phone *phone, const char *address);
static void phone_status(const Phone *phone, char *text, size_t text_size);
static void desktop_choose(Client *client, App *app, const char *address);
static void settings_path(char *path, size_t path_size);
static bool settings_load(char *address, size_t address_size);
static void settings_save(const char *address);
static View drawer_update(Drawer *drawer, View current);
static View drawer_update_tap(Drawer *drawer, View current, Vector2 pointer);
static bool drawer_is_closed(const Drawer *drawer);
static Rectangle drawer_item(int32_t index, float x);
static void drawer_draw(const Drawer *drawer, const App *app, View current, const char *status,
                        float width, float height);
static void gutter_draw(float height);
static void screen_begin(float scale);
static void screen_end(void);
static void connect_screen_open(ConnectScreen *screen, const char *address);
static void connect_screen_close(ConnectScreen *screen);
static ConnectResult connect_screen_frame(ConnectScreen *screen, const App *app, float scale,
                                          const char *status, bool input);
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
    AppConfig config = { .font_directory = "", .scale = phone.scale, .touch = true };
    phone.app = app_create(&backend, &config);
    phone.view = VIEW_CALENDAR;
    if (settings_load(phone.address, sizeof(phone.address))) {
        desktop_choose(&phone.client, phone.app, phone.address);
    } else {
        // Nothing to show until a desktop is chosen.
        phone_view_set(&phone, VIEW_CONNECTION);
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

// Draws the open view beside the edge's handle, with the drawer over both, and moves between
// views. The drawer reads the pointer first, so that the view ignores it while the drawer is out.
static void phone_frame(Phone *phone) {
    if (phone->scanning) {
        phone_frame_scan(phone);
        return;
    }
    const float scale = phone->scale;
    SetMouseScale(1.0f / scale, 1.0f / scale);
    View chosen = drawer_update(&phone->drawer, phone->view);
    SetMouseScale(1.0f, 1.0f);
    const bool input = drawer_is_closed(&phone->drawer);
    // Back closes the drawer first; then, on the settings, it returns to a chosen desktop.
    bool desktop_chosen = phone->address[0] != '\0';
    if (input && phone->view == VIEW_CONNECTION && desktop_chosen && IsKeyPressed(KEY_BACK)) {
        chosen = VIEW_CALENDAR;
    }
    phone_view_set(phone, chosen);
    if (!input) {
        phone->connect.editing = false;
    }
    char status[MESSAGE_SIZE];
    phone_status(phone, status, sizeof(status));

    bool typing = false;
    ConnectResult result = CONNECT_NONE;
    BeginDrawing();
    ClearBackground(GetColor((unsigned int)GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    if (phone->view == VIEW_CALENDAR) {
        const float gutter = GUTTER_WIDTH_PIXELS * scale;
        app_set_input_enabled(phone->app, input);
        app_frame(phone->app, (Rectangle){ gutter, 0, (float)GetScreenWidth() - gutter,
                                           (float)GetScreenHeight() });
        typing = input && app_text_input_active(phone->app);
    } else {
        result = connect_screen_frame(&phone->connect, phone->app, scale, status, input);
        typing = phone->connect.editing;
    }
    const float width = (float)GetScreenWidth() / scale;
    const float height = (float)GetScreenHeight() / scale;
    BeginMode2D((Camera2D){ .offset = { 0, 0 }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
    gutter_draw(height);
    drawer_draw(&phone->drawer, phone->app, phone->view, status, width, height);
    EndMode2D();
    EndDrawing();

    if (result == CONNECT_SCAN) {
        phone->scan.message[0] = '\0';
        phone->scan.permission_asked = false;
        phone->scanning = true;
    } else if (result == CONNECT_CHOSEN) {
        phone_desktop_set(phone, phone->connect.address);
    }
    android_input_keyboard_set_visible(&phone->input, typing);
}

// The QR scan covers the whole screen; giving it up returns to the connection settings, which
// stay open meanwhile.
static void phone_frame_scan(Phone *phone) {
    assert(phone->view == VIEW_CONNECTION);
    assert(phone->connect.open);
    char chosen[ADDRESS_TEXT_SIZE] = "";
    ConnectResult result = scan_screen_frame(&phone->scan, phone->scale, chosen, sizeof(chosen));
    if (result == CONNECT_CANCELLED) {
        phone->scanning = false;
    } else if (result == CONNECT_CHOSEN) {
        phone->scanning = false;
        phone_desktop_set(phone, chosen);
    }
    android_input_keyboard_set_visible(&phone->input, false);
}

// The connection settings search the network only while they are open.
static void phone_view_set(Phone *phone, View view) {
    assert(view >= 0);
    assert(view < VIEW_COUNT);
    if (view == phone->view) {
        return;
    }
    if (view == VIEW_CONNECTION) {
        connect_screen_open(&phone->connect, phone->address);
    } else {
        connect_screen_close(&phone->connect);
    }
    phone->view = view;
}

// Connects to the desktop at address, already checked, remembers it for the next start, and
// shows its calendar. Choosing the desktop in use again reconnects to it.
static void phone_desktop_set(Phone *phone, const char *address) {
    assert(address[0] != '\0');
    char copy[ADDRESS_TEXT_SIZE];
    snprintf(copy, sizeof(copy), "%s", address);  // address may be the connection screen's
    snprintf(phone->address, sizeof(phone->address), "%s", copy);
    desktop_choose(&phone->client, phone->app, phone->address);
    settings_save(phone->address);
    phone_view_set(phone, VIEW_CALENDAR);
}

// One line on the connection, for the drawer and the connection settings.
static void phone_status(const Phone *phone, char *text, size_t text_size) {
    assert(text_size > 0);
    char host[CLIENT_HOST_SIZE] = "";
    uint16_t port = 0;
    // Saved and chosen addresses are checked first, so only an empty one fails to parse.
    if (!protocol_link_parse(phone->address, host, sizeof(host), &port)) {
        assert(phone->address[0] == '\0');
        snprintf(text, text_size, "No desktop chosen yet.");
    } else if (client_is_connected(&phone->client)) {
        snprintf(text, text_size, "Connected to %s", host);
    } else {
        snprintf(text, text_size, "Not connected to %s", host);
    }
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
        address[0] = '\0';
        return false;
    }
    address[strcspn(address, "\r\n")] = '\0';
    char host[CLIENT_HOST_SIZE];
    uint16_t port = 0;
    if (!protocol_link_parse(address, host, sizeof(host), &port)) {
        address[0] = '\0';  // Empty means no desktop chosen, see Phone.address.
        return false;
    }
    return true;
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

// Reads the pointer, in layout pixels, and returns the view to show. Closed, the drawer takes
// presses on the edge only; open, it takes them everywhere, so that a tap beside it or a drag
// to the left closes it. Back closes it too.
static View drawer_update(Drawer *drawer, View current) {
    assert(drawer->open_fraction >= 0.0f);
    assert(drawer->open_fraction <= 1.0f);
    const Vector2 pointer = GetMousePosition();
    if (drawer->open && IsKeyPressed(KEY_BACK)) {
        drawer->open = false;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (drawer->open || pointer.x < GUTTER_WIDTH_PIXELS) {
            drawer->pressed = true;
            drawer->dragging = false;
            drawer->drag_x = pointer.x;
            drawer->drag_fraction = drawer->open_fraction;
        }
    }

    View chosen = current;
    if (drawer->pressed && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float moved = pointer.x - drawer->drag_x;
        if (moved > DRAWER_DRAG_THRESHOLD_PIXELS || moved < -DRAWER_DRAG_THRESHOLD_PIXELS) {
            drawer->dragging = true;
        }
        if (drawer->dragging) {
            float fraction = drawer->drag_fraction + moved / DRAWER_WIDTH_PIXELS;
            if (fraction < 0.0f) {
                fraction = 0.0f;
            }
            if (fraction > 1.0f) {
                fraction = 1.0f;
            }
            drawer->open_fraction = fraction;
        }
    } else if (drawer->pressed) {
        // Released: a drag settles on the nearer side, and a tap acts where it landed.
        drawer->pressed = false;
        if (drawer->dragging) {
            drawer->open = drawer->open_fraction > 0.5f;
        } else {
            chosen = drawer_update_tap(drawer, current, pointer);
        }
        drawer->dragging = false;
    }

    if (!drawer->dragging) {
        float target = drawer->open ? 1.0f : 0.0f;
        float step = target - drawer->open_fraction;
        if (step > DRAWER_STEP) {
            step = DRAWER_STEP;
        }
        if (step < -DRAWER_STEP) {
            step = -DRAWER_STEP;
        }
        drawer->open_fraction += step;
    }
    assert(drawer->open_fraction >= 0.0f);
    assert(drawer->open_fraction <= 1.0f);
    return chosen;
}

// Closed, a tap on the edge opens the drawer. Open, a tap on an item shows its view and closes
// the drawer, a tap beside the drawer closes it, and a tap elsewhere on it does nothing.
static View drawer_update_tap(Drawer *drawer, View current, Vector2 pointer) {
    if (!drawer->open) {
        assert(!drawer->dragging);
        drawer->open = true;
        return current;
    }
    if (pointer.x >= DRAWER_WIDTH_PIXELS) {
        drawer->open = false;
        return current;
    }
    for (int32_t i = 0; i < VIEW_COUNT; i++) {
        if (CheckCollisionPointRec(pointer, drawer_item(i, 0.0f))) {
            drawer->open = false;
            return (View)i;
        }
    }
    return current;
}

// The view takes input only with the drawer wholly closed and nothing pressed on it.
static bool drawer_is_closed(const Drawer *drawer) {
    if (drawer->open || drawer->pressed) {
        return false;
    }
    return drawer->open_fraction == 0.0f;
}

// Where an item of the drawer is, with the drawer's left edge at x.
static Rectangle drawer_item(int32_t index, float x) {
    assert(index >= 0);
    assert(index < VIEW_COUNT);
    return (Rectangle){ x, DRAWER_ITEMS_TOP_PIXELS + (float)(index * DRAWER_ITEM_HEIGHT_PIXELS),
                        DRAWER_WIDTH_PIXELS - 1, DRAWER_ITEM_HEIGHT_PIXELS };
}

// The drawer slides in from the left over a darkened view. Its items look like the desktop's
// navigation; the connection is shown at the bottom.
static void drawer_draw(const Drawer *drawer, const App *app, View current, const char *status,
                        float width, float height) {
    assert(width > 0.0f);
    assert(height > 0.0f);
    if (drawer->open_fraction == 0.0f) {
        return;
    }
    const float x = (drawer->open_fraction - 1.0f) * DRAWER_WIDTH_PIXELS;
    const Color border = GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_NORMAL));
    DrawRectangleRec((Rectangle){ 0, 0, width, height },
                     Fade(BLACK, DRAWER_SCRIM_ALPHA * drawer->open_fraction));
    DrawRectangleRec((Rectangle){ x, 0, DRAWER_WIDTH_PIXELS, height },
                     GetColor((unsigned int)GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    DrawRectangleRec((Rectangle){ x + DRAWER_WIDTH_PIXELS - 1, 0, 1, height }, border);
    app_draw_text(app, APP_TEXT_HEADING, "Menu", x + 20, 24);

    for (int32_t i = 0; i < VIEW_COUNT; i++) {
        Rectangle item = drawer_item(i, x);
        if (i == (int32_t)current) {
            DrawRectangleRec(item, GetColor((unsigned int)GuiGetStyle(DEFAULT,
                                                                       BASE_COLOR_PRESSED)));
            // A bar on the left edge marks the open view, as in the desktop's navigation.
            DrawRectangleRec((Rectangle){ item.x, item.y, 4, item.height },
                             GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED)));
        }
        app_draw_text(app, APP_TEXT_NORMAL, view_names[i], item.x + 20, item.y + 16);
    }
    app_draw_text(app, APP_TEXT_DIM, "Desktop", x + 20, height - 64);
    app_draw_text(app, APP_TEXT_SMALL, status, x + 20, height - 40);
}

// The strip along the left edge, with a handle and an arrow pointing out of it, so that the
// navigation is seen to come from there.
static void gutter_draw(float height) {
    assert(height > 0.0f);
    const Color border = GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_NORMAL));
    const Color accent = GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED));
    const float middle = height / 2;
    DrawRectangleRec((Rectangle){ 0, 0, GUTTER_WIDTH_PIXELS, height },
                     GetColor((unsigned int)GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL)));
    DrawRectangleRec((Rectangle){ GUTTER_WIDTH_PIXELS - 1, 0, 1, height }, border);
    // The grip lines and arrow are centred in the strip.
    const float center = (GUTTER_WIDTH_PIXELS - 1) / 2.0f;
    DrawRectangleRounded((Rectangle){ center - 2, middle - 64, 4, 44 }, 1.0f, 4, border);
    DrawRectangleRounded((Rectangle){ center - 2, middle + 20, 4, 44 }, 1.0f, 4, border);
    DrawLineEx((Vector2){ center - 4, middle - 10 }, (Vector2){ center + 4, middle }, 3.0f,
               accent);
    DrawLineEx((Vector2){ center + 4, middle }, (Vector2){ center - 4, middle + 10 }, 3.0f,
               accent);
}

// The scan screen uses the calendar's layout pixels: drawing and touches are scaled the same
// way, as app_frame does for the calendar.
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

static void connect_screen_open(ConnectScreen *screen, const char *address) {
    assert(!screen->open);
    // Searching the network starts at once; without it, the code or the address still work.
    bool searching = discovery_start(&screen->discovery);
    (void)searching;
    snprintf(screen->address, sizeof(screen->address), "%s", address);
    screen->message[0] = '\0';
    screen->editing = false;
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

// Draws the connection settings right of the edge's strip, between the caller's BeginDrawing
// and EndDrawing. Without input, as while the drawer is out, the controls ignore touches.
static ConnectResult connect_screen_frame(ConnectScreen *screen, const App *app, float scale,
                                          const char *status, bool input) {
    assert(screen->open);
    assert(scale > 0.0f);
    discovery_poll(&screen->discovery);
    // Layout pixels start right of the strip: drawing and touches move and scale the same way.
    const float offset = GUTTER_WIDTH_PIXELS * scale;
    SetMouseOffset(-(int)offset, 0);
    SetMouseScale(1.0f / scale, 1.0f / scale);
    if (!input) {
        GuiLock();
    }
    BeginMode2D((Camera2D){ .offset = { offset, 0 }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
    const float width = ((float)GetScreenWidth() - offset) / scale;
    const float x = MARGIN_PIXELS;
    const float content_width = width - 2 * MARGIN_PIXELS;
    float y = 24;
    // A heading in the calendar's style, as the desktop's Connected Devices view has.
    app_draw_text(app, APP_TEXT_HEADING, "Connection settings", x, y);
    y += 44;
    GuiLabel((Rectangle){ x, y, content_width, LINE_PIXELS }, status);
    y += 2 * LINE_PIXELS;
    ConnectResult result = connect_screen_draw_choices(screen, width, &y);
    ConnectResult typed = connect_screen_draw_address(screen, width, y);
    EndMode2D();
    if (!input) {
        GuiUnlock();
    }
    SetMouseOffset(0, 0);
    SetMouseScale(1.0f, 1.0f);
    return result != CONNECT_NONE ? result : typed;
}

// The recommended way first, the QR code; then a desktop found on this network, if any.
static ConnectResult connect_screen_draw_choices(ConnectScreen *screen, float width, float *y) {
    const float x = MARGIN_PIXELS;
    const float content_width = width - 2 * MARGIN_PIXELS;
    ConnectResult result = CONNECT_NONE;
    GuiLabel((Rectangle){ x, *y, content_width, LINE_PIXELS },
             "Connect, or reconnect, to the desktop:");
    *y += LINE_PIXELS + 8;
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
    if (GuiButton((Rectangle){ x, y, content_width, 40 }, "Connect")) {
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
