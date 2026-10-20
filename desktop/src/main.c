#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raygui.h"
#include "rlgl.h"
// Only to wait for input with a timeout and to see a close request, which raylib does not
// wrap. The GL headers are left out because raylib loads OpenGL itself.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <zeman/client.h>
#include <zeman/devices_view.h>
#include <zeman/local_backend.h>
#include <zeman/problems_view.h>
#include <zeman/settings.h>
#include <zeman/settings_view.h>

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

// ZEMAN_DATABASE_NEXT_TO_EXECUTABLE (see desktop/CMakeLists.txt, the mingw-package preset) means
// a packaged release: the database goes beside the executable at run time (database_path), so
// that the whole release folder stays self-contained wherever it is copied to. Otherwise this is
// a development build, and the database lives at the fixed, compile-time path below, the
// repository root, so that deleting a build directory (which is routine) never deletes it.
#ifndef ZEMAN_DATABASE_NEXT_TO_EXECUTABLE
#ifndef ZEMAN_DATABASE_PATH
#error "ZEMAN_DATABASE_PATH must be defined by the build."
#endif
static const char DATABASE_PATH[] = ZEMAN_DATABASE_PATH;
static_assert(sizeof(DATABASE_PATH) > 1, "The database path is not empty.");
static_assert(sizeof(DATABASE_PATH) <= SETTINGS_PATH_SIZE, "The path fits the Settings view.");
#endif

typedef enum View {
    VIEW_CALENDAR,
    VIEW_PROBLEMS,
    VIEW_DEVICES,
    VIEW_SETTINGS,
    VIEW_COUNT,
} View;

// Navigation items, top to bottom, indexed by View.
static const char *const view_names[VIEW_COUNT] = { "Calendar", "Problems",
                                                    "Connected Devices", "Settings" };

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
    Settings_View settings_view;
    Settings settings;
    Backend backend;
    Redraw redraw;
    View view;
    bool client_mode;  // Without a database, server or navigation; see main
    char info[INFO_SIZE];
    char database_path[SETTINGS_PATH_SIZE];  // The database in use, or empty when none is open
} Desktop;

// What desktop_window_refresh needs to draw, as GLFW hands a callback only the window.
typedef struct Desktop_Frame {
    Desktop *desktop;
    App *app;
} Desktop_Frame;

static bool options_parse(int argc, char **argv, Options *options);
static bool port_parse(const char *text, uint16_t *port);
static void desktop_open(Desktop *desktop, const Options *options);
static void desktop_close(Desktop *desktop);
static void desktop_database_open(Desktop *desktop);
static void desktop_database_apply(Desktop *desktop, App *app);
static const char *database_path(char *buffer, int32_t buffer_size);
static bool desktop_redraw_needed(Desktop *desktop);
static void desktop_wait(void);
static void desktop_frame(Desktop *desktop, App *app);
static void desktop_draw(Desktop *desktop, App *app, float width, float height);
static void desktop_window_refresh(GLFWwindow *window);
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
    if (!desktop.client_mode && !desktop.local.store_open) {
        app_set_status(app, desktop.settings_view.message);
    }

    // Windows runs its own loop while the user drags a window edge, in which the main loop below
    // does not run, so without this the new area stays black until the mouse is released. GLFW
    // calls the refresh callback from that loop, which draws the app at the new size.
    Desktop_Frame frame = { .desktop = &desktop, .app = app };
    GLFWwindow *window = glfwGetCurrentContext();
    assert(window != NULL);
    glfwSetWindowUserPointer(window, &frame);
    glfwSetWindowRefreshCallback(window, desktop_window_refresh);

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
    local_backend_init(&desktop->local);
    desktop->backend = local_backend(&desktop->local);
    desktop_database_open(desktop);
    if (!server_start(&desktop->server, &desktop->backend, options->port)) {
        snprintf(desktop->info, sizeof(desktop->info), "Phone access off: %s",
                 desktop->server.error);
    }
    devices_view_init(&desktop->devices, &desktop->server);
}

