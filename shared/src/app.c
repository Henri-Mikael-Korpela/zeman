#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "raygui.h"

#include <zeman/app.h>
#include <zeman/text_box.h>

enum {
    // All sizes are layout pixels, which App_Config.scale turns into screen pixels.
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
    EVENT_BAR_HEIGHT_PIXELS = APP_BAR_HEIGHT_PIXELS,
    EVENT_BAR_GAP_PIXELS = APP_BAR_GAP_PIXELS,
    // Three small lines (title, dates, creation), with 8 pixels above and below.
    EVENT_CARD_HEIGHT_PIXELS = 8 + 3 * TEXT_LINE_HEIGHT_PIXELS_SMALL + 8,
    // While asking to confirm a delete, a card shows its title, the question and the buttons.
    EVENT_CARD_CONFIRM_HEIGHT_PIXELS = 8 + 2 * TEXT_LINE_HEIGHT_PIXELS_SMALL + 32 + 8,
    EVENT_CARD_GAP_PIXELS = 8,
    CREATION_MARKER_RADIUS_PIXELS = 5,
    // Room kept free for the day number, and today's circle around it, left of the creation
    // markers. The markers that fit beside it depend on the cell width; past that a "+" is drawn.
    DAY_NUMBER_WIDTH_PIXELS = 44,
    // Kept free below the last bar of a cell, so that the bar does not touch the cell's border.
    CELL_BOTTOM_MARGIN_PIXELS = 4,
    // One hour of the week and day views' timeline: room for two lines of small text, so that
    // a half-hour item still holds its one-line label (see TIMELINE_ITEM_MINUTES_MIN).
    TIMELINE_HOUR_HEIGHT_PIXELS = 48,
    TIMELINE_HEIGHT_PIXELS = MINUTES_PER_DAY / 60 * TIMELINE_HOUR_HEIGHT_PIXELS,
    // Room left of the days for the hour labels, "00:00" to "23:00", in the small font.
    TIMELINE_GUTTER_WIDTH_PIXELS = 48,
    // An item with only a start or only an end time is drawn an hour long, as a meeting
    // usually is.
    TIMELINE_ITEM_MINUTES_DEFAULT = 60,
    // Shorter items are drawn this long, so that their label fits.
    TIMELINE_ITEM_MINUTES_MIN = 30,
    // Overlapping items share a day's width in up to this many lanes, so that each lane stays
    // wide enough to see on a phone's week view. More items share the lane that frees up first.
    TIMELINE_LANE_COUNT_MAX = 6,
    // The hour at the top of the timeline when the app opens: the start of a usual day, since
    // the hours before it are rarely used.
    TIMELINE_SCROLL_HOUR_START = 7,
    // In a week or day, the day cells above the timeline hold the items without a time. They
    // grow with the busiest day shown, up to this share of the grid, so that the hours stay in
    // sight; past that they say "+N more".
    UNTIMED_HEIGHT_PERCENT_MAX = 40,
    LEGEND_HEIGHT_PIXELS = 56,
    // A scrolling list moves this far per wheel notch: about three lines of a card.
    LIST_WHEEL_STEP_PIXELS = 48,
    // A press that moves this far scrolls a list instead of clicking, because a finger wobbles
    // by a few pixels during a tap.
    LIST_DRAG_THRESHOLD_PIXELS = 8,
    // One problem or opportunity in a task's checklist: tall enough to tap with a finger.
    TARGET_ROW_HEIGHT_PIXELS = 36,
    // Room kept at the right of a checklist row for the target's status.
    TARGET_STATUS_WIDTH_PIXELS = 120,
    // Room around the label of each toggle of the form's kind selector, which is sized to its
    // label so that "Opportunity" fits beside the shorter names.
    KIND_TOGGLE_PADDING_PIXELS = 20,
    KIND_TOGGLE_GAP_PIXELS = 2,
    // The bar showing the list's scroll position, and the room kept free for it beside the cards.
    SCROLLBAR_WIDTH_PIXELS = 4,
    SCROLLBAR_GAP_PIXELS = 10,
    // Events loaded for one six-week view. If more match, the status bar says so.
    EVENT_COUNT_MAX = 512,
    // Task completions loaded for one view, including a year before it (see
    // Backend_Ops.load_completions). More than this is reported like too many events.
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

typedef enum Form_Field {
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
} Form_Field;

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
typedef enum Repeat_Option {
    REPEAT_OPTION_NONE,
    REPEAT_OPTION_WEEK,
    REPEAT_OPTION_DAYS,
    REPEAT_OPTION_MONTH,
    REPEAT_OPTION_YEAR,
    REPEAT_OPTION_COUNT,
} Repeat_Option;

// raygui separates combo box items with semicolons.
static const char repeat_option_labels[] =
    "Does not repeat;Every week;Every X days;Every month;Every year";

static const int32_t repeat_option_units[REPEAT_OPTION_COUNT] = {
    REPEAT_NONE, REPEAT_WEEK, REPEAT_DAY, REPEAT_MONTH, REPEAT_YEAR,
};

static_assert(REPEAT_INTERVAL_MAX < 1000, "REPEAT_DAYS_TEXT_SIZE holds three digits.");
static_assert(TIMELINE_ITEM_MINUTES_MIN * TIMELINE_HOUR_HEIGHT_PIXELS / 60 >=
              EVENT_BAR_HEIGHT_PIXELS, "The shortest timeline item holds a label.");
static_assert(TIMELINE_ITEM_MINUTES_DEFAULT >= TIMELINE_ITEM_MINUTES_MIN,
              "The default length is not cut short.");
static_assert(TIMELINE_SCROLL_HOUR_START < MINUTES_PER_DAY / 60, "The start hour is in a day.");

// Cast because the constants come from different enums.
static_assert((int)DATE_ISO_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)TIME_TEXT_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)EVENT_TITLE_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_ADDRESS_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_CITY_SIZE <= (int)EVENT_DESCRIPTION_SIZE, "Field fits the buffer.");
static_assert((int)EVENT_DESCRIPTION_SIZE <= (int)TEXT_BOX_SIZE_MAX, "Every field takes a paste.");

// Mid-tones: dark enough for white text on a bar, yet bright enough to stand out from the dark
// surfaces. Picked by event id, so an event keeps its color in every view.
static const Color event_colors[] = {
    { 59, 111, 216, 255 },   // Blue
    { 200, 70, 60, 255 },    // Red
    { 46, 138, 78, 255 },    // Green
    { 130, 80, 200, 255 },   // Purple
    { 140, 90, 60, 255 },    // Brown
    { 24, 128, 140, 255 },   // Teal
    { 192, 90, 16, 255 },    // Orange
    { 180, 60, 140, 255 },   // Magenta
};
enum { EVENT_COLOR_COUNT = sizeof(event_colors) / sizeof(event_colors[0]) };

// Light, because error text is drawn on the dark surfaces, not on a bar.
static const Color error_color = { 248, 113, 113, 255 };
// Every problem has this color, darker than any event color, so that problems read as problems
// at a glance rather than as one more event.
static const Color problem_color = { 160, 36, 48, 255 };
// A solved problem or used opportunity turns green, the usual color for "resolved", dark enough
// for white text.
static const Color target_done_color = { 40, 140, 76, 255 };
// Every opportunity is gold, unlike any event color, so that a chance stands out as one. A
// missed one turns gray, so that it recedes like anything past.
static const Color opportunity_color = { 168, 124, 16, 255 };
static const Color opportunity_missed_color = { 92, 98, 110, 255 };

// One color of the theme: a raygui DEFAULT property and its value, as 0xRRGGBBAA.
typedef struct Theme_Color {
    uint32_t rgba;
    int32_t property;
} Theme_Color;

// The dark theme, shared by the desktop and Android apps. Every control, and every surface the
// apps draw themselves, reads its colors from raygui's style table, so this table is the whole
// theme. Near-black surfaces with a slight blue cast, borders just above the surfaces so that
// the grid recedes behind the entries, and one blue accent for what is selected or today.
static const Theme_Color theme_colors[] = {
    { 0x0E1014FF, BACKGROUND_COLOR },       // Window, panel, days outside the month
    { 0x262A33FF, LINE_COLOR },             // Panel outline
    { 0x171A20FF, BASE_COLOR_NORMAL },      // Day cells, cards, buttons, navigation
    { 0x272C35FF, BORDER_COLOR_NORMAL },
    { 0xE4E7ECFF, TEXT_COLOR_NORMAL },
    { 0x1F232BFF, BASE_COLOR_FOCUSED },     // Hovered
    { 0x3A414DFF, BORDER_COLOR_FOCUSED },
    { 0xFFFFFFFFu, TEXT_COLOR_FOCUSED },
    { 0x1D2A45FF, BASE_COLOR_PRESSED },     // Accent tint: active toggle, edited text box
    { 0x7AA2F7FF, BORDER_COLOR_PRESSED },   // The accent: selection, focus, today
    { 0xDCE6FFFF, TEXT_COLOR_PRESSED },
    { 0x20242BFF, BORDER_COLOR_DISABLED },
    { 0x13161BFF, BASE_COLOR_DISABLED },
    // Secondary text as well, so it keeps a contrast of about 5:1 on the cells and cards.
    { 0x828A96FF, TEXT_COLOR_DISABLED },
};
enum { THEME_COLOR_COUNT = sizeof(theme_colors) / sizeof(theme_colors[0]) };

// How many days the calendar shows at once. Every range uses the same day cells; a week is one
// row of seven, and a day one cell the width of the calendar, so that whole titles show. A week
// or a day keeps only the items without a time in its cells, and lays out the items with a time
// on a timeline of the day's hours below them, as most calendar apps do.
typedef enum Calendar_Range {
    RANGE_MONTH,
    RANGE_WEEK,
    RANGE_DAY,
    RANGE_COUNT,
} Calendar_Range;

// raygui separates toggle group items with semicolons; the order is Calendar_Range's.
static const char range_labels[] = "Month;Week;Day";

typedef struct Key_Step {
    int32_t key;     // raylib KeyboardKey
    int32_t days;
} Key_Step;

// The arrow keys move the selection by a day or a week, whatever the range; Page Up and Page
// Down move by the range (see range_step).
static const Key_Step key_steps[] = {
    { KEY_LEFT, -1 },
    { KEY_RIGHT, 1 },
    { KEY_UP, -7 },
    { KEY_DOWN, 7 },
};
enum { KEY_STEP_COUNT = sizeof(key_steps) / sizeof(key_steps[0]) };

// A list that scrolls by the wheel, or by dragging it with a finger or the mouse.
typedef struct Scroll {
    float offset_pixels;       // How far the list is scrolled down
    float drag_y;              // Pointer y where the press in the list began
    float drag_offset_pixels;  // offset_pixels when that press began
    bool pressed;              // The pointer went down in the list and is held
    bool dragging;             // That press moved far enough to scroll instead of click
} Scroll;

typedef struct Form {
    // Links between link targets and the tasks linked to them, loaded when the form opens: an
    // existing target's tasks, or else the targets a task can be linked to, with the user's
    // choices in their linked flags until the form is saved.
    Linked_Task tasks[LINK_COUNT_MAX];
    Link_Target targets[LINK_COUNT_MAX];
    Scroll targets_scroll;
    int64_t editing_id;    // Event being edited, or 0 for a new event
    int32_t field_active;  // Form_Field being edited, or FORM_FIELD_NONE
    int32_t task_count;
    int32_t target_count;
    int repeat_option;     // Repeat_Option; int because raygui's combo box writes it
    int32_t kind;          // Event_Kind
    bool open;
    bool links_loaded;       // False after a failed load, so that saving keeps the stored links
    bool links_truncated;    // More links or targets exist than were loaded
    char text[FORM_FIELD_COUNT][EVENT_DESCRIPTION_SIZE];
    char error[FORM_ERROR_SIZE];  // Why the last save failed, or empty
} Form;

