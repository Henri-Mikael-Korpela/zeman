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
    // Multiples of the default font's 10 pixel glyphs render crisply.
    TEXT_SIZE_PIXELS = 20,
    TEXT_SIZE_PIXELS_LARGE = 30,
    TEXT_LINE_HEIGHT_PIXELS = 24,
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
    CELL_TOP_LINE_PIXELS = 36,
    EVENT_BAR_HEIGHT_PIXELS = 22,
    EVENT_BAR_GAP_PIXELS = 2,
    EVENT_CARD_HEIGHT_PIXELS = 104,
    EVENT_CARD_GAP_PIXELS = 8,
    CREATION_MARKER_RADIUS_PIXELS = 5,
    // Per cell. Past this a "+" is drawn, because more dots would run into the day number.
    CREATION_MARKER_COUNT_MAX = 5,
    LEGEND_HEIGHT_PIXELS = 56,
    // Events loaded for one six-week view. If more match, the status bar says so.
    EVENT_COUNT_MAX = 512,
    STATUS_SIZE = 192,
    TEXT_LINE_SIZE = 128,
    DATABASE_PATH_SIZE = 1024,
};

static const float LEAD_TINT_ALPHA = 0.22f;

typedef enum FormField {
    FORM_FIELD_TITLE,
    FORM_FIELD_START_DATE,
    FORM_FIELD_START_TIME,
    FORM_FIELD_END_DATE,
    FORM_FIELD_END_TIME,
    FORM_FIELD_COUNT,
} FormField;

// No form field is being edited.
enum { FORM_FIELD_NONE = -1 };

// Buffer size of each field, which raygui also uses to cap how much can be typed.
static const int32_t form_field_sizes[FORM_FIELD_COUNT] = {
    EVENT_TITLE_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE,
};

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
    int32_t field_active;  // FormField being edited, or FORM_FIELD_NONE
    bool open;
    char text[FORM_FIELD_COUNT][EVENT_TITLE_SIZE];
    char error[EVENT_STORE_ERROR_SIZE];  // Why the last save failed, or empty
} Form;

typedef struct App {
    Event events[EVENT_COUNT_MAX];  // Events of the visible six weeks, ordered by start
    EventStore store;
    int64_t event_focused_id;       // Event whose lead time is highlighted, or 0
    Date today;
    Date selected;
    Date events_month;              // First day of the month that events[] was loaded for
    int32_t event_count;
    Form form;
    bool events_stale;              // Reload events[] even though the month is unchanged
    bool events_truncated;
    bool store_open;
    char status[STATUS_SIZE];       // Message for the status bar, cleared on navigation
} App;

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
static void app_deinit(App *app);
static void app_handle_keys(App *app, const Layout *layout);
static void app_select(App *app, Date date);
static void app_sync(App *app);
static void app_load_events(App *app, Date month);
static const Event *app_event_find(const App *app, int64_t id);
static int64_t app_event_first_on_day(const App *app, int32_t day_number);
static void app_form_submit(App *app);
static Layout layout_compute(float width, float height);
static Rectangle grid_cell_rectangle(const Layout *layout, int32_t cell_index);
static Date grid_first_date(Date month);
static void draw_header(App *app, Rectangle header);
static void draw_weekdays(const Layout *layout);
static void draw_grid(App *app, const Layout *layout);
static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Event *focused);
static void draw_grid_cell_day_number(const App *app, Rectangle cell, Date date, bool in_month);
static void draw_grid_cell_creation_markers(const App *app, Rectangle cell, int32_t day_number,
                                            const Event *focused);
static void draw_grid_cell_events(const App *app, Rectangle cell, int32_t day_number,
                                  int32_t column);
static void draw_grid_cell_event_bar(const Event *event, Rectangle bar_slot, int32_t day_number,
                                     int32_t column);
