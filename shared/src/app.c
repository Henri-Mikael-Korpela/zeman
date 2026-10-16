#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "raygui.h"

#include "app.h"
#include "backend.h"
#include "calendar.h"
#include "event.h"

enum {
    // All sizes are layout pixels, which AppConfig.scale turns into screen pixels.
    // Each size is rasterized once at startup (see fonts_load), so that text is drawn at the
    // size its glyphs were made for. Calendar entries and cards use the small size, so that
    // more of each title fits.
    TEXT_SIZE_PIXELS = 18,
    TEXT_SIZE_PIXELS_SMALL = 15,
    TEXT_SIZE_PIXELS_HEADING = 28,
    TEXT_LINE_HEIGHT_PIXELS = 24,
    TEXT_LINE_HEIGHT_PIXELS_SMALL = 20,
    PADDING_PIXELS = 16,
    // Less padding on a phone, where every pixel of width counts.
    PADDING_PIXELS_PORTRAIT = 8,
    // In the portrait layout, the share of the height the calendar grid takes; the panel gets
    // the rest.
    GRID_HEIGHT_PERCENT_PORTRAIT = 46,
    HEADER_HEIGHT_PIXELS = 40,
    WEEKDAY_HEIGHT_PIXELS = 32,
    STATUS_HEIGHT_PIXELS = 28,
    PANEL_WIDTH_PIXELS = 400,
    BUTTON_HEIGHT_PIXELS = 36,
    // Form rows are compact so that all of them, and the buttons, fit in the panel at the
    // minimum window height.
    FIELD_HEIGHT_PIXELS = 28,
    FORM_LABEL_HEIGHT_PIXELS = 20,
    FORM_ROW_GAP_PIXELS = 8,
    // Title, start, end, location, description, repeat and repeat until. The rows keep their
    // place for every kind of entry, so that the buttons below never move.
    FORM_ROW_COUNT = 7,
    // The Month/Week/Day selector: one toggle per range.
    RANGE_ITEM_WIDTH_PIXELS = 72,
    RANGE_BAR_HEIGHT_PIXELS = 28,
    GRID_COLUMN_COUNT = 7,
    // Six weeks always cover a whole month, whatever weekday it starts on.
    GRID_ROW_COUNT = 6,
    GRID_CELL_COUNT = GRID_COLUMN_COUNT * GRID_ROW_COUNT,
    // Card text starts 16 pixels in from the left and keeps 12 from the right edge.
    CARD_TEXT_MARGIN_PIXELS = 28,
    // Longer descriptions are cut off with "..." on the last line.
    DESCRIPTION_LINES_MAX = 3,
    // Day number and creation markers share the top line of a cell; bars start below it.
    CELL_TOP_LINE_PIXELS = 32,
    EVENT_BAR_HEIGHT_PIXELS = 19,
    EVENT_BAR_GAP_PIXELS = 2,
    // Four small lines, with 8 pixels above and below.
    EVENT_CARD_HEIGHT_PIXELS = 8 + 4 * TEXT_LINE_HEIGHT_PIXELS_SMALL + 8,
    EVENT_CARD_GAP_PIXELS = 8,
    CREATION_MARKER_RADIUS_PIXELS = 5,
    // Room kept free for the day number, and today's circle around it, left of the creation
    // markers. The markers that fit beside it depend on the cell width; past that a "+" is drawn.
    DAY_NUMBER_WIDTH_PIXELS = 44,
    LEGEND_HEIGHT_PIXELS = 56,
    // The panel's list scrolls this far per wheel notch: about three lines of a card.
    PANEL_WHEEL_STEP_PIXELS = 48,
    // A press that moves this far scrolls the list instead of clicking, because a finger
    // wobbles by a few pixels during a tap.
    PANEL_DRAG_THRESHOLD_PIXELS = 8,
    // The bar showing the list's scroll position, and the room kept free for it beside the cards.
    SCROLLBAR_WIDTH_PIXELS = 4,
    SCROLLBAR_GAP_PIXELS = 10,
    // Events loaded for one six-week view. If more match, the status bar says so.
    EVENT_COUNT_MAX = 512,
    // Task completions loaded for one view, including a year before it (see
    // BackendOps.load_completions). More than this is reported like too many events.
    COMPLETION_COUNT_MAX = 1024,
    STATUS_SIZE = 192,
    INFO_SIZE = 192,
    FORM_ERROR_SIZE = 160,
    TEXT_LINE_SIZE = 128,
    PATH_SIZE = 1024,
    // Added to pointer positions while input is off, which puts them far outside any layout.
    INPUT_OFF_MOUSE_OFFSET_PIXELS = -1000000,
    // Text is never drawn this far from the layout's corner. Small enough that, times the
    // largest phone scale, rounding stays within int32_t and float keeps whole pixels exact.
    TEXT_POSITION_LIMIT_PIXELS = 100000,
    // Printable ASCII and Latin-1, so that titles with letters such as a-umlaut display.
    FONT_CODEPOINT_COUNT = (126 - 32 + 1) + (255 - 160 + 1),
};

// Downloaded from Google Fonts by CMake, copied next to the desktop executable and into the
// Android APK's assets, so that the app needs no network for them at run time.
static const char FONT_FILE_REGULAR[] = "fonts/Rubik-Regular.ttf";
static const char FONT_FILE_MEDIUM[] = "fonts/Rubik-Medium.ttf";

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
    FORM_FIELD_LOCATION_ADDRESS,  // Location fields are optional; empty means no location
    FORM_FIELD_LOCATION_CITY,
    FORM_FIELD_DESCRIPTION,       // Optional free text
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
    LOCATION_ADDRESS_SIZE, LOCATION_CITY_SIZE, EVENT_DESCRIPTION_SIZE, REPEAT_DAYS_TEXT_SIZE,
    DATE_ISO_SIZE,
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
static_assert((int)DATE_ISO_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)TIME_TEXT_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)EVENT_TITLE_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_ADDRESS_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_CITY_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");

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
// Every problem has this color, darker than any event color, so that problems read as problems
// at a glance rather than as one more event.
static const Color problem_color = { 130, 20, 20, 255 };

// How many days the calendar shows at once. Every range uses the same day cells; a week is one
// row of seven, and a day one cell the width of the calendar, so that whole titles show.
typedef enum CalendarRange {
    RANGE_MONTH,
    RANGE_WEEK,
    RANGE_DAY,
    RANGE_COUNT,
} CalendarRange;

// raygui separates toggle group items with semicolons; the order is CalendarRange's.
static const char range_labels[] = "Month;Week;Day";

typedef struct KeyStep {
    int32_t key;     // raylib KeyboardKey
    int32_t days;
} KeyStep;

// The arrow keys move the selection by a day or a week, whatever the range; Page Up and Page
// Down move by the range (see range_step).
static const KeyStep key_steps[] = {
    { KEY_LEFT, -1 },
    { KEY_RIGHT, 1 },
    { KEY_UP, -7 },
    { KEY_DOWN, 7 },
};
enum { KEY_STEP_COUNT = sizeof(key_steps) / sizeof(key_steps[0]) };

typedef struct Form {
    int64_t editing_id;    // Event being edited, or 0 for a new event
    int32_t field_active;  // FormField being edited, or FORM_FIELD_NONE
    int repeat_option;     // RepeatOption; int because raygui's combo box writes it
    int kind;              // EventKind; int because raygui's toggle group writes it
    bool open;
    char text[FORM_FIELD_COUNT][EVENT_DESCRIPTION_SIZE];
    char error[FORM_ERROR_SIZE];  // Why the last save failed, or empty
} Form;

// A font together with the one size it is drawn at. raylib rasterizes a font once, so drawing
// it at another size would blur or pixelate it.
typedef struct TextStyle {
    Font font;
    float size_pixels;  // Layout pixels
    float scale;        // Screen pixels per layout pixel that the font was rasterized for
} TextStyle;

typedef struct Fonts {
    TextStyle ui;            // Controls, labels and day numbers; also raygui's font
    TextStyle small;         // Calendar entries and the details on panel cards
    TextStyle small_strong;  // Titles on panel cards
    TextStyle heading;       // Month title and the selected date
    // Screen pixels per layout pixel. Kept with the fonts, which are rasterized at this scale,
    // because every function that draws text receives them.
    float scale;
    // Screen position of the layout's top-left corner during app_frame, for scissor rectangles,
    // which raylib takes in screen pixels.
    Vector2 origin;
    // Layout rectangle that scissor rectangles stay within while clipping is true: the panel's
    // scrolling list. raylib has a single scissor rectangle, so a card's own clipping inside the
    // list must be intersected with it rather than replace it.
    Rectangle clip;
    bool clipping;
    bool loaded;             // False when Rubik was missing and raylib's default font is used
} Fonts;

