#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "raygui.h"

#include "calendar.h"
#include "event_store.h"

enum {
    WINDOW_WIDTH_PIXELS = 1320,
    WINDOW_HEIGHT_PIXELS = 820,
    // Below this the grid cells get too small for an event bar and the panel too narrow for
    // its text.
    WINDOW_WIDTH_PIXELS_MIN = 1000,
    WINDOW_HEIGHT_PIXELS_MIN = 640,
    // Each size is rasterized once at startup (see fonts_load), so that text is drawn at the
    // size its glyphs were made for. Calendar entries and cards use the small size, so that
    // more of each title fits.
    TEXT_SIZE_PIXELS = 18,
    TEXT_SIZE_PIXELS_SMALL = 15,
    TEXT_SIZE_PIXELS_HEADING = 28,
    TEXT_LINE_HEIGHT_PIXELS = 24,
    TEXT_LINE_HEIGHT_PIXELS_SMALL = 20,
    PADDING_PIXELS = 16,
    HEADER_HEIGHT_PIXELS = 40,
    WEEKDAY_HEIGHT_PIXELS = 32,
    STATUS_HEIGHT_PIXELS = 28,
    PANEL_WIDTH_PIXELS = 400,
    BUTTON_HEIGHT_PIXELS = 36,
    FIELD_HEIGHT_PIXELS = 32,
    GRID_COLUMN_COUNT = 7,
    // Six weeks always cover a whole month, whatever weekday it starts on.
    GRID_ROW_COUNT = 6,
    GRID_CELL_COUNT = GRID_COLUMN_COUNT * GRID_ROW_COUNT,
    // Day number and creation markers share the top line of a cell; bars start below it.
    CELL_TOP_LINE_PIXELS = 32,
    EVENT_BAR_HEIGHT_PIXELS = 19,
    EVENT_BAR_GAP_PIXELS = 2,
    // Four small lines, with 8 pixels above and below.
    EVENT_CARD_HEIGHT_PIXELS = 8 + 4 * TEXT_LINE_HEIGHT_PIXELS_SMALL + 8,
    EVENT_CARD_GAP_PIXELS = 8,
    CREATION_MARKER_RADIUS_PIXELS = 5,
    // Per cell. Past this a "+" is drawn, because more dots would run into the day number.
    CREATION_MARKER_COUNT_MAX = 5,
    LEGEND_HEIGHT_PIXELS = 56,
    // Events loaded for one six-week view. If more match, the status bar says so.
    EVENT_COUNT_MAX = 512,
    // Task completions loaded for one view, including a year before it (see
    // event_store_load_completions). More than this is reported like too many events.
    COMPLETION_COUNT_MAX = 1024,
    STATUS_SIZE = 192,
    TEXT_LINE_SIZE = 128,
    DATABASE_PATH_SIZE = 1024,
    // Printable ASCII and Latin-1, so that titles with letters such as a-umlaut display.
    FONT_CODEPOINT_COUNT = (126 - 32 + 1) + (255 - 160 + 1),
};

// Downloaded from Google Fonts by CMake and copied next to the executable, so that the app
// needs no network at run time.
static const char FONT_FILE_REGULAR[] = "fonts/Rubik-Regular.ttf";
static const char FONT_FILE_MEDIUM[] = "fonts/Rubik-Medium.ttf";

static const float LEAD_TINT_ALPHA = 0.22f;
// Tasks are drawn as tinted, outlined bars rather than solid ones, so that they stand apart from
// events at a glance; done tasks fade further.
static const float TASK_FILL_ALPHA = 0.20f;
static const float TASK_DONE_FILL_ALPHA = 0.08f;
static const float TASK_DONE_OUTLINE_ALPHA = 0.35f;
static const float TASK_DONE_TEXT_ALPHA = 0.6f;

typedef enum FormField {
    FORM_FIELD_TITLE,
    FORM_FIELD_START_DATE,
    FORM_FIELD_START_TIME,
    FORM_FIELD_END_DATE,
    FORM_FIELD_END_TIME,
    FORM_FIELD_REPEAT_DAYS,      // Shown only for "Every X days"
    FORM_FIELD_REPEAT_END_DATE,  // Shown only for repeating events; empty repeats forever
    FORM_FIELD_COUNT,
} FormField;

enum {
    // No form field is being edited.
    FORM_FIELD_NONE = -1,
    // Three digits for REPEAT_INTERVAL_MAX, plus the terminator.
    REPEAT_DAYS_TEXT_SIZE = 4,
};

// Buffer size of each field, which raygui also uses to cap how much can be typed.
static const int32_t form_field_sizes[FORM_FIELD_COUNT] = {
    EVENT_TITLE_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE,
    REPEAT_DAYS_TEXT_SIZE, DATE_ISO_SIZE,
};

// Choices in the form's repeat selector, in the order shown, and the unit each one means.
typedef enum RepeatOption {
    REPEAT_OPTION_NONE,
    REPEAT_OPTION_WEEK,
    REPEAT_OPTION_DAYS,
    REPEAT_OPTION_MONTH,
    REPEAT_OPTION_YEAR,
    REPEAT_OPTION_COUNT,
} RepeatOption;

// raygui separates combo box items with semicolons.
static const char repeat_option_labels[] =
    "Does not repeat;Every week;Every X days;Every month;Every year";

static const int32_t repeat_option_units[REPEAT_OPTION_COUNT] = {
    REPEAT_NONE, REPEAT_WEEK, REPEAT_DAY, REPEAT_MONTH, REPEAT_YEAR,
};

static_assert(REPEAT_INTERVAL_MAX < 1000, "REPEAT_DAYS_TEXT_SIZE holds three digits.");

// Cast because the constants come from different enums.
static_assert((int)DATE_ISO_SIZE <= (int)EVENT_TITLE_SIZE, "Field fits the shared buffer.");
static_assert((int)TIME_TEXT_SIZE <= (int)EVENT_TITLE_SIZE, "Field fits the shared buffer.");

// Dark enough for white text. Picked by event id, so an event keeps its color in every view.
static const Color event_colors[] = {
    { 31, 119, 180, 255 },   // Blue
    { 214, 39, 40, 255 },    // Red
    { 44, 140, 44, 255 },    // Green
    { 148, 103, 189, 255 },  // Purple
    { 140, 86, 75, 255 },    // Brown
    { 23, 128, 140, 255 },   // Teal
    { 210, 100, 0, 255 },    // Orange
    { 190, 55, 135, 255 },   // Magenta
};
enum { EVENT_COLOR_COUNT = sizeof(event_colors) / sizeof(event_colors[0]) };

static const Color error_color = { 190, 30, 30, 255 };

typedef struct KeyStep {
    int32_t key;     // raylib KeyboardKey
    int32_t days;
    int32_t months;
} KeyStep;

static const KeyStep key_steps[] = {
    { KEY_LEFT, -1, 0 },
    { KEY_RIGHT, 1, 0 },
    { KEY_UP, -7, 0 },
    { KEY_DOWN, 7, 0 },
    { KEY_PAGE_UP, 0, -1 },
    { KEY_PAGE_DOWN, 0, 1 },
};
enum { KEY_STEP_COUNT = sizeof(key_steps) / sizeof(key_steps[0]) };

typedef struct Form {
    int64_t editing_id;    // Event being edited, or 0 for a new event
    int32_t field_active;  // FormField being edited, or FORM_FIELD_NONE
    int repeat_option;     // RepeatOption; int because raygui's combo box writes it
    int kind;              // EventKind; int because raygui's toggle group writes it
    bool open;
    char text[FORM_FIELD_COUNT][EVENT_TITLE_SIZE];
    char error[EVENT_STORE_ERROR_SIZE];  // Why the last save failed, or empty
} Form;

// A font together with the one size it is drawn at. raylib rasterizes a font once, so drawing
// it at another size would blur or pixelate it.
typedef struct TextStyle {
    Font font;
    float size_pixels;
} TextStyle;

typedef struct Fonts {
    TextStyle ui;            // Controls, labels and day numbers; also raygui's font
    TextStyle small;         // Calendar entries and the details on panel cards
    TextStyle small_strong;  // Titles on panel cards
    TextStyle heading;       // Month title and the selected date
    bool loaded;             // False when Rubik was missing and raylib's default font is used
} Fonts;