// A font together with the one size it is drawn at. raylib rasterizes a font once, so drawing
// it at another size would blur or pixelate it.
typedef struct Text_Style {
    Font font;
    float size_pixels;  // Layout pixels
    float scale;        // Screen pixels per layout pixel that the font was rasterized for
} Text_Style;

typedef struct Fonts {
    Text_Style ui;            // Controls, labels and day numbers; also raygui's font
    Text_Style small;         // Calendar entries and the details on panel cards
    Text_Style small_strong;  // Titles on panel cards
    Text_Style heading;       // Month title and the selected date
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
    Task_Completion completions[COMPLETION_COUNT_MAX];  // Done task occurrences near the view
    Backend *backend;
    Fonts fonts;
    uint64_t events_revision;       // Backend revision that events[] was loaded at
    int64_t event_focused_id;       // Selected item on the panel, ringed in the grid, or 0
    int64_t event_delete_id;        // Event waiting for the user to confirm deleting, or 0
    Date today;
    int32_t now_minute;  // Minute of day, read from the clock together with today
    Date selected;
    Date events_month;              // First day of the month that events[] was loaded for
    // First day shown, set by app_sync. The six weeks loaded for a month contain every week and
    // day within it, so week and day views need no loading of their own.
    Date visible_first;
    int32_t event_count;
    int range;                      // Calendar_Range; int because raygui's toggle group writes it
    int32_t completion_count;
    Scroll panel_scroll;            // The panel's list of cards
    // The week and day views' hours. Kept when moving between days and ranges, so that the
    // hours in sight stay put, as on a paper planner.
    Scroll timeline_scroll;
    Form form;
    bool events_stale;              // Reload events[] even though the month is unchanged
    bool events_truncated;
    bool touch;                     // See App_Config
    bool input_enabled;             // See app_set_input_enabled
    bool created;
    char status[STATUS_SIZE];       // Message for the status bar, cleared on navigation
    char info[INFO_SIZE];           // Shown first in the status bar, see app_set_info
} App;

// A text broken into lines no wider than a given width: where each line starts in the text and
// how long it is. The text is not copied.
typedef struct Text_Lines {
    int32_t start[DESCRIPTION_LINES_MAX];
    int32_t length[DESCRIPTION_LINES_MAX];
    int32_t count;
    bool cut;  // The text continues past the last line
} Text_Lines;

// An item with a time on one day of the timeline: the minutes its block covers, and its place
// among the items it overlaps.
typedef struct Timeline_Item {
    int32_t event_index;    // Into App.events
    int32_t minute_top;     // 0 ... MINUTES_PER_DAY - TIMELINE_ITEM_MINUTES_MIN
    int32_t minute_bottom;  // At least TIMELINE_ITEM_MINUTES_MIN after minute_top
    int32_t lane;           // 0 ... lane_count - 1, from the left
    // Lanes of the group of items that overlap it, directly or through others, which share the
    // day's width evenly.
    int32_t lane_count;
} Timeline_Item;

typedef struct Layout {
    Rectangle header;
    Rectangle range_bar;  // The Month/Week/Day selector; empty when not shown
    Rectangle weekdays;
    Rectangle grid;       // The day cells; in a week or day, the row above the timeline
    // The hours of a week or day below the day cells, with the hour labels in a gutter at the
    // left, so that the days start at grid.x; empty in a month.
    Rectangle timeline;
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
static Text_Style fonts_load_style(const char *directory, const char *file_name,
                                   int32_t size_pixels, float scale, const int *codepoints,
                                   bool *loaded);
static void fonts_unload(Fonts *fonts);
static void app_handle_keys(App *app, const Layout *layout);
static void app_select(App *app, Date date);
static void app_sync(App *app);
static void app_load_events(App *app, Date month, uint64_t revision);
static const Event *app_event_find(const App *app, int64_t id);
static int64_t app_event_first_on_day(const App *app, int32_t day_number);
static void app_form_open(App *app, Date date, int32_t kind);
static void app_form_open_edit(App *app, const Event *event);
static void app_form_load_links(App *app);
static void app_form_clipboard(App *app);
static void app_form_submit(App *app);
static bool app_form_save_links(App *app, int64_t id, int32_t kind, bool inserted);
static void app_event_delete(App *app, int64_t id);
static bool app_task_done(const App *app, int64_t id, int32_t start_day);
static Date_Time app_now(const App *app);
static void app_task_set_done(App *app, const Event *event, int32_t start_day, bool done);
static Layout layout_compute(float width, float height, bool form_open, int32_t range,
                             int32_t untimed_count);
static Layout layout_compute_portrait(float width, float height, bool form_open,
                                      int32_t range, int32_t untimed_count);
static void layout_set_grid(Layout *layout, int32_t range, int32_t untimed_count);
static void layout_set_grid_timeline(Layout *layout, int32_t untimed_count);
static int32_t app_untimed_count_max(const App *app);
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
static void draw_grid_selection(const App *app, const Layout *layout, int32_t first_day);
static void draw_timeline(App *app, const Layout *layout, int32_t first_day);
static void draw_timeline_hours(const App *app, const Layout *layout, float top);
static void draw_timeline_day(const App *app, Rectangle day, int32_t day_number, bool hovered);
static void draw_timeline_day_background(Rectangle day, int32_t busy_top, int32_t busy_bottom,
                                         bool hovered);
static void draw_timeline_item(const App *app, const Event *event, Rectangle block,
                               int32_t day_number);
static int32_t timeline_items_collect(const App *app, int32_t day_number, Timeline_Item *items);
static Timeline_Item timeline_item_make(const Event *event, int32_t event_index);
static void timeline_items_sort(Timeline_Item *items, int32_t count);
static bool timeline_item_is_before(const Timeline_Item *a, const Timeline_Item *b);
static void timeline_items_assign_lanes(Timeline_Item *items, int32_t count);
static int32_t timeline_lane_find(const int32_t *lane_bottoms, int32_t lane_count,
                                  int32_t minute_top);
static void timeline_items_set_lane_count(Timeline_Item *items, int32_t first_index,
                                          int32_t end_index, int32_t lane_count);
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
static void draw_event_label(const App *app, const Event *event, const char *label,
                             Rectangle area, bool done);
static void draw_bar_outline(Rectangle bar, bool starts_here, bool ends_here, Color color);
static void draw_check_mark(Rectangle box, bool done, Color color);
static void draw_tick(Rectangle area, float thickness, Color color);
static void draw_panel(App *app, Rectangle panel);
static void draw_panel_events(App *app, Rectangle body);
static float draw_panel_events_height(const App *app, float card_width);
static void draw_panel_events_list(App *app, Rectangle list);
static void scroll_update(Scroll *scroll, Rectangle list, float content_height);
static void draw_scrollbar(Rectangle list, float content_height, float offset_pixels);
static bool draw_panel_event_card(App *app, const Event *event, Rectangle card);
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event, Date_Time now,
                                          Rectangle card);
static void draw_panel_legend(const Fonts *fonts, Rectangle area);
static void draw_panel_form(App *app, Rectangle body);
static void draw_panel_form_kinds(App *app, Rectangle body, bool editing);
static void draw_panel_form_buttons(App *app, Rectangle body, float y);
static float draw_panel_form_rows_end(float y);
static float draw_panel_form_fields(App *app, Rectangle body, float y);
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y, float end_y);
static void draw_panel_form_target_tasks(const App *app, Rectangle body, float y, float end_y);
static void draw_panel_form_targets(App *app, Rectangle body, float y, float end_y);
static bool draw_panel_form_targets_row(const App *app, const Link_Target *target,
                                        Rectangle row, bool clickable);
static int32_t form_targets_linked_count(const Form *form);
static bool form_deadline_is_missed(const Form *form, Date_Time now, bool tasks_done);
static void draw_panel_form_field(const Fonts *fonts, Form *form, int32_t field,
                                  const char *label, Rectangle box);
static void draw_status(App *app, Rectangle status);
static void form_open(Form *form, Date date, int32_t kind);
static void form_open_edit(Form *form, const Event *event);
static void form_close(Form *form);
static int32_t form_field_next(const Form *form, bool backwards);
static bool form_field_visible(const Form *form, int32_t field);
static int32_t form_repeat_option(const Form *form);
static float event_card_height(const Fonts *fonts, const Event *event, bool confirming,
                               float card_width);
static Text_Lines text_wrap(const Text_Style *style, const char *text, float width_pixels);
static bool text_line_fits(const Text_Style *style, const char *text, int32_t length,
                           float width_pixels);
static const char *form_parse(const Form *form, Date today, Event *event);
static const char *form_parse_problem(const Form *form, Date today, Event *event);
static const char *form_parse_order(const Event *event);
static bool form_parse_time(const char *text, int32_t *minute_of_day);
static bool form_parse_repeat_days(const char *text, int32_t *days);
static bool event_occurs_on_day(const Event *event, int32_t day_number);
static bool event_created_on_day(const Event *event, int32_t day_number);
static bool event_is_timed(const Event *event);
static bool event_in_day_cell(const Event *event, int32_t range);
static bool event_is_missed(const Event *event, Date_Time now);
static Color event_color(const Event *event, Date_Time now);
static void event_format_label(const Event *event, bool with_time, Date_Time now, char *buffer,
                               size_t buffer_size);
static void event_format_range(const Event *event, Date_Time now, char *buffer,
                               size_t buffer_size);
static void event_format_range_dates(const Event *event, char *buffer, size_t buffer_size);
static void event_format_created(const Event *event, Date_Time now, char *buffer,
                                 size_t buffer_size);
static void event_format_location(const Event *event, char *buffer, size_t buffer_size);
static void text_copy_trimmed(char *buffer, size_t buffer_size, const char *text);
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size);
static void theme_apply(void);
static Color style_color(int32_t property);
static void draw_text(const Text_Style *style, const char *text, float x, float y, Color color);
static float draw_text_round(float position, float scale);
static Vector2 measure_text(const Text_Style *style, const char *text);
static void draw_text_centered(const Text_Style *style, const char *text, Rectangle area,
                               Color color);
static void scissor_begin(const Fonts *fonts, float x, float y, float width, float height);
static void scissor_end(const Fonts *fonts);

App *app_create(Backend *backend, const App_Config *config) {
    // Static rather than on the stack, because App holds every loaded event; one per program.
    static App app;
    assert(!app.created);
    assert(backend != NULL);
    assert(config->scale > 0.0f);
    app.created = true;
    app.backend = backend;
    app.touch = config->touch;
    app.input_enabled = true;

    theme_apply();
    fonts_load(&app.fonts, config->font_directory, config->scale);
    GuiSetFont(app.fonts.ui.font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, TEXT_SIZE_PIXELS);
    // Rubik's advances already space its glyphs; raylib's default font needs extra room.
    GuiSetStyle(DEFAULT, TEXT_SPACING, app.fonts.loaded ? 0 : 2);
    if (!app.fonts.loaded) {
        snprintf(app.status, sizeof(app.status), "Rubik font not found: using the default.");
    }

    const Date_Time now = date_time_now();
    app.today = now.date;
    app.now_minute = now.minute_of_day;
    app.selected = app.today;
    app.events_month = (Date){ app.today.year, app.today.month, 1 };
    // Before the first app_sync, because the first frame's layout reads it.
    app.visible_first = range_first_date(&app);
    app.timeline_scroll.offset_pixels =
        (float)(TIMELINE_SCROLL_HOUR_START * TIMELINE_HOUR_HEIGHT_PIXELS);
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
                                   app->range, app_untimed_count_max(app));
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
    // Again, because a key may have changed the range or opened the form, and loading may have
    // changed the items without a time.
    layout = layout_compute(area.width / scale, area.height / scale, app->form.open, app->range,
                            app_untimed_count_max(app));

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

