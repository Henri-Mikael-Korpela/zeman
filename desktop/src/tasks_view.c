#include <zeman/tasks_view.h>

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "raygui.h"

enum {
    // The calendar's padding, header and weekday heights, so that the columns sit where the
    // calendar's days do, as in the Problems view.
    PADDING_PIXELS = 16,
    HEADER_HEIGHT_PIXELS = 40,
    COLUMN_HEADER_HEIGHT_PIXELS = 32,
    // Above the first bar in a column; a day cell has its day number there instead.
    COLUMN_TOP_PIXELS = 8,
    // Room for the longest group name, a count and the "showing" note.
    LABEL_SIZE = 96,
    // Right of the checkbox, still counted as a click on it, so that it is easy to hit.
    CHECK_BOX_MARGIN_PIXELS = 4,
    // As tall as the text boxes and buttons of the calendar's form and the Settings view.
    SEARCH_HEIGHT_PIXELS = 32,
    SEARCH_GAP_PIXELS = 8,
    // Room for "YYYY-MM-DD" and "HH:MM" with the box's padding, and no more, so that the name
    // gets the rest of the row.
    SEARCH_DATE_WIDTH_PIXELS = 116,
    SEARCH_TIME_WIDTH_PIXELS = 68,
    SEARCH_BUTTON_WIDTH_PIXELS = 80,
    // Where a text box's own text starts and the calendar's text size, so that a hint drawn in an
    // empty box sits where typed text will.
    SEARCH_TEXT_INSET_PIXELS = 6,
    SEARCH_TEXT_HEIGHT_PIXELS = 18,
};

// Column headings, indexed by Task_Group.
static char const *const TASK_GROUP_NAMES[TASK_GROUP_COUNT] = { "Overdue", "Today", "Upcoming",
                                                                "Completed" };

// What an empty box of the search bar shows, so that the user sees what to type, indexed by
// Tasks_View_Field.
static char const *const SEARCH_HINTS[TASKS_VIEW_FIELD_COUNT] = { "Search by name", "YYYY-MM-DD",
                                                                  "HH:MM", "YYYY-MM-DD",
                                                                  "HH:MM" };

// The task under the pointer while drawing, acted on once every column is drawn.
typedef struct Tasks_View_Hit {
    int32_t index;    // Into Tasks_View.tasks, or -1 for none
    bool check_box;   // Over the task's checkbox rather than the rest of its bar
} Tasks_View_Hit;

// The search bar read into what a task must match, once a frame. Times are minutes since
// 1970-01-01, so that a date and a time compare as one number; a bound left empty is the end
// of the range of int64_t, which every task is within.
typedef struct Tasks_View_Filter {
    int64_t from_minute;  // The task must end at or after it
    int64_t to_minute;    // The task must start at or before it
    Str name;             // Case-folded, see tasks_view_fold; empty matches every title
    Str problem;          // Why a box could not be read, for the header; empty when none
} Tasks_View_Filter;

static void tasks_view_load(Tasks_View *view, Backend *backend, uint64_t revision, Date today);
static void tasks_view_draw_header(Tasks_View const *view, App const *app, Str problem, float x,
                                   float y, float width);
static void tasks_view_draw_search(Tasks_View *view, App const *app, float x, float y,
                                   float width);
static float tasks_view_draw_search_field(Tasks_View *view, App const *app, int32_t field,
                                          Rectangle box);
static Tasks_View_Filter tasks_view_filter_read(Tasks_View const *view, char *name_buffer,
                                                size_t name_capacity);
static Str tasks_view_filter_bound(Str date_text, Str time_text, char const *date_problem,
                                   char const *time_problem, int32_t minute_default,
                                   int64_t *minute);
static bool tasks_view_matches(Task_Occurrence const *occurrence, Tasks_View_Filter const *filter);
static Str tasks_view_fold(Str text, char *buffer, size_t capacity);
static Tasks_View_Hit tasks_view_draw_column(Tasks_View const *view, App const *app,
                                             Tasks_View_Filter const *filter, Task_Group group,
                                             int32_t count, Rectangle header, Rectangle cell);