typedef struct App {
    Event events[EVENT_COUNT_MAX];  // Events of the visible six weeks, ordered by start
    TaskCompletion completions[COMPLETION_COUNT_MAX];  // Done task occurrences near the view
    EventStore store;
    Fonts fonts;
    int64_t event_focused_id;       // Event whose lead time is highlighted, or 0
    int64_t event_delete_id;        // Event waiting for the user to confirm deleting, or 0
    Date today;
    Date selected;
    Date events_month;              // First day of the month that events[] was loaded for
    int32_t event_count;
    int32_t completion_count;
    Form form;
    bool events_stale;              // Reload events[] even though the month is unchanged
    bool events_truncated;
    bool store_open;
    char status[STATUS_SIZE];       // Message for the status bar, cleared on navigation
} App;

// The focused event and the days of its lead time: from its creation to the start of the
// occurrence on the selected date.
typedef struct Highlight {
    const Event *event;  // NULL when nothing is highlighted
    int32_t lead_first_day;
    int32_t lead_last_day;
} Highlight;

typedef struct Layout {
    Rectangle header;
    Rectangle weekdays;
    Rectangle grid;
    Rectangle panel;
    Rectangle status;
    float cell_width;
    float cell_height;
} Layout;

static void app_init(App *app);
static void fonts_load(Fonts *fonts);
static TextStyle fonts_load_style(const char *file_name, int32_t size_pixels,
                                  const int *codepoints, bool *loaded);
static void fonts_unload(Fonts *fonts);
static void app_deinit(App *app);
static void app_handle_keys(App *app, const Layout *layout);
static void app_select(App *app, Date date);
static void app_sync(App *app);
static void app_load_events(App *app, Date month);
static const Event *app_event_find(const App *app, int64_t id);
static int64_t app_event_first_on_day(const App *app, int32_t day_number);
static void app_form_submit(App *app);
static void app_event_delete(App *app, int64_t id);
static bool app_task_done(const App *app, int64_t id, int32_t start_day);
static void app_task_set_done(App *app, const Event *event, int32_t start_day, bool done);
static Layout layout_compute(float width, float height);
static Rectangle grid_cell_rectangle(const Layout *layout, int32_t cell_index);
static Date grid_first_date(Date month);
static void draw_header(App *app, Rectangle header);
static void draw_weekdays(const Fonts *fonts, const Layout *layout);
static void draw_grid(App *app, const Layout *layout);
static Highlight highlight_compute(const App *app);
static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Highlight *highlight);
static void draw_grid_cell_day_number(const App *app, Rectangle cell, Date date, bool in_month);
static void draw_grid_cell_creation_markers(const App *app, Rectangle cell, int32_t day_number,
                                            const Event *focused);
static void draw_grid_cell_events(const App *app, Rectangle cell, int32_t day_number,
                                  int32_t column);
static void draw_grid_cell_event_bar(const App *app, const Event *event, Rectangle bar_slot,
                                     int32_t day_number, int32_t start_day, int32_t column);
static void draw_bar_outline(Rectangle bar, bool starts_here, bool ends_here, Color color);
static void draw_check_mark(Rectangle box, bool done, Color color);
static void draw_panel(App *app, Rectangle panel);
static void draw_panel_events(App *app, Rectangle body);
static bool draw_panel_event_card(App *app, const Event *event, Rectangle card);
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event,
                                          Rectangle card);
static void draw_panel_legend(const Fonts *fonts, Rectangle area);
static void draw_panel_form(App *app, Rectangle body);
static void draw_panel_form_field(const Fonts *fonts, Form *form, int32_t field,
                                  const char *label, Rectangle box);
static void draw_status(const App *app, Rectangle status);
static void form_open(Form *form, Date date, int32_t kind);
static void form_open_edit(Form *form, const Event *event);
static void form_close(Form *form);
static int32_t form_field_next(const Form *form, bool backwards);
static bool form_field_visible(const Form *form, int32_t field);
static float event_card_height(const Event *event);
static const char *form_parse(const Form *form, Event *event);
static bool form_parse_time(const char *text, int32_t *minute_of_day);
static bool form_parse_repeat_days(const char *text, int32_t *days);
static bool event_occurs_on_day(const Event *event, int32_t day_number);
static bool event_created_on_day(const Event *event, int32_t day_number);
static Color event_color(const Event *event);
static void event_format_range(const Event *event, char *buffer, size_t buffer_size);
static void event_format_range_dates(const Event *event, char *buffer, size_t buffer_size);
static void event_format_created(const Event *event, char *buffer, size_t buffer_size);
static void event_format_lead(const Event *event, char *buffer, size_t buffer_size);
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size);
static Color style_color(int32_t property);
static void draw_text(const TextStyle *style, const char *text, float x, float y, Color color);
static Vector2 measure_text(const TextStyle *style, const char *text);
static void draw_text_centered(const TextStyle *style, const char *text, Rectangle area,
                               Color color);

int main(void) {
    // Static rather than on the stack, because App holds every loaded event.
    static App app;
    app_init(&app);

    // The one intentionally unbounded loop: it runs until the window is closed.
    while (!WindowShouldClose()) {
        Layout layout = layout_compute((float)GetScreenWidth(), (float)GetScreenHeight());
        app_handle_keys(&app, &layout);
        app_sync(&app);

        BeginDrawing();
        ClearBackground(style_color(BACKGROUND_COLOR));
        draw_header(&app, layout.header);
        draw_weekdays(&app.fonts, &layout);
        draw_grid(&app, &layout);
        draw_panel(&app, layout.panel);
        draw_status(&app, layout.status);
        EndDrawing();
    }

    app_deinit(&app);
    return 0;
}

static void app_init(App *app) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH_PIXELS, WINDOW_HEIGHT_PIXELS, "Calendar");
    SetWindowMinSize(WINDOW_WIDTH_PIXELS_MIN, WINDOW_HEIGHT_PIXELS_MIN);
    SetTargetFPS(60);
    // Escape cancels the event form; closing the window stays with the window controls.
    SetExitKey(KEY_NULL);
    fonts_load(&app->fonts);
    GuiSetFont(app->fonts.ui.font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, TEXT_SIZE_PIXELS);
    // Rubik's advances already space its glyphs; raylib's default font needs extra room.
    GuiSetStyle(DEFAULT, TEXT_SPACING, app->fonts.loaded ? 0 : 2);
    if (!app->fonts.loaded) {
        snprintf(app->status, sizeof(app->status), "Rubik font not found: using the default.");
    }

    app->today = date_today();
    app->selected = app->today;
    app->events_month = (Date){ app->today.year, app->today.month, 1 };
    app->events_stale = true;
    app->form.field_active = FORM_FIELD_NONE;

    // Beside the executable: the app has no settings or installer, so this is the one place
    // that is both writable and predictable.
    char path[DATABASE_PATH_SIZE];
    int written = snprintf(path, sizeof(path), "%scalendar.db", GetApplicationDirectory());
    if (written < 0 || written >= (int)sizeof(path)) {
        snprintf(app->status, sizeof(app->status), "Database path is too long.");
        return;
    }
    app->store_open = event_store_open(&app->store, path);
    if (!app->store_open) {
        snprintf(app->status, sizeof(app->status), "%s", app->store.error);
        event_store_close(&app->store);
    }
}

static void app_deinit(App *app) {
    if (app->store_open) {
        event_store_close(&app->store);
        app->store_open = false;
    }
    fonts_unload(&app->fonts);
    CloseWindow();
}