static void draw_panel(App *app, Rectangle panel);
static void draw_panel_events(App *app, Rectangle body);
static bool draw_panel_event_card(const App *app, const Event *event, Rectangle card);
static void draw_panel_legend(Rectangle area);
static void draw_panel_form(App *app, Rectangle body);
static void draw_panel_form_field(Form *form, int32_t field, const char *label, Rectangle box);
static void draw_status(const App *app, Rectangle status);
static void form_open(Form *form, Date date);
static void form_close(Form *form);
static const char *form_parse(const Form *form, Event *event);
static bool form_parse_time(const char *text, int32_t *minute_of_day);
static bool event_overlaps_day(const Event *event, int32_t day_number);
static bool event_created_on_day(const Event *event, int32_t day_number);
static bool event_lead_contains_day(const Event *event, int32_t day_number);
static Color event_color(const Event *event);
static void event_format_range(const Event *event, char *buffer, size_t buffer_size);
static void event_format_created(const Event *event, char *buffer, size_t buffer_size);
static void event_format_lead(const Event *event, char *buffer, size_t buffer_size);
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size);
static Color style_color(int32_t property);
static void draw_text(const char *text, float x, float y, int32_t size, Color color);
static Vector2 measure_text(const char *text, int32_t size);
static void draw_text_centered(const char *text, Rectangle area, int32_t size, Color color);

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
        draw_weekdays(&layout);
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
    GuiSetStyle(DEFAULT, TEXT_SIZE, TEXT_SIZE_PIXELS);

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
    CloseWindow();
}