// Opens the database that settings.ini names, or the build's default when it names none. A
// named file must exist already, while the default is created on the first run, as it was
// before there were settings. On failure no database is open, rather than the default: the
// user chose another one, and events added to the default by mistake would seem lost to them.
static void desktop_database_open(Desktop *desktop) {
    Settings_View *view = &desktop->settings_view;
    desktop->database_path[0] = '\0';
    char message[SETTINGS_VIEW_MESSAGE_SIZE];
    if (!settings_load(&desktop->settings, GetApplicationDirectory())) {
        settings_view_init(view, "");
        snprintf(message, sizeof(message), "No database opened: %s", desktop->settings.error);
        settings_view_set_message(view, message);
        return;
    }
    const bool configured = desktop->settings.database_path[0] != '\0';
    char default_path[SETTINGS_PATH_SIZE];
    const char *path = configured ? desktop->settings.database_path
                                  : database_path(default_path, sizeof(default_path));
    settings_view_init(view, path);
    if (!local_backend_switch(&desktop->local, path, !configured)) {
        snprintf(message, sizeof(message), "No database opened: %s", desktop->local.error);
        settings_view_set_message(view, message);
        return;
    }
    snprintf(desktop->database_path, sizeof(desktop->database_path), "%s", path);
}

// Switches to the database at the path in the Settings view and saves it as the setting. When
// the new database cannot be opened, the one in use stays open, so the calendar keeps working.
static void desktop_database_apply(Desktop *desktop, App *app) {
    Settings_View *view = &desktop->settings_view;
    if (view->path[0] == '\0') {
        settings_view_set_message(view, "Enter the path of a database file.");
        return;
    }
    char message[SETTINGS_VIEW_MESSAGE_SIZE];
    if (!local_backend_switch(&desktop->local, view->path, false)) {
        snprintf(message, sizeof(message), "Not changed: %s", desktop->local.error);
        settings_view_set_message(view, message);
        return;
    }
    assert(desktop->local.store_open);
    snprintf(desktop->database_path, sizeof(desktop->database_path), "%s", view->path);
    // The calendar and problems reload on the new revision, but the open form and the focused
    // item hold ids of the old database, which would name other items in the new one.
    app_discard_edits(app);
    app_set_status(app, "");
    snprintf(desktop->settings.database_path, sizeof(desktop->settings.database_path), "%s",
             view->path);
    if (!settings_save(&desktop->settings, GetApplicationDirectory())) {
        snprintf(message, sizeof(message),
                 "Switched, but the next start will not remember it: %s",
                 desktop->settings.error);
        settings_view_set_message(view, message);
        return;
    }
    settings_view_set_message(view, "Switched to this database.");
}

// Returns the database's path: the fixed, compile-time path in a development build, or, in a
// packaged release, buffer, filled with the running executable's own directory plus "zeman.db".
// That directory is unknown until the executable runs there, since a packaged release can be
// copied anywhere, so that case cannot be a compile-time constant the way the dev path is.
static const char *database_path(char *buffer, int32_t buffer_size) {
    assert(buffer != NULL);
    assert(buffer_size > 0);
#ifdef ZEMAN_DATABASE_NEXT_TO_EXECUTABLE
    // GetApplicationDirectory() ends with a path separator already, as it does for fonts above.
    int written = snprintf(buffer, (size_t)buffer_size, "%szeman.db", GetApplicationDirectory());
    assert(written > 0);
    assert(written < buffer_size);
    return buffer;
#else
    (void)buffer;
    (void)buffer_size;
    return DATABASE_PATH;
#endif
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
    BeginDrawing();
    desktop_draw(desktop, app, (float)GetScreenWidth(), (float)GetScreenHeight());
    EndDrawing();
}

// Draws the frame between BeginDrawing and the end of drawing, which the caller does, as
// EndDrawing also polls input and waits for the next frame, neither wanted while resizing.
static void desktop_draw(Desktop *desktop, App *app, float width, float height) {
    assert(width > 0.0f);
    assert(height > 0.0f);
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
    case VIEW_SETTINGS:
        if (settings_view_draw(&desktop->settings_view, app, desktop->database_path,
                               GetApplicationDirectory(), content)) {
            desktop_database_apply(desktop, app);
        }
        break;
    case VIEW_COUNT:
        assert(false); // Not a view.
        break;
    }
}

// Draws and shows a frame at the window's current size, from inside the resize loop. Not
// EndDrawing: it would poll events again from within an event callback and sleep to hold the
// frame rate, so only the part that puts the picture on screen is done.
static void desktop_window_refresh(GLFWwindow *window) {
    Desktop_Frame *frame = glfwGetWindowUserPointer(window);
    assert(frame != NULL);
    BeginDrawing();
    desktop_draw(frame->desktop, frame->app, (float)GetScreenWidth(),
                 (float)GetScreenHeight());
    rlDrawRenderBatchActive();
    SwapScreenBuffer();
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