// Loads Rubik at each size it is drawn at. Needs the window, because glyphs go to a texture.
static void fonts_load(Fonts *fonts) {
    int codepoints[FONT_CODEPOINT_COUNT];
    int32_t count = 0;
    for (int32_t codepoint = 32; codepoint <= 126; codepoint++) {
        codepoints[count++] = codepoint;
    }
    for (int32_t codepoint = 160; codepoint <= 255; codepoint++) {
        codepoints[count++] = codepoint;
    }
    assert(count == FONT_CODEPOINT_COUNT);

    fonts->loaded = true;
    fonts->ui = fonts_load_style(FONT_FILE_REGULAR, TEXT_SIZE_PIXELS, codepoints, &fonts->loaded);
    fonts->small = fonts_load_style(FONT_FILE_REGULAR, TEXT_SIZE_PIXELS_SMALL, codepoints,
                                    &fonts->loaded);
    fonts->small_strong = fonts_load_style(FONT_FILE_MEDIUM, TEXT_SIZE_PIXELS_SMALL, codepoints,
                                           &fonts->loaded);
    fonts->heading = fonts_load_style(FONT_FILE_MEDIUM, TEXT_SIZE_PIXELS_HEADING, codepoints,
                                      &fonts->loaded);
}

// Falls back to raylib's default font, scaled, when the file is missing or unreadable, so that
// the calendar still works; *loaded then becomes false.
static TextStyle fonts_load_style(const char *file_name, int32_t size_pixels,
                                  const int *codepoints, bool *loaded) {
    assert(size_pixels > 0);
    TextStyle style = { GetFontDefault(), (float)size_pixels };
    char path[DATABASE_PATH_SIZE];
    int written = snprintf(path, sizeof(path), "%s%s", GetApplicationDirectory(), file_name);
    if (written < 0 || written >= (int)sizeof(path)) {
        *loaded = false;
        return style;
    }
    // raylib takes a non-const pointer but only reads the codepoints.
    Font font = LoadFontEx(path, size_pixels, (int *)codepoints, FONT_CODEPOINT_COUNT);
    if (!IsFontValid(font)) {
        *loaded = false;
        return style;
    }
    style.font = font;
    return style;
}

static void fonts_unload(Fonts *fonts) {
    // UnloadFont leaves raylib's default font alone, so a fallback style is safe to pass.
    UnloadFont(fonts->ui.font);
    UnloadFont(fonts->small.font);
    UnloadFont(fonts->small_strong.font);
    UnloadFont(fonts->heading.font);
    *fonts = (Fonts){ 0 };
}

static void app_handle_keys(App *app, const Layout *layout) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (app->form.open) {
            form_close(&app->form);
        }
        app->event_delete_id = 0;
        return;
    }
    if (app->form.field_active != FORM_FIELD_NONE) {
        // Keys belong to the text box being edited; only Tab is ours, to move between fields.
        if (IsKeyPressed(KEY_TAB)) {
            bool backwards = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            app->form.field_active = form_field_next(&app->form, backwards);
        }
        return;
    }

    Date target = app->selected;
    for (int32_t i = 0; i < KEY_STEP_COUNT; i++) {
        if (IsKeyPressed(key_steps[i].key)) {
            target = date_add_days(target, key_steps[i].days);
            target = date_add_months(target, key_steps[i].months);
        }
    }
    if (IsKeyPressed(KEY_HOME)) {
        target = app->today;
    }
    // The wheel changes month only over the grid, so that it means nothing over the panel.
    if (CheckCollisionPointRec(GetMousePosition(), layout->grid)) {
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) {
            target = date_add_months(target, -1);
        }
        if (wheel < 0.0f) {
            target = date_add_months(target, 1);
        }
    }
    app_select(app, target);
}

static void app_select(App *app, Date date) {
    assert(date_is_valid(date));
    if (date_equal(date, app->selected)) {
        return;
    }
    app->selected = date;
    app->event_focused_id = 0;
    // A confirmation belongs to the card it was asked on.
    app->event_delete_id = 0;
    app->status[0] = '\0';
}

// Brings derived state up to date before drawing: today's date, the loaded events and the
// focused event, which is always one of the events on the selected date.
static void app_sync(App *app) {
    app->today = date_today();
    Date month = { app->selected.year, app->selected.month, 1 };
    if (app->events_stale || !date_equal(month, app->events_month)) {
        app_load_events(app, month);
    }

    int32_t selected_day = date_to_day_number(app->selected);
    const Event *focused = app_event_find(app, app->event_focused_id);
    if (focused == NULL || !event_occurs_on_day(focused, selected_day)) {
        app->event_focused_id = app_event_first_on_day(app, selected_day);
    }
}

static void app_load_events(App *app, Date month) {
    assert(month.day == 1);
    app->events_month = month;
    app->events_stale = false;
    app->events_truncated = false;
    app->event_count = 0;
    app->completion_count = 0;
    if (!app->store_open) {
        return;
    }

    Date first = grid_first_date(month);
    Date last = date_add_days(first, GRID_CELL_COUNT - 1);
    int32_t count = event_store_load_range(&app->store, first, last, app->events,
                                           EVENT_COUNT_MAX, &app->events_truncated);
    if (count < 0) {
        snprintf(app->status, sizeof(app->status), "%s", app->store.error);
        return;
    }
    assert(count <= EVENT_COUNT_MAX);
    app->event_count = count;

    bool completions_truncated = false;
    count = event_store_load_completions(&app->store, first, last, app->completions,
                                         COMPLETION_COUNT_MAX, &completions_truncated);
    if (count < 0) {
        snprintf(app->status, sizeof(app->status), "%s", app->store.error);
        return;
    }
    assert(count <= COMPLETION_COUNT_MAX);
    app->completion_count = count;
    app->events_truncated = app->events_truncated || completions_truncated;
}

static bool app_task_done(const App *app, int64_t id, int32_t start_day) {
    for (int32_t i = 0; i < app->completion_count; i++) {
        const TaskCompletion *completion = &app->completions[i];
        if (completion->event_id == id && completion->occurrence_start_day == start_day) {
            return true;
        }
    }
    return false;
}

static void app_task_set_done(App *app, const Event *event, int32_t start_day, bool done) {
    assert(event->kind == EVENT_KIND_TASK);
    assert(app_task_done(app, event->id, start_day) != done);
    Date occurrence_start = date_from_day_number(start_day);
    if (!event_store_set_task_done(&app->store, event->id, occurrence_start, done)) {
        snprintf(app->status, sizeof(app->status), "%s", app->store.error);
        return;
    }
    // completions[] is reloaded by the next app_sync, together with the events.
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "%s \"%s\".", done ? "Done:" : "Not done:",
             event->title);
}

static const Event *app_event_find(const App *app, int64_t id) {
    if (id == 0) {
        return NULL;
    }
    for (int32_t i = 0; i < app->event_count; i++) {
        if (app->events[i].id == id) {
            return &app->events[i];
        }
    }
    return NULL;
}

static int64_t app_event_first_on_day(const App *app, int32_t day_number) {
    for (int32_t i = 0; i < app->event_count; i++) {
        if (event_occurs_on_day(&app->events[i], day_number)) {
            return app->events[i].id;
        }
    }
    return 0;
}

static void app_form_submit(App *app) {
    assert(app->form.open);
    Event event = { 0 };
    const char *problem = form_parse(&app->form, &event);
    if (problem != NULL) {
        snprintf(app->form.error, sizeof(app->form.error), "%s", problem);
        return;
    }
    if (!app->store_open) {
        snprintf(app->form.error, sizeof(app->form.error), "No database: cannot save.");
        return;
    }
    bool editing = app->form.editing_id != 0;
    int64_t id = app->form.editing_id;
    if (editing) {
        event.id = id;
        if (!event_store_update(&app->store, &event)) {
            snprintf(app->form.error, sizeof(app->form.error), "%s", app->store.error);
            return;
        }
    } else {
        id = event_store_insert(&app->store, &event);
        if (id == 0) {
            snprintf(app->form.error, sizeof(app->form.error), "%s", app->store.error);
            return;
        }
    }

    form_close(&app->form);
    // Show the event at once, focused, so that its lead time is visible. An edited event that
    // still occurs on the selected day stays there, rather than jumping to its first start.
    int32_t start_day = 0;
    if (!event_occurrence_on_day(&event, date_to_day_number(app->selected), &start_day)) {
        app_select(app, event.start_date);
    }
    app->event_focused_id = id;
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "%s \"%s\".", editing ? "Updated" : "Saved",
             event.title);
}

