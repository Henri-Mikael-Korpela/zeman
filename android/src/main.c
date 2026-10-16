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
#include "app.h"
#include "client.h"
#include "discovery.h"
#include "net.h"
#include "protocol.h"

enum {
    // The phone layout is this many layout pixels wide, whatever the screen's resolution.
    LAYOUT_WIDTH_PIXELS = 420,
    // "255.255.255.255:65535" with room to spare.
    ADDRESS_TEXT_SIZE = 32,
    PATH_SIZE = 512,
    MESSAGE_SIZE = 160,
    INFO_SIZE = 64,
};

typedef enum Screen {
    SCREEN_CONNECT,
    SCREEN_CALENDAR,
} Screen;

typedef enum ConnectResult {
    CONNECT_NONE,
    CONNECT_CHOSEN,     // screen->address holds the desktop to use
    CONNECT_CANCELLED,  // Back to the calendar with the desktop chosen before
} ConnectResult;

// The screen for choosing the desktop app: found on the local network, or typed in.
typedef struct ConnectScreen {
    Discovery discovery;
    char address[ADDRESS_TEXT_SIZE];
    char message[MESSAGE_SIZE];  // Why the typed address cannot be used, or empty
    bool editing;                // The address box has the keyboard
    bool can_cancel;             // A desktop was chosen before, so Back returns to it
    bool open;
} ConnectScreen;

typedef struct ServerAddress {
    char host[CLIENT_HOST_SIZE];
    uint16_t port;
} ServerAddress;

static void desktop_choose(Client *client, App *app, const char *address);
static bool address_parse(const char *text, ServerAddress *server);
static void settings_path(char *path, size_t path_size);
static bool settings_load(char *address, size_t address_size);
static void settings_save(const char *address);
static void connect_screen_open(ConnectScreen *screen, const char *address, bool can_cancel);
static void connect_screen_close(ConnectScreen *screen);
static ConnectResult connect_screen_frame(ConnectScreen *screen, float scale);
static ConnectResult connect_screen_draw(ConnectScreen *screen, float width);

// The Android app: the calendar of a desktop app on the same local network. raylib's Android
// platform code calls this from android_main.
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // Full screen, at the display's own resolution.
    InitWindow(0, 0, "Calendar");
    SetTargetFPS(60);
    static AndroidInput input;
    android_input_install(&input);

    int shorter_side = GetScreenWidth() < GetScreenHeight() ? GetScreenWidth() : GetScreenHeight();
    const float scale = (float)shorter_side / LAYOUT_WIDTH_PIXELS;
    // Static rather than on the stack: the client holds a large message buffer.
    static Client client;
    client_init(&client);
    Backend backend = client_backend(&client);
    AppConfig config = { .font_directory = "", .scale = scale, .touch = true,
                         .server_button = true };
    App *app = app_create(&backend, &config);

    static ConnectScreen connect;
    char address[ADDRESS_TEXT_SIZE] = "";
    ServerAddress saved;
    Screen screen = SCREEN_CONNECT;
    if (settings_load(address, sizeof(address)) && address_parse(address, &saved)) {
        desktop_choose(&client, app, address);
        screen = SCREEN_CALENDAR;
    } else {
        connect_screen_open(&connect, "", false);
    }

    // The one intentionally unbounded loop: it runs until Android closes the app.
    while (!WindowShouldClose()) {
        bool typing = false;
        if (screen == SCREEN_CONNECT) {
            ConnectResult result = connect_screen_frame(&connect, scale);
            typing = connect.editing;
            if (result == CONNECT_CHOSEN) {
                snprintf(address, sizeof(address), "%s", connect.address);
                desktop_choose(&client, app, address);
                settings_save(address);
            }
            if (result != CONNECT_NONE) {
                connect_screen_close(&connect);
                screen = SCREEN_CALENDAR;
            }
        } else {
            app_frame(app);
            typing = app_text_input_active(app);
            if (app_take_server_request(app)) {
                connect_screen_open(&connect, address, true);
                screen = SCREEN_CONNECT;
            }
        }
        android_input_keyboard_set_visible(&input, typing);
    }

    connect_screen_close(&connect);
    app_destroy(app);
    client_deinit(&client);
    CloseWindow();
    return 0;
}

static void desktop_choose(Client *client, App *app, const char *address) {
    ServerAddress server;
    bool parsed = address_parse(address, &server);
    assert(parsed);
    (void)parsed; // Unused when assertions are compiled out.
    client_set_server(client, server.host, server.port);
    char info[INFO_SIZE];
    snprintf(info, sizeof(info), "Desktop %s", server.host);
    app_set_info(app, info);
}