typedef struct App {
    Event events[EVENT_COUNT_MAX];  // Events of the visible six weeks, ordered by start
    TaskCompletion completions[COMPLETION_COUNT_MAX];  // Done task occurrences near the view
    Backend *backend;
    Fonts fonts;
    uint64_t events_revision;       // Backend revision that events[] was loaded at
    int64_t event_focused_id;       // Selected item on the panel, ringed in the grid, or 0
    int64_t event_delete_id;        // Event waiting for the user to confirm deleting, or 0
    Date today;
    Date selected;
    Date events_month;              // First day of the month that events[] was loaded for
    // First day shown, set by app_sync. The six weeks loaded for a month contain every week and
    // day within it, so week and day views need no loading of their own.
    Date visible_first;
    int32_t event_count;
    int range;                      // CalendarRange; int because raygui's toggle group writes it
    int32_t completion_count;
    float panel_scroll_pixels;      // How far the panel's list is scrolled down
    float panel_drag_y;             // Pointer y where the press in the list began
    float panel_drag_scroll_pixels; // panel_scroll_pixels when that press began
    Form form;
    bool events_stale;              // Reload events[] even though the month is unchanged
    bool events_truncated;
    bool touch;                     // See AppConfig
    bool input_enabled;             // See app_set_input_enabled
    bool panel_pressed;             // The pointer went down in the panel's list and is held
    bool panel_dragging;            // That press moved far enough to scroll instead of click
    bool created;
    char status[STATUS_SIZE];       // Message for the status bar, cleared on navigation
    char info[INFO_SIZE];           // Shown first in the status bar, see app_set_info
} App;

// A text broken into lines no wider than a given width: where each line starts in the text and
// how long it is. The text is not copied.
typedef struct TextLines {
    int32_t start[DESCRIPTION_LINES_MAX];
    int32_t length[DESCRIPTION_LINES_MAX];
    int32_t count;
    bool cut;  // The text continues past the last line
} TextLines;

typedef struct Layout {
    Rectangle header;
    Rectangle range_bar;  // The Month/Week/Day selector; empty when not shown
    Rectangle weekdays;
    Rectangle grid;
    Rectangle panel;
    Rectangle status;
    float cell_width;
    float cell_height;
    int32_t columns;    // Days across, 7 or 1
    int32_t rows;       // Weeks down, 6 or 1
    bool grid_visible;  // False in the portrait layout while the form takes the screen
    bool portrait;
} Layout;

static void fonts_load(Fonts *fonts, const char *directory, float scale);
static TextStyle fonts_load_style(const char *directory, const char *file_name,
                                  int32_t size_pixels, float scale, const int *codepoints,
                                  bool *loaded);
static void fonts_unload(Fonts *fonts);
static void app_handle_keys(App *app, const Layout *layout);
static void app_select(App *app, Date date);
static void app_sync(App *app);
static void app_load_events(App *app, Date month, uint64_t revision);
static const Event *app_event_find(const App *app, int64_t id);
static int64_t app_event_first_on_day(const App *app, int32_t day_number);
static void app_form_submit(App *app);
static void app_event_delete(App *app, int64_t id);
static bool app_task_done(const App *app, int64_t id, int32_t start_day);
static void app_task_set_done(App *app, const Event *event, int32_t start_day, bool done);
static Layout layout_compute(float width, float height, bool form_open, int32_t range);
static Layout layout_compute_portrait(float width, float height, bool form_open,
                                      int32_t range);
static void layout_set_grid(Layout *layout, int32_t range);
static int32_t range_day_count(int32_t range);
static Date range_step(Date date, int32_t range, int32_t direction);
static Date range_first_date(const App *app);
static void range_title(const App *app, char *buffer, size_t buffer_size);
static int32_t app_visible_item_count(const App *app);
static Rectangle grid_cell_rectangle(const Layout *layout, int32_t cell_index);
static Date grid_first_date(Date month);
static void draw_header(App *app, const Layout *layout);
static void draw_weekdays(const Fonts *fonts, const Layout *layout, Date first);
static void draw_grid(App *app, const Layout *layout);
static const Event *focused_event_find(const App *app);
static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Event *focused);
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
static float draw_panel_events_height(const App *app, float card_width);
static void draw_panel_events_scroll(App *app, Rectangle list, float content_height);
static void draw_panel_events_list(App *app, Rectangle list);
static void draw_panel_events_scrollbar(Rectangle list, float content_height,
                                        float scroll_pixels);
static bool draw_panel_event_card(App *app, const Event *event, Rectangle card);
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event,
                                          Rectangle card);
static void draw_panel_legend(const Fonts *fonts, Rectangle area);
static void draw_panel_form(App *app, Rectangle body);
static float draw_panel_form_fields(App *app, Rectangle body, float y);
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y);
static void draw_panel_form_field(const Fonts *fonts, Form *form, int32_t field,
                                  const char *label, Rectangle box);
static void draw_status(App *app, Rectangle status);
static void form_open(Form *form, Date date, int32_t kind);
static void form_open_edit(Form *form, const Event *event);
static void form_close(Form *form);
static int32_t form_field_next(const Form *form, bool backwards);
static bool form_field_visible(const Form *form, int32_t field);
static float event_card_height(const Fonts *fonts, const Event *event, float card_width);
static TextLines text_wrap(const TextStyle *style, const char *text, float width_pixels);
static bool text_line_fits(const TextStyle *style, const char *text, int32_t length,
                           float width_pixels);
static const char *form_parse(const Form *form, Date today, Event *event);
static const char *form_parse_problem(const Form *form, Date today, Event *event);
static const char *form_parse_order(const Event *event);
static bool form_parse_time(const char *text, int32_t *minute_of_day);
static bool form_parse_repeat_days(const char *text, int32_t *days);
static bool event_occurs_on_day(const Event *event, int32_t day_number);
static bool event_created_on_day(const Event *event, int32_t day_number);
static Color event_color(const Event *event);
static void event_format_range(const Event *event, char *buffer, size_t buffer_size);
static void event_format_range_dates(const Event *event, char *buffer, size_t buffer_size);
static void event_format_created(const Event *event, char *buffer, size_t buffer_size);
static void event_format_location(const Event *event, char *buffer, size_t buffer_size);
static void text_copy_trimmed(char *buffer, size_t buffer_size, const char *text);
static void event_format_lead(const Event *event, char *buffer, size_t buffer_size);
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size);
static Color style_color(int32_t property);
static void draw_text(const TextStyle *style, const char *text, float x, float y, Color color);
static float draw_text_round(float position, float scale);
static Vector2 measure_text(const TextStyle *style, const char *text);
static void draw_text_centered(const TextStyle *style, const char *text, Rectangle area,
                               Color color);
static void scissor_begin(const Fonts *fonts, float x, float y, float width, float height);
static void scissor_end(const Fonts *fonts);

App *app_create(Backend *backend, const AppConfig *config) {
    // Static rather than on the stack, because App holds every loaded event; one per program.
    static App app;
    assert(!app.created);
    assert(backend != NULL);
    assert(config->scale > 0.0f);
    app.created = true;
    app.backend = backend;
    app.touch = config->touch;
    app.input_enabled = true;

    fonts_load(&app.fonts, config->font_directory, config->scale);
    GuiSetFont(app.fonts.ui.font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, TEXT_SIZE_PIXELS);
    // Rubik's advances already space its glyphs; raylib's default font needs extra room.
    GuiSetStyle(DEFAULT, TEXT_SPACING, app.fonts.loaded ? 0 : 2);
    if (!app.fonts.loaded) {
        snprintf(app.status, sizeof(app.status), "Rubik font not found: using the default.");
    }

    app.today = date_today();
    app.selected = app.today;
    app.events_month = (Date){ app.today.year, app.today.month, 1 };
    app.events_stale = true;
    app.form.field_active = FORM_FIELD_NONE;
    return &app;
}

void app_destroy(App *app) {
    assert(app->created);
    fonts_unload(&app->fonts);
    app->created = false;
}