static void app_event_delete(App *app, int64_t id) {
    assert(id > 0);
    app->event_delete_id = 0;
    char title[EVENT_TITLE_SIZE] = "";
    const Event *event = app_event_find(app, id);
    if (event != NULL) {
        snprintf(title, sizeof(title), "%s", event->title);
    }

    if (!event_store_delete(&app->store, id)) {
        snprintf(app->status, sizeof(app->status), "%s", app->store.error);
        // The event may be gone, so show the real state.
        app->events_stale = true;
        return;
    }
    // events[] is reloaded by the next app_sync; this frame still draws the old list.
    app->event_focused_id = 0;
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "Deleted \"%s\".", title);
}

static Layout layout_compute(float width, float height) {
    const float padding = (float)PADDING_PIXELS;
    const float status_height = (float)STATUS_HEIGHT_PIXELS;
    Layout layout = { 0 };
    layout.status = (Rectangle){ 0, height - status_height, width, status_height };
    layout.panel = (Rectangle){ width - padding - PANEL_WIDTH_PIXELS, padding,
                                PANEL_WIDTH_PIXELS, height - status_height - 2 * padding };

    float calendar_width = layout.panel.x - 2 * padding;
    layout.header = (Rectangle){ padding, padding, calendar_width, HEADER_HEIGHT_PIXELS };
    float weekdays_y = layout.header.y + layout.header.height + padding / 2;
    layout.weekdays = (Rectangle){ padding, weekdays_y, calendar_width, WEEKDAY_HEIGHT_PIXELS };
    float grid_y = weekdays_y + WEEKDAY_HEIGHT_PIXELS;
    layout.grid = (Rectangle){ padding, grid_y, calendar_width,
                               height - status_height - padding - grid_y };
    layout.cell_width = layout.grid.width / GRID_COLUMN_COUNT;
    layout.cell_height = layout.grid.height / GRID_ROW_COUNT;
    // The minimum window size guarantees room for every part.
    assert(layout.cell_width > 0.0f);
    assert(layout.cell_height > 0.0f);
    return layout;
}

static Rectangle grid_cell_rectangle(const Layout *layout, int32_t cell_index) {
    assert(cell_index >= 0);
    assert(cell_index < GRID_CELL_COUNT);
    int32_t column = cell_index % GRID_COLUMN_COUNT;
    int32_t row = cell_index / GRID_COLUMN_COUNT;
    return (Rectangle){ layout->grid.x + (float)column * layout->cell_width,
                        layout->grid.y + (float)row * layout->cell_height,
                        layout->cell_width, layout->cell_height };
}

// The grid starts on the Monday of the week that contains the first of the month.
static Date grid_first_date(Date month) {
    assert(month.day == 1);
    Date first = date_add_days(month, -date_weekday(month));
    assert(date_weekday(first) == 0);
    return first;
}

static void draw_header(App *app, Rectangle header) {
    const float height = header.height;
    if (GuiButton((Rectangle){ header.x, header.y, 40, height }, "<")) {
        app_select(app, date_add_months(app->selected, -1));
    }
    if (GuiButton((Rectangle){ header.x + 48, header.y, 40, height }, ">")) {
        app_select(app, date_add_months(app->selected, 1));
    }
    if (GuiButton((Rectangle){ header.x + 96, header.y, 90, height }, "Today")) {
        app_select(app, app->today);
    }

    char title[TEXT_LINE_SIZE];
    snprintf(title, sizeof(title), "%s %" PRId32, month_name(app->events_month.month),
             app->events_month.year);
    draw_text_centered(&app->fonts.heading, title, header, style_color(TEXT_COLOR_NORMAL));
}

static void draw_weekdays(const Fonts *fonts, const Layout *layout) {
    for (int32_t column = 0; column < GRID_COLUMN_COUNT; column++) {
        Rectangle area = { layout->weekdays.x + (float)column * layout->cell_width,
                           layout->weekdays.y, layout->cell_width, layout->weekdays.height };
        // Weekend names are dimmed, as on a paper calendar.
        bool weekend = column >= 5;
        Color color = style_color(weekend ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
        draw_text_centered(&fonts->ui, weekday_short_name(column), area, color);
    }
}

// Draws from events_month rather than the selected date, so that a click that changes month
// mid-frame never draws one month's days with another month's events.
static void draw_grid(App *app, const Layout *layout) {
    int32_t first_day = date_to_day_number(grid_first_date(app->events_month));
    Highlight highlight = highlight_compute(app);
    for (int32_t cell_index = 0; cell_index < GRID_CELL_COUNT; cell_index++) {
        Rectangle cell = grid_cell_rectangle(layout, cell_index);
        int32_t column = cell_index % GRID_COLUMN_COUNT;
        draw_grid_cell(app, cell, first_day + cell_index, column, &highlight);
    }
}

static Highlight highlight_compute(const App *app) {
    Highlight highlight = { NULL, 0, 0 };
    const Event *event = app_event_find(app, app->event_focused_id);
    if (event == NULL) {
        return highlight;
    }
    // A click earlier in this frame may have moved the selection off the focused event.
    int32_t start_day = 0;
    if (!event_occurrence_on_day(event, date_to_day_number(app->selected), &start_day)) {
        return highlight;
    }

    int32_t created_day = date_to_day_number(event->created_date);
    highlight.event = event;
    // An event added after its occurrence began runs its lead time backwards.
    highlight.lead_first_day = created_day < start_day ? created_day : start_day;
    highlight.lead_last_day = created_day < start_day ? start_day : created_day;
    assert(highlight.lead_first_day <= highlight.lead_last_day);
    return highlight;
}

static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Highlight *highlight) {
    Date date = date_from_day_number(day_number);
    bool in_month = date.month == app->events_month.month;
    bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        app_select(app, date);
    }

    Color fill = style_color(in_month ? BASE_COLOR_NORMAL : BACKGROUND_COLOR);
    if (hovered) {
        fill = style_color(BASE_COLOR_FOCUSED);
    }
    DrawRectangleRec(cell, fill);
    if (highlight->event != NULL) {
        if (highlight->lead_first_day <= day_number && day_number <= highlight->lead_last_day) {
            DrawRectangleRec(cell, Fade(event_color(highlight->event), LEAD_TINT_ALPHA));
        }
    }
    DrawRectangleLinesEx(cell, 1, style_color(BORDER_COLOR_NORMAL));

    draw_grid_cell_day_number(app, cell, date, in_month);
    draw_grid_cell_creation_markers(app, cell, day_number, highlight->event);
    draw_grid_cell_events(app, cell, day_number, column);

    if (date_equal(date, app->selected)) {
        Rectangle inner = { cell.x + 2, cell.y + 2, cell.width - 4, cell.height - 4 };
        DrawRectangleLinesEx(inner, 2, style_color(BORDER_COLOR_PRESSED));
    }
}

static void draw_grid_cell_day_number(const App *app, Rectangle cell, Date date, bool in_month) {
    char number[4];
    snprintf(number, sizeof(number), "%" PRId32, date.day);
    Vector2 position = { cell.x + 10, cell.y + 7 };
    Color color = style_color(in_month ? TEXT_COLOR_NORMAL : TEXT_COLOR_DISABLED);

    if (date_equal(date, app->today)) {
        Vector2 size = measure_text(&app->fonts.ui, number);
        Vector2 center = { position.x + size.x / 2, position.y + size.y / 2 };
        DrawCircleV(center, size.y * 0.85f, style_color(BASE_COLOR_PRESSED));
        color = style_color(TEXT_COLOR_PRESSED);
    }
    draw_text(&app->fonts.ui, number, position.x, position.y, color);
}

