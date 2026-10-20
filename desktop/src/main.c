#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raygui.h"
// Only to wait for input with a timeout and to see a close request, which raylib does not
// wrap. The GL headers are left out because raylib loads OpenGL itself.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <zeman/client.h>
#include <zeman/devices_view.h>
#include <zeman/local_backend.h>
#include <zeman/problems_view.h>

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
    // Room for the longest server error or client address, so that nothing is cut here.
    INFO_SIZE = 256,
    // Frames drawn after the last input, because input takes effect a frame late: a click sets
    // state in one frame that the next draws, and some of it, like events reloaded for a new
    // month, is drawn the frame after that.
    REDRAW_FRAMES_AFTER_INPUT = 3,
    // Every keyboard key code raylib tracks, from raylib's MAX_KEYBOARD_KEYS. Key 0 is no key.
    INPUT_KEY_COUNT = 512,
    // Every mouse button raylib tracks, from MOUSE_BUTTON_LEFT to MOUSE_BUTTON_BACK.
    INPUT_MOUSE_BUTTON_COUNT = MOUSE_BUTTON_BACK + 1,
};
static_assert((int32_t)INPUT_KEY_COUNT > (int32_t)KEY_KB_MENU,
              "Every raylib key code is checked for input.");

// How long an idle wait may sleep before the app polls phones and the backend again. Phones are
// then answered within 20 ms, near the 16 ms of a frame at 60 FPS, while a wake-up that finds
// nothing to do costs microseconds rather than a frame's drawing.
static const double IDLE_WAIT_SECONDS = 0.020;
// The longest the window goes without a frame while idle, so that text that changes with time
// alone, such as how long ago an item was created and which day is today, is never stale.
static const double REDRAW_INTERVAL_SECONDS = 1.0;

// Where the events are kept: the repository root, set by CMake (see desktop/CMakeLists.txt), so
// that deleting a build directory never deletes them. There is no fallback, because a guess
// would silently start a second, empty database.
#ifndef ZEMAN_DATABASE_PATH
#error "ZEMAN_DATABASE_PATH must be defined by the build."
#endif
static const char DATABASE_PATH[] = ZEMAN_DATABASE_PATH;
static_assert(sizeof(DATABASE_PATH) > 1, "The database path is not empty.");

typedef enum View {
    VIEW_CALENDAR,
    VIEW_PROBLEMS,
    VIEW_DEVICES,
    VIEW_COUNT,
} View;

// Navigation items, top to bottom, indexed by View.
static const char *const view_names[VIEW_COUNT] = { "Calendar", "Problems",
                                                    "Connected Devices" };

// What the last frame showed, to tell when the window needs a new one. Drawing every frame at
// 60 FPS kept a CPU core busy while the app sat idle; drawing only when something changed
// leaves it asleep between inputs.
typedef struct Redraw {
    double drawn_at_seconds;  // GetTime when the last frame was drawn
    uint64_t revision;        // Backend revision when the last frame was drawn
    int32_t client_count;     // Phones connected when the last frame was drawn
    int32_t frames_pending;   // Frames still to draw for the last input
} Redraw;

typedef struct Options {
    char connect_host[CLIENT_HOST_SIZE];  // Empty unless --connect was given
    uint16_t connect_port;
    uint16_t port;  // Where to serve phones, from --port
} Options;

// Everything the desktop app runs. The server and client hold large message buffers, so this
// lives in static storage rather than on the stack.
typedef struct Desktop {
    Local_Backend local;
    Server server;
    Client client;
    Devices_View devices;
    Problems_View problems;
    Backend backend;
    Redraw redraw;
    View view;
    bool client_mode;  // Without a database, server or navigation; see main
    bool store_open;
    char info[INFO_SIZE];
} Desktop;