void app_frame(App *app, Rectangle area) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);
    const float scale = app->fonts.scale;
    // Layout pixels start at the area's corner: mouse positions, drawing and scissor
    // rectangles are all moved and scaled the same way, so raygui works unchanged.
    app->fonts.origin = (Vector2){ area.x, area.y };
    SetMouseScale(1.0f / scale, 1.0f / scale);
    Layout layout = layout_compute(area.width / scale, area.height / scale, app->form.open,
                                   app->range);
    if (app->input_enabled) {
        SetMouseOffset(-(int)area.x, -(int)area.y);
        app_handle_keys(app, &layout);
    } else {
        // The pointer is moved far away, so that nothing is hovered, clicked, scrolled or
        // swiped, and raygui is locked; one place covers every control instead of each.
        SetMouseOffset(INPUT_OFF_MOUSE_OFFSET_PIXELS, INPUT_OFF_MOUSE_OFFSET_PIXELS);
        GuiLock();
    }
    app_sync(app);
    // Again, because a key may have changed the range or opened the form.
    layout = layout_compute(area.width / scale, area.height / scale, app->form.open, app->range);

    // The fonts were rasterized at this scale, so text stays sharp; on the desktop the scale is
    // 1 and only the offset matters.
    BeginMode2D((Camera2D){ .offset = { area.x, area.y }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
    draw_header(app, &layout);
    if (layout.grid_visible) {
        draw_weekdays(&app->fonts, &layout, app->visible_first);
        draw_grid(app, &layout);
    }
    draw_panel(app, layout.panel);
    draw_status(app, layout.status);
    EndMode2D();
    if (!app->input_enabled) {
        GuiUnlock();
    }
    app->fonts.origin = (Vector2){ 0, 0 };
    SetMouseOffset(0, 0);
    SetMouseScale(1.0f, 1.0f);
}

void app_draw_text(const App *app, AppText style, const char *text, float x, float y) {
    switch (style) {
    case APP_TEXT_HEADING:
        draw_text(&app->fonts.heading, text, x, y, style_color(TEXT_COLOR_NORMAL));
        return;
    case APP_TEXT_NORMAL:
        draw_text(&app->fonts.ui, text, x, y, style_color(TEXT_COLOR_NORMAL));
        return;
    case APP_TEXT_DIM:
        draw_text(&app->fonts.ui, text, x, y, style_color(TEXT_COLOR_DISABLED));
        return;
    case APP_TEXT_SMALL:
        draw_text(&app->fonts.small, text, x, y, style_color(TEXT_COLOR_NORMAL));
        return;
    }
    assert(false); // Not an AppText.
}

float app_measure_text(const App *app, AppText style, const char *text) {
    switch (style) {
    case APP_TEXT_HEADING:
        return measure_text(&app->fonts.heading, text).x;
    case APP_TEXT_NORMAL:
    case APP_TEXT_DIM:
        return measure_text(&app->fonts.ui, text).x;
    case APP_TEXT_SMALL:
        return measure_text(&app->fonts.small, text).x;
    }
    assert(false); // Not an AppText.
    return 0.0f;
}

void app_set_info(App *app, const char *info) {
    snprintf(app->info, sizeof(app->info), "%s", info);
}

void app_set_status(App *app, const char *status) {
    snprintf(app->status, sizeof(app->status), "%s", status);
}

bool app_text_input_active(const App *app) {
    return app->form.open && app->form.field_active != FORM_FIELD_NONE;
}

void app_set_input_enabled(App *app, bool enabled) {
    assert(app->created);
    app->input_enabled = enabled;
}

// Loads Rubik at each size it is drawn at, times the scale, so that each glyph covers exactly
// the screen pixels it is drawn on. Needs the window, because glyphs go to a texture.
static void fonts_load(Fonts *fonts, const char *directory, float scale) {
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
    fonts->scale = scale;
    fonts->ui = fonts_load_style(directory, FONT_FILE_REGULAR, TEXT_SIZE_PIXELS, scale,
                                 codepoints, &fonts->loaded);
    fonts->small = fonts_load_style(directory, FONT_FILE_REGULAR, TEXT_SIZE_PIXELS_SMALL, scale,
                                    codepoints, &fonts->loaded);
    fonts->small_strong = fonts_load_style(directory, FONT_FILE_MEDIUM, TEXT_SIZE_PIXELS_SMALL,
                                           scale, codepoints, &fonts->loaded);
    fonts->heading = fonts_load_style(directory, FONT_FILE_MEDIUM, TEXT_SIZE_PIXELS_HEADING,
                                      scale, codepoints, &fonts->loaded);
}

// Falls back to raylib's default font, scaled, when the file is missing or unreadable, so that
// the calendar still works; *loaded then becomes false.
static TextStyle fonts_load_style(const char *directory, const char *file_name,
                                  int32_t size_pixels, float scale, const int *codepoints,
                                  bool *loaded) {
    assert(size_pixels > 0);
    assert(scale > 0.0f);
    TextStyle style = { GetFontDefault(), (float)size_pixels, scale };
    char path[PATH_SIZE];
    int written = snprintf(path, sizeof(path), "%s%s", directory, file_name);
    if (written < 0 || written >= (int)sizeof(path)) {
        *loaded = false;
        return style;
    }
    // The texture holds glyphs at screen size; size_pixels stays in layout pixels.
    int32_t raster_pixels = (int32_t)((float)size_pixels * scale + 0.5f);
    // raylib takes a non-const pointer but only reads the codepoints.
    Font font = LoadFontEx(path, raster_pixels, (int *)codepoints, FONT_CODEPOINT_COUNT);
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
    fonts->loaded = false;
}

static void app_handle_keys(App *app, const Layout *layout) {
    // Android's Back button does what Escape does on the desktop.
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK)) {
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

    if (IsKeyPressed(KEY_M)) {
        app->range = RANGE_MONTH;
    }
    if (IsKeyPressed(KEY_W)) {
        app->range = RANGE_WEEK;
    }
    if (IsKeyPressed(KEY_D)) {
        app->range = RANGE_DAY;
    }
    Date target = app->selected;
    for (int32_t i = 0; i < KEY_STEP_COUNT; i++) {
        if (IsKeyPressed(key_steps[i].key)) {
            target = date_add_days(target, key_steps[i].days);
        }
    }
    if (IsKeyPressed(KEY_PAGE_UP)) {
        target = range_step(target, app->range, -1);
    }
    if (IsKeyPressed(KEY_PAGE_DOWN)) {
        target = range_step(target, app->range, 1);
    }
    if (IsKeyPressed(KEY_HOME)) {
        target = app->today;
    }
    // Over the grid only, so that they mean nothing over the panel: a sideways swipe on a touch
    // screen, or the wheel, moves by the range.
    if (CheckCollisionPointRec(GetMousePosition(), layout->grid)) {
        if (app->touch && IsGestureDetected(GESTURE_SWIPE_LEFT)) {
            target = range_step(target, app->range, 1);
        }
        if (app->touch && IsGestureDetected(GESTURE_SWIPE_RIGHT)) {
            target = range_step(target, app->range, -1);
        }
        float wheel = GetMouseWheelMove();
        if (wheel > 0.0f) {
            target = range_step(target, app->range, -1);
        }
        if (wheel < 0.0f) {
            target = range_step(target, app->range, 1);
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
    // Another day has other cards, so its list starts from the top.
    app->panel_scroll_pixels = 0.0f;
    app->status[0] = '\0';
}

// Brings derived state up to date before drawing: today's date, the loaded events and the
// focused event, which is always one of the events on the selected date.
static void app_sync(App *app) {
    app->today = date_today();
    Date month = { app->selected.year, app->selected.month, 1 };
    // A new revision means a change from anywhere: this app, the desktop, or another phone.
    uint64_t revision = backend_revision(app->backend);
    if (revision != app->events_revision) {
        app->events_stale = true;
    }
    if (app->events_stale || !date_equal(month, app->events_month)) {
        app_load_events(app, month, revision);
    }
    // After loading, because a month view starts from the loaded month.
    app->visible_first = range_first_date(app);

    int32_t selected_day = date_to_day_number(app->selected);
    const Event *focused = app_event_find(app, app->event_focused_id);
    if (focused == NULL || !event_occurs_on_day(focused, selected_day)) {
        app->event_focused_id = app_event_first_on_day(app, selected_day);
    }
}

// Also records the revision when loading fails, so that a failure is not retried every frame;
// the next change of revision, or of month, tries again.
static void app_load_events(App *app, Date month, uint64_t revision) {
    assert(month.day == 1);
    app->events_month = month;
    app->events_revision = revision;
    app->events_stale = false;
    app->events_truncated = false;
    app->event_count = 0;
    app->completion_count = 0;

    Date first = grid_first_date(month);
    Date last = date_add_days(first, GRID_CELL_COUNT - 1);
    int32_t count = backend_load_range(app->backend, first, last, app->events, EVENT_COUNT_MAX,
                                       &app->events_truncated);
    if (count < 0) {
        snprintf(app->status, sizeof(app->status), "%s", backend_error(app->backend));
        return;
    }
    assert(count <= EVENT_COUNT_MAX);
    app->event_count = count;

    bool completions_truncated = false;
    count = backend_load_completions(app->backend, first, last, app->completions,
                                     COMPLETION_COUNT_MAX, &completions_truncated);
    if (count < 0) {
        snprintf(app->status, sizeof(app->status), "%s", backend_error(app->backend));
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
    if (!backend_set_task_done(app->backend, event->id, occurrence_start, done)) {
        snprintf(app->status, sizeof(app->status), "%s", backend_error(app->backend));
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
    const char *mistake = form_parse(&app->form, app->today, &event);
    if (mistake != NULL) {
        snprintf(app->form.error, sizeof(app->form.error), "%s", mistake);
        return;
    }
    // Typing the address and city of an existing location links to it, so that events at the
    // same place share one location.
    bool has_location = event.location.address[0] != '\0' || event.location.city[0] != '\0';
    if (has_location) {
        event.location.id = backend_location_find_or_create(
            app->backend, event.location.address, event.location.city);
        if (event.location.id == 0) {
            snprintf(app->form.error, sizeof(app->form.error), "%s", backend_error(app->backend));
            return;
        }
    }
    bool editing = app->form.editing_id != 0;
    int64_t id = app->form.editing_id;
    if (editing) {
        event.id = id;
        if (!backend_update(app->backend, &event)) {
            snprintf(app->form.error, sizeof(app->form.error), "%s", backend_error(app->backend));
            return;
        }
    } else {
        id = backend_insert(app->backend, &event);
        if (id == 0) {
            snprintf(app->form.error, sizeof(app->form.error), "%s", backend_error(app->backend));
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

    if (!backend_remove(app->backend, id)) {
        snprintf(app->status, sizeof(app->status), "%s", backend_error(app->backend));
        // The event may be gone, so show the real state.
        app->events_stale = true;
        return;
    }
    // events[] is reloaded by the next app_sync; this frame still draws the old list.
    app->event_focused_id = 0;
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "Deleted \"%s\".", title);
}

// width and height are in layout pixels. A window taller than wide, such as a phone, gets the
// portrait layout; otherwise the panel sits to the right of the calendar.
static Layout layout_compute(float width, float height, bool form_open, int32_t range) {
    if (width < height) {
        return layout_compute_portrait(width, height, form_open, range);
    }
    const float padding = (float)PADDING_PIXELS;
    const float status_height = (float)STATUS_HEIGHT_PIXELS;
    Layout layout = { .grid_visible = true, .portrait = false };
    layout.status = (Rectangle){ 0, height - status_height, width, status_height };
    layout.panel = (Rectangle){ width - padding - PANEL_WIDTH_PIXELS, padding,
                                PANEL_WIDTH_PIXELS, height - status_height - 2 * padding };

    float calendar_width = layout.panel.x - 2 * padding;
    layout.header = (Rectangle){ padding, padding, calendar_width, HEADER_HEIGHT_PIXELS };
    // The range selector at the right end of the header, centered in its height.
    const float range_width = RANGE_COUNT * (RANGE_ITEM_WIDTH_PIXELS + 2) - 2;
    layout.range_bar = (Rectangle){ padding + calendar_width - range_width,
                                    padding + (HEADER_HEIGHT_PIXELS - RANGE_BAR_HEIGHT_PIXELS) / 2,
                                    range_width, RANGE_BAR_HEIGHT_PIXELS };
    float weekdays_y = layout.header.y + layout.header.height + padding / 2;
    layout.weekdays = (Rectangle){ padding, weekdays_y, calendar_width, WEEKDAY_HEIGHT_PIXELS };
    float grid_y = weekdays_y + WEEKDAY_HEIGHT_PIXELS;
    layout.grid = (Rectangle){ padding, grid_y, calendar_width,
                               height - status_height - padding - grid_y };
    layout_set_grid(&layout, range);
    return layout;
}

// Rows and columns of day cells for the range, filling the grid rectangle.
static void layout_set_grid(Layout *layout, int32_t range) {
    assert(range >= 0);
    assert(range < RANGE_COUNT);
    layout->columns = range == RANGE_DAY ? 1 : GRID_COLUMN_COUNT;
    layout->rows = range == RANGE_MONTH ? GRID_ROW_COUNT : 1;
    assert(layout->columns * layout->rows == range_day_count(range));
    layout->cell_width = layout->grid.width / (float)layout->columns;
    layout->cell_height = layout->grid.height / (float)layout->rows;
    // The minimum window size guarantees room for every part.
    assert(layout->cell_width > 0.0f);
    assert(layout->cell_height > 0.0f);
}

static int32_t range_day_count(int32_t range) {
    switch ((CalendarRange)range) {
    case RANGE_MONTH:
        return GRID_CELL_COUNT;
    case RANGE_WEEK:
        return GRID_COLUMN_COUNT;
    case RANGE_DAY:
        return 1;
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a CalendarRange.
    return 1;
}

// The date one range before (direction -1) or after (1) date: a month, a week or a day.
static Date range_step(Date date, int32_t range, int32_t direction) {
    assert(direction == -1 || direction == 1);
    switch ((CalendarRange)range) {
    case RANGE_MONTH:
        return date_add_months(date, direction);
    case RANGE_WEEK:
        return date_add_days(date, 7 * direction);
    case RANGE_DAY:
        return date_add_days(date, direction);
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a CalendarRange.
    return date;
}

// A month starts on the Monday of the week that holds its first day, a week on its Monday, and
// a day view shows the selected day.
static Date range_first_date(const App *app) {
    switch ((CalendarRange)app->range) {
    case RANGE_MONTH:
        return grid_first_date(app->events_month);
    case RANGE_WEEK:
        return date_add_days(app->selected, -date_weekday(app->selected));
    case RANGE_DAY:
        return app->selected;
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a CalendarRange.
    return app->selected;
}

// "October 2026"; "12 - 18 October 2026", or with both months or years when the week spans
// them; "Tue 20 October 2026".
static void range_title(const App *app, char *buffer, size_t buffer_size) {
    const Date first = app->visible_first;
    const Date last = date_add_days(first, GRID_COLUMN_COUNT - 1);
    switch ((CalendarRange)app->range) {
    case RANGE_MONTH:
        snprintf(buffer, buffer_size, "%s %" PRId32, month_name(app->events_month.month),
                 app->events_month.year);
        return;
    case RANGE_WEEK:
        if (first.month == last.month) {
            snprintf(buffer, buffer_size, "%" PRId32 " - %" PRId32 " %s %" PRId32, first.day,
                     last.day, month_name(first.month), first.year);
        } else if (first.year == last.year) {
            snprintf(buffer, buffer_size, "%" PRId32 " %s - %" PRId32 " %s %" PRId32, first.day,
                     month_short_name(first.month), last.day, month_short_name(last.month),
                     last.year);
        } else {
            snprintf(buffer, buffer_size, "%" PRId32 " %s %" PRId32 " - %" PRId32 " %s %" PRId32,
                     first.day, month_short_name(first.month), first.year, last.day,
                     month_short_name(last.month), last.year);
        }
        return;
    case RANGE_DAY:
        snprintf(buffer, buffer_size, "%s %" PRId32 " %s %" PRId32,
                 weekday_short_name(date_weekday(first)), first.day, month_name(first.month),
                 first.year);
        return;
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a CalendarRange.
    buffer[0] = '\0';
}

// Items with an occurrence on a day shown, for the status bar.
static int32_t app_visible_item_count(const App *app) {
    const int32_t first_day = date_to_day_number(app->visible_first);
    const int32_t day_count = range_day_count(app->range);
    int32_t count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        for (int32_t day = 0; day < day_count; day++) {
            if (event_occurs_on_day(&app->events[i], first_day + day)) {
                count++;
                break;
            }
        }
    }
    return count;
}

// The calendar above and the panel below, each the full width. While the form is open it takes
// everything below the header, because it needs the height and the grid cannot be used then.
static Layout layout_compute_portrait(float width, float height, bool form_open,
                                      int32_t range) {
    const float padding = (float)PADDING_PIXELS_PORTRAIT;
    const float status_height = (float)STATUS_HEIGHT_PIXELS;
    const float content_width = width - 2 * padding;
    Layout layout = { .grid_visible = !form_open, .portrait = true };
    layout.status = (Rectangle){ 0, height - status_height, width, status_height };
    layout.header = (Rectangle){ padding, padding, content_width, HEADER_HEIGHT_PIXELS };
    float top = layout.header.y + layout.header.height + padding;
    float bottom = layout.status.y - padding;
    if (form_open) {
        layout.panel = (Rectangle){ padding, top, content_width, bottom - top };
        return layout;
    }

    // The header has no room beside its buttons and title, so the range selector gets a row.
    layout.range_bar = (Rectangle){ padding, top, content_width, RANGE_BAR_HEIGHT_PIXELS };
    top += RANGE_BAR_HEIGHT_PIXELS + padding;
    float grid_height = (bottom - top) * GRID_HEIGHT_PERCENT_PORTRAIT / 100.0f -
                        WEEKDAY_HEIGHT_PIXELS;
    layout.weekdays = (Rectangle){ padding, top, content_width, WEEKDAY_HEIGHT_PIXELS };
    layout.grid = (Rectangle){ padding, top + WEEKDAY_HEIGHT_PIXELS, content_width, grid_height };
    layout_set_grid(&layout, range);
    float panel_y = layout.grid.y + layout.grid.height + padding;
    layout.panel = (Rectangle){ padding, panel_y, content_width, bottom - panel_y };
    assert(layout.panel.height > 0.0f);
    return layout;
}

static Rectangle grid_cell_rectangle(const Layout *layout, int32_t cell_index) {
    assert(cell_index >= 0);
    assert(cell_index < layout->rows * layout->columns);
    int32_t column = cell_index % layout->columns;
    int32_t row = cell_index / layout->columns;
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

// Buttons on the left, the range selector on the right (or in its own row on a phone), and the
// title of what is shown between them.
static void draw_header(App *app, const Layout *layout) {
    const Rectangle header = layout->header;
    const float height = header.height;
    if (GuiButton((Rectangle){ header.x, header.y, 40, height }, "<")) {
        app_select(app, range_step(app->selected, app->range, -1));
    }
    if (GuiButton((Rectangle){ header.x + 48, header.y, 40, height }, ">")) {
        app_select(app, range_step(app->selected, app->range, 1));
    }
    if (GuiButton((Rectangle){ header.x + 96, header.y, 90, height }, "Today")) {
        app_select(app, app->today);
    }
    if (layout->range_bar.width > 0.0f) {
        // raygui lays the toggles out side by side, each the size of these bounds, 2 apart.
        Rectangle item = { layout->range_bar.x, layout->range_bar.y,
                           (layout->range_bar.width - 2 * (RANGE_COUNT - 1)) / RANGE_COUNT,
                           layout->range_bar.height };
        GuiToggleGroup(item, range_labels, &app->range);
    }

    char title[TEXT_LINE_SIZE];
    range_title(app, title, sizeof(title));
    const float buttons_width = 194;
    float right = header.x + header.width;
    if (!layout->portrait) {
        right = layout->range_bar.x - 8;
    }
    Rectangle area = { header.x + buttons_width, header.y, right - header.x - buttons_width,
                       height };
    // The normal font when the heading font is too wide for the space, as on a phone.
    const TextStyle *style = &app->fonts.heading;
    if (measure_text(style, title).x > area.width) {
        style = &app->fonts.ui;
    }
    draw_text_centered(style, title, area, style_color(TEXT_COLOR_NORMAL));
}

// The names of the days shown: short in a week or month, in full for a single day.
static void draw_weekdays(const Fonts *fonts, const Layout *layout, Date first) {
    for (int32_t column = 0; column < layout->columns; column++) {
        Rectangle area = { layout->weekdays.x + (float)column * layout->cell_width,
                           layout->weekdays.y, layout->cell_width, layout->weekdays.height };
        int32_t weekday = date_weekday(date_add_days(first, column));
        // Weekend names are dimmed, as on a paper calendar.
        Color color = style_color(weekday >= 5 ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
        const char *name = layout->columns == 1 ? weekday_name(weekday)
                                                : weekday_short_name(weekday);
        draw_text_centered(&fonts->ui, name, area, color);
    }
}

// Draws from visible_first, set by app_sync, rather than the selected date, so that a click
// that changes the selection mid-frame never draws one range's days with another's events.
static void draw_grid(App *app, const Layout *layout) {
    int32_t first_day = date_to_day_number(app->visible_first);
    const Event *focused = focused_event_find(app);
    const int32_t cell_count = layout->rows * layout->columns;
    for (int32_t cell_index = 0; cell_index < cell_count; cell_index++) {
        Rectangle cell = grid_cell_rectangle(layout, cell_index);
        int32_t column = cell_index % layout->columns;
        draw_grid_cell(app, cell, first_day + cell_index, column, focused);
    }
}

// The focused item, if it occurs on the selected day; its creation dot is ringed in the grid.
static const Event *focused_event_find(const App *app) {
    const Event *event = app_event_find(app, app->event_focused_id);
    if (event == NULL) {
        return NULL;
    }
    // A click earlier in this frame may have moved the selection off the focused event.
    if (!event_occurs_on_day(event, date_to_day_number(app->selected))) {
        return NULL;
    }
    return event;
}

static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Event *focused) {
    Date date = date_from_day_number(day_number);
    // Days of the next and previous month are dimmed in the month view only; a week or day
    // shows its days alike.
    bool in_month = app->range != RANGE_MONTH || date.month == app->events_month.month;
    bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        app_select(app, date);
    }

    Color fill = style_color(in_month ? BASE_COLOR_NORMAL : BACKGROUND_COLOR);
    if (hovered) {
        fill = style_color(BASE_COLOR_FOCUSED);
    }
    DrawRectangleRec(cell, fill);
    DrawRectangleLinesEx(cell, 1, style_color(BORDER_COLOR_NORMAL));

    draw_grid_cell_day_number(app, cell, date, in_month);
    draw_grid_cell_creation_markers(app, cell, day_number, focused);
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
// event's dot gets a ring, so that the day it was created is easy to find.
static void draw_grid_cell_creation_markers(const App *app, Rectangle cell, int32_t day_number,
                                            const Event *focused) {
    const float radius = (float)CREATION_MARKER_RADIUS_PIXELS;
    const float spacing = 2 * radius + 6;
    Vector2 center = { cell.x + cell.width - 14, cell.y + 7 + TEXT_SIZE_PIXELS / 2.0f };
    // Slots between the day number and the right edge, each holding a dot or the "+".
    int32_t slot_count = (int32_t)((cell.width - 14 - DAY_NUMBER_WIDTH_PIXELS) / spacing) + 1;
    if (slot_count < 1) {
        slot_count = 1;
    }
    int32_t created_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        created_count += event_created_on_day(&app->events[i], day_number) ? 1 : 0;
    }
    // When not all fit, the last slot holds the "+" instead of a dot.
    int32_t dot_count_max = created_count <= slot_count ? slot_count : slot_count - 1;

    int32_t marker_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        if (!event_created_on_day(event, day_number)) {
            continue;
        }
        if (marker_count == dot_count_max) {
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
        // A problem has no time; its "!" marks it even where its color is hard to tell apart.
        snprintf(label, sizeof(label), "%s%s", event->kind == EVENT_KIND_PROBLEM ? "! " : "",
                 event->title);
    }

    float text_x = bar.x + 4;
    Color text_color = WHITE;
    scissor_begin(&app->fonts, bar.x, bar.y, bar.width, bar.height);
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
    scissor_end(&app->fonts);
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
    const float gap = 8;
    const float button_width = (body.width - 2 * gap) / 3;
    Rectangle event_button = { body.x, body.y, button_width, BUTTON_HEIGHT_PIXELS };
    Rectangle task_button = { body.x + button_width + gap, body.y, button_width,
                              BUTTON_HEIGHT_PIXELS };
    Rectangle problem_button = { body.x + 2 * (button_width + gap), body.y, button_width,
                                 BUTTON_HEIGHT_PIXELS };
    if (GuiButton(event_button, "New event")) {
        app->event_delete_id = 0;
        form_open(&app->form, app->selected, EVENT_KIND_EVENT);
    }
    if (GuiButton(task_button, "New task")) {
        app->event_delete_id = 0;
        form_open(&app->form, app->selected, EVENT_KIND_TASK);
    }
    // A problem is recorded today, whichever day is selected.
    if (GuiButton(problem_button, "New problem")) {
        app->event_delete_id = 0;
        form_open(&app->form, app->today, EVENT_KIND_PROBLEM);
    }

    // The list ends above the legend; it scrolls when its cards do not fit.
    float list_top = body.y + BUTTON_HEIGHT_PIXELS + PADDING_PIXELS;
    float list_bottom = body.y + body.height - LEGEND_HEIGHT_PIXELS - PADDING_PIXELS;
    Rectangle list = { body.x, list_top, body.width, list_bottom - list_top };
    float content_height = draw_panel_events_height(app, list.width);
    if (content_height > list.height) {
        // The cards narrow to make room for the scroll bar, which can make them taller.
        list.width -= SCROLLBAR_GAP_PIXELS;
        content_height = draw_panel_events_height(app, list.width);
    }

    if (content_height == 0.0f) {
        draw_text(&app->fonts.ui, "Nothing on this day.", body.x, list_top,
                  style_color(TEXT_COLOR_DISABLED));
    } else if (list.height > 0.0f) {
        draw_panel_events_scroll(app, list, content_height);
        draw_panel_events_list(app, list);
        draw_panel_events_scrollbar(list, content_height, app->panel_scroll_pixels);
    } else {
        // A window too short for any list shows only the buttons and the legend.
    }
    draw_panel_legend(&app->fonts, (Rectangle){ body.x, body.y + body.height -
                                                LEGEND_HEIGHT_PIXELS, body.width,
                                                LEGEND_HEIGHT_PIXELS });
}

// Height of the selected day's cards stacked with their gaps, or 0 when the day has none.
static float draw_panel_events_height(const App *app, float card_width) {
    assert(card_width > 0.0f);
    assert(app->event_count <= EVENT_COUNT_MAX);
    int32_t selected_day = date_to_day_number(app->selected);
    float height = 0.0f;
    for (int32_t i = 0; i < app->event_count; i++) {
        int32_t start_day = 0;
        if (!event_occurrence_on_day(&app->events[i], selected_day, &start_day)) {
            continue;
        }
        if (height > 0.0f) {
            height += EVENT_CARD_GAP_PIXELS;
        }
        height += event_card_height(&app->fonts, &app->events[i], card_width);
    }
    assert(height >= 0.0f);
    return height;
}

// Scrolls the list by the wheel, or by dragging it with a finger or the mouse, and keeps the
// scroll within the cards.
static void draw_panel_events_scroll(App *app, Rectangle list, float content_height) {
    assert(list.height > 0.0f);
    assert(content_height > 0.0f);
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, list);
    // A press is forgotten one frame after its release, so that on the release frame the cards
    // still know whether it was a drag, and do not take it as a click.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        app->panel_pressed = false;
        app->panel_dragging = false;
    }
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        app->panel_pressed = true;
        app->panel_dragging = false;
        app->panel_drag_y = mouse.y;
        app->panel_drag_scroll_pixels = app->panel_scroll_pixels;
    }
    if (app->panel_pressed && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float moved = mouse.y - app->panel_drag_y;
        if (moved > PANEL_DRAG_THRESHOLD_PIXELS || moved < -PANEL_DRAG_THRESHOLD_PIXELS) {
            app->panel_dragging = true;
        }
        if (app->panel_dragging) {
            app->panel_scroll_pixels = app->panel_drag_scroll_pixels - moved;
        }
    }
    if (over) {
        app->panel_scroll_pixels -= GetMouseWheelMove() * PANEL_WHEEL_STEP_PIXELS;
    }

    // Also when the cards changed or the window was resized since the last frame.
    float scroll_max = content_height > list.height ? content_height - list.height : 0.0f;
    if (app->panel_scroll_pixels > scroll_max) {
        app->panel_scroll_pixels = scroll_max;
    }
    if (app->panel_scroll_pixels < 0.0f) {
        app->panel_scroll_pixels = 0.0f;
    }
    assert(app->panel_scroll_pixels >= 0.0f);
    assert(app->panel_scroll_pixels <= scroll_max);
}

// Draws the selected day's cards, moved up by the scroll and clipped to the list. Controls are
// locked while the list is dragged, and while the pointer is outside the list, so that neither
// the end of a drag nor a click on a clipped part of a card acts on it.
static void draw_panel_events_list(App *app, Rectangle list) {
    assert(list.height > 0.0f);
    assert(app->panel_scroll_pixels >= 0.0f);
    const bool locked = app->panel_dragging || !CheckCollisionPointRec(GetMousePosition(), list);
    // Locked already when the app has no input; that lock stays until app_frame lifts it.
    const bool locked_before = GuiIsLocked();
    if (locked) {
        GuiLock();
    }
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);

    int32_t selected_day = date_to_day_number(app->selected);
    float y = list.y - app->panel_scroll_pixels;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        int32_t start_day = 0;
        if (!event_occurrence_on_day(event, selected_day, &start_day)) {
            continue;
        }
        float card_height = event_card_height(&app->fonts, event, list.width);
        Rectangle card = { list.x, y, list.width, card_height };
        y += card_height + EVENT_CARD_GAP_PIXELS;
        // Cards scrolled wholly out of the list can be neither seen nor clicked.
        if (!CheckCollisionRecs(card, list)) {
            continue;
        }
        // The card shows this day's occurrence, so its dates and lead time are for this day.
        Event occurrence = event_occurrence_at(event, start_day);
        bool clicked = draw_panel_event_card(app, &occurrence, card);
        // Only a press that began in the list opens a card, not one dragged in from elsewhere.
        if (clicked && !locked && app->panel_pressed) {
            // Edit the event itself, not this occurrence, so that the form shows its first dates.
            app->event_focused_id = event->id;
            form_open_edit(&app->form, event);
        }
    }

    app->fonts.clipping = false;
    EndScissorMode();
    if (locked && !locked_before) {
        GuiUnlock();
    }
}

// A thin bar right of the cards: its length is the visible share of the cards, and its place
// is where the visible part lies among them.
static void draw_panel_events_scrollbar(Rectangle list, float content_height,
                                        float scroll_pixels) {
    assert(content_height > 0.0f);
    assert(scroll_pixels >= 0.0f);
    if (content_height <= list.height) {
        return;
    }
    float thumb_height = list.height * list.height / content_height;
    float thumb_y = list.y + scroll_pixels * list.height / content_height;
    float x = list.x + list.width + SCROLLBAR_GAP_PIXELS - SCROLLBAR_WIDTH_PIXELS;
    DrawRectangleRec((Rectangle){ x, list.y, SCROLLBAR_WIDTH_PIXELS, list.height },
                     style_color(BASE_COLOR_NORMAL));
    DrawRectangleRec((Rectangle){ x, thumb_y, SCROLLBAR_WIDTH_PIXELS, thumb_height },
                     style_color(BORDER_COLOR_NORMAL));
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
    scissor_begin(&app->fonts, card.x, card.y, card.width - 90, card.height);
    draw_text(title_style, event->title, title_x, card.y + 8, title_color);
    if (done) {
        float width = measure_text(title_style, event->title).x;
        float middle_y = card.y + 8 + title_style->size_pixels / 2 + 1;
        DrawLineEx((Vector2){ title_x, middle_y }, (Vector2){ title_x + width, middle_y }, 1.5f,
                   title_color);
    }
    scissor_end(&app->fonts);

    if (confirming) {
        const char *question = event->repeat_unit != REPEAT_NONE ? "Delete it and all repeats?"
                               : task                            ? "Delete this task?"
                               : event->kind == EVENT_KIND_PROBLEM ? "Delete this problem?"
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
    // On release rather than press, because the press may still become a drag of the list.
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !on_button;
}

// The lines below the title: dates and repeat, location, description, repeat end, creation,
// and lead time. A problem shows only its kind, description and creation.
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event,
                                          Rectangle card) {
    const float x = card.x + 16;
    const float line = (float)TEXT_LINE_HEIGHT_PIXELS_SMALL;
    const TextStyle *style = &fonts->small;
    char text[TEXT_LINE_SIZE];
    scissor_begin(fonts, card.x, card.y, card.width - 4, card.height);
    float y = card.y + 8 + line;
    event_format_range(event, text, sizeof(text));
    draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    if (event->location.id != 0) {
        y += line;
        event_format_location(event, text, sizeof(text));
        draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    }
    if (event->description[0] != '\0') {
        TextLines lines = text_wrap(style, event->description,
                                    card.width - (float)CARD_TEXT_MARGIN_PIXELS);
        for (int32_t i = 0; i < lines.count; i++) {
            y += line;
            int32_t shown = snprintf(text, sizeof(text), "%.*s", lines.length[i],
                                     event->description + lines.start[i]);
            if (shown > (int32_t)sizeof(text) - 4) {
                shown = (int32_t)sizeof(text) - 4;
            }
            // The last line says that there is more, as the card cannot show all of it.
            if (lines.cut && i == lines.count - 1) {
                snprintf(text + shown, sizeof(text) - (size_t)shown, "...");
            }
            draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
        }
    }
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
    // A problem falls on the day it was created, so it has no lead time to show.
    if (event->kind != EVENT_KIND_PROBLEM) {
        y += line;
        event_format_lead(event, text, sizeof(text));
        draw_text(style, text, x, y, event_color(event));
    }
    scissor_end(fonts);
}

// Explains the grid's creation dots.
static void draw_panel_legend(const Fonts *fonts, Rectangle area) {
    const Color sample = event_colors[0];
    const Color dim = style_color(TEXT_COLOR_DISABLED);
    const float text_x = area.x + 26;
    DrawCircleV((Vector2){ area.x + 8, area.y + 8 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text(&fonts->small, "Day it was created", text_x, area.y, dim);

    float second_y = area.y + TEXT_LINE_HEIGHT_PIXELS_SMALL + 4;
    DrawCircleV((Vector2){ area.x + 8, second_y + 8 }, CREATION_MARKER_RADIUS_PIXELS + 3,
                style_color(BORDER_COLOR_PRESSED));
    DrawCircleV((Vector2){ area.x + 8, second_y + 8 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text(&fonts->small, "Day the selected item was created", text_x, second_y, dim);
}

static void draw_panel_form(App *app, Rectangle body) {
    Form *form = &app->form;
    const float gap = 12;
    static const char *const headings[EVENT_KIND_COUNT][2] = {
        { "New event", "Edit event" },
        { "New task", "Edit task" },
        { "New problem", "Edit problem" },
    };
    const bool editing = form->editing_id != 0;
    draw_text(&app->fonts.ui, headings[form->kind][editing ? 1 : 0], body.x, body.y,
              style_color(TEXT_COLOR_NORMAL));
    // A new entry can be any kind. An existing problem keeps its kind, and an existing event or
    // task cannot become a problem, because a problem must be on the day it was created.
    const bool problem = form->kind == EVENT_KIND_PROBLEM;
    const int32_t toggle_count = !editing ? 3 : problem ? 0 : 2;
    // raygui lays the toggles out side by side, each the size of these bounds, 2 pixels apart.
    const float toggle_width = 76;
    const float toggles_width = (float)toggle_count * (toggle_width + 2) - 2;
    if (toggle_count > 0) {
        GuiToggleGroup((Rectangle){ body.x + body.width - toggles_width, body.y - 4,
                                    toggle_width, 28 },
                       toggle_count == 3 ? "Event;Task;Problem" : "Event;Task", &form->kind);
    }

    // The first field's label sits below the heading and its box below the label.
    float y = draw_panel_form_fields(app, body, body.y + 32 + FORM_LABEL_HEIGHT_PIXELS);
    Color dim = style_color(TEXT_COLOR_DISABLED);
    if (form->kind != EVENT_KIND_PROBLEM) {
        draw_text(&app->fonts.small, "Dates YYYY-MM-DD, times HH:MM (optional)", body.x, y,
                  dim);
    }
    y += TEXT_LINE_HEIGHT_PIXELS_SMALL;
    if (form->error[0] != '\0') {
        scissor_begin(&app->fonts, body.x, y, body.width, TEXT_LINE_HEIGHT_PIXELS);
        draw_text(&app->fonts.ui, form->error, body.x, y, error_color);
        scissor_end(&app->fonts);
    }

    y += TEXT_LINE_HEIGHT_PIXELS + 4;
    const float button_width = (body.width - gap) / 2;
    if (GuiButton((Rectangle){ body.x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Cancel")) {
        form_close(form);
    }
    Rectangle save = { body.x + button_width + gap, y, button_width, BUTTON_HEIGHT_PIXELS };
    if (GuiButton(save, "Save")) {
        app_form_submit(app);
    }
}

// Draws the rows of fields, the first box at y. Returns the y just below the last row.
static float draw_panel_form_fields(App *app, Rectangle body, float y) {
    Form *form = &app->form;
    const Fonts *fonts = &app->fonts;
    const float row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    const float gap = 12;
    const float time_width = 96;
    const float date_width = body.width - time_width - gap;
    const float time_x = body.x + date_width + gap;
    const float city_width = 136;
    const float address_width = body.width - city_width - gap;
    const float city_x = body.x + address_width + gap;
    const float end_y = y + (FORM_ROW_COUNT - 1) * row_height + FIELD_HEIGHT_PIXELS +
                        FORM_ROW_GAP_PIXELS;
    if (form->kind == EVENT_KIND_PROBLEM) {
        draw_panel_form_fields_problem(app, body, y);
        return end_y;
    }

    draw_panel_form_field(fonts, form, FORM_FIELD_TITLE, "Title",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_START_DATE, "Start date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_START_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_END_DATE, "End date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_END_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_LOCATION_ADDRESS, "Address (optional)",
                          (Rectangle){ body.x, y, address_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_LOCATION_CITY, "City",
                          (Rectangle){ city_x, y, city_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_DESCRIPTION, "Description (optional)",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_text(&fonts->ui, "Repeat", body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    GuiComboBox((Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS }, repeat_option_labels,
                &form->repeat_option);
    if (form_field_visible(form, FORM_FIELD_REPEAT_DAYS)) {
        draw_panel_form_field(fonts, form, FORM_FIELD_REPEAT_DAYS, "Days",
                              (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    }
    // The row is kept while hidden, so that the buttons do not move under the pointer.
    y += row_height;
    if (form_field_visible(form, FORM_FIELD_REPEAT_END_DATE)) {
        draw_panel_form_field(fonts, form, FORM_FIELD_REPEAT_END_DATE, "Repeat until (optional)",
                              (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    }
    assert(y + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS == end_y);
    return end_y;
}

// A problem has a name and a description; its day is shown, not chosen.
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y) {
    Form *form = &app->form;
    const Fonts *fonts = &app->fonts;
    const float row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    draw_panel_form_field(fonts, form, FORM_FIELD_TITLE, "Name",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_DESCRIPTION, "Description (optional)",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;

    // A new problem is recorded today; an existing one stays on the day it was created, which
    // form_open_edit put in the start date text.
    Date date = app->today;
    if (form->editing_id != 0) {
        bool parsed = date_parse_iso(form->text[FORM_FIELD_START_DATE], &date);
        assert(parsed);
        (void)parsed; // Unused when assertions are compiled out.
    }
    char text[TEXT_LINE_SIZE];
    snprintf(text, sizeof(text), "On %s, %" PRId32 " %s %" PRId32, weekday_name(date_weekday(date)),
             date.day, month_name(date.month), date.year);
    draw_text(&fonts->ui, text, body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    draw_text(&fonts->small, "A problem is always on the day it is created.", body.x, y + 4,
              style_color(TEXT_COLOR_DISABLED));
}

static void draw_panel_form_field(const Fonts *fonts, Form *form, int32_t field,
                                  const char *label, Rectangle box) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    draw_text(&fonts->ui, label, box.x, box.y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    bool editing = form->field_active == field;
    // raygui returns true when edit mode should toggle: a click on the box, or Enter or a
    // click elsewhere while editing.
    if (GuiTextBox(box, form->text[field], form_field_sizes[field], editing)) {
        form->field_active = editing ? FORM_FIELD_NONE : field;
    }
}

static void draw_status(App *app, Rectangle status) {
    char message[STATUS_SIZE];
    if (app->status[0] != '\0') {
        snprintf(message, sizeof(message), "%s", app->status);
    } else if (app->events_truncated) {
        snprintf(message, sizeof(message), "Showing the first %d events; more are hidden.",
                 EVENT_COUNT_MAX);
    } else if (app->touch) {
        snprintf(message, sizeof(message), "%" PRId32 " items in view",
                 app_visible_item_count(app));
    } else {
        snprintf(message, sizeof(message),
                 "%" PRId32 " items in view    Arrows: day/week    PgUp/PgDn: previous/next    "
                 "M/W/D: month/week/day    Click an item to edit", app_visible_item_count(app));
    }
    char text[INFO_SIZE + STATUS_SIZE + 8];
    if (app->info[0] != '\0') {
        snprintf(text, sizeof(text), "%s    %s", app->info, message);
    } else {
        snprintf(text, sizeof(text), "%s", message);
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
    snprintf(form->text[FORM_FIELD_LOCATION_ADDRESS], LOCATION_ADDRESS_SIZE, "%s",
             event->location.address);
    snprintf(form->text[FORM_FIELD_LOCATION_CITY], LOCATION_CITY_SIZE, "%s",
             event->location.city);
    snprintf(form->text[FORM_FIELD_DESCRIPTION], EVENT_DESCRIPTION_SIZE, "%s",
             event->description);
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
    if (form->kind == EVENT_KIND_PROBLEM) {
        return field == FORM_FIELD_TITLE || field == FORM_FIELD_DESCRIPTION;
    }
    if (field == FORM_FIELD_REPEAT_DAYS) {
        return form->repeat_option == REPEAT_OPTION_DAYS;
    }
    if (field == FORM_FIELD_REPEAT_END_DATE) {
        return form->repeat_option != REPEAT_OPTION_NONE;
    }
    return true;
}

// Fills event from the form. Returns NULL on success, or a message saying what to fix.
static const char *form_parse(const Form *form, Date today, Event *event) {
    assert(event->id == 0);
    // Leading spaces would let a blank-looking title through.
    const char *title = form->text[FORM_FIELD_TITLE];
    title += strspn(title, " ");
    if (title[0] == '\0') {
        return form->kind == EVENT_KIND_PROBLEM ? "Enter a name." : "Enter a title.";
    }
    snprintf(event->title, sizeof(event->title), "%s", title);
    if (form->kind == EVENT_KIND_PROBLEM) {
        return form_parse_problem(form, today, event);
    }

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
    // The location id is found or created when saving, from these texts.
    text_copy_trimmed(event->location.address, sizeof(event->location.address),
                      form->text[FORM_FIELD_LOCATION_ADDRESS]);
    text_copy_trimmed(event->location.city, sizeof(event->location.city),
                      form->text[FORM_FIELD_LOCATION_CITY]);
    text_copy_trimmed(event->description, sizeof(event->description),
                      form->text[FORM_FIELD_DESCRIPTION]);
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

    return form_parse_order(event);
}

// Checks that the parsed dates and times come in order. Returns NULL, or what to fix.
static const char *form_parse_order(const Event *event) {
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

// The hidden fields of the form are ignored: a problem has no times, location or repeat.
static const char *form_parse_problem(const Form *form, Date today, Event *event) {
    assert(form->kind == EVENT_KIND_PROBLEM);
    event->kind = EVENT_KIND_PROBLEM;
    // A new problem falls on today, the day it is created. An edited one keeps its day.
    event->start_date = today;
    if (form->editing_id != 0) {
        if (!date_parse_iso(form->text[FORM_FIELD_START_DATE], &event->start_date)) {
            return "Problem date is invalid.";
        }
    }
    event->end_date = event->start_date;
    event->start_minute = TIME_NONE;
    event->end_minute = TIME_NONE;
    event->repeat_unit = REPEAT_NONE;
    event->repeat_interval = 1;
    event->repeat_has_end = false;
    text_copy_trimmed(event->description, sizeof(event->description),
                      form->text[FORM_FIELD_DESCRIPTION]);
    assert(event->location.id == 0);
    assert(event->location.address[0] == '\0');
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

// One more line for a location, one for a repeating event that ends, to say when, and one per
// line of description.
static float event_card_height(const Fonts *fonts, const Event *event, float card_width) {
    int32_t extra_lines = (event->location.id != 0 ? 1 : 0) + (event->repeat_has_end ? 1 : 0);
    // A problem has no lead time line.
    extra_lines -= event->kind == EVENT_KIND_PROBLEM ? 1 : 0;
    if (event->description[0] != '\0') {
        float width = card_width - (float)CARD_TEXT_MARGIN_PIXELS;
        extra_lines += text_wrap(&fonts->small, event->description, width).count;
    }
    return (float)(EVENT_CARD_HEIGHT_PIXELS + extra_lines * TEXT_LINE_HEIGHT_PIXELS_SMALL);
}

// Breaks text at spaces into at most DESCRIPTION_LINES_MAX lines that fit width_pixels, and
// breaks inside a word only when one word alone is wider than a line.
static TextLines text_wrap(const TextStyle *style, const char *text, float width_pixels) {
    TextLines lines = { .count = 0, .cut = false };
    const int32_t length = (int32_t)strlen(text);
    int32_t position = 0;
    // Bounded by DESCRIPTION_LINES_MAX, and each pass either ends the loop or consumes text.
    while (lines.count < DESCRIPTION_LINES_MAX) {
        while (position < length && text[position] == ' ') {
            position++;
        }
        if (position >= length) {
            break;
        }

        // Extend the line one word at a time while it still fits.
        int32_t line_end = position;
        for (int32_t i = position; i <= length; i++) {
            if (i == length || text[i] == ' ') {
                if (!text_line_fits(style, text + position, i - position, width_pixels)) {
                    break;
                }
                line_end = i;
            }
        }
        // One word wider than the line: break it by characters, at least one per line.
        if (line_end == position) {
            line_end = position + 1;
            while (line_end < length &&
                   text_line_fits(style, text + position, line_end + 1 - position, width_pixels)) {
                line_end++;
            }
        }
        lines.start[lines.count] = position;
        lines.length[lines.count] = line_end - position;
        lines.count++;
        position = line_end;
    }
    while (position < length && text[position] == ' ') {
        position++;
    }
    lines.cut = position < length;
    return lines;
}

static bool text_line_fits(const TextStyle *style, const char *text, int32_t length,
                           float width_pixels) {
    char line[EVENT_DESCRIPTION_SIZE];
    assert(length >= 0);
    assert(length < EVENT_DESCRIPTION_SIZE);
    memcpy(line, text, (size_t)length);
    line[length] = '\0';
    return measure_text(style, line).x <= width_pixels;
}

static Color event_color(const Event *event) {
    assert(event->id > 0);
    if (event->kind == EVENT_KIND_PROBLEM) {
        return problem_color;
    }
    return event_colors[event->id % EVENT_COLOR_COUNT];
}

// The dates and times, followed by how the event repeats, if it does.
static void event_format_range(const Event *event, char *buffer, size_t buffer_size) {
    // A problem has no range: it is on its one day, which the panel heading shows.
    if (event->kind == EVENT_KIND_PROBLEM) {
        snprintf(buffer, buffer_size, "Problem");
        return;
    }
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

// "Address, City", or whichever of the two the location has.
static void event_format_location(const Event *event, char *buffer, size_t buffer_size) {
    const Location *location = &event->location;
    assert(location->id != 0);
    bool has_address = location->address[0] != '\0';
    bool has_city = location->city[0] != '\0';
    assert(has_address || has_city);
    if (has_address && has_city) {
        snprintf(buffer, buffer_size, "%s, %s", location->address, location->city);
    } else {
        snprintf(buffer, buffer_size, "%s", has_address ? location->address : location->city);
    }
}

// Copies text without leading and trailing spaces, so that "Main St " and "Main St" are the
// same location, and a field of only spaces counts as empty.
static void text_copy_trimmed(char *buffer, size_t buffer_size, const char *text) {
    assert(buffer_size > 0);
    text += strspn(text, " ");
    size_t length = strlen(text);
    // Bounded by length, which shrinks to zero at most.
    while (length > 0 && text[length - 1] == ' ') {
        length--;
    }
    if (length >= buffer_size) {
        length = buffer_size - 1;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
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

// Positions are rounded to whole screen pixels, so that glyphs, rasterized at the screen
// scale, map one to one onto screen pixels and stay sharp. A position may be left of or above
// the screen, as in the phone's drawer while it slides in.
static void draw_text(const TextStyle *style, const char *text, float x, float y, Color color) {
    // Far beyond any screen, which would also overflow the rounding.
    assert(x > -(float)TEXT_POSITION_LIMIT_PIXELS);
    assert(x < (float)TEXT_POSITION_LIMIT_PIXELS);
    assert(y > -(float)TEXT_POSITION_LIMIT_PIXELS);
    assert(y < (float)TEXT_POSITION_LIMIT_PIXELS);
    const float scale = style->scale;
    Vector2 position = { draw_text_round(x, scale), draw_text_round(y, scale) };
    DrawTextEx(style->font, text, position, style->size_pixels,
               (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

// Rounds a layout position to the nearest whole screen pixel. Casting truncates toward zero,
// so negative positions are moved up by the limit first, which keeps the cast's input positive.
static float draw_text_round(float position, float scale) {
    assert(scale > 0.0f);
    const float shift = (float)TEXT_POSITION_LIMIT_PIXELS * scale;
    float screen = position * scale + shift + 0.5f;
    assert(screen >= 0.0f);
    return ((float)(int32_t)screen - shift) / scale;
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

// raylib's scissor rectangle is in screen pixels, so the layout rectangle is scaled to match.
// Within a clip, such as the panel's scrolling list, only the part inside the clip is drawn.
static void scissor_begin(const Fonts *fonts, float x, float y, float width, float height) {
    assert(width >= 0.0f);
    assert(height >= 0.0f);
    Rectangle area = { x, y, width, height };
    if (fonts->clipping) {
        // An empty overlap clips everything, which is right for text wholly outside the clip.
        area = GetCollisionRec(area, fonts->clip);
    }
    const float scale = fonts->scale;
    BeginScissorMode((int)(fonts->origin.x + area.x * scale),
                     (int)(fonts->origin.y + area.y * scale), (int)(area.width * scale),
                     (int)(area.height * scale));
}

// Ends a scissor_begin. Within a clip, the clip's own scissor rectangle comes back, because
// raylib cannot nest them.
static void scissor_end(const Fonts *fonts) {
    if (fonts->clipping) {
        assert(fonts->clip.width > 0.0f);
        assert(fonts->clip.height > 0.0f);
        scissor_begin(fonts, fonts->clip.x, fonts->clip.y, fonts->clip.width,
                      fonts->clip.height);
    } else {
        EndScissorMode();
    }
}
