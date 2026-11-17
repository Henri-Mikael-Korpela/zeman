#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raygui.h"

#include <zeman/app.h>
#include <zeman/array_slice.h>
#include <zeman/bound.h>
#include <zeman/byte.h>
#include <zeman/rich_text.h>
#include <zeman/str.h>
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
    // Left of the month view's day cells, for each week's number: "53" and the "Wk" above them
    // in the small font, with room to spare.
    WEEK_NUMBER_GUTTER_WIDTH_PIXELS = 32,
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
    // The button beside a date field that opens the date picker, and the picker's layout: a
    // header with the month, the weekday names, and six weeks of cells, which always cover a
    // month. A cell is big enough to hit with a finger.
    PICKER_BUTTON_WIDTH_PIXELS = 28,
    PICKER_BUTTON_GAP_PIXELS = 4,
    PICKER_PADDING_PIXELS = 6,
    PICKER_HEADER_HEIGHT_PIXELS = 30,
    PICKER_WEEKDAY_HEIGHT_PIXELS = 20,
    PICKER_CELL_WIDTH_PIXELS = 32,
    PICKER_CELL_HEIGHT_PIXELS = 28,
    PICKER_WIDTH_PIXELS = 7 * PICKER_CELL_WIDTH_PIXELS + 2 * PICKER_PADDING_PIXELS,
    PICKER_HEIGHT_PIXELS = PICKER_HEADER_HEIGHT_PIXELS + PICKER_WEEKDAY_HEIGHT_PIXELS +
                           6 * PICKER_CELL_HEIGHT_PIXELS + 2 * PICKER_PADDING_PIXELS,
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
    // Links found in the lines of a description that a card shows: three short lines hold far
    // fewer, and any past this would only lose their highlight.
    CARD_LINK_COUNT_MAX = 32,
    // The description editor's lines as laid out: each holds at least one byte of the text, or
    // is the one after its last line break, so there are never more than its bytes.
    DESCRIPTION_LINE_COUNT_MAX = EVENT_DESCRIPTION_SIZE,
    // Links in a description: each takes at least six bytes, "www.a" and a byte that ends it.
    DESCRIPTION_LINK_COUNT_MAX = EVENT_DESCRIPTION_SIZE / 6 + 1,
    // Room between the description editor's border and its text.
    DESCRIPTION_EDITOR_PADDING_PIXELS = 8,
    // Kept free above and below an image in a description, so that it does not touch the text.
    DESCRIPTION_IMAGE_GAP_PIXELS = 4,
    // The frame drawn for an image that could not be loaded: a line of text with room around it.
    DESCRIPTION_IMAGE_PLACEHOLDER_HEIGHT_PIXELS = 48,
    // Characters taken from typing in one frame: more than anyone types, and a bound for the loop.
    TYPED_CHARACTER_COUNT_MAX = 64,
    // An image whose PNG file is larger than allowed is shrunk to three quarters, at most this
    // many times: a 1024-pixel photo then ends at about 100 pixels, which always fits.
    IMAGE_SHRINK_STEP_COUNT_MAX = 8,
    // The Attachments button beside the description's box in the form: wide enough for its label
    // above it, "Attachments".
    ATTACHMENT_BUTTON_WIDTH_PIXELS = 112,
    // The area for adding a file, tall enough to be an easy target, and one row of the attached
    // files: the name with the size below it, and a Save and a Remove button.
    ATTACHMENT_ZONE_HEIGHT_PIXELS = 80,
    ATTACHMENT_ROW_HEIGHT_PIXELS = 48,
    ATTACHMENT_SAVE_WIDTH_PIXELS = 56,
    ATTACHMENT_REMOVE_WIDTH_PIXELS = 76,
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
    // A block drawn on the timeline snaps to quarter hours, which most meetings start on and
    // which is 12 pixels at the timeline's scale, easy to hit with a mouse.
    TIMELINE_MARK_STEP_MINUTES = 15,
    // With zoom on at the press, it snaps to five minutes instead, which is 4 pixels at the
    // timeline's scale, too fine to hit; hence the magnifier below.
    TIMELINE_MARK_ZOOM_STEP_MINUTES = 5,
    // The magnifier's scale, and how much slower than the pointer its time moves: a five-minute
    // step is then 16 pixels of pointer travel, as easy to hit as a quarter hour unzoomed.
    TIMELINE_MARK_ZOOM = 4,
    // The magnifier's circle: about half an hour either side of its middle at its scale.
    TIMELINE_MARK_ZOOM_RADIUS_PIXELS = 90,
    // How far into an item's block from its top or bottom a press drags that edge rather than
    // the whole item. The shortest block is 24 pixels, which leaves its middle 12 to move it by.
    TIMELINE_EDGE_GRAB_PIXELS = 6,
    // Half the length of the line that shows, before a press, where a block would start:
    // short, so that it marks a point in time without hiding the items under it.
    TIMELINE_MARK_HOVER_HALF_PIXELS = 28,
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
    // Printable ASCII, Latin-1 and Latin Extended-A, so that titles with letters such as
    // a-umlaut or s-caron display, and the common typographic punctuation and the euro sign.
    FONT_CODEPOINT_COUNT = (126 - 32 + 1) + (255 - 160 + 1) + (383 - 256 + 1) +
                           (8212 - 8211 + 1) + (8223 - 8216 + 1) + 1 + 1,
    // raylib draws and measures C strings, so a Str is copied into a buffer of this size for the
    // call. Longer than any text on screen, the longest being a settings line with a file path of
    // up to 1023 bytes, so that nothing is cut off in practice; if it were, the text would be
    // drawn cut at a whole character.
    TEXT_DRAW_SIZE_MAX = 1280,
};

// Downloaded from Google Fonts by CMake, copied next to the desktop executable and into the
// Android APK's assets, so that the app needs no network for them at run time.
static char const FONT_FILE_REGULAR[] = "fonts/Rubik-Regular.ttf";
static char const FONT_FILE_MEDIUM[] = "fonts/Rubik-Medium.ttf";

// Tasks are drawn as tinted, outlined bars rather than solid ones, so that they stand apart from
// events at a glance; done tasks fade further.
static float const TASK_FILL_ALPHA = 0.20f;
static float const TASK_DONE_FILL_ALPHA = 0.08f;
static float const TASK_DONE_OUTLINE_ALPHA = 0.35f;

typedef enum Form_Field {
    FORM_FIELD_TITLE,
    FORM_FIELD_START_DATE,
    FORM_FIELD_START_TIME,
    FORM_FIELD_END_DATE,
    FORM_FIELD_END_TIME,
    FORM_FIELD_LOCATION_ADDRESS,  // Location fields are optional; empty means no location
    FORM_FIELD_LOCATION_CITY,
    // Optional rich text, edited on its own page of the form rather than in a text box, and
    // kept in Form.description rather than in Form.text.
    FORM_FIELD_DESCRIPTION,
    FORM_FIELD_REPEAT_DAYS,      // Shown only for "Every X days"
    FORM_FIELD_REPEAT_END_DATE,  // Shown only for repeating events; empty repeats forever
    // Shown only for tasks that do not repeat, in the row of the repeat end, which they cannot
    // have; an empty date means no deadline.
    FORM_FIELD_DEADLINE_DATE,
    FORM_FIELD_DEADLINE_TIME,
    FORM_FIELD_COUNT,
} Form_Field;

enum {
    // No form field is being edited.
    FORM_FIELD_NONE = -1,
    // Three digits for REPEAT_INTERVAL_MAX, plus the terminator.
    REPEAT_DAYS_TEXT_SIZE = 4,
    // Bytes of the buffer of each one-line field: the longest one, the title.
    FORM_FIELD_TEXT_SIZE = EVENT_TITLE_SIZE,
};

// Buffer size of each field, which raygui also uses to cap how much can be typed. The
// description has none here: it is not a text box (see FORM_FIELD_DESCRIPTION).
static int32_t const FORM_FIELD_SIZES[FORM_FIELD_COUNT] = {
    EVENT_TITLE_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE,
    LOCATION_ADDRESS_SIZE, LOCATION_CITY_SIZE, 0, REPEAT_DAYS_TEXT_SIZE,
    DATE_ISO_SIZE, DATE_ISO_SIZE, TIME_TEXT_SIZE,
};

// Choices in the form's repeat selector, in the order shown, and the unit each one means. Every
// key must stay, even those the code never names: the combo box picks them by index, and
// REPEAT_OPTION_COUNT is how many items it offers, so the labels, units and keys must line up.
typedef enum Repeat_Option {
    REPEAT_OPTION_NONE,
    REPEAT_OPTION_WEEK,
    REPEAT_OPTION_DAYS,
    REPEAT_OPTION_MONTH,
    REPEAT_OPTION_YEAR,
    REPEAT_OPTION_COUNT,
} Repeat_Option;

// raygui separates combo box items with semicolons.
static char const REPEAT_OPTION_LABELS[] =
    "Does not repeat;Every week;Every X days;Every month;Every year";

static int32_t const REPEAT_OPTION_UNITS[REPEAT_OPTION_COUNT] = {
    REPEAT_NONE, REPEAT_WEEK, REPEAT_DAY, REPEAT_MONTH, REPEAT_YEAR,
};

// One label per option: five items, four semicolons.
static_assert(REPEAT_OPTION_COUNT == 5, "REPEAT_OPTION_LABELS has an item for each option.");

static_assert(REPEAT_INTERVAL_MAX < 1000, "REPEAT_DAYS_TEXT_SIZE holds three digits.");
static_assert(TIMELINE_ITEM_MINUTES_MIN * TIMELINE_HOUR_HEIGHT_PIXELS / 60 >=
              EVENT_BAR_HEIGHT_PIXELS, "The shortest timeline item holds a label.");
static_assert(TIMELINE_ITEM_MINUTES_DEFAULT >= TIMELINE_ITEM_MINUTES_MIN,
              "The default length is not cut short.");
static_assert(TIMELINE_SCROLL_HOUR_START < MINUTES_PER_DAY / 60, "The start hour is in a day.");
// A press held this long on a day of the month view opens the form on release, rather than
// selecting the day: longer than an unhurried click, and short enough not to feel like waiting.
static double const MONTH_MARK_HOLD_SECONDS = 0.4;

// How opaque the ghost of an unsaved item is in the month view: the bar still reads as the bar
// it will become, yet the day and its other bars show through.
static float const GHOST_ALPHA = 0.7f;

static_assert(MINUTES_PER_DAY % TIMELINE_MARK_STEP_MINUTES == 0, "Steps fill a day.");
static_assert(TIMELINE_MARK_STEP_MINUTES % TIMELINE_MARK_ZOOM_STEP_MINUTES == 0,
              "Zoomed steps land on every unzoomed one.");
static_assert(60 % TIMELINE_MARK_STEP_MINUTES == 0, "Steps land on every hour.");

// Cast because the constants come from different enums.
static_assert((int)DATE_ISO_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)TIME_TEXT_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)EVENT_TITLE_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_ADDRESS_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)LOCATION_CITY_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)REPEAT_DAYS_TEXT_SIZE <= (int)FORM_FIELD_TEXT_SIZE, "Field fits the buffer.");
static_assert((int)FORM_FIELD_TEXT_SIZE <= (int)TEXT_BOX_SIZE_MAX, "Every field takes a paste.");
static_assert((int)DESCRIPTION_IMAGE_SIZE_BYTES_MAX <= (int)ATTACHMENT_SIZE_BYTES_MAX,
              "The App's buffer for files holds an image too.");
static_assert((int)RICH_TEXT_URL_SIZE >= (int)EVENT_DESCRIPTION_SIZE + 8,
              "Any link of a description, with a scheme put before it, can be opened.");

// Mid-tones: dark enough for white text on a bar, yet bright enough to stand out from the dark
// surfaces. Picked by event id, so an event keeps its color in every view.
static Color const EVENT_COLORS[] = {
    { 59, 111, 216, 255 },   // Blue
    { 200, 70, 60, 255 },    // Red
    { 46, 138, 78, 255 },    // Green
    { 130, 80, 200, 255 },   // Purple
    { 140, 90, 60, 255 },    // Brown
    { 24, 128, 140, 255 },   // Teal
    { 192, 90, 16, 255 },    // Orange
    { 180, 60, 140, 255 },   // Magenta
};
enum { EVENT_COLOR_COUNT = sizeof(EVENT_COLORS) / sizeof(EVENT_COLORS[0]) };

// Light, because error text is drawn on the dark surfaces, not on a bar.
static Color const ERROR_COLOR = { 248, 113, 113, 255 };
// Every problem has this color, darker than any event color, so that problems read as problems
// at a glance rather than as one more event.
static Color const PROBLEM_COLOR = { 160, 36, 48, 255 };
// A solved problem or used opportunity turns green, the usual color for "resolved", dark enough
// for white text.
static Color const TARGET_DONE_COLOR = { 40, 140, 76, 255 };
// Every opportunity is gold, unlike any event color, so that a chance stands out as one. A
// missed one turns gray, so that it recedes like anything past.
static Color const OPPORTUNITY_COLOR = { 168, 124, 16, 255 };
static Color const OPPORTUNITY_MISSED_COLOR = { 92, 98, 110, 255 };
// The part of a day between its first and last timed item, dark blue so that it reads as a
// span of the day and stays quieter than the items on it; lighter when the day is hovered.
static Color const TIMELINE_BUSY_COLOR = { 17, 27, 52, 255 };
static Color const TIMELINE_BUSY_HOVERED_COLOR = { 22, 35, 66, 255 };

// One color of the theme: a raygui DEFAULT property and its value, as 0xRRGGBBAA.
typedef struct Theme_Color {
    uint32_t rgba;
    int32_t property;
} Theme_Color;

