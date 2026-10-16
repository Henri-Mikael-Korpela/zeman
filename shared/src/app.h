#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "backend.h"

// The calendar's user interface, shared by the desktop and Android apps. Each app opens its own
// window and backend, then calls app_frame once per frame.

typedef struct AppConfig {
    // Prefix for the "fonts/..." files: the executable's folder on the desktop, and "" on
    // Android, where raylib reads relative paths from the APK's assets.
    const char *font_directory;
    // Screen pixels per layout pixel. 1 on the desktop; on a phone, large enough that the
    // portrait layout is 420 layout pixels wide.
    float scale;
    // Touch screen: swipes change month and the keyboard hints are left out.
    bool touch;
    // Shows a "Server" button in the status bar, for the Android app's connection screen.
    bool server_button;
} AppConfig;

typedef struct App App;

// Creates the one App. Needs the window, because fonts are loaded into textures.
App *app_create(Backend *backend, const AppConfig *config);
void app_destroy(App *app);
// Handles input and draws one frame, including BeginDrawing and EndDrawing.
void app_frame(App *app);
// Shown first in the status bar for as long as the app runs, for example where phones connect.
void app_set_info(App *app, const char *info);
// Shown in the status bar until the user moves to another day.
void app_set_status(App *app, const char *status);
// True while a form field is being typed into, so that Android can show its keyboard.
bool app_text_input_active(const App *app);
// True once after each press of the "Server" button.
bool app_take_server_request(App *app);

#endif