// One dot per event created on this day, right to left from the top-right corner. The focused
// event's dot gets a ring so that the start of its highlighted lead time is easy to find.
static void draw_grid_cell_creation_markers(const App *app, Rectangle cell, int32_t day_number,
                                            const Event *focused) {
    const float radius = (float)CREATION_MARKER_RADIUS_PIXELS;
    const float spacing = 2 * radius + 6;
    Vector2 center = { cell.x + cell.width - 14, cell.y + 7 + TEXT_SIZE_PIXELS / 2.0f };
    int32_t marker_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        if (!event_created_on_day(event, day_number)) {
            continue;
        }
        if (marker_count == CREATION_MARKER_COUNT_MAX) {
            draw_text(&app->fonts.small, "+", center.x - radius,
                      center.y - TEXT_SIZE_PIXELS_SMALL / 2.0f, style_color(TEXT_COLOR_NORMAL));
            break;
        }
        if (event == focused) {
            DrawCircleV(center, radius + 3, style_color(BORDER_COLOR_PRESSED));
        }
        DrawCircleV(center, radius, event_color(event));
        center.x -= spacing;
        marker_count++;
    }
}

static void draw_grid_cell_events(const App *app, Rectangle cell, int32_t day_number,
                                  int32_t column) {
    const float slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    float top = cell.y + CELL_TOP_LINE_PIXELS;
    int32_t slot_count = (int32_t)((cell.y + cell.height - 4 - top) / slot_height);
    if (slot_count <= 0) {
        return;
    }

    int32_t overlap_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        if (event_occurs_on_day(&app->events[i], day_number)) {
            overlap_count++;
        }
    }
    // When not all fit, the last slot says how many are hidden instead of showing a bar.
    int32_t bar_count = overlap_count <= slot_count ? overlap_count : slot_count - 1;

    // Multi-day events first, then one-day events, so that a multi-day bar keeps its row from
    // one day to the next instead of moving below whatever one-day events each day has.
    int32_t bar_index = 0;
    for (int32_t pass = 0; pass < 2; pass++) {
        bool multi_day_pass = pass == 0;
        for (int32_t i = 0; i < app->event_count && bar_index < bar_count; i++) {
            const Event *event = &app->events[i];
            int32_t start_day = 0;
            if ((event_duration_days(event) > 0) != multi_day_pass) {
                continue;
            }
            if (event_occurrence_on_day(event, day_number, &start_day)) {
                Rectangle slot = { cell.x, top + (float)bar_index * slot_height, cell.width,
                                   EVENT_BAR_HEIGHT_PIXELS };
                draw_grid_cell_event_bar(app, event, slot, day_number, start_day, column);
                bar_index++;
            }
        }
    }
    if (overlap_count > bar_count) {
        char more[TEXT_LINE_SIZE];
        snprintf(more, sizeof(more), "+%" PRId32 " more", overlap_count - bar_count);
        draw_text(&app->fonts.small, more, cell.x + 6, top + (float)bar_count * slot_height + 2,
                  style_color(TEXT_COLOR_NORMAL));
    }
}

// start_day is the start of the occurrence that covers day_number.
static void draw_grid_cell_event_bar(const App *app, const Event *event, Rectangle bar_slot,
                                     int32_t day_number, int32_t start_day, int32_t column) {
    assert(start_day <= day_number);
    const bool task = event->kind == EVENT_KIND_TASK;
    const bool done = task && app_task_done(app, event->id, start_day);
    const TextStyle *style = &app->fonts.small;
    const Color color = event_color(event);
    bool starts_here = day_number == start_day;
    bool ends_here = day_number == start_day + event_duration_days(event);
    // Multi-day events run edge to edge between days, so that they read as one bar.
    float left = bar_slot.x + (starts_here ? 4.0f : 0.0f);
    float right = bar_slot.x + bar_slot.width - (ends_here ? 4.0f : 0.0f);
    Rectangle bar = { left, bar_slot.y, right - left, bar_slot.height };
    if (task) {
        DrawRectangleRec(bar, Fade(color, done ? TASK_DONE_FILL_ALPHA : TASK_FILL_ALPHA));
        draw_bar_outline(bar, starts_here, ends_here,
                         Fade(color, done ? TASK_DONE_OUTLINE_ALPHA : 1.0f));
    } else {
        DrawRectangleRec(bar, color);
    }

    // Label the first day and the start of each week, where the eye starts reading a row.
    if (!starts_here && column != 0) {
        return;
    }
    char label[TEXT_LINE_SIZE];
    if (starts_here && event->start_minute != TIME_NONE) {
        char time[TIME_TEXT_SIZE];
        time_format(event->start_minute, time, sizeof(time));
        snprintf(label, sizeof(label), "%s %s", time, event->title);
    } else {
        snprintf(label, sizeof(label), "%s", event->title);
    }

    float text_x = bar.x + 4;
    Color text_color = WHITE;
    BeginScissorMode((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height);
    if (task) {
        Rectangle box = { bar.x + 4, bar.y + 4, 11, 11 };
        draw_check_mark(box, done, color);
        text_x = box.x + box.width + 4;
        // The task's own color, faded when done: grey would vanish on the grey cells.
        text_color = done ? Fade(color, TASK_DONE_TEXT_ALPHA) : color;
    }
    // Centered vertically in the bar.
    draw_text(style, label, text_x, bar.y + (bar.height - style->size_pixels) / 2, text_color);
    if (done) {
        float middle_y = bar.y + bar.height / 2;
        float width = measure_text(style, label).x;
        DrawLineEx((Vector2){ text_x, middle_y }, (Vector2){ text_x + width, middle_y }, 1.5f,
                   text_color);
    }
    EndScissorMode();
}

// Outlines a bar without lines between the days of a multi-day occurrence.
static void draw_bar_outline(Rectangle bar, bool starts_here, bool ends_here, Color color) {
    DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width, 1 }, color);
    DrawRectangleRec((Rectangle){ bar.x, bar.y + bar.height - 1, bar.width, 1 }, color);
    if (starts_here) {
        DrawRectangleRec((Rectangle){ bar.x, bar.y, 1, bar.height }, color);
    }
    if (ends_here) {
        DrawRectangleRec((Rectangle){ bar.x + bar.width - 1, bar.y, 1, bar.height }, color);
    }
}

// An empty box for a task that is not done, a filled one for a task that is.
static void draw_check_mark(Rectangle box, bool done, Color color) {
    DrawRectangleLinesEx(box, 1.5f, color);
    if (done) {
        DrawRectangleRec((Rectangle){ box.x + 3, box.y + 3, box.width - 6, box.height - 6 },
                         color);
    }
}

static void draw_panel(App *app, Rectangle panel) {
    GuiPanel(panel, NULL);
    const float padding = (float)PADDING_PIXELS;
    Rectangle area = { panel.x + padding, panel.y + padding, panel.width - 2 * padding,
                       panel.height - 2 * padding };

    draw_text(&app->fonts.ui, weekday_name(date_weekday(app->selected)), area.x, area.y,
              style_color(TEXT_COLOR_DISABLED));
    char heading[TEXT_LINE_SIZE];
    snprintf(heading, sizeof(heading), "%" PRId32 " %s %" PRId32, app->selected.day,
             month_name(app->selected.month), app->selected.year);
    draw_text(&app->fonts.heading, heading, area.x, area.y + TEXT_LINE_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));

    const float heading_height = 72;
    Rectangle body = { area.x, area.y + heading_height, area.width,
                       area.height - heading_height };
    if (app->form.open) {
        draw_panel_form(app, body);
    } else {
        draw_panel_events(app, body);
    }
}

