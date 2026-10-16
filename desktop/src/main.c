#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "raygui.h"

#include "app.h"
#include "backend.h"
#include "client.h"
#include "devices_view.h"
#include "local_backend.h"
#include "protocol.h"
#include "server.h"

enum {
    WINDOW_WIDTH_PIXELS = 1520,
    WINDOW_HEIGHT_PIXELS = 820,
    // Below this the grid cells get too small for an event bar and the panel too narrow for
    // its text, next to the navigation.
    WINDOW_WIDTH_PIXELS_MIN = 1200,
    // Tall enough for every row of the event form and its buttons.
    WINDOW_HEIGHT_PIXELS_MIN = 720,
    NAVIGATION_WIDTH_PIXELS = 200,
    NAVIGATION_ITEM_HEIGHT_PIXELS = 44,
    PATH_SIZE = 1024,
    // Room for the longest server error or client address, so that nothing is cut here.
    INFO_SIZE = 256,
};

typedef enum View {
    VIEW_CALENDAR,
    VIEW_DEVICES,
    VIEW_COUNT,
} View;

// Navigation items, top to bottom, indexed by View.
static const char *const view_names[VIEW_COUNT] = { "Calendar", "Connected Devices" };

typedef struct Options {
    char connect_host[CLIENT_HOST_SIZE];  // Empty unless --connect was given
    uint16_t connect_port;
    uint16_t port;  // Where to serve phones, from --port
} Options;

// Everything the desktop app runs. The server and client hold large message buffers, so this
// lives in static storage rather than on the stack.
typedef struct Desktop {
    LocalBackend local;
    Server server;
    Client client;
    DevicesView devices;
    Backend backend;
    View view;
    bool client_mode;  // Without a database, server or navigation; see main
    bool store_open;
    char info[INFO_SIZE];
} Desktop;

static bool options_parse(int argc, char **argv, Options *options);
static bool port_parse(const char *text, uint16_t *port);
static void desktop_open(Desktop *desktop, const Options *options);
static void desktop_close(Desktop *desktop);
static void desktop_frame(Desktop *desktop, App *app);
static View navigation_draw(const App *app, View current, Rectangle area);
static void info_update(const Server *server, App *app);

// The desktop app keeps the calendar in its SQLite database and serves it to phones on the
// local network, on --port PORT or the default. With --connect ADDRESS[:PORT] it instead works
// as a client of another desktop app, exactly as the phone does, which tests the network path
// on one computer.
int main(int argc, char **argv) {
    Options options = { .connect_host = "", .connect_port = PROTOCOL_PORT_DEFAULT,
                        .port = PROTOCOL_PORT_DEFAULT };
    if (!options_parse(argc, argv, &options)) {
        fprintf(stderr, "Usage: calendar [--port PORT | --connect ADDRESS[:PORT]]\n");
        return 1;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS,
               options.connect_host[0] != '\0' ? "Calendar (client)" : "Calendar");
    SetWindowMinSize(WINDOW_WIDTH_PIXELS_MIN, WINDOW_HEIGHT_PIXELS_MIN);
    SetTargetFPS(60);
    // Escape cancels the event form; closing the window stays with the window controls.
    SetExitKey(KEY_NULL);

    static Desktop desktop;
    desktop_open(&desktop, &options);
    AppConfig config = {
        .font_directory = GetApplicationDirectory(),
        .scale = 1.0f,
        .touch = false,
        .server_button = false,
    };
    App *app = app_create(&desktop.backend, &config);
    app_set_info(app, desktop.info);
    if (!desktop.store_open) {
        app_set_status(app, desktop.local.error);
    }

    // The one intentionally unbounded loop: it runs until the window is closed.
    while (!WindowShouldClose()) {
        desktop_frame(&desktop, app);
    }

    app_destroy(app);
    desktop_close(&desktop);
    CloseWindow();
    return 0;
}

static void desktop_open(Desktop *desktop, const Options *options) {
    desktop->client_mode = options->connect_host[0] != '\0';
    desktop->store_open = true;
    desktop->view = VIEW_CALENDAR;
    desktop->info[0] = '\0';
    if (desktop->client_mode) {
        client_init(&desktop->client);
        client_set_server(&desktop->client, options->connect_host, options->connect_port);
        desktop->backend = client_backend(&desktop->client);
        snprintf(desktop->info, sizeof(desktop->info), "Client of %s:%u", options->connect_host,
                 (unsigned)options->connect_port);
        return;
    }
    // Beside the executable: the app has no settings or installer, so this is the one place
    // that is both writable and predictable.
    char path[PATH_SIZE];
    int written = snprintf(path, sizeof(path), "%scalendar.db", GetApplicationDirectory());
    assert(written > 0 && written < (int)sizeof(path));
    desktop->store_open = local_backend_open(&desktop->local, path);
    desktop->backend = local_backend(&desktop->local);
    if (!server_start(&desktop->server, &desktop->backend, options->port)) {
        snprintf(desktop->info, sizeof(desktop->info), "Phone access off: %s",
                 desktop->server.error);
    }
    devices_view_init(&desktop->devices, &desktop->server);
}