// "192.168.1.20" or "192.168.1.20:47800": a numeric address, and a port when not the default.
static bool address_parse(const char *text, ServerAddress *server) {
    *server = (ServerAddress){ .port = PROTOCOL_PORT_DEFAULT };
    const char *colon = strchr(text, ':');
    size_t host_length = colon != NULL ? (size_t)(colon - text) : strlen(text);
    if (host_length == 0 || host_length >= sizeof(server->host)) {
        return false;
    }
    memcpy(server->host, text, host_length);
    server->host[host_length] = '\0';
    if (colon != NULL) {
        char *end = NULL;
        long port = strtol(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0' || port < 1 || port > UINT16_MAX) {
            return false;
        }
        server->port = (uint16_t)port;
    }
    return true;
}

static void settings_path(char *path, size_t path_size) {
    int written = snprintf(path, path_size, "%s/desktop_address.txt",
                           android_input_data_directory());
    assert(written > 0 && (size_t)written < path_size);
}

static bool settings_load(char *address, size_t address_size) {
    char path[PATH_SIZE];
    settings_path(path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    bool ok = fgets(address, (int)address_size, file) != NULL;
    fclose(file);
    if (ok) {
        address[strcspn(address, "\r\n")] = '\0';
    }
    return ok && address[0] != '\0';
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

static void connect_screen_open(ConnectScreen *screen, const char *address, bool can_cancel) {
    assert(!screen->open);
    // Searching starts at once; when it cannot, the address can still be typed.
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
    bool found = discovery_poll(&screen->discovery);
    // On first start, the first desktop found is used at once, so that nothing has to be typed.
    if (found && !screen->can_cancel && !screen->editing) {
        Discovery *discovery = &screen->discovery;
        snprintf(screen->address, sizeof(screen->address), "%s:%u", discovery->found_address,
                 (unsigned)discovery->found_port);
        return CONNECT_CHOSEN;
    }
    if (screen->can_cancel && IsKeyPressed(KEY_BACK)) {
        return CONNECT_CANCELLED;
    }

    BeginDrawing();
    ClearBackground(GetColor((unsigned int)GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    // The same layout pixels as the calendar: app_create set the mouse scale to match.
    BeginMode2D((Camera2D){ .offset = { 0, 0 }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
    ConnectResult result = connect_screen_draw(screen, (float)GetScreenWidth() / scale);
    EndMode2D();
    EndDrawing();
    return result;
}

static ConnectResult connect_screen_draw(ConnectScreen *screen, float width) {
    const float x = 16;
    const float content_width = width - 2 * x;
    const float line = 28;
    float y = 32;
    GuiLabel((Rectangle){ x, y, content_width, line }, "Connect to the desktop calendar");
    y += 2 * line;

    const Discovery *discovery = &screen->discovery;
    ConnectResult result = CONNECT_NONE;
    char text[MESSAGE_SIZE];
    if (discovery->found) {
        snprintf(text, sizeof(text), "Found it at %s.", discovery->found_address);
        GuiLabel((Rectangle){ x, y, content_width - 110, line }, text);
        if (GuiButton((Rectangle){ x + content_width - 100, y - 4, 100, 36 }, "Use it")) {
            snprintf(screen->address, sizeof(screen->address), "%s:%u",
                     discovery->found_address, (unsigned)discovery->found_port);
            result = CONNECT_CHOSEN;
        }
    } else {
        GuiLabel((Rectangle){ x, y, content_width, line },
                 discovery->open ? "Looking for it on this network..." : discovery->error);
    }
    y += 2 * line;

    GuiLabel((Rectangle){ x, y, content_width, line }, "Or type the address from its");
    GuiLabel((Rectangle){ x, y + line, content_width, line }, "status bar, such as 192.168.1.20:");
    y += 2 * line + 8;
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
        ServerAddress server;
        screen->editing = false;
        if (!address_parse(screen->address, &server)) {
            snprintf(screen->message, sizeof(screen->message), "Type an address such as "
                                                               "192.168.1.20.");
        } else if (!net_address_is_local(server.host)) {
            snprintf(screen->message, sizeof(screen->message), "That is not a local address.");
        } else {
            result = CONNECT_CHOSEN;
        }
    }
    y += 56;
    GuiLabel((Rectangle){ x, y, content_width, line }, screen->message);
    y += line + 8;
    GuiLabel((Rectangle){ x, y, content_width, line }, "Both must be on the same Wi-Fi.");
    return result;
}