static void draw_panel_events(App *app, Rectangle body) {
    const float button_width = (body.width - PADDING_PIXELS) / 2;
    Rectangle event_button = { body.x, body.y, button_width, BUTTON_HEIGHT_PIXELS };
    Rectangle task_button = { body.x + button_width + PADDING_PIXELS, body.y, button_width,
                              BUTTON_HEIGHT_PIXELS };
    if (!app->store_open) {
        GuiDisable();
    }
    if (GuiButton(event_button, "New event")) {
        app->event_delete_id = 0;
        form_open(&app->form, app->selected, EVENT_KIND_EVENT);
    }
    if (GuiButton(task_button, "New task")) {
        app->event_delete_id = 0;
        form_open(&app->form, app->selected, EVENT_KIND_TASK);
    }
    GuiEnable();

    // Cards stop where the "+N more" line and the legend begin.
    float list_top = body.y + BUTTON_HEIGHT_PIXELS + PADDING_PIXELS;
    float list_bottom = body.y + body.height - LEGEND_HEIGHT_PIXELS - TEXT_LINE_HEIGHT_PIXELS;
    int32_t selected_day = date_to_day_number(app->selected);
    float y = list_top;
    int32_t shown_count = 0;
    int32_t hidden_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        int32_t start_day = 0;
        if (!event_occurrence_on_day(event, selected_day, &start_day)) {
            continue;
        }
        float card_height = event_card_height(event);
        if (y + card_height > list_bottom) {
            hidden_count++;
            continue;
        }
        Rectangle card = { body.x, y, body.width, card_height };
        // The card shows this day's occurrence, so its dates and lead time are for this day.
        Event occurrence = event_occurrence_at(event, start_day);
        if (draw_panel_event_card(app, &occurrence, card)) {
            // Edit the event itself, not this occurrence, so that the form shows its first dates.
            app->event_focused_id = event->id;
            form_open_edit(&app->form, event);
        }
        y += card_height + EVENT_CARD_GAP_PIXELS;
        shown_count++;
    }

    Color dim = style_color(TEXT_COLOR_DISABLED);
    if (shown_count + hidden_count == 0) {
        draw_text(&app->fonts.ui, "Nothing on this day.", body.x, list_top, dim);
    }
    if (hidden_count > 0) {
        char more[TEXT_LINE_SIZE];
        snprintf(more, sizeof(more), "+%" PRId32 " more", hidden_count);
        draw_text(&app->fonts.small, more, body.x, y, dim);
    }
    draw_panel_legend(&app->fonts, (Rectangle){ body.x, body.y + body.height -
                                                LEGEND_HEIGHT_PIXELS, body.width,
                                                LEGEND_HEIGHT_PIXELS });
}

// Returns true when the card itself was clicked, not one of its buttons. Deleting asks first, on
// the card, because it is permanent and also removes every repeat of a repeating event.
// A task's checkbox marks the occurrence on this card, which starts on event->start_date.
static bool draw_panel_event_card(App *app, const Event *event, Rectangle card) {
    const float x = card.x + 16;
    const Rectangle delete_button = { card.x + card.width - 82, card.y + 6, 72, 24 };
    const Rectangle checkbox = { x, card.y + 8, 16, 16 };
    const TextStyle *title_style = &app->fonts.small_strong;
    const Rectangle confirm_button = { x, card.y + card.height - 44, 110, 32 };
    const Rectangle cancel_button = { x + 122, confirm_button.y, 110, 32 };
    const bool task = event->kind == EVENT_KIND_TASK;
    const int32_t start_day = date_to_day_number(event->start_date);
    const bool done = task && app_task_done(app, event->id, start_day);
    const bool confirming = event->id == app->event_delete_id;
    const bool focused = event->id == app->event_focused_id;
    const Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, card);

    DrawRectangleRec(card, style_color(hovered ? BASE_COLOR_FOCUSED : BASE_COLOR_NORMAL));
    DrawRectangleRec((Rectangle){ card.x, card.y, 6, card.height }, event_color(event));
    DrawRectangleLinesEx(card, focused || confirming ? 2.0f : 1.0f,
                         style_color(focused ? BORDER_COLOR_PRESSED : BORDER_COLOR_NORMAL));

    // Clip the title short of the delete button. A done task's title is dimmed and struck out.
    const float title_x = task ? x + 24 : x;
    const Color title_color = style_color(done ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
    BeginScissorMode((int)card.x, (int)card.y, (int)(card.width - 90), (int)card.height);
    draw_text(title_style, event->title, title_x, card.y + 8, title_color);
    if (done) {
        float width = measure_text(title_style, event->title).x;
        float middle_y = card.y + 8 + title_style->size_pixels / 2 + 1;
        DrawLineEx((Vector2){ title_x, middle_y }, (Vector2){ title_x + width, middle_y }, 1.5f,
                   title_color);
    }
    EndScissorMode();

    if (confirming) {
        const char *question = event->repeat_unit != REPEAT_NONE ? "Delete it and all repeats?"
                               : task                            ? "Delete this task?"
                                                                 : "Delete this event?";
        draw_text(&app->fonts.small, question, x, card.y + 8 + TEXT_LINE_HEIGHT_PIXELS_SMALL,
                  error_color);
        if (GuiButton(confirm_button, "Delete")) {
            app_event_delete(app, event->id);
        }
        if (GuiButton(cancel_button, "Cancel")) {
            app->event_delete_id = 0;
        }
        // Only the buttons act while confirming, so that a stray click cannot change focus.
        return false;
    }

    draw_panel_event_card_details(&app->fonts, event, card);
    if (task) {
        bool checked = done;
        GuiCheckBox(checkbox, NULL, &checked);
        if (checked != done) {
            app_task_set_done(app, event, start_day, checked);
        }
    }
    if (GuiButton(delete_button, "Delete")) {
        app->event_delete_id = event->id;
    }
    bool on_button = CheckCollisionPointRec(mouse, delete_button) ||
                     (task && CheckCollisionPointRec(mouse, checkbox));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !on_button;
}

// The lines below the title: dates and repeat, repeat end, creation, and lead time.
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event,
                                          Rectangle card) {
    const float x = card.x + 16;
    const float line = (float)TEXT_LINE_HEIGHT_PIXELS_SMALL;
    const TextStyle *style = &fonts->small;
    char text[TEXT_LINE_SIZE];
    BeginScissorMode((int)card.x, (int)card.y, (int)card.width - 4, (int)card.height);
    float y = card.y + 8 + line;
    event_format_range(event, text, sizeof(text));
    draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    if (event->repeat_has_end) {
        y += line;
        Date end = event->repeat_end_date;
        snprintf(text, sizeof(text), "Repeats until %" PRId32 " %s %" PRId32, end.day,
                 month_short_name(end.month), end.year);
        draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    }
    y += line;
    event_format_created(event, text, sizeof(text));
    draw_text(style, text, x, y, style_color(TEXT_COLOR_DISABLED));
    y += line;
    event_format_lead(event, text, sizeof(text));
    draw_text(style, text, x, y, event_color(event));
    EndScissorMode();
}