static Tasks_View_Choice tasks_view_click(Tasks_View *view, Backend *backend, Tasks_View_Hit hit);
static Task_Group tasks_view_group(Task_Occurrence const *occurrence, Date today);

void tasks_view_init(Tasks_View *view) {
    view->revision = 0;
    view->today = date_today();
    view->task_count = 0;
    view->field_active = TASKS_VIEW_FIELD_NONE;
    view->loaded = false;
    view->truncated = false;
    view->error[0] = '\0';
    // Every box empty: no limit on the name or the dates, so that every task shows at first.
    for (int32_t field = 0; field < TASKS_VIEW_FIELD_COUNT; field++) {
        view->search[field][0] = '\0';
    }
}

Tasks_View_Choice tasks_view_draw(Tasks_View *view, Backend *backend, App const *app,
                                  Rectangle area) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);

    // Checking a task off anywhere changes the revision; a new day moves tasks along, from
    // Upcoming to Today to Overdue, and a repeating task on to its next occurrence.
    uint64_t const revision = backend_revision(backend);
    Date const today = date_today();
    if (!view->loaded || revision != view->revision || !date_equal(today, view->today)) {
        tasks_view_load(view, backend, revision, today);
    }

    float const x = area.x + PADDING_PIXELS;
    float const width = area.width - 2 * PADDING_PIXELS;
    float y = area.y + PADDING_PIXELS;
    // The search bar before the filter is read, so that what was typed this frame shows at once.
    float const search_y = y + HEADER_HEIGHT_PIXELS + PADDING_PIXELS / 2;
    tasks_view_draw_search(view, app, x, search_y, width);
    char name_buffer[EVENT_TITLE_SIZE] = "";
    Tasks_View_Filter const filter = tasks_view_filter_read(view, name_buffer,
                                                            sizeof(name_buffer));
    tasks_view_draw_header(view, app, filter.problem, x, y, width);
    y = search_y + SEARCH_HEIGHT_PIXELS + PADDING_PIXELS / 2;

    // One pass for the counts that the headers show, of the tasks the search finds.
    int32_t counts[TASK_GROUP_COUNT] = { 0 };
    for (int32_t i = 0; i < view->task_count; i++) {
        if (tasks_view_matches(&view->tasks[i], &filter)) {
            counts[tasks_view_group(&view->tasks[i], view->today)]++;
        }
    }
    float const column_width = width / (float)TASK_GROUP_COUNT;
    float const cell_y = y + COLUMN_HEADER_HEIGHT_PIXELS;
    float const cell_height = area.y + area.height - PADDING_PIXELS - cell_y;
    Tasks_View_Hit hit = { .index = -1, .check_box = false };
    // Left to right from the most pressing, as the groups are ordered.
    for (int32_t group = 0; group < TASK_GROUP_COUNT; group++) {
        float const column_x = x + (float)group * column_width;
        Rectangle header = { column_x, y, column_width, COLUMN_HEADER_HEIGHT_PIXELS };
        Rectangle cell = { column_x, cell_y, column_width, cell_height };
        Tasks_View_Hit column_hit = tasks_view_draw_column(view, app, &filter, (Task_Group)group,
                                                           counts[group], header, cell);
        if (column_hit.index >= 0) {
            hit = column_hit;
        }
    }
    // After drawing, because a check-off reloads the tasks that the columns draw from.
    return tasks_view_click(view, backend, hit);
}

/**
 * Marked loaded either way, so that a failure is retried at the next change, not every frame,
 * as the calendar does.
 */
static void tasks_view_load(Tasks_View *view, Backend *backend, uint64_t revision, Date today) {
    assert(date_is_valid(today));

    view->revision = revision;
    view->today = today;
    view->task_count = 0;
    view->truncated = false;
    view->error[0] = '\0';
    view->loaded = true;
    int32_t count = backend_load_tasks(backend, today,
                                       array_slice_make(view->tasks, TASKS_VIEW_COUNT_MAX),
                                       &view->truncated);
    if (count < 0) {
        backend_error_copy(backend, view->error, sizeof(view->error));
        return;
    }
    assert(count <= TASKS_VIEW_COUNT_MAX);
    view->task_count = count;
}