static bool options_parse(int argc, char **argv, Options *options);
static bool port_parse(const char *text, uint16_t *port);
static void desktop_open(Desktop *desktop, const Options *options);
static void desktop_close(Desktop *desktop);
static bool desktop_redraw_needed(Desktop *desktop);
static void desktop_wait(void);
static void desktop_frame(Desktop *desktop, App *app);
static bool input_is_active(void);
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
        fprintf(stderr, "Usage: zeman [--port PORT | --connect ADDRESS[:PORT]]\n");
        return 1;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS,
               options.connect_host[0] != '\0' ? "Zeman (client)" : "Zeman");
    SetWindowMinSize(WINDOW_WIDTH_PIXELS_MIN, WINDOW_HEIGHT_PIXELS_MIN);
    SetTargetFPS(60);
    // Escape cancels the event form; closing the window stays with the window controls.
    SetExitKey(KEY_NULL);

    static Desktop desktop;
    desktop_open(&desktop, &options);
    App_Config config = {
        .font_directory = GetApplicationDirectory(),
        .scale = 1.0f,
        .touch = false,
    };
    App *app = app_create(&desktop.backend, &config);
    app_set_info(app, desktop.info);
    if (!desktop.store_open) {
        app_set_status(app, desktop.local.error);
    }

    // The one intentionally unbounded loop: it runs until the window is closed. Phones are
    // served on every pass, drawn or not, so that they never wait for the window.
    while (!WindowShouldClose()) {
        if (!desktop.client_mode) {
            server_poll(&desktop.server);
        }
        if (desktop_redraw_needed(&desktop)) {
            desktop_frame(&desktop, app);
        } else {
            desktop_wait();
        }
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
    // Draws the first frame at once, as nothing is on screen yet.
    desktop->redraw = (Redraw){ .frames_pending = 1 };
    problems_view_init(&desktop->problems);
    if (desktop->client_mode) {
        client_init(&desktop->client);
        client_set_server(&desktop->client, options->connect_host, options->connect_port);
        desktop->backend = client_backend(&desktop->client);
        snprintf(desktop->info, sizeof(desktop->info), "Client of %s:%u", options->connect_host,
                 (unsigned)options->connect_port);
        return;
    }
    desktop->store_open = local_backend_open(&desktop->local, DATABASE_PATH);
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
    assert(desktop->redraw.frames_pending >= 0);
    if (!desktop->client_mode) {
        info_update(&desktop->server, app);
        desktop->redraw.client_count = server_client_count(&desktop->server);
    }
    // Taken before drawing, so that a change during the frame is drawn by the next one.
    desktop->redraw.revision = backend_revision(&desktop->backend);
    desktop->redraw.drawn_at_seconds = GetTime();
    if (desktop->redraw.frames_pending > 0) {
        desktop->redraw.frames_pending--;
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
    case VIEW_PROBLEMS:
        problems_view_draw(&desktop->problems, &desktop->backend, app, content);
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

// Whether something changed since the last frame: input, data from this app or a phone, the
// phones connected, or the time. The window keeps showing the last frame until then.
static bool desktop_redraw_needed(Desktop *desktop) {
    Redraw *redraw = &desktop->redraw;
    assert(redraw->frames_pending >= 0);
    assert(redraw->frames_pending <= REDRAW_FRAMES_AFTER_INPUT);
    if (input_is_active()) {
        redraw->frames_pending = REDRAW_FRAMES_AFTER_INPUT;
    }
    if (redraw->frames_pending > 0) {
        return true;
    }
    // raylib learns of a close request only in the input poll at the end of a frame, so one
    // more frame lets the main loop see it. The window is GLFW's current context, raylib's only
    // window; GetWindowHandle would not do, as on Windows it returns the HWND instead.
    GLFWwindow *window = glfwGetCurrentContext();
    assert(window != NULL);
    if (glfwWindowShouldClose(window)) {
        return true;
    }
    if (GetTime() - redraw->drawn_at_seconds >= REDRAW_INTERVAL_SECONDS) {
        return true;
    }
    // The client backend asks the desktop app for its revision at most every so often, so asking
    // on every idle pass costs no more network than drawing every frame did.
    if (backend_revision(&desktop->backend) != redraw->revision) {
        return true;
    }
    if (desktop->client_mode) {
        return false;
    }
    return server_client_count(&desktop->server) != redraw->client_count;
}

// Sleeps until input arrives or IDLE_WAIT_SECONDS pass. raylib's own EnableEventWaiting has no
// timeout, which would leave phones unanswered until the user moved the mouse. GLFW's callbacks
// record the input into raylib's state on top of the last frame's poll, so the input functions
// see it as at the start of a frame, and input_is_active finds it.
static void desktop_wait(void) {
    assert(IsWindowReady());
    assert(IDLE_WAIT_SECONDS > 0.0);
    glfwWaitEventsTimeout(IDLE_WAIT_SECONDS);
}

// Whether the user did anything since the last input poll, or still holds a key or button, as
// in a drag. A held key keeps frames coming, which raygui's key repeat in text boxes needs.
static bool input_is_active(void) {
    if (IsWindowResized()) {
        return true;
    }
    const Vector2 mouse_delta = GetMouseDelta();
    if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f) {
        return true;
    }
    const Vector2 wheel_move = GetMouseWheelMoveV();
    if (wheel_move.x != 0.0f || wheel_move.y != 0.0f) {
        return true;
    }
    for (int32_t button = 0; button < INPUT_MOUSE_BUTTON_COUNT; button++) {
        if (IsMouseButtonDown(button) || IsMouseButtonReleased(button)) {
            return true;
        }
    }
    // A press shows as down; a release is checked on its own, as nothing is down after it.
    for (int32_t key = 1; key < INPUT_KEY_COUNT; key++) {
        if (IsKeyDown(key) || IsKeyReleased(key)) {
            return true;
        }
    }
    return false;
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