void app_draw_text(const App *app, App_Text style, const char *text, float x, float y) {
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
    assert(false); // Not an App_Text.
}

float app_measure_text(const App *app, App_Text style, const char *text) {
    switch (style) {
    case APP_TEXT_HEADING:
        return measure_text(&app->fonts.heading, text).x;
    case APP_TEXT_NORMAL:
    case APP_TEXT_DIM:
        return measure_text(&app->fonts.ui, text).x;
    case APP_TEXT_SMALL:
        return measure_text(&app->fonts.small, text).x;
    }
    assert(false); // Not an App_Text.
    return 0.0f;
}

void app_set_info(App *app, const char *info) {
    snprintf(app->info, sizeof(app->info), "%s", info);
}

void app_set_status(App *app, const char *status) {
    snprintf(app->status, sizeof(app->status), "%s", status);
}

void app_discard_edits(App *app) {
    assert(app->created);
    if (app->form.open) {
        form_close(&app->form);
    }
    assert(!app->form.open);
    app->event_delete_id = 0;
    app->event_focused_id = 0;
    app->events_stale = true;
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
static Text_Style fonts_load_style(const char *directory, const char *file_name,
                                   int32_t size_pixels, float scale, const int *codepoints,
                                   bool *loaded) {
    assert(size_pixels > 0);
    assert(scale > 0.0f);
    Text_Style style = { GetFontDefault(), (float)size_pixels, scale };
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
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            app_form_clipboard(app);
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
    // Over the calendar only, so that they mean nothing over the panel: a sideways swipe on a
    // touch screen, or the wheel over the day cells, moves by the range. Over the timeline the
    // wheel scrolls the hours instead (see draw_timeline).
    const Vector2 mouse = GetMousePosition();
    const bool over_grid = CheckCollisionPointRec(mouse, layout->grid);
    if (over_grid || CheckCollisionPointRec(mouse, layout->timeline)) {
        if (app->touch && IsGestureDetected(GESTURE_SWIPE_LEFT)) {
            target = range_step(target, app->range, 1);
        }
        if (app->touch && IsGestureDetected(GESTURE_SWIPE_RIGHT)) {
            target = range_step(target, app->range, -1);
        }
    }
    if (over_grid) {
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
    app->panel_scroll.offset_pixels = 0.0f;
    app->status[0] = '\0';
}

// Brings derived state up to date before drawing: the date and time, the loaded events and the
// focused event, which is always one of the events on the selected date.
static void app_sync(App *app) {
    const Date_Time now = date_time_now();
    app->today = now.date;
    app->now_minute = now.minute_of_day;
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
        const Task_Completion *completion = &app->completions[i];
        if (completion->event_id == id && completion->occurrence_start_day == start_day) {
            return true;
        }
    }
    return false;
}

// The moment app_sync last read from the clock, so that a frame draws one time throughout.
static Date_Time app_now(const App *app) {
    assert(date_is_valid(app->today));
    assert(app->now_minute >= 0);
    return (Date_Time){ app->today, app->now_minute };
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
        // Saving again after the links fail below updates this entry rather than adding another.
        app->form.editing_id = id;
    }
    if (!app_form_save_links(app, id, event.kind, !editing)) {
        return;
    }

    form_close(&app->form);
    // Show the event at once, focused, so that its card is visible. An edited event that
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

// Ctrl+C copies the field being edited, Ctrl+X cuts it, and Ctrl+V pastes at its cursor.
// raygui's text box has no selection, so copying and cutting take the whole field, which the
// status bar says. GLFW drops the control characters these keys type, so the text box itself
// sees nothing of them. On Android raylib has no clipboard, and pasting finds nothing.
static void app_form_clipboard(App *app) {
    Form *form = &app->form;
    const int32_t field = form->field_active;
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    char *text = form->text[field];
    const bool cut = IsKeyPressed(KEY_X);
    if (IsKeyPressed(KEY_C) || cut) {
        if (text[0] == '\0') {
            return; // Copying nothing would only empty the clipboard.
        }
        SetClipboardText(text);
        if (cut) {
            text[0] = '\0';
            text_box_cursor_index_set(0);
        }
        snprintf(app->status, sizeof(app->status), "%s the whole field.",
                 cut ? "Cut" : "Copied");
        return;
    }
    if (IsKeyPressed(KEY_V)) {
        // NULL when the clipboard is empty, holds no text, or does not exist.
        const char *clipboard = GetClipboardText();
        if (clipboard == NULL) {
            return;
        }
        int32_t cursor_index = text_box_cursor_index();
        const int32_t length = (int32_t)strlen(text);
        if (cursor_index > length) {
            cursor_index = length; // raygui clamps it only when it next draws the box.
        }
        text_box_cursor_index_set(text_box_paste(text, form_field_sizes[field], cursor_index,
                                                 clipboard));
    }
}

// Opens the form for a new entry. The targets a task can be linked to are loaded whatever the
// kind, because the kind can still be switched to task.
static void app_form_open(App *app, Date date, int32_t kind) {
    app->event_delete_id = 0;
    form_open(&app->form, date, kind);
    app_form_load_links(app);
}

static void app_form_open_edit(App *app, const Event *event) {
    assert(event->id > 0);
    form_open_edit(&app->form, event);
    app_form_load_links(app);
}

// An existing link target's form lists its tasks; every other form offers the targets a task
// can be linked to. A failure shows in the form and leaves links_loaded false, so that saving
// cannot erase links that were never shown.
static void app_form_load_links(App *app) {
    Form *form = &app->form;
    assert(form->open);
    form->links_loaded = false;
    form->links_truncated = false;
    form->task_count = 0;
    form->target_count = 0;
    int32_t count = 0;
    if (event_kind_is_target(form->kind) && form->editing_id != 0) {
        count = backend_load_target_tasks(app->backend, form->editing_id, form->tasks,
                                          LINK_COUNT_MAX, &form->links_truncated);
        form->task_count = count < 0 ? 0 : count;
    } else {
        count = backend_load_task_targets(app->backend, form->editing_id, false, form->targets,
                                          LINK_COUNT_MAX, &form->links_truncated);
        form->target_count = count < 0 ? 0 : count;
    }
    if (count < 0) {
        snprintf(form->error, sizeof(form->error), "%s", backend_error(app->backend));
        return;
    }
    assert(count <= LINK_COUNT_MAX);
    form->links_loaded = true;
}

// Saves the targets checked in a task's form. An entry saved as an event loses its links, as
// it may have been a task until now. Returns false, with the form's error set, on failure.
static bool app_form_save_links(App *app, int64_t id, int32_t kind, bool inserted) {
    const Form *form = &app->form;
    assert(id > 0);
    // A target's links are made from its tasks' forms, and a new event has none.
    if (event_kind_is_target(kind)) {
        return true;
    }
    if (kind == EVENT_KIND_EVENT && inserted) {
        return true;
    }
    // Links that could not be loaded were never shown, so they are kept as stored.
    if (kind == EVENT_KIND_TASK && !form->links_loaded) {
        return true;
    }
    int64_t target_ids[LINK_COUNT_MAX];
    int32_t target_count = 0;
    for (int32_t i = 0; kind == EVENT_KIND_TASK && i < form->target_count; i++) {
        if (form->targets[i].linked) {
            target_ids[target_count] = form->targets[i].target_id;
            target_count++;
        }
    }
    assert(target_count == (kind == EVENT_KIND_TASK ? form_targets_linked_count(form) : 0));
    if (!backend_set_task_targets(app->backend, id, target_ids, target_count)) {
        snprintf(app->form.error, sizeof(app->form.error), "%s", backend_error(app->backend));
        return false;
    }
    return true;
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
// untimed_count is app_untimed_count_max: how many rows of bars a week or day's cells need.
static Layout layout_compute(float width, float height, bool form_open, int32_t range,
                             int32_t untimed_count) {
    if (width < height) {
        return layout_compute_portrait(width, height, form_open, range, untimed_count);
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
    layout_set_grid(&layout, range, untimed_count);
    return layout;
}

// Rows and columns of day cells for the range, filling the grid rectangle; in a week or day,
// the grid is first split into the day cells and the timeline.
static void layout_set_grid(Layout *layout, int32_t range, int32_t untimed_count) {
    assert(range >= 0);
    assert(range < RANGE_COUNT);
    layout->columns = range == RANGE_DAY ? 1 : GRID_COLUMN_COUNT;
    layout->rows = range == RANGE_MONTH ? GRID_ROW_COUNT : 1;
    assert(layout->columns * layout->rows == range_day_count(range));
    if (range != RANGE_MONTH) {
        layout_set_grid_timeline(layout, untimed_count);
    }
    layout->cell_width = layout->grid.width / (float)layout->columns;
    layout->cell_height = layout->grid.height / (float)layout->rows;
    // The minimum window size guarantees room for every part.
    assert(layout->cell_width > 0.0f);
    assert(layout->cell_height > 0.0f);
}

// The day cells keep the top of the grid, as tall as the busiest day's items without a time
// need, and the timeline takes the rest. The gutter for the hour labels is cut from the left
// of both, and of the weekday names, so that each day lines up with its hours.
static void layout_set_grid_timeline(Layout *layout, int32_t untimed_count) {
    assert(untimed_count >= 0);
    assert(untimed_count <= EVENT_COUNT_MAX);
    const float gutter = (float)TIMELINE_GUTTER_WIDTH_PIXELS;
    const float slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    const Rectangle area = layout->grid;
    float height = (float)(CELL_TOP_LINE_PIXELS + CELL_BOTTOM_MARGIN_PIXELS) +
                   (float)untimed_count * slot_height;
    const float height_max = area.height * UNTIMED_HEIGHT_PERCENT_MAX / 100.0f;
    if (height > height_max) {
        height = height_max;
    }
    layout->grid = (Rectangle){ area.x + gutter, area.y, area.width - gutter, height };
    layout->timeline = (Rectangle){ area.x, area.y + height, area.width, area.height - height };
    layout->weekdays.x += gutter;
    layout->weekdays.width -= gutter;
    // The minimum window size guarantees room for both.
    assert(layout->grid.width > 0.0f);
    assert(layout->timeline.height > 0.0f);
}

// The most items without a time on any one day shown, which sets how tall a week or day's cells
// are. A month's cells share the grid evenly, so it needs none.
static int32_t app_untimed_count_max(const App *app) {
    if (app->range == RANGE_MONTH) {
        return 0;
    }
    const int32_t first_day = date_to_day_number(app->visible_first);
    const int32_t day_count = range_day_count(app->range);
    int32_t count_max = 0;
    for (int32_t day = 0; day < day_count; day++) {
        int32_t count = 0;
        for (int32_t i = 0; i < app->event_count; i++) {
            const Event *event = &app->events[i];
            if (!event_is_timed(event) && event_occurs_on_day(event, first_day + day)) {
                count++;
            }
        }
        if (count > count_max) {
            count_max = count;
        }
    }
    assert(count_max <= app->event_count);
    return count_max;
}

static int32_t range_day_count(int32_t range) {
    switch ((Calendar_Range)range) {
    case RANGE_MONTH:
        return GRID_CELL_COUNT;
    case RANGE_WEEK:
        return GRID_COLUMN_COUNT;
    case RANGE_DAY:
        return 1;
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a Calendar_Range.
    return 1;
}

// The date one range before (direction -1) or after (1) date: a month, a week or a day.
static Date range_step(Date date, int32_t range, int32_t direction) {
    assert(direction == -1 || direction == 1);
    switch ((Calendar_Range)range) {
    case RANGE_MONTH:
        return date_add_months(date, direction);
    case RANGE_WEEK:
        return date_add_days(date, 7 * direction);
    case RANGE_DAY:
        return date_add_days(date, direction);
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a Calendar_Range.
    return date;
}

// A month starts on the Monday of the week that holds its first day, a week on its Monday, and
// a day view shows the selected day.
static Date range_first_date(const App *app) {
    switch ((Calendar_Range)app->range) {
    case RANGE_MONTH:
        return grid_first_date(app->events_month);
    case RANGE_WEEK:
        return date_add_days(app->selected, -date_weekday(app->selected));
    case RANGE_DAY:
        return app->selected;
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a Calendar_Range.
    return app->selected;
}

// "October 2026"; "12 - 18 October 2026", or with both months or years when the week spans
// them; "Tue 20 October 2026".
static void range_title(const App *app, char *buffer, size_t buffer_size) {
    const Date first = app->visible_first;
    const Date last = date_add_days(first, GRID_COLUMN_COUNT - 1);
    switch ((Calendar_Range)app->range) {
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
    assert(false); // Not a Calendar_Range.
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
                                      int32_t range, int32_t untimed_count) {
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
    // The panel goes below the whole grid, before it is split into cells and timeline.
    float panel_y = layout.grid.y + layout.grid.height + padding;
    layout_set_grid(&layout, range, untimed_count);
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
    const Text_Style *style = &app->fonts.heading;
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
    if (layout->timeline.height > 0.0f) {
        draw_timeline(app, layout, first_day);
    }
    // Last, so that neither the cells nor the timeline cover it.
    draw_grid_selection(app, layout, first_day);
}

// An accent outline around the selected day; in a week or day, down through its hours too.
static void draw_grid_selection(const App *app, const Layout *layout, int32_t first_day) {
    const int32_t cell_index = date_to_day_number(app->selected) - first_day;
    // A click this frame may have selected a day outside the range drawn.
    if (cell_index < 0 || cell_index >= layout->rows * layout->columns) {
        return;
    }
    Rectangle cell = grid_cell_rectangle(layout, cell_index);
    if (layout->timeline.height > 0.0f) {
        cell.height = layout->timeline.y + layout->timeline.height - cell.y;
    }
    Rectangle inner = { cell.x + 2, cell.y + 2, cell.width - 4, cell.height - 4 };
    assert(inner.width > 0.0f);
    assert(inner.height > 0.0f);
    DrawRectangleLinesEx(inner, 2, style_color(BORDER_COLOR_PRESSED));
}

// The hours of a week or day, scrolled by the wheel or by dragging, with each day's items that
// have a time placed at their times. Clicking a day's hours selects the day, as its cell does.
static void draw_timeline(App *app, const Layout *layout, int32_t first_day) {
    const Rectangle timeline = layout->timeline;
    assert(timeline.height > 0.0f);
    assert(layout->rows == 1);
    scroll_update(&app->timeline_scroll, timeline, (float)TIMELINE_HEIGHT_PIXELS);
    // Where 00:00 is: above the timeline while it is scrolled down.
    const float top = timeline.y - app->timeline_scroll.offset_pixels;
    // Every drawing below, and the scissor rectangles of the labels, stays within the timeline.
    app->fonts.clip = timeline;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, timeline.x, timeline.y, timeline.width, timeline.height);

    for (int32_t column = 0; column < layout->columns; column++) {
        Rectangle area = { layout->grid.x + (float)column * layout->cell_width, timeline.y,
                           layout->cell_width, timeline.height };
        bool hovered = CheckCollisionPointRec(GetMousePosition(), area);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            app_select(app, date_from_day_number(first_day + column));
        }
        Rectangle day = { area.x, top, area.width, (float)TIMELINE_HEIGHT_PIXELS };
        draw_timeline_day(app, day, first_day + column, hovered);
        DrawRectangleLinesEx(area, 1, style_color(BORDER_COLOR_NORMAL));
    }
    draw_timeline_hours(app, layout, top);

    app->fonts.clipping = false;
    EndScissorMode();
}

// The hour labels in the gutter left of the days; each day draws its own hour lines.
static void draw_timeline_hours(const App *app, const Layout *layout, float top) {
    const Rectangle timeline = layout->timeline;
    const float days_left = layout->grid.x;
    assert(days_left > timeline.x);
    assert(days_left < timeline.x + timeline.width);
    const Text_Style *style = &app->fonts.small;
    for (int32_t hour = 0; hour < MINUTES_PER_DAY / 60; hour++) {
        float y = top + (float)(hour * TIMELINE_HOUR_HEIGHT_PIXELS);
        // Hours scrolled out of sight would be clipped anyway; skipping them saves the text.
        bool visible = y + TIMELINE_HOUR_HEIGHT_PIXELS > timeline.y &&
                       y < timeline.y + timeline.height;
        if (visible) {
            char label[TIME_TEXT_SIZE];
            time_format(hour * 60, label, sizeof(label));
            float width = measure_text(style, label).x;
            // Below the line rather than across it, so that 00:00 is not cut off at the top.
            draw_text(style, label, days_left - 6 - width, y + 3, style_color(TEXT_COLOR_DISABLED));
        }
    }
}

// day is the column from 00:00 to 24:00, which may reach outside the timeline that clips it.
// Items that overlap share the day's width side by side.
static void draw_timeline_day(const App *app, Rectangle day, int32_t day_number, bool hovered) {
    assert(day.width > 0.0f);
    assert(day.height == (float)TIMELINE_HEIGHT_PIXELS);
    // On the stack, because it is needed only while one day is drawn; about 10 KB.
    Timeline_Item items[EVENT_COUNT_MAX] = { 0 };
    const int32_t count = timeline_items_collect(app, day_number, items);
    timeline_items_sort(items, count);
    timeline_items_assign_lanes(items, count);

    // The busy part of the day runs from the first item's start to the last one's end; sorted
    // by start, the first item starts first, but any item may end last.
    int32_t busy_top = MINUTES_PER_DAY;
    int32_t busy_bottom = 0;
    if (count > 0) {
        busy_top = items[0].minute_top;
    }
    for (int32_t i = 0; i < count; i++) {
        if (items[i].minute_bottom > busy_bottom) {
            busy_bottom = items[i].minute_bottom;
        }
    }
    draw_timeline_day_background(day, busy_top, busy_bottom, hovered);

    const float pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    // Kept free at the sides of the day, so that items do not touch the day's borders.
    const float inset = 2.0f;
    for (int32_t i = 0; i < count; i++) {
        const Timeline_Item *item = &items[i];
        assert(item->lane < item->lane_count);
        float lane_width = (day.width - 2 * inset) / (float)item->lane_count;
        // One pixel less each way, so that neighbouring blocks of one color stay apart.
        Rectangle block = { day.x + inset + (float)item->lane * lane_width,
                            day.y + (float)item->minute_top * pixels_per_minute,
                            lane_width - 1,
                            (float)(item->minute_bottom - item->minute_top) * pixels_per_minute -
                                1 };
        draw_timeline_item(app, &app->events[item->event_index], block, day_number);
    }
}

// The free hours before the day's first item and after its last are darker, so that the busy
// part of the day stands out at a glance; a day without timed items is dark all through, which
// busy_top past busy_bottom means. A hovered day is lighter in both parts, as cells are.
static void draw_timeline_day_background(Rectangle day, int32_t busy_top, int32_t busy_bottom,
                                         bool hovered) {
    assert(busy_top >= 0);
    assert(busy_bottom <= MINUTES_PER_DAY);
    // The window's background is the theme's darker surface, as for days outside the month;
    // hovered, the disabled base lies between it and the normal cells.
    DrawRectangleRec(day, style_color(hovered ? BASE_COLOR_DISABLED : BACKGROUND_COLOR));
    if (busy_top < busy_bottom) {
        const float pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
        Rectangle busy = { day.x, day.y + (float)busy_top * pixels_per_minute, day.width,
                           (float)(busy_bottom - busy_top) * pixels_per_minute };
        DrawRectangleRec(busy, style_color(hovered ? BASE_COLOR_FOCUSED : BASE_COLOR_NORMAL));
    }
    for (int32_t hour = 0; hour < MINUTES_PER_DAY / 60; hour++) {
        float y = day.y + (float)(hour * TIMELINE_HOUR_HEIGHT_PIXELS);
        DrawRectangleRec((Rectangle){ day.x, y, day.width, 1 }, style_color(BORDER_COLOR_NORMAL));
    }
}

// A block from the item's start to its end, with its time and title on the top line.
static void draw_timeline_item(const App *app, const Event *event, Rectangle block,
                               int32_t day_number) {
    assert(event_duration_days(event) == 0);
    assert(block.width > 0.0f);
    assert(block.height > 0.0f);
    // A one-day item's occurrence starts on the day it is drawn on.
    const bool task = event->kind == EVENT_KIND_TASK;
    const bool done = task && app_task_done(app, event->id, day_number);
    const Color color = event_color(event, app_now(app));
    if (task) {
        DrawRectangleRec(block, Fade(color, done ? TASK_DONE_FILL_ALPHA : TASK_FILL_ALPHA));
        DrawRectangleLinesEx(block, 1, Fade(color, done ? TASK_DONE_OUTLINE_ALPHA : 1.0f));
    } else {
        DrawRectangleRec(block, color);
    }

    char label[TEXT_LINE_SIZE];
    event_format_label(event, true, app_now(app), label, sizeof(label));
    draw_event_label(app, event, label, block, done);
}

// The items with a time that occur on day_number, in App.events' order. Returns how many.
static int32_t timeline_items_collect(const App *app, int32_t day_number, Timeline_Item *items) {
    assert(app->event_count <= EVENT_COUNT_MAX);
    int32_t count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        if (event_is_timed(event) && event_occurs_on_day(event, day_number)) {
            items[count] = timeline_item_make(event, i);
            count++;
        }
    }
    assert(count <= app->event_count);
    return count;
}

// The minutes an item's block covers: from its start to its end time, or an hour from the one
// time it has; at least TIMELINE_ITEM_MINUTES_MIN, and within the day.
static Timeline_Item timeline_item_make(const Event *event, int32_t event_index) {
    assert(event_is_timed(event));
    assert(event_index >= 0);
    int32_t top = event->start_minute;
    int32_t bottom = event->end_minute;
    if (top == TIME_NONE) {
        top = bottom - TIMELINE_ITEM_MINUTES_DEFAULT;
    }
    if (bottom == TIME_NONE) {
        bottom = top + TIMELINE_ITEM_MINUTES_DEFAULT;
    }
    top = top < 0 ? 0 : top;
    bottom = bottom > MINUTES_PER_DAY ? MINUTES_PER_DAY : bottom;
    if (bottom - top < TIMELINE_ITEM_MINUTES_MIN) {
        // Lengthened downwards, or upwards at the end of the day, so that it stays in the day.
        if (top + TIMELINE_ITEM_MINUTES_MIN <= MINUTES_PER_DAY) {
            bottom = top + TIMELINE_ITEM_MINUTES_MIN;
        } else {
            bottom = MINUTES_PER_DAY;
            top = MINUTES_PER_DAY - TIMELINE_ITEM_MINUTES_MIN;
        }
    }
    assert(top >= 0);
    assert(bottom <= MINUTES_PER_DAY);
    assert(bottom - top >= TIMELINE_ITEM_MINUTES_MIN);
    return (Timeline_Item){ .event_index = event_index, .minute_top = top,
                            .minute_bottom = bottom, .lane = 0, .lane_count = 1 };
}

// Insertion sort, because a day holds few items and they mostly come in order already, by
// start; a stable sort also keeps App.events' order for items that start together.
static void timeline_items_sort(Timeline_Item *items, int32_t count) {
    assert(count >= 0);
    assert(count <= EVENT_COUNT_MAX);
    for (int32_t i = 1; i < count; i++) {
        const Timeline_Item item = items[i];
        int32_t j = i;
        // Bounded by i, which j counts down from.
        while (j > 0 && timeline_item_is_before(&item, &items[j - 1])) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = item;
    }
}

// Earlier starts first, and of two that start together, the longer first, so that it takes
// the leftmost lane and the shorter one sits beside it.
static bool timeline_item_is_before(const Timeline_Item *a, const Timeline_Item *b) {
    if (a->minute_top != b->minute_top) {
        return a->minute_top < b->minute_top;
    }
    return a->minute_bottom > b->minute_bottom;
}

// Gives each item, in order of start, the leftmost lane that is free when it starts. A group of
// items that overlap, directly or through others, shares its lane count, so that the group
// divides the day's width evenly and its lanes line up.
static void timeline_items_assign_lanes(Timeline_Item *items, int32_t count) {
    assert(count >= 0);
    int32_t lane_bottoms[TIMELINE_LANE_COUNT_MAX] = { 0 };  // Minute each lane is free from
    int32_t lane_count = 0;
    int32_t group_first_index = 0;
    int32_t group_bottom = 0;  // Minute the group's last item ends
    for (int32_t i = 0; i < count; i++) {
        Timeline_Item *item = &items[i];
        assert(i == 0 || !timeline_item_is_before(item, &items[i - 1]));
        if (item->minute_top >= group_bottom) {
            // Nothing before overlaps this item, so the group before it is complete.
            timeline_items_set_lane_count(items, group_first_index, i, lane_count);
            group_first_index = i;
            lane_count = 0;
        }
        const int32_t lane = timeline_lane_find(lane_bottoms, lane_count, item->minute_top);
        if (lane == lane_count) {
            lane_count++;
        }
        assert(lane < lane_count);
        assert(lane_count <= TIMELINE_LANE_COUNT_MAX);
        // Never earlier than before, since a shared lane is busy until its last item ends. A
        // free lane, even one left from an earlier group, ends before this item does.
        if (item->minute_bottom > lane_bottoms[lane]) {
            lane_bottoms[lane] = item->minute_bottom;
        }
        item->lane = lane;
        if (item->minute_bottom > group_bottom) {
            group_bottom = item->minute_bottom;
        }
    }
    timeline_items_set_lane_count(items, group_first_index, count, lane_count);
}

// The leftmost of the first lane_count lanes that is free at minute_top; else a new lane,
// lane_count, while there may be more; else the lane that frees up first.
static int32_t timeline_lane_find(const int32_t *lane_bottoms, int32_t lane_count,
                                  int32_t minute_top) {
    assert(lane_count >= 0);
    assert(lane_count <= TIMELINE_LANE_COUNT_MAX);
    for (int32_t lane = 0; lane < lane_count; lane++) {
        if (lane_bottoms[lane] <= minute_top) {
            return lane;
        }
    }
    if (lane_count < TIMELINE_LANE_COUNT_MAX) {
        return lane_count;
    }
    int32_t earliest = 0;
    for (int32_t lane = 1; lane < lane_count; lane++) {
        if (lane_bottoms[lane] < lane_bottoms[earliest]) {
            earliest = lane;
        }
    }
    return earliest;
}

// Items first_index up to, but not including, end_index form one group.
static void timeline_items_set_lane_count(Timeline_Item *items, int32_t first_index,
                                          int32_t end_index, int32_t lane_count) {
    assert(first_index <= end_index);
    // A group of items has a lane for each; only the empty group before the first has none.
    assert((lane_count > 0) == (first_index < end_index));
    for (int32_t i = first_index; i < end_index; i++) {
        items[i].lane_count = lane_count;
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
}

static void draw_grid_cell_day_number(const App *app, Rectangle cell, Date date, bool in_month) {
    char number[4];
    snprintf(number, sizeof(number), "%" PRId32, date.day);
    Vector2 position = { cell.x + 10, cell.y + 7 };
    Color color = style_color(in_month ? TEXT_COLOR_NORMAL : TEXT_COLOR_DISABLED);

    if (date_equal(date, app->today)) {
        Vector2 size = measure_text(&app->fonts.ui, number);
        Vector2 center = { position.x + size.x / 2, position.y + size.y / 2 };
        // A solid accent circle with the number in the background color, so that today is the
        // brightest spot in the grid.
        DrawCircleV(center, size.y * 0.85f, style_color(BORDER_COLOR_PRESSED));
        color = style_color(BACKGROUND_COLOR);
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
        DrawCircleV(center, radius, event_color(event, app_now(app)));
        center.x -= spacing;
        marker_count++;
    }
}

static void draw_grid_cell_events(const App *app, Rectangle cell, int32_t day_number,
                                  int32_t column) {
    const float slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    float top = cell.y + CELL_TOP_LINE_PIXELS;
    int32_t slot_count =
        (int32_t)((cell.y + cell.height - CELL_BOTTOM_MARGIN_PIXELS - top) / slot_height);
    if (slot_count <= 0) {
        return;
    }

    int32_t overlap_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        if (event_in_day_cell(event, app->range) && event_occurs_on_day(event, day_number)) {
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
            if (!event_in_day_cell(event, app->range)) {
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
    const Color color = event_color(event, app_now(app));
    bool starts_here = day_number == start_day;
    bool ends_here = day_number == start_day + event_duration_days(event);
    // Multi-day events run edge to edge between days, so that they read as one bar.
    float left = bar_slot.x + (starts_here ? 4.0f : 0.0f);
    float right = bar_slot.x + bar_slot.width - (ends_here ? 4.0f : 0.0f);
    Rectangle bar = { left, bar_slot.y, right - left, bar_slot.height };
    // A problem is one day long, so it is always labelled; the problems view draws it alike.
    if (event->kind == EVENT_KIND_PROBLEM) {
        app_draw_problem_bar(app, event->title, event->tasks_done, bar);
        return;
    }
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
    // The time only where the occurrence starts; later days' bars continue it.
    event_format_label(event, starts_here, app_now(app), label, sizeof(label));
    draw_event_label(app, event, label, bar, done);
}

// An item's label on a bar-high line at the top of area, clipped to area: after a checkbox for
// a task, or a tick for a used opportunity, and struck through for a done task. Shared by the
// day cells' bars and the timeline's blocks, so that an item reads the same in both.
static void draw_event_label(const App *app, const Event *event, const char *label,
                             Rectangle area, bool done) {
    assert(area.height > 0.0f);
    const bool task = event->kind == EVENT_KIND_TASK;
    assert(task || !done);
    const Text_Style *style = &app->fonts.small;
    const float line_height = (float)EVENT_BAR_HEIGHT_PIXELS;
    float text_x = area.x + 4;
    Color text_color = WHITE;
    scissor_begin(&app->fonts, area.x, area.y, area.width, area.height);
    if (task) {
        Rectangle box = { area.x + 4, area.y + 4, 11, 11 };
        draw_check_mark(box, done, event_color(event, app_now(app)));
        text_x = box.x + box.width + 4;
        // The theme's text colors rather than the task's own, which is too dark to read on the
        // dark cells; the outline and checkbox already carry the task's color.
        text_color = style_color(done ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
    } else if (event->tasks_done) {
        // As on a done problem's bar (see app_draw_problem_bar).
        Rectangle tick = { area.x + 4, area.y + 4, 11, 11 };
        draw_tick(tick, 2.0f, WHITE);
        text_x = tick.x + tick.width + 4;
    }
    // Centered vertically in the line.
    draw_text(style, label, text_x, area.y + (line_height - style->size_pixels) / 2, text_color);
    if (done) {
        float middle_y = area.y + line_height / 2;
        float width = measure_text(style, label).x;
        DrawLineEx((Vector2){ text_x, middle_y }, (Vector2){ text_x + width, middle_y }, 1.5f,
                   text_color);
    }
    scissor_end(&app->fonts);
}

// A problem has no time; its "!" marks it even where its color is hard to tell apart, and a
// done problem has a tick instead.
void app_draw_problem_bar(const App *app, const char *title, bool done, Rectangle bar) {
    assert(bar.width > 0.0f);
    assert(bar.height > 0.0f);
    const Text_Style *style = &app->fonts.small;
    DrawRectangleRec(bar, done ? target_done_color : problem_color);
    char label[TEXT_LINE_SIZE];
    snprintf(label, sizeof(label), "%s%s", done ? "" : "! ", title);
    float text_x = bar.x + 4;
    scissor_begin(&app->fonts, bar.x, bar.y, bar.width, bar.height);
    if (done) {
        Rectangle tick = { bar.x + 4, bar.y + 4, 11, 11 };
        draw_tick(tick, 2.0f, WHITE);
        text_x = tick.x + tick.width + 4;
    }
    // Centered vertically in the bar.
    draw_text(style, label, text_x, bar.y + (bar.height - style->size_pixels) / 2, WHITE);
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

// A check mark filling area, drawn rather than typed, because Rubik as loaded has no glyph
// for it (see FONT_CODEPOINT_COUNT).
static void draw_tick(Rectangle area, float thickness, Color color) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);
    Vector2 start = { area.x, area.y + area.height * 0.55f };
    Vector2 bottom = { area.x + area.width * 0.38f, area.y + area.height };
    Vector2 end = { area.x + area.width, area.y };
    DrawLineEx(start, bottom, thickness, color);
    DrawLineEx(bottom, end, thickness, color);
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

// Two rows of two buttons, because "New opportunity" does not fit in a quarter of the panel.
static void draw_panel_events(App *app, Rectangle body) {
    const float gap = 8;
    const float button_width = (body.width - gap) / 2;
    const float right_x = body.x + button_width + gap;
    const float second_y = body.y + BUTTON_HEIGHT_PIXELS + gap;
    Rectangle event_button = { body.x, body.y, button_width, BUTTON_HEIGHT_PIXELS };
    Rectangle task_button = { right_x, body.y, button_width, BUTTON_HEIGHT_PIXELS };
    Rectangle problem_button = { body.x, second_y, button_width, BUTTON_HEIGHT_PIXELS };
    Rectangle opportunity_button = { right_x, second_y, button_width, BUTTON_HEIGHT_PIXELS };
    if (GuiButton(event_button, "New event")) {
        app_form_open(app, app->selected, EVENT_KIND_EVENT);
    }
    if (GuiButton(task_button, "New task")) {
        app_form_open(app, app->selected, EVENT_KIND_TASK);
    }
    // A problem is recorded today, whichever day is selected.
    if (GuiButton(problem_button, "New problem")) {
        app_form_open(app, app->today, EVENT_KIND_PROBLEM);
    }
    if (GuiButton(opportunity_button, "New opportunity")) {
        app_form_open(app, app->selected, EVENT_KIND_OPPORTUNITY);
    }

    // The list ends above the legend; it scrolls when its cards do not fit.
    float list_top = second_y + BUTTON_HEIGHT_PIXELS + PADDING_PIXELS;
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
        scroll_update(&app->panel_scroll, list, content_height);
        draw_panel_events_list(app, list);
        draw_scrollbar(list, content_height, app->panel_scroll.offset_pixels);
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
        height += event_card_height(&app->fonts, &app->events[i],
                                    app->events[i].id == app->event_delete_id, card_width);
    }
    assert(height >= 0.0f);
    return height;
}

// Scrolls the list by the wheel, or by dragging it with a finger or the mouse, and keeps the
// offset within the content. Callers take a release as a click only while the press began in
// the list and did not become a drag.
static void scroll_update(Scroll *scroll, Rectangle list, float content_height) {
    assert(list.height > 0.0f);
    assert(content_height > 0.0f);
    const Vector2 mouse = GetMousePosition();
    const bool over = CheckCollisionPointRec(mouse, list);
    // A press is forgotten one frame after its release, so that on the release frame the rows
    // still know whether it was a drag, and do not take it as a click.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        scroll->pressed = false;
        scroll->dragging = false;
    }
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        scroll->pressed = true;
        scroll->dragging = false;
        scroll->drag_y = mouse.y;
        scroll->drag_offset_pixels = scroll->offset_pixels;
    }
    if (scroll->pressed && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float moved = mouse.y - scroll->drag_y;
        if (moved > LIST_DRAG_THRESHOLD_PIXELS || moved < -LIST_DRAG_THRESHOLD_PIXELS) {
            scroll->dragging = true;
        }
        if (scroll->dragging) {
            scroll->offset_pixels = scroll->drag_offset_pixels - moved;
        }
    }
    if (over) {
        scroll->offset_pixels -= GetMouseWheelMove() * LIST_WHEEL_STEP_PIXELS;
    }

    // Also when the content changed or the window was resized since the last frame.
    float offset_max = content_height > list.height ? content_height - list.height : 0.0f;
    if (scroll->offset_pixels > offset_max) {
        scroll->offset_pixels = offset_max;
    }
    if (scroll->offset_pixels < 0.0f) {
        scroll->offset_pixels = 0.0f;
    }
    assert(scroll->offset_pixels >= 0.0f);
    assert(scroll->offset_pixels <= offset_max);
}

// Draws the selected day's cards, moved up by the scroll and clipped to the list. Controls are
// locked while the list is dragged, and while the pointer is outside the list, so that neither
// the end of a drag nor a click on a clipped part of a card acts on it.
static void draw_panel_events_list(App *app, Rectangle list) {
    assert(list.height > 0.0f);
    const Scroll *scroll = &app->panel_scroll;
    assert(scroll->offset_pixels >= 0.0f);
    const bool locked = scroll->dragging || !CheckCollisionPointRec(GetMousePosition(), list);
    // Locked already when the app has no input; that lock stays until app_frame lifts it.
    const bool locked_before = GuiIsLocked();
    if (locked) {
        GuiLock();
    }
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);

    int32_t selected_day = date_to_day_number(app->selected);
    float y = list.y - scroll->offset_pixels;
    for (int32_t i = 0; i < app->event_count; i++) {
        const Event *event = &app->events[i];
        int32_t start_day = 0;
        if (!event_occurrence_on_day(event, selected_day, &start_day)) {
            continue;
        }
        float card_height = event_card_height(&app->fonts, event,
                                              event->id == app->event_delete_id, list.width);
        Rectangle card = { list.x, y, list.width, card_height };
        y += card_height + EVENT_CARD_GAP_PIXELS;
        // Cards scrolled wholly out of the list can be neither seen nor clicked.
        if (!CheckCollisionRecs(card, list)) {
            continue;
        }
        // The card shows this day's occurrence, so its dates are for this day.
        Event occurrence = event_occurrence_at(event, start_day);
        bool clicked = draw_panel_event_card(app, &occurrence, card);
        // Only a press that began in the list opens a card, not one dragged in from elsewhere.
        if (clicked && !locked && scroll->pressed) {
            // Edit the event itself, not this occurrence, so that the form shows its first dates.
            app->event_focused_id = event->id;
            app_form_open_edit(app, event);
        }
    }

    app->fonts.clipping = false;
    EndScissorMode();
    if (locked && !locked_before) {
        GuiUnlock();
    }
}

// A thin bar right of a list: its length is the visible share of the content, and its place is
// where the visible part lies within it.
static void draw_scrollbar(Rectangle list, float content_height, float offset_pixels) {
    assert(content_height > 0.0f);
    assert(offset_pixels >= 0.0f);
    if (content_height <= list.height) {
        return;
    }
    float thumb_height = list.height * list.height / content_height;
    float thumb_y = list.y + offset_pixels * list.height / content_height;
    float x = list.x + list.width + SCROLLBAR_GAP_PIXELS - SCROLLBAR_WIDTH_PIXELS;
    DrawRectangleRec((Rectangle){ x, list.y, SCROLLBAR_WIDTH_PIXELS, list.height },
                     style_color(BASE_COLOR_NORMAL));
    // The focused border color, because the normal one is too close to the track to see.
    DrawRectangleRec((Rectangle){ x, thumb_y, SCROLLBAR_WIDTH_PIXELS, thumb_height },
                     style_color(BORDER_COLOR_FOCUSED));
}

// Returns true when the card itself was clicked, not one of its buttons. Deleting asks first, on
// the card, because it is permanent and also removes every repeat of a repeating event.
// A task's checkbox marks the occurrence on this card, which starts on event->start_date.
static bool draw_panel_event_card(App *app, const Event *event, Rectangle card) {
    const float x = card.x + 16;
    const Rectangle delete_button = { card.x + card.width - 82, card.y + 6, 72, 24 };
    const Rectangle checkbox = { x, card.y + 8, 16, 16 };
    const Text_Style *title_style = &app->fonts.small_strong;
    const Rectangle confirm_button = { x, card.y + card.height - 40, 110, 32 };
    const Rectangle cancel_button = { x + 122, confirm_button.y, 110, 32 };
    const bool task = event->kind == EVENT_KIND_TASK;
    const int32_t start_day = date_to_day_number(event->start_date);
    const bool done = task && app_task_done(app, event->id, start_day);
    const bool confirming = event->id == app->event_delete_id;
    const bool focused = event->id == app->event_focused_id;
    const Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, card);

    DrawRectangleRec(card, style_color(hovered ? BASE_COLOR_FOCUSED : BASE_COLOR_NORMAL));
    DrawRectangleRec((Rectangle){ card.x, card.y, 6, card.height },
                     event_color(event, app_now(app)));
    DrawRectangleLinesEx(card, focused || confirming ? 2.0f : 1.0f,
                         style_color(focused ? BORDER_COLOR_PRESSED : BORDER_COLOR_NORMAL));

    // Clip the title short of the delete button. A done task's title is dimmed and struck out,
    // and a missed opportunity's dimmed; a done target's has a tick before it, in the green of
    // its stripe.
    const bool dimmed = done || event_is_missed(event, app_now(app));
    const float title_x = task || event->tasks_done ? x + 24 : x;
    const Color title_color = style_color(dimmed ? TEXT_COLOR_DISABLED : TEXT_COLOR_NORMAL);
    scissor_begin(&app->fonts, card.x, card.y, card.width - 90, card.height);
    if (event->tasks_done) {
        draw_tick((Rectangle){ x + 1, card.y + 9, 14, 13 }, 2.5f, target_done_color);
    }
    draw_text(title_style, event->title, title_x, card.y + 8, title_color);
    if (done) {
        float width = measure_text(title_style, event->title).x;
        float middle_y = card.y + 8 + title_style->size_pixels / 2 + 1;
        DrawLineEx((Vector2){ title_x, middle_y }, (Vector2){ title_x + width, middle_y }, 1.5f,
                   title_color);
    }
    scissor_end(&app->fonts);

    if (confirming) {
        const char *question =
            event->repeat_unit != REPEAT_NONE       ? "Delete it and all repeats?"
            : task                                  ? "Delete this task?"
            : event->kind == EVENT_KIND_PROBLEM     ? "Delete this problem?"
            : event->kind == EVENT_KIND_OPPORTUNITY ? "Delete this opportunity?"
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

    draw_panel_event_card_details(&app->fonts, event, app_now(app), card);
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

// The lines below the title: dates and repeat, location, description, repeat end, and creation,
// with how long ago that was. A problem shows only its kind, description and creation.
static void draw_panel_event_card_details(const Fonts *fonts, const Event *event, Date_Time now,
                                          Rectangle card) {
    const float x = card.x + 16;
    const float line = (float)TEXT_LINE_HEIGHT_PIXELS_SMALL;
    const Text_Style *style = &fonts->small;
    char text[TEXT_LINE_SIZE];
    scissor_begin(fonts, card.x, card.y, card.width - 4, card.height);
    float y = card.y + 8 + line;
    event_format_range(event, now, text, sizeof(text));
    draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    if (event->location.id != 0) {
        y += line;
        event_format_location(event, text, sizeof(text));
        draw_text(style, text, x, y, style_color(TEXT_COLOR_NORMAL));
    }
    if (event->description[0] != '\0') {
        Text_Lines lines = text_wrap(style, event->description,
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
    event_format_created(event, now, text, sizeof(text));
    draw_text(style, text, x, y, style_color(TEXT_COLOR_DISABLED));
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
    static const char *const headings[EVENT_KIND_COUNT] = {
        "Edit event", "Edit task", "Edit problem", "Edit opportunity",
    };
    const bool editing = form->editing_id != 0;
    // A new entry has no heading: the kind selector in its place says what it is.
    if (editing) {
        draw_text(&app->fonts.ui, headings[form->kind], body.x, body.y,
                  style_color(TEXT_COLOR_NORMAL));
    }
    // An existing link target keeps its kind: a problem must stay on the day it was created,
    // and the tasks linked to an opportunity would mean nothing to an event.
    if (!editing || !event_kind_is_target(form->kind)) {
        draw_panel_form_kinds(app, body, editing);
    }

    // The first field's label sits below the heading and its box below the label.
    const float fields_y = body.y + 32 + FORM_LABEL_HEIGHT_PIXELS;
    float y = draw_panel_form_fields(app, body, fields_y);
    // An error takes the hint's line, because both are about the fields and the error is what
    // needs reading then; sharing the line leaves a task's checklist more room.
    if (form->error[0] != '\0') {
        scissor_begin(&app->fonts, body.x, y, body.width, TEXT_LINE_HEIGHT_PIXELS);
        draw_text(&app->fonts.ui, form->error, body.x, y, error_color);
        scissor_end(&app->fonts);
    } else if (form->kind != EVENT_KIND_PROBLEM) {
        draw_text(&app->fonts.small, "Dates YYYY-MM-DD, times HH:MM (optional)", body.x, y,
                  style_color(TEXT_COLOR_DISABLED));
    }
    y += TEXT_LINE_HEIGHT_PIXELS + 4;

    // The buttons sit at the bottom of the panel, so that they stay in place whatever the kind,
    // and a task's checklist of targets, or an opportunity's tasks, fill the room between the
    // fields and them.
    const float buttons_y = body.y + body.height - BUTTON_HEIGHT_PIXELS;
    if (form->kind == EVENT_KIND_TASK) {
        draw_panel_form_targets(app, body, y + FORM_LABEL_HEIGHT_PIXELS,
                                buttons_y - FORM_ROW_GAP_PIXELS);
    }
    if (form->kind == EVENT_KIND_OPPORTUNITY) {
        draw_panel_form_target_tasks(app, body, y + FORM_LABEL_HEIGHT_PIXELS,
                                     buttons_y - FORM_ROW_GAP_PIXELS);
    }
    draw_panel_form_buttons(app, body, buttons_y);
}

// The kind selector: a new entry can be any kind, from the left where the heading would be; an
// existing event or task can switch between the two, right of its heading. Each toggle is as
// wide as its label, because "Opportunity" needs more room than an even share leaves it.
static void draw_panel_form_kinds(App *app, Rectangle body, bool editing) {
    static const char *const names[EVENT_KIND_COUNT] = { "Event", "Task", "Problem",
                                                         "Opportunity" };
    static_assert(EVENT_KIND_EVENT == 0, "Event and task are the first kinds.");
    static_assert(EVENT_KIND_TASK == 1, "Event and task are the first kinds.");
    Form *form = &app->form;
    assert(!editing || !event_kind_is_target(form->kind));
    const int32_t count = editing ? 2 : EVENT_KIND_COUNT;
    float widths[EVENT_KIND_COUNT] = { 0 };
    float total_width = (float)((count - 1) * KIND_TOGGLE_GAP_PIXELS);
    for (int32_t kind = 0; kind < count; kind++) {
        widths[kind] = measure_text(&app->fonts.ui, names[kind]).x + KIND_TOGGLE_PADDING_PIXELS;
        total_width += widths[kind];
    }
    float x = editing ? body.x + body.width - total_width : body.x;
    for (int32_t kind = 0; kind < count; kind++) {
        bool active = form->kind == kind;
        GuiToggle((Rectangle){ x, body.y - 4, widths[kind], 28 }, names[kind], &active);
        // A click on the active toggle turns it off in raygui; one kind is always chosen, so
        // only turning another one on counts.
        if (active && form->kind != kind) {
            form->kind = kind;
        }
        x += widths[kind] + KIND_TOGGLE_GAP_PIXELS;
    }
}

static void draw_panel_form_buttons(App *app, Rectangle body, float y) {
    const float gap = 12;
    const float button_width = (body.width - gap) / 2;
    if (GuiButton((Rectangle){ body.x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Cancel")) {
        form_close(&app->form);
        return;
    }
    const float save_x = body.x + button_width + gap;
    if (GuiButton((Rectangle){ save_x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Save")) {
        app_form_submit(app);
    }
}

// Where the rows of fields end, below the last box, with the first box at y. Rows are kept for
// hidden fields, so that what follows them does not move as the repeat choice changes.
static float draw_panel_form_rows_end(float y) {
    const float row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    return y + (FORM_ROW_COUNT - 1) * row_height + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
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
    const float end_y = draw_panel_form_rows_end(y);
    if (form->kind == EVENT_KIND_PROBLEM) {
        draw_panel_form_fields_problem(app, body, y, end_y);
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
    // An opportunity's end is the last moment it can be used.
    const bool opportunity = form->kind == EVENT_KIND_OPPORTUNITY;
    draw_panel_form_field(fonts, form, FORM_FIELD_END_DATE, opportunity ? "Deadline" : "End date",
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
    // An opportunity cannot repeat, so its rows end here, and its tasks get the repeat rows'
    // room below.
    if (opportunity) {
        return y - FORM_LABEL_HEIGHT_PIXELS;
    }
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

// A problem has a name and a description; its day is shown, not chosen. Below them are its
// status and the tasks that solve it.
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y, float end_y) {
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
    draw_panel_form_target_tasks(app, body, y + row_height, end_y);
}

// The status on the label line above y, then one line per linked task, with whether it is
// done, down to end_y. Tasks that do not fit are counted on the last line.
static void draw_panel_form_target_tasks(const App *app, Rectangle body, float y, float end_y) {
    const Form *form = &app->form;
    const Fonts *fonts = &app->fonts;
    assert(form->task_count <= LINK_COUNT_MAX);
    assert(y < end_y);
    const Color normal = style_color(TEXT_COLOR_NORMAL);
    const Color dim = style_color(TEXT_COLOR_DISABLED);
    if (!form->links_loaded) {
        draw_text(&fonts->ui, "Status unknown", body.x, y - FORM_LABEL_HEIGHT_PIXELS, normal);
        return;
    }
    int32_t done_count = 0;
    for (int32_t i = 0; i < form->task_count; i++) {
        done_count += form->tasks[i].done ? 1 : 0;
    }
    // With more tasks than were loaded, the counts are of the loaded ones; that many tasks for
    // one target is far beyond any real use (see LINK_COUNT_MAX).
    Target_Status status = target_status(form->task_count, done_count);
    // An opportunity's deadline as the form has it now, so that changing it shows at once.
    const bool missed = form->kind == EVENT_KIND_OPPORTUNITY &&
                        form_deadline_is_missed(form, app_now(app),
                                                status == TARGET_STATUS_DONE);
    const char *name = missed ? "Missed" : target_status_name(status);
    char text[TEXT_LINE_SIZE];
    snprintf(text, sizeof(text), "Status: %s", name);
    if (status == TARGET_STATUS_PLANNED || status == TARGET_STATUS_PARTLY_DONE) {
        snprintf(text, sizeof(text), "Status: %s, %" PRId32 " of %" PRId32 " tasks done", name,
                 done_count, form->task_count);
    }
    draw_text(&fonts->ui, text, body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              missed ? error_color : normal);
    if (form->task_count == 0) {
        draw_text(&fonts->small, "Link tasks to it in a task's form.", body.x, y + 4, dim);
        return;
    }

    const float line = TEXT_LINE_HEIGHT_PIXELS_SMALL;
    const int32_t line_count = (int32_t)((end_y - y) / line);
    // A phone too short for a task and the count of the rest shows only the status.
    if (line_count < 2) {
        return;
    }
    const bool all_fit = form->task_count <= line_count && !form->links_truncated;
    const int32_t shown = all_fit ? form->task_count : line_count - 1;
    for (int32_t i = 0; i < shown; i++) {
        const Linked_Task *task = &form->tasks[i];
        const float line_y = y + 4 + (float)i * line;
        // In the task's own color, as on the calendar, so that it is easy to find there.
        Color color = event_color(&(Event){ .id = task->task_id, .kind = EVENT_KIND_TASK },
                                  app_now(app));
        draw_check_mark((Rectangle){ body.x, line_y + 3, 12, 12 }, task->done, color);
        scissor_begin(fonts, body.x + 20, line_y, body.width - 20, line);
        draw_text(&fonts->small, task->title, body.x + 20, line_y, task->done ? dim : normal);
        scissor_end(fonts);
    }
    if (!all_fit) {
        snprintf(text, sizeof(text), "+%" PRId32 " more%s", form->task_count - shown,
                 form->links_truncated ? ", and others not loaded" : "");
        draw_text(&fonts->small, text, body.x + 20, y + 4 + (float)shown * line, dim);
    }
}

// The problems a task can solve and the opportunities it can help use, each with a check box,
// below the task's fields: the heading on the label line above y, the rows from y down to
// end_y. Checking one links the task to it when the form is saved. Each target's status is as
// stored, without the choices not saved yet. Rows scroll when they do not fit, which at the
// smallest window is after about one.
static void draw_panel_form_targets(App *app, Rectangle body, float y, float end_y) {
    Form *form = &app->form;
    char heading[TEXT_LINE_SIZE];
    snprintf(heading, sizeof(heading), "Problems and opportunities (%" PRId32 ")%s",
             form_targets_linked_count(form), form->links_truncated ? ", newest only" : "");
    scissor_begin(&app->fonts, body.x, y - FORM_LABEL_HEIGHT_PIXELS, body.width,
                  FORM_LABEL_HEIGHT_PIXELS);
    draw_text(&app->fonts.ui, heading, body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    scissor_end(&app->fonts);
    if (!form->links_loaded || form->target_count == 0) {
        const char *text = form->links_loaded ? "No problems or opportunities yet."
                                              : "Problems and opportunities could not be loaded.";
        draw_text(&app->fonts.small, text, body.x, y + 4, style_color(TEXT_COLOR_DISABLED));
        return;
    }
    // A window too short for any row shows only the heading; it is never that short at the
    // minimum window size, but a phone's height is not ours to choose.
    if (end_y - y < 1.0f) {
        return;
    }

    Rectangle list = { body.x, y, body.width, end_y - y };
    const float content_height = (float)form->target_count * TARGET_ROW_HEIGHT_PIXELS;
    if (content_height > list.height) {
        list.width -= SCROLLBAR_GAP_PIXELS;
    }
    scroll_update(&form->targets_scroll, list, content_height);
    const Scroll *scroll = &form->targets_scroll;
    // A release opens nothing unless the press began here and stayed a tap.
    const bool clickable = scroll->pressed && !scroll->dragging &&
                           CheckCollisionPointRec(GetMousePosition(), list);
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);
    for (int32_t i = 0; i < form->target_count; i++) {
        Rectangle row = { list.x,
                          list.y - scroll->offset_pixels + (float)i * TARGET_ROW_HEIGHT_PIXELS,
                          list.width, TARGET_ROW_HEIGHT_PIXELS };
        // Rows scrolled wholly out of the list can be neither seen nor clicked.
        if (!CheckCollisionRecs(row, list)) {
            continue;
        }
        if (draw_panel_form_targets_row(app, &form->targets[i], row, clickable)) {
            form->targets[i].linked = !form->targets[i].linked;
        }
    }
    app->fonts.clipping = false;
    EndScissorMode();
    draw_scrollbar(list, content_height, scroll->offset_pixels);
}

// Returns true when the row was tapped or clicked, which toggles its check box. The box has the
// target's color, so that problems and opportunities tell apart as on the calendar.
static bool draw_panel_form_targets_row(const App *app, const Link_Target *target,
                                        Rectangle row, bool clickable) {
    assert(event_kind_is_target(target->kind));
    const Fonts *fonts = &app->fonts;
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
    if (hovered) {
        DrawRectangleRec(row, style_color(BASE_COLOR_FOCUSED));
    }
    DrawRectangleRec((Rectangle){ row.x, row.y + row.height - 1, row.width, 1 },
                     style_color(BORDER_COLOR_NORMAL));
    const Target_Status status = target_status(target->task_count, target->task_done_count);
    const bool done = status == TARGET_STATUS_DONE;
    const bool missed = target->kind == EVENT_KIND_OPPORTUNITY &&
                        opportunity_is_missed(target->end_date, target->end_minute, done,
                                              app_now(app));
    const Color color = target->kind == EVENT_KIND_PROBLEM ? problem_color
                        : missed                           ? opportunity_missed_color
                                                           : opportunity_color;
    const float box_size = 18;
    draw_check_mark((Rectangle){ row.x + 6, row.y + (row.height - box_size) / 2, box_size,
                                 box_size },
                    target->linked, color);

    const float title_x = row.x + 34;
    const float status_x = row.x + row.width - TARGET_STATUS_WIDTH_PIXELS;
    const float text_y = row.y + (row.height - TEXT_LINE_HEIGHT_PIXELS) / 2;
    scissor_begin(fonts, title_x, row.y, status_x - title_x - 8, row.height);
    draw_text(&fonts->ui, target->title, title_x, text_y, style_color(TEXT_COLOR_NORMAL));
    scissor_end(fonts);
    draw_text(&fonts->small, missed ? "Missed" : target_status_name(status), status_x, text_y + 3,
              style_color(TEXT_COLOR_DISABLED));
    return clickable && hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static int32_t form_targets_linked_count(const Form *form) {
    assert(form->target_count >= 0);
    assert(form->target_count <= LINK_COUNT_MAX);
    int32_t count = 0;
    for (int32_t i = 0; i < form->target_count; i++) {
        count += form->targets[i].linked ? 1 : 0;
    }
    return count;
}

// Whether the deadline typed in the form has passed. A deadline not typed correctly yet is not
// missed, because the form says what to fix when it is saved.
static bool form_deadline_is_missed(const Form *form, Date_Time now, bool tasks_done) {
    assert(form->kind == EVENT_KIND_OPPORTUNITY);
    Date end_date = { 0, 0, 0 };
    if (!date_parse_iso(form->text[FORM_FIELD_END_DATE], &end_date)) {
        return false;
    }
    int32_t end_minute = TIME_NONE;
    if (!form_parse_time(form->text[FORM_FIELD_END_TIME], &end_minute)) {
        return false;
    }
    return opportunity_is_missed(end_date, end_minute, tasks_done, now);
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
        return form_repeat_option(form) == REPEAT_OPTION_DAYS;
    }
    if (field == FORM_FIELD_REPEAT_END_DATE) {
        return form_repeat_option(form) != REPEAT_OPTION_NONE;
    }
    return true;
}

// The repeat chosen, or none for an opportunity, which cannot repeat; the choice stays in the
// form, hidden, in case the kind is switched back.
static int32_t form_repeat_option(const Form *form) {
    assert(form->repeat_option >= 0);
    assert(form->repeat_option < REPEAT_OPTION_COUNT);
    if (form->kind == EVENT_KIND_OPPORTUNITY) {
        return REPEAT_OPTION_NONE;
    }
    return form->repeat_option;
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
    // Named as the form labels it.
    const bool opportunity = form->kind == EVENT_KIND_OPPORTUNITY;
    if (!date_parse_iso(form->text[FORM_FIELD_END_DATE], &event->end_date)) {
        return opportunity ? "Deadline must be YYYY-MM-DD." : "End date must be YYYY-MM-DD.";
    }
    if (!form_parse_time(form->text[FORM_FIELD_END_TIME], &event->end_minute)) {
        return opportunity ? "Deadline time must be HH:MM." : "End time must be HH:MM.";
    }
    // The location id is found or created when saving, from these texts.
    text_copy_trimmed(event->location.address, sizeof(event->location.address),
                      form->text[FORM_FIELD_LOCATION_ADDRESS]);
    text_copy_trimmed(event->location.city, sizeof(event->location.city),
                      form->text[FORM_FIELD_LOCATION_CITY]);
    text_copy_trimmed(event->description, sizeof(event->description),
                      form->text[FORM_FIELD_DESCRIPTION]);
    assert(form->kind >= 0);
    assert(form->kind < EVENT_KIND_COUNT);
    const int32_t repeat_option = form_repeat_option(form);
    event->kind = form->kind;
    event->repeat_unit = repeat_option_units[repeat_option];
    event->repeat_interval = 1;
    if (repeat_option == REPEAT_OPTION_DAYS) {
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
    // Named as the form labels it.
    const bool opportunity = event->kind == EVENT_KIND_OPPORTUNITY;
    if (end_day < start_day) {
        return opportunity ? "Deadline is before start." : "End date is before start.";
    }
    if (end_day == start_day) {
        if (event->start_minute != TIME_NONE && event->end_minute != TIME_NONE) {
            if (event->end_minute < event->start_minute) {
                return opportunity ? "Deadline is before start." : "End time is before start.";
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

// An item that goes on the week and day views' timeline: one day long, with a start or an end
// time. A multi-day item stays a bar across the day cells, where it reads as one span.
static bool event_is_timed(const Event *event) {
    // A problem has no times, so it is always in the day cells.
    assert(event->kind != EVENT_KIND_PROBLEM || event->start_minute == TIME_NONE);
    if (event_duration_days(event) > 0) {
        return false;
    }
    return event->start_minute != TIME_NONE || event->end_minute != TIME_NONE;
}

// A month shows every item in its cells; a week or day only those that are not on the timeline.
static bool event_in_day_cell(const Event *event, int32_t range) {
    assert(range >= 0);
    assert(range < RANGE_COUNT);
    return range == RANGE_MONTH || !event_is_timed(event);
}

// One more line for a location, one for a repeating event that ends, to say when, and one per
// line of description.
static float event_card_height(const Fonts *fonts, const Event *event, bool confirming,
                               float card_width) {
    int32_t extra_lines = (event->location.id != 0 ? 1 : 0) + (event->repeat_has_end ? 1 : 0);
    if (event->description[0] != '\0') {
        float width = card_width - (float)CARD_TEXT_MARGIN_PIXELS;
        extra_lines += text_wrap(&fonts->small, event->description, width).count;
    }
    float height = (float)(EVENT_CARD_HEIGHT_PIXELS + extra_lines * TEXT_LINE_HEIGHT_PIXELS_SMALL);
    // A confirming card hides its details, but needs room for the question and the buttons.
    if (confirming && height < (float)EVENT_CARD_CONFIRM_HEIGHT_PIXELS) {
        height = (float)EVENT_CARD_CONFIRM_HEIGHT_PIXELS;
    }
    return height;
}

// Breaks text at spaces into at most DESCRIPTION_LINES_MAX lines that fit width_pixels, and
// breaks inside a word only when one word alone is wider than a line.
static Text_Lines text_wrap(const Text_Style *style, const char *text, float width_pixels) {
    Text_Lines lines = { .count = 0, .cut = false };
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

static bool text_line_fits(const Text_Style *style, const char *text, int32_t length,
                           float width_pixels) {
    char line[EVENT_DESCRIPTION_SIZE];
    assert(length >= 0);
    assert(length < EVENT_DESCRIPTION_SIZE);
    memcpy(line, text, (size_t)length);
    line[length] = '\0';
    return measure_text(style, line).x <= width_pixels;
}

static bool event_is_missed(const Event *event, Date_Time now) {
    if (event->kind != EVENT_KIND_OPPORTUNITY) {
        return false;
    }
    return opportunity_is_missed(event->end_date, event->end_minute, event->tasks_done, now);
}

// now matters only for an opportunity, which turns gray once missed.
static Color event_color(const Event *event, Date_Time now) {
    assert(event->id > 0);
    if (event_kind_is_target(event->kind) && event->tasks_done) {
        return target_done_color;
    }
    if (event->kind == EVENT_KIND_PROBLEM) {
        return problem_color;
    }
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        return event_is_missed(event, now) ? opportunity_missed_color : opportunity_color;
    }
    assert(!event->tasks_done);
    return event_colors[event->id % EVENT_COLOR_COUNT];
}

// What a bar or timeline block shows: the title, after the time when with_time, as where an
// occurrence starts. An item with only an end time says "Until", since its block is drawn in
// the hour before the end, not from a start; a multi-day item's end is on another day, so it
// has none. A missed opportunity says so first, because its gray alone could be any item.
static void event_format_label(const Event *event, bool with_time, Date_Time now, char *buffer,
                               size_t buffer_size) {
    assert(buffer_size > 0);
    const char *missed = event_is_missed(event, now) ? "Missed: " : "";
    char time[TIME_TEXT_SIZE];
    if (with_time && event->start_minute != TIME_NONE) {
        time_format(event->start_minute, time, sizeof(time));
        snprintf(buffer, buffer_size, "%s%s %s", missed, time, event->title);
    } else if (with_time && event->end_minute != TIME_NONE && event_duration_days(event) == 0) {
        time_format(event->end_minute, time, sizeof(time));
        snprintf(buffer, buffer_size, "%sUntil %s %s", missed, time, event->title);
    } else {
        snprintf(buffer, buffer_size, "%s%s", missed, event->title);
    }
}

// The dates and times, followed by how the event repeats, if it does.
static void event_format_range(const Event *event, Date_Time now, char *buffer,
                               size_t buffer_size) {
    // A problem has no range: it is on its one day, which the panel heading shows.
    if (event->kind == EVENT_KIND_PROBLEM) {
        snprintf(buffer, buffer_size, "%s", event->tasks_done ? "Problem, completed" : "Problem");
        return;
    }
    // An opportunity is about its deadline, and whether it was used or missed by then; the
    // calendar shows when it starts.
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        // "31 Dec 23:59", with room for any int32_t day as far as the compiler can tell.
        char deadline[32];
        format_date_time_short(event->end_date, event->end_minute, deadline, sizeof(deadline));
        const char *state = event->tasks_done          ? "Used opportunity"
                            : event_is_missed(event, now) ? "Missed opportunity"
                                                          : "Opportunity";
        snprintf(buffer, buffer_size, "%s, deadline %s", state, deadline);
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

// "Created 18 Oct 2026, 20:47 (yesterday)": the exact moment, and how long ago it was.
static void event_format_created(const Event *event, Date_Time now, char *buffer,
                                 size_t buffer_size) {
    char time[TIME_TEXT_SIZE];
    time_format(event->created_minute, time, sizeof(time));
    char age[DATE_TIME_AGE_SIZE];
    date_time_format_age((Date_Time){ event->created_date, event->created_minute }, now, age,
                         sizeof(age));
    snprintf(buffer, buffer_size, "Created %" PRId32 " %s %" PRId32 ", %s (%s)",
             event->created_date.day, month_short_name(event->created_date.month),
             event->created_date.year, time, age);
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

// Loads theme_colors into raygui. DEFAULT's base properties reach every control, so that
// buttons, text boxes and toggles match the surfaces the apps draw themselves.
static void theme_apply(void) {
    for (int32_t i = 0; i < THEME_COLOR_COUNT; i++) {
        const Theme_Color *color = &theme_colors[i];
        // A color property: raygui's base properties start with the colors, and
        // BACKGROUND_COLOR is the last of DEFAULT's extended ones.
        assert(color->property >= BORDER_COLOR_NORMAL);
        assert(color->property <= BACKGROUND_COLOR);
        // Opaque, because surfaces are drawn over one another and must hide what is below.
        assert((color->rgba & 0xFFu) == 0xFFu);
        // raygui stores colors as int; values above INT32_MAX wrap, and GetColor reads them
        // back as unsigned, as raygui's own styles do.
        GuiSetStyle(DEFAULT, color->property, (int)color->rgba);
    }
    assert(GuiGetStyle(BUTTON, BASE_COLOR_NORMAL) == GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL));
    assert(GuiGetStyle(TEXTBOX, BORDER_COLOR_PRESSED) ==
           GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED));
}

static Color style_color(int32_t property) {
    return GetColor((unsigned int)GuiGetStyle(DEFAULT, property));
}

// Positions are rounded to whole screen pixels, so that glyphs, rasterized at the screen
// scale, map one to one onto screen pixels and stay sharp. A position may be left of or above
// the screen, as in the phone's drawer while it slides in.
static void draw_text(const Text_Style *style, const char *text, float x, float y, Color color) {
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

static Vector2 measure_text(const Text_Style *style, const char *text) {
    return MeasureTextEx(style->font, text, style->size_pixels,
                         (float)GuiGetStyle(DEFAULT, TEXT_SPACING));
}

static void draw_text_centered(const Text_Style *style, const char *text, Rectangle area,
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