/**
 * The heading, and at the right what went wrong, else why the search cannot be read, else that
 * some tasks are left out. The columns stay below a note, so that a failed check-off or a date
 * half typed does not hide the tasks.
 */
static void tasks_view_draw_header(Tasks_View const *view, App const *app, Str problem, float x,
                                   float y, float width) {
    assert(width > 0.0f);

    app_draw_text(app, APP_TEXT_HEADING, STR("Tasks"), x, y + 4);
    char text[LABEL_SIZE] = "";
    Str note = str_from_cstr(view->error);
    if (note.length == 0) {
        note = problem;
    }
    if (note.length == 0 && view->truncated) {
        snprintf(text, sizeof(text), "Showing %d tasks; the oldest are left out.",
                 TASKS_VIEW_COUNT_MAX);
        note = str_from_cstr(text);
    }
    if (note.length == 0) {
        return;
    }
    float const note_width = app_measure_text(app, APP_TEXT_DIM, note);
    app_draw_text(app, APP_TEXT_DIM, note, x + width - note_width, y + 10);
}

/**
 * One row: the name, which takes the room left, then "From" and "To", each a date and a time,
 * and a button that empties every box, which lifts every limit again.
 */
static void tasks_view_draw_search(Tasks_View *view, App const *app, float x, float y,
                                   float width) {
    assert(width > 0.0f);

    float const gap = (float)SEARCH_GAP_PIXELS;
    float const from_width = app_measure_text(app, APP_TEXT_NORMAL, STR("From"));
    float const to_width = app_measure_text(app, APP_TEXT_NORMAL, STR("To"));
    float const bound_width = SEARCH_DATE_WIDTH_PIXELS + gap + SEARCH_TIME_WIDTH_PIXELS;
    float const fixed_width = 2 * gap + from_width + gap + bound_width + 2 * gap + to_width +
                              gap + bound_width + gap + SEARCH_BUTTON_WIDTH_PIXELS;
    // Never narrower than a date box, so that a narrow window still leaves room to type in it.
    float const name_width = fmaxf(width - fixed_width, (float)SEARCH_DATE_WIDTH_PIXELS);
    float const height = (float)SEARCH_HEIGHT_PIXELS;
    float const label_y = y + (height - SEARCH_TEXT_HEIGHT_PIXELS) / 2;

    float left = x;
    left += tasks_view_draw_search_field(view, app, TASKS_VIEW_FIELD_NAME,
                                         (Rectangle){ left, y, name_width, height });
    left += 2 * gap;
    app_draw_text(app, APP_TEXT_NORMAL, STR("From"), left, label_y);
    left += from_width + gap;
    left += tasks_view_draw_search_field(view, app, TASKS_VIEW_FIELD_FROM_DATE,
                                         (Rectangle){ left, y, SEARCH_DATE_WIDTH_PIXELS, height });
    left += gap;
    left += tasks_view_draw_search_field(view, app, TASKS_VIEW_FIELD_FROM_TIME,
                                         (Rectangle){ left, y, SEARCH_TIME_WIDTH_PIXELS, height });
    left += 2 * gap;
    app_draw_text(app, APP_TEXT_NORMAL, STR("To"), left, label_y);
    left += to_width + gap;
    left += tasks_view_draw_search_field(view, app, TASKS_VIEW_FIELD_TO_DATE,
                                         (Rectangle){ left, y, SEARCH_DATE_WIDTH_PIXELS, height });
    left += gap;
    left += tasks_view_draw_search_field(view, app, TASKS_VIEW_FIELD_TO_TIME,
                                         (Rectangle){ left, y, SEARCH_TIME_WIDTH_PIXELS, height });
    left += gap;
    if (GuiButton((Rectangle){ left, y, SEARCH_BUTTON_WIDTH_PIXELS, height }, "Clear")) {
        for (int32_t field = 0; field < TASKS_VIEW_FIELD_COUNT; field++) {
            view->search[field][0] = '\0';
        }
        view->field_active = TASKS_VIEW_FIELD_NONE;
    }
}