// Explains the two grid markings that are not self-evident.
static void draw_panel_legend(const Fonts *fonts, Rectangle area) {
    const Color sample = event_colors[0];
    const Color dim = style_color(TEXT_COLOR_DISABLED);
    const float text_x = area.x + 26;
    DrawCircleV((Vector2){ area.x + 8, area.y + 8 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text(&fonts->small, "Day it was created", text_x, area.y, dim);

    float second_y = area.y + TEXT_LINE_HEIGHT_PIXELS_SMALL + 4;
    DrawRectangleRec((Rectangle){ area.x, second_y, 16, 16 }, Fade(sample, LEAD_TINT_ALPHA));
    draw_text(&fonts->small, "From creation to start, selected", text_x, second_y, dim);
}

static void draw_panel_form(App *app, Rectangle body) {
    Form *form = &app->form;
    const float row_height = TEXT_LINE_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + 12;
    const float gap = 12;
    const float time_width = 96;
    const float date_width = body.width - time_width - gap;
    const float time_x = body.x + date_width + gap;

    const char *heading = form->kind == EVENT_KIND_TASK
                              ? (form->editing_id != 0 ? "Edit task" : "New task")
                              : (form->editing_id != 0 ? "Edit event" : "New event");
    draw_text(&app->fonts.ui, heading, body.x, body.y, style_color(TEXT_COLOR_NORMAL));
    // raygui lays the toggles out side by side, each the size of these bounds, 2 pixels apart.
    const float toggle_width = 72;
    GuiToggleGroup((Rectangle){ body.x + body.width - 2 * toggle_width - 2, body.y - 4,
                                toggle_width, 28 },
                   "Event;Task", &form->kind);
    // Each field's label sits one line above its box, so leave room for the heading and it.
    float y = body.y + 2 * TEXT_LINE_HEIGHT_PIXELS + 12;
    draw_panel_form_field(&app->fonts, form, FORM_FIELD_TITLE, "Title",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(&app->fonts, form, FORM_FIELD_START_DATE, "Start date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(&app->fonts, form, FORM_FIELD_START_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(&app->fonts, form, FORM_FIELD_END_DATE, "End date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(&app->fonts, form, FORM_FIELD_END_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_text(&app->fonts.ui, "Repeat", body.x, y - TEXT_LINE_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    GuiComboBox((Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS }, repeat_option_labels,
                &form->repeat_option);
    if (form_field_visible(form, FORM_FIELD_REPEAT_DAYS)) {
        draw_panel_form_field(&app->fonts, form, FORM_FIELD_REPEAT_DAYS, "Days",
                              (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    }
    // The row is kept while hidden, so that the buttons do not move under the pointer.
    y += row_height;
    if (form_field_visible(form, FORM_FIELD_REPEAT_END_DATE)) {
        draw_panel_form_field(&app->fonts, form, FORM_FIELD_REPEAT_END_DATE,
                              "Repeat until (optional)",
                              (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    }

    // One hint line, so that the buttons still fit in the panel at the minimum window height.
    y += FIELD_HEIGHT_PIXELS + 12;
    Color dim = style_color(TEXT_COLOR_DISABLED);
    draw_text(&app->fonts.small, "Dates YYYY-MM-DD, times HH:MM (optional)", body.x, y,
              dim);
    y += TEXT_LINE_HEIGHT_PIXELS + 8;
    if (form->error[0] != '\0') {
        BeginScissorMode((int)body.x, (int)y, (int)body.width, TEXT_LINE_HEIGHT_PIXELS);
        draw_text(&app->fonts.ui, form->error, body.x, y, error_color);
        EndScissorMode();
    }

    y += TEXT_LINE_HEIGHT_PIXELS + 8;
    const float button_width = (body.width - gap) / 2;
    if (GuiButton((Rectangle){ body.x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Cancel")) {
        form_close(form);
    }
    Rectangle save = { body.x + button_width + gap, y, button_width, BUTTON_HEIGHT_PIXELS };
    if (GuiButton(save, "Save")) {
        app_form_submit(app);
    }
}

static void draw_panel_form_field(const Fonts *fonts, Form *form, int32_t field,
                                  const char *label, Rectangle box) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    draw_text(&fonts->ui, label, box.x, box.y - TEXT_LINE_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    bool editing = form->field_active == field;
    // raygui returns true when edit mode should toggle: a click on the box, or Enter or a
    // click elsewhere while editing.
    if (GuiTextBox(box, form->text[field], form_field_sizes[field], editing)) {
        form->field_active = editing ? FORM_FIELD_NONE : field;
    }
}

static void draw_status(const App *app, Rectangle status) {
    char text[STATUS_SIZE];
    if (app->status[0] != '\0') {
        snprintf(text, sizeof(text), "%s", app->status);
    } else if (!app->store_open) {
        snprintf(text, sizeof(text), "No database: events cannot be loaded or saved.");
    } else if (app->events_truncated) {
        snprintf(text, sizeof(text), "Showing the first %d events in this view; more are hidden.",
                 EVENT_COUNT_MAX);
    } else {
        snprintf(text, sizeof(text),
                 "%" PRId32 " items in view    Arrows: day/week    PgUp/PgDn: month    "
                 "Click an item to edit", app->event_count);
    }
    GuiStatusBar(status, text);
}

static void form_open(Form *form, Date date, int32_t kind) {
    assert(!form->open);
    assert(kind >= 0);
    assert(kind < EVENT_KIND_COUNT);
    *form = (Form){ .field_active = FORM_FIELD_TITLE, .repeat_option = REPEAT_OPTION_NONE,
                    .kind = kind, .open = true };
    date_format_iso(date, form->text[FORM_FIELD_START_DATE], DATE_ISO_SIZE);
    date_format_iso(date, form->text[FORM_FIELD_END_DATE], DATE_ISO_SIZE);
    // A starting value, so that choosing "Every X days" shows what to type.
    snprintf(form->text[FORM_FIELD_REPEAT_DAYS], REPEAT_DAYS_TEXT_SIZE, "2");
}

// Fills the form from a stored event. Saving then updates that event instead of adding one.
static void form_open_edit(Form *form, const Event *event) {
    assert(event->id > 0);
    form_open(form, event->start_date, event->kind);
    form->editing_id = event->id;
    snprintf(form->text[FORM_FIELD_TITLE], EVENT_TITLE_SIZE, "%s", event->title);
    date_format_iso(event->end_date, form->text[FORM_FIELD_END_DATE], DATE_ISO_SIZE);
    if (event->start_minute != TIME_NONE) {
        time_format(event->start_minute, form->text[FORM_FIELD_START_TIME], TIME_TEXT_SIZE);
    }
    if (event->end_minute != TIME_NONE) {
        time_format(event->end_minute, form->text[FORM_FIELD_END_TIME], TIME_TEXT_SIZE);
    }

    for (int32_t option = 0; option < REPEAT_OPTION_COUNT; option++) {
        if (repeat_option_units[option] == event->repeat_unit) {
            form->repeat_option = option;
        }
    }
    assert(repeat_option_units[form->repeat_option] == event->repeat_unit);
    if (event->repeat_unit == REPEAT_DAY) {
        snprintf(form->text[FORM_FIELD_REPEAT_DAYS], REPEAT_DAYS_TEXT_SIZE, "%" PRId32,
                 event->repeat_interval);
    }
    if (event->repeat_has_end) {
        date_format_iso(event->repeat_end_date, form->text[FORM_FIELD_REPEAT_END_DATE],
                        DATE_ISO_SIZE);
    }
    // Start with no field in edit mode, so that a click on Save or Cancel acts at once.
    form->field_active = FORM_FIELD_NONE;
}

static void form_close(Form *form) {
    assert(form->open);
    form->open = false;
    form->field_active = FORM_FIELD_NONE;
}

// The field after the active one for Tab, skipping the days field while it is hidden.
static int32_t form_field_next(const Form *form, bool backwards) {
    assert(form->field_active >= 0);
    assert(form->field_active < FORM_FIELD_COUNT);
    int32_t step = backwards ? FORM_FIELD_COUNT - 1 : 1;
    int32_t field = (form->field_active + step) % FORM_FIELD_COUNT;
    // Bounded: the title field is always visible, so at most FORM_FIELD_COUNT steps are taken.
    for (int32_t i = 0; i < FORM_FIELD_COUNT && !form_field_visible(form, field); i++) {
        field = (field + step) % FORM_FIELD_COUNT;
    }
    assert(form_field_visible(form, field));
    return field;
}

static bool form_field_visible(const Form *form, int32_t field) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    if (field == FORM_FIELD_REPEAT_DAYS) {
        return form->repeat_option == REPEAT_OPTION_DAYS;
    }
    if (field == FORM_FIELD_REPEAT_END_DATE) {
        return form->repeat_option != REPEAT_OPTION_NONE;
    }
    return true;
}

// Fills event from the form. Returns NULL on success, or a message saying what to fix.
static const char *form_parse(const Form *form, Event *event) {
    assert(event->id == 0);
    // Leading spaces would let a blank-looking title through.
    const char *title = form->text[FORM_FIELD_TITLE];
    title += strspn(title, " ");
    if (title[0] == '\0') {
        return "Enter a title.";
    }
    snprintf(event->title, sizeof(event->title), "%s", title);

    if (!date_parse_iso(form->text[FORM_FIELD_START_DATE], &event->start_date)) {
        return "Start date must be YYYY-MM-DD.";
    }
    if (!form_parse_time(form->text[FORM_FIELD_START_TIME], &event->start_minute)) {
        return "Start time must be HH:MM.";
    }
    if (!date_parse_iso(form->text[FORM_FIELD_END_DATE], &event->end_date)) {
        return "End date must be YYYY-MM-DD.";
    }
    if (!form_parse_time(form->text[FORM_FIELD_END_TIME], &event->end_minute)) {
        return "End time must be HH:MM.";
    }
    assert(form->repeat_option >= 0);
    assert(form->repeat_option < REPEAT_OPTION_COUNT);
    assert(form->kind >= 0);
    assert(form->kind < EVENT_KIND_COUNT);
    event->kind = form->kind;
    event->repeat_unit = repeat_option_units[form->repeat_option];
    event->repeat_interval = 1;
    if (form->repeat_option == REPEAT_OPTION_DAYS) {
        if (!form_parse_repeat_days(form->text[FORM_FIELD_REPEAT_DAYS],
                                    &event->repeat_interval)) {
            return "Days must be 1 to 999.";
        }
    }
    // Ignored for one-time events, where the field is hidden.
    const char *repeat_end = form->text[FORM_FIELD_REPEAT_END_DATE];
    event->repeat_has_end = event->repeat_unit != REPEAT_NONE && repeat_end[0] != '\0';
    if (event->repeat_has_end) {
        if (!date_parse_iso(repeat_end, &event->repeat_end_date)) {
            return "Repeat until must be YYYY-MM-DD.";
        }
    }

    int32_t start_day = date_to_day_number(event->start_date);
    int32_t end_day = date_to_day_number(event->end_date);
    if (end_day < start_day) {
        return "End date is before start.";
    }
    if (end_day == start_day) {
        if (event->start_minute != TIME_NONE && event->end_minute != TIME_NONE) {
            if (event->end_minute < event->start_minute) {
                return "End time is before start.";
            }
        }
    }
    if (event->repeat_has_end) {
        if (date_to_day_number(event->repeat_end_date) < start_day) {
            return "Repeat until is before start.";
        }
    }
    // Overlapping occurrences would make a day show the same event twice.
    if (event_duration_days(event) >= event_repeat_gap_days_min(event)) {
        return "Event must end before it repeats.";
    }
    return NULL;
}

// An empty field means the event has no time of day at that end.
static bool form_parse_time(const char *text, int32_t *minute_of_day) {
    if (text[0] == '\0') {
        *minute_of_day = TIME_NONE;
        return true;
    }
    return time_parse(text, minute_of_day);
}

// Digits only, so that signs, spaces and zero are rejected.
static bool form_parse_repeat_days(const char *text, int32_t *days) {
    int32_t result = 0;
    int32_t digit_count = 0;
    for (int32_t i = 0; i < REPEAT_DAYS_TEXT_SIZE && text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        result = result * 10 + (text[i] - '0');
        digit_count++;
    }
    if (digit_count == 0 || result < 1 || result > REPEAT_INTERVAL_MAX) {
        return false;
    }
    *days = result;
    return true;
}

static bool event_occurs_on_day(const Event *event, int32_t day_number) {
    int32_t start_day = 0;
    return event_occurrence_on_day(event, day_number, &start_day);
}

static bool event_created_on_day(const Event *event, int32_t day_number) {
    return date_to_day_number(event->created_date) == day_number;
}

// One more line for a repeating event that ends, to say when.
static float event_card_height(const Event *event) {
    float extra = event->repeat_has_end ? (float)TEXT_LINE_HEIGHT_PIXELS_SMALL : 0.0f;
    return (float)EVENT_CARD_HEIGHT_PIXELS + extra;
}

static Color event_color(const Event *event) {
    assert(event->id > 0);
    return event_colors[event->id % EVENT_COLOR_COUNT];
}

// The dates and times, followed by how the event repeats, if it does.
static void event_format_range(const Event *event, char *buffer, size_t buffer_size) {
    event_format_range_dates(event, buffer, buffer_size);
    // The longest is "every 999 days".
    char repeat[32];
    event_format_repeat(event, repeat, sizeof(repeat));
    if (repeat[0] != '\0') {
        size_t length = strlen(buffer);
        assert(length < buffer_size);
        // Cutting the text short is acceptable: the card clips long lines anyway.
        int written = snprintf(buffer + length, buffer_size - length, ", %s", repeat);
        assert(written > 0);
    }
}

static void event_format_range_dates(const Event *event, char *buffer, size_t buffer_size) {
    char start[TEXT_LINE_SIZE] = "";
    char end[TEXT_LINE_SIZE] = "";
    if (date_equal(event->start_date, event->end_date)) {
        if (event->start_minute == TIME_NONE && event->end_minute == TIME_NONE) {
            snprintf(buffer, buffer_size, "All day");
            return;
        }
        if (event->start_minute != TIME_NONE) {
            time_format(event->start_minute, start, sizeof(start));
        }
        if (event->end_minute != TIME_NONE) {
            time_format(event->end_minute, end, sizeof(end));
        }
    } else {
        format_date_time_short(event->start_date, event->start_minute, start, sizeof(start));
        format_date_time_short(event->end_date, event->end_minute, end, sizeof(end));
    }

    if (end[0] == '\0') {
        snprintf(buffer, buffer_size, "From %s", start);
    } else if (start[0] == '\0') {
        snprintf(buffer, buffer_size, "Until %s", end);
    } else {
        snprintf(buffer, buffer_size, "%s - %s", start, end);
    }
}

static void event_format_created(const Event *event, char *buffer, size_t buffer_size) {
    char time[TIME_TEXT_SIZE];
    time_format(event->created_minute, time, sizeof(time));
    snprintf(buffer, buffer_size, "Created %" PRId32 " %s %" PRId32 ", %s",
             event->created_date.day, month_short_name(event->created_date.month),
             event->created_date.year, time);
}

// Days between creation and start. On the start day itself, hours and minutes when the
// start has a time, because "0 days" says nothing. A repeating event is measured to the
// occurrence shown, which "this date" says, because "start" would suggest the first one.
static void event_format_lead(const Event *event, char *buffer, size_t buffer_size) {
    const char *target = event->repeat_unit == REPEAT_NONE ? "start" : "this date";
    int32_t lead_days = date_to_day_number(event->start_date) -
                        date_to_day_number(event->created_date);
    if (lead_days > 0) {
        snprintf(buffer, buffer_size, "%" PRId32 " day%s before %s", lead_days,
                 lead_days == 1 ? "" : "s", target);
        return;
    }
    if (lead_days < 0) {
        snprintf(buffer, buffer_size, "Added %" PRId32 " day%s after %s", -lead_days,
                 lead_days == -1 ? "" : "s", target);
        return;
    }
    if (event->start_minute == TIME_NONE) {
        snprintf(buffer, buffer_size, "%s",
                 event->repeat_unit == REPEAT_NONE ? "Created on the start day"
                                                   : "Created on this date");
        return;
    }

    int32_t lead_minutes = event->start_minute - event->created_minute;
    int32_t magnitude = lead_minutes < 0 ? -lead_minutes : lead_minutes;
    const char *direction = lead_minutes < 0 ? "after" : "before";
    snprintf(buffer, buffer_size, "%" PRId32 " h %02" PRId32 " min %s %s", magnitude / 60,
             magnitude % 60, direction, target);
}

// "15 Oct" or "15 Oct 09:00".
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size) {
    if (minute_of_day == TIME_NONE) {
        snprintf(buffer, buffer_size, "%" PRId32 " %s", date.day, month_short_name(date.month));
        return;
    }
    char time[TIME_TEXT_SIZE];
    time_format(minute_of_day, time, sizeof(time));
    snprintf(buffer, buffer_size, "%" PRId32 " %s %s", date.day, month_short_name(date.month),
             time);
}

static Color style_color(int32_t property) {
    return GetColor((unsigned int)GuiGetStyle(DEFAULT, property));
}

// Positions are rounded to whole pixels, so that glyphs map one to one onto screen pixels and
// stay sharp.
static void draw_text(const TextStyle *style, const char *text, float x, float y, Color color) {
    assert(x >= 0.0f);
    assert(y >= 0.0f);
    Vector2 position = { (float)(int32_t)(x + 0.5f), (float)(int32_t)(y + 0.5f) };
    DrawTextEx(style->font, text, position, style->size_pixels,
               (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

static Vector2 measure_text(const TextStyle *style, const char *text) {
    return MeasureTextEx(style->font, text, style->size_pixels,
                         (float)GuiGetStyle(DEFAULT, TEXT_SPACING));
}

static void draw_text_centered(const TextStyle *style, const char *text, Rectangle area,
                               Color color) {
    Vector2 text_size = measure_text(style, text);
    draw_text(style, text, area.x + (area.width - text_size.x) / 2,
              area.y + (area.height - text_size.y) / 2, color);
}
