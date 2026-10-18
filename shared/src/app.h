#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "raylib.h"

#include "backend.h"

// The calendar's user interface, shared by the desktop and Android apps. Each app opens its own
// window and backend, then calls app_frame once per frame.

typedef struct App_Config {
    // Prefix for the "fonts/..." files: the executable's folder on the desktop, and "" on
    // Android, where raylib reads relative paths from the APK's assets.
    const char *font_directory;
    // Screen pixels per layout pixel. 1 on the desktop; on a phone, large enough that the
    // portrait layout is 420 layout pixels wide.
    float scale;
    // Touch screen: swipes change month and the keyboard hints are left out.
    bool touch;
} App_Config;

typedef struct App App;

enum {
    // One entry in the calendar's day cells, and in other views that list entries the same way.
    APP_BAR_HEIGHT_PIXELS = 19,
    APP_BAR_GAP_PIXELS = 2,
};

// Text styles for screens drawn outside the calendar, such as the desktop's navigation, so that
// they look the same as the calendar.
typedef enum App_Text {
    APP_TEXT_HEADING,  // Large, medium weight
    APP_TEXT_NORMAL,
    APP_TEXT_DIM,      // Normal size, dimmed, for explanations
    APP_TEXT_SMALL,
} App_Text;

// Creates the one App. Needs the window, because fonts are loaded into textures.
App *app_create(Backend *backend, const App_Config *config);
void app_destroy(App *app);
// Handles input and draws the calendar into area, in screen pixels, between the caller's
// BeginDrawing and EndDrawing. Mouse positions are moved into the area while it runs and left
// unscaled and unmoved after, for whatever the caller draws next.
void app_frame(App *app, Rectangle area);
// Draws text in one of the calendar's styles, at a position in screen pixels divided by the
// scale (layout pixels), outside app_frame.
void app_draw_text(const App *app, App_Text style, const char *text, float x, float y);
float app_measure_text(const App *app, App_Text style, const char *text);
// Draws a problem as the calendar does in its day cells, in layout pixels outside app_frame:
// red with a "!" while open, green with a check mark once done.
void app_draw_problem_bar(const App *app, const char *title, bool done, Rectangle bar);
// Shown first in the status bar for as long as the app runs, for example where phones connect.
void app_set_info(App *app, const char *info);
// Shown in the status bar until the user moves to another day.
void app_set_status(App *app, const char *status);
// True while a form field is being typed into, so that Android can show its keyboard.
bool app_text_input_active(const App *app);
// With input off, app_frame still draws and loads but ignores the pointer and keys, for example
// while the phone's navigation drawer covers the calendar. On at start.
void app_set_input_enabled(App *app, bool enabled);

#endif