/**
 * One text box of the search bar, with its hint while it is empty and not being edited. Returns
 * its width, for the next box to start after it.
 */
static float tasks_view_draw_search_field(Tasks_View *view, App const *app, int32_t field,
                                          Rectangle box) {
    assert(field >= 0);
    assert(field < TASKS_VIEW_FIELD_COUNT);

    bool const editing = view->field_active == field;
    // raygui returns true when edit mode should toggle: a click on the box, or Enter or a click
    // elsewhere while editing, as in the calendar's form.
    if (GuiTextBox(box, view->search[field], (int)sizeof(view->search[field]), editing)) {
        if (editing) {
            view->field_active = TASKS_VIEW_FIELD_NONE;
        } else {
            view->field_active = field;
        }
    }
    if (view->search[field][0] == '\0' && view->field_active != field) {
        app_draw_text(app, APP_TEXT_DIM, str_from_cstr(SEARCH_HINTS[field]),
                      box.x + SEARCH_TEXT_INSET_PIXELS,
                      box.y + (box.height - SEARCH_TEXT_HEIGHT_PIXELS) / 2);
    }
    return box.width;
}

/**
 * The search bar as a filter. The range takes in a whole day at each end when its time is left
 * out: From at 00:00 and To at 23:59. A box that cannot be read sets no limit, and says why.
 */
static Tasks_View_Filter tasks_view_filter_read(Tasks_View const *view, char *name_buffer,
                                                size_t name_capacity) {
    assert(name_capacity > 0);

    Tasks_View_Filter filter = { .from_minute = INT64_MIN, .to_minute = INT64_MAX,
                                 .name = STR(""), .problem = STR("") };
    Str const spaces = STR(" ");
    Str const name = str_trim_any(str_from_cstr(view->search[TASKS_VIEW_FIELD_NAME]), spaces);
    filter.name = tasks_view_fold(name, name_buffer, name_capacity);
    Str const from_problem = tasks_view_filter_bound(
        str_trim_any(str_from_cstr(view->search[TASKS_VIEW_FIELD_FROM_DATE]), spaces),
        str_trim_any(str_from_cstr(view->search[TASKS_VIEW_FIELD_FROM_TIME]), spaces),
        "From date is not YYYY-MM-DD.", "From time is not HH:MM, or has no date.", 0,
        &filter.from_minute);
    Str const to_problem = tasks_view_filter_bound(
        str_trim_any(str_from_cstr(view->search[TASKS_VIEW_FIELD_TO_DATE]), spaces),
        str_trim_any(str_from_cstr(view->search[TASKS_VIEW_FIELD_TO_TIME]), spaces),
        "To date is not YYYY-MM-DD.", "To time is not HH:MM, or has no date.",
        MINUTES_PER_DAY - 1, &filter.to_minute);
    filter.problem = from_problem;
    if (filter.problem.length == 0) {
        filter.problem = to_problem;
    }
    // Both read, yet nothing can be found: say so, rather than show empty columns unexplained.
    if (filter.problem.length == 0 && filter.from_minute > filter.to_minute) {
        filter.problem = STR("From is after To.");
    }
    return filter;
}

/**
 * Sets *minute to the date and time typed, with minute_default as the time when none is typed.
 * Leaves *minute, an open end, when both are empty. Returns why they cannot be read, from the
 * messages given, or an empty Str; a time without a date cannot be, since it has no day.
 */