static void app_handle_keys(App *app, const Layout *layout) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (app->form.open) {
            form_close(&app->form);
        }
        return;
    }
    if (app->form.field_active != FORM_FIELD_NONE) {
        // Keys belong to the text box being edited; only Tab is ours, to move between fields.
        if (IsKeyPressed(KEY_TAB)) {
            bool backwards = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            int32_t step = backwards ? FORM_FIELD_COUNT - 1 : 1;
            app->form.field_active = (app->form.field_active + step) % FORM_FIELD_COUNT;
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
    if (focused == NULL || !event_overlaps_day(focused, selected_day)) {
        app->event_focused_id = app_event_first_on_day(app, selected_day);
    }
}

static void app_load_events(App *app, Date month) {
    assert(month.day == 1);
    app->events_month = month;
    app->events_stale = false;
    app->events_truncated = false;
    app->event_count = 0;
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
        if (event_overlaps_day(&app->events[i], day_number)) {
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
    int64_t id = event_store_insert(&app->store, &event);
    if (id == 0) {
        snprintf(app->form.error, sizeof(app->form.error), "%s", app->store.error);
        return;
    }

    form_close(&app->form);
    // Show the new event at once, focused, so that its lead time is visible.
    app_select(app, event.start_date);
    app->event_focused_id = id;
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "Saved \"%s\".", event.title);
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
    draw_text_centered(title, header, TEXT_SIZE_PIXELS_LARGE, style_color(TEXT_COLOR_NORMAL));
}

static void draw_weekdays(const Layout *layout) {
    for (int32_t column = 0; column < GRID_COLUMN_COUNT; column++) {
        Rectangle area = { layout->weekdays.x + (float)column * layout->cell_width,
                           layout->weekdays.y, layout->cell_width, layout->weekdays.height };
        // Weekend names are dimmed, as on a paper calendar.
        bool weekend = column >= 5;
        Color color = style_color(weekend ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
        draw_text_centered(weekday_short_name(column), area, TEXT_SIZE_PIXELS, color);
    }
}

// Draws from events_month rather than the selected date, so that a click that changes month
// mid-frame never draws one month's days with another month's events.
static void draw_grid(App *app, const Layout *layout) {
    int32_t first_day = date_to_day_number(grid_first_date(app->events_month));
    const Event *focused = app_event_find(app, app->event_focused_id);
    for (int32_t cell_index = 0; cell_index < GRID_CELL_COUNT; cell_index++) {
        Rectangle cell = grid_cell_rectangle(layout, cell_index);
        int32_t column = cell_index % GRID_COLUMN_COUNT;
        draw_grid_cell(app, cell, first_day + cell_index, column, focused);
    }
}

static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           const Event *focused) {
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
    if (focused != NULL) {
        if (event_lead_contains_day(focused, day_number)) {
            DrawRectangleRec(cell, Fade(event_color(focused), LEAD_TINT_ALPHA));
        }
    }
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
    Vector2 position = { cell.x + 10, cell.y + 8 };
    Color color = style_color(in_month ? TEXT_COLOR_NORMAL : TEXT_COLOR_DISABLED);

    if (date_equal(date, app->today)) {
        Vector2 size = measure_text(number, TEXT_SIZE_PIXELS);
        Vector2 center = { position.x + size.x / 2, position.y + size.y / 2 };
        DrawCircleV(center, size.y * 0.85f, style_color(BASE_COLOR_PRESSED));
        color = style_color(TEXT_COLOR_PRESSED);
    }
    draw_text(number, position.x, position.y, TEXT_SIZE_PIXELS, color);
}

// One dot per event created on this day, right to left from the top-right corner. The focused
// event's dot gets a ring so that the start of its highlighted lead time is easy to find.
static void draw_grid_cell_creation_markers(const App *app, Rectangle cell, int32_t day_number,
                                            const Event *focused) {
    const float radius = (float)CREATION_MARKER_RADIUS_PIXELS;
    const float spacing = 2 * radius + 6;
    Vector2 center = { cell.x + cell.width - 14, cell.y + 8 + TEXT_SIZE_PIXELS / 2.0f };
    int32_t marker_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        if (!event_created_on_day(event, day_number)) {
            continue;
        }
        if (marker_count == CREATION_MARKER_COUNT_MAX) {
            draw_text("+", center.x - radius, center.y - TEXT_SIZE_PIXELS / 2.0f,
                      TEXT_SIZE_PIXELS, style_color(TEXT_COLOR_NORMAL));
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
        if (event_overlaps_day(&app->events[i], day_number)) {
            overlap_count++;
        }
    }
    // When not all fit, the last slot says how many are hidden instead of showing a bar.
    int32_t bar_count = overlap_count <= slot_count ? overlap_count : slot_count - 1;

    int32_t bar_index = 0;
    for (int32_t i = 0; i < app->event_count && bar_index < bar_count; i++) {
        const Event *event = &app->events[i];
        if (event_overlaps_day(event, day_number)) {
            Rectangle slot = { cell.x, top + (float)bar_index * slot_height, cell.width,
                               EVENT_BAR_HEIGHT_PIXELS };
            draw_grid_cell_event_bar(event, slot, day_number, column);
            bar_index++;
        }
    }
    if (overlap_count > bar_count) {
        char more[TEXT_LINE_SIZE];
        snprintf(more, sizeof(more), "+%" PRId32 " more", overlap_count - bar_count);
        draw_text(more, cell.x + 6, top + (float)bar_count * slot_height + 1, TEXT_SIZE_PIXELS,
                  style_color(TEXT_COLOR_NORMAL));
    }
}

static void draw_grid_cell_event_bar(const Event *event, Rectangle bar_slot, int32_t day_number,
                                     int32_t column) {
    bool starts_here = day_number == date_to_day_number(event->start_date);
    bool ends_here = day_number == date_to_day_number(event->end_date);
    // Multi-day events run edge to edge between days, so that they read as one bar.
    float left = bar_slot.x + (starts_here ? 4.0f : 0.0f);
    float right = bar_slot.x + bar_slot.width - (ends_here ? 4.0f : 0.0f);
    Rectangle bar = { left, bar_slot.y, right - left, bar_slot.height };
    DrawRectangleRec(bar, event_color(event));

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
    BeginScissorMode((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height);
    draw_text(label, bar.x + 4, bar.y + 1, TEXT_SIZE_PIXELS, WHITE);
    EndScissorMode();
}

static void draw_panel(App *app, Rectangle panel) {
    GuiPanel(panel, NULL);
    const float padding = (float)PADDING_PIXELS;
    Rectangle area = { panel.x + padding, panel.y + padding, panel.width - 2 * padding,
                       panel.height - 2 * padding };

    draw_text(weekday_name(date_weekday(app->selected)), area.x, area.y, TEXT_SIZE_PIXELS,
              style_color(TEXT_COLOR_DISABLED));
    char heading[TEXT_LINE_SIZE];
    snprintf(heading, sizeof(heading), "%" PRId32 " %s %" PRId32, app->selected.day,
             month_name(app->selected.month), app->selected.year);
    draw_text(heading, area.x, area.y + TEXT_LINE_HEIGHT_PIXELS, TEXT_SIZE_PIXELS_LARGE,
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
    Rectangle button = { body.x, body.y, body.width, BUTTON_HEIGHT_PIXELS };
    if (!app->store_open) {
        GuiDisable();
    }
    if (GuiButton(button, "New event")) {
        form_open(&app->form, app->selected);
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
        if (!event_overlaps_day(event, selected_day)) {
            continue;
        }
        if (y + EVENT_CARD_HEIGHT_PIXELS > list_bottom) {
            hidden_count++;
            continue;
        }
        Rectangle card = { body.x, y, body.width, EVENT_CARD_HEIGHT_PIXELS };
        if (draw_panel_event_card(app, event, card)) {
            app->event_focused_id = event->id;
        }
        y += EVENT_CARD_HEIGHT_PIXELS + EVENT_CARD_GAP_PIXELS;
        shown_count++;
    }

    Color dim = style_color(TEXT_COLOR_DISABLED);
    if (shown_count + hidden_count == 0) {
        draw_text("No events on this day.", body.x, list_top, TEXT_SIZE_PIXELS, dim);
    }
    if (hidden_count > 0) {
        char more[TEXT_LINE_SIZE];
        snprintf(more, sizeof(more), "+%" PRId32 " more event%s", hidden_count,
                 hidden_count == 1 ? "" : "s");
        draw_text(more, body.x, y, TEXT_SIZE_PIXELS, dim);
    }
    draw_panel_legend((Rectangle){ body.x, body.y + body.height - LEGEND_HEIGHT_PIXELS,
                                   body.width, LEGEND_HEIGHT_PIXELS });
}

// Returns true when the card was clicked.
static bool draw_panel_event_card(const App *app, const Event *event, Rectangle card) {
    bool focused = event->id == app->event_focused_id;
    bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
    DrawRectangleRec(card, style_color(hovered ? BASE_COLOR_FOCUSED : BASE_COLOR_NORMAL));
    DrawRectangleRec((Rectangle){ card.x, card.y, 6, card.height }, event_color(event));
    DrawRectangleLinesEx(card, focused ? 2.0f : 1.0f,
                         style_color(focused ? BORDER_COLOR_PRESSED : BORDER_COLOR_NORMAL));

    const float x = card.x + 16;
    const float line = (float)TEXT_LINE_HEIGHT_PIXELS;
    char text[TEXT_LINE_SIZE];
    // Clip to the card, because titles can be wider than the panel.
    BeginScissorMode((int)card.x, (int)card.y, (int)card.width - 4, (int)card.height);
    draw_text(event->title, x, card.y + 8, TEXT_SIZE_PIXELS, style_color(TEXT_COLOR_NORMAL));
    event_format_range(event, text, sizeof(text));
    draw_text(text, x, card.y + 8 + line, TEXT_SIZE_PIXELS, style_color(TEXT_COLOR_NORMAL));
    event_format_created(event, text, sizeof(text));
    draw_text(text, x, card.y + 8 + 2 * line, TEXT_SIZE_PIXELS, style_color(TEXT_COLOR_DISABLED));
    event_format_lead(event, text, sizeof(text));
    draw_text(text, x, card.y + 8 + 3 * line, TEXT_SIZE_PIXELS, event_color(event));
    EndScissorMode();

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Explains the two grid markings that are not self-evident.
static void draw_panel_legend(Rectangle area) {
    const Color sample = event_colors[0];
    const Color dim = style_color(TEXT_COLOR_DISABLED);
    const float text_x = area.x + 26;
    DrawCircleV((Vector2){ area.x + 8, area.y + 10 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text("Day the event was created", text_x, area.y, TEXT_SIZE_PIXELS, dim);

    float second_y = area.y + TEXT_LINE_HEIGHT_PIXELS + 4;
    DrawRectangleRec((Rectangle){ area.x, second_y, 16, 20 }, Fade(sample, LEAD_TINT_ALPHA));
    draw_text("Created to start, selected", text_x, second_y, TEXT_SIZE_PIXELS, dim);
}

static void draw_panel_form(App *app, Rectangle body) {
    Form *form = &app->form;
    const float row_height = TEXT_LINE_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + 12;
    const float gap = 12;
    const float time_width = 96;
    const float date_width = body.width - time_width - gap;
    const float time_x = body.x + date_width + gap;

    draw_text("New event", body.x, body.y, TEXT_SIZE_PIXELS, style_color(TEXT_COLOR_NORMAL));
    // Each field's label sits one line above its box, so leave room for the heading and it.
    float y = body.y + 2 * TEXT_LINE_HEIGHT_PIXELS + 12;
    draw_panel_form_field(form, FORM_FIELD_TITLE, "Title",
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(form, FORM_FIELD_START_DATE, "Start date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(form, FORM_FIELD_START_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(form, FORM_FIELD_END_DATE, "End date",
                          (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(form, FORM_FIELD_END_TIME, "Time",
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });

    y += FIELD_HEIGHT_PIXELS + 12;
    Color dim = style_color(TEXT_COLOR_DISABLED);
    draw_text("Dates: YYYY-MM-DD", body.x, y, TEXT_SIZE_PIXELS, dim);
    draw_text("Times: HH:MM, optional", body.x, y + TEXT_LINE_HEIGHT_PIXELS, TEXT_SIZE_PIXELS,
              dim);
    y += 2 * TEXT_LINE_HEIGHT_PIXELS + 8;
    if (form->error[0] != '\0') {
        BeginScissorMode((int)body.x, (int)y, (int)body.width, TEXT_LINE_HEIGHT_PIXELS);
        draw_text(form->error, body.x, y, TEXT_SIZE_PIXELS, error_color);
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

static void draw_panel_form_field(Form *form, int32_t field, const char *label, Rectangle box) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    draw_text(label, box.x, box.y - TEXT_LINE_HEIGHT_PIXELS, TEXT_SIZE_PIXELS,
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
                 "%" PRId32 " events in view    Arrows: day/week    PgUp/PgDn: month    "
                 "Home: today", app->event_count);
    }
    GuiStatusBar(status, text);
}

static void form_open(Form *form, Date date) {
    assert(!form->open);
    *form = (Form){ .field_active = FORM_FIELD_TITLE, .open = true };
    date_format_iso(date, form->text[FORM_FIELD_START_DATE], DATE_ISO_SIZE);
    date_format_iso(date, form->text[FORM_FIELD_END_DATE], DATE_ISO_SIZE);
}

static void form_close(Form *form) {
    assert(form->open);
    form->open = false;
    form->field_active = FORM_FIELD_NONE;
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

static bool event_overlaps_day(const Event *event, int32_t day_number) {
    int32_t start_day = date_to_day_number(event->start_date);
    int32_t end_day = date_to_day_number(event->end_date);
    assert(start_day <= end_day);
    return start_day <= day_number && day_number <= end_day;
}

static bool event_created_on_day(const Event *event, int32_t day_number) {
    return date_to_day_number(event->created_date) == day_number;
}

// The lead time runs from creation to start, or backwards for events added after they began.
static bool event_lead_contains_day(const Event *event, int32_t day_number) {
    int32_t created_day = date_to_day_number(event->created_date);
    int32_t start_day = date_to_day_number(event->start_date);
    int32_t first_day = created_day < start_day ? created_day : start_day;
    int32_t last_day = created_day < start_day ? start_day : created_day;
    return first_day <= day_number && day_number <= last_day;
}

static Color event_color(const Event *event) {
    assert(event->id > 0);
    return event_colors[event->id % EVENT_COLOR_COUNT];
}

static void event_format_range(const Event *event, char *buffer, size_t buffer_size) {
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
// start has a time, because "0 days" says nothing.
static void event_format_lead(const Event *event, char *buffer, size_t buffer_size) {
    int32_t lead_days = date_to_day_number(event->start_date) -
                        date_to_day_number(event->created_date);
    if (lead_days > 0) {
        snprintf(buffer, buffer_size, "%" PRId32 " day%s before start", lead_days,
                 lead_days == 1 ? "" : "s");
        return;
    }
    if (lead_days < 0) {
        snprintf(buffer, buffer_size, "Added %" PRId32 " day%s after start", -lead_days,
                 lead_days == -1 ? "" : "s");
        return;
    }
    if (event->start_minute == TIME_NONE) {
        snprintf(buffer, buffer_size, "Created on the start day");
        return;
    }

    int32_t lead_minutes = event->start_minute - event->created_minute;
    int32_t magnitude = lead_minutes < 0 ? -lead_minutes : lead_minutes;
    const char *direction = lead_minutes < 0 ? "after" : "before";
    snprintf(buffer, buffer_size, "%" PRId32 " h %02" PRId32 " min %s start", magnitude / 60,
             magnitude % 60, direction);
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

static void draw_text(const char *text, float x, float y, int32_t size, Color color) {
    DrawTextEx(GuiGetFont(), text, (Vector2){ x, y }, (float)size,
               (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

static Vector2 measure_text(const char *text, int32_t size) {
    return MeasureTextEx(GuiGetFont(), text, (float)size,
                         (float)GuiGetStyle(DEFAULT, TEXT_SPACING));
}

static void draw_text_centered(const char *text, Rectangle area, int32_t size, Color color) {
    Vector2 text_size = measure_text(text, size);
    draw_text(text, area.x + (area.width - text_size.x) / 2,
              area.y + (area.height - text_size.y) / 2, size, color);
}