static void desktop_close(Desktop *desktop) {
    if (desktop->client_mode) {
        client_deinit(&desktop->client);
        return;
    }
    server_stop(&desktop->server);
    local_backend_close(&desktop->local);
}

// The navigation on the left, and the chosen view beside it. A client has no server, so it
// shows the calendar alone.
static void desktop_frame(Desktop *desktop, App *app) {
    if (!desktop->client_mode) {
        server_poll(&desktop->server);
        info_update(&desktop->server, app);
    }
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    BeginDrawing();
    ClearBackground(GetColor((unsigned int)GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    Rectangle content = { 0, 0, width, height };
    if (!desktop->client_mode) {
        Rectangle navigation = { 0, 0, NAVIGATION_WIDTH_PIXELS, height };
        desktop->view = navigation_draw(app, desktop->view, navigation);
        content = (Rectangle){ NAVIGATION_WIDTH_PIXELS, 0, width - NAVIGATION_WIDTH_PIXELS,
                               height };
    }
    switch (desktop->view) {
    case VIEW_CALENDAR:
        app_frame(app, content);
        break;
    case VIEW_DEVICES:
        devices_view_draw(&desktop->devices, &desktop->server, app, content);
        break;
    case VIEW_COUNT:
        assert(false); // Not a view.
        break;
    }
    EndDrawing();
}

// Draws the items and returns the view to show: the one clicked this frame, or current.
static View navigation_draw(const App *app, View current, Rectangle area) {
    DrawRectangleRec(area, GetColor((unsigned int)GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL)));
    DrawRectangleRec((Rectangle){ area.x + area.width - 1, area.y, 1, area.height },
                     GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_NORMAL)));
    const Vector2 mouse = GetMousePosition();
    View chosen = current;
    for (int32_t i = 0; i < VIEW_COUNT; i++) {
        Rectangle item = { area.x, area.y + 16 + (float)(i * NAVIGATION_ITEM_HEIGHT_PIXELS),
                           area.width - 1, NAVIGATION_ITEM_HEIGHT_PIXELS };
        bool hovered = CheckCollisionPointRec(mouse, item);
        if (i == (int32_t)current) {
            DrawRectangleRec(item, GetColor((unsigned int)GuiGetStyle(DEFAULT,
                                                                       BASE_COLOR_PRESSED)));
            // A bar on the left edge marks the open view, as in most desktop apps.
            DrawRectangleRec((Rectangle){ item.x, item.y, 4, item.height },
                             GetColor((unsigned int)GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED)));
        } else if (hovered) {
            DrawRectangleRec(item, GetColor((unsigned int)GuiGetStyle(DEFAULT,
                                                                       BASE_COLOR_FOCUSED)));
        }
        app_draw_text(app, APP_TEXT_NORMAL, view_names[i], item.x + 20, item.y + 12);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            chosen = (View)i;
        }
    }
    return chosen;
}

static bool options_parse(int argc, char **argv, Options *options) {
    if (argc == 1) {
        return true;
    }
    if (argc != 3) {
        return false;
    }
    if (strcmp(argv[1], "--port") == 0) {
        return port_parse(argv[2], &options->port);
    }
    if (strcmp(argv[1], "--connect") != 0) {
        return false;
    }
    snprintf(options->connect_host, sizeof(options->connect_host), "%s", argv[2]);
    char *colon = strchr(options->connect_host, ':');
    if (colon == NULL) {
        return options->connect_host[0] != '\0';
    }
    *colon = '\0';
    return port_parse(colon + 1, &options->connect_port) && options->connect_host[0] != '\0';
}

static bool port_parse(const char *text, uint16_t *port) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 1 || value > UINT16_MAX) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}

// Tells the user where phones connect, and how many are connected.
static void info_update(const Server *server, App *app) {
    if (!server->listening) {
        return;
    }
    char info[INFO_SIZE];
    int32_t count = server_client_count(server);
    if (count == 0) {
        snprintf(info, sizeof(info), "Phone access: %s", server->address);
    } else {
        snprintf(info, sizeof(info), "Phone access: %s, %d connected", server->address,
                 (int)count);
    }
    app_set_info(app, info);
}