static Str tasks_view_filter_bound(Str date_text, Str time_text, char const *date_problem,
                                   char const *time_problem, int32_t minute_default,
                                   int64_t *minute) {
    assert(minute_default >= 0);
    assert(minute_default < MINUTES_PER_DAY);

    if (date_text.length == 0 && time_text.length == 0) {
        return STR("");
    }
    Date date = { 0 };
    if (!date_parse_iso(date_text, &date)) {
        if (date_text.length == 0) {
            return str_from_cstr(time_problem);
        }
        return str_from_cstr(date_problem);
    }
    int32_t minute_of_day = minute_default;
    if (time_text.length > 0 && !time_parse(time_text, &minute_of_day)) {
        return str_from_cstr(time_problem);
    }
    assert(minute_of_day >= 0);
    assert(minute_of_day < MINUTES_PER_DAY);
    *minute = (int64_t)date_to_day_number(date) * MINUTES_PER_DAY + minute_of_day;
    return STR("");
}

/**
 * Whether the occurrence shows: its title holds the name, in any case, and it overlaps the
 * range, from its start to its end. A day without a time counts from 00:00 to 23:59, so that a
 * task of the day is found by any time within it.
 */
static bool tasks_view_matches(Task_Occurrence const *occurrence, Tasks_View_Filter const *filter) {
    Event const *task = &occurrence->task;
    if (filter->name.length > 0) {
        char title_buffer[EVENT_TITLE_SIZE] = "";
        Str const title = tasks_view_fold(str_from_cstr(task->title), title_buffer,
                                          sizeof(title_buffer));
        if (str_find(title, filter->name) == STR_NOT_FOUND) {
            return false;
        }
    }
    int32_t start_minute = 0;
    if (task->start_minute != TIME_NONE) {
        start_minute = task->start_minute;
    }
    int32_t end_minute = MINUTES_PER_DAY - 1;
    if (task->end_minute != TIME_NONE) {
        end_minute = task->end_minute;
    }
    int32_t const end_day = occurrence->start_day + event_duration_days(task);
    int64_t const start = (int64_t)occurrence->start_day * MINUTES_PER_DAY + start_minute;
    int64_t const end = (int64_t)end_day * MINUTES_PER_DAY + end_minute;
    if (start > filter->to_minute) {
        return false;
    }
    return end >= filter->from_minute;
}

/**
 * text in lower case, copied into buffer, so that a search finds a title in any case: A to Z,
 * and the capitals of Latin-1 in UTF-8, such as A and O with umlauts, which Finnish titles use.
 * Other bytes are copied as they are. Cut at capacity, as both texts fit in a title's size.
 */
static Str tasks_view_fold(Str text, char *buffer, size_t capacity) {
    assert(capacity > 0);

    size_t length = text.length;
    if (length > capacity) {
        length = capacity;
    }
    // Latin-1's capitals are U+00C0 to U+00DE, except U+00D7, the multiplication sign; in UTF-8
    // 0xC3 then 0x80 to 0x9E, and their small letters 0x20 above.
    unsigned char const lead_latin1 = 0xC3;
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)text.data[i];
        if (byte >= 'A' && byte <= 'Z') {
            byte = (unsigned char)(byte + ('a' - 'A'));
        }
        bool const after_lead = i > 0 && (unsigned char)text.data[i - 1] == lead_latin1;
        if (after_lead && byte >= 0x80 && byte <= 0x9E && byte != 0x97) {
            byte = (unsigned char)(byte + 0x20);
        }
        buffer[i] = (char)byte;
    }
    return str_make(buffer, length);
}

/**
 * The group and its count above, and its tasks as bars in a cell below, like a weekday name
 * above its day. When not all fit, the last line counts the rest. Returns the task under the
 * pointer, if it is in this column.
 */