// The dark theme, shared by the desktop and Android apps. Every control, and every surface the
// apps draw themselves, reads its colors from raygui's style table, so this table is the whole
// theme. Near-black surfaces with a slight blue cast, borders just above the surfaces so that
// the grid recedes behind the entries, and one blue accent for what is selected or today.
static Theme_Color const THEME_COLORS[] = {
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
enum { THEME_COLOR_COUNT = sizeof(THEME_COLORS) / sizeof(THEME_COLORS[0]) };

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
static char const RANGE_LABELS[] = "Month;Week;Day";

typedef struct Key_Step {
    int32_t key;     // raylib KeyboardKey
    int32_t days;
} Key_Step;

// The arrow keys move the selection by a day or a week, whatever the range; Page Up and Page
// Down move by the range (see range_step).
static Key_Step const KEY_STEPS[] = {
    { KEY_LEFT, -1 },
    { KEY_RIGHT, 1 },
    { KEY_UP, -7 },
    { KEY_DOWN, 7 },
};
enum { KEY_STEP_COUNT = sizeof(KEY_STEPS) / sizeof(KEY_STEPS[0]) };

// A list that scrolls by the wheel, or by dragging it with a finger or the mouse.
typedef struct Scroll {
    float offset_pixels;       // How far the list is scrolled down
    float drag_y;              // Pointer y where the press in the list began
    float drag_offset_pixels;  // offset_pixels when that press began
    bool pressed;              // The pointer went down in the list and is held
    bool dragging;             // That press moved far enough to scroll instead of click
} Scroll;

// One image of the description being edited: its stored id and, while the form is open, its
// texture.
typedef struct Description_Image {
    Texture2D texture;  // id 0 when the image could not be loaded or decoded
    int64_t id;         // 0 for a marker without an image, which saving drops
} Description_Image;

// One line of the description editor as laid out: a line of text, or an image, which always
// has a line of its own.
typedef struct Description_Line {
    float y_pixels;       // Top, from the top of the first line
    float height_pixels;
    int32_t start;        // First byte
    // Byte after the last one on the line: a line break that ends it is not on it.
    int32_t end;
    int32_t image_index;  // The image this line shows, or -1 for a line of text
} Description_Line;

// The description being edited: the text, its images, and the caret and selection in it.
// Positions are byte indexes into text, always at the start of a character.
typedef struct Description_Editor {
    Description_Line lines[DESCRIPTION_LINE_COUNT_MAX];
    Rich_Text_Link links[DESCRIPTION_LINK_COUNT_MAX];       // In order, found as laid out
    Description_Image images[DESCRIPTION_IMAGE_COUNT_MAX];  // In the order of their markers
    Scroll scroll;
    float layout_width_pixels;  // Width the lines were laid out for
    float layout_scale;         // Screen pixels per layout pixel then, which sizes images
    // x the caret keeps while it moves up and down through shorter lines, or -1 when it has
    // moved otherwise since.
    float goal_x_pixels;
    int32_t length;       // Bytes of text, before its terminator
    int32_t cursor;       // The caret
    int32_t anchor;       // The other end of the selection: equal to cursor when there is none
    int32_t line_count;
    int32_t link_count;
    int32_t image_count;  // Equal to the markers in text
    bool open;            // Shown in place of the form's fields
    // False when the images could not be loaded: then the description is shown but cannot be
    // changed, and saving leaves its images as they are, so that nothing is lost.
    bool images_loaded;
    bool layout_stale;    // Laid out for text, images or a width that have changed since
    bool selecting;       // A press in the text is held, and drags the caret
    bool caret_reveal;    // Scroll so that the caret is in sight, on the next draw
    char text[EVENT_DESCRIPTION_SIZE];
} Description_Editor;

// The files attached to the item being edited, as the form's attachments page shows them. A file
// is stored as soon as it is added, with no item yet, and the item is given the ones listed when
// the form is saved (see app_form_save_attachments); removing one from the list takes effect
// then too.
typedef struct Attachment_List {
    Attachment items[ATTACHMENT_COUNT_MAX];
    Scroll scroll;
    int32_t count;
    bool open;    // Shown in place of the form's fields
    // False when the item's files could not be loaded: then saving leaves its files as they are,
    // so that nothing is lost, and none can be added or removed.
    bool loaded;
} Attachment_List;

// What a button on a row of the attachments page asks for.
typedef enum Attachment_Action {
    ATTACHMENT_ACTION_NONE,
    ATTACHMENT_ACTION_REMOVE,
    ATTACHMENT_ACTION_SAVE,
} Attachment_Action;

typedef struct Form {
    // Links between link targets and the tasks linked to them, loaded when the form opens: an
    // existing target's tasks, or else the targets a task can be linked to, with the user's
    // choices in their linked flags until the form is saved.
    Linked_Task tasks[LINK_COUNT_MAX];
    Link_Target targets[LINK_COUNT_MAX];
    int64_t editing_id;    // Event being edited, or 0 for a new event
    Scroll targets_scroll;  // Of the list of targets, or of a target's tasks
    int32_t field_active;  // Form_Field being edited, or FORM_FIELD_NONE
    int32_t task_count;
    int32_t target_count;
    int repeat_option;     // Repeat_Option; int because raygui's combo box writes it
    int32_t kind;          // Event_Kind
    bool open;
    // The date picker, a month of days under a date field, which sets that field. Shown while
    // picker_open; picker_anchor and picker_button are where the field and its button were
    // drawn this frame, so that the picker can sit under the field and a press on the button
    // can close it.
    Rectangle picker_anchor;
    Rectangle picker_button;
    Date picker_month;     // First day of the month shown
    int32_t picker_field;  // Form_Field the picker sets
    bool picker_open;
    bool links_loaded;       // False after a failed load, so that saving keeps the stored links
    bool links_truncated;    // More links or targets exist than were loaded
    Description_Editor description;
    Attachment_List attachments;
    char text[FORM_FIELD_COUNT][FORM_FIELD_TEXT_SIZE];
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

// A span of one day in minutes, from minute_top up to but not including minute_bottom.
typedef struct Minute_Range {
    int32_t minute_top;
    int32_t minute_bottom;
} Minute_Range;

// The part of an item's block on the timeline the pointer is on: an edge, which a drag moves
// to change that time, or the rest, which a drag moves to change the item's time or day.
typedef enum Block_Part {
    BLOCK_PART_NONE,
    BLOCK_PART_TOP,
    BLOCK_PART_BOTTOM,
    BLOCK_PART_MIDDLE,
} Block_Part;

// The item block under the pointer on the timeline, found before anything there handles input.
typedef struct Timeline_Hit {
    int64_t event_id;    // 0 when the pointer is over no block
    int32_t day_number;  // Day of the column the block is in
    int32_t part;        // Block_Part
    // Over the check box of a task's block, which marks the task done instead of starting a
    // drag. Never while zoomed, where the blocks are not drawn as they are laid out.
    bool on_task_checkbox;
} Timeline_Hit;

// The bar of a day cell under the pointer, found while the cell is drawn.
typedef struct Bar_Hit {
    int64_t event_id;        // 0 when the pointer is over no bar
    int32_t start_day;       // Start of the occurrence the bar shows
    bool on_task_checkbox;   // Over the check box of a task's bar, which marks it done
} Bar_Hit;

// An item dragged on the timeline by an edge, to change that time, or by the rest, to move it.
// Saved on release; until then only a preview shows where it goes.
typedef struct Timeline_Drag {
    int64_t event_id;    // 0 when nothing is dragged
    float press_minute;  // Minute of day under the pointer at the press, not snapped
    float press_y;       // Pointer y at the press, in layout pixels
    int32_t press_day;   // Day of the column the press was in
    int32_t part;        // Block_Part pressed
    bool moved;          // Moved far enough to drag; a press that does not is a click
} Timeline_Drag;

// A task without a time dragged from its day cell to another day's cell, or down to the hours,
// in the week or day view. Saved on release; until then only a ghost shows where it goes.
typedef struct Task_Drag {
    int64_t event_id;   // 0 when nothing is dragged
    int32_t press_day;  // Day of the cell the press was in
    bool moved;         // The pointer reached another day or the hours; else it is a click
} Task_Drag;

// A block the user draws on the timeline with the mouse to make an event or task at its times.
// It belongs to the day the press began on, whichever way the pointer goes after.
typedef struct Timeline_Mark {
    float press_minute;   // Minute of day under the pointer at the press, not yet snapped
    float press_y;        // Pointer y at the press, in layout pixels
    int32_t day_number;   // Day of the column the press began in
    bool pressed;         // The press began on the timeline and is held
    bool drawing;         // It moved far enough to draw rather than click, or it is zoomed
    bool zoomed;          // Zoom was on at the press: five-minute steps under a magnifier
} Timeline_Mark;

// Days the user marks in the month view with the mouse to make an event or task over them. It
// holds the day number rather than a cell, so that the wheel can change the month under it.
typedef struct Month_Mark {
    double press_seconds;  // GetTime at the press, which tells a held press from a click
    int32_t day_number;    // Day the press began on
    bool pressed;          // The press began on a day and is held
    bool drawing;          // It was held long enough, or reached another day; else it is a click
} Month_Mark;

typedef struct App {
    Event events[EVENT_COUNT_MAX];  // Events of the visible six weeks, ordered by start
    Task_Completion completions[COMPLETION_COUNT_MAX];  // Done task occurrences near the view
    // The bytes of a description's image as it is loaded, before it becomes a texture, and of an
    // attached file as it is read or saved out. Sized for the larger, the file.
    byte blob_bytes[ATTACHMENT_SIZE_BYTES_MAX];
    Backend *backend;
    Image (*clipboard_image)(void);  // See App_Config
    File_Access const *files;        // See App_Config
    Fonts fonts;
    Form form;
    Timeline_Hit timeline_hit;
    Timeline_Drag timeline_drag;
    Task_Drag task_drag;
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
    Timeline_Mark timeline_mark;
    Month_Mark month_mark;
    // Set during app_frame for what the pointer is over or dragging; see app_cursor.
    int32_t cursor;
    bool events_stale;              // Reload events[] even though the month is unchanged
    // The focused item was just chosen in the calendar: the panel scrolls its card into sight.
    bool panel_reveal_focused;
    bool events_truncated;
    bool touch;                     // See App_Config
    bool input_enabled;             // See app_set_input_enabled
    // Five-minute steps with a magnifier on the timeline, toggled with Z. A toggle rather than a
    // held key, because a drag needs the pointer and both hands are rarely free for a chord.
    bool timeline_fine;
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
    // The week numbers left of the month's rows, or the one number of a week or day in the
    // gutter beside its day cells, with the cells starting after them at grid.x.
    Rectangle week_numbers;
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

static void fonts_load(Fonts *fonts, char const *directory, float scale);
static Text_Style fonts_load_style(char const *directory, char const *file_name,
                                   int32_t size_pixels, float scale, int const *codepoints,
                                   bool *loaded);
static void fonts_unload(Fonts *fonts);
static void app_handle_keys(App *app, Layout const *layout);
static Date app_handle_keys_pointer(App const *app, Layout const *layout, Date target);
static void app_select(App *app, Date date);
static void app_sync(App *app);
static void app_load_events(App *app, Date month, uint64_t revision);
static Event const *app_event_find(App const *app, int64_t id);
static int64_t app_event_first_on_day(App const *app, int32_t day_number);
static void app_form_open(App *app, Date date, int32_t kind);
static void app_form_open_edit(App *app, Event const *event);
static void app_form_load_links(App *app);
static void app_form_clipboard(App *app);
static void app_form_submit(App *app);
static bool app_form_save_links(App *app, int64_t id, int32_t kind, bool inserted);
static void app_event_delete(App *app, int64_t id);
static bool app_task_done(App const *app, int64_t id, int32_t start_day);
static Date_Time app_now(App const *app);
static void app_task_set_done(App *app, Event const *event, int32_t start_day, bool done);
static Layout layout_compute(float width, float height, bool form_open, int32_t range,
                             int32_t untimed_count);
static Layout layout_compute_portrait(float width, float height, bool form_open,
                                      int32_t range, int32_t untimed_count);
static void layout_set_grid(Layout *layout, int32_t range, int32_t untimed_count);
static void layout_set_grid_timeline(Layout *layout, int32_t untimed_count);
static int32_t app_untimed_count_max(App const *app);
static int32_t range_day_count(int32_t range);
static Date range_step(Date date, int32_t range, int32_t direction);
static Date range_first_date(App const *app);
static void range_title(App const *app, char *buffer, size_t buffer_size);
static int32_t app_visible_item_count(App const *app);
static Rectangle grid_cell_rectangle(Layout const *layout, int32_t cell_index);
static Date grid_first_date(Date month);
static void draw_header(App *app, Layout const *layout);
static void draw_weekdays(Fonts const *fonts, Layout const *layout, Date first);
static void draw_grid(App *app, Layout const *layout);
static void draw_week_numbers(App const *app, Layout const *layout);
static void draw_grid_selection(App const *app, Layout const *layout, int32_t first_day);
static bool month_mark_can_start(App const *app, int64_t event_id_under_press);
static void month_mark_update(App *app, Layout const *layout, int32_t first_day);
static void month_mark_finish(App *app, int32_t pointer_day);
static int32_t month_mark_day_at(Layout const *layout, Vector2 pointer, int32_t first_day);
static void draw_month_mark(App const *app, Layout const *layout, int32_t first_day);
static void draw_month_ghost(App const *app, Layout const *layout, int32_t first_day);
static void draw_grid_ghost(App const *app, Layout const *layout, int32_t first_day,
                            Event const *event);
static void draw_grid_ghost_cell(App const *app, Event const *event, Rectangle cell,
                                 int32_t day_number, int32_t start_day, int32_t column);
static void task_drag_start(App *app, Bar_Hit hit, int32_t day_number);
static void task_drag_update(App *app, Layout const *layout, int32_t first_day);
static void task_drag_finish(App *app, Event const *task, Event const *moved, int32_t press_day);
static int32_t task_drag_pointer_day(Layout const *layout, Vector2 pointer, int32_t first_day);
static Event task_drag_result(App const *app, Layout const *layout, Event const *task,
                              int32_t press_day, int32_t first_day, Vector2 pointer);
static void draw_timeline(App *app, Layout const *layout, int32_t first_day);
static void draw_timeline_hours(App const *app, Layout const *layout, float top);
static void timeline_mark_update(App *app, Layout const *layout, float top, int32_t first_day);
static void timeline_mark_finish(App *app, float top);
static int32_t timeline_column_at(Layout const *layout, Vector2 pointer);
static float timeline_minute_at(float pointer_y, float top);
static bool timeline_mark_can_start(App const *app);
static float timeline_mark_pointer_minute(Timeline_Mark const *mark, float pointer_y, float top);
static Minute_Range timeline_mark_range(float press_minute, float pointer_minute,
                                        int32_t step_minutes);
static void draw_timeline_mark(App const *app, Layout const *layout, float top,
                               int32_t first_day);
static void draw_timeline_ghost(App const *app, Layout const *layout, float top,
                                int32_t first_day);
static void draw_timeline_now(App const *app, Layout const *layout, float top,
                              int32_t first_day);
static void draw_timeline_ghost_block(App const *app, Event const *event, Rectangle day);
static bool form_ghost_event(Form const *form, Event *event);
static void draw_timeline_mark_zoom(App const *app, Layout const *layout, float top,
                                    int32_t first_day);
static void draw_timeline_mark_hover(App const *app, Layout const *layout, float top);
static void draw_timeline_mark_hover_zoom(App const *app, Layout const *layout, float top,
                                          int32_t first_day);
static void draw_timeline_mark_zoom_items(App const *app, Layout const *layout,
                                          int32_t first_day, int32_t day_number,
                                          Vector2 center, float center_minute);
static void draw_timeline_mark_zoom_item(App const *app, Event const *event,
                                         Minute_Range range, Rectangle block, Vector2 center,
                                         float center_minute, float alpha);
static void draw_timeline_mark_zoom_edge(App const *app, Vector2 center, float center_minute,
                                         int32_t edge_minute, bool label_above);
static void draw_timeline_mark_zoom_ticks(App const *app, Vector2 center, float center_minute);
static void draw_timeline_mark_zoom_band(Vector2 center, float center_minute,
                                         Minute_Range range, float left, float right,
                                         Color color);
static float circle_half_width(float radius, float offset_y);
static void draw_timeline_day(App const *app, Rectangle day, int32_t day_number, bool hovered);
static Rectangle timeline_item_block(Rectangle day, Timeline_Item const *item);
static int32_t timeline_day_items(App const *app, int32_t day_number, Array_Slice items);
static Timeline_Hit timeline_hit_test(App const *app, Layout const *layout, float top,
                                      int32_t first_day);
static void timeline_drag_update(App *app, Layout const *layout, float top);
static void timeline_drag_finish(App *app, Layout const *layout, float top);
static Event timeline_drag_result(Event const *event, Timeline_Drag const *drag,
                                  float pointer_minute, int32_t pointer_day, int32_t step);
static int32_t timeline_drag_step(App const *app);
static int32_t timeline_step(bool fine);
static int32_t timeline_drag_day_delta(Timeline_Drag const *drag, int32_t pointer_day);
static Minute_Range event_time_range(Event const *event);
static int32_t minute_snap(float minute, int32_t step_minutes);
static int32_t timeline_column_nearest(Layout const *layout, float pointer_x);
static void timeline_cursor_update(App *app);
static void draw_timeline_drag(App const *app, Layout const *layout, float top,
                               int32_t first_day);
static void draw_timeline_day_background(Rectangle day, int32_t busy_top, int32_t busy_bottom,
                                         bool hovered);
static void draw_timeline_item(App const *app, Event const *event, Rectangle block,
                               int32_t day_number);
static int32_t timeline_items_collect(App const *app, int32_t day_number, Array_Slice items);
static Timeline_Item timeline_item_make(Event const *event, int32_t event_index);
static void timeline_items_sort(Array_Slice items);
static bool timeline_item_is_before(Timeline_Item const *a, Timeline_Item const *b);
static void timeline_items_assign_lanes(Array_Slice items);
static int32_t timeline_lane_find(Array_Slice lanes, int32_t minute_top);
static void timeline_items_set_lane_count(Array_Slice items, int32_t first_index,
                                          int32_t end_index, int32_t lane_count);
static Event const *focused_event_find(App const *app);
static void app_focus_item(App *app, int64_t id);
static void draw_grid_cell(App *app, Rectangle cell, int32_t day_number, int32_t column,
                           Event const *focused);
static void draw_grid_cell_day_number(App const *app, Rectangle cell, Date date, bool in_month);
static void draw_grid_cell_creation_markers(App const *app, Rectangle cell, int32_t day_number,
                                            Event const *focused);
static Bar_Hit draw_grid_cell_events(App const *app, Rectangle cell, int32_t day_number,
                                     int32_t column);
static bool draw_grid_cell_event_bar(App const *app, Event const *event, Rectangle bar_slot,
                                     int32_t day_number, int32_t start_day, int32_t column,
                                     bool *on_task_checkbox);
static void draw_event_label(App const *app, Event const *event, Str label, Rectangle area,
                             bool done);
static void draw_bar_outline(Rectangle bar, bool starts_here, bool ends_here, Color color);
static void draw_check_mark(Rectangle box, bool done, Color color);
static void draw_tick(Rectangle area, float thickness, Color color);
static void draw_panel(App *app, Rectangle panel);
static void draw_panel_events(App *app, Rectangle body);
static float draw_panel_events_height(App const *app, float card_width);
static void draw_panel_events_reveal(App *app, Rectangle list);
static void draw_panel_events_list(App *app, Rectangle list);
static void scroll_update(Scroll *scroll, Rectangle list, float content_height,
                          bool drag_scrolls);
static void draw_scrollbar(Rectangle list, float content_height, float offset_pixels);
static bool draw_panel_event_card(App *app, Event const *event, Rectangle card);
static void draw_panel_event_card_frame(App const *app, Event const *event, Rectangle card,
                                        bool hovered, bool focused, bool confirming);
static void draw_panel_event_card_title(App const *app, Event const *event, Rectangle card,
                                        bool done);
static Rich_Text_Link draw_panel_event_card_details(Fonts const *fonts, Event const *event,
                                                    Date_Time now, Rectangle card);
static void draw_panel_legend(Fonts const *fonts, Rectangle area);
static void draw_panel_form(App *app, Rectangle body);
static void draw_panel_form_kinds(App *app, Rectangle body, bool editing);
static void draw_panel_form_buttons(App *app, Rectangle body, float y);
static float draw_panel_form_rows_end(float y);
static float draw_panel_form_fields(App *app, Rectangle body, float y);
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y, float end_y);
static void draw_panel_form_target_tasks(App *app, Rectangle body, float y, float end_y);
static float form_target_tasks_height(Fonts const *fonts, Form const *form, float card_width);
static void draw_panel_form_target_tasks_list(App *app, Rectangle list);
static void draw_panel_form_target_task_card(App *app, Linked_Task const *task, Rectangle card,
                                             bool *toggled, bool *opened);
static void app_form_task_set_done(App *app, Linked_Task const *task);
static void app_form_goto_task(App *app, Event const *task);
static void draw_panel_form_target_tasks_status(App const *app, Rectangle body, float y);
static void draw_panel_form_targets(App *app, Rectangle body, float y, float end_y);
static bool draw_panel_form_targets_row(App const *app, Link_Target const *target,
                                        Rectangle row, bool clickable);
static int32_t form_targets_linked_count(Form const *form);
static bool form_deadline_is_missed(Form const *form, Date_Time now, bool tasks_done);
static void draw_panel_form_field(Fonts const *fonts, Form *form, int32_t field, Str label,
                                  Rectangle box);
static void draw_panel_form_date_field(Fonts const *fonts, Form *form, Date fallback,
                                       int32_t field, Str label, Rectangle box);
static void draw_panel_form_picker(App *app, Rectangle body);
static void draw_panel_form_picker_header(App *app, Rectangle popup, bool pressed);
static void draw_panel_form_picker_cells(Fonts const *fonts, Form *form, Rectangle popup,
                                         Date today, bool pressed, bool *chosen);
static void form_picker_open(Form *form, int32_t field, Date fallback);
static void draw_status(App *app, Rectangle status);
static void form_open(Form *form, Date date, int32_t kind);
static void form_open_edit(Form *form, Event const *event);
static void form_close(Form *form);
static int32_t form_field_next(Form const *form, bool backwards);
static bool form_field_visible(Form const *form, int32_t field);
static int32_t form_repeat_option(Form const *form);
static float event_card_height(Fonts const *fonts, Event const *event, bool confirming,
                               float card_width);
static Text_Lines text_wrap(Text_Style const *style, Str text, float width_pixels);
static bool text_line_fits(Text_Style const *style, Str line, float width_pixels);
static Str form_parse(Form const *form, Date today, Event *event);
static Str form_parse_problem(Form const *form, Date today, Event *event);
static Str form_parse_body(Form const *form, Event *event);
static Str form_parse_order(Event const *event);
static bool form_parse_time(Str text, int32_t *minute_of_day);
static bool form_parse_repeat_days(Str text, int32_t *days);
static bool event_occurs_on_day(Event const *event, int32_t day_number);
static bool event_created_on_day(Event const *event, int32_t day_number);
static bool event_is_timed(Event const *event);
static float task_fill_alpha(bool done);
static float task_outline_alpha(bool done);
static Str event_delete_question(Event const *event);
static bool event_in_day_cell(Event const *event, int32_t range);
static bool event_is_missed(Event const *event, Date_Time now);
static Color event_color(Event const *event, Date_Time now);
static void event_format_label(Event const *event, bool with_time, Date_Time now, char *buffer,
                               size_t buffer_size);
static void event_format_range(Event const *event, Date_Time now, char *buffer,
                               size_t buffer_size);
static void event_format_range_dates(Event const *event, char *buffer, size_t buffer_size);
static void event_format_created(Event const *event, Date_Time now, char *buffer,
                                 size_t buffer_size);
static void event_format_location(Event const *event, char *buffer, size_t buffer_size);
static void text_copy_trimmed(char *buffer, size_t buffer_size, Str text);
static Str text_skip_spaces(Str text);
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size);
static void draw_panel_form_description_field(App *app, Rectangle box);
static Str description_first_line(Str text);
static void description_format_image_count(int32_t count, char *buffer, size_t buffer_size);
static void draw_panel_description(App *app, Rectangle body);
static void draw_panel_description_hint(App const *app, Rectangle body, float y);
static void draw_description_editor(App *app, Rectangle box);
static void description_editor_pointer(App *app, Rectangle box, Rectangle inner);
static void description_focus_at(Form *form, int32_t index, bool extend);
static void description_editor_keys(App *app);
static void description_editor_shortcuts(App *app);
static void app_description_paste(App *app);
static void description_editor_typing(App *app);
static void description_type(Form *form, Str text);
static void description_delete(Description_Editor *editor, bool forward);
static void description_editor_moves(Description_Editor *editor, Text_Style const *style,
                                     bool shift, bool control);
static int32_t description_line_end_caret(Description_Editor const *editor, int32_t line_index);
static void app_description_insert_image(App *app, Image image);
static void image_fit_side(Image *image, int32_t side_max);
static void app_take_dropped_files(App *app);
static void app_open_link(App *app, Str link);
static void description_open(Form *form);
static void description_close(Form *form);
static void description_set_text(Description_Editor *editor, Str text);
static void description_clear_images(Description_Editor *editor);
static Str description_text(Description_Editor const *editor);
static bool description_replace(Description_Editor *editor, int32_t start, int32_t end,
                                Str insert, Description_Image const *added);
static void description_layout_update(Description_Editor *editor, Text_Style const *style);
static void description_layout(Description_Editor *editor, Text_Style const *style);
static int32_t description_layout_text_line(Text_Style const *style, Str text, int32_t start,
                                            float width_pixels, int32_t *end);
static Vector2 description_image_size(Description_Editor const *editor, int32_t image_index,
                                      float width_pixels);
static float description_content_height(Description_Editor const *editor);
static bool description_line_continues(Description_Editor const *editor, int32_t line_index);
static int32_t description_caret_line(Description_Editor const *editor, int32_t index);
static float description_caret_x(Description_Editor const *editor, Text_Style const *style,
                                 int32_t index);
static int32_t description_line_at(Description_Editor const *editor, float y);
static int32_t description_index_at(Description_Editor const *editor, Text_Style const *style,
                                    float x, float y);
static int32_t description_line_index_at_x(Description_Editor const *editor,
                                           Text_Style const *style, int32_t line_index,
                                           float x);
static Rich_Text_Link description_link_at_point(Description_Editor const *editor,
                                                Text_Style const *style, float x, float y);
static void description_caret_reveal(Description_Editor *editor, float view_height,
                                     float content_height);
static void draw_description_editor_lines(App const *app, Rectangle inner, bool focused);
static void draw_description_image(App const *app, Texture2D texture, Rectangle area);
static void app_form_load_images(App *app, int64_t event_id);
static Texture2D app_image_texture_load(App *app, int64_t id);
static bool app_form_save_images(App *app, int64_t id, bool inserted);
static void form_description_copy(Form const *form, char *buffer, size_t buffer_size);
static float text_width(Text_Style const *style, Str text);
static float text_advance(Text_Style const *style, int32_t codepoint);
static int32_t text_index_at_x(Text_Style const *style, Str text, size_t start, size_t end,
                               float x);
static int32_t links_collect(Str text, size_t until, Rich_Text_Link *links, int32_t count_max);
static Rich_Text_Link links_find_at(Array_Slice links, size_t index);
static void draw_text_links(Text_Style const *style, Str text, size_t start, size_t end,
                            Array_Slice links, float x, float y, Color color, Color link_color);
static size_t text_wrap_skip(Str text, size_t index);
static void draw_panel_form_description_row(App *app, Rectangle row);
static void draw_panel_form_attachments_field(App *app, Rectangle box);
static void attachments_format_count(int32_t count, char *buffer, size_t buffer_size);
static void attachments_format_size(int32_t size_bytes, char *buffer, size_t buffer_size);
static void draw_panel_attachments(App *app, Rectangle body);
static void draw_panel_attachments_hint(App const *app, Rectangle body, float y);
static void draw_panel_attachments_zone(App *app, Rectangle zone);
static void draw_dashed_rectangle(Rectangle area, Color color);
static void draw_panel_attachments_list(App *app, Rectangle list);
static Attachment_Action draw_panel_attachments_row(App const *app, Attachment const *attachment,
                                                    Rectangle row);
static void attachments_open(Form *form);
static void attachments_close(Form *form);
static void attachments_remove(Attachment_List *attachments, int32_t index);
static void app_attachment_choose(App *app);
static void app_attachment_add(App *app, Str path);
static void app_attachment_save(App *app, Attachment const *attachment);
static Str path_file_name(Str path);
static void app_form_load_attachments(App *app, int64_t event_id);
static bool app_form_save_attachments(App *app, int64_t id, bool inserted);
static void theme_apply(void);
static Color style_color(int32_t property);
static void draw_text(Text_Style const *style, Str text, float x, float y, Color color);
static float draw_text_round(float position, float scale);
static Vector2 measure_text(Text_Style const *style, Str text);
static void draw_text_centered(Text_Style const *style, Str text, Rectangle area,
                               Color color);
static void scissor_begin(Fonts const *fonts, float x, float y, float width, float height);
static void scissor_end(Fonts const *fonts);

App *app_create(Backend *backend, App_Config const *config) {
    // Static rather than on the stack, because App holds every loaded event; one per program.
    static App app;
    assert(!app.created);
    assert(backend != NULL);
    assert(config->scale > 0.0f);
    app.created = true;
    app.backend = backend;
    app.clipboard_image = config->clipboard_image;
    app.files = config->files;
    app.touch = config->touch;
    app.input_enabled = true;

    theme_apply();
    fonts_load(&app.fonts, config->font_directory, config->scale);
    GuiSetFont(app.fonts.ui.font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, TEXT_SIZE_PIXELS);
    // Rubik's advances already space its glyphs; raylib's default font needs extra room.
    int32_t text_spacing_pixels = 2;
    if (app.fonts.loaded) {
        text_spacing_pixels = 0;
    }
    GuiSetStyle(DEFAULT, TEXT_SPACING, text_spacing_pixels);
    if (!app.fonts.loaded) {
        snprintf(app.status, sizeof(app.status), "Rubik font not found: using the default.");
    }

    Date_Time const now = date_time_now();
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

    float const scale = app->fonts.scale;
    // Layout pixels start at the area's corner: mouse positions, drawing and scissor
    // rectangles are all moved and scaled the same way, so raygui works unchanged.
    app->fonts.origin = (Vector2){ area.x, area.y };
    app->cursor = APP_CURSOR_DEFAULT;
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
    app_take_dropped_files(app);
    app_sync(app);
    // Again, because a key may have changed the range or opened the form, and loading may have
    // changed the items without a time.
    layout = layout_compute(area.width / scale, area.height / scale, app->form.open, app->range,
                            app_untimed_count_max(app));

    // The fonts were rasterized at this scale, so text stays sharp; on the desktop the scale is
    // 1 and only the offset matters.
    BeginMode2D((Camera2D){ .offset = { area.x, area.y }, .target = { 0, 0 }, .rotation = 0,
                            .zoom = scale });
    int32_t const range_before = app->range;
    Date const selected_before = app->selected;
    draw_header(app, &layout);
    // The header's toggle, or its arrows, may have changed the range or the selection after the
    // layout and the visible dates were worked out. Drawing the grid with the old ones for one
    // frame shows a month's cells in a week's layout, or the reverse, as a flash.
    if (app->range != range_before || !date_equal(app->selected, selected_before)) {
        app_sync(app);
        layout = layout_compute(area.width / scale, area.height / scale, app->form.open,
                                app->range, app_untimed_count_max(app));
    }
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

void app_draw_text(App const *app, App_Text style, Str text, float x, float y) {
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

float app_measure_text(App const *app, App_Text style, Str text) {
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

void app_set_info(App *app, Str info) {
    // Kept as far as it fits: the status bar is no place for text longer than its buffer.
    (void)str_copy_truncated(info, app->info, sizeof(app->info));
}

void app_set_status(App *app, Str status) {
    // Kept as far as it fits, as for the info.
    (void)str_copy_truncated(status, app->status, sizeof(app->status));
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

App_Cursor app_cursor(App const *app) {
    assert(app->created);
    assert(app->cursor >= APP_CURSOR_DEFAULT);
    assert(app->cursor <= APP_CURSOR_TEXT);

    return (App_Cursor)app->cursor;
}

bool app_text_input_active(App const *app) {
    return app->form.open && app->form.field_active != FORM_FIELD_NONE;
}

void app_set_input_enabled(App *app, bool enabled) {
    assert(app->created);

    app->input_enabled = enabled;
}

/**
 * Loads Rubik at each size it is drawn at, times the scale, so that each glyph covers exactly
 * the screen pixels it is drawn on. Needs the window, because glyphs go to a texture.
 */
static void fonts_load(Fonts *fonts, char const *directory, float scale) {
    int codepoints[FONT_CODEPOINT_COUNT] = { 0 };
    int32_t count = 0;
    for (int32_t codepoint = 32; codepoint <= 126; codepoint++) {
        codepoints[count++] = codepoint;
    }
    for (int32_t codepoint = 160; codepoint <= 255; codepoint++) {
        codepoints[count++] = codepoint;
    }
    for (int32_t codepoint = 256; codepoint <= 383; codepoint++) {
        codepoints[count++] = codepoint;
    }
    for (int32_t codepoint = 8211; codepoint <= 8212; codepoint++) {  // En and em dash
        codepoints[count++] = codepoint;
    }
    for (int32_t codepoint = 8216; codepoint <= 8223; codepoint++) {  // Curly quotes
        codepoints[count++] = codepoint;
    }
    codepoints[count++] = 8230;  // Ellipsis
    codepoints[count++] = 8364;  // Euro sign
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

/**
 * Falls back to raylib's default font, scaled, when the file is missing or unreadable, so that
 * the calendar still works; *loaded then becomes false.
 */
static Text_Style fonts_load_style(char const *directory, char const *file_name,
                                   int32_t size_pixels, float scale, int const *codepoints,
                                   bool *loaded) {
    assert(size_pixels > 0);
    assert(scale > 0.0f);

    Text_Style style = { GetFontDefault(), (float)size_pixels, scale };
    char path[PATH_SIZE] = "";
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

static void app_handle_keys(App *app, Layout const *layout) {
    // Android's Back button does what Escape does on the desktop.
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK)) {
        if (app->form.open && app->form.picker_open) {
            // The picker is over the form: Escape closes what is on top first.
            app->form.picker_open = false;
            return;
        }
        if (app->form.open && app->form.description.open) {
            // The description's page is over the form's fields, which Escape goes back to.
            description_close(&app->form);
            return;
        }
        if (app->form.open && app->form.attachments.open) {
            // As the description's page is: over the fields, which Escape goes back to.
            attachments_close(&app->form);
            return;
        }
        if (app->form.open) {
            form_close(&app->form);
        }
        app->event_delete_id = 0;
        // A block being drawn, or an item being dragged, is dropped; the release then does
        // nothing.
        app->timeline_mark = (Timeline_Mark){ .pressed = false };
        app->month_mark = (Month_Mark){ .pressed = false };
        app->timeline_drag = (Timeline_Drag){ .event_id = 0 };
        app->task_drag = (Task_Drag){ .event_id = 0 };
        return;
    }
    if (app->form.open && app->form.description.open) {
        // Keys belong to the description, which reads them as it is drawn (see
        // description_editor_keys), and do not move the calendar under it.
        return;
    }
    if (app->form.open && app->form.attachments.open) {
        // The attachments' page takes no keys, and the calendar under it must not move.
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

    if (IsKeyPressed(KEY_Z)) {
        app->timeline_fine = !app->timeline_fine;
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
        if (IsKeyPressed(KEY_STEPS[i].key)) {
            target = date_add_days(target, KEY_STEPS[i].days);
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
    app_select(app, app_handle_keys_pointer(app, layout, target));
}

/**
 * target moved by the pointer's gestures. Over the calendar only, so that they mean nothing over
 * the panel: a sideways swipe on a touch screen, or the wheel over the day cells, moves by the
 * range. Over the timeline the wheel scrolls the hours instead (see draw_timeline).
 */
static Date app_handle_keys_pointer(App const *app, Layout const *layout, Date target) {
    Vector2 const mouse = GetMousePosition();
    bool const over_grid = CheckCollisionPointRec(mouse, layout->grid);
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
    return target;
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

/**
 * Brings derived state up to date before drawing: the date and time, the loaded events and the
 * focused event, which is always one of the events on the selected date.
 */
static void app_sync(App *app) {
    Date_Time const now = date_time_now();
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
    Event const *focused = app_event_find(app, app->event_focused_id);
    if (focused == NULL || !event_occurs_on_day(focused, selected_day)) {
        app->event_focused_id = app_event_first_on_day(app, selected_day);
    }
}

/**
 * Also records the revision when loading fails, so that a failure is not retried every frame;
 * the next change of revision, or of month, tries again.
 */
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
    int32_t count = backend_load_range(app->backend, first, last,
                                       array_slice_make(app->events, EVENT_COUNT_MAX),
                                       &app->events_truncated);
    if (count < 0) {
        backend_error_copy(app->backend, app->status, sizeof(app->status));
        return;
    }
    assert(count <= EVENT_COUNT_MAX);
    app->event_count = count;

    bool completions_truncated = false;
    count = backend_load_completions(app->backend, first, last,
                                     array_slice_make(app->completions, COMPLETION_COUNT_MAX),
                                     &completions_truncated);
    if (count < 0) {
        backend_error_copy(app->backend, app->status, sizeof(app->status));
        return;
    }
    assert(count <= COMPLETION_COUNT_MAX);
    app->completion_count = count;
    app->events_truncated = app->events_truncated || completions_truncated;
}

static bool app_task_done(App const *app, int64_t id, int32_t start_day) {
    for (int32_t i = 0; i < app->completion_count; i++) {
        Task_Completion const *completion = &app->completions[i];
        if (completion->event_id == id && completion->occurrence_start_day == start_day) {
            return true;
        }
    }
    return false;
}

/**
 * The moment app_sync last read from the clock, so that a frame draws one time throughout.
 */
static Date_Time app_now(App const *app) {
    assert(date_is_valid(app->today));
    assert(app->now_minute >= 0);

    return (Date_Time){ app->today, app->now_minute };
}

static void app_task_set_done(App *app, Event const *event, int32_t start_day, bool done) {
    assert(event->kind == EVENT_KIND_TASK);
    assert(app_task_done(app, event->id, start_day) != done);

    Date occurrence_start = date_from_day_number(start_day);
    if (!backend_set_task_done(app->backend, event->id, occurrence_start, done)) {
        backend_error_copy(app->backend, app->status, sizeof(app->status));
        return;
    }
    // completions[] is reloaded by the next app_sync, together with the events.
    app->events_stale = true;
    char const *outcome = "Not done:";
    if (done) {
        outcome = "Done:";
    }
    snprintf(app->status, sizeof(app->status), "%s \"%s\".", outcome, event->title);
}

static Event const *app_event_find(App const *app, int64_t id) {
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

static int64_t app_event_first_on_day(App const *app, int32_t day_number) {
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
    Str const mistake = form_parse(&app->form, app->today, &event);
    if (mistake.length > 0) {
        (void)str_copy_truncated(mistake, app->form.error, sizeof(app->form.error));
        return;
    }
    // Typing the address and city of an existing location links to it, so that events at the
    // same place share one location.
    bool has_location = event.location.address[0] != '\0' || event.location.city[0] != '\0';
    if (has_location) {
        event.location.id = backend_location_find_or_create(
            app->backend, str_from_cstr(event.location.address),
            str_from_cstr(event.location.city));
        if (event.location.id == 0) {
            backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
            return;
        }
    }
    bool editing = app->form.editing_id != 0;
    int64_t id = app->form.editing_id;
    if (editing) {
        event.id = id;
        if (!backend_update(app->backend, &event)) {
            backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
            return;
        }
    } else {
        id = backend_insert(app->backend, &event);
        if (id == 0) {
            backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
            return;
        }
        // Saving again after the links fail below updates this entry rather than adding another.
        app->form.editing_id = id;
    }
    if (!app_form_save_images(app, id, !editing)) {
        return;
    }
    if (!app_form_save_attachments(app, id, !editing)) {
        return;
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
    char const *outcome = "Saved";
    if (editing) {
        outcome = "Updated";
    }
    snprintf(app->status, sizeof(app->status), "%s \"%s\".", outcome, event.title);
}

/**
 * Ctrl+C copies the field being edited, Ctrl+X cuts it, and Ctrl+V pastes at its cursor.
 * raygui's text box has no selection, so copying and cutting take the whole field, which the
 * status bar says. GLFW drops the control characters these keys type, so the text box itself
 * sees nothing of them. On Android raylib has no clipboard, and pasting finds nothing.
 */
static void app_form_clipboard(App *app) {
    Form *form = &app->form;
    int32_t const field = form->field_active;
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    assert(field != FORM_FIELD_DESCRIPTION); // Its page handles its own keys.
    char *text = form->text[field];
    bool const cut = IsKeyPressed(KEY_X);
    if (IsKeyPressed(KEY_C) || cut) {
        if (text[0] == '\0') {
            return; // Copying nothing would only empty the clipboard.
        }
        SetClipboardText(text);
        if (cut) {
            text[0] = '\0';
            text_box_cursor_index_set(0);
        }
        char const *outcome = "Copied";
        if (cut) {
            outcome = "Cut";
        }
        snprintf(app->status, sizeof(app->status), "%s the whole field.", outcome);
        return;
    }
    if (IsKeyPressed(KEY_V)) {
        // NULL when the clipboard is empty, holds no text, or does not exist.
        char const *clipboard = GetClipboardText();
        if (clipboard == NULL) {
            return;
        }
        int32_t cursor_index = text_box_cursor_index();
        int32_t const length = (int32_t)strlen(text);
        if (cursor_index > length) {
            cursor_index = length; // raygui clamps it only when it next draws the box.
        }
        text_box_cursor_index_set(text_box_paste(text, FORM_FIELD_SIZES[field], cursor_index,
                                                 str_from_cstr(clipboard)));
    }
}

/**
 * Opens the form for a new entry. The targets a task can be linked to are loaded whatever the
 * kind, because the kind can still be switched to task.
 */
static void app_form_open(App *app, Date date, int32_t kind) {
    app->event_delete_id = 0;
    form_open(&app->form, date, kind);
    app_form_load_links(app);
}

static void app_form_open_edit(App *app, Event const *event) {
    assert(event->id > 0);

    form_open_edit(&app->form, event);
    app_form_load_images(app, event->id);
    app_form_load_attachments(app, event->id);
    app_form_load_links(app);
}

/**
 * An existing link target's form lists its tasks; every other form offers the targets a task
 * can be linked to. A failure shows in the form and leaves links_loaded false, so that saving
 * cannot erase links that were never shown.
 */
static void app_form_load_links(App *app) {
    Form *form = &app->form;
    assert(form->open);
    form->links_loaded = false;
    form->links_truncated = false;
    form->task_count = 0;
    form->target_count = 0;
    int32_t count = 0;
    if (event_kind_is_target(form->kind) && form->editing_id != 0) {
        count = backend_load_target_tasks(app->backend, form->editing_id,
                                          array_slice_make(form->tasks, LINK_COUNT_MAX),
                                          &form->links_truncated);
        form->task_count = int32_max(count, 0);
    } else {
        count = backend_load_task_targets(app->backend, form->editing_id, false,
                                          array_slice_make(form->targets, LINK_COUNT_MAX),
                                          &form->links_truncated);
        form->target_count = int32_max(count, 0);
    }
    if (count < 0) {
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    assert(count <= LINK_COUNT_MAX);
    form->links_loaded = true;
}

/**
 * Saves the targets checked in a task's form. An entry saved as an event loses its links, as
 * it may have been a task until now. Returns false, with the form's error set, on failure.
 */
static bool app_form_save_links(App *app, int64_t id, int32_t kind, bool inserted) {
    Form const *form = &app->form;
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
    int64_t target_ids[LINK_COUNT_MAX] = { 0 };
    int32_t target_count = 0;
    for (int32_t i = 0; kind == EVENT_KIND_TASK && i < form->target_count; i++) {
        if (form->targets[i].linked) {
            target_ids[target_count] = form->targets[i].target_id;
            target_count++;
        }
    }
    if (kind == EVENT_KIND_TASK) {
        assert(target_count == form_targets_linked_count(form));
    } else {
        assert(target_count == 0);
    }
    if (!backend_set_task_targets(app->backend, id,
                                  array_slice_make(target_ids, target_count))) {
        backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
        return false;
    }
    return true;
}

static void app_event_delete(App *app, int64_t id) {
    assert(id > 0);

    app->event_delete_id = 0;
    char title[EVENT_TITLE_SIZE] = "";
    Event const *event = app_event_find(app, id);
    if (event != NULL) {
        snprintf(title, sizeof(title), "%s", event->title);
    }

    if (!backend_remove(app->backend, id)) {
        backend_error_copy(app->backend, app->status, sizeof(app->status));
        // The event may be gone, so show the real state.
        app->events_stale = true;
        return;
    }
    // events[] is reloaded by the next app_sync; this frame still draws the old list.
    app->event_focused_id = 0;
    app->events_stale = true;
    snprintf(app->status, sizeof(app->status), "Deleted \"%s\".", title);
}

/**
 * width and height are in layout pixels. A window taller than wide, such as a phone, gets the
 * portrait layout; otherwise the panel sits to the right of the calendar.
 * untimed_count is app_untimed_count_max: how many rows of bars a week or day's cells need.
 */
static Layout layout_compute(float width, float height, bool form_open, int32_t range,
                             int32_t untimed_count) {
    if (width < height) {
        return layout_compute_portrait(width, height, form_open, range, untimed_count);
    }
    float const padding = (float)PADDING_PIXELS;
    float const status_height = (float)STATUS_HEIGHT_PIXELS;
    Layout layout = { .grid_visible = true, .portrait = false };
    layout.status = (Rectangle){ 0, height - status_height, width, status_height };
    layout.panel = (Rectangle){ width - padding - PANEL_WIDTH_PIXELS, padding,
                                PANEL_WIDTH_PIXELS, height - status_height - 2 * padding };

    float calendar_width = layout.panel.x - 2 * padding;
    layout.header = (Rectangle){ padding, padding, calendar_width, HEADER_HEIGHT_PIXELS };
    // The range selector at the right end of the header, centered in its height.
    float const range_width = RANGE_COUNT * (RANGE_ITEM_WIDTH_PIXELS + 2) - 2;
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

/**
 * Rows and columns of day cells for the range, filling the grid rectangle; in a week or day,
 * the grid is first split into the day cells and the timeline.
 */
static void layout_set_grid(Layout *layout, int32_t range, int32_t untimed_count) {
    assert(range >= 0);
    assert(range < RANGE_COUNT);

    layout->columns = GRID_COLUMN_COUNT;
    if (range == RANGE_DAY) {
        layout->columns = 1;
    }
    layout->rows = 1;
    if (range == RANGE_MONTH) {
        layout->rows = GRID_ROW_COUNT;
    }
    assert(layout->columns * layout->rows == range_day_count(range));
    if (range != RANGE_MONTH) {
        layout_set_grid_timeline(layout, untimed_count);
    } else {
        // Cut from the left of the grid and of the weekday names, so that each day stays above
        // its column, as the timeline's gutter does.
        float const gutter = (float)WEEK_NUMBER_GUTTER_WIDTH_PIXELS;
        layout->week_numbers = (Rectangle){ layout->grid.x, layout->grid.y, gutter,
                                            layout->grid.height };
        layout->grid.x += gutter;
        layout->grid.width -= gutter;
        layout->weekdays.x += gutter;
        layout->weekdays.width -= gutter;
        assert(layout->grid.width > 0.0f);
    }
    layout->cell_width = layout->grid.width / (float)layout->columns;
    layout->cell_height = layout->grid.height / (float)layout->rows;
    // The minimum window size guarantees room for every part.
    assert(layout->cell_width > 0.0f);
    assert(layout->cell_height > 0.0f);
}

/**
 * The day cells keep the top of the grid, as tall as the busiest day's items without a time
 * need, and the timeline takes the rest. The gutter for the hour labels is cut from the left
 * of both, and of the weekday names, so that each day lines up with its hours.
 */
static void layout_set_grid_timeline(Layout *layout, int32_t untimed_count) {
    assert(untimed_count >= 0);
    assert(untimed_count <= EVENT_COUNT_MAX);

    float const gutter = (float)TIMELINE_GUTTER_WIDTH_PIXELS;
    float const slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    Rectangle const area = layout->grid;
    float height = (float)(CELL_TOP_LINE_PIXELS + CELL_BOTTOM_MARGIN_PIXELS) +
                   (float)untimed_count * slot_height;
    float const height_max = area.height * UNTIMED_HEIGHT_PERCENT_MAX / 100.0f;
    if (height > height_max) {
        height = height_max;
    }
    layout->grid = (Rectangle){ area.x + gutter, area.y, area.width - gutter, height };
    // The week's number sits in the gutter beside the day cells, as the month view's do.
    layout->week_numbers = (Rectangle){ area.x, area.y, gutter, height };
    layout->timeline = (Rectangle){ area.x, area.y + height, area.width, area.height - height };
    layout->weekdays.x += gutter;
    layout->weekdays.width -= gutter;
    // The minimum window size guarantees room for both.
    assert(layout->grid.width > 0.0f);
    assert(layout->timeline.height > 0.0f);
}

/**
 * The most items without a time on any one day shown, which sets how tall a week or day's cells
 * are. A month's cells share the grid evenly, so it needs none.
 */
static int32_t app_untimed_count_max(App const *app) {
    if (app->range == RANGE_MONTH) {
        return 0;
    }
    int32_t const first_day = date_to_day_number(app->visible_first);
    int32_t const day_count = range_day_count(app->range);
    int32_t count_max = 0;
    for (int32_t day = 0; day < day_count; day++) {
        int32_t count = 0;
        for (int32_t i = 0; i < app->event_count; i++) {
            Event const *event = &app->events[i];
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

/**
 * The date one range before (direction -1) or after (1) date: a month, a week or a day.
 */
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

/**
 * A month starts on the Monday of the week that holds its first day, a week on its Monday, and
 * a day view shows the selected day.
 */
static Date range_first_date(App const *app) {
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

/**
 * "October 2026"; "12 - 18 October 2026", or with both months or years when the week spans
 * them; "Tue 20 October 2026".
 */
static void range_title(App const *app, char *buffer, size_t buffer_size) {
    Date const first = app->visible_first;
    Date const last = date_add_days(first, GRID_COLUMN_COUNT - 1);
    Str const first_month = month_name(first.month);
    Str const first_month_short = month_short_name(first.month);
    Str const last_month_short = month_short_name(last.month);
    switch ((Calendar_Range)app->range) {
    case RANGE_MONTH: {
        Str const month = month_name(app->events_month.month);
        snprintf(buffer, buffer_size, "%.*s %" PRId32, STR_ARGUMENTS(month),
                 app->events_month.year);
        return;
    }
    case RANGE_WEEK:
        if (first.month == last.month) {
            snprintf(buffer, buffer_size, "%" PRId32 " - %" PRId32 " %.*s %" PRId32, first.day,
                     last.day, STR_ARGUMENTS(first_month), first.year);
        } else if (first.year == last.year) {
            snprintf(buffer, buffer_size, "%" PRId32 " %.*s - %" PRId32 " %.*s %" PRId32,
                     first.day, STR_ARGUMENTS(first_month_short), last.day,
                     STR_ARGUMENTS(last_month_short), last.year);
        } else {
            snprintf(buffer, buffer_size,
                     "%" PRId32 " %.*s %" PRId32 " - %" PRId32 " %.*s %" PRId32, first.day,
                     STR_ARGUMENTS(first_month_short), first.year, last.day,
                     STR_ARGUMENTS(last_month_short), last.year);
        }
        return;
    case RANGE_DAY: {
        Str const weekday = weekday_short_name(date_weekday(first));
        snprintf(buffer, buffer_size, "%.*s %" PRId32 " %.*s %" PRId32, STR_ARGUMENTS(weekday),
                 first.day, STR_ARGUMENTS(first_month), first.year);
        return;
    }
    case RANGE_COUNT:
        break;
    }
    assert(false); // Not a Calendar_Range.
    buffer[0] = '\0';
}

/**
 * Items with an occurrence on a day shown, for the status bar.
 */
static int32_t app_visible_item_count(App const *app) {
    int32_t const first_day = date_to_day_number(app->visible_first);
    int32_t const day_count = range_day_count(app->range);
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

/**
 * The calendar above and the panel below, each the full width. While the form is open it takes
 * everything below the header, because it needs the height and the grid cannot be used then.
 */
static Layout layout_compute_portrait(float width, float height, bool form_open,
                                      int32_t range, int32_t untimed_count) {
    float const padding = (float)PADDING_PIXELS_PORTRAIT;
    float const status_height = (float)STATUS_HEIGHT_PIXELS;
    float const content_width = width - 2 * padding;
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

static Rectangle grid_cell_rectangle(Layout const *layout, int32_t cell_index) {
    assert(cell_index >= 0);
    assert(cell_index < layout->rows * layout->columns);

    int32_t column = cell_index % layout->columns;
    int32_t row = cell_index / layout->columns;
    return (Rectangle){ layout->grid.x + (float)column * layout->cell_width,
                        layout->grid.y + (float)row * layout->cell_height,
                        layout->cell_width, layout->cell_height };
}

/**
 * The grid starts on the Monday of the week that contains the first of the month.
 */
static Date grid_first_date(Date month) {
    assert(month.day == 1);

    Date first = date_add_days(month, -date_weekday(month));
    assert(date_weekday(first) == 0);
    return first;
}

/**
 * Buttons on the left, the range selector on the right (or in its own row on a phone), and the
 * title of what is shown between them.
 */
static void draw_header(App *app, Layout const *layout) {
    Rectangle const header = layout->header;
    float const height = header.height;
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
        GuiToggleGroup(item, RANGE_LABELS, &app->range);
    }

    char title[TEXT_LINE_SIZE] = "";
    range_title(app, title, sizeof(title));
    float const buttons_width = 194;
    float right = header.x + header.width;
    if (!layout->portrait) {
        right = layout->range_bar.x - 8;
    }
    Rectangle area = { header.x + buttons_width, header.y, right - header.x - buttons_width,
                       height };
    // The normal font when the heading font is too wide for the space, as on a phone.
    Text_Style const *style = &app->fonts.heading;
    if (measure_text(style, str_from_cstr(title)).x > area.width) {
        style = &app->fonts.ui;
    }
    draw_text_centered(style, str_from_cstr(title), area, style_color(TEXT_COLOR_NORMAL));
}

/**
 * The names of the days shown: short in a week or month, in full for a single day.
 */
static void draw_weekdays(Fonts const *fonts, Layout const *layout, Date first) {
    for (int32_t column = 0; column < layout->columns; column++) {
        Rectangle area = { layout->weekdays.x + (float)column * layout->cell_width,
                           layout->weekdays.y, layout->cell_width, layout->weekdays.height };
        int32_t weekday = date_weekday(date_add_days(first, column));
        // Weekend names are dimmed, as on a paper calendar.
        int32_t color_property = TEXT_COLOR_NORMAL;
        if (weekday >= 5) {
            color_property = TEXT_COLOR_DISABLED;
        }
        Color const color = style_color(color_property);
        // The full name when there is room for it: a day alone.
        Str name = weekday_short_name(weekday);
        if (layout->columns == 1) {
            name = weekday_name(weekday);
        }
        draw_text_centered(&fonts->ui, name, area, color);
    }
}

/**
 * Draws from visible_first, set by app_sync, rather than the selected date, so that a click
 * that changes the selection mid-frame never draws one range's days with another's events.
 */
static void draw_grid(App *app, Layout const *layout) {
    int32_t first_day = date_to_day_number(app->visible_first);
    Event const *focused = focused_event_find(app);
    int32_t const cell_count = layout->rows * layout->columns;
    for (int32_t cell_index = 0; cell_index < cell_count; cell_index++) {
        Rectangle cell = grid_cell_rectangle(layout, cell_index);
        int32_t column = cell_index % layout->columns;
        draw_grid_cell(app, cell, first_day + cell_index, column, focused);
    }
    if (layout->timeline.height > 0.0f) {
        draw_timeline(app, layout, first_day);
    }
    draw_week_numbers(app, layout);
    month_mark_update(app, layout, first_day);
    draw_month_mark(app, layout, first_day);
    draw_month_ghost(app, layout, first_day);
    task_drag_update(app, layout, first_day);
    // Last, so that neither the cells nor the timeline cover it.
    draw_grid_selection(app, layout, first_day);
}

/**
 * The number of each week left of its row, dimmed so that the days stand out, with "Wk" above
 * them: one per row in the month view, and the one number of the range in a week or day, in the
 * gutter beside the day cells. The numbers are the ISO 8601 ones, as the Monday-first weeks are.
 */
static void draw_week_numbers(App const *app, Layout const *layout) {
    if (layout->week_numbers.width <= 0.0f) {
        return;
    }
    Color const color = style_color(TEXT_COLOR_DISABLED);
    draw_text_centered(&app->fonts.small, STR("Wk"),
                       (Rectangle){ layout->week_numbers.x, layout->weekdays.y,
                                    layout->week_numbers.width, layout->weekdays.height },
                       color);
    for (int32_t row = 0; row < layout->rows; row++) {
        // A month row starts on a Monday. A week has one row, and a day's week is its own.
        Date const row_first = date_add_days(app->visible_first, row * layout->columns);
        if (layout->columns == GRID_COLUMN_COUNT) {
            assert(date_weekday(row_first) == 0);
        }
        char number[4] = "";
        snprintf(number, sizeof(number), "%" PRId32, date_iso_week(row_first));
        // As tall as the day number's line in the row's first cell, so that they read together.
        Rectangle const line = { layout->week_numbers.x,
                                 layout->grid.y + (float)row * layout->cell_height + 7,
                                 layout->week_numbers.width, (float)TEXT_SIZE_PIXELS };
        draw_text_centered(&app->fonts.small, str_from_cstr(number), line, color);
    }
}

/**
 * An accent outline around the selected day; in a week or day, down through its hours too.
 */
static void draw_grid_selection(App const *app, Layout const *layout, int32_t first_day) {
    int32_t const cell_index = date_to_day_number(app->selected) - first_day;
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

/**
 * A press on a task without a time, in a day cell of the week or day view, may start moving it:
 * to another day's cell, or down to the hours to give it a time. Only there, where the hours
 * exist; the desktop only, as a finger's drag is a swipe. Not while a form is open, which may
 * hold the task's old dates and would save them back over the move, nor on the check box, which
 * marks the task done instead.
 */
static void task_drag_start(App *app, Bar_Hit hit, int32_t day_number) {
    assert(hit.event_id != 0);

    bool const hours_shown = app->range == RANGE_WEEK || app->range == RANGE_DAY;
    if (!hours_shown || app->touch || app->form.open || hit.on_task_checkbox) {
        return;
    }
    Event const *task = app_event_find(app, hit.event_id);
    assert(task != NULL);
    if (task->kind != EVENT_KIND_TASK || event_is_timed(task)) {
        return;
    }
    app->task_drag = (Task_Drag){ .event_id = task->id, .press_day = day_number };
}

/**
 * Follows a task dragged by the pointer while the button is held, shows a ghost of it where it
 * would land, and saves it at the release.
 */
static void task_drag_update(App *app, Layout const *layout, int32_t first_day) {
    Task_Drag *drag = &app->task_drag;
    if (drag->event_id == 0) {
        return;
    }
    bool const hours_shown = app->range == RANGE_WEEK || app->range == RANGE_DAY;
    // The range changed under the press, by a key: the days are no longer the ones pressed.
    if (!hours_shown || app->form.open) {
        *drag = (Task_Drag){ .event_id = 0 };
        return;
    }
    // Gone when a reload meanwhile no longer has it, as after a phone deleted it.
    Event const *task = app_event_find(app, drag->event_id);
    if (task == NULL) {
        *drag = (Task_Drag){ .event_id = 0 };
        return;
    }
    Vector2 const pointer = GetMousePosition();
    Event const moved = task_drag_result(app, layout, task, drag->press_day, first_day, pointer);
    bool const changed = !date_equal(moved.start_date, task->start_date) ||
                         moved.start_minute != task->start_minute;
    if (changed) {
        drag->moved = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (changed) {
            task_drag_finish(app, task, &moved, drag->press_day);
        }
        *drag = (Task_Drag){ .event_id = 0 };
        return;
    }
    // A release this frame never seen, as when the window lost the pointer, ends the drag.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        *drag = (Task_Drag){ .event_id = 0 };
        return;
    }
    if (!drag->moved || !changed) {
        return;
    }
    app->cursor = APP_CURSOR_MOVE;
    if (!event_is_timed(&moved)) {
        draw_grid_ghost(app, layout, first_day, &moved);
        return;
    }
    // On the hours: a block at the time, on the day under the pointer, within the timeline.
    Rectangle const timeline = layout->timeline;
    float const top = timeline.y - app->timeline_scroll.offset_pixels;
    int32_t const column = task_drag_pointer_day(layout, pointer, first_day) - first_day;
    Rectangle const day = { layout->grid.x + (float)column * layout->cell_width, top,
                            layout->cell_width, (float)TIMELINE_HEIGHT_PIXELS };
    app->fonts.clip = timeline;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, timeline.x, timeline.y, timeline.width, timeline.height);
    draw_timeline_ghost_block(app, &moved, day);
    app->fonts.clipping = false;
    EndScissorMode();
}

/**
 * The day under the pointer: the column of the hours when it is over them, else the nearest
 * column, so that a drag past the first or last day stays on it.
 */
static int32_t task_drag_pointer_day(Layout const *layout, Vector2 pointer, int32_t first_day) {
    int32_t column = timeline_column_at(layout, pointer);
    if (column < 0) {
        column = timeline_column_nearest(layout, pointer.x);
    }
    return first_day + column;
}

/**
 * The task as the drag would leave it: its dates shifted by the days from the day pressed to the
 * day under the pointer, so that a repeating task moves as a whole, as editing it does. Over the
 * hours, a task of one day also gets the time under the pointer, snapped as drawing a block is,
 * as its start; its length is then the default one, as for any item with one time. A task of
 * several days has no time to give, since an item is on the hours only within a day.
 */
static Event task_drag_result(App const *app, Layout const *layout, Event const *task,
                              int32_t press_day, int32_t first_day, Vector2 pointer) {
    assert(!event_is_timed(task));

    int32_t const day_delta = task_drag_pointer_day(layout, pointer, first_day) - press_day;
    Event moved = *task;
    moved.start_date = date_add_days(task->start_date, day_delta);
    moved.end_date = date_add_days(task->end_date, day_delta);
    bool const over_hours = timeline_column_at(layout, pointer) >= 0;
    if (over_hours && event_duration_days(task) == 0) {
        float const top = layout->timeline.y - app->timeline_scroll.offset_pixels;
        int32_t const step = timeline_step(app->timeline_fine);
        int32_t const minute = minute_snap(timeline_minute_at(pointer.y, top), step);
        // Kept in the day, with room for one step.
        moved.start_minute = int32_min(minute, MINUTES_PER_DAY - step);
    }
    return moved;
}

/**
 * Saves the task as moved, after the checks the form makes, and selects the day it now has where
 * it was pressed. Gives the reason in the status bar if it cannot, and leaves the task as it was.
 */
static void task_drag_finish(App *app, Event const *task, Event const *moved, int32_t press_day) {
    assert(task->id == moved->id);

    Str const mistake = form_parse_order(moved);
    if (mistake.length > 0) {
        snprintf(app->status, sizeof(app->status), "Not moved: %.*s", STR_ARGUMENTS(mistake));
        return;
    }
    if (!backend_update(app->backend, moved)) {
        Str const reason = backend_error(app->backend);
        snprintf(app->status, sizeof(app->status), "Not moved: %.*s", STR_ARGUMENTS(reason));
        return;
    }
    app->events_stale = true;
    int32_t const day_delta = date_to_day_number(moved->start_date) -
                              date_to_day_number(task->start_date);
    app_select(app, date_from_day_number(press_day + day_delta));
    app->event_focused_id = moved->id;
    snprintf(app->status, sizeof(app->status), "Updated \"%s\".", moved->title);
}

/**
 * A press may start marking days only in the month view with a mouse: a week or day draws on its
 * hours instead, and on a touch screen a drag is a swipe. Not over an item, which the press
 * selects, nor while a form is open, which a new form would replace, losing what was typed in it.
 */
static bool month_mark_can_start(App const *app, int64_t event_id_under_press) {
    if (app->range != RANGE_MONTH || app->touch) {
        return false;
    }
    if (event_id_under_press != 0) {
        return false;
    }
    return !app->form.open;
}

/**
 * Follows a press on a day while the button is held. The release opens the form over the days
 * from the press to the pointer, or only selects the day if it was a click: a short press that
 * stayed on its day.
 */
static void month_mark_update(App *app, Layout const *layout, int32_t first_day) {
    Month_Mark *mark = &app->month_mark;
    if (!mark->pressed) {
        return;
    }
    // The range changed under the press, by a key: the days marked are no longer in sight.
    if (app->range != RANGE_MONTH) {
        *mark = (Month_Mark){ .pressed = false };
        return;
    }
    int32_t const pointer_day = month_mark_day_at(layout, GetMousePosition(), first_day);
    if (pointer_day != mark->day_number) {
        mark->drawing = true;
    }
    if (GetTime() - mark->press_seconds >= MONTH_MARK_HOLD_SECONDS) {
        mark->drawing = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (!mark->drawing) {
            app_select(app, date_from_day_number(mark->day_number));
        } else if (!app->form.open) {
            month_mark_finish(app, pointer_day);
        }
        *mark = (Month_Mark){ .pressed = false };
        return;
    }
    // A release this frame never seen, as when the window lost the pointer, ends the press.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        *mark = (Month_Mark){ .pressed = false };
    }
}

/**
 * Opens the form for a new event over the marked days, whichever way the pointer went. The form's
 * Event and Task toggle then makes it a task instead, as for any new item.
 */
static void month_mark_finish(App *app, int32_t pointer_day) {
    Month_Mark const *mark = &app->month_mark;
    assert(mark->pressed);
    assert(mark->drawing);
    assert(!app->form.open);

    Date start = date_from_day_number(mark->day_number);
    Date end = date_from_day_number(pointer_day);
    if (pointer_day < mark->day_number) {
        start = date_from_day_number(pointer_day);
        end = date_from_day_number(mark->day_number);
    }
    // The first day, unless it is in another month than the one shown: selecting it would then
    // take away the month the user just scrolled to.
    Date selection = end;
    if (start.year == app->events_month.year && start.month == app->events_month.month) {
        selection = start;
    }
    app_select(app, selection);
    app_form_open(app, start, EVENT_KIND_EVENT);
    Form *form = &app->form;
    date_format_iso(end, form->text[FORM_FIELD_END_DATE], DATE_ISO_SIZE);
    assert(form->field_active == FORM_FIELD_TITLE);
}

/**
 * The day under the pointer, or the nearest one when the pointer has left the grid, so that a
 * drag that overshoots the edge of the calendar does not lose its days.
 */
static int32_t month_mark_day_at(Layout const *layout, Vector2 pointer, int32_t first_day) {
    assert(layout->cell_width > 0.0f);
    assert(layout->cell_height > 0.0f);

    float const column_max = (float)(layout->columns - 1);
    float const row_max = (float)(layout->rows - 1);
    float const column = (pointer.x - layout->grid.x) / layout->cell_width;
    float const row = (pointer.y - layout->grid.y) / layout->cell_height;
    int32_t const column_index = (int32_t)fminf(fmaxf(column, 0.0f), column_max);
    int32_t const row_index = (int32_t)fminf(fmaxf(row, 0.0f), row_max);
    return first_day + row_index * layout->columns + column_index;
}

/**
 * The accent over each marked day in sight. Marked days outside the six weeks shown are not
 * drawn; the release still includes them.
 */
static void draw_month_mark(App const *app, Layout const *layout, int32_t first_day) {
    Month_Mark const *mark = &app->month_mark;
    if (!mark->drawing) {
        return;
    }
    assert(mark->pressed);
    int32_t const pointer_day = month_mark_day_at(layout, GetMousePosition(), first_day);
    int32_t day_first = mark->day_number;
    int32_t day_last = pointer_day;
    if (pointer_day < mark->day_number) {
        day_first = pointer_day;
        day_last = mark->day_number;
    }
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    int32_t const cell_count = layout->rows * layout->columns;
    for (int32_t cell_index = 0; cell_index < cell_count; cell_index++) {
        int32_t const day_number = first_day + cell_index;
        if (day_number >= day_first && day_number <= day_last) {
            Rectangle const cell = grid_cell_rectangle(layout, cell_index);
            DrawRectangleRec(cell, Fade(accent, 0.35f));
            DrawRectangleLinesEx(cell, 2, accent);
        }
    }
}

/**
 * While the form for a new event or task is open, the item as it would be saved: a bar in each
 * day cell it occurs on, drawn as a saved item's bar is but see-through, so that the user sees
 * it placed but not yet saved. Read from the form each frame, as draw_timeline_ghost does. An
 * opportunity is left out, as only an event or task is placed by marking days.
 */
static void draw_month_ghost(App const *app, Layout const *layout, int32_t first_day) {
    if (app->range != RANGE_MONTH) {
        return;
    }
    Event event = { .id = 0 };
    if (!form_ghost_event(&app->form, &event)) {
        return;
    }
    if (event.kind != EVENT_KIND_EVENT && event.kind != EVENT_KIND_TASK) {
        return;
    }
    draw_grid_ghost(app, layout, first_day, &event);
}

/**
 * The ghost bar of event on every day cell in sight it occurs on.
 */
static void draw_grid_ghost(App const *app, Layout const *layout, int32_t first_day,
                            Event const *event) {
    int32_t const cell_count = layout->rows * layout->columns;
    for (int32_t cell_index = 0; cell_index < cell_count; cell_index++) {
        int32_t const day_number = first_day + cell_index;
        int32_t start_day = 0;
        if (event_occurrence_on_day(event, day_number, &start_day)) {
            draw_grid_ghost_cell(app, event, grid_cell_rectangle(layout, cell_index), day_number,
                                 start_day, cell_index % layout->columns);
        }
    }
}

/**
 * The ghost's bar in a cell, for a new item or a dragged one, in the slot a saved item would
 * take: draw_grid_cell_events puts multi-day bars first and then one-day bars, and a new item is
 * last in its group, so the slot follows the bars of its own kind, or all of them for a one-day
 * item. A dragged item's own bar is not counted, as it moves away. Its color is the accent,
 * because a saved item's color comes from its id, which it does not have yet.
 */
static void draw_grid_ghost_cell(App const *app, Event const *event, Rectangle cell,
                                 int32_t day_number, int32_t start_day, int32_t column) {
    assert(start_day <= day_number);

    float const slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    float const top = cell.y + CELL_TOP_LINE_PIXELS;
    int32_t const slot_count =
        (int32_t)((cell.y + cell.height - CELL_BOTTOM_MARGIN_PIXELS - top) / slot_height);
    if (slot_count <= 0) {
        return;
    }
    bool const multi_day = event_duration_days(event) > 0;
    int32_t slot = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *other = &app->events[i];
        bool const counted = multi_day == (event_duration_days(other) > 0) || !multi_day;
        if (counted && other->id != event->id && event_in_day_cell(other, app->range) &&
            event_occurs_on_day(other, day_number)) {
            slot++;
        }
    }
    // Over the last slot rather than past the cell when the day is full.
    if (slot > slot_count - 1) {
        slot = slot_count - 1;
    }

    bool const starts_here = day_number == start_day;
    bool const ends_here = day_number == start_day + event_duration_days(event);
    float left = cell.x;
    if (starts_here) {
        left += 4.0f;
    }
    float right = cell.x + cell.width;
    if (ends_here) {
        right -= 4.0f;
    }
    Rectangle const bar = { left, top + (float)slot * slot_height, right - left,
                            (float)EVENT_BAR_HEIGHT_PIXELS };
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    bool const task = event->kind == EVENT_KIND_TASK;
    if (task) {
        DrawRectangleRec(bar, Fade(accent, task_fill_alpha(false) * GHOST_ALPHA));
        draw_bar_outline(bar, starts_here, ends_here,
                         Fade(accent, task_outline_alpha(false) * GHOST_ALPHA));
    } else {
        DrawRectangleRec(bar, Fade(accent, GHOST_ALPHA));
    }
    // Labelled where a saved bar is: the first day and the start of each week.
    if (!starts_here && column != 0) {
        return;
    }
    char label[TEXT_LINE_SIZE] = "";
    event_format_label(event, starts_here, app_now(app), label, sizeof(label));
    float text_x = bar.x + 4;
    scissor_begin(&app->fonts, bar.x, bar.y, bar.width, bar.height);
    if (task) {
        Rectangle const box = { bar.x + 4, bar.y + 4, 11, 11 };
        draw_check_mark(box, false, Fade(accent, GHOST_ALPHA));
        text_x = box.x + box.width + 4;
    }
    Text_Style const *style = &app->fonts.small;
    draw_text(style, str_from_cstr(label), text_x,
              bar.y + ((float)EVENT_BAR_HEIGHT_PIXELS - style->size_pixels) / 2,
              Fade(style_color(TEXT_COLOR_NORMAL), GHOST_ALPHA));
    scissor_end(&app->fonts);
}

/**
 * The hours of a week or day, scrolled by the wheel, with each day's items that have a time
 * placed at their times. Clicking a day's hours selects the day, as its cell does. Dragging
 * draws a block that becomes a new event at its times; on a touch screen, where a finger has
 * no wheel, dragging scrolls instead.
 */
static void draw_timeline(App *app, Layout const *layout, int32_t first_day) {
    Rectangle const timeline = layout->timeline;
    assert(timeline.height > 0.0f);
    assert(layout->rows == 1);
    scroll_update(&app->timeline_scroll, timeline, (float)TIMELINE_HEIGHT_PIXELS, app->touch);
    // Where 00:00 is: above the timeline while it is scrolled down.
    float const top = timeline.y - app->timeline_scroll.offset_pixels;
    app->timeline_hit = timeline_hit_test(app, layout, top, first_day);
    if (!app->touch) {
        // Before the marking, so that a press on an item drags it rather than drawing a block.
        timeline_drag_update(app, layout, top);
        timeline_mark_update(app, layout, top, first_day);
    }
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
    // After the columns, whose press selects the day and so clears the focus.
    if (app->timeline_hit.event_id != 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        app_focus_item(app, app->timeline_hit.event_id);
        // A block holds one day's occurrence of a task, which starts on that day.
        if (app->timeline_hit.on_task_checkbox) {
            Event const *task = app_event_find(app, app->timeline_hit.event_id);
            assert(task != NULL);
            int32_t const start_day = app->timeline_hit.day_number;
            app_task_set_done(app, task, start_day, !app_task_done(app, task->id, start_day));
        }
    }
    timeline_cursor_update(app);
    draw_timeline_hours(app, layout, top);
    draw_timeline_ghost(app, layout, top, first_day);
    draw_timeline_drag(app, layout, top, first_day);
    // Over the items and the ghost, so that the time now is never hidden, but under the block
    // being drawn, which the user is looking at.
    draw_timeline_now(app, layout, top, first_day);
    draw_timeline_mark(app, layout, top, first_day);
    draw_timeline_mark_hover(app, layout, top);

    app->fonts.clipping = false;
    EndScissorMode();
    // After the scissor, because the circle may reach past the timeline's edges.
    if (app->timeline_mark.drawing && app->timeline_mark.zoomed) {
        draw_timeline_mark_zoom(app, layout, top, first_day);
    }
    draw_timeline_mark_hover_zoom(app, layout, top, first_day);
}

/**
 * Starts a block at a press on a day's hours, follows the pointer while the button is held, and
 * opens the form for a new event at the block's times on release. A press that does not move
 * stays a click, which selects the day as before. Not while a form is open, which a new form
 * would replace, losing what was typed in it.
 */
static void timeline_mark_update(App *app, Layout const *layout, float top, int32_t first_day) {
    Timeline_Mark *mark = &app->timeline_mark;
    Vector2 const pointer = GetMousePosition();
    int32_t const column = timeline_column_at(layout, pointer);
    // A new press always starts afresh, even over a block left pressed when its release came
    // while the timeline was not shown, as in the month view.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && column >= 0 && !app->form.open &&
        app->timeline_hit.event_id == 0) {
        assert(column < layout->columns);
        bool const zoomed = app->timeline_fine;
        *mark = (Timeline_Mark){
            .press_minute = timeline_minute_at(pointer.y, top),
            .press_y = pointer.y,
            .day_number = first_day + column,
            .pressed = true,
            // Zoomed, the magnifier shows at once, so that the start can be set precisely too.
            .drawing = zoomed,
            .zoomed = zoomed,
        };
        return;
    }
    if (!mark->pressed) {
        return;
    }
    float const moved = fabsf(pointer.y - mark->press_y);
    if (moved > (float)LIST_DRAG_THRESHOLD_PIXELS) {
        mark->drawing = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (mark->drawing && !app->form.open) {
            timeline_mark_finish(app, top);
        }
        *mark = (Timeline_Mark){ .pressed = false };
        return;
    }
    // A release this frame never seen, as when the window lost the pointer, ends the press.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        *mark = (Timeline_Mark){ .pressed = false };
    }
}

/**
 * Opens the form for a new event on the block's day at its times. The form's Event and Task
 * toggle then makes it a task instead, as for any new item.
 */
static void timeline_mark_finish(App *app, float top) {
    Timeline_Mark const *mark = &app->timeline_mark;
    assert(mark->pressed);
    assert(mark->drawing);
    assert(!app->form.open);
    float const pointer_minute = timeline_mark_pointer_minute(mark, GetMousePosition().y, top);
    int32_t const step = timeline_step(mark->zoomed);
    Minute_Range const range = timeline_mark_range(mark->press_minute, pointer_minute, step);
    Date const date = date_from_day_number(mark->day_number);
    app_select(app, date);
    app_form_open(app, date, EVENT_KIND_EVENT);
    Form *form = &app->form;
    time_format(range.minute_top, form->text[FORM_FIELD_START_TIME], TIME_TEXT_SIZE);
    time_format(range.minute_bottom, form->text[FORM_FIELD_END_TIME], TIME_TEXT_SIZE);
    assert(form->field_active == FORM_FIELD_TITLE);
}

/**
 * The minute of day the pointer stands for, not yet snapped. Unzoomed, it is the minute under
 * the pointer. Zoomed, it moves TIMELINE_MARK_ZOOM times slower than the pointer from where the
 * press was, so that the magnifier's five-minute steps are as far apart under the hand as
 * quarter hours are unzoomed; the wheel scrolling meanwhile then changes nothing.
 */
static float timeline_mark_pointer_minute(Timeline_Mark const *mark, float pointer_y, float top) {
    assert(mark->pressed);

    if (!mark->zoomed) {
        return timeline_minute_at(pointer_y, top);
    }
    float const zoomed_pixels_per_minute =
        (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f * (float)TIMELINE_MARK_ZOOM;
    float const moved_minutes = (pointer_y - mark->press_y) / zoomed_pixels_per_minute;
    return fminf(fmaxf(mark->press_minute + moved_minutes, 0.0f), (float)MINUTES_PER_DAY);
}

/**
 * The day column under the pointer, from 0, or -1 when the pointer is not over the days'
 * hours, as over the hour labels in the gutter.
 */
static int32_t timeline_column_at(Layout const *layout, Vector2 pointer) {
    if (!CheckCollisionPointRec(pointer, layout->timeline)) {
        return -1;
    }
    if (pointer.x < layout->grid.x) {
        return -1;
    }
    int32_t column = (int32_t)((pointer.x - layout->grid.x) / layout->cell_width);
    // The right border's pixel would round to the column past the last.
    if (column >= layout->columns) {
        column = layout->columns - 1;
    }
    assert(column >= 0);
    return column;
}

/**
 * The minute of day under the pointer, not yet snapped, kept within the day.
 */
static float timeline_minute_at(float pointer_y, float top) {
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    float const minute = (pointer_y - top) / pixels_per_minute;
    return fminf(fmaxf(minute, 0.0f), (float)MINUTES_PER_DAY);
}

/**
 * A press may start a block: none is being drawn, and no form is open, which a new form would
 * replace, losing what was typed in it. The hints before a press follow the same rule, so that
 * they never promise a block the press would not draw.
 */
static bool timeline_mark_can_start(App const *app) {
    if (app->timeline_mark.pressed) {
        return false;
    }
    // A press there would drag an item rather than draw.
    if (app->timeline_drag.event_id != 0 || app->timeline_hit.event_id != 0) {
        return false;
    }
    // A task being dragged to the hours is not a block being drawn.
    if (app->task_drag.event_id != 0) {
        return false;
    }
    return !app->form.open;
}

/**
 * The block between the press and the pointer, either way up, covering every step either one
 * touches, and at least one step long. A block that would end at midnight ends at 23:59
 * instead, because a time of day stops there, and the item then stays on its own day.
 */
static Minute_Range timeline_mark_range(float press_minute, float pointer_minute,
                                        int32_t step_minutes) {
    assert(press_minute >= 0.0f);
    assert(press_minute <= (float)MINUTES_PER_DAY);
    assert(pointer_minute >= 0.0f);
    assert(pointer_minute <= (float)MINUTES_PER_DAY);
    assert(step_minutes > 0);
    assert(MINUTES_PER_DAY % step_minutes == 0);

    float const step = (float)step_minutes;
    Minute_Range range = {
        .minute_top = (int32_t)floorf(fminf(press_minute, pointer_minute) / step) * step_minutes,
        .minute_bottom = (int32_t)ceilf(fmaxf(press_minute, pointer_minute) / step) * step_minutes,
    };
    // A press at the very bottom of the day starts there, with no step left below it.
    if (range.minute_top >= MINUTES_PER_DAY) {
        range.minute_top = MINUTES_PER_DAY - step_minutes;
    }
    // A click that did not move, or a drag along one step's edge, still makes one step.
    if (range.minute_bottom <= range.minute_top) {
        range.minute_bottom = range.minute_top + step_minutes;
    }
    assert(range.minute_bottom <= MINUTES_PER_DAY);
    if (range.minute_bottom == MINUTES_PER_DAY) {
        range.minute_bottom = MINUTES_PER_DAY - 1;
    }
    assert(range.minute_top >= 0);
    assert(range.minute_top < range.minute_bottom);
    assert(range.minute_bottom < MINUTES_PER_DAY);
    return range;
}

/**
 * The block being drawn, across its day's column, with its times. Not drawn while its day is out
 * of sight, as after moving to another week with the keys; the release still makes the item.
 */
static void draw_timeline_mark(App const *app, Layout const *layout, float top,
                               int32_t first_day) {
    Timeline_Mark const *mark = &app->timeline_mark;
    if (!mark->drawing) {
        return;
    }
    assert(mark->pressed);
    int32_t const column = mark->day_number - first_day;
    if (column < 0 || column >= layout->columns) {
        return;
    }
    float const pointer_minute = timeline_mark_pointer_minute(mark, GetMousePosition().y, top);
    int32_t const step = timeline_step(mark->zoomed);
    Minute_Range const range = timeline_mark_range(mark->press_minute, pointer_minute, step);
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    // Inset as items are, so that the block lines up with the items it will sit among.
    float const inset = 2.0f;
    Rectangle const block = {
        layout->grid.x + (float)column * layout->cell_width + inset,
        top + (float)range.minute_top * pixels_per_minute,
        layout->cell_width - 2 * inset,
        (float)(range.minute_bottom - range.minute_top) * pixels_per_minute,
    };
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    DrawRectangleRec(block, Fade(accent, 0.35f));
    DrawRectangleLinesEx(block, 2, accent);
    char start[TIME_TEXT_SIZE] = "";
    char end[TIME_TEXT_SIZE] = "";
    time_format(range.minute_top, start, sizeof(start));
    time_format(range.minute_bottom, end, sizeof(end));
    // A hyphen rather than a dash, which the fonts' Latin-1 glyphs do not have.
    char label[2 * TIME_TEXT_SIZE + 4] = "";
    snprintf(label, sizeof(label), "%s - %s", start, end);
    draw_text(&app->fonts.small_strong, str_from_cstr(label), block.x + 6, block.y + 4,
              style_color(TEXT_COLOR_NORMAL));
}

/**
 * A circle at the pointer showing the hours around the block's moving edge TIMELINE_MARK_ZOOM
 * times larger, with a line every five minutes, so that the edge can be put on any of them.
 */
static void draw_timeline_mark_zoom(App const *app, Layout const *layout, float top,
                                    int32_t first_day) {
    Timeline_Mark const *mark = &app->timeline_mark;
    assert(mark->zoomed);
    assert(mark->drawing);
    Vector2 const center = GetMousePosition();
    float const pointer_minute = timeline_mark_pointer_minute(mark, center.y, top);
    Minute_Range const range = timeline_mark_range(mark->press_minute, pointer_minute,
                                                   TIMELINE_MARK_ZOOM_STEP_MINUTES);
    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    DrawCircleV(center, radius, style_color(BACKGROUND_COLOR));
    draw_timeline_mark_zoom_items(app, layout, first_day, mark->day_number, center,
                                  pointer_minute);
    draw_timeline_mark_zoom_band(center, pointer_minute, range, center.x - radius,
                                 center.x + radius, Fade(accent, 0.35f));
    draw_timeline_mark_zoom_ticks(app, center, pointer_minute);
    // The edge that follows the pointer, with the time the form will get, on the side of the
    // line away from the block, so that the band does not cover it.
    bool const upwards = pointer_minute < mark->press_minute;
    int32_t edge = range.minute_bottom;
    if (upwards) {
        edge = range.minute_top;
    }
    draw_timeline_mark_zoom_edge(app, center, pointer_minute, edge, upwards);
    DrawRing(center, radius - 2, radius, 0.0f, 360.0f, 64, accent);
}

/**
 * A white line across today's column at the time now, with a dot at its left end, as calendar
 * apps mark it. Nothing when today is not in sight. The time is read by app_sync every frame,
 * and the desktop draws at least once a second while idle, so the line moves on each minute.
 */
static void draw_timeline_now(App const *app, Layout const *layout, float top,
                              int32_t first_day) {
    int32_t const column = date_to_day_number(app->today) - first_day;
    if (column < 0 || column >= layout->columns) {
        return;
    }
    assert(app->now_minute >= 0);
    assert(app->now_minute < MINUTES_PER_DAY);
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    float const y = top + (float)app->now_minute * pixels_per_minute;
    float const left = layout->grid.x + (float)column * layout->cell_width;
    // Two pixels high, centred on the minute, so that it stands out from the one-pixel hour
    // lines it may lie on.
    DrawRectangleRec((Rectangle){ left, y - 1, layout->cell_width, 2 }, WHITE);
    DrawCircleV((Vector2){ left + 1, y }, 4, WHITE);
}

/**
 * While the form for a new item is open, the item as it would be saved: a see-through block at
 * its times on every day in sight it occurs on, so that the user sees it is placed but not yet
 * saved. Read from the form each frame, so that it follows edits to its times, date and repeat,
 * and is gone once the form is saved or cancelled. Nothing while the form cannot be read yet.
 */
static void draw_timeline_ghost(App const *app, Layout const *layout, float top,
                                int32_t first_day) {
    Event event = { .id = 0 };
    if (!form_ghost_event(&app->form, &event)) {
        return;
    }
    if (!event_is_timed(&event)) {
        return; // A ghost in a week's or day's cells is not drawn: the form's dates already show.
    }
    for (int32_t column = 0; column < layout->columns; column++) {
        if (event_occurs_on_day(&event, first_day + column)) {
            Rectangle const day = { layout->grid.x + (float)column * layout->cell_width, top,
                                    layout->cell_width, (float)TIMELINE_HEIGHT_PIXELS };
            draw_timeline_ghost_block(app, &event, day);
        }
    }
}

/**
 * Fills *event with the new item the open form would save, for drawing as a ghost. False when
 * there is none: no form, an edit, a problem, or fields that cannot be read yet.
 */
static bool form_ghost_event(Form const *form, Event *event) {
    assert(event->id == 0);

    if (!form->open || form->editing_id != 0 || form->kind == EVENT_KIND_PROBLEM) {
        return false;
    }
    if (form_parse_body(form, event).length > 0) {
        return false;
    }
    Str const title = text_skip_spaces(str_from_cstr(form->text[FORM_FIELD_TITLE]));
    // Until the user types a title, the ghost says what it will be.
    Str ghost_title = title;
    if (title.length == 0) {
        ghost_title = STR("New event");
        if (event->kind == EVENT_KIND_TASK) {
            ghost_title = STR("New task");
        }
    }
    (void)str_copy_truncated(ghost_title, event->title, sizeof(event->title));
    return true;
}

/**
 * The ghost across the whole day, over the items it overlaps, which show through it; its place
 * among them in lanes is known only once it is saved.
 */
static void draw_timeline_ghost_block(App const *app, Event const *event, Rectangle day) {
    assert(event_is_timed(event));

    // The same top and length the saved item will have, including the defaults for an item
    // with only one time. The index is not used here.
    Timeline_Item const item = timeline_item_make(event, 0);
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    float const inset = 2.0f;
    Rectangle const block = {
        day.x + inset,
        day.y + (float)item.minute_top * pixels_per_minute,
        day.width - 2 * inset - 1,
        (float)(item.minute_bottom - item.minute_top) * pixels_per_minute - 1,
    };
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    DrawRectangleRec(block, Fade(accent, 0.25f));
    DrawRectangleLinesEx(block, 1, Fade(accent, 0.8f));
    char label[TEXT_LINE_SIZE] = "";
    event_format_label(event, true, app_now(app), label, sizeof(label));
    draw_text(&app->fonts.small, str_from_cstr(label), block.x + 6, block.y + 4,
              Fade(style_color(TEXT_COLOR_NORMAL), 0.8f));
}

/**
 * Before a press, a short line across the pointer where a block would start, with its time, so
 * that the press can be aimed. It snaps as the press will, through the same function: to
 * quarter hours, or to five minutes with zoom toggled on, when the magnifier shows it larger too.
 */
static void draw_timeline_mark_hover(App const *app, Layout const *layout, float top) {
    Vector2 const pointer = GetMousePosition();
    int32_t const column = timeline_column_at(layout, pointer);
    if (column < 0 || !timeline_mark_can_start(app)) {
        return;
    }
    int32_t const step = timeline_step(app->timeline_fine);
    float const minute = timeline_minute_at(pointer.y, top);
    int32_t const start = timeline_mark_range(minute, minute, step).minute_top;
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    float const y = top + (float)start * pixels_per_minute;
    // Centred on the pointer, but within its day, whose borders it does not cross.
    float const day_left = layout->grid.x + (float)column * layout->cell_width + 2;
    float const day_right = day_left + layout->cell_width - 4;
    float const left = fmaxf(pointer.x - (float)TIMELINE_MARK_HOVER_HALF_PIXELS, day_left);
    float const right = fminf(pointer.x + (float)TIMELINE_MARK_HOVER_HALF_PIXELS, day_right);
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    DrawRectangleRec((Rectangle){ left, y - 1, right - left, 2 }, accent);
    char label[TIME_TEXT_SIZE] = "";
    time_format(start, label, sizeof(label));
    // Above the line, where the block will not be, since it grows downwards from here.
    draw_text(&app->fonts.small_strong, str_from_cstr(label), left, y - 18, accent);
}

/**
 * With zoom toggled on before a press, the magnifier at the pointer, with the line where a block
 * would start, so that the start can be aimed to five minutes as precisely as the end. It also
 * shows over an item and while dragging one, which snap to the same minutes.
 */
static void draw_timeline_mark_hover_zoom(App const *app, Layout const *layout, float top,
                                          int32_t first_day) {
    if (!app->timeline_fine) {
        return;
    }
    // A block being drawn has its own magnifier.
    if (app->timeline_mark.pressed || app->form.open) {
        return;
    }
    Vector2 const center = GetMousePosition();
    if (timeline_column_at(layout, center) < 0) {
        return;
    }
    float const minute = timeline_minute_at(center.y, top);
    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    DrawCircleV(center, radius, style_color(BACKGROUND_COLOR));
    draw_timeline_mark_zoom_items(app, layout, first_day,
                                  first_day + timeline_column_at(layout, center), center,
                                  minute);
    draw_timeline_mark_zoom_ticks(app, center, minute);
    // Over an item, or dragging one, a press does not start a block, so there is no start line;
    // the magnifier still shows the minutes the item will snap to.
    if (timeline_mark_can_start(app)) {
        int32_t const start = timeline_mark_range(minute, minute, TIMELINE_MARK_ZOOM_STEP_MINUTES)
                                  .minute_top;
        draw_timeline_mark_zoom_edge(app, center, minute, start, true);
    }
    DrawRing(center, radius - 2, radius, 0.0f, 360.0f, 64, style_color(BORDER_COLOR_PRESSED));
}

/**
 * A line across the magnifier at edge_minute, with its time above or below it. The edge is at
 * most one step from the middle, so it is always inside the circle.
 */
static void draw_timeline_mark_zoom_edge(App const *app, Vector2 center, float center_minute,
                                         int32_t edge_minute, bool label_above) {
    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    float const zoomed_pixels_per_minute =
        (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f * (float)TIMELINE_MARK_ZOOM;
    float const edge_y = center.y + ((float)edge_minute - center_minute) *
                                        zoomed_pixels_per_minute;
    assert(fabsf(edge_y - center.y) < radius);
    float const half = circle_half_width(radius, edge_y - center.y);
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    DrawRectangleRec((Rectangle){ center.x - half, edge_y - 1, 2 * half, 2 }, accent);
    char label[TIME_TEXT_SIZE] = "";
    time_format(edge_minute, label, sizeof(label));
    float label_y = edge_y + 4;
    if (label_above) {
        label_y = edge_y - 20;
    }
    draw_text(&app->fonts.small_strong, str_from_cstr(label), center.x + 8, label_y,
              style_color(TEXT_COLOR_NORMAL));
}

/**
 * A line every five minutes across the circle, brighter every quarter hour, which also has its
 * time at the left, so that the scale reads at a glance.
 */
static void draw_timeline_mark_zoom_ticks(App const *app, Vector2 center, float center_minute) {
    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    float const zoomed_pixels_per_minute =
        (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f * (float)TIMELINE_MARK_ZOOM;
    // Minutes from the middle to the circle's top or bottom.
    float const reach = radius / zoomed_pixels_per_minute;
    int32_t const step = TIMELINE_MARK_ZOOM_STEP_MINUTES;
    int32_t first = (int32_t)ceilf((center_minute - reach) / (float)step) * step;
    int32_t last = (int32_t)floorf((center_minute + reach) / (float)step) * step;
    if (first < 0) {
        first = 0;
    }
    if (last > MINUTES_PER_DAY) {
        last = MINUTES_PER_DAY;
    }
    // Bounded by the circle: about 2 * reach / step + 1 lines, a dozen at the scale chosen.
    for (int32_t minute = first; minute <= last; minute += step) {
        float const y = center.y + ((float)minute - center_minute) * zoomed_pixels_per_minute;
        float const half = circle_half_width(radius, y - center.y);
        bool const quarter = minute % TIMELINE_MARK_STEP_MINUTES == 0;
        int32_t color_property = BORDER_COLOR_NORMAL;
        if (quarter) {
            color_property = BORDER_COLOR_FOCUSED;
        }
        Color const color = style_color(color_property);
        DrawRectangleRec((Rectangle){ center.x - half, y, 2 * half, 1 }, color);
        // Near the top and bottom the circle is too narrow for a label.
        if (quarter && minute < MINUTES_PER_DAY && half > radius / 2) {
            char label[TIME_TEXT_SIZE] = "";
            time_format(minute, label, sizeof(label));
            draw_text(&app->fonts.small, str_from_cstr(label), center.x - half + 8, y + 2,
                      style_color(TEXT_COLOR_DISABLED));
        }
    }
}

/**
 * The items of the day the magnifier is over, at the magnified scale and their true times, so
 * that the edges to drag can be seen. Horizontally the magnifier is not scaled and is centred
 * on the pointer, so each block is where it is on the timeline. An item being dragged stays
 * faint where it was, and where the drag would leave it, on its new day, it is drawn bright.
 */
static void draw_timeline_mark_zoom_items(App const *app, Layout const *layout,
                                          int32_t first_day, int32_t day_number,
                                          Vector2 center, float center_minute) {
    int32_t const column = day_number - first_day;
    if (column < 0 || column >= layout->columns) {
        return;
    }
    Rectangle const day = { layout->grid.x + (float)column * layout->cell_width, 0,
                            layout->cell_width, (float)TIMELINE_HEIGHT_PIXELS };
    // On the stack, as when the day is drawn; about 10 KB.
    Timeline_Item items[EVENT_COUNT_MAX] = { 0 };
    int32_t const count = timeline_day_items(app, day_number,
                                             array_slice_make(items, EVENT_COUNT_MAX));
    Timeline_Drag const *drag = &app->timeline_drag;
    bool const dragging = drag->event_id != 0 && drag->moved;
    for (int32_t i = 0; i < count; i++) {
        Event const *event = &app->events[items[i].event_index];
        float alpha = 0.55f;
        if (dragging && event->id == drag->event_id) {
            alpha = 0.2f;
        }
        draw_timeline_mark_zoom_item(app, event, event_time_range(event),
                                     timeline_item_block(day, &items[i]), center,
                                     center_minute, alpha);
    }
    if (!dragging) {
        return;
    }
    Event const *dragged = app_event_find(app, drag->event_id);
    if (dragged == NULL) {
        return;
    }
    int32_t const pointer_day = first_day + timeline_column_nearest(layout, center.x);
    Event const moved = timeline_drag_result(dragged, drag, center_minute, pointer_day,
                                             timeline_drag_step(app));
    if (drag->press_day + timeline_drag_day_delta(drag, pointer_day) != day_number) {
        return;
    }
    Timeline_Item const item = timeline_item_make(&moved, 0);
    draw_timeline_mark_zoom_item(app, &moved, event_time_range(&moved),
                                 timeline_item_block(day, &item), center, center_minute, 0.85f);
}

/**
 * One item in the magnifier: a fill from its true start to its true end, solid lines at both
 * edges, and its title, if there is room in the middle of the circle. What a press at the
 * pointer would grab is in the accent color: an edge, or for the middle the whole block.
 */
static void draw_timeline_mark_zoom_item(App const *app, Event const *event,
                                         Minute_Range range, Rectangle block, Vector2 center,
                                         float center_minute, float alpha) {
    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    float const zoomed_pixels_per_minute =
        (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f * (float)TIMELINE_MARK_ZOOM;
    float const left = block.x;
    float const right = block.x + block.width;
    // Zero length is possible for an item with equal times; one minute is the least to see.
    Minute_Range const shown = { range.minute_top,
                                 int32_max(range.minute_bottom, range.minute_top + 1) };
    Color const color = event_color(event, app_now(app));
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    Timeline_Hit const *hit = &app->timeline_hit;
    // Nothing is grabbed while an item is dragged, whose release would not be a press.
    bool const grabbed = hit->event_id == event->id && app->timeline_drag.event_id == 0;
    if (grabbed && hit->part == BLOCK_PART_MIDDLE) {
        alpha = 0.9f;
    }
    draw_timeline_mark_zoom_band(center, center_minute, shown, left, right, Fade(color, alpha));
    Color top_color = color;
    Color bottom_color = color;
    if (grabbed && hit->part == BLOCK_PART_TOP) {
        top_color = accent;
    }
    if (grabbed && hit->part == BLOCK_PART_BOTTOM) {
        bottom_color = accent;
    }
    draw_timeline_mark_zoom_band(center, center_minute,
                                 (Minute_Range){ shown.minute_top, shown.minute_top + 1 },
                                 left, right, top_color);
    draw_timeline_mark_zoom_band(center, center_minute,
                                 (Minute_Range){ shown.minute_bottom - 1, shown.minute_bottom },
                                 left, right, bottom_color);

    // The title sits near the item's top, kept in the middle of the circle where it is wide.
    float const top_y = center.y + ((float)shown.minute_top - center_minute) *
                                       zoomed_pixels_per_minute;
    float const bottom_y = center.y + ((float)shown.minute_bottom - center_minute) *
                                          zoomed_pixels_per_minute;
    float const text_height = 16;
    float const text_y = fmaxf(top_y + 4, center.y - radius * 0.6f);
    if (text_y + text_height > fminf(bottom_y, center.y + radius * 0.6f)) {
        return;
    }
    float const half = circle_half_width(radius, text_y + text_height - center.y);
    float const clip_left = fmaxf(center.x - half, left);
    float const clip_right = fminf(center.x + half, right);
    if (clip_right <= clip_left) {
        return;
    }
    scissor_begin(&app->fonts, clip_left, text_y, clip_right - clip_left, text_height);
    draw_text(&app->fonts.small, str_from_cstr(event->title), clip_left + 4, text_y, WHITE);
    scissor_end(&app->fonts);
}

/**
 * The block's part inside the circle, between the x positions left and right, one pixel row
 * at a time at the circle's width there, because raylib clips drawing to rectangles only.
 */
static void draw_timeline_mark_zoom_band(Vector2 center, float center_minute,
                                         Minute_Range range, float left, float right,
                                         Color color) {
    assert(range.minute_top < range.minute_bottom);

    float const radius = (float)TIMELINE_MARK_ZOOM_RADIUS_PIXELS;
    float const zoomed_pixels_per_minute =
        (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f * (float)TIMELINE_MARK_ZOOM;
    float const band_top = center.y + ((float)range.minute_top - center_minute) *
                                          zoomed_pixels_per_minute;
    float const band_bottom = center.y + ((float)range.minute_bottom - center_minute) *
                                             zoomed_pixels_per_minute;
    float const from = fmaxf(band_top, center.y - radius);
    float const to = fminf(band_bottom, center.y + radius);
    // At most the circle's height in rows.
    int32_t row_count = 0;
    if (to > from) {
        row_count = (int32_t)ceilf(to - from);
    }
    assert(row_count <= 2 * TIMELINE_MARK_ZOOM_RADIUS_PIXELS + 1);
    for (int32_t row = 0; row < row_count; row++) {
        float const y = from + (float)row;
        float const half = circle_half_width(radius, y + 0.5f - center.y);
        float const x_left = fmaxf(center.x - half, left);
        float const x_right = fminf(center.x + half, right);
        if (x_right > x_left) {
            DrawRectangleRec((Rectangle){ x_left, y, x_right - x_left, 1 }, color);
        }
    }
}

/**
 * Half the width of a circle at offset_y from its middle; 0 outside it.
 */
static float circle_half_width(float radius, float offset_y) {
    assert(radius > 0.0f);

    float const squared = radius * radius - offset_y * offset_y;
    if (squared > 0.0f) {
        return sqrtf(squared);
    }
    return 0.0f;
}

/**
 * The hour labels in the gutter left of the days; each day draws its own hour lines.
 */
static void draw_timeline_hours(App const *app, Layout const *layout, float top) {
    Rectangle const timeline = layout->timeline;
    float const days_left = layout->grid.x;
    assert(days_left > timeline.x);
    assert(days_left < timeline.x + timeline.width);
    Text_Style const *style = &app->fonts.small;
    for (int32_t hour = 0; hour < MINUTES_PER_DAY / 60; hour++) {
        float y = top + (float)(hour * TIMELINE_HOUR_HEIGHT_PIXELS);
        // Hours scrolled out of sight would be clipped anyway; skipping them saves the text.
        bool visible = y + TIMELINE_HOUR_HEIGHT_PIXELS > timeline.y &&
                       y < timeline.y + timeline.height;
        if (visible) {
            char label[TIME_TEXT_SIZE] = "";
            time_format(hour * 60, label, sizeof(label));
            float width = measure_text(style, str_from_cstr(label)).x;
            // Below the line rather than across it, so that 00:00 is not cut off at the top.
            draw_text(style, str_from_cstr(label), days_left - 6 - width, y + 3,
                      style_color(TEXT_COLOR_DISABLED));
        }
    }
}

/**
 * day is the column from 00:00 to 24:00, which may reach outside the timeline that clips it.
 * Items that overlap share the day's width side by side.
 */
static void draw_timeline_day(App const *app, Rectangle day, int32_t day_number, bool hovered) {
    assert(day.width > 0.0f);
    assert(day.height == (float)TIMELINE_HEIGHT_PIXELS);

    // On the stack, because it is needed only while one day is drawn; about 10 KB.
    Timeline_Item items[EVENT_COUNT_MAX] = { 0 };
    int32_t const count = timeline_day_items(app, day_number,
                                             array_slice_make(items, EVENT_COUNT_MAX));

    // The busy part of the day runs from the first item's start to the last one's end; sorted
    // by start, the first item starts first, but any item may end last. One item alone is a
    // span of nothing but itself, so it marks nothing: the highlight needs two items.
    int32_t busy_top = MINUTES_PER_DAY;
    int32_t busy_bottom = 0;
    if (count >= 2) {
        busy_top = items[0].minute_top;
        for (int32_t i = 0; i < count; i++) {
            if (items[i].minute_bottom > busy_bottom) {
                busy_bottom = items[i].minute_bottom;
            }
        }
    }
    draw_timeline_day_background(day, busy_top, busy_bottom, hovered);
    // A task's block is see-through, so it gets the day's plain background under it: the blue
    // of the busy part is behind the items, and must not tint them.
    int32_t day_property = BACKGROUND_COLOR;
    if (hovered) {
        day_property = BASE_COLOR_DISABLED;
    }
    for (int32_t i = 0; i < count; i++) {
        DrawRectangleRec(timeline_item_block(day, &items[i]), style_color(day_property));
    }
    for (int32_t i = 0; i < count; i++) {
        draw_timeline_item(app, &app->events[items[i].event_index],
                           timeline_item_block(day, &items[i]), day_number);
    }
}

/**
 * The day's timed items in drawing order, with their lanes: the one layout that both drawing
 * and finding the block under the pointer use, so that they always agree. Returns how many.
 */
static int32_t timeline_day_items(App const *app, int32_t day_number, Array_Slice items) {
    int32_t const count = timeline_items_collect(app, day_number, items);
    // Only the items collected are put in order, and given lanes.
    Array_Slice const collected = array_slice_make(items.items, count);
    timeline_items_sort(collected);
    timeline_items_assign_lanes(collected);
    return count;
}

/**
 * Where an item's block is in its day, from 00:00 at day.y.
 */
static Rectangle timeline_item_block(Rectangle day, Timeline_Item const *item) {
    assert(item->lane < item->lane_count);

    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    // Kept free at the sides of the day, so that items do not touch the day's borders.
    float const inset = 2.0f;
    float const lane_width = (day.width - 2 * inset) / (float)item->lane_count;
    // One pixel less each way, so that neighbouring blocks of one color stay apart.
    return (Rectangle){
        day.x + inset + (float)item->lane * lane_width,
        day.y + (float)item->minute_top * pixels_per_minute,
        lane_width - 1,
        (float)(item->minute_bottom - item->minute_top) * pixels_per_minute - 1,
    };
}

/**
 * The block under the pointer and which part of it, within the timeline's visible part only,
 * since blocks scrolled out of it are clipped away. Blocks are drawn in order, so the last one
 * under the pointer is the one on top.
 */
static Timeline_Hit timeline_hit_test(App const *app, Layout const *layout, float top,
                                      int32_t first_day) {
    Timeline_Hit hit = { .event_id = 0, .day_number = 0, .part = BLOCK_PART_NONE };
    Vector2 const pointer = GetMousePosition();
    int32_t const column = timeline_column_at(layout, pointer);
    if (column < 0) {
        return hit;
    }
    Rectangle const day = { layout->grid.x + (float)column * layout->cell_width, top,
                            layout->cell_width, (float)TIMELINE_HEIGHT_PIXELS };
    // On the stack, as when the day is drawn; about 10 KB.
    Timeline_Item items[EVENT_COUNT_MAX] = { 0 };
    int32_t const count = timeline_day_items(app, first_day + column,
                                             array_slice_make(items, EVENT_COUNT_MAX));
    float const grab = (float)TIMELINE_EDGE_GRAB_PIXELS;
    // With zoom on, the magnifier shows each item at its true times, so a press is tested
    // against those, and the edge zones are as wide as they look in the magnifier.
    float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
    float const grab_minutes = grab / (pixels_per_minute * (float)TIMELINE_MARK_ZOOM);
    float const pointer_minute = timeline_minute_at(pointer.y, top);
    for (int32_t i = 0; i < count; i++) {
        Event const *event = &app->events[items[i].event_index];
        Rectangle const block = timeline_item_block(day, &items[i]);
        if (app->timeline_fine) {
            Minute_Range const range = event_time_range(event);
            if (pointer.x < block.x || pointer.x >= block.x + block.width) {
                continue;
            }
            if (pointer_minute < (float)range.minute_top ||
                pointer_minute > (float)range.minute_bottom) {
                continue;
            }
            hit.part = BLOCK_PART_MIDDLE;
            if (pointer_minute < (float)range.minute_top + grab_minutes) {
                hit.part = BLOCK_PART_TOP;
            } else if (pointer_minute > (float)range.minute_bottom - grab_minutes) {
                hit.part = BLOCK_PART_BOTTOM;
            }
            hit.event_id = event->id;
            hit.day_number = first_day + column;
            hit.on_task_checkbox = false;
            continue;
        }
        if (!CheckCollisionPointRec(pointer, block)) {
            continue;
        }
        hit.event_id = event->id;
        hit.day_number = first_day + column;
        // The same area as on a day cell's bar, where draw_event_label puts the check box.
        Rectangle const checkbox_area = { block.x, block.y, 22,
                                          fminf(block.height, (float)EVENT_BAR_HEIGHT_PIXELS) };
        hit.on_task_checkbox = event->kind == EVENT_KIND_TASK &&
                               CheckCollisionPointRec(pointer, checkbox_area);
        if (pointer.y < block.y + grab) {
            hit.part = BLOCK_PART_TOP;
        } else if (pointer.y >= block.y + block.height - grab) {
            hit.part = BLOCK_PART_BOTTOM;
        } else {
            hit.part = BLOCK_PART_MIDDLE;
        }
    }
    return hit;
}

/**
 * Starts a drag at a press on an item's block and saves it at the release. A press that does
 * not move stays a click, which selects the item. Not while a form is open, which may hold the
 * item's old times and would save them back over the drag.
 */
static void timeline_drag_update(App *app, Layout const *layout, float top) {
    Timeline_Drag *drag = &app->timeline_drag;
    Timeline_Hit const *hit = &app->timeline_hit;
    Vector2 const pointer = GetMousePosition();
    // A press on a task's check box is a click, not the start of a drag.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit->event_id != 0 && !app->form.open &&
        !hit->on_task_checkbox) {
        assert(hit->part != BLOCK_PART_NONE);
        *drag = (Timeline_Drag){
            .event_id = hit->event_id,
            .press_minute = timeline_minute_at(pointer.y, top),
            .press_y = pointer.y,
            .press_day = hit->day_number,
            .part = hit->part,
            .moved = false,
        };
        return;
    }
    if (drag->event_id == 0) {
        return;
    }
    int32_t const first_day = date_to_day_number(app->visible_first);
    int32_t const pointer_day = first_day + timeline_column_nearest(layout, pointer.x);
    if (fabsf(pointer.y - drag->press_y) > (float)LIST_DRAG_THRESHOLD_PIXELS) {
        drag->moved = true;
    }
    // Only the middle moves an item to another day; an edge changes a time on its own day.
    if (drag->part == BLOCK_PART_MIDDLE && pointer_day != drag->press_day) {
        drag->moved = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (drag->moved) {
            timeline_drag_finish(app, layout, top);
        }
        *drag = (Timeline_Drag){ .event_id = 0 };
        return;
    }
    // A release this frame never seen, as when the window lost the pointer, ends the drag.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        *drag = (Timeline_Drag){ .event_id = 0 };
    }
}

/**
 * Saves the item at the drag's place, after the checks the form makes, and selects its new day.
 * A repeating item moves as a whole, as editing it does.
 */
static void timeline_drag_finish(App *app, Layout const *layout, float top) {
    Timeline_Drag const *drag = &app->timeline_drag;
    assert(drag->event_id != 0);
    assert(drag->moved);
    // Gone when a reload meanwhile no longer has it, as after a phone deleted it.
    Event const *event = app_event_find(app, drag->event_id);
    if (event == NULL) {
        return;
    }
    Vector2 const pointer = GetMousePosition();
    int32_t const pointer_day = date_to_day_number(app->visible_first) +
                                timeline_column_nearest(layout, pointer.x);
    Event const moved = timeline_drag_result(event, drag, timeline_minute_at(pointer.y, top),
                                             pointer_day, timeline_drag_step(app));
    Str const mistake = form_parse_order(&moved);
    if (mistake.length > 0) {
        snprintf(app->status, sizeof(app->status), "Not moved: %.*s", STR_ARGUMENTS(mistake));
        return;
    }
    if (!backend_update(app->backend, &moved)) {
        Str const reason = backend_error(app->backend);
        snprintf(app->status, sizeof(app->status), "Not moved: %.*s", STR_ARGUMENTS(reason));
        return;
    }
    app->events_stale = true;
    int32_t const day_delta = timeline_drag_day_delta(drag, pointer_day);
    app_select(app, date_from_day_number(drag->press_day + day_delta));
    app->event_focused_id = moved.id;
    snprintf(app->status, sizeof(app->status), "Updated \"%s\".", moved.title);
}

/**
 * The item as the drag would leave it. An edge sets that time to the step nearest the pointer,
 * keeping at least one step before the other; the middle shifts both times, keeping the length,
 * and the dates by the days moved across. A time the item lacks stays missing when it is moved,
 * but an edge dragged sets both times, as the block the user sized has both.
 */
static Event timeline_drag_result(Event const *event, Timeline_Drag const *drag,
                                  float pointer_minute, int32_t pointer_day, int32_t step) {
    assert(event_is_timed(event));
    assert(step > 0);

    Minute_Range const range = event_time_range(event);
    float const delta = pointer_minute - drag->press_minute;
    // The last minute of the day stands for midnight, as for a drawn block.
    int32_t const minute_last = MINUTES_PER_DAY - 1;
    Event result = *event;
    switch ((Block_Part)drag->part) {
    case BLOCK_PART_TOP: {
        int32_t top = minute_snap((float)range.minute_top + delta, step);
        top = int32_min(top, range.minute_bottom - step);
        result.start_minute = int32_max(top, 0);
        result.end_minute = range.minute_bottom;
        break;
    }
    case BLOCK_PART_BOTTOM: {
        int32_t bottom = minute_snap((float)range.minute_bottom + delta, step);
        bottom = int32_max(bottom, range.minute_top + step);
        result.start_minute = range.minute_top;
        result.end_minute = int32_min(bottom, minute_last);
        break;
    }
    case BLOCK_PART_MIDDLE: {
        int32_t const length = range.minute_bottom - range.minute_top;
        int32_t top = minute_snap((float)range.minute_top + delta, step);
        top = int32_min(top, minute_last - length);
        top = int32_max(top, 0);
        // A time the item lacks stays missing.
        result.start_minute = TIME_NONE;
        if (event->start_minute != TIME_NONE) {
            result.start_minute = top;
        }
        result.end_minute = TIME_NONE;
        if (event->end_minute != TIME_NONE) {
            result.end_minute = top + length;
        }
        int32_t const day_delta = pointer_day - drag->press_day;
        result.start_date = date_add_days(event->start_date, day_delta);
        result.end_date = date_add_days(event->end_date, day_delta);
        break;
    }
    case BLOCK_PART_NONE:
        assert(false); // A drag always begins on a part of a block.
        break;
    }
    assert(result.id == event->id);
    return result;
}

/**
 * As when drawing a block: quarter hours, or five minutes with zoom on.
 */
static int32_t timeline_drag_step(App const *app) {
    return timeline_step(app->timeline_fine);
}

/**
 * Minutes a block snaps to: five when fine, as with zoom on, else a quarter hour.
 */
static int32_t timeline_step(bool fine) {
    if (fine) {
        return TIMELINE_MARK_ZOOM_STEP_MINUTES;
    }
    return TIMELINE_MARK_STEP_MINUTES;
}

/**
 * Days a drag moved the item: only the middle moves it to another day, as an edge changes a
 * time on its own day.
 */
static int32_t timeline_drag_day_delta(Timeline_Drag const *drag, int32_t pointer_day) {
    if (drag->part == BLOCK_PART_MIDDLE) {
        return pointer_day - drag->press_day;
    }
    return 0;
}

/**
 * The item's real times on its day. A missing time is the other one an hour away, as the block
 * is drawn; unlike the block, a short item is not lengthened, so a drag keeps its true length.
 */
static Minute_Range event_time_range(Event const *event) {
    assert(event_is_timed(event));

    int32_t top = event->start_minute;
    int32_t bottom = event->end_minute;
    if (top == TIME_NONE) {
        top = bottom - TIMELINE_ITEM_MINUTES_DEFAULT;
    }
    if (bottom == TIME_NONE) {
        bottom = top + TIMELINE_ITEM_MINUTES_DEFAULT;
    }
    top = int32_max(top, 0);
    bottom = int32_min(bottom, MINUTES_PER_DAY - 1);
    assert(top <= bottom);
    return (Minute_Range){ .minute_top = top, .minute_bottom = bottom };
}

/**
 * The whole step nearest to minute.
 */
static int32_t minute_snap(float minute, int32_t step_minutes) {
    assert(step_minutes > 0);

    return (int32_t)roundf(minute / (float)step_minutes) * step_minutes;
}

/**
 * The day column nearest the pointer, so that a drag past the first or last day stays on it.
 */
static int32_t timeline_column_nearest(Layout const *layout, float pointer_x) {
    assert(layout->columns > 0);

    float const position = (pointer_x - layout->grid.x) / layout->cell_width;
    if (position < 0.0f) {
        return 0;
    }
    int32_t const column = (int32_t)position;
    return int32_min(column, layout->columns - 1);
}

/**
 * Arrows over an item's edges, which a drag resizes, and while dragging; a hand over the rest,
 * which a click selects. Without a drag to make, as on a touch screen or while a form is open,
 * every part is just clicked.
 */
static void timeline_cursor_update(App *app) {
    Timeline_Drag const *drag = &app->timeline_drag;
    if (drag->event_id != 0) {
        if (drag->part != BLOCK_PART_MIDDLE) {
            app->cursor = APP_CURSOR_RESIZE;
        } else {
            app->cursor = APP_CURSOR_HAND;
            if (drag->moved) {
                app->cursor = APP_CURSOR_MOVE;
            }
        }
        return;
    }
    Timeline_Hit const *hit = &app->timeline_hit;
    if (hit->event_id == 0) {
        return;
    }
    bool const draggable = !app->touch && !app->form.open;
    bool const edge = hit->part == BLOCK_PART_TOP || hit->part == BLOCK_PART_BOTTOM;
    app->cursor = APP_CURSOR_HAND;
    if (draggable && edge) {
        app->cursor = APP_CURSOR_RESIZE;
    }
}

/**
 * Where the dragged item will go: a block in its color with an accent outline and its new
 * times, in the column of its new day, over the item's old place, which stays until it is saved.
 */
static void draw_timeline_drag(App const *app, Layout const *layout, float top,
                               int32_t first_day) {
    Timeline_Drag const *drag = &app->timeline_drag;
    if (drag->event_id == 0 || !drag->moved) {
        return;
    }
    Event const *event = app_event_find(app, drag->event_id);
    if (event == NULL) {
        return;
    }
    Vector2 const pointer = GetMousePosition();
    int32_t const pointer_day = first_day + timeline_column_nearest(layout, pointer.x);
    Event const moved = timeline_drag_result(event, drag, timeline_minute_at(pointer.y, top),
                                             pointer_day, timeline_drag_step(app));
    int32_t const day_delta = timeline_drag_day_delta(drag, pointer_day);
    int32_t const column = drag->press_day + day_delta - first_day;
    if (column < 0 || column >= layout->columns) {
        return;
    }
    Rectangle const day = { layout->grid.x + (float)column * layout->cell_width, top,
                            layout->cell_width, (float)TIMELINE_HEIGHT_PIXELS };
    // The same height the saved item will be drawn at, across the whole day.
    Timeline_Item const item = timeline_item_make(&moved, 0);
    Rectangle const block = timeline_item_block(day, &item);
    DrawRectangleRec(block, Fade(event_color(event, app_now(app)), 0.75f));
    DrawRectangleLinesEx(block, 2, style_color(BORDER_COLOR_PRESSED));
    Minute_Range const range = event_time_range(&moved);
    char start[TIME_TEXT_SIZE] = "";
    char end[TIME_TEXT_SIZE] = "";
    time_format(range.minute_top, start, sizeof(start));
    time_format(range.minute_bottom, end, sizeof(end));
    char label[2 * TIME_TEXT_SIZE + 4] = "";
    snprintf(label, sizeof(label), "%s - %s", start, end);
    draw_text(&app->fonts.small_strong, str_from_cstr(label), block.x + 6, block.y + 4, WHITE);
}

/**
 * The hours between the day's first item and its last are dark blue, so that the busy part of
 * the day stands out at a glance; the free hours before and after are darker. A day with fewer
 * than two timed items has no busy part, which busy_top past busy_bottom means. A hovered day
 * is lighter in both parts, as cells are.
 */
static void draw_timeline_day_background(Rectangle day, int32_t busy_top, int32_t busy_bottom,
                                         bool hovered) {
    assert(busy_top >= 0);
    assert(busy_bottom <= MINUTES_PER_DAY);

    // The window's background is the theme's darker surface, as for days outside the month;
    // hovered, the disabled base lies between it and the normal cells.
    int32_t day_property = BACKGROUND_COLOR;
    if (hovered) {
        day_property = BASE_COLOR_DISABLED;
    }
    DrawRectangleRec(day, style_color(day_property));
    if (busy_top < busy_bottom) {
        float const pixels_per_minute = (float)TIMELINE_HOUR_HEIGHT_PIXELS / 60.0f;
        Rectangle busy = { day.x, day.y + (float)busy_top * pixels_per_minute, day.width,
                           (float)(busy_bottom - busy_top) * pixels_per_minute };
        Color busy_color = TIMELINE_BUSY_COLOR;
        if (hovered) {
            busy_color = TIMELINE_BUSY_HOVERED_COLOR;
        }
        DrawRectangleRec(busy, busy_color);
    }
    for (int32_t hour = 0; hour < MINUTES_PER_DAY / 60; hour++) {
        float y = day.y + (float)(hour * TIMELINE_HOUR_HEIGHT_PIXELS);
        DrawRectangleRec((Rectangle){ day.x, y, day.width, 1 }, style_color(BORDER_COLOR_NORMAL));
    }
}

/**
 * A block from the item's start to its end, with its time and title on the top line.
 */
static void draw_timeline_item(App const *app, Event const *event, Rectangle block,
                               int32_t day_number) {
    assert(event_duration_days(event) == 0);
    assert(block.width > 0.0f);
    assert(block.height > 0.0f);

    // A one-day item's occurrence starts on the day it is drawn on.
    bool const task = event->kind == EVENT_KIND_TASK;
    bool const done = task && app_task_done(app, event->id, day_number);
    Color const color = event_color(event, app_now(app));
    if (task) {
        DrawRectangleRec(block, Fade(color, task_fill_alpha(done)));
        DrawRectangleLinesEx(block, 1, Fade(color, task_outline_alpha(done)));
    } else {
        DrawRectangleRec(block, color);
    }

    char label[TEXT_LINE_SIZE] = "";
    event_format_label(event, true, app_now(app), label, sizeof(label));
    draw_event_label(app, event, str_from_cstr(label), block, done);
}

/**
 * The items with a time that occur on day_number, in App.events' order. Returns how many.
 */
static int32_t timeline_items_collect(App const *app, int32_t day_number, Array_Slice items) {
    assert(app->event_count <= EVENT_COUNT_MAX);
    // Room for every event, as in the worst case every one of them is on the day.
    assert(items.count >= app->event_count);

    Timeline_Item *timeline_items = items.items;
    int32_t count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *event = &app->events[i];
        if (event_is_timed(event) && event_occurs_on_day(event, day_number)) {
            timeline_items[count] = timeline_item_make(event, i);
            count++;
        }
    }
    assert(count <= app->event_count);
    return count;
}

/**
 * The minutes an item's block covers: from its start to its end time, or an hour from the one
 * time it has; at least TIMELINE_ITEM_MINUTES_MIN, and within the day.
 */
static Timeline_Item timeline_item_make(Event const *event, int32_t event_index) {
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
    top = int32_max(top, 0);
    bottom = int32_min(bottom, MINUTES_PER_DAY);
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

/**
 * Insertion sort, because a day holds few items and they mostly come in order already, by
 * start; a stable sort also keeps App.events' order for items that start together.
 */
static void timeline_items_sort(Array_Slice items) {
    assert(items.count >= 0);
    assert(items.count <= EVENT_COUNT_MAX);

    Timeline_Item *timeline_items = items.items;
    for (int32_t i = 1; i < items.count; i++) {
        Timeline_Item const item = timeline_items[i];
        int32_t j = i;
        // Bounded by i, which j counts down from.
        while (j > 0 && timeline_item_is_before(&item, &timeline_items[j - 1])) {
            timeline_items[j] = timeline_items[j - 1];
            j--;
        }
        timeline_items[j] = item;
    }
}

/**
 * Earlier starts first, and of two that start together, the longer first, so that it takes
 * the leftmost lane and the shorter one sits beside it.
 */
static bool timeline_item_is_before(Timeline_Item const *a, Timeline_Item const *b) {
    if (a->minute_top != b->minute_top) {
        return a->minute_top < b->minute_top;
    }
    return a->minute_bottom > b->minute_bottom;
}

/**
 * Gives each item, in order of start, the leftmost lane that is free when it starts. A group of
 * items that overlap, directly or through others, shares its lane count, so that the group
 * divides the day's width evenly and its lanes line up.
 */
static void timeline_items_assign_lanes(Array_Slice items) {
    assert(items.count >= 0);

    Timeline_Item *timeline_items = items.items;
    int32_t lane_bottoms[TIMELINE_LANE_COUNT_MAX] = { 0 };  // Minute each lane is free from
    int32_t lane_count = 0;
    int32_t group_first_index = 0;
    int32_t group_bottom = 0;  // Minute the group's last item ends
    for (int32_t i = 0; i < items.count; i++) {
        Timeline_Item *item = &timeline_items[i];
        assert(i == 0 || !timeline_item_is_before(item, &timeline_items[i - 1]));

        if (item->minute_top >= group_bottom) {
            // Nothing before overlaps this item, so the group before it is complete.
            timeline_items_set_lane_count(items, group_first_index, i, lane_count);
            group_first_index = i;
            lane_count = 0;
        }
        int32_t const lane = timeline_lane_find(array_slice_make(lane_bottoms, lane_count),
                                                item->minute_top);
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
    timeline_items_set_lane_count(items, group_first_index, items.count, lane_count);
}

/**
 * The leftmost of the first lane_count lanes that is free at minute_top; else a new lane,
 * lane_count, while there may be more; else the lane that frees up first.
 */
static int32_t timeline_lane_find(Array_Slice lanes, int32_t minute_top) {
    int32_t const *lane_bottoms = lanes.items;
    int32_t const lane_count = lanes.count;
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

/**
 * Items first_index up to, but not including, end_index form one group.
 */
static void timeline_items_set_lane_count(Array_Slice items, int32_t first_index,
                                          int32_t end_index, int32_t lane_count) {
    assert(first_index <= end_index);
    assert(end_index <= items.count);

    Timeline_Item *timeline_items = items.items;
    // A group of items has a lane for each; only the empty group before the first has none.
    assert((lane_count > 0) == (first_index < end_index));

    for (int32_t i = first_index; i < end_index; i++) {
        timeline_items[i].lane_count = lane_count;
    }
}

/**
 * The focused item, if it occurs on the selected day; its creation dot is ringed in the grid.
 */
static Event const *focused_event_find(App const *app) {
    Event const *event = app_event_find(app, app->event_focused_id);
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
                           Event const *focused) {
    Date date = date_from_day_number(day_number);
    // Days of the next and previous month are dimmed in the month view only; a week or day
    // shows its days alike.
    bool in_month = app->range != RANGE_MONTH || date.month == app->events_month.month;
    bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);

    int32_t fill_property = BACKGROUND_COLOR;
    if (in_month) {
        fill_property = BASE_COLOR_NORMAL;
    }
    Color fill = style_color(fill_property);
    if (hovered) {
        fill = style_color(BASE_COLOR_FOCUSED);
    }
    DrawRectangleRec(cell, fill);
    DrawRectangleLinesEx(cell, 1, style_color(BORDER_COLOR_NORMAL));

    draw_grid_cell_day_number(app, cell, date, in_month);
    draw_grid_cell_creation_markers(app, cell, day_number, focused);
    Bar_Hit const hit = draw_grid_cell_events(app, cell, day_number, column);
    if (hit.event_id != 0) {
        app->cursor = APP_CURSOR_HAND;
    }
    // A press may begin marking days instead. It selects nothing yet, because selecting a dimmed
    // day would change the month under the pointer; the release selects it if it was a click.
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        month_mark_can_start(app, hit.event_id)) {
        app->month_mark = (Month_Mark){ .press_seconds = GetTime(), .day_number = day_number,
                                        .pressed = true };
        return;
    }
    // A press selects the day, and the item under it too, whose card the panel then rings.
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        app_select(app, date);
        if (hit.event_id != 0) {
            app_focus_item(app, hit.event_id);
            task_drag_start(app, hit, day_number);
        }
        // A press on a task's check box also marks the occurrence on this bar done, or not.
        if (hit.on_task_checkbox) {
            Event const *task = app_event_find(app, hit.event_id);
            assert(task != NULL);
            app_task_set_done(app, task, hit.start_day,
                              !app_task_done(app, task->id, hit.start_day));
        }
    }
}

/**
 * Makes the item the focused one, as a click on its card would, and has the panel scroll the
 * card into sight. The item occurs on the selected day, so app_sync keeps it focused.
 */
static void app_focus_item(App *app, int64_t id) {
    assert(id > 0);
    assert(app_event_find(app, id) != NULL);

    app->event_focused_id = id;
    // A confirmation belongs to the card it was asked on.
    app->event_delete_id = 0;
    app->panel_reveal_focused = true;
}

static void draw_grid_cell_day_number(App const *app, Rectangle cell, Date date, bool in_month) {
    char number[4] = "";
    snprintf(number, sizeof(number), "%" PRId32, date.day);
    Vector2 position = { cell.x + 10, cell.y + 7 };
    int32_t color_property = TEXT_COLOR_DISABLED;
    if (in_month) {
        color_property = TEXT_COLOR_NORMAL;
    }
    Color color = style_color(color_property);

    if (date_equal(date, app->today)) {
        Vector2 size = measure_text(&app->fonts.ui, str_from_cstr(number));
        Vector2 center = { position.x + size.x / 2, position.y + size.y / 2 };
        // A solid accent circle with the number in the background color, so that today is the
        // brightest spot in the grid.
        DrawCircleV(center, size.y * 0.85f, style_color(BORDER_COLOR_PRESSED));
        color = style_color(BACKGROUND_COLOR);
    }
    draw_text(&app->fonts.ui, str_from_cstr(number), position.x, position.y, color);
}

/**
 * One dot per event created on this day, right to left from the top-right corner. The focused
 * event's dot gets a ring, so that the day it was created is easy to find.
 */
static void draw_grid_cell_creation_markers(App const *app, Rectangle cell, int32_t day_number,
                                            Event const *focused) {
    float const radius = (float)CREATION_MARKER_RADIUS_PIXELS;
    float const spacing = 2 * radius + 6;
    Vector2 center = { cell.x + cell.width - 14, cell.y + 7 + TEXT_SIZE_PIXELS / 2.0f };
    // Slots between the day number and the right edge, each holding a dot or the "+".
    int32_t slot_count = (int32_t)((cell.width - 14 - DAY_NUMBER_WIDTH_PIXELS) / spacing) + 1;
    if (slot_count < 1) {
        slot_count = 1;
    }
    int32_t created_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        created_count += (int32_t)event_created_on_day(&app->events[i], day_number);
    }
    // When not all fit, the last slot holds the "+" instead of a dot.
    int32_t dot_count_max = slot_count;
    if (created_count > slot_count) {
        dot_count_max = slot_count - 1;
    }

    int32_t marker_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *event = &app->events[i];
        if (!event_created_on_day(event, day_number)) {
            continue;
        }
        if (marker_count == dot_count_max) {
            draw_text(&app->fonts.small, STR("+"), center.x - radius,
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

/**
 * Returns the bar the pointer is over, with a zero event_id when there is none.
 */
static Bar_Hit draw_grid_cell_events(App const *app, Rectangle cell, int32_t day_number,
                                     int32_t column) {
    float const slot_height = (float)(EVENT_BAR_HEIGHT_PIXELS + EVENT_BAR_GAP_PIXELS);
    float top = cell.y + CELL_TOP_LINE_PIXELS;
    int32_t slot_count =
        (int32_t)((cell.y + cell.height - CELL_BOTTOM_MARGIN_PIXELS - top) / slot_height);
    if (slot_count <= 0) {
        return (Bar_Hit){ 0 };
    }
    Bar_Hit hit = { 0 };

    int32_t overlap_count = 0;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *event = &app->events[i];
        if (event_in_day_cell(event, app->range) && event_occurs_on_day(event, day_number)) {
            overlap_count++;
        }
    }
    // When not all fit, the last slot says how many are hidden instead of showing a bar.
    int32_t bar_count = overlap_count;
    if (overlap_count > slot_count) {
        bar_count = slot_count - 1;
    }

    // Multi-day events first, then one-day events, so that a multi-day bar keeps its row from
    // one day to the next instead of moving below whatever one-day events each day has.
    int32_t bar_index = 0;
    for (int32_t pass = 0; pass < 2; pass++) {
        bool multi_day_pass = pass == 0;
        for (int32_t i = 0; i < app->event_count && bar_index < bar_count; i++) {
            Event const *event = &app->events[i];
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
                bool on_task_checkbox = false;
                if (draw_grid_cell_event_bar(app, event, slot, day_number, start_day, column,
                                             &on_task_checkbox)) {
                    hit = (Bar_Hit){ event->id, start_day, on_task_checkbox };
                }
                bar_index++;
            }
        }
    }
    if (overlap_count > bar_count) {
        char more[TEXT_LINE_SIZE] = "";
        snprintf(more, sizeof(more), "+%" PRId32 " more", overlap_count - bar_count);
        draw_text(&app->fonts.small, str_from_cstr(more), cell.x + 6,
                  top + (float)bar_count * slot_height + 2, style_color(TEXT_COLOR_NORMAL));
    }
    return hit;
}

/**
 * start_day is the start of the occurrence that covers day_number. Returns whether the pointer
 * is over the bar, and sets *on_task_checkbox when it is over the check box of a task, which is
 * drawn only where the bar is labelled. The hit area is wider than the box, to be easy to press.
 */
static bool draw_grid_cell_event_bar(App const *app, Event const *event, Rectangle bar_slot,
                                     int32_t day_number, int32_t start_day, int32_t column,
                                     bool *on_task_checkbox) {
    assert(start_day <= day_number);

    *on_task_checkbox = false;
    bool const task = event->kind == EVENT_KIND_TASK;
    bool const done = task && app_task_done(app, event->id, start_day);
    Color const color = event_color(event, app_now(app));
    bool starts_here = day_number == start_day;
    bool ends_here = day_number == start_day + event_duration_days(event);
    // Multi-day events run edge to edge between days, so that they read as one bar.
    float left = bar_slot.x;
    if (starts_here) {
        left += 4.0f;
    }
    float right = bar_slot.x + bar_slot.width;
    if (ends_here) {
        right -= 4.0f;
    }
    Rectangle bar = { left, bar_slot.y, right - left, bar_slot.height };
    bool const hovered = CheckCollisionPointRec(GetMousePosition(), bar);
    // A problem is one day long, so it is always labelled; the problems view draws it alike.
    if (event->kind == EVENT_KIND_PROBLEM) {
        app_draw_problem_bar(app, str_from_cstr(event->title), event->tasks_done, bar);
        return hovered;
    }
    if (task) {
        DrawRectangleRec(bar, Fade(color, task_fill_alpha(done)));
        draw_bar_outline(bar, starts_here, ends_here, Fade(color, task_outline_alpha(done)));
    } else {
        DrawRectangleRec(bar, color);
    }

    // Label the first day and the start of each week, where the eye starts reading a row.
    if (!starts_here && column != 0) {
        return hovered;
    }
    if (task) {
        Rectangle const checkbox_area = { bar.x, bar.y, 22, bar.height };
        *on_task_checkbox = CheckCollisionPointRec(GetMousePosition(), checkbox_area);
    }
    char label[TEXT_LINE_SIZE] = "";
    // The time only where the occurrence starts; later days' bars continue it.
    event_format_label(event, starts_here, app_now(app), label, sizeof(label));
    draw_event_label(app, event, str_from_cstr(label), bar, done);
    return hovered;
}

/**
 * An item's label on a bar-high line at the top of area, clipped to area: after a checkbox for
 * a task, or a tick for a used opportunity, and struck through for a done task. Shared by the
 * day cells' bars and the timeline's blocks, so that an item reads the same in both.
 */
static void draw_event_label(App const *app, Event const *event, Str label, Rectangle area,
                             bool done) {
    assert(area.height > 0.0f);

    bool const task = event->kind == EVENT_KIND_TASK;
    assert(task || !done);

    Text_Style const *style = &app->fonts.small;
    float const line_height = (float)EVENT_BAR_HEIGHT_PIXELS;
    float text_x = area.x + 4;
    Color text_color = WHITE;
    scissor_begin(&app->fonts, area.x, area.y, area.width, area.height);
    if (task) {
        Rectangle box = { area.x + 4, area.y + 4, 11, 11 };
        draw_check_mark(box, done, event_color(event, app_now(app)));
        text_x = box.x + box.width + 4;
        // The theme's text colors rather than the task's own, which is too dark to read on the
        // dark cells; the outline and checkbox already carry the task's color.
        int32_t text_property = TEXT_COLOR_NORMAL;
        if (done) {
            text_property = TEXT_COLOR_DISABLED;
        }
        text_color = style_color(text_property);
    } else if (event->tasks_done) {
        // As on a done problem's bar (see app_draw_problem_bar).
        Rectangle tick = { area.x + 4, area.y + 4, 11, 11 };
        draw_tick(tick, 2.0f, WHITE);
        text_x = tick.x + tick.width + 4;
    }
    // Centered vertically in the line.
    draw_text(style, label, text_x, area.y + (line_height - style->size_pixels) / 2,
              text_color);
    if (done) {
        float middle_y = area.y + line_height / 2;
        float width = measure_text(style, label).x;
        DrawLineEx((Vector2){ text_x, middle_y }, (Vector2){ text_x + width, middle_y }, 1.5f,
                   text_color);
    }
    scissor_end(&app->fonts);
}

/**
 * A problem has no time; its "!" marks it even where its color is hard to tell apart, and a
 * done problem has a tick instead.
 */
void app_draw_problem_bar(App const *app, Str title, bool done, Rectangle bar) {
    assert(bar.width > 0.0f);
    assert(bar.height > 0.0f);

    Text_Style const *style = &app->fonts.small;
    Color fill = PROBLEM_COLOR;
    Str marker = STR("! ");
    if (done) {
        fill = TARGET_DONE_COLOR;
        marker = STR("");
    }
    DrawRectangleRec(bar, fill);
    char label[TEXT_LINE_SIZE] = "";
    snprintf(label, sizeof(label), "%.*s%.*s", STR_ARGUMENTS(marker), STR_ARGUMENTS(title));
    float text_x = bar.x + 4;
    scissor_begin(&app->fonts, bar.x, bar.y, bar.width, bar.height);
    if (done) {
        Rectangle tick = { bar.x + 4, bar.y + 4, 11, 11 };
        draw_tick(tick, 2.0f, WHITE);
        text_x = tick.x + tick.width + 4;
    }
    // Centered vertically in the bar.
    draw_text(style, str_from_cstr(label), text_x, bar.y + (bar.height - style->size_pixels) / 2,
              WHITE);
    scissor_end(&app->fonts);
}

/**
 * The label starts where it would in a day cell, so that the checkbox is where draw_event_label
 * draws it.
 */
Rectangle app_draw_task_bar(App const *app, Task_Occurrence const *occurrence, Rectangle bar) {
    assert(occurrence->task.kind == EVENT_KIND_TASK);
    assert(bar.width > 0.0f);
    assert(bar.height > 0.0f);

    Event const task = event_occurrence_at(&occurrence->task, occurrence->start_day);
    Color const color = event_color(&task, app_now(app));
    DrawRectangleRec(bar, Fade(color, task_fill_alpha(occurrence->done)));
    draw_bar_outline(bar, true, true, Fade(color, task_outline_alpha(occurrence->done)));

    // "31 Dec", then the time and title as on the bar where the occurrence starts.
    char date[TEXT_LINE_SIZE] = "";
    format_date_time_short(task.start_date, TIME_NONE, date, sizeof(date));
    char title[TEXT_LINE_SIZE] = "";
    event_format_label(&task, true, app_now(app), title, sizeof(title));
    char label[2 * TEXT_LINE_SIZE] = "";
    snprintf(label, sizeof(label), "%s %s", date, title);
    draw_event_label(app, &task, str_from_cstr(label), bar, occurrence->done);
    return (Rectangle){ bar.x + 4, bar.y + 4, 11, 11 };
}

void app_show_item(App *app, Date date, int64_t id) {
    assert(date_is_valid(date));
    assert(id > 0);

    app_select(app, date);
    // Not app_focus_item, which needs the item loaded: the date's month may not be yet. app_sync
    // keeps the focus once it is, because the item occurs on the selected date.
    app->event_focused_id = id;
    app->event_delete_id = 0;
    app->panel_reveal_focused = true;
}

/**
 * Outlines a bar without lines between the days of a multi-day occurrence.
 */
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

/**
 * A check mark filling area, drawn rather than typed, because Rubik as loaded has no glyph
 * for it (see FONT_CODEPOINT_COUNT).
 */
static void draw_tick(Rectangle area, float thickness, Color color) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);

    Vector2 start = { area.x, area.y + area.height * 0.55f };
    Vector2 bottom = { area.x + area.width * 0.38f, area.y + area.height };
    Vector2 end = { area.x + area.width, area.y };
    DrawLineEx(start, bottom, thickness, color);
    DrawLineEx(bottom, end, thickness, color);
}

/**
 * An empty box for a task that is not done, a filled one for a task that is.
 */
static void draw_check_mark(Rectangle box, bool done, Color color) {
    DrawRectangleLinesEx(box, 1.5f, color);
    if (done) {
        DrawRectangleRec((Rectangle){ box.x + 3, box.y + 3, box.width - 6, box.height - 6 },
                         color);
    }
}

static void draw_panel(App *app, Rectangle panel) {
    GuiPanel(panel, NULL);
    float const padding = (float)PADDING_PIXELS;
    Rectangle area = { panel.x + padding, panel.y + padding, panel.width - 2 * padding,
                       panel.height - 2 * padding };

    draw_text(&app->fonts.ui, weekday_name(date_weekday(app->selected)), area.x, area.y,
              style_color(TEXT_COLOR_DISABLED));
    char heading[TEXT_LINE_SIZE] = "";
    Str const selected_month = month_name(app->selected.month);
    snprintf(heading, sizeof(heading), "%" PRId32 " %.*s %" PRId32, app->selected.day,
             STR_ARGUMENTS(selected_month), app->selected.year);
    draw_text(&app->fonts.heading, str_from_cstr(heading), area.x, area.y + TEXT_LINE_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));

    float const heading_height = 72;
    Rectangle body = { area.x, area.y + heading_height, area.width,
                       area.height - heading_height };
    if (app->form.open) {
        draw_panel_form(app, body);
    } else {
        draw_panel_events(app, body);
    }
}

/**
 * Two rows of two buttons, because "New opportunity" does not fit in a quarter of the panel.
 */
static void draw_panel_events(App *app, Rectangle body) {
    float const gap = 8;
    float const button_width = (body.width - gap) / 2;
    float const right_x = body.x + button_width + gap;
    float const second_y = body.y + BUTTON_HEIGHT_PIXELS + gap;
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
        draw_text(&app->fonts.ui, STR("Nothing on this day."), body.x, list_top,
                  style_color(TEXT_COLOR_DISABLED));
    } else if (list.height > 0.0f) {
        if (app->panel_reveal_focused) {
            draw_panel_events_reveal(app, list);
        }
        scroll_update(&app->panel_scroll, list, content_height, true);
        draw_panel_events_list(app, list);
        draw_scrollbar(list, content_height, app->panel_scroll.offset_pixels);
    } else {
        // A window too short for any list shows only the buttons and the legend.
    }
    draw_panel_legend(&app->fonts, (Rectangle){ body.x, body.y + body.height -
                                                LEGEND_HEIGHT_PIXELS, body.width,
                                                LEGEND_HEIGHT_PIXELS });
}

/**
 * Height of the selected day's cards stacked with their gaps, or 0 when the day has none.
 */
static float draw_panel_events_height(App const *app, float card_width) {
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

/**
 * Scrolls the list by the wheel, or by dragging it with a finger or the mouse, and keeps the
 * offset within the content. Callers take a release as a click only while the press began in
 * the list and did not become a drag.
 * With drag_scrolls off, dragging is left to the caller and only the wheel scrolls.
 */
static void scroll_update(Scroll *scroll, Rectangle list, float content_height,
                          bool drag_scrolls) {
    assert(list.height > 0.0f);
    assert(content_height > 0.0f);

    Vector2 const mouse = GetMousePosition();
    bool const over = CheckCollisionPointRec(mouse, list);
    // A press is forgotten one frame after its release, so that on the release frame the rows
    // still know whether it was a drag, and do not take it as a click.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        scroll->pressed = false;
        scroll->dragging = false;
    }
    if (drag_scrolls && over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
    float offset_max = 0.0f;
    if (content_height > list.height) {
        offset_max = content_height - list.height;
    }
    if (scroll->offset_pixels > offset_max) {
        scroll->offset_pixels = offset_max;
    }
    if (scroll->offset_pixels < 0.0f) {
        scroll->offset_pixels = 0.0f;
    }

    assert(scroll->offset_pixels >= 0.0f);
    assert(scroll->offset_pixels <= offset_max);
}

/**
 * Draws the selected day's cards, moved up by the scroll and clipped to the list. Controls are
 * locked while the list is dragged, and while the pointer is outside the list, so that neither
 * the end of a drag nor a click on a clipped part of a card acts on it.
 */
static void draw_panel_events_list(App *app, Rectangle list) {
    assert(list.height > 0.0f);

    Scroll const *scroll = &app->panel_scroll;
    assert(scroll->offset_pixels >= 0.0f);

    bool const locked = scroll->dragging || !CheckCollisionPointRec(GetMousePosition(), list);
    // Locked already when the app has no input; that lock stays until app_frame lifts it.
    bool const locked_before = GuiIsLocked();
    if (locked) {
        GuiLock();
    }
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);

    int32_t selected_day = date_to_day_number(app->selected);
    float y = list.y - scroll->offset_pixels;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *event = &app->events[i];
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
        if (!locked && CheckCollisionPointRec(GetMousePosition(), card)) {
            app->cursor = APP_CURSOR_HAND;
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

/**
 * Scrolls the list just enough that the focused card is in sight, laid out as
 * draw_panel_events_list lays the cards out; scroll_update clamps the result afterwards. A card
 * taller than the list shows from its top.
 */
static void draw_panel_events_reveal(App *app, Rectangle list) {
    assert(app->panel_reveal_focused);

    app->panel_reveal_focused = false;
    int32_t const selected_day = date_to_day_number(app->selected);
    float card_top = 0.0f;
    for (int32_t i = 0; i < app->event_count; i++) {
        Event const *event = &app->events[i];
        int32_t start_day = 0;
        if (!event_occurrence_on_day(event, selected_day, &start_day)) {
            continue;
        }
        float const card_height = event_card_height(&app->fonts, event,
                                                    event->id == app->event_delete_id,
                                                    list.width);
        if (event->id == app->event_focused_id) {
            Scroll *scroll = &app->panel_scroll;
            if (card_top + card_height > scroll->offset_pixels + list.height) {
                scroll->offset_pixels = card_top + card_height - list.height;
            }
            if (card_top < scroll->offset_pixels) {
                scroll->offset_pixels = card_top;
            }
            return;
        }
        card_top += card_height + EVENT_CARD_GAP_PIXELS;
    }
}

/**
 * A thin bar right of a list: its length is the visible share of the content, and its place is
 * where the visible part lies within it.
 */
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

/**
 * Returns true when the card itself was clicked, not one of its buttons. Deleting asks first, on
 * the card, because it is permanent and also removes every repeat of a repeating event.
 * A task's checkbox marks the occurrence on this card, which starts on event->start_date.
 */
static bool draw_panel_event_card(App *app, Event const *event, Rectangle card) {
    float const x = card.x + 16;
    Rectangle const delete_button = { card.x + card.width - 82, card.y + 6, 72, 24 };
    Rectangle const checkbox = { x, card.y + 8, 16, 16 };
    Rectangle const confirm_button = { x, card.y + card.height - 40, 110, 32 };
    Rectangle const cancel_button = { x + 122, confirm_button.y, 110, 32 };
    bool const task = event->kind == EVENT_KIND_TASK;
    int32_t const start_day = date_to_day_number(event->start_date);
    bool const done = task && app_task_done(app, event->id, start_day);
    bool const confirming = event->id == app->event_delete_id;
    bool const focused = event->id == app->event_focused_id;
    Vector2 const mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, card);

    draw_panel_event_card_frame(app, event, card, hovered, focused, confirming);
    draw_panel_event_card_title(app, event, card, done);

    if (confirming) {
        Str const question = event_delete_question(event);
        draw_text(&app->fonts.small, question, x, card.y + 8 + TEXT_LINE_HEIGHT_PIXELS_SMALL,
                  ERROR_COLOR);
        if (GuiButton(confirm_button, "Delete")) {
            app_event_delete(app, event->id);
        }
        if (GuiButton(cancel_button, "Cancel")) {
            app->event_delete_id = 0;
        }
        // Only the buttons act while confirming, so that a stray click cannot change focus.
        return false;
    }

    Rich_Text_Link const link = draw_panel_event_card_details(&app->fonts, event, app_now(app),
                                                              card);
    bool const on_link = link.start != STR_NOT_FOUND;
    // As for the card itself: the list locks its controls while it is dragged, or while the
    // pointer is outside it, and only a press that began in it counts.
    if (on_link && hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !GuiIsLocked() &&
        app->panel_scroll.pressed) {
        app_open_link(app, str_slice(str_from_cstr(event->description), link.start, link.end));
        return false;
    }
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
                     (task && CheckCollisionPointRec(mouse, checkbox)) || on_link;
    // On release rather than press, because the press may still become a drag of the list.
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !on_button;
}

/**
 * The card's background, its stripe in the item's color, and its border, which is thicker while
 * the card is focused or asks about deleting.
 */
static void draw_panel_event_card_frame(App const *app, Event const *event, Rectangle card,
                                        bool hovered, bool focused, bool confirming) {
    int32_t card_property = BASE_COLOR_NORMAL;
    if (hovered) {
        card_property = BASE_COLOR_FOCUSED;
    }
    DrawRectangleRec(card, style_color(card_property));
    DrawRectangleRec((Rectangle){ card.x, card.y, 6, card.height },
                     event_color(event, app_now(app)));
    float border_pixels = 1.0f;
    if (focused || confirming) {
        border_pixels = 2.0f;
    }
    int32_t border_property = BORDER_COLOR_NORMAL;
    if (focused) {
        border_property = BORDER_COLOR_PRESSED;
    }
    DrawRectangleLinesEx(card, border_pixels, style_color(border_property));
}

/**
 * The title, clipped short of the delete button. A done task's title is dimmed and struck out,
 * and a missed opportunity's dimmed; a done target's has a tick before it, in the green of its
 * stripe.
 */
static void draw_panel_event_card_title(App const *app, Event const *event, Rectangle card,
                                        bool done) {
    Text_Style const *title_style = &app->fonts.small_strong;
    float const x = card.x + 16;
    bool const dimmed = done || event_is_missed(event, app_now(app));
    // Room for the checkbox of a task, or the tick of a used opportunity, before the title.
    float title_x = x;
    if (event->kind == EVENT_KIND_TASK || event->tasks_done) {
        title_x = x + 24;
    }
    int32_t title_property = TEXT_COLOR_NORMAL;
    if (dimmed) {
        title_property = TEXT_COLOR_DISABLED;
    }
    Color const title_color = style_color(title_property);
    scissor_begin(&app->fonts, card.x, card.y, card.width - 90, card.height);
    if (event->tasks_done) {
        draw_tick((Rectangle){ x + 1, card.y + 9, 14, 13 }, 2.5f, TARGET_DONE_COLOR);
    }
    draw_text(title_style, str_from_cstr(event->title), title_x, card.y + 8, title_color);
    if (done) {
        float width = measure_text(title_style, str_from_cstr(event->title)).x;
        float middle_y = card.y + 8 + title_style->size_pixels / 2 + 1;
        DrawLineEx((Vector2){ title_x, middle_y }, (Vector2){ title_x + width, middle_y }, 1.5f,
                   title_color);
    }
    scissor_end(&app->fonts);
}

/**
 * The lines below the title: dates and repeat, location, description, how many images it has,
 * repeat end, and creation, with how long ago that was. A problem shows only its kind,
 * description and creation. Returns the link of the description under the pointer, or one with
 * start STR_NOT_FOUND.
 */
static Rich_Text_Link draw_panel_event_card_details(Fonts const *fonts, Event const *event,
                                                    Date_Time now, Rectangle card) {
    float const x = card.x + 16;
    float const line = (float)TEXT_LINE_HEIGHT_PIXELS_SMALL;
    Text_Style const *style = &fonts->small;
    char text[TEXT_LINE_SIZE] = "";
    scissor_begin(fonts, card.x, card.y, card.width - 4, card.height);
    float y = card.y + 8 + line;
    event_format_range(event, now, text, sizeof(text));
    draw_text(style, str_from_cstr(text), x, y, style_color(TEXT_COLOR_NORMAL));
    if (event->location.id != 0) {
        y += line;
        event_format_location(event, text, sizeof(text));
        draw_text(style, str_from_cstr(text), x, y, style_color(TEXT_COLOR_NORMAL));
    }
    Str const description = str_from_cstr(event->description);
    Rich_Text_Link hovered_link = { .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
    if (description.length > 0) {
        Text_Lines lines = text_wrap(style, description,
                                     card.width - (float)CARD_TEXT_MARGIN_PIXELS);
        size_t shown_end = 0;
        if (lines.count > 0) {
            shown_end = (size_t)(lines.start[lines.count - 1] + lines.length[lines.count - 1]);
        }
        Rich_Text_Link links[CARD_LINK_COUNT_MAX];
        int32_t const link_count = links_collect(description, shown_end, links,
                                                 CARD_LINK_COUNT_MAX);
        Array_Slice const link_slice = array_slice_make(links, link_count);
        Vector2 const mouse = GetMousePosition();
        for (int32_t i = 0; i < lines.count; i++) {
            y += line;
            size_t const start = (size_t)lines.start[i];
            size_t const end = start + (size_t)lines.length[i];
            draw_text_links(style, description, start, end, link_slice, x, y,
                            style_color(TEXT_COLOR_NORMAL), style_color(BORDER_COLOR_PRESSED));
            // The last line says that there is more, as the card cannot show all of it.
            if (lines.cut && i == lines.count - 1) {
                float const shown_width = text_width(style, str_slice(description, start, end));
                draw_text(style, STR("..."), x + shown_width, y, style_color(TEXT_COLOR_NORMAL));
            }
            if (CheckCollisionPointRec(mouse, card) && mouse.y >= y && mouse.y < y + line) {
                int32_t const index = text_index_at_x(style, description, start, end,
                                                      mouse.x - x);
                if (index >= 0) {
                    hovered_link = links_find_at(link_slice, (size_t)index);
                }
            }
        }
    }
    int32_t const image_count = rich_text_image_count(description);
    if (image_count > 0) {
        y += line;
        description_format_image_count(image_count, text, sizeof(text));
        draw_text(style, str_from_cstr(text), x, y, style_color(TEXT_COLOR_DISABLED));
    }
    if (event->repeat_has_end) {
        y += line;
        Date end = event->repeat_end_date;
        Str const end_month = month_short_name(end.month);
        snprintf(text, sizeof(text), "Repeats until %" PRId32 " %.*s %" PRId32, end.day,
                 STR_ARGUMENTS(end_month), end.year);
        draw_text(style, str_from_cstr(text), x, y, style_color(TEXT_COLOR_NORMAL));
    }
    y += line;
    event_format_created(event, now, text, sizeof(text));
    draw_text(style, str_from_cstr(text), x, y, style_color(TEXT_COLOR_DISABLED));
    scissor_end(fonts);
    return hovered_link;
}

/**
 * Explains the grid's creation dots.
 */
static void draw_panel_legend(Fonts const *fonts, Rectangle area) {
    Color const sample = EVENT_COLORS[0];
    Color const dim = style_color(TEXT_COLOR_DISABLED);
    float const text_x = area.x + 26;
    DrawCircleV((Vector2){ area.x + 8, area.y + 8 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text(&fonts->small, STR("Day it was created"), text_x, area.y, dim);

    float second_y = area.y + TEXT_LINE_HEIGHT_PIXELS_SMALL + 4;
    DrawCircleV((Vector2){ area.x + 8, second_y + 8 }, CREATION_MARKER_RADIUS_PIXELS + 3,
                style_color(BORDER_COLOR_PRESSED));
    DrawCircleV((Vector2){ area.x + 8, second_y + 8 }, CREATION_MARKER_RADIUS_PIXELS, sample);
    draw_text(&fonts->small, STR("Day the selected item was created"), text_x, second_y, dim);
}

static void draw_panel_form(App *app, Rectangle body) {
    Form *form = &app->form;
    if (form->description.open) {
        draw_panel_description(app, body);
        return;
    }
    if (form->attachments.open) {
        draw_panel_attachments(app, body);
        return;
    }
    // While the picker is open, it is on top: the controls under it are locked, so that a
    // press meant for it does not reach them. It reads the pointer itself, below.
    bool const locked_before = GuiIsLocked();
    bool const picker_locks = form->picker_open && !locked_before;
    if (picker_locks) {
        GuiLock();
    }
    static char const *const FORM_HEADINGS[EVENT_KIND_COUNT] = {
        "Edit event", "Edit task", "Edit problem", "Edit opportunity",
    };
    bool const editing = form->editing_id != 0;
    // A new entry has no heading: the kind selector in its place says what it is.
    if (editing) {
        draw_text(&app->fonts.ui, str_from_cstr(FORM_HEADINGS[form->kind]), body.x, body.y,
                  style_color(TEXT_COLOR_NORMAL));
    }
    // An existing link target keeps its kind: a problem must stay on the day it was created,
    // and the tasks linked to an opportunity would mean nothing to an event.
    if (!editing || !event_kind_is_target(form->kind)) {
        draw_panel_form_kinds(app, body, editing);
    }

    // The first field's label sits below the heading and its box below the label.
    float const fields_y = body.y + 32 + FORM_LABEL_HEIGHT_PIXELS;
    float y = draw_panel_form_fields(app, body, fields_y);
    // An error takes the hint's line, because both are about the fields and the error is what
    // needs reading then; sharing the line leaves a task's checklist more room.
    if (form->error[0] != '\0') {
        scissor_begin(&app->fonts, body.x, y, body.width, TEXT_LINE_HEIGHT_PIXELS);
        draw_text(&app->fonts.ui, str_from_cstr(form->error), body.x, y, ERROR_COLOR);
        scissor_end(&app->fonts);
    } else if (form->kind != EVENT_KIND_PROBLEM) {
        draw_text(&app->fonts.small, STR("Dates YYYY-MM-DD, times HH:MM (optional)"), body.x, y,
                  style_color(TEXT_COLOR_DISABLED));
    }
    y += TEXT_LINE_HEIGHT_PIXELS + 4;

    // The buttons sit at the bottom of the panel, so that they stay in place whatever the kind,
    // and a task's checklist of targets, or an opportunity's tasks, fill the room between the
    // fields and them.
    float const buttons_y = body.y + body.height - BUTTON_HEIGHT_PIXELS;
    if (form->kind == EVENT_KIND_TASK) {
        draw_panel_form_targets(app, body, y + FORM_LABEL_HEIGHT_PIXELS,
                                buttons_y - FORM_ROW_GAP_PIXELS);
    }
    if (form->kind == EVENT_KIND_OPPORTUNITY) {
        draw_panel_form_target_tasks(app, body, y + FORM_LABEL_HEIGHT_PIXELS,
                                     buttons_y - FORM_ROW_GAP_PIXELS);
    }
    draw_panel_form_buttons(app, body, buttons_y);
    if (picker_locks) {
        GuiUnlock();
    }
    // Last, so that it covers the fields and buttons it overlaps.
    if (form->picker_open) {
        draw_panel_form_picker(app, body);
    }
}

/**
 * The kind selector: a new entry can be any kind, from the left where the heading would be; an
 * existing event or task can switch between the two, right of its heading. Each toggle is as
 * wide as its label, because "Opportunity" needs more room than an even share leaves it.
 */
static void draw_panel_form_kinds(App *app, Rectangle body, bool editing) {
    static char const *const FORM_KIND_NAMES[EVENT_KIND_COUNT] = { "Event", "Task", "Problem",
                                                         "Opportunity" };
    static_assert(EVENT_KIND_EVENT == 0, "Event and task are the first kinds.");
    static_assert(EVENT_KIND_TASK == 1, "Event and task are the first kinds.");

    Form *form = &app->form;
    assert(!editing || !event_kind_is_target(form->kind));

    // An existing item can switch between two kinds only; a new one has them all.
    int32_t count = EVENT_KIND_COUNT;
    if (editing) {
        count = 2;
    }
    float widths[EVENT_KIND_COUNT] = { 0 };
    float total_width = (float)((count - 1) * KIND_TOGGLE_GAP_PIXELS);
    for (int32_t kind = 0; kind < count; kind++) {
        widths[kind] = measure_text(&app->fonts.ui, str_from_cstr(FORM_KIND_NAMES[kind])).x
                       + KIND_TOGGLE_PADDING_PIXELS;
        total_width += widths[kind];
    }
    float x = body.x;
    if (editing) {
        x = body.x + body.width - total_width;
    }
    for (int32_t kind = 0; kind < count; kind++) {
        bool active = form->kind == kind;
        GuiToggle((Rectangle){ x, body.y - 4, widths[kind], 28 }, FORM_KIND_NAMES[kind], &active);
        // A click on the active toggle turns it off in raygui; one kind is always chosen, so
        // only turning another one on counts.
        if (active && form->kind != kind) {
            form->kind = kind;
        }
        x += widths[kind] + KIND_TOGGLE_GAP_PIXELS;
    }
}

/**
 * Cancel and Save, and for an existing item Delete at the right of both, in red. Delete closes
 * the form and leaves the question to the item's card, which asks first, because deleting is
 * permanent and also removes every repeat.
 */
static void draw_panel_form_buttons(App *app, Rectangle body, float y) {
    float const gap = 12;
    bool const editing = app->form.editing_id != 0;
    int32_t button_count = 2;
    if (editing) {
        button_count = 3;
    }
    float const button_width = (body.width - (float)(button_count - 1) * gap) / (float)button_count;
    if (GuiButton((Rectangle){ body.x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Cancel")) {
        form_close(&app->form);
        return;
    }
    float const save_x = body.x + button_width + gap;
    if (GuiButton((Rectangle){ save_x, y, button_width, BUTTON_HEIGHT_PIXELS }, "Save")) {
        app_form_submit(app);
        return;
    }
    if (!editing) {
        return;
    }
    Rectangle const delete_button = { save_x + button_width + gap, y, button_width,
                                      BUTTON_HEIGHT_PIXELS };
    // Red for this button only; the styles are global in raygui, so put them back after.
    static int32_t const properties[] = { BASE_COLOR_NORMAL, BASE_COLOR_FOCUSED,
                                          BASE_COLOR_PRESSED, BORDER_COLOR_NORMAL,
                                          BORDER_COLOR_FOCUSED, BORDER_COLOR_PRESSED,
                                          TEXT_COLOR_NORMAL, TEXT_COLOR_FOCUSED,
                                          TEXT_COLOR_PRESSED };
    enum { PROPERTY_COUNT = sizeof(properties) / sizeof(properties[0]) };
    Color const reds[PROPERTY_COUNT] = {
        { 185, 40, 40, 255 }, { 210, 55, 55, 255 }, { 150, 30, 30, 255 },
        { 120, 20, 20, 255 }, { 120, 20, 20, 255 }, { 120, 20, 20, 255 },
        { 255, 255, 255, 255 }, { 255, 255, 255, 255 }, { 255, 255, 255, 255 },
    };
    int32_t saved[PROPERTY_COUNT] = { 0 };
    for (int32_t i = 0; i < PROPERTY_COUNT; i++) {
        saved[i] = GuiGetStyle(BUTTON, properties[i]);
        GuiSetStyle(BUTTON, properties[i], (int32_t)ColorToInt(reds[i]));
    }
    bool const pressed = GuiButton(delete_button, "Delete");
    for (int32_t i = 0; i < PROPERTY_COUNT; i++) {
        GuiSetStyle(BUTTON, properties[i], saved[i]);
    }
    if (pressed) {
        int64_t const id = app->form.editing_id;
        form_close(&app->form);
        app->event_delete_id = id;
        app->event_focused_id = id;
        app->panel_reveal_focused = true;
    }
}

/**
 * Where the rows of fields end, below the last box, with the first box at y. Rows are kept for
 * hidden fields, so that what follows them does not move as the repeat choice changes.
 */
static float draw_panel_form_rows_end(float y) {
    float const row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    return y + (FORM_ROW_COUNT - 1) * row_height + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
}

/**
 * Draws the rows of fields, the first box at y. Returns the y just below the last row.
 */
static float draw_panel_form_fields(App *app, Rectangle body, float y) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    float const row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    float const gap = 12;
    float const time_width = 96;
    float const date_width = body.width - time_width - gap;
    float const time_x = body.x + date_width + gap;
    float const city_width = 136;
    float const address_width = body.width - city_width - gap;
    float const city_x = body.x + address_width + gap;
    float const end_y = draw_panel_form_rows_end(y);
    if (form->kind == EVENT_KIND_PROBLEM) {
        draw_panel_form_fields_problem(app, body, y, end_y);
        return end_y;
    }

    draw_panel_form_field(fonts, form, FORM_FIELD_TITLE, STR("Title"),
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_date_field(fonts, form, app->selected, FORM_FIELD_START_DATE,
                               STR("Start date"),
                               (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_START_TIME, STR("Time"),
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    // An opportunity's end is the last moment it can be used.
    bool const opportunity = form->kind == EVENT_KIND_OPPORTUNITY;
    Str end_label = STR("End date");
    if (opportunity) {
        end_label = STR("Deadline");
    }
    draw_panel_form_date_field(fonts, form, app->selected, FORM_FIELD_END_DATE, end_label,
                               (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_END_TIME, STR("Time"),
                          (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_field(fonts, form, FORM_FIELD_LOCATION_ADDRESS, STR("Address (optional)"),
                          (Rectangle){ body.x, y, address_width, FIELD_HEIGHT_PIXELS });
    draw_panel_form_field(fonts, form, FORM_FIELD_LOCATION_CITY, STR("City"),
                          (Rectangle){ city_x, y, city_width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_description_row(
        app, (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    // An opportunity cannot repeat, so its rows end here, and its tasks get the repeat rows'
    // room below.
    if (opportunity) {
        return y - FORM_LABEL_HEIGHT_PIXELS;
    }
    draw_text(&fonts->ui, STR("Repeat"), body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    GuiComboBox((Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS }, REPEAT_OPTION_LABELS,
                &form->repeat_option);
    if (form_field_visible(form, FORM_FIELD_REPEAT_DAYS)) {
        draw_panel_form_field(fonts, form, FORM_FIELD_REPEAT_DAYS, STR("Days"),
                              (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    }
    // The row is kept while hidden, so that the buttons do not move under the pointer.
    y += row_height;
    if (form_field_visible(form, FORM_FIELD_REPEAT_END_DATE)) {
        draw_panel_form_date_field(fonts, form, app->selected, FORM_FIELD_REPEAT_END_DATE,
                                   STR("Repeat until (optional)"),
                                   (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
    }
    // Never both: a task that repeats has no deadline, and one that does not has no repeat end.
    if (form_field_visible(form, FORM_FIELD_DEADLINE_DATE)) {
        assert(!form_field_visible(form, FORM_FIELD_REPEAT_END_DATE));
        draw_panel_form_date_field(fonts, form, app->selected, FORM_FIELD_DEADLINE_DATE,
                                   STR("Deadline (optional)"),
                                   (Rectangle){ body.x, y, date_width, FIELD_HEIGHT_PIXELS });
        draw_panel_form_field(fonts, form, FORM_FIELD_DEADLINE_TIME, STR("Time"),
                              (Rectangle){ time_x, y, time_width, FIELD_HEIGHT_PIXELS });
    }
    assert(y + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS == end_y);
    return end_y;
}

/**
 * A problem has a name and a description; its day is shown, not chosen. Below them are its
 * status and the tasks that solve it.
 */
static void draw_panel_form_fields_problem(App *app, Rectangle body, float y, float end_y) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    float const row_height = FORM_LABEL_HEIGHT_PIXELS + FIELD_HEIGHT_PIXELS + FORM_ROW_GAP_PIXELS;
    draw_panel_form_field(fonts, form, FORM_FIELD_TITLE, STR("Name"),
                          (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;
    draw_panel_form_description_row(
        app, (Rectangle){ body.x, y, body.width, FIELD_HEIGHT_PIXELS });
    y += row_height;

    // A new problem is recorded today; an existing one stays on the day it was created, which
    // form_open_edit put in the start date text.
    Date date = app->today;
    if (form->editing_id != 0) {
        bool parsed = date_parse_iso(str_from_cstr(form->text[FORM_FIELD_START_DATE]), &date);
        assert(parsed);
        (void)parsed; // Unused when assertions are compiled out.
    }
    char text[TEXT_LINE_SIZE] = "";
    Str const date_weekday_name = weekday_name(date_weekday(date));
    Str const date_month_name = month_name(date.month);
    snprintf(text, sizeof(text), "On %.*s, %" PRId32 " %.*s %" PRId32,
             STR_ARGUMENTS(date_weekday_name), date.day, STR_ARGUMENTS(date_month_name),
             date.year);
    draw_text(&fonts->ui, str_from_cstr(text), body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    draw_text(&fonts->small, STR("A problem is always on the day it is created."), body.x, y + 4,
              style_color(TEXT_COLOR_DISABLED));
    draw_panel_form_target_tasks(app, body, y + row_height, end_y);
}

/**
 * The status on the label line above y, then the linked tasks as cards like the calendar's,
 * down to end_y, scrolling when they do not fit. A task's check box marks it done, and the arrow
 * at the right of its card goes to the day it starts on.
 */
static void draw_panel_form_target_tasks(App *app, Rectangle body, float y, float end_y) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    assert(form->task_count <= LINK_COUNT_MAX);
    assert(y < end_y);
    Color const normal = style_color(TEXT_COLOR_NORMAL);
    Color const dim = style_color(TEXT_COLOR_DISABLED);
    if (!form->links_loaded) {
        draw_text(&fonts->ui, STR("Status unknown"), body.x, y - FORM_LABEL_HEIGHT_PIXELS, normal);
        return;
    }
    draw_panel_form_target_tasks_status(app, body, y);
    if (form->task_count == 0) {
        draw_text(&fonts->small, STR("Link tasks to it in a task's form."), body.x, y + 4, dim);
        return;
    }

    // A window too short for any card shows only the status.
    Rectangle list = { body.x, y + 4, body.width, end_y - y - 4 };
    if (list.height < 1.0f) {
        return;
    }
    float content_height = form_target_tasks_height(fonts, form, list.width);
    if (content_height > list.height) {
        // The cards narrow to make room for the scroll bar, which can make them taller.
        list.width -= SCROLLBAR_GAP_PIXELS;
        content_height = form_target_tasks_height(fonts, form, list.width);
    }
    scroll_update(&form->targets_scroll, list, content_height, true);
    draw_panel_form_target_tasks_list(app, list);
    draw_scrollbar(list, content_height, form->targets_scroll.offset_pixels);
}

/**
 * Height of the linked tasks' cards stacked with their gaps, and a line after them that says
 * when more tasks are linked than were loaded.
 */
static float form_target_tasks_height(Fonts const *fonts, Form const *form, float card_width) {
    assert(card_width > 0.0f);
    assert(form->task_count > 0);
    assert(form->task_count <= LINK_COUNT_MAX);

    float height = 0.0f;
    for (int32_t i = 0; i < form->task_count; i++) {
        if (i > 0) {
            height += EVENT_CARD_GAP_PIXELS;
        }
        height += event_card_height(fonts, &form->tasks[i].task, false, card_width);
    }
    if (form->links_truncated) {
        height += EVENT_CARD_GAP_PIXELS + TEXT_LINE_HEIGHT_PIXELS_SMALL;
    }
    return height;
}

/**
 * Draws the cards moved up by the scroll and clipped to the list. Controls are locked while the
 * list is dragged, and while the pointer is outside the list, as in draw_panel_events_list. What
 * a card's controls ask for is done after the cards, because it reloads or closes the form.
 */
static void draw_panel_form_target_tasks_list(App *app, Rectangle list) {
    Form *form = &app->form;
    assert(list.height > 0.0f);

    Scroll const *scroll = &form->targets_scroll;
    bool const locked = scroll->dragging || !CheckCollisionPointRec(GetMousePosition(), list);
    bool const locked_before = GuiIsLocked();
    if (locked) {
        GuiLock();
    }
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);

    int32_t toggled_index = -1;
    int32_t opened_index = -1;
    float y = list.y - scroll->offset_pixels;
    for (int32_t i = 0; i < form->task_count; i++) {
        Linked_Task const *task = &form->tasks[i];
        float const card_height = event_card_height(&app->fonts, &task->task, false, list.width);
        Rectangle const card = { list.x, y, list.width, card_height };
        y += card_height + EVENT_CARD_GAP_PIXELS;
        // Cards scrolled wholly out of the list can be neither seen nor clicked.
        if (!CheckCollisionRecs(card, list)) {
            continue;
        }
        bool toggled = false;
        bool opened = false;
        draw_panel_form_target_task_card(app, task, card, &toggled, &opened);
        if (toggled) {
            toggled_index = i;
        }
        if (opened) {
            opened_index = i;
        }
    }
    if (form->links_truncated) {
        draw_text(&app->fonts.small, STR("More tasks are linked than are shown."), list.x, y,
                  style_color(TEXT_COLOR_DISABLED));
    }

    app->fonts.clipping = false;
    EndScissorMode();
    if (locked && !locked_before) {
        GuiUnlock();
    }
    if (opened_index >= 0) {
        app_form_goto_task(app, &form->tasks[opened_index].task);
    } else if (toggled_index >= 0) {
        app_form_task_set_done(app, &form->tasks[toggled_index]);
    }
}

/**
 * One task as the calendar's card shows it: a stripe in its color, a check box, its title and
 * the lines below it, with an arrow at the right. A change of the check box sets *toggled, a
 * click on the arrow sets *opened.
 */
static void draw_panel_form_target_task_card(App *app, Linked_Task const *task, Rectangle card,
                                             bool *toggled, bool *opened) {
    Rectangle const checkbox = { card.x + 16, card.y + 8, 16, 16 };
    Rectangle const arrow = { card.x + card.width - 40, card.y + 6, 32, 24 };
    Vector2 const mouse = GetMousePosition();
    bool const hovered = CheckCollisionPointRec(mouse, card);

    draw_panel_event_card_frame(app, &task->task, card, hovered, false, false);
    draw_panel_event_card_title(app, &task->task, card, task->done);
    draw_panel_event_card_details(&app->fonts, &task->task, app_now(app), card);
    bool checked = task->done;
    GuiCheckBox(checkbox, NULL, &checked);
    *toggled = checked != task->done;

    *opened = GuiButton(arrow, "");
    if (CheckCollisionPointRec(mouse, arrow)) {
        app->cursor = APP_CURSOR_HAND;
    }
    // An arrow pointing left, drawn because the font may lack a glyph for one.
    Color const color = style_color(TEXT_COLOR_NORMAL);
    Vector2 const middle = { arrow.x + arrow.width / 2, arrow.y + arrow.height / 2 };
    DrawLineEx((Vector2){ middle.x - 8, middle.y }, (Vector2){ middle.x + 8, middle.y }, 2.0f,
               color);
    DrawLineEx((Vector2){ middle.x - 8, middle.y }, (Vector2){ middle.x - 2, middle.y - 6 },
               2.0f, color);
    DrawLineEx((Vector2){ middle.x - 8, middle.y }, (Vector2){ middle.x - 2, middle.y + 6 },
               2.0f, color);
}

/**
 * Checks off a task of the open target, or unchecks it, and shows the result: the form's list
 * is loaded again, and so are the calendar's events, whose target may now be done.
 */
static void app_form_task_set_done(App *app, Linked_Task const *task) {
    Form *form = &app->form;
    assert(form->open);
    assert(task->task.id > 0);

    bool const done = !task->done;
    Date const occurrence_start = date_from_day_number(task->occurrence_start_day);
    if (!backend_set_task_done(app->backend, task->task.id, occurrence_start, done)) {
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    form->error[0] = '\0';
    app->events_stale = true;
    char const *outcome = "Not done:";
    if (done) {
        outcome = "Done:";
    }
    snprintf(app->status, sizeof(app->status), "%s \"%s\".", outcome, task->task.title);
    app_form_load_links(app);
}

/**
 * Closes the form and shows the day the task starts on, with the task focused in the list. What
 * was typed in the form and not saved is lost, as when it is closed any other way.
 */
static void app_form_goto_task(App *app, Event const *task) {
    assert(task->kind == EVENT_KIND_TASK);

    // Copied first, because task points into the form, whose list is loaded again when it opens.
    Event const copy = *task;
    form_close(&app->form);
    app->event_delete_id = 0;
    app_select(app, copy.start_date);
    app->event_focused_id = copy.id;
    app->panel_reveal_focused = true;
}

/**
 * The target's status on the label line above y, with how many of its tasks are done while some
 * are still to do; in the error color for an opportunity whose deadline has passed.
 */
static void draw_panel_form_target_tasks_status(App const *app, Rectangle body, float y) {
    Form const *form = &app->form;
    int32_t done_count = 0;
    for (int32_t i = 0; i < form->task_count; i++) {
        done_count += (int32_t)form->tasks[i].done;
    }
    // With more tasks than were loaded, the counts are of the loaded ones; that many tasks for
    // one target is far beyond any real use (see LINK_COUNT_MAX).
    Target_Status status = target_status(form->task_count, done_count);
    // An opportunity's deadline as the form has it now, so that changing it shows at once.
    bool const missed = form->kind == EVENT_KIND_OPPORTUNITY &&
                        form_deadline_is_missed(form, app_now(app),
                                                status == TARGET_STATUS_DONE);
    Str name = target_status_name(status);
    if (missed) {
        name = STR("Missed");
    }
    char text[TEXT_LINE_SIZE] = "";
    snprintf(text, sizeof(text), "Status: %.*s", STR_ARGUMENTS(name));
    if (status == TARGET_STATUS_PLANNED || status == TARGET_STATUS_PARTLY_DONE) {
        snprintf(text, sizeof(text), "Status: %.*s, %" PRId32 " of %" PRId32 " tasks done",
                 STR_ARGUMENTS(name), done_count, form->task_count);
    }
    Color status_color = style_color(TEXT_COLOR_NORMAL);
    if (missed) {
        status_color = ERROR_COLOR;
    }
    draw_text(&app->fonts.ui, str_from_cstr(text), body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              status_color);
}

/**
 * The problems a task can solve and the opportunities it can help use, each with a check box,
 * below the task's fields: the heading on the label line above y, the rows from y down to
 * end_y. Checking one links the task to it when the form is saved. Each target's status is as
 * stored, without the choices not saved yet. Rows scroll when they do not fit, which at the
 * smallest window is after about one.
 */
static void draw_panel_form_targets(App *app, Rectangle body, float y, float end_y) {
    Form *form = &app->form;
    char heading[TEXT_LINE_SIZE] = "";
    char const *newest_only = "";
    if (form->links_truncated) {
        newest_only = ", newest only";
    }
    snprintf(heading, sizeof(heading), "Problems and opportunities (%" PRId32 ")%s",
             form_targets_linked_count(form), newest_only);
    scissor_begin(&app->fonts, body.x, y - FORM_LABEL_HEIGHT_PIXELS, body.width,
                  FORM_LABEL_HEIGHT_PIXELS);
    draw_text(&app->fonts.ui, str_from_cstr(heading), body.x, y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    scissor_end(&app->fonts);
    if (!form->links_loaded || form->target_count == 0) {
        char const *text = "Problems and opportunities could not be loaded.";
        if (form->links_loaded) {
            text = "No problems or opportunities yet.";
        }
        draw_text(&app->fonts.small, str_from_cstr(text), body.x, y + 4,
                  style_color(TEXT_COLOR_DISABLED));
        return;
    }
    // A window too short for any row shows only the heading; it is never that short at the
    // minimum window size, but a phone's height is not ours to choose.
    if (end_y - y < 1.0f) {
        return;
    }

    Rectangle list = { body.x, y, body.width, end_y - y };
    float const content_height = (float)form->target_count * TARGET_ROW_HEIGHT_PIXELS;
    if (content_height > list.height) {
        list.width -= SCROLLBAR_GAP_PIXELS;
    }
    scroll_update(&form->targets_scroll, list, content_height, true);
    Scroll const *scroll = &form->targets_scroll;
    // A release opens nothing unless the press began here and stayed a tap.
    bool const clickable = scroll->pressed && !scroll->dragging &&
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

/**
 * Returns true when the row was tapped or clicked, which toggles its check box. The box has the
 * target's color, so that problems and opportunities tell apart as on the calendar.
 */
static bool draw_panel_form_targets_row(App const *app, Link_Target const *target,
                                        Rectangle row, bool clickable) {
    assert(event_kind_is_target(target->kind));

    Fonts const *fonts = &app->fonts;
    bool const hovered = CheckCollisionPointRec(GetMousePosition(), row);
    if (hovered) {
        DrawRectangleRec(row, style_color(BASE_COLOR_FOCUSED));
    }
    DrawRectangleRec((Rectangle){ row.x, row.y + row.height - 1, row.width, 1 },
                     style_color(BORDER_COLOR_NORMAL));
    Target_Status const status = target_status(target->task_count, target->task_done_count);
    bool const done = status == TARGET_STATUS_DONE;
    bool const missed = target->kind == EVENT_KIND_OPPORTUNITY &&
                        opportunity_is_missed(target->end_date, target->end_minute, done,
                                              app_now(app));
    Color color = OPPORTUNITY_COLOR;
    if (missed) {
        color = OPPORTUNITY_MISSED_COLOR;
    }
    if (target->kind == EVENT_KIND_PROBLEM) {
        color = PROBLEM_COLOR;
    }
    float const box_size = 18;
    draw_check_mark((Rectangle){ row.x + 6, row.y + (row.height - box_size) / 2, box_size,
                                 box_size },
                    target->linked, color);

    float const title_x = row.x + 34;
    float const status_x = row.x + row.width - TARGET_STATUS_WIDTH_PIXELS;
    float const text_y = row.y + (row.height - TEXT_LINE_HEIGHT_PIXELS) / 2;
    scissor_begin(fonts, title_x, row.y, status_x - title_x - 8, row.height);
    draw_text(&fonts->ui, str_from_cstr(target->title), title_x, text_y,
              style_color(TEXT_COLOR_NORMAL));
    scissor_end(fonts);
    Str status_text = target_status_name(status);
    if (missed) {
        status_text = STR("Missed");
    }
    draw_text(&fonts->small, status_text, status_x, text_y + 3,
              style_color(TEXT_COLOR_DISABLED));
    return clickable && hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static int32_t form_targets_linked_count(Form const *form) {
    assert(form->target_count >= 0);
    assert(form->target_count <= LINK_COUNT_MAX);

    int32_t count = 0;
    for (int32_t i = 0; i < form->target_count; i++) {
        count += (int32_t)form->targets[i].linked;
    }
    return count;
}

/**
 * Whether the deadline typed in the form has passed. A deadline not typed correctly yet is not
 * missed, because the form says what to fix when it is saved.
 */
static bool form_deadline_is_missed(Form const *form, Date_Time now, bool tasks_done) {
    assert(form->kind == EVENT_KIND_OPPORTUNITY);

    Date end_date = { 0, 0, 0 };
    if (!date_parse_iso(str_from_cstr(form->text[FORM_FIELD_END_DATE]), &end_date)) {
        return false;
    }
    int32_t end_minute = TIME_NONE;
    if (!form_parse_time(str_from_cstr(form->text[FORM_FIELD_END_TIME]), &end_minute)) {
        return false;
    }
    return opportunity_is_missed(end_date, end_minute, tasks_done, now);
}

static void draw_panel_form_field(Fonts const *fonts, Form *form, int32_t field, Str label,
                                  Rectangle box) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);
    assert(field != FORM_FIELD_DESCRIPTION); // See draw_panel_form_description_field.

    draw_text(&fonts->ui, label, box.x, box.y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    bool editing = form->field_active == field;
    // raygui returns true when edit mode should toggle: a click on the box, or Enter or a
    // click elsewhere while editing.
    if (GuiTextBox(box, form->text[field], FORM_FIELD_SIZES[field], editing)) {
        if (editing) {
            form->field_active = FORM_FIELD_NONE;
        } else {
            form->field_active = field;
        }
    }
}

/**
 * A date field: the text box, narrowed to make room for a button at its right end that opens
 * the date picker. Typing stays as it was. fallback is the month the picker shows when the text
 * is not a date yet.
 */
static void draw_panel_form_date_field(Fonts const *fonts, Form *form, Date fallback,
                                       int32_t field, Str label, Rectangle box) {
    float const text_width = box.width - PICKER_BUTTON_WIDTH_PIXELS - PICKER_BUTTON_GAP_PIXELS;
    draw_panel_form_field(fonts, form, field, label,
                          (Rectangle){ box.x, box.y, text_width, box.height });
    Rectangle const button = { box.x + box.width - PICKER_BUTTON_WIDTH_PIXELS, box.y,
                               PICKER_BUTTON_WIDTH_PIXELS, box.height };
    // Locked while the picker is open, so a press on it is the picker's to read.
    if (GuiButton(button, "")) {
        form_picker_open(form, field, fallback);
    }
    // A downward arrow, drawn because the font may lack a glyph for one.
    Vector2 const middle = { button.x + button.width / 2, button.y + button.height / 2 };
    DrawTriangle((Vector2){ middle.x - 5, middle.y - 3 }, (Vector2){ middle.x, middle.y + 4 },
                 (Vector2){ middle.x + 5, middle.y - 3 }, style_color(TEXT_COLOR_NORMAL));
    if (form->picker_open && form->picker_field == field) {
        form->picker_anchor = box;
        form->picker_button = button;
    }
}

/**
 * Opens the picker for the field, at the month of the date typed in it, or else at fallback's.
 * The year is kept off the ends of the calendar's range, so that the weeks around the month
 * are always valid dates.
 */
static void form_picker_open(Form *form, int32_t field, Date fallback) {
    assert(field >= 0);
    assert(field < FORM_FIELD_COUNT);

    Date date = fallback;
    Date typed = { 0, 0, 0 };
    if (date_parse_iso(str_from_cstr(form->text[field]), &typed)) {
        date = typed;
    }
    if (date.year <= DATE_YEAR_MIN) {
        date = (Date){ DATE_YEAR_MIN + 1, 1, 1 };
    }
    if (date.year >= DATE_YEAR_MAX) {
        date = (Date){ DATE_YEAR_MAX - 1, 12, 1 };
    }
    form->picker_open = true;
    form->picker_field = field;
    form->picker_month = (Date){ date.year, date.month, 1 };
    // The picker is the one thing being edited now.
    form->field_active = FORM_FIELD_NONE;
}

/**
 * The date picker: a mini calendar of one month under the field it sets, with arrows to other
 * months. A click on a day, even one of the neighbouring months shown to fill the weeks, writes
 * it into the field as typed text and closes the picker; a click anywhere else closes it. Drawn
 * and read with the pointer directly, over controls that draw_panel_form has locked.
 */
static void draw_panel_form_picker(App *app, Rectangle body) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    assert(form->picker_open);
    assert(form->picker_field >= 0);
    assert(form->picker_field < FORM_FIELD_COUNT);

    // Under the field, flipped above it when the panel ends first, and within the panel's width.
    float const width = (float)PICKER_WIDTH_PIXELS;
    float const height = (float)PICKER_HEIGHT_PIXELS;
    Rectangle const anchor = form->picker_anchor;
    float x = fminf(anchor.x, body.x + body.width - width);
    x = fmaxf(x, body.x);
    float y = anchor.y + anchor.height + 2;
    if (y + height > body.y + body.height) {
        y = fmaxf(anchor.y - height - 2, body.y);
    }
    Rectangle const popup = { x, y, width, height };
    Vector2 const mouse = GetMousePosition();
    bool const pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangleRec(popup, style_color(BASE_COLOR_NORMAL));
    DrawRectangleLinesEx(popup, 1, style_color(BORDER_COLOR_FOCUSED));

    draw_panel_form_picker_header(app, popup, pressed);
    bool chosen = false;
    draw_panel_form_picker_cells(fonts, form, popup, app->today, pressed, &chosen);
    // A day chosen closes the picker. So does a press anywhere outside it, the field's own
    // button included, which the controls under it never see because they are locked.
    if (chosen || (pressed && !CheckCollisionPointRec(mouse, popup))) {
        form->picker_open = false;
    }
}

/**
 * The picker's header: previous month, the month's name and year, next month. A press on an
 * arrow changes the month shown, until the end of the calendar's range.
 */
static void draw_panel_form_picker_header(App *app, Rectangle popup, bool pressed) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    assert(form->picker_open);
    assert(popup.width > 0.0f);

    Vector2 const mouse = GetMousePosition();
    float const pad = (float)PICKER_PADDING_PIXELS;
    float const arrow_width = 32;
    float const arrow_height = PICKER_HEADER_HEIGHT_PIXELS - pad;
    Rectangle const previous = { popup.x + pad, popup.y + pad, arrow_width, arrow_height };
    Rectangle const next = { popup.x + popup.width - pad - arrow_width, popup.y + pad,
                             arrow_width, arrow_height };
    // Months stop one year short of the calendar's ends, as form_picker_open keeps them.
    bool const previous_ok = form->picker_month.year > DATE_YEAR_MIN + 1 ||
                             form->picker_month.month > 1;
    bool const next_ok = form->picker_month.year < DATE_YEAR_MAX - 1 ||
                         form->picker_month.month < 12;
    if (previous_ok && CheckCollisionPointRec(mouse, previous)) {
        DrawRectangleRec(previous, style_color(BASE_COLOR_FOCUSED));
        app->cursor = APP_CURSOR_HAND;
        if (pressed) {
            form->picker_month = date_add_months(form->picker_month, -1);
        }
    }
    if (next_ok && CheckCollisionPointRec(mouse, next)) {
        DrawRectangleRec(next, style_color(BASE_COLOR_FOCUSED));
        app->cursor = APP_CURSOR_HAND;
        if (pressed) {
            form->picker_month = date_add_months(form->picker_month, 1);
        }
    }
    int32_t arrow_color = TEXT_COLOR_NORMAL;
    if (!previous_ok) {
        arrow_color = TEXT_COLOR_DISABLED;
    }
    draw_text_centered(&fonts->ui, STR("<"), previous, style_color(arrow_color));
    arrow_color = TEXT_COLOR_NORMAL;
    if (!next_ok) {
        arrow_color = TEXT_COLOR_DISABLED;
    }
    draw_text_centered(&fonts->ui, STR(">"), next, style_color(arrow_color));
    char title[TEXT_LINE_SIZE] = "";
    Str const month = month_name(form->picker_month.month);
    snprintf(title, sizeof(title), "%.*s %" PRId32, STR_ARGUMENTS(month),
             form->picker_month.year);
    draw_text_centered(&fonts->ui, str_from_cstr(title),
                       (Rectangle){ previous.x + arrow_width, previous.y,
                                    next.x - previous.x - arrow_width, previous.height },
                       style_color(TEXT_COLOR_NORMAL));
}

/**
 * The picker's weekday names and six weeks of days from the Monday on or before the month's
 * first. A day outside the month is dimmer, the date in the field has the accent fill and today
 * an outline. A press on a day writes it into the field and sets *chosen.
 */
static void draw_panel_form_picker_cells(Fonts const *fonts, Form *form, Rectangle popup,
                                         Date today, bool pressed, bool *chosen) {
    float const pad = (float)PICKER_PADDING_PIXELS;
    Vector2 const mouse = GetMousePosition();
    float const names_y = popup.y + PICKER_HEADER_HEIGHT_PIXELS + pad;
    for (int32_t weekday = 0; weekday < 7; weekday++) {
        Rectangle const name = { popup.x + pad + (float)weekday * PICKER_CELL_WIDTH_PIXELS,
                                 names_y, PICKER_CELL_WIDTH_PIXELS, PICKER_WEEKDAY_HEIGHT_PIXELS };
        draw_text_centered(&fonts->small, weekday_short_name(weekday), name,
                           style_color(TEXT_COLOR_DISABLED));
    }

    Date selected = { 0, 0, 0 };
    bool const has_selected = date_parse_iso(
        str_from_cstr(form->text[form->picker_field]), &selected);
    Date const first = grid_first_date(form->picker_month);
    float const cells_y = names_y + PICKER_WEEKDAY_HEIGHT_PIXELS;
    for (int32_t i = 0; i < 42; i++) {
        Date const date = date_add_days(first, i);
        Rectangle const cell = {
            popup.x + pad + (float)(i % 7) * PICKER_CELL_WIDTH_PIXELS,
            cells_y + (float)(i / 7) * PICKER_CELL_HEIGHT_PIXELS,
            PICKER_CELL_WIDTH_PIXELS, PICKER_CELL_HEIGHT_PIXELS,
        };
        bool const hovered = CheckCollisionPointRec(mouse, cell);
        bool const is_selected = has_selected && date_equal(date, selected);
        if (is_selected) {
            DrawRectangleRec(cell, style_color(BASE_COLOR_PRESSED));
        } else if (hovered) {
            DrawRectangleRec(cell, style_color(BASE_COLOR_FOCUSED));
        }
        if (date_equal(date, today)) {
            DrawRectangleLinesEx(cell, 1, style_color(BORDER_COLOR_PRESSED));
        }
        int32_t text_property = TEXT_COLOR_NORMAL;
        if (date.month != form->picker_month.month) {
            text_property = TEXT_COLOR_DISABLED;
        }
        if (is_selected) {
            text_property = TEXT_COLOR_PRESSED;
        }
        char number[8] = "";
        snprintf(number, sizeof(number), "%" PRId32, date.day);
        draw_text_centered(&fonts->ui, str_from_cstr(number), cell, style_color(text_property));
        if (hovered && pressed) {
            date_format_iso(date, form->text[form->picker_field], DATE_ISO_SIZE);
            *chosen = true;
        }
    }
}

static void draw_status(App *app, Rectangle status) {
    char message[STATUS_SIZE] = "";
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
    char text[INFO_SIZE + STATUS_SIZE + 8] = "";
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

    // The textures of the last form's images were unloaded when it closed.
    assert(form->description.image_count == 0);
    // Cleared in place rather than assigned a compound literal: the form is large, and a
    // compiler may build the literal on the stack first.
    memset(form, 0, sizeof(*form));
    form->field_active = FORM_FIELD_TITLE;
    form->repeat_option = REPEAT_OPTION_NONE;
    form->kind = kind;
    form->open = true;
    form->description.images_loaded = true;
    form->attachments.loaded = true;
    form->description.layout_stale = true;
    form->description.goal_x_pixels = -1.0f;
    date_format_iso(date, form->text[FORM_FIELD_START_DATE], DATE_ISO_SIZE);
    date_format_iso(date, form->text[FORM_FIELD_END_DATE], DATE_ISO_SIZE);
    // A starting value, so that choosing "Every X days" shows what to type.
    snprintf(form->text[FORM_FIELD_REPEAT_DAYS], REPEAT_DAYS_TEXT_SIZE, "2");
}

/**
 * Fills the form from a stored event. Saving then updates that event instead of adding one.
 */
static void form_open_edit(Form *form, Event const *event) {
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
        if (REPEAT_OPTION_UNITS[option] == event->repeat_unit) {
            form->repeat_option = option;
        }
    }
    assert(REPEAT_OPTION_UNITS[form->repeat_option] == event->repeat_unit);
    if (event->repeat_unit == REPEAT_DAY) {
        snprintf(form->text[FORM_FIELD_REPEAT_DAYS], REPEAT_DAYS_TEXT_SIZE, "%" PRId32,
                 event->repeat_interval);
    }
    if (event->repeat_has_end) {
        date_format_iso(event->repeat_end_date, form->text[FORM_FIELD_REPEAT_END_DATE],
                        DATE_ISO_SIZE);
    }
    if (event->has_deadline) {
        date_format_iso(event->deadline_date, form->text[FORM_FIELD_DEADLINE_DATE],
                        DATE_ISO_SIZE);
        if (event->deadline_minute != TIME_NONE) {
            time_format(event->deadline_minute, form->text[FORM_FIELD_DEADLINE_TIME],
                        TIME_TEXT_SIZE);
        }
    }
    snprintf(form->text[FORM_FIELD_LOCATION_ADDRESS], LOCATION_ADDRESS_SIZE, "%s",
             event->location.address);
    snprintf(form->text[FORM_FIELD_LOCATION_CITY], LOCATION_CITY_SIZE, "%s",
             event->location.city);
    description_set_text(&form->description, str_from_cstr(event->description));
    // Start with no field in edit mode, so that a click on Save or Cancel acts at once.
    form->field_active = FORM_FIELD_NONE;
}

static void form_close(Form *form) {
    assert(form->open);

    form->open = false;
    form->picker_open = false;
    form->description.open = false;
    form->description.selecting = false;
    form->attachments.open = false;
    description_clear_images(&form->description);
    form->field_active = FORM_FIELD_NONE;
}

/**
 * The field after the active one for Tab, skipping the days field while it is hidden.
 */
static int32_t form_field_next(Form const *form, bool backwards) {
    assert(form->field_active >= 0);
    assert(form->field_active < FORM_FIELD_COUNT);

    // One field back is a step all the way round forwards, which keeps the remainder positive.
    int32_t step = 1;
    if (backwards) {
        step = FORM_FIELD_COUNT - 1;
    }
    int32_t field = (form->field_active + step) % FORM_FIELD_COUNT;
    // Bounded: the title field is always visible, so at most FORM_FIELD_COUNT steps are taken.
    // The description is skipped: it is edited on its own page, which a click opens.
    for (int32_t i = 0; i < FORM_FIELD_COUNT &&
                        (!form_field_visible(form, field) || field == FORM_FIELD_DESCRIPTION);
         i++) {
        field = (field + step) % FORM_FIELD_COUNT;
    }
    assert(form_field_visible(form, field));
    assert(field != FORM_FIELD_DESCRIPTION);
    return field;
}

static bool form_field_visible(Form const *form, int32_t field) {
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
    if (field == FORM_FIELD_DEADLINE_DATE || field == FORM_FIELD_DEADLINE_TIME) {
        return form->kind == EVENT_KIND_TASK && form_repeat_option(form) == REPEAT_OPTION_NONE;
    }
    return true;
}

/**
 * The repeat chosen, or none for an opportunity, which cannot repeat; the choice stays in the
 * form, hidden, in case the kind is switched back.
 */
static int32_t form_repeat_option(Form const *form) {
    assert(form->repeat_option >= 0);
    assert(form->repeat_option < REPEAT_OPTION_COUNT);

    if (form->kind == EVENT_KIND_OPPORTUNITY) {
        return REPEAT_OPTION_NONE;
    }
    return form->repeat_option;
}

/**
 * Fills event from the form. Returns an empty Str on success, or a message saying what to fix.
 */
static Str form_parse(Form const *form, Date today, Event *event) {
    assert(event->id == 0);

    // Leading spaces would let a blank-looking title through.
    Str const title = text_skip_spaces(str_from_cstr(form->text[FORM_FIELD_TITLE]));
    if (title.length == 0) {
        if (form->kind == EVENT_KIND_PROBLEM) {
            return STR("Enter a name.");
        }
        return STR("Enter a title.");
    }
    (void)str_copy_truncated(title, event->title, sizeof(event->title));
    if (form->kind == EVENT_KIND_PROBLEM) {
        return form_parse_problem(form, today, event);
    }
    return form_parse_body(form, event);
}

/**
 * Everything of an event, task or opportunity but its title: dates, times, location,
 * description and repeat. Apart from form_parse, so that the timeline can show an unsaved item
 * at its times before it has a title, read exactly as saving will read it.
 */
static Str form_parse_body(Form const *form, Event *event) {
    assert(form->kind != EVENT_KIND_PROBLEM);

    if (!date_parse_iso(str_from_cstr(form->text[FORM_FIELD_START_DATE]), &event->start_date)) {
        return STR("Start date must be YYYY-MM-DD.");
    }
    if (!form_parse_time(str_from_cstr(form->text[FORM_FIELD_START_TIME]),
                         &event->start_minute)) {
        return STR("Start time must be HH:MM.");
    }
    // Named as the form labels it.
    bool const opportunity = form->kind == EVENT_KIND_OPPORTUNITY;
    if (!date_parse_iso(str_from_cstr(form->text[FORM_FIELD_END_DATE]), &event->end_date)) {
        if (opportunity) {
            return STR("Deadline must be YYYY-MM-DD.");
        }
        return STR("End date must be YYYY-MM-DD.");
    }
    if (!form_parse_time(str_from_cstr(form->text[FORM_FIELD_END_TIME]), &event->end_minute)) {
        if (opportunity) {
            return STR("Deadline time must be HH:MM.");
        }
        return STR("End time must be HH:MM.");
    }
    // The location id is found or created when saving, from these texts.
    text_copy_trimmed(event->location.address, sizeof(event->location.address),
                      str_from_cstr(form->text[FORM_FIELD_LOCATION_ADDRESS]));
    text_copy_trimmed(event->location.city, sizeof(event->location.city),
                      str_from_cstr(form->text[FORM_FIELD_LOCATION_CITY]));
    form_description_copy(form, event->description, sizeof(event->description));
    assert(form->kind >= 0);
    assert(form->kind < EVENT_KIND_COUNT);
    int32_t const repeat_option = form_repeat_option(form);
    event->kind = form->kind;
    event->repeat_unit = REPEAT_OPTION_UNITS[repeat_option];
    event->repeat_interval = 1;
    if (repeat_option == REPEAT_OPTION_DAYS) {
        if (!form_parse_repeat_days(str_from_cstr(form->text[FORM_FIELD_REPEAT_DAYS]),
                                    &event->repeat_interval)) {
            return STR("Days must be 1 to 999.");
        }
    }
    // Ignored for one-time events, where the field is hidden.
    Str const repeat_end = str_from_cstr(form->text[FORM_FIELD_REPEAT_END_DATE]);
    event->repeat_has_end = event->repeat_unit != REPEAT_NONE && repeat_end.length > 0;
    if (event->repeat_has_end) {
        if (!date_parse_iso(repeat_end, &event->repeat_end_date)) {
            return STR("Repeat until must be YYYY-MM-DD.");
        }
    }
    // Ignored while the fields are hidden, as the repeat end is for one-time events.
    Str const deadline = str_from_cstr(form->text[FORM_FIELD_DEADLINE_DATE]);
    Str const deadline_time = str_from_cstr(form->text[FORM_FIELD_DEADLINE_TIME]);
    event->has_deadline = form_field_visible(form, FORM_FIELD_DEADLINE_DATE) &&
                          (deadline.length > 0 || deadline_time.length > 0);
    if (event->has_deadline) {
        // A time alone says when, but not on which day, so it is an error, not a deadline today.
        if (!date_parse_iso(deadline, &event->deadline_date)) {
            return STR("Deadline must be YYYY-MM-DD.");
        }
        if (!form_parse_time(deadline_time, &event->deadline_minute)) {
            return STR("Deadline time must be HH:MM.");
        }
    }

    return form_parse_order(event);
}

/**
 * Checks that the parsed dates and times come in order. Returns an empty Str, or what to fix.
 */
static Str form_parse_order(Event const *event) {
    int32_t start_day = date_to_day_number(event->start_date);
    int32_t end_day = date_to_day_number(event->end_date);
    // Named as the form labels it.
    bool const opportunity = event->kind == EVENT_KIND_OPPORTUNITY;
    if (end_day < start_day) {
        if (opportunity) {
            return STR("Deadline is before start.");
        }
        return STR("End date is before start.");
    }
    if (end_day == start_day) {
        if (event->start_minute != TIME_NONE && event->end_minute != TIME_NONE) {
            if (event->end_minute < event->start_minute) {
                if (opportunity) {
                    return STR("Deadline is before start.");
                }
                return STR("End time is before start.");
            }
        }
    }
    if (event->repeat_has_end) {
        if (date_to_day_number(event->repeat_end_date) < start_day) {
            return STR("Repeat until is before start.");
        }
    }
    if (event->has_deadline) {
        if (date_to_day_number(event->deadline_date) < start_day) {
            return STR("Deadline is before start.");
        }
    }
    // Overlapping occurrences would make a day show the same event twice.
    if (event_duration_days(event) >= event_repeat_gap_days_min(event)) {
        return STR("Event must end before it repeats.");
    }
    return STR("");
}

/**
 * The hidden fields of the form are ignored: a problem has no times, location or repeat.
 */
static Str form_parse_problem(Form const *form, Date today, Event *event) {
    assert(form->kind == EVENT_KIND_PROBLEM);

    event->kind = EVENT_KIND_PROBLEM;
    // A new problem falls on today, the day it is created. An edited one keeps its day.
    event->start_date = today;
    if (form->editing_id != 0) {
        if (!date_parse_iso(str_from_cstr(form->text[FORM_FIELD_START_DATE]),
                            &event->start_date)) {
            return STR("Problem date is invalid.");
        }
    }
    event->end_date = event->start_date;
    event->start_minute = TIME_NONE;
    event->end_minute = TIME_NONE;
    event->repeat_unit = REPEAT_NONE;
    event->repeat_interval = 1;
    event->repeat_has_end = false;
    form_description_copy(form, event->description, sizeof(event->description));
    assert(event->location.id == 0);
    assert(event->location.address[0] == '\0');
    return STR("");
}

/**
 * An empty field means the event has no time of day at that end.
 */
static bool form_parse_time(Str text, int32_t *minute_of_day) {
    if (text.length == 0) {
        *minute_of_day = TIME_NONE;
        return true;
    }
    return time_parse(text, minute_of_day);
}

/**
 * Digits only, so that signs, spaces and zero are rejected.
 */
static bool form_parse_repeat_days(Str text, int32_t *days) {
    uint64_t number = 0;
    // Only digits, and not too many to fit: the text box holds at most REPEAT_DAYS_TEXT_SIZE - 1.
    if (!str_to_u64(text, &number)) {
        return false;
    }
    if (number < 1 || number > REPEAT_INTERVAL_MAX) {
        return false;
    }
    *days = (int32_t)number;
    return true;
}

static bool event_occurs_on_day(Event const *event, int32_t day_number) {
    int32_t start_day = 0;
    return event_occurrence_on_day(event, day_number, &start_day);
}

static bool event_created_on_day(Event const *event, int32_t day_number) {
    return date_to_day_number(event->created_date) == day_number;
}

/**
 * An item that goes on the week and day views' timeline: one day long, with a start or an end
 * time. A multi-day item stays a bar across the day cells, where it reads as one span.
 * A done task is faded, so that the tasks still to do stand out.
 */
static float task_fill_alpha(bool done) {
    if (done) {
        return TASK_DONE_FILL_ALPHA;
    }
    return TASK_FILL_ALPHA;
}

static float task_outline_alpha(bool done) {
    if (done) {
        return TASK_DONE_OUTLINE_ALPHA;
    }
    return 1.0f;
}

/**
 * What the card asks before deleting, said for what is deleted: a repeating item goes with all
 * of its repeats.
 */
static Str event_delete_question(Event const *event) {
    if (event->repeat_unit != REPEAT_NONE) {
        return STR("Delete it and all repeats?");
    }
    if (event->kind == EVENT_KIND_TASK) {
        return STR("Delete this task?");
    }
    if (event->kind == EVENT_KIND_PROBLEM) {
        return STR("Delete this problem?");
    }
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        return STR("Delete this opportunity?");
    }
    return STR("Delete this event?");
}

static bool event_is_timed(Event const *event) {
    // A problem has no times, so it is always in the day cells.
    assert(event->kind != EVENT_KIND_PROBLEM || event->start_minute == TIME_NONE);
    if (event_duration_days(event) > 0) {
        return false;
    }
    return event->start_minute != TIME_NONE || event->end_minute != TIME_NONE;
}

/**
 * A month shows every item in its cells; a week or day only those that are not on the timeline.
 */
static bool event_in_day_cell(Event const *event, int32_t range) {
    assert(range >= 0);
    assert(range < RANGE_COUNT);

    return range == RANGE_MONTH || !event_is_timed(event);
}

/**
 * One more line for a location, one for a repeating event that ends, to say when, one per line
 * of description, and one for how many images it has.
 */
static float event_card_height(Fonts const *fonts, Event const *event, bool confirming,
                               float card_width) {
    int32_t extra_lines = (int32_t)(event->location.id != 0) + (int32_t)event->repeat_has_end;
    Str const description = str_from_cstr(event->description);
    if (description.length > 0) {
        float width = card_width - (float)CARD_TEXT_MARGIN_PIXELS;
        extra_lines += text_wrap(&fonts->small, description, width).count;
        extra_lines += (int32_t)(rich_text_image_count(description) > 0);
    }
    float height = (float)(EVENT_CARD_HEIGHT_PIXELS + extra_lines * TEXT_LINE_HEIGHT_PIXELS_SMALL);
    // A confirming card hides its details, but needs room for the question and the buttons.
    if (confirming && height < (float)EVENT_CARD_CONFIRM_HEIGHT_PIXELS) {
        height = (float)EVENT_CARD_CONFIRM_HEIGHT_PIXELS;
    }
    return height;
}

/**
 * Breaks text at spaces into at most DESCRIPTION_LINES_MAX lines that fit width_pixels, and
 * breaks inside a word only when one word alone is wider than a line. A line break or an image
 * ends a line too; images, and the empty lines between paragraphs, are left out, so that a card
 * shows as much of the text as it can.
 */
static Text_Lines text_wrap(Text_Style const *style, Str text, float width_pixels) {
    Text_Lines lines = { .count = 0, .cut = false };
    size_t position = 0;
    // Bounded by DESCRIPTION_LINES_MAX, and each pass either ends the loop or consumes text.
    while (lines.count < DESCRIPTION_LINES_MAX) {
        position = text_wrap_skip(text, position);
        if (position >= text.length) {
            break;
        }
        // The line ends at the next line break or image at the latest.
        size_t hard_end = position;
        while (hard_end < text.length && text.data[hard_end] != '\n' &&
               !rich_text_is_image_marker(text, hard_end)) {
            hard_end++;
        }

        // Extend the line one word at a time while it still fits.
        size_t line_end = position;
        for (size_t i = position; i <= hard_end; i++) {
            if (i == hard_end || text.data[i] == ' ') {
                if (!text_line_fits(style, str_slice(text, position, i), width_pixels)) {
                    break;
                }
                line_end = i;
            }
        }
        // One word wider than the line: break it between characters, at least one per line.
        if (line_end == position) {
            line_end = rich_text_character_next(text, position);
            while (line_end < hard_end) {
                size_t const next = rich_text_character_next(text, line_end);
                if (!text_line_fits(style, str_slice(text, position, next), width_pixels)) {
                    break;
                }
                line_end = next;
            }
        }
        lines.start[lines.count] = (int32_t)position;
        lines.length[lines.count] = (int32_t)(line_end - position);
        lines.count++;
        position = line_end;
    }
    position = text_wrap_skip(text, position);
    lines.cut = position < text.length;
    return lines;
}

static bool text_line_fits(Text_Style const *style, Str line, float width_pixels) {
    assert(line.length < EVENT_DESCRIPTION_SIZE);

    return measure_text(style, line).x <= width_pixels;
}

static bool event_is_missed(Event const *event, Date_Time now) {
    if (event->kind != EVENT_KIND_OPPORTUNITY) {
        return false;
    }
    return opportunity_is_missed(event->end_date, event->end_minute, event->tasks_done, now);
}

/**
 * now matters only for an opportunity, which turns gray once missed.
 */
static Color event_color(Event const *event, Date_Time now) {
    assert(event->id > 0);

    if (event_kind_is_target(event->kind) && event->tasks_done) {
        return TARGET_DONE_COLOR;
    }
    if (event->kind == EVENT_KIND_PROBLEM) {
        return PROBLEM_COLOR;
    }
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        if (event_is_missed(event, now)) {
            return OPPORTUNITY_MISSED_COLOR;
        }
        return OPPORTUNITY_COLOR;
    }
    assert(!event->tasks_done);
    return EVENT_COLORS[event->id % EVENT_COLOR_COUNT];
}

/**
 * What a bar or timeline block shows: the title, after the time when with_time, as where an
 * occurrence starts. An item with only an end time says "Until", since its block is drawn in
 * the hour before the end, not from a start; a multi-day item's end is on another day, so it
 * has none. A missed opportunity says so first, because its gray alone could be any item.
 */
static void event_format_label(Event const *event, bool with_time, Date_Time now, char *buffer,
                               size_t buffer_size) {
    assert(buffer_size > 0);

    char const *missed = "";
    if (event_is_missed(event, now)) {
        missed = "Missed: ";
    }
    char time[TIME_TEXT_SIZE] = "";
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

/**
 * The dates and times, followed by how the event repeats, if it does.
 */
static void event_format_range(Event const *event, Date_Time now, char *buffer,
                               size_t buffer_size) {
    // A problem has no range: it is on its one day, which the panel heading shows.
    if (event->kind == EVENT_KIND_PROBLEM) {
        char const *text = "Problem";
        if (event->tasks_done) {
            text = "Problem, completed";
        }
        snprintf(buffer, buffer_size, "%s", text);
        return;
    }
    // An opportunity is about its deadline, and whether it was used or missed by then; the
    // calendar shows when it starts.
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        // "31 Dec 23:59", with room for any int32_t day as far as the compiler can tell.
        char deadline[32] = "";
        format_date_time_short(event->end_date, event->end_minute, deadline, sizeof(deadline));
        char const *state = "Opportunity";
        if (event_is_missed(event, now)) {
            state = "Missed opportunity";
        }
        // A used opportunity is never missed, so this one wins.
        if (event->tasks_done) {
            state = "Used opportunity";
        }
        snprintf(buffer, buffer_size, "%s, deadline %s", state, deadline);
        return;
    }
    event_format_range_dates(event, buffer, buffer_size);
    if (event->has_deadline) {
        char deadline[32] = "";
        format_date_time_short(event->deadline_date, event->deadline_minute, deadline,
                               sizeof(deadline));
        size_t length = strlen(buffer);
        assert(length < buffer_size);
        // Cutting the text short is acceptable: the card clips long lines anyway.
        snprintf(buffer + length, buffer_size - length, ", due %s", deadline);
    }
    // The longest is "every 999 days".
    char repeat[32] = "";
    event_format_repeat(event, repeat, sizeof(repeat));
    if (repeat[0] != '\0') {
        size_t length = strlen(buffer);
        assert(length < buffer_size);
        // Cutting the text short is acceptable: the card clips long lines anyway.
        int written = snprintf(buffer + length, buffer_size - length, ", %s", repeat);
        assert(written > 0);
    }
}

static void event_format_range_dates(Event const *event, char *buffer, size_t buffer_size) {
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

/**
 * "Address, City", or whichever of the two the location has.
 */
static void event_format_location(Event const *event, char *buffer, size_t buffer_size) {
    Location const *location = &event->location;
    assert(location->id != 0);
    bool has_address = location->address[0] != '\0';
    bool has_city = location->city[0] != '\0';
    assert(has_address || has_city);
    if (has_address && has_city) {
        snprintf(buffer, buffer_size, "%s, %s", location->address, location->city);
    } else {
        char const *place = location->city;
        if (has_address) {
            place = location->address;
        }
        snprintf(buffer, buffer_size, "%s", place);
    }
}

/**
 * Copies text without leading and trailing spaces, so that "Main St " and "Main St" are the
 * same location, and a field of only spaces counts as empty.
 */
/**
 * text without the spaces at its start. A text of only spaces gives the empty text, as a title
 * of nothing but spaces looks blank.
 */
static Str text_skip_spaces(Str text) {
    size_t const first = str_find_not_any(text, STR(" "));
    if (first == STR_NOT_FOUND) {
        return str_make("", 0);
    }
    return str_slice(text, first, text.length);
}

static void text_copy_trimmed(char *buffer, size_t buffer_size, Str text) {
    assert(buffer_size > 0);

    // Cut short, at a whole character, when it does not fit: a field holds what it can.
    (void)str_copy_truncated(str_trim_any(text, STR(" ")), buffer, buffer_size);
}

/**
 * "Created 18 Oct 2026, 20:47 (yesterday)": the exact moment, and how long ago it was.
 */
static void event_format_created(Event const *event, Date_Time now, char *buffer,
                                 size_t buffer_size) {
    char time[TIME_TEXT_SIZE] = "";
    time_format(event->created_minute, time, sizeof(time));
    char age[DATE_TIME_AGE_SIZE] = "";
    date_time_format_age((Date_Time){ event->created_date, event->created_minute }, now, age,
                         sizeof(age));
    Str const month = month_short_name(event->created_date.month);
    snprintf(buffer, buffer_size, "Created %" PRId32 " %.*s %" PRId32 ", %s (%s)",
             event->created_date.day, STR_ARGUMENTS(month), event->created_date.year, time, age);
}

/**
 * "15 Oct" or "15 Oct 09:00".
 */
static void format_date_time_short(Date date, int32_t minute_of_day, char *buffer,
                                   size_t buffer_size) {
    Str const month = month_short_name(date.month);
    if (minute_of_day == TIME_NONE) {
        snprintf(buffer, buffer_size, "%" PRId32 " %.*s", date.day, STR_ARGUMENTS(month));
        return;
    }
    char time[TIME_TEXT_SIZE] = "";
    time_format(minute_of_day, time, sizeof(time));
    snprintf(buffer, buffer_size, "%" PRId32 " %.*s %s", date.day, STR_ARGUMENTS(month), time);
}

/**
 * The description's row among the form's fields: its first line and how many images it has, in
 * a box that opens the description's own page, where there is room to write and to see images.
 */
static void draw_panel_form_description_field(App *app, Rectangle box) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    Description_Editor const *editor = &form->description;
    draw_text(&fonts->ui, STR("Description (optional)"), box.x, box.y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    bool const hovered = !GuiIsLocked() && CheckCollisionPointRec(GetMousePosition(), box);
    int32_t base_property = BASE_COLOR_NORMAL;
    int32_t border_property = BORDER_COLOR_NORMAL;
    if (hovered) {
        base_property = BASE_COLOR_FOCUSED;
        border_property = BORDER_COLOR_FOCUSED;
        app->cursor = APP_CURSOR_HAND;
    }
    DrawRectangleRec(box, style_color(base_property));
    DrawRectangleLinesEx(box, 1.0f, style_color(border_property));

    float const text_x = box.x + DESCRIPTION_EDITOR_PADDING_PIXELS;
    float const text_y = box.y + (box.height - TEXT_SIZE_PIXELS) / 2;
    float right_x = box.x + box.width - DESCRIPTION_EDITOR_PADDING_PIXELS;
    if (editor->image_count > 0) {
        char count_text[TEXT_LINE_SIZE] = "";
        description_format_image_count(editor->image_count, count_text, sizeof(count_text));
        Str const count = str_from_cstr(count_text);
        right_x -= measure_text(&fonts->small, count).x;
        draw_text(&fonts->small, count, right_x,
                  box.y + (box.height - TEXT_SIZE_PIXELS_SMALL) / 2,
                  style_color(TEXT_COLOR_DISABLED));
        right_x -= DESCRIPTION_EDITOR_PADDING_PIXELS;
    }
    Str const first_line = description_first_line(description_text(editor));
    scissor_begin(fonts, text_x, box.y, float_max(right_x - text_x, 0.0f), box.height);
    if (first_line.length > 0) {
        draw_text(&fonts->ui, first_line, text_x, text_y, style_color(TEXT_COLOR_NORMAL));
    } else if (editor->image_count == 0) {
        draw_text(&fonts->ui, STR("Notes, links and images"), text_x, text_y,
                  style_color(TEXT_COLOR_DISABLED));
    }
    scissor_end(fonts);
    if (hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        description_open(form);
    }
}

/**
 * The first line of text of a description, without the spaces, line breaks and images before
 * it, as a preview of the whole.
 */
static Str description_first_line(Str text) {
    size_t const start = text_wrap_skip(text, 0);
    size_t end = start;
    // Bounded by the length: each pass moves end on by one byte.
    while (end < text.length && text.data[end] != '\n' && !rich_text_is_image_marker(text, end)) {
        end++;
    }
    return str_slice(text, start, end);
}

/**
 * "1 image" or "3 images".
 */
static void description_format_image_count(int32_t count, char *buffer, size_t buffer_size) {
    assert(count > 0);

    char const *noun = "images";
    if (count == 1) {
        noun = "image";
    }
    snprintf(buffer, buffer_size, "%" PRId32 " %s", count, noun);
}

/**
 * The form's page for the description, in place of its fields: the text with its images and
 * links, a line of help or of what went wrong, and Done, which goes back to the fields.
 */
static void draw_panel_description(App *app, Rectangle body) {
    Form *form = &app->form;
    assert(form->open);
    assert(form->description.open);

    draw_text(&app->fonts.ui, STR("Description"), body.x, body.y, style_color(TEXT_COLOR_NORMAL));
    float const buttons_y = body.y + body.height - BUTTON_HEIGHT_PIXELS;
    float const hint_y = buttons_y - FORM_ROW_GAP_PIXELS - TEXT_LINE_HEIGHT_PIXELS;
    float const box_y = body.y + 32;
    Rectangle const box = { body.x, box_y, body.width, hint_y - FORM_ROW_GAP_PIXELS - box_y };
    // A window too short for a line of text and the padding around it shows only the buttons.
    if (box.height > 2 * DESCRIPTION_EDITOR_PADDING_PIXELS + TEXT_LINE_HEIGHT_PIXELS) {
        draw_description_editor(app, box);
    }
    draw_panel_description_hint(app, body, hint_y);
    if (GuiButton((Rectangle){ body.x, buttons_y, body.width, BUTTON_HEIGHT_PIXELS }, "Done")) {
        description_close(form);
    }
}

/**
 * What went wrong last, or else how to add images and open links.
 */
static void draw_panel_description_hint(App const *app, Rectangle body, float y) {
    Form const *form = &app->form;
    Str hint = STR("Paste or drop images. Ctrl+click opens a link.");
    if (app->touch) {
        hint = STR("Tap a link to open it.");
    }
    Color color = style_color(TEXT_COLOR_DISABLED);
    if (!form->description.images_loaded) {
        hint = STR("Images not loaded: the description is read-only.");
        color = ERROR_COLOR;
    }
    if (form->error[0] != '\0') {
        hint = str_from_cstr(form->error);
        color = ERROR_COLOR;
    }
    scissor_begin(&app->fonts, body.x, y, body.width, TEXT_LINE_HEIGHT_PIXELS);
    draw_text(&app->fonts.small, hint, body.x, y, color);
    scissor_end(&app->fonts);
}

/**
 * The description's text box: handles the pointer and, while it has the focus, the keys, then
 * draws the lines, the selection and the caret, scrolled to the caret after it moved.
 */
static void draw_description_editor(App *app, Rectangle box) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    Fonts const *fonts = &app->fonts;
    float const padding = (float)DESCRIPTION_EDITOR_PADDING_PIXELS;
    // Room for the scroll bar is kept whether it shows or not, so that the lines do not wrap
    // differently as it comes and goes.
    Rectangle const inner = { box.x + padding, box.y + padding,
                              box.width - 2 * padding - SCROLLBAR_GAP_PIXELS,
                              box.height - 2 * padding };
    assert(inner.width > 0.0f);
    assert(inner.height > 0.0f);

    if (editor->layout_width_pixels != inner.width || editor->layout_scale != fonts->scale) {
        editor->layout_width_pixels = inner.width;
        editor->layout_scale = fonts->scale;
        editor->layout_stale = true;
    }
    description_layout_update(editor, &fonts->ui);
    // Before the pointer, so that a release knows whether the press became a drag of the text.
    scroll_update(&editor->scroll, inner, description_content_height(editor), app->touch);
    if (!GuiIsLocked()) {
        description_editor_pointer(app, box, inner);
    }
    bool const focused = form->field_active == FORM_FIELD_DESCRIPTION;
    if (focused && !GuiIsLocked() && editor->images_loaded) {
        description_editor_keys(app);
    }
    description_layout_update(editor, &fonts->ui);
    float const content_height = description_content_height(editor);
    if (editor->caret_reveal) {
        description_caret_reveal(editor, inner.height, content_height);
    }

    int32_t base_property = BASE_COLOR_NORMAL;
    int32_t border_property = BORDER_COLOR_NORMAL;
    if (focused) {
        base_property = BASE_COLOR_PRESSED;
        border_property = BORDER_COLOR_PRESSED;
    }
    DrawRectangleRec(box, style_color(base_property));
    DrawRectangleLinesEx(box, 1.0f, style_color(border_property));
    // Within the border, so that the caret at the start of a line still shows.
    scissor_begin(fonts, box.x + 1, box.y + 1, box.width - 2, box.height - 2);
    draw_description_editor_lines(app, inner, focused);
    scissor_end(fonts);
    draw_scrollbar((Rectangle){ inner.x, inner.y, inner.width, inner.height }, content_height,
                   editor->scroll.offset_pixels);
}

/**
 * A press in the text gives it the focus and places the caret, and a drag selects; with Ctrl,
 * a press on a link opens it instead. On a touch screen a drag scrolls, and a tap places the
 * caret or opens the link under it. A press elsewhere takes the focus away, which on a phone
 * hides the keyboard.
 */
static void description_editor_pointer(App *app, Rectangle box, Rectangle inner) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    Text_Style const *style = &app->fonts.ui;
    Vector2 const mouse = GetMousePosition();
    bool const over = CheckCollisionPointRec(mouse, box);
    float const x = mouse.x - inner.x;
    float const y = mouse.y - (inner.y - editor->scroll.offset_pixels);
    bool const control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    Rich_Text_Link link = { .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
    if (over) {
        link = description_link_at_point(editor, style, x, y);
        app->cursor = APP_CURSOR_TEXT;
        if (link.start != STR_NOT_FOUND && (control || app->touch)) {
            app->cursor = APP_CURSOR_HAND;
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !over) {
        if (form->field_active == FORM_FIELD_DESCRIPTION) {
            form->field_active = FORM_FIELD_NONE;
        }
        return;
    }
    if (app->touch) {
        Scroll const *scroll = &editor->scroll;
        bool const tapped = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && scroll->pressed &&
                            !scroll->dragging;
        if (!tapped) {
            return;
        }
        if (link.start != STR_NOT_FOUND) {
            app_open_link(app, str_slice(description_text(editor), link.start, link.end));
            return;
        }
        description_focus_at(form, description_index_at(editor, style, x, y), false);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (control && link.start != STR_NOT_FOUND) {
            app_open_link(app, str_slice(description_text(editor), link.start, link.end));
            return;
        }
        bool const shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        description_focus_at(form, description_index_at(editor, style, x, y), shift);
        editor->selecting = true;
        return;
    }
    if (!editor->selecting) {
        return;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        editor->selecting = false;
        return;
    }
    // Dragging past the top or bottom selects on into the lines out of sight, which the caret's
    // reveal then scrolls to, a line per frame.
    editor->cursor = description_index_at(editor, style, x, y);
    editor->caret_reveal = true;
}

/**
 * Gives the text the focus with the caret at index, keeping the selection's other end where it
 * was with extend.
 */
static void description_focus_at(Form *form, int32_t index, bool extend) {
    Description_Editor *editor = &form->description;
    assert(index >= 0);
    assert(index <= editor->length);

    if (editor->images_loaded) {
        form->field_active = FORM_FIELD_DESCRIPTION;
    }
    editor->cursor = index;
    if (!extend) {
        editor->anchor = index;
    }
    editor->goal_x_pixels = -1.0f;
    editor->caret_reveal = true;
}

/**
 * Typing, the clipboard and the keys that move the caret, while the text has the focus.
 */
static void description_editor_keys(App *app) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    assert(editor->images_loaded);

    bool const control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool const shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (control) {
        description_editor_shortcuts(app);
    }
    description_editor_typing(app);
    // Android types Enter as a line break character instead (see android_key_character), and
    // a hardware keyboard there would otherwise break the line twice.
    if (!app->touch &&
        (text_input_key_pressed(KEY_ENTER) || text_input_key_pressed(KEY_KP_ENTER))) {
        description_type(form, STR("\n"));
    }
    if (text_input_key_pressed(KEY_BACKSPACE)) {
        description_delete(editor, false);
    }
    if (text_input_key_pressed(KEY_DELETE)) {
        description_delete(editor, true);
    }
    // The caret moves through the lines as they are after this frame's typing.
    description_layout_update(editor, &app->fonts.ui);
    description_editor_moves(editor, &app->fonts.ui, shift, control);
}

/**
 * Ctrl+A selects everything, Ctrl+C copies the selection and Ctrl+X cuts it, and Ctrl+V pastes
 * text, or an image when the clipboard holds no text. Images are not copied: the clipboard holds
 * the selected text without them.
 */
static void description_editor_shortcuts(App *app) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    if (IsKeyPressed(KEY_A)) {
        editor->anchor = 0;
        editor->cursor = editor->length;
        editor->caret_reveal = true;
    }
    int32_t const start = int32_min(editor->cursor, editor->anchor);
    int32_t const end = int32_max(editor->cursor, editor->anchor);
    bool const cut = IsKeyPressed(KEY_X);
    if ((IsKeyPressed(KEY_C) || cut) && start < end) {
        char copied[EVENT_DESCRIPTION_SIZE] = "";
        Str const selected = str_slice(description_text(editor), (size_t)start, (size_t)end);
        size_t const length = rich_text_clean(selected, copied, sizeof(copied) - 1);
        copied[length] = '\0';
        if (length > 0) {
            SetClipboardText(copied);
        }
        if (cut) {
            bool const removed = description_replace(editor, start, end, STR(""), NULL);
            assert(removed); // Removing always fits.
            (void)removed; // Unused when assertions are compiled out.
        }
    }
    if (IsKeyPressed(KEY_V)) {
        app_description_paste(app);
    }
}

/**
 * Pastes the clipboard's text at the caret, cleaned as rich_text_clean does and cut short to
 * the room left, or else its image.
 */
static void app_description_paste(App *app) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    // NULL when the clipboard is empty, holds no text, or does not exist.
    char const *clipboard = GetClipboardText();
    if (clipboard != NULL && clipboard[0] != '\0') {
        int32_t const selected = int32_max(editor->cursor, editor->anchor) -
                                 int32_min(editor->cursor, editor->anchor);
        // The room the text has, with the selection replaced, before its terminator.
        int32_t const room = EVENT_DESCRIPTION_SIZE - 1 - (editor->length - selected);
        assert(room >= 0);
        char pasted[EVENT_DESCRIPTION_SIZE] = "";
        size_t const length = rich_text_clean(str_from_cstr(clipboard), pasted, (size_t)room);
        if (length > 0) {
            description_type(form, str_make(pasted, length));
            return;
        }
    }
    if (app->clipboard_image == NULL) {
        return;
    }
    Image const image = app->clipboard_image();
    if (!IsImageValid(image)) {
        snprintf(form->error, sizeof(form->error), "The clipboard holds no text or image.");
        return;
    }
    app_description_insert_image(app, image);
}

/**
 * The characters typed this frame, at the caret. Control characters other than a line break,
 * and an image marker, which only an image may bring, are left out.
 */
static void description_editor_typing(App *app) {
    Form *form = &app->form;
    for (int32_t i = 0; i < TYPED_CHARACTER_COUNT_MAX; i++) {
        int32_t const codepoint = text_input_char_pressed();
        if (codepoint == 0) {
            break;
        }
        bool const surrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
        bool const printable = codepoint >= 0x20 && codepoint != 0x7F && codepoint != 0xFFFC &&
                               codepoint <= 0x10FFFF && !surrogate;
        if (codepoint != '\n' && !printable) {
            continue;
        }
        char bytes[4] = { 0 };
        int32_t const size = rich_text_codepoint_encode(codepoint, bytes);
        description_type(form, str_make(bytes, (size_t)size));
    }
}

/**
 * Puts text in place of the selection, or at the caret, or says that the description is full.
 */
static void description_type(Form *form, Str text) {
    Description_Editor *editor = &form->description;
    int32_t const start = int32_min(editor->cursor, editor->anchor);
    int32_t const end = int32_max(editor->cursor, editor->anchor);
    if (!description_replace(editor, start, end, text, NULL)) {
        snprintf(form->error, sizeof(form->error), "The description is full.");
    }
}

/**
 * Deletes the selection, or else the character before the caret, or with forward the one
 * after it. An image is one character.
 */
static void description_delete(Description_Editor *editor, bool forward) {
    Str const text = description_text(editor);
    int32_t start = int32_min(editor->cursor, editor->anchor);
    int32_t end = int32_max(editor->cursor, editor->anchor);
    if (start == end) {
        if (forward) {
            if (end == editor->length) {
                return;
            }
            end = (int32_t)rich_text_character_next(text, (size_t)end);
        } else {
            if (start == 0) {
                return;
            }
            start = (int32_t)rich_text_character_previous(text, (size_t)start);
        }
    }
    bool const removed = description_replace(editor, start, end, STR(""), NULL);
    assert(removed); // Removing always fits.
    (void)removed; // Unused when assertions are compiled out.
}

/**
 * The arrow keys, Home and End move the caret, by a character, a line, or with Ctrl to either
 * end of the text; with Shift they select. Up and Down keep to the column the caret came from.
 */
static void description_editor_moves(Description_Editor *editor, Text_Style const *style,
                                     bool shift, bool control) {
    assert(!editor->layout_stale);

    Str const text = description_text(editor);
    int32_t const start = int32_min(editor->cursor, editor->anchor);
    int32_t const end = int32_max(editor->cursor, editor->anchor);
    int32_t const line_index = description_caret_line(editor, editor->cursor);
    Description_Line const *line = &editor->lines[line_index];
    int32_t target = editor->cursor;
    bool moved = false;
    bool vertical = false;
    if (text_input_key_pressed(KEY_LEFT)) {
        moved = true;
        // Without Shift, a selection collapses to its start, as in other editors.
        if (!shift && start < end) {
            target = start;
        } else if (target > 0) {
            target = (int32_t)rich_text_character_previous(text, (size_t)target);
        }
    }
    if (text_input_key_pressed(KEY_RIGHT)) {
        moved = true;
        if (!shift && start < end) {
            target = end;
        } else if (target < editor->length) {
            target = (int32_t)rich_text_character_next(text, (size_t)target);
        }
    }
    int32_t step = 0;
    if (text_input_key_pressed(KEY_UP)) {
        step = -1;
    }
    if (text_input_key_pressed(KEY_DOWN)) {
        step = 1;
    }
    if (step != 0) {
        moved = true;
        vertical = true;
        if (editor->goal_x_pixels < 0.0f) {
            editor->goal_x_pixels = description_caret_x(editor, style, editor->cursor);
        }
        int32_t const target_line = line_index + step;
        if (target_line < 0) {
            target = 0;
        } else if (target_line >= editor->line_count) {
            target = editor->length;
        } else {
            target = description_line_index_at_x(editor, style, target_line,
                                                 editor->goal_x_pixels);
        }
    }
    if (text_input_key_pressed(KEY_HOME)) {
        moved = true;
        target = line->start;
        if (control) {
            target = 0;
        }
    }
    if (text_input_key_pressed(KEY_END)) {
        moved = true;
        target = description_line_end_caret(editor, line_index);
        if (control) {
            target = editor->length;
        }
    }
    if (!moved) {
        return;
    }
    if (!vertical) {
        editor->goal_x_pixels = -1.0f;
    }
    editor->cursor = target;
    if (!shift) {
        editor->anchor = target;
    }
    editor->caret_reveal = true;
    assert(editor->cursor >= 0);
    assert(editor->cursor <= editor->length);
}

/**
 * Where End puts the caret on a line: after its last character, or before the last one of a
 * line that wraps inside a word, where the end is already the next line's start.
 */
static int32_t description_line_end_caret(Description_Editor const *editor, int32_t line_index) {
    assert(line_index >= 0);
    assert(line_index < editor->line_count);

    Description_Line const *line = &editor->lines[line_index];
    if (description_line_continues(editor, line_index) && line->end > line->start) {
        return (int32_t)rich_text_character_previous(description_text(editor),
                                                     (size_t)line->end);
    }
    return line->end;
}

/**
 * Adds an image at the caret, in place of the selection: scaled down to the largest size kept,
 * stored as a PNG file, and shown from a texture. Takes the image and unloads it. When it
 * cannot, the form says why. raylib allocates as it converts and encodes, which is fine here:
 * this runs once per image the user adds, not every frame.
 */
static void app_description_insert_image(App *app, Image image) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    assert(IsImageValid(image));

    int32_t const start = int32_min(editor->cursor, editor->anchor);
    int32_t const end = int32_max(editor->cursor, editor->anchor);
    Str const text = description_text(editor);
    int32_t const replaced = rich_text_image_index_at(text, (size_t)end) -
                             rich_text_image_index_at(text, (size_t)start);
    if (!editor->images_loaded) {
        UnloadImage(image);
        return;
    }
    if (editor->image_count - replaced >= DESCRIPTION_IMAGE_COUNT_MAX) {
        snprintf(form->error, sizeof(form->error), "A description holds at most %d images.",
                 (int)DESCRIPTION_IMAGE_COUNT_MAX);
        UnloadImage(image);
        return;
    }
    if (editor->length - (end - start) + RICH_TEXT_IMAGE_MARKER_SIZE >= EVENT_DESCRIPTION_SIZE) {
        snprintf(form->error, sizeof(form->error), "The description is full.");
        UnloadImage(image);
        return;
    }
    // Eight bits a channel with alpha, which PNG encodes from any source format, such as a
    // compressed texture format or a 16-bit one.
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    image_fit_side(&image, DESCRIPTION_IMAGE_SIDE_PIXELS_MAX);
    int png_size = 0;
    unsigned char *png = ExportImageToMemory(image, ".png", &png_size);
    // Bounded by IMAGE_SHRINK_STEP_COUNT_MAX: each pass shrinks the image to three quarters.
    for (int32_t step = 0; png != NULL && png_size > DESCRIPTION_IMAGE_SIZE_BYTES_MAX &&
                           step < IMAGE_SHRINK_STEP_COUNT_MAX; step++) {
        MemFree(png);
        ImageResize(&image, int32_max(image.width * 3 / 4, 1), int32_max(image.height * 3 / 4, 1));
        png = ExportImageToMemory(image, ".png", &png_size);
    }
    if (png == NULL || png_size > DESCRIPTION_IMAGE_SIZE_BYTES_MAX) {
        snprintf(form->error, sizeof(form->error), "The image could not be made small enough.");
        MemFree(png);
        UnloadImage(image);
        return;
    }
    int64_t const id = backend_image_insert(app->backend, array_slice_make(png, png_size));
    MemFree(png);
    if (id == 0) {
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        UnloadImage(image);
        return;
    }
    Description_Image added = { .texture = LoadTextureFromImage(image), .id = id };
    UnloadImage(image);
    // Images are drawn at the panel's width, rarely at their own size, so they are smoothed.
    SetTextureFilter(added.texture, TEXTURE_FILTER_BILINEAR);
    bool const inserted = description_replace(editor, start, end, STR(RICH_TEXT_IMAGE_MARKER),
                                              &added);
    assert(inserted); // The room and the count were checked above.
    (void)inserted; // Unused when assertions are compiled out.
    form->field_active = FORM_FIELD_DESCRIPTION;
    form->error[0] = '\0';
}

/**
 * Scales the image down, keeping its proportions, so that neither side is longer than side_max.
 */
static void image_fit_side(Image *image, int32_t side_max) {
    assert(image->width > 0);
    assert(image->height > 0);
    assert(side_max > 0);

    int32_t const side = int32_max(image->width, image->height);
    if (side <= side_max) {
        return;
    }
    int32_t const width = int32_max((int32_t)((int64_t)image->width * side_max / side), 1);
    int32_t const height = int32_max((int32_t)((int64_t)image->height * side_max / side), 1);
    ImageResize(image, width, height);
    assert(image->width <= side_max);
    assert(image->height <= side_max);
}

/**
 * Files dropped on the window go where the form's open page takes them: image files into the
 * description, at the caret, and any files onto the attachments, as many as there is room for.
 * Anywhere on the window counts, because the window system does not say where above the app a
 * file is dropped. raylib keeps the dropped files until they are unloaded, so they are taken
 * every frame, and dropped when nothing can use them, so that a drop on another screen does not
 * land in a description or an attachment list opened later.
 */
static void app_take_dropped_files(App *app) {
    if (!IsFileDropped()) {
        return;
    }
    FilePathList const files = LoadDroppedFiles();
    Form *form = &app->form;
    bool const input = app->input_enabled && form->open;
    bool const images_accepted = input && form->description.open &&
                                 form->description.images_loaded;
    bool const files_accepted = input && form->attachments.open && form->attachments.loaded &&
                                app->files != NULL;
    for (unsigned int i = 0; i < files.count; i++) {
        if (images_accepted) {
            // raylib reads PNG, JPEG, BMP, GIF and a few more, by the name's extension.
            Image const image = LoadImage(files.paths[i]);
            if (!IsImageValid(image)) {
                snprintf(form->error, sizeof(form->error),
                         "Not an image file that can be added, such as PNG or JPEG.");
                continue;
            }
            app_description_insert_image(app, image);
        }
        if (files_accepted) {
            app_attachment_add(app, str_from_cstr(files.paths[i]));
        }
    }
    UnloadDroppedFiles(files);
}

/**
 * Opens a link of a description in the system's browser. Only web addresses are links (see
 * rich_text_link_find), so that nothing else, such as a program, can be started this way.
 */
static void app_open_link(App *app, Str link) {
    char url[RICH_TEXT_URL_SIZE] = "";
    bool const fits = rich_text_link_url(link, url, sizeof(url));
    assert(fits); // RICH_TEXT_URL_SIZE holds the longest description and the scheme.
    if (!fits) {
        return;
    }
    OpenURL(url);
    app_set_status(app, STR("Opened the link in the browser."));
}

/**
 * Shows the description's page with the caret at the end of the text.
 */
static void description_open(Form *form) {
    Description_Editor *editor = &form->description;
    assert(form->open);
    assert(!editor->open);

    editor->open = true;
    form->picker_open = false;
    form->error[0] = '\0';
    description_focus_at(form, editor->length, false);
}

/**
 * Goes back to the form's fields; the description is kept for saving.
 */
static void description_close(Form *form) {
    Description_Editor *editor = &form->description;
    assert(editor->open);

    editor->open = false;
    editor->selecting = false;
    if (form->field_active == FORM_FIELD_DESCRIPTION) {
        form->field_active = FORM_FIELD_NONE;
    }
}

/**
 * Starts the editor with text as the description, without images; the caller loads those.
 */
static void description_set_text(Description_Editor *editor, Str text) {
    assert(editor->image_count == 0);

    (void)str_copy_truncated(text, editor->text, sizeof(editor->text)); // Fits: same size.
    editor->length = (int32_t)strlen(editor->text);
    editor->cursor = editor->length;
    editor->anchor = editor->length;
    editor->layout_stale = true;
}

/**
 * Unloads the images' textures, which the GPU holds, and forgets them.
 */
static void description_clear_images(Description_Editor *editor) {
    assert(editor->image_count >= 0);
    assert(editor->image_count <= DESCRIPTION_IMAGE_COUNT_MAX);

    for (int32_t i = 0; i < editor->image_count; i++) {
        if (editor->images[i].texture.id != 0) {
            UnloadTexture(editor->images[i].texture);
        }
        editor->images[i] = (Description_Image){ .id = 0 };
    }
    editor->image_count = 0;
}

static Str description_text(Description_Editor const *editor) {
    assert(editor->length >= 0);
    assert(editor->length < EVENT_DESCRIPTION_SIZE);

    return str_make(editor->text, (size_t)editor->length);
}

/**
 * Replaces the bytes from start up to end with insert, and the images of the markers among them
 * with added when insert is an image's marker, or else with none. Returns false, changing
 * nothing, when the text would not fit. The caret ends after the inserted text.
 */
static bool description_replace(Description_Editor *editor, int32_t start, int32_t end,
                                Str insert, Description_Image const *added) {
    assert(start >= 0);
    assert(start <= end);
    assert(end <= editor->length);
    assert((added != NULL) == str_eq(insert, STR(RICH_TEXT_IMAGE_MARKER)));

    Str const text = description_text(editor);
    int32_t const first = rich_text_image_index_at(text, (size_t)start);
    int32_t const removed = rich_text_image_index_at(text, (size_t)end) - first;
    size_t length = (size_t)editor->length;
    if (!rich_text_replace(editor->text, &length, sizeof(editor->text), (size_t)start,
                           (size_t)end, insert)) {
        return false;
    }
    for (int32_t i = first; i < first + removed; i++) {
        if (editor->images[i].texture.id != 0) {
            UnloadTexture(editor->images[i].texture);
        }
    }
    int32_t const after = editor->image_count - first - removed;
    memmove(&editor->images[first], &editor->images[first + removed],
            (size_t)after * sizeof(editor->images[0]));
    editor->image_count -= removed;
    if (added != NULL) {
        assert(editor->image_count < DESCRIPTION_IMAGE_COUNT_MAX);
        memmove(&editor->images[first + 1], &editor->images[first],
                (size_t)after * sizeof(editor->images[0]));
        editor->images[first] = *added;
        editor->image_count++;
    }
    editor->length = (int32_t)length;
    editor->cursor = start + (int32_t)insert.length;
    editor->anchor = editor->cursor;
    editor->goal_x_pixels = -1.0f;
    editor->layout_stale = true;
    editor->caret_reveal = true;
    assert(editor->image_count == rich_text_image_count(description_text(editor)));
    return true;
}

/**
 * Lays the text out again, if it or the width changed since it was last laid out.
 */
static void description_layout_update(Description_Editor *editor, Text_Style const *style) {
    if (editor->layout_stale) {
        description_layout(editor, style);
        editor->layout_stale = false;
    }
}

/**
 * Breaks the text into lines at its line breaks, before and after each image, and where a line
 * would be wider than the editor, at the last space that fits or else within the word, and
 * finds its links. A line break right after an image ends the image's line, rather than
 * leaving an empty line below it. Measures each character once, which keeps this fast enough to
 * run whenever the text changes.
 */
static void description_layout(Description_Editor *editor, Text_Style const *style) {
    float const width = editor->layout_width_pixels;
    assert(width > 0.0f);

    Str const text = description_text(editor);
    int32_t const length = editor->length;
    int32_t position = 0;
    int32_t image_index = 0;
    float y = 0.0f;
    int32_t count = 0;
    // Bounded by DESCRIPTION_LINE_COUNT_MAX: every line but the last takes at least one byte.
    while (count < DESCRIPTION_LINE_COUNT_MAX) {
        Description_Line line = { .y_pixels = y, .start = position, .image_index = -1 };
        int32_t next = position;
        if (rich_text_is_image_marker(text, (size_t)position)) {
            line.image_index = image_index;
            image_index++;
            line.end = position + RICH_TEXT_IMAGE_MARKER_SIZE;
            line.height_pixels = description_image_size(editor, line.image_index, width).y +
                                 2 * DESCRIPTION_IMAGE_GAP_PIXELS;
            next = line.end;
            if (next < length && text.data[next] == '\n') {
                next++;
            }
        } else {
            line.height_pixels = TEXT_LINE_HEIGHT_PIXELS;
            next = description_layout_text_line(style, text, position, width, &line.end);
        }
        editor->lines[count] = line;
        count++;
        y += line.height_pixels;
        // A line break that ends the text leaves an empty line after it, for the caret.
        bool const break_taken = next > line.end && text.data[next - 1] == '\n';
        if (next >= length && !break_taken) {
            break;
        }
        assert(next > position);
        position = next;
    }
    assert(count >= 1);
    assert(image_index == editor->image_count);
    editor->line_count = count;
    editor->link_count = links_collect(text, text.length, editor->links,
                                       DESCRIPTION_LINK_COUNT_MAX);
}

/**
 * Finds where the line of text that starts at start ends, at width_pixels at the most, into
 * *end. Returns where the next line starts: after the line break that ends this one, or at the
 * image or the word that comes next.
 */
static int32_t description_layout_text_line(Text_Style const *style, Str text, int32_t start,
                                            float width_pixels, int32_t *end) {
    int32_t const length = (int32_t)text.length;
    float x = 0.0f;
    int32_t i = start;
    // Where a line may wrap: after the last space seen, or -1 before the first.
    int32_t wrap_at = -1;
    // Bounded by the length: each pass moves i on by a character.
    while (i < length) {
        char const character = text.data[i];
        if (character == '\n') {
            *end = i;
            return i + 1;
        }
        if (rich_text_is_image_marker(text, (size_t)i)) {
            *end = i;
            return i;
        }
        int32_t size = 0;
        float const advance = text_advance(style, rich_text_codepoint(text, (size_t)i, &size));
        // Spaces may run past the edge, unseen, so that a line never starts with one.
        if (x + advance > width_pixels && i > start && character != ' ') {
            if (wrap_at > start) {
                i = wrap_at;
            }
            *end = i;
            return i;
        }
        x += advance;
        i += size;
        if (character == ' ') {
            wrap_at = i;
        }
    }
    *end = length;
    return length;
}

/**
 * The size an image is drawn at: its own size in screen pixels, which is the sharpest, when
 * that fits the line, or else narrowed to the line, keeping its proportions. One that could not
 * be loaded is a placeholder across the line.
 */
static Vector2 description_image_size(Description_Editor const *editor, int32_t image_index,
                                      float width_pixels) {
    assert(image_index >= 0);
    assert(image_index < editor->image_count);
    assert(editor->layout_scale > 0.0f);

    Texture2D const texture = editor->images[image_index].texture;
    if (texture.id == 0) {
        return (Vector2){ width_pixels, DESCRIPTION_IMAGE_PLACEHOLDER_HEIGHT_PIXELS };
    }
    float width = (float)texture.width / editor->layout_scale;
    float height = (float)texture.height / editor->layout_scale;
    if (width > width_pixels) {
        height = height * width_pixels / width;
        width = width_pixels;
    }
    return (Vector2){ width, float_max(height, 1.0f) };
}

static float description_content_height(Description_Editor const *editor) {
    assert(!editor->layout_stale);
    assert(editor->line_count >= 1);

    Description_Line const *last = &editor->lines[editor->line_count - 1];
    return last->y_pixels + last->height_pixels;
}

/**
 * Whether the line after line_index carries on from it without a break: one that wrapped, or
 * a line of text after an image. At such a line's end, the caret is at the next line's start.
 */
static bool description_line_continues(Description_Editor const *editor, int32_t line_index) {
    assert(line_index >= 0);
    assert(line_index < editor->line_count);

    if (line_index + 1 >= editor->line_count) {
        return false;
    }
    Description_Line const *line = &editor->lines[line_index];
    Description_Line const *next = &editor->lines[line_index + 1];
    // Text that stops before an image keeps the caret at its end, so that typing there goes on
    // with the text before the image.
    bool const text_before_image = line->image_index < 0 && next->image_index >= 0;
    return next->start == line->end && !text_before_image;
}

/**
 * The line the caret is drawn on at index.
 */
static int32_t description_caret_line(Description_Editor const *editor, int32_t index) {
    assert(!editor->layout_stale);
    assert(index >= 0);
    assert(index <= editor->length);

    for (int32_t i = 0; i < editor->line_count; i++) {
        Description_Line const *line = &editor->lines[i];
        if (index < line->start) {
            break;
        }
        if (index > line->end) {
            continue;
        }
        if (index == line->end && description_line_continues(editor, i)) {
            continue;
        }
        return i;
    }
    return editor->line_count - 1;
}

/**
 * x of the caret at index, from the start of the lines: after the characters before it on its
 * line, or at either side of an image.
 */
static float description_caret_x(Description_Editor const *editor, Text_Style const *style,
                                 int32_t index) {
    Description_Line const *line = &editor->lines[description_caret_line(editor, index)];
    if (line->image_index >= 0) {
        if (index == line->start) {
            return 0.0f;
        }
        return description_image_size(editor, line->image_index, editor->layout_width_pixels).x;
    }
    Str const text = description_text(editor);
    int32_t const end = int32_max(int32_min(index, line->end), line->start);
    return text_width(style, str_slice(text, (size_t)line->start, (size_t)end));
}

/**
 * The line at y, from the top of the lines; the first above them and the last below them.
 */
static int32_t description_line_at(Description_Editor const *editor, float y) {
    assert(editor->line_count >= 1);

    for (int32_t i = 0; i < editor->line_count; i++) {
        Description_Line const *line = &editor->lines[i];
        if (y < line->y_pixels + line->height_pixels) {
            return i;
        }
    }
    return editor->line_count - 1;
}

/**
 * The caret position nearest the point x, y, from the top left of the lines.
 */
static int32_t description_index_at(Description_Editor const *editor, Text_Style const *style,
                                    float x, float y) {
    return description_line_index_at_x(editor, style, description_line_at(editor, y), x);
}

/**
 * The caret position on a line nearest x: between the characters on either side of it, or the
 * side of an image that x is on.
 */
static int32_t description_line_index_at_x(Description_Editor const *editor,
                                           Text_Style const *style, int32_t line_index,
                                           float x) {
    assert(line_index >= 0);
    assert(line_index < editor->line_count);

    Description_Line const *line = &editor->lines[line_index];
    if (line->image_index >= 0) {
        float const width = description_image_size(editor, line->image_index,
                                                   editor->layout_width_pixels).x;
        if (x < width / 2) {
            return line->start;
        }
        return line->end;
    }
    Str const text = description_text(editor);
    float left = 0.0f;
    int32_t i = line->start;
    // Bounded by the line: each pass moves i on by a character.
    while (i < line->end) {
        int32_t size = 0;
        float const advance = text_advance(style, rich_text_codepoint(text, (size_t)i, &size));
        if (x < left + advance / 2) {
            return i;
        }
        left += advance;
        i += size;
    }
    // Past the end of a line that wraps inside a word, the caret stays before its last
    // character, as it would be drawn on the next line otherwise.
    return description_line_end_caret(editor, line_index);
}

/**
 * The link under the point x, y, from the top left of the lines, or one with start
 * STR_NOT_FOUND when there is none.
 */
static Rich_Text_Link description_link_at_point(Description_Editor const *editor,
                                                Text_Style const *style, float x, float y) {
    Rich_Text_Link const none = { .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
    if (editor->layout_stale || y < 0.0f || y >= description_content_height(editor)) {
        return none;
    }
    Description_Line const *line = &editor->lines[description_line_at(editor, y)];
    if (line->image_index >= 0) {
        return none;
    }
    Str const text = description_text(editor);
    int32_t const index = text_index_at_x(style, text, (size_t)line->start, (size_t)line->end, x);
    if (index < 0) {
        return none;
    }
    return links_find_at(array_slice_make((void *)editor->links, editor->link_count),
                         (size_t)index);
}

/**
 * Scrolls just enough that the caret's line is in sight.
 */
static void description_caret_reveal(Description_Editor *editor, float view_height,
                                     float content_height) {
    assert(view_height > 0.0f);

    editor->caret_reveal = false;
    Description_Line const *line =
        &editor->lines[description_caret_line(editor, editor->cursor)];
    Scroll *scroll = &editor->scroll;
    if (line->y_pixels + line->height_pixels > scroll->offset_pixels + view_height) {
        scroll->offset_pixels = line->y_pixels + line->height_pixels - view_height;
    }
    if (line->y_pixels < scroll->offset_pixels) {
        scroll->offset_pixels = line->y_pixels;
    }
    float const offset_max = float_max(content_height - view_height, 0.0f);
    scroll->offset_pixels = float_max(float_min(scroll->offset_pixels, offset_max), 0.0f);
}

/**
 * The lines in sight, each image, the selection behind them and the caret.
 */
static void draw_description_editor_lines(App const *app, Rectangle inner, bool focused) {
    Description_Editor const *editor = &app->form.description;
    Text_Style const *style = &app->fonts.ui;
    Str const text = description_text(editor);
    float const top = inner.y - editor->scroll.offset_pixels;
    int32_t const selection_start = int32_min(editor->cursor, editor->anchor);
    int32_t const selection_end = int32_max(editor->cursor, editor->anchor);
    Color const normal = style_color(TEXT_COLOR_NORMAL);
    Color const accent = style_color(BORDER_COLOR_PRESSED);
    Color const selection = Fade(accent, 0.35f);
    Array_Slice const links = array_slice_make((void *)editor->links, editor->link_count);
    for (int32_t i = 0; i < editor->line_count; i++) {
        Description_Line const *line = &editor->lines[i];
        float const y = top + line->y_pixels;
        if (y + line->height_pixels < inner.y) {
            continue;
        }
        if (y > inner.y + inner.height) {
            break;
        }
        bool const selected_whole = selection_start <= line->start && line->end <= selection_end &&
                                    selection_start < selection_end;
        if (line->image_index >= 0) {
            Vector2 const size = description_image_size(editor, line->image_index, inner.width);
            Rectangle const area = { inner.x, y + DESCRIPTION_IMAGE_GAP_PIXELS, size.x, size.y };
            draw_description_image(app, editor->images[line->image_index].texture, area);
            if (selected_whole) {
                DrawRectangleRec(area, selection);
            }
            continue;
        }
        if (selection_start < selection_end && selection_start <= line->end &&
            selection_end > line->start) {
            int32_t const from = int32_max(selection_start, line->start);
            int32_t const to = int32_min(selection_end, line->end);
            float const left = text_width(style, str_slice(text, (size_t)line->start,
                                                           (size_t)from));
            float width = text_width(style, str_slice(text, (size_t)from, (size_t)to));
            // A selected line break shows as a space's width after the line.
            if (selection_end > line->end && line->end < editor->length &&
                text.data[line->end] == '\n') {
                width += text_advance(style, ' ');
            }
            DrawRectangleRec((Rectangle){ inner.x + left, y, width, TEXT_LINE_HEIGHT_PIXELS },
                             selection);
        }
        draw_text_links(style, text, (size_t)line->start, (size_t)line->end, links, inner.x,
                        y + (TEXT_LINE_HEIGHT_PIXELS - TEXT_SIZE_PIXELS) / 2, normal, accent);
    }
    if (focused) {
        Description_Line const *line =
            &editor->lines[description_caret_line(editor, editor->cursor)];
        float const x = inner.x + description_caret_x(editor, style, editor->cursor);
        float height = TEXT_LINE_HEIGHT_PIXELS - 4.0f;
        float y = top + line->y_pixels + 2.0f;
        if (line->image_index >= 0) {
            height = line->height_pixels - 2 * DESCRIPTION_IMAGE_GAP_PIXELS;
            y = top + line->y_pixels + DESCRIPTION_IMAGE_GAP_PIXELS;
        }
        DrawRectangleRec((Rectangle){ x - 1.0f, y, 2.0f, height }, accent);
    }
}

/**
 * An image scaled into area, or for one that could not be loaded, a frame saying so.
 */
static void draw_description_image(App const *app, Texture2D texture, Rectangle area) {
    if (texture.id != 0) {
        Rectangle const source = { 0, 0, (float)texture.width, (float)texture.height };
        DrawTexturePro(texture, source, area, (Vector2){ 0, 0 }, 0.0f, WHITE);
        return;
    }
    DrawRectangleLinesEx(area, 1.0f, style_color(BORDER_COLOR_FOCUSED));
    draw_text_centered(&app->fonts.small, STR("The image could not be shown."), area,
                       style_color(TEXT_COLOR_DISABLED));
}

/**
 * Loads the images of the item's description, in the order of their markers, into textures. A
 * failure to list them makes the description read-only (see Description_Editor.images_loaded);
 * an image that cannot be loaded or decoded is shown as a placeholder, and kept when saving.
 */
static void app_form_load_images(App *app, int64_t event_id) {
    Form *form = &app->form;
    Description_Editor *editor = &form->description;
    assert(event_id > 0);
    assert(editor->image_count == 0);

    int32_t const marker_count = rich_text_image_count(description_text(editor));
    if (marker_count == 0) {
        return; // Nothing to ask the backend, which may be across the network.
    }
    if (marker_count > DESCRIPTION_IMAGE_COUNT_MAX) {
        editor->images_loaded = false;
        snprintf(form->error, sizeof(form->error), "The description has too many images.");
        return;
    }
    int64_t ids[DESCRIPTION_IMAGE_COUNT_MAX] = { 0 };
    int32_t const count = backend_load_event_images(
        app->backend, event_id, array_slice_make(ids, DESCRIPTION_IMAGE_COUNT_MAX));
    if (count < 0) {
        editor->images_loaded = false;
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    // A marker without an image, which only a database changed by another program has, shows
    // a placeholder and is dropped when saving; an image without a marker is deleted then.
    for (int32_t i = 0; i < marker_count; i++) {
        Description_Image image = { .id = 0 };
        if (i < count) {
            image.id = ids[i];
            image.texture = app_image_texture_load(app, ids[i]);
        }
        editor->images[i] = image;
    }
    editor->image_count = marker_count;
    editor->layout_stale = true;
}

/**
 * The stored image with id as a texture, or one with id 0 when it cannot be loaded or decoded.
 */
static Texture2D app_image_texture_load(App *app, int64_t id) {
    assert(id > 0);

    Texture2D const none = { .id = 0 };
    int32_t const size = backend_image_load(
        app->backend, id, array_slice_make(app->blob_bytes, DESCRIPTION_IMAGE_SIZE_BYTES_MAX));
    if (size <= 0) {
        return none;
    }
    Image const image = LoadImageFromMemory(".png", app->blob_bytes, size);
    if (!IsImageValid(image)) {
        return none;
    }
    Texture2D const texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (!IsTextureValid(texture)) {
        return none;
    }
    // As for an added image (see app_description_insert_image).
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

/**
 * Gives the saved item the description's images, in the order of their markers; those it had
 * and no longer shows are deleted. Skipped when they could not be loaded, which keeps them as
 * they are, and for a new item without images, which has none to delete.
 */
static bool app_form_save_images(App *app, int64_t id, bool inserted) {
    Description_Editor const *editor = &app->form.description;
    assert(id > 0);

    if (!editor->images_loaded) {
        return true;
    }
    int64_t ids[DESCRIPTION_IMAGE_COUNT_MAX] = { 0 };
    int32_t count = 0;
    for (int32_t i = 0; i < editor->image_count; i++) {
        // An image that was never there is dropped, with its marker (see form_description_copy).
        if (editor->images[i].id != 0) {
            ids[count] = editor->images[i].id;
            count++;
        }
    }
    if (inserted && count == 0) {
        return true;
    }
    if (!backend_set_event_images(app->backend, id, array_slice_make(ids, count))) {
        backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
        return false;
    }
    return true;
}

/**
 * The description as it is saved: without spaces and empty lines at its ends, and without the
 * markers of images that were never there, which saving drops (see app_form_save_images).
 */
static void form_description_copy(Form const *form, char *buffer, size_t buffer_size) {
    Description_Editor const *editor = &form->description;
    assert(buffer_size >= sizeof(editor->text));

    Str const text = description_text(editor);
    Str_Builder builder;
    str_builder_init(&builder, buffer, buffer_size - 1);
    int32_t image_index = 0;
    size_t i = 0;
    // Bounded by the length: each pass moves i on by at least one byte.
    while (i < text.length) {
        if (rich_text_is_image_marker(text, i)) {
            assert(image_index < editor->image_count || !editor->images_loaded);
            bool const kept = !editor->images_loaded || editor->images[image_index].id != 0;
            if (kept) {
                (void)str_builder_append(&builder, STR(RICH_TEXT_IMAGE_MARKER));
            }
            image_index++;
            i += RICH_TEXT_IMAGE_MARKER_SIZE;
            continue;
        }
        (void)str_builder_append(&builder, str_slice(text, i, i + 1));
        i++;
    }
    assert(!builder.overflow); // The buffer is as large as the editor's text.
    Str const trimmed = str_trim_any(str_builder_str(&builder), STR(" \n"));
    memmove(buffer, trimmed.data, trimmed.length);
    buffer[trimmed.length] = '\0';
}

/**
 * The width of text drawn in style, added up from each character's advance as raylib draws
 * them, so that the caret and the selection line up with the drawn text.
 */
static float text_width(Text_Style const *style, Str text) {
    float width = 0.0f;
    size_t i = 0;
    // Bounded by the length: each pass moves i on by a character.
    while (i < text.length) {
        int32_t size = 0;
        width += text_advance(style, rich_text_codepoint(text, i, &size));
        i += (size_t)size;
    }
    return width;
}

/**
 * How far raylib's DrawTextEx moves on after drawing the character, in layout pixels.
 */
static float text_advance(Text_Style const *style, int32_t codepoint) {
    Font const font = style->font;
    assert(font.baseSize > 0);

    int const glyph = GetGlyphIndex(font, (int)codepoint);
    float advance = (float)font.glyphs[glyph].advanceX;
    // raylib's own rule: a glyph without an advance moves by its width.
    if (font.glyphs[glyph].advanceX == 0) {
        advance = font.recs[glyph].width;
    }
    float const scale_factor = style->size_pixels / (float)font.baseSize;
    return advance * scale_factor + (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
}

/**
 * The index of the character between start and end of text that x is on, from where start is
 * drawn, or -1 when x is before or after them.
 */
static int32_t text_index_at_x(Text_Style const *style, Str text, size_t start, size_t end,
                               float x) {
    assert(start <= end);
    assert(end <= text.length);

    float left = 0.0f;
    size_t i = start;
    // Bounded by end: each pass moves i on by a character.
    while (i < end && x >= 0.0f) {
        int32_t size = 0;
        float const advance = text_advance(style, rich_text_codepoint(text, i, &size));
        if (x < left + advance) {
            return (int32_t)i;
        }
        left += advance;
        i += (size_t)size;
    }
    return -1;
}

/**
 * The links of text that start before until, in order, into links, an array of count_max.
 * Returns how many; more are left out.
 */
static int32_t links_collect(Str text, size_t until, Rich_Text_Link *links, int32_t count_max) {
    assert(count_max > 0);

    int32_t count = 0;
    size_t from = 0;
    // Bounded by count_max, and by the length: each link found ends after from.
    while (count < count_max && from < until) {
        Rich_Text_Link const link = rich_text_link_find(text, from);
        if (link.start == STR_NOT_FOUND || link.start >= until) {
            break;
        }
        links[count] = link;
        count++;
        from = link.end;
    }
    return count;
}

/**
 * The link in links, a slice of Rich_Text_Link in order, that holds index, or one with start
 * STR_NOT_FOUND.
 */
static Rich_Text_Link links_find_at(Array_Slice links, size_t index) {
    Rich_Text_Link const *items = links.items;
    for (int32_t i = 0; i < links.count; i++) {
        if (items[i].start <= index && index < items[i].end) {
            return items[i];
        }
    }
    return (Rich_Text_Link){ .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
}

/**
 * Draws the bytes of text from start up to end on one line at x, y, with the parts in links,
 * a slice of Rich_Text_Link in order, in link_color and underlined. The links are of the whole
 * text, so that one that wraps onto the next line shows as a link on both.
 */
static void draw_text_links(Text_Style const *style, Str text, size_t start, size_t end,
                            Array_Slice links, float x, float y, Color color, Color link_color) {
    assert(start <= end);
    assert(end <= text.length);

    Rich_Text_Link const *items = links.items;
    size_t position = start;
    int32_t link_index = 0;
    // Bounded by the line: each pass draws up to a link's start or end, or the line's end.
    while (position < end) {
        while (link_index < links.count && items[link_index].end <= position) {
            link_index++;
        }
        bool in_link = false;
        size_t part_end = end;
        if (link_index < links.count) {
            Rich_Text_Link const link = items[link_index];
            if (link.start <= position) {
                in_link = true;
                part_end = link.end;
            } else {
                part_end = link.start;
            }
        }
        if (part_end > end) {
            part_end = end;
        }
        Str const part = str_slice(text, position, part_end);
        float const width = text_width(style, part);
        if (in_link) {
            draw_text(style, part, x, y, link_color);
            float const underline_y = y + style->size_pixels + 1.0f;
            DrawLineEx((Vector2){ x, underline_y }, (Vector2){ x + width, underline_y }, 1.0f,
                       link_color);
        } else {
            draw_text(style, part, x, y, color);
        }
        x += width;
        position = part_end;
    }
}

/**
 * Skips the spaces, line breaks and image markers at index, which a card or a preview does
 * not show. Returns the index after them.
 */
static size_t text_wrap_skip(Str text, size_t index) {
    // Bounded by the length: each pass moves index on by at least one byte.
    while (index < text.length) {
        if (rich_text_is_image_marker(text, index)) {
            index += RICH_TEXT_IMAGE_MARKER_SIZE;
        } else if (text.data[index] == ' ' || text.data[index] == '\n') {
            index++;
        } else {
            break;
        }
    }
    return index;
}

/**
 * The description's row among the form's fields: the description's box, and beside it the button
 * of the attachments, each opening a page of its own, where there is room.
 */
static void draw_panel_form_description_row(App *app, Rectangle row) {
    float const gap = 12;
    float const attachments_width = (float)ATTACHMENT_BUTTON_WIDTH_PIXELS;
    draw_panel_form_description_field(
        app, (Rectangle){ row.x, row.y, row.width - attachments_width - gap, row.height });
    draw_panel_form_attachments_field(
        app, (Rectangle){ row.x + row.width - attachments_width, row.y, attachments_width,
                          row.height });
}

/**
 * How many files the item has, in a box that opens the attachments' page.
 */
static void draw_panel_form_attachments_field(App *app, Rectangle box) {
    Form *form = &app->form;
    Fonts const *fonts = &app->fonts;
    Attachment_List const *attachments = &form->attachments;
    draw_text(&fonts->ui, STR("Attachments"), box.x, box.y - FORM_LABEL_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_NORMAL));
    bool const hovered = !GuiIsLocked() && CheckCollisionPointRec(GetMousePosition(), box);
    int32_t base_property = BASE_COLOR_NORMAL;
    int32_t border_property = BORDER_COLOR_NORMAL;
    if (hovered) {
        base_property = BASE_COLOR_FOCUSED;
        border_property = BORDER_COLOR_FOCUSED;
        app->cursor = APP_CURSOR_HAND;
    }
    DrawRectangleRec(box, style_color(base_property));
    DrawRectangleLinesEx(box, 1.0f, style_color(border_property));

    char text[TEXT_LINE_SIZE] = "";
    int32_t text_property = TEXT_COLOR_NORMAL;
    if (!attachments->loaded) {
        snprintf(text, sizeof(text), "Unavailable");
        text_property = TEXT_COLOR_DISABLED;
    } else if (attachments->count == 0) {
        snprintf(text, sizeof(text), "None");
        text_property = TEXT_COLOR_DISABLED;
    } else {
        attachments_format_count(attachments->count, text, sizeof(text));
    }
    scissor_begin(fonts, box.x, box.y, box.width, box.height);
    draw_text(&fonts->ui, str_from_cstr(text), box.x + DESCRIPTION_EDITOR_PADDING_PIXELS,
              box.y + (box.height - TEXT_SIZE_PIXELS) / 2, style_color(text_property));
    scissor_end(fonts);
    if (hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        attachments_open(form);
    }
}

/**
 * "1 file" or "3 files".
 */
static void attachments_format_count(int32_t count, char *buffer, size_t buffer_size) {
    assert(count > 0);

    char const *noun = "files";
    if (count == 1) {
        noun = "file";
    }
    snprintf(buffer, buffer_size, "%" PRId32 " %s", count, noun);
}

/**
 * "523 bytes", "12.3 KB" or "1.5 MB".
 */
static void attachments_format_size(int32_t size_bytes, char *buffer, size_t buffer_size) {
    assert(size_bytes >= 0);

    if (size_bytes < 1024) {
        snprintf(buffer, buffer_size, "%" PRId32 " bytes", size_bytes);
        return;
    }
    if (size_bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", (double)size_bytes / 1024.0);
        return;
    }
    snprintf(buffer, buffer_size, "%.1f MB", (double)size_bytes / (1024.0 * 1024.0));
}

/**
 * The form's page for the attachments, in place of its fields: the area to choose or drop a
 * file on, the files attached so far, a line of help or of what went wrong, and Done, which
 * goes back to the fields. Where the platform has no file access, there is no area, and the
 * files can only be looked at and removed.
 */
static void draw_panel_attachments(App *app, Rectangle body) {
    Form *form = &app->form;
    Attachment_List const *attachments = &form->attachments;
    assert(form->open);
    assert(attachments->open);

    draw_text(&app->fonts.ui, STR("Attachments"), body.x, body.y, style_color(TEXT_COLOR_NORMAL));
    float const buttons_y = body.y + body.height - BUTTON_HEIGHT_PIXELS;
    float const hint_y = buttons_y - FORM_ROW_GAP_PIXELS - TEXT_LINE_HEIGHT_PIXELS;
    float y = body.y + 32;
    if (attachments->loaded) {
        if (app->files != NULL) {
            Rectangle const zone = { body.x, y, body.width, ATTACHMENT_ZONE_HEIGHT_PIXELS };
            // A window too short for the area and a row shows the files alone.
            if (zone.y + zone.height + FORM_ROW_GAP_PIXELS + ATTACHMENT_ROW_HEIGHT_PIXELS <
                hint_y) {
                draw_panel_attachments_zone(app, zone);
                y += zone.height + FORM_ROW_GAP_PIXELS;
            }
        } else {
            draw_text(&app->fonts.small, STR("Files are added on the desktop."), body.x, y,
                      style_color(TEXT_COLOR_DISABLED));
            y += TEXT_LINE_HEIGHT_PIXELS_SMALL + FORM_ROW_GAP_PIXELS;
        }
        if (attachments->count == 0) {
            draw_text(&app->fonts.ui, STR("No files attached."), body.x, y,
                      style_color(TEXT_COLOR_DISABLED));
        }
    }
    Rectangle const list = { body.x, y, body.width, hint_y - FORM_ROW_GAP_PIXELS - y };
    if (attachments->count > 0 && list.height > 0.0f) {
        draw_panel_attachments_list(app, list);
    }
    draw_panel_attachments_hint(app, body, hint_y);
    if (GuiButton((Rectangle){ body.x, buttons_y, body.width, BUTTON_HEIGHT_PIXELS }, "Done")) {
        attachments_close(form);
    }
}

/**
 * What went wrong last, or else how the files are kept.
 */
static void draw_panel_attachments_hint(App const *app, Rectangle body, float y) {
    Form const *form = &app->form;
    Str hint = STR("Files are saved with the item.");
    Color color = style_color(TEXT_COLOR_DISABLED);
    if (!form->attachments.loaded) {
        hint = STR("Files not loaded: they are kept as they are.");
        color = ERROR_COLOR;
    }
    if (form->error[0] != '\0') {
        hint = str_from_cstr(form->error);
        color = ERROR_COLOR;
    }
    scissor_begin(&app->fonts, body.x, y, body.width, TEXT_LINE_HEIGHT_PIXELS);
    draw_text(&app->fonts.small, hint, body.x, y, color);
    scissor_end(&app->fonts);
}

/**
 * The area for adding a file: a click opens the system's dialog for choosing one, and files
 * dropped on the window are added whether or not they land on it (see app_take_dropped_files),
 * since the window system does not tell where above the app a drag is until it is dropped.
 * Replaced by a message once the item has as many files as it can.
 */
static void draw_panel_attachments_zone(App *app, Rectangle zone) {
    Attachment_List const *attachments = &app->form.attachments;
    Fonts const *fonts = &app->fonts;
    assert(app->files != NULL);

    bool const full = attachments->count >= ATTACHMENT_COUNT_MAX;
    bool const hovered = !GuiIsLocked() && !full && CheckCollisionPointRec(GetMousePosition(),
                                                                         zone);
    int32_t base_property = BASE_COLOR_NORMAL;
    int32_t border_property = BORDER_COLOR_FOCUSED;
    if (hovered) {
        base_property = BASE_COLOR_FOCUSED;
        border_property = BORDER_COLOR_PRESSED;
    }
    DrawRectangleRec(zone, style_color(base_property));
    draw_dashed_rectangle(zone, style_color(border_property));

    char label_text[TEXT_LINE_SIZE] = "Select file or drag and drop file here";
    int32_t text_property = TEXT_COLOR_NORMAL;
    if (full) {
        snprintf(label_text, sizeof(label_text), "At most %d files can be attached.",
                 (int)ATTACHMENT_COUNT_MAX);
        text_property = TEXT_COLOR_DISABLED;
    }
    scissor_begin(fonts, zone.x, zone.y, zone.width, zone.height);
    draw_text_centered(&fonts->ui, str_from_cstr(label_text), zone, style_color(text_property));
    scissor_end(fonts);
    if (hovered) {
        app->cursor = APP_CURSOR_HAND;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            app_attachment_choose(app);
        }
    }
}

/**
 * The outline of area as dashes, which says that something can be dropped in it.
 */
static void draw_dashed_rectangle(Rectangle area, Color color) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);

    float const dash = 8.0f;
    float const gap = 5.0f;
    Vector2 const corners[4] = {
        { area.x, area.y },
        { area.x + area.width, area.y },
        { area.x + area.width, area.y + area.height },
        { area.x, area.y + area.height },
    };
    for (int32_t side = 0; side < 4; side++) {
        Vector2 const from = corners[side];
        Vector2 const to = corners[(side + 1) % 4];
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const length = fabsf(dx) + fabsf(dy); // One of them is zero: the sides are straight.
        // Bounded by the side's length, which a window's size bounds: each pass moves on by a dash
        // and a gap.
        for (float at = 0.0f; at < length; at += dash + gap) {
            float const end = float_min(at + dash, length);
            DrawLineEx((Vector2){ from.x + dx * at / length, from.y + dy * at / length },
                       (Vector2){ from.x + dx * end / length, from.y + dy * end / length }, 1.5f,
                       color);
        }
    }
}

/**
 * The attached files as rows, scrolling when they do not fit. What a row asks for is done after
 * the rows are drawn, because the dialog it may show must not open in the middle of drawing
 * them, and removing a row would move the ones after it.
 */
static void draw_panel_attachments_list(App *app, Rectangle list) {
    Form *form = &app->form;
    Attachment_List *attachments = &form->attachments;
    assert(list.height > 0.0f);
    assert(attachments->count > 0);
    assert(attachments->count <= ATTACHMENT_COUNT_MAX);

    float const content_height = (float)attachments->count * ATTACHMENT_ROW_HEIGHT_PIXELS;
    if (content_height > list.height) {
        list.width -= SCROLLBAR_GAP_PIXELS;
    }
    scroll_update(&attachments->scroll, list, content_height, true);
    Scroll const *scroll = &attachments->scroll;
    // Controls are locked while the list is dragged, and while the pointer is outside it, so
    // that neither the end of a drag nor a click on a clipped row acts on a button.
    bool const locked = scroll->dragging || !CheckCollisionPointRec(GetMousePosition(), list);
    bool const locked_before = GuiIsLocked();
    if (locked) {
        GuiLock();
    }
    app->fonts.clip = list;
    app->fonts.clipping = true;
    scissor_begin(&app->fonts, list.x, list.y, list.width, list.height);
    int32_t action_index = -1;
    Attachment_Action action = ATTACHMENT_ACTION_NONE;
    for (int32_t i = 0; i < attachments->count; i++) {
        Rectangle const row = { list.x,
                                list.y - scroll->offset_pixels +
                                    (float)i * ATTACHMENT_ROW_HEIGHT_PIXELS,
                                list.width, ATTACHMENT_ROW_HEIGHT_PIXELS };
        // Rows scrolled wholly out of the list can be neither seen nor clicked.
        if (!CheckCollisionRecs(row, list)) {
            continue;
        }
        Attachment_Action const asked = draw_panel_attachments_row(app, &attachments->items[i],
                                                                   row);
        if (asked != ATTACHMENT_ACTION_NONE) {
            action = asked;
            action_index = i;
        }
    }
    app->fonts.clipping = false;
    EndScissorMode();
    if (locked && !locked_before) {
        GuiUnlock();
    }
    draw_scrollbar(list, content_height, scroll->offset_pixels);

    if (action == ATTACHMENT_ACTION_REMOVE) {
        assert(action_index >= 0);
        attachments_remove(attachments, action_index);
        form->error[0] = '\0';
    }
    if (action == ATTACHMENT_ACTION_SAVE) {
        assert(action_index >= 0);
        app_attachment_save(app, &attachments->items[action_index]);
    }
}

/**
 * One file: its name and size, and the buttons that save it out and remove it. Returns what the
 * user asked for with a button.
 */
static Attachment_Action draw_panel_attachments_row(App const *app, Attachment const *attachment,
                                                    Rectangle row) {
    Fonts const *fonts = &app->fonts;
    float const gap = 6;
    float const button_height = 28;
    float const button_y = row.y + (row.height - button_height) / 2;
    Rectangle const remove_button = { row.x + row.width - gap - ATTACHMENT_REMOVE_WIDTH_PIXELS,
                                      button_y, ATTACHMENT_REMOVE_WIDTH_PIXELS, button_height };
    Rectangle const save_button = { remove_button.x - gap - ATTACHMENT_SAVE_WIDTH_PIXELS,
                                    button_y, ATTACHMENT_SAVE_WIDTH_PIXELS, button_height };
    // Saving out needs the platform's dialog, which a phone does not have.
    bool const can_save = app->files != NULL;
    float text_right = remove_button.x - gap;
    if (can_save) {
        text_right = save_button.x - gap;
    }

    if (CheckCollisionPointRec(GetMousePosition(), row)) {
        DrawRectangleRec(row, style_color(BASE_COLOR_FOCUSED));
    }
    DrawRectangleRec((Rectangle){ row.x, row.y + row.height - 1, row.width, 1 },
                     style_color(BORDER_COLOR_NORMAL));
    float const text_x = row.x + gap;
    char size_text[TEXT_LINE_SIZE] = "";
    attachments_format_size(attachment->size_bytes, size_text, sizeof(size_text));
    scissor_begin(fonts, text_x, row.y, text_right - text_x, row.height);
    draw_text(&fonts->ui, str_from_cstr(attachment->name), text_x, row.y + 3,
              style_color(TEXT_COLOR_NORMAL));
    draw_text(&fonts->small, str_from_cstr(size_text), text_x, row.y + 3 + TEXT_LINE_HEIGHT_PIXELS,
              style_color(TEXT_COLOR_DISABLED));
    scissor_end(fonts);

    Attachment_Action action = ATTACHMENT_ACTION_NONE;
    if (can_save && GuiButton(save_button, "Save")) {
        action = ATTACHMENT_ACTION_SAVE;
    }
    if (GuiButton(remove_button, "Remove")) {
        action = ATTACHMENT_ACTION_REMOVE;
    }
    return action;
}

/**
 * Shows the attachments' page.
 */
static void attachments_open(Form *form) {
    Attachment_List *attachments = &form->attachments;
    assert(form->open);
    assert(!attachments->open);

    attachments->open = true;
    form->picker_open = false;
    form->field_active = FORM_FIELD_NONE;
    form->error[0] = '\0';
}

/**
 * Goes back to the form's fields; the files are kept for saving.
 */
static void attachments_close(Form *form) {
    assert(form->attachments.open);

    form->attachments.open = false;
}

/**
 * Takes the file at index out of the list, which the form's save then deletes; the later ones
 * move up. It is not deleted from the database before, so that cancelling the form keeps it.
 */
static void attachments_remove(Attachment_List *attachments, int32_t index) {
    assert(index >= 0);
    assert(index < attachments->count);

    int32_t const after = attachments->count - index - 1;
    memmove(&attachments->items[index], &attachments->items[index + 1],
            (size_t)after * sizeof(attachments->items[0]));
    attachments->count--;
    attachments->items[attachments->count] = (Attachment){ .id = 0 };
}

/**
 * Asks the system's dialog for a file, and adds it unless the user cancels.
 */
static void app_attachment_choose(App *app) {
    assert(app->files != NULL);

    char path[FILE_PATH_SIZE] = "";
    if (!app->files->choose(path, sizeof(path))) {
        return; // Cancelled.
    }
    app_attachment_add(app, str_from_cstr(path));
}

/**
 * Reads the file at path and stores it as an attachment of the form's item, which keeps it only
 * once the form is saved. When it cannot, the form says why.
 */
static void app_attachment_add(App *app, Str path) {
    Form *form = &app->form;
    Attachment_List *attachments = &form->attachments;
    assert(app->files != NULL);
    assert(attachments->loaded);

    if (attachments->count >= ATTACHMENT_COUNT_MAX) {
        snprintf(form->error, sizeof(form->error), "At most %d files can be attached.",
                 (int)ATTACHMENT_COUNT_MAX);
        return;
    }
    Str const name = path_file_name(path);
    if (!attachment_name_is_valid(name)) {
        snprintf(form->error, sizeof(form->error), "That file's name cannot be used.");
        return;
    }
    // The one place the path leaves as a C string: the operating system wants it that way.
    char c_path[FILE_PATH_SIZE] = "";
    if (!str_to_cstr(path, c_path, sizeof(c_path))) {
        snprintf(form->error, sizeof(form->error), "That file's path is too long.");
        return;
    }
    int32_t size = 0;
    File_Read_Status const status = app->files->read(
        c_path, array_slice_make(app->blob_bytes, ATTACHMENT_SIZE_BYTES_MAX), &size);
    switch (status) {
    case FILE_READ_FAILED:
        snprintf(form->error, sizeof(form->error), "That file cannot be read, or is a folder.");
        return;
    case FILE_READ_OK:
        break;
    case FILE_READ_TOO_LARGE:
        snprintf(form->error, sizeof(form->error), "That file is larger than %d MB.",
                 (int)(ATTACHMENT_SIZE_BYTES_MAX / (1024 * 1024)));
        return;
    }
    if (size == 0) {
        snprintf(form->error, sizeof(form->error), "That file is empty.");
        return;
    }
    int64_t const id = backend_attachment_insert(app->backend, name,
                                                 array_slice_make(app->blob_bytes, size));
    if (id == 0) {
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    Attachment *added = &attachments->items[attachments->count];
    *added = (Attachment){ .id = id, .size_bytes = size };
    // The name fits: attachment_name_is_valid bounds it by the field's size.
    (void)str_copy_truncated(name, added->name, sizeof(added->name));
    attachments->count++;
    form->error[0] = '\0';
}

/**
 * Asks where to save the file, and writes it there. The name offered is the file's own. When it
 * cannot be done, the form says why.
 */
static void app_attachment_save(App *app, Attachment const *attachment) {
    Form *form = &app->form;
    assert(app->files != NULL);
    assert(attachment->id > 0);

    char path[FILE_PATH_SIZE] = "";
    if (!app->files->save_as(str_from_cstr(attachment->name), path, sizeof(path))) {
        return; // Cancelled.
    }
    int32_t const size = backend_attachment_load(
        app->backend, attachment->id, array_slice_make(app->blob_bytes, ATTACHMENT_SIZE_BYTES_MAX));
    if (size < 0) {
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    if (!app->files->write(path, array_slice_make(app->blob_bytes, size))) {
        snprintf(form->error, sizeof(form->error), "The file could not be written there.");
        return;
    }
    form->error[0] = '\0';
    app_set_status(app, STR("Saved the file."));
}

/**
 * The name of the file at path: what follows the last folder separator, of either kind.
 */
static Str path_file_name(Str path) {
    size_t start = 0;
    // Bounded by the length: each pass moves i on by a byte.
    for (size_t i = 0; i < path.length; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            start = i + 1;
        }
    }
    return str_slice(path, start, path.length);
}

/**
 * Loads the item's files, without their contents. A failure to list them shows in the form and
 * leaves loaded false, so that saving cannot delete files that were never shown.
 */
static void app_form_load_attachments(App *app, int64_t event_id) {
    Form *form = &app->form;
    Attachment_List *attachments = &form->attachments;
    assert(event_id > 0);
    assert(attachments->count == 0);

    int32_t const count = backend_load_event_attachments(
        app->backend, event_id, array_slice_make(attachments->items, ATTACHMENT_COUNT_MAX));
    if (count < 0) {
        attachments->loaded = false;
        backend_error_copy(app->backend, form->error, sizeof(form->error));
        return;
    }
    attachments->count = count;
}

/**
 * Gives the saved item its files, in their order; those it had and no longer shows are deleted.
 * Skipped when they could not be loaded, which keeps them as they are, and for a new item
 * without files, which has none to delete.
 */
static bool app_form_save_attachments(App *app, int64_t id, bool inserted) {
    Attachment_List const *attachments = &app->form.attachments;
    assert(id > 0);

    if (!attachments->loaded) {
        return true;
    }
    if (inserted && attachments->count == 0) {
        return true;
    }
    int64_t ids[ATTACHMENT_COUNT_MAX] = { 0 };
    for (int32_t i = 0; i < attachments->count; i++) {
        ids[i] = attachments->items[i].id;
    }
    if (!backend_set_event_attachments(app->backend, id,
                                       array_slice_make(ids, attachments->count))) {
        backend_error_copy(app->backend, app->form.error, sizeof(app->form.error));
        return false;
    }
    return true;
}

/**
 * Loads THEME_COLORS into raygui. DEFAULT's base properties reach every control, so that
 * buttons, text boxes and toggles match the surfaces the apps draw themselves.
 */
static void theme_apply(void) {
    for (int32_t i = 0; i < THEME_COLOR_COUNT; i++) {
        Theme_Color const *color = &THEME_COLORS[i];
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

/**
 * Positions are rounded to whole screen pixels, so that glyphs, rasterized at the screen
 * scale, map one to one onto screen pixels and stay sharp. A position may be left of or above
 * the screen, as in the phone's drawer while it slides in.
 */
static void draw_text(Text_Style const *style, Str text, float x, float y, Color color) {
    // Far beyond any screen, which would also overflow the rounding.
    assert(x > -(float)TEXT_POSITION_LIMIT_PIXELS);
    assert(x < (float)TEXT_POSITION_LIMIT_PIXELS);
    assert(y > -(float)TEXT_POSITION_LIMIT_PIXELS);
    assert(y < (float)TEXT_POSITION_LIMIT_PIXELS);
    float const scale = style->scale;
    Vector2 position = { draw_text_round(x, scale), draw_text_round(y, scale) };
    // The one place a Str meets raylib's drawing: it takes a C string.
    char c_text[TEXT_DRAW_SIZE_MAX] = "";
    (void)str_copy_truncated(text, c_text, sizeof(c_text)); // Cut at a whole character if long.
    DrawTextEx(style->font, c_text, position, style->size_pixels,
               (float)GuiGetStyle(DEFAULT, TEXT_SPACING), color);
}

/**
 * Rounds a layout position to the nearest whole screen pixel. Casting truncates toward zero,
 * so negative positions are moved up by the limit first, which keeps the cast's input positive.
 */
static float draw_text_round(float position, float scale) {
    assert(scale > 0.0f);

    float const shift = (float)TEXT_POSITION_LIMIT_PIXELS * scale;
    float screen = position * scale + shift + 0.5f;
    assert(screen >= 0.0f);
    return ((float)(int32_t)screen - shift) / scale;
}

static Vector2 measure_text(Text_Style const *style, Str text) {
    // As in draw_text, the same cut, so that the measure is of what is drawn.
    char c_text[TEXT_DRAW_SIZE_MAX] = "";
    (void)str_copy_truncated(text, c_text, sizeof(c_text));
    return MeasureTextEx(style->font, c_text, style->size_pixels,
                         (float)GuiGetStyle(DEFAULT, TEXT_SPACING));
}

static void draw_text_centered(Text_Style const *style, Str text, Rectangle area,
                               Color color) {
    Vector2 text_size = measure_text(style, text);
    draw_text(style, text, area.x + (area.width - text_size.x) / 2,
              area.y + (area.height - text_size.y) / 2, color);
}

/**
 * raylib's scissor rectangle is in screen pixels, so the layout rectangle is scaled to match.
 * Within a clip, such as the panel's scrolling list, only the part inside the clip is drawn.
 */
static void scissor_begin(Fonts const *fonts, float x, float y, float width, float height) {
    assert(width >= 0.0f);
    assert(height >= 0.0f);

    Rectangle area = { x, y, width, height };
    if (fonts->clipping) {
        // An empty overlap clips everything, which is right for text wholly outside the clip.
        area = GetCollisionRec(area, fonts->clip);
    }
    float const scale = fonts->scale;
    BeginScissorMode((int)(fonts->origin.x + area.x * scale),
                     (int)(fonts->origin.y + area.y * scale), (int)(area.width * scale),
                     (int)(area.height * scale));
}

/**
 * Ends a scissor_begin. Within a clip, the clip's own scissor rectangle comes back, because
 * raylib cannot nest them.
 */
static void scissor_end(Fonts const *fonts) {
    if (fonts->clipping) {
        assert(fonts->clip.width > 0.0f);
        assert(fonts->clip.height > 0.0f);
        scissor_begin(fonts, fonts->clip.x, fonts->clip.y, fonts->clip.width,
                      fonts->clip.height);
    } else {
        EndScissorMode();
    }
}
