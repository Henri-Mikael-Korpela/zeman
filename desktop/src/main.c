#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "app.h"
#include "backend.h"
#include "client.h"
#include "local_backend.h"
#include "protocol.h"
#include "server.h"

enum {
    WINDOW_WIDTH_PIXELS = 1320,
    WINDOW_HEIGHT_PIXELS = 820,
    // Below this the grid cells get too small for an event bar and the panel too narrow for
    // its text.
    WINDOW_WIDTH_PIXELS_MIN = 1000,
    // Tall enough for every row of the event form and its buttons.
    WINDOW_HEIGHT_PIXELS_MIN = 720,
    PATH_SIZE = 1024,
    // Room for the longest server error or client address, so that nothing is cut here.
    INFO_SIZE = 256,
};

typedef struct Options {
    char connect_host[CLIENT_HOST_SIZE];  // Empty unless --connect was given
    uint16_t connect_port;
} Options;

static bool options_parse(int argc, char **argv, Options *options);
static void info_update(const Server *server, App *app);

// The desktop app keeps the calendar in its SQLite database and serves it to phones on the
// local network. With --connect ADDRESS[:PORT] it instead works as a client of another desktop
// app, exactly as the phone does, which tests the network path on one computer.
int main(int argc, char **argv) {
    Options options = { .connect_host = "", .connect_port = PROTOCOL_PORT_DEFAULT };
    if (!options_parse(argc, argv, &options)) {
        fprintf(stderr, "Usage: calendar [--connect ADDRESS[:PORT]]\n");
        return 1;
    }
    const bool client_mode = options.connect_host[0] != '\0';

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS,
               client_mode ? "Calendar (client)" : "Calendar");
    SetWindowMinSize(WINDOW_WIDTH_PIXELS_MIN, WINDOW_HEIGHT_PIXELS_MIN);
    SetTargetFPS(60);
    // Escape cancels the event form; closing the window stays with the window controls.
    SetExitKey(KEY_NULL);

    // Static rather than on the stack: the server and client hold large message buffers.
    static LocalBackend local;
    static Server server;
    static Client client;
    Backend backend;
    char info[INFO_SIZE] = "";
    bool store_open = true;
    if (client_mode) {
        client_init(&client);
        client_set_server(&client, options.connect_host, options.connect_port);
        backend = client_backend(&client);
        snprintf(info, sizeof(info), "Client of %s:%u", options.connect_host,
                 (unsigned)options.connect_port);
    } else {
        // Beside the executable: the app has no settings or installer, so this is the one place
        // that is both writable and predictable.
        char path[PATH_SIZE];
        int written = snprintf(path, sizeof(path), "%scalendar.db", GetApplicationDirectory());
        assert(written > 0 && written < (int)sizeof(path));
        store_open = local_backend_open(&local, path);
        backend = local_backend(&local);
        if (!server_start(&server, &backend, PROTOCOL_PORT_DEFAULT)) {
            snprintf(info, sizeof(info), "Phone access off: %s", server.error);
        }
    }

    AppConfig config = {
        .font_directory = GetApplicationDirectory(),
        .scale = 1.0f,
        .touch = false,
        .server_button = false,
    };
    App *app = app_create(&backend, &config);
    app_set_info(app, info);
    if (!store_open) {
        app_set_status(app, local.error);
    }

    // The one intentionally unbounded loop: it runs until the window is closed.
    while (!WindowShouldClose()) {
        if (!client_mode) {
            server_poll(&server);
            info_update(&server, app);
        }
        app_frame(app);
    }

    app_destroy(app);
    if (client_mode) {
        client_deinit(&client);
    } else {
        server_stop(&server);
        local_backend_close(&local);
    }
    CloseWindow();
    return 0;
}

static bool options_parse(int argc, char **argv, Options *options) {
    if (argc == 1) {
        return true;
    }
    if (argc != 3 || strcmp(argv[1], "--connect") != 0) {
        return false;
    }
    snprintf(options->connect_host, sizeof(options->connect_host), "%s", argv[2]);
    char *colon = strchr(options->connect_host, ':');
    if (colon == NULL) {
        return options->connect_host[0] != '\0';
    }
    *colon = '\0';
    char *end = NULL;
    long port = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || *end != '\0' || port < 1 || port > UINT16_MAX) {
        return false;
    }
    options->connect_port = (uint16_t)port;
    return options->connect_host[0] != '\0';
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