static Tasks_View_Hit tasks_view_draw_column(Tasks_View const *view, App const *app,
                                             Tasks_View_Filter const *filter, Task_Group group,
                                             int32_t count, Rectangle header, Rectangle cell) {
    assert(count >= 0);
    assert(count <= view->task_count);

    char text[LABEL_SIZE] = "";
    snprintf(text, sizeof(text), "%s (%" PRId32 ")", TASK_GROUP_NAMES[group], count);
    Str const label = str_from_cstr(text);
    float const text_width = app_measure_text(app, APP_TEXT_NORMAL, label);
    app_draw_text(app, APP_TEXT_NORMAL, label, header.x + (header.width - text_width) / 2,
                  header.y + 4);
    DrawRectangleRec(cell, GetColor((unsigned int)GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL)));
    DrawRectangleLinesEx(cell, 1, GetColor((unsigned int)GuiGetStyle(DEFAULT,
                                                                     BORDER_COLOR_NORMAL)));

    Tasks_View_Hit hit = { .index = -1, .check_box = false };
    float const slot_height = APP_BAR_HEIGHT_PIXELS + APP_BAR_GAP_PIXELS;
    float const top = cell.y + COLUMN_TOP_PIXELS;
    int32_t const slot_count = (int32_t)((cell.y + cell.height - 4 - top) / slot_height);
    if (slot_count <= 0) {
        return hit;
    }
    int32_t bar_count = count;
    if (count > slot_count) {
        bar_count = slot_count - 1;
    }
    Vector2 const mouse = GetMousePosition();
    int32_t bar_index = 0;
    for (int32_t i = 0; i < view->task_count && bar_index < bar_count; i++) {
        // The tasks are in start order; the completed ones go the other way, so that those
        // just checked off are at the top rather than cut off at the bottom.
        int32_t index = i;
        if (group == TASK_GROUP_DONE) {
            index = view->task_count - 1 - i;
        }
        Task_Occurrence const *occurrence = &view->tasks[index];
        if (tasks_view_group(occurrence, view->today) != group) {
            continue;
        }
        if (!tasks_view_matches(occurrence, filter)) {
            continue;
        }
        // Inset as a one-day bar is in its day cell.
        Rectangle bar = { cell.x + 4, top + (float)bar_index * slot_height, cell.width - 8,
                          APP_BAR_HEIGHT_PIXELS };
        Rectangle const box = app_draw_task_bar(app, occurrence, bar);
        if (CheckCollisionPointRec(mouse, bar)) {
            float const box_right = box.x + box.width + CHECK_BOX_MARGIN_PIXELS;
            hit = (Tasks_View_Hit){ .index = index, .check_box = mouse.x < box_right };
        }
        bar_index++;
    }
    if (count > bar_count) {
        snprintf(text, sizeof(text), "+%" PRId32 " more", count - bar_count);
        app_draw_text(app, APP_TEXT_SMALL, str_from_cstr(text), cell.x + 6,
                      top + (float)bar_count * slot_height + 2);
    }
    return hit;
}

/**
 * A press on a checkbox checks that occurrence off, or back on, and the tasks reload with the
 * revision it raises; a press on the rest of a bar chooses the task, for the calendar to show.
 */
static Tasks_View_Choice tasks_view_click(Tasks_View *view, Backend *backend, Tasks_View_Hit hit) {
    assert(hit.index >= -1);
    assert(hit.index < view->task_count);

    Tasks_View_Choice choice = { .date = view->today, .task_id = 0, .hovered = hit.index >= 0 };
    if (hit.index < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return choice;
    }
    Task_Occurrence const *occurrence = &view->tasks[hit.index];
    Date const start = date_from_day_number(occurrence->start_day);
    if (!hit.check_box) {
        choice.date = start;
        choice.task_id = occurrence->task.id;
        return choice;
    }
    view->error[0] = '\0';
    if (!backend_set_task_done(backend, occurrence->task.id, start, !occurrence->done)) {
        backend_error_copy(backend, view->error, sizeof(view->error));
    }
    return choice;
}

static Task_Group tasks_view_group(Task_Occurrence const *occurrence, Date today) {
    assert(occurrence->task.kind == EVENT_KIND_TASK);

    if (occurrence->done) {
        return TASK_GROUP_DONE;
    }
    int32_t const today_day = date_to_day_number(today);
    if (occurrence->start_day > today_day) {
        return TASK_GROUP_UPCOMING;
    }
    if (occurrence->start_day + event_duration_days(&occurrence->task) < today_day) {
        return TASK_GROUP_OVERDUE;
    }
    return TASK_GROUP_TODAY;
}
