#include <zeman/tasks_view.h>

#include <assert.h>
#include <inttypes.h>
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
};

// Column headings, indexed by Task_Group.
static char const *const task_group_names[TASK_GROUP_COUNT] = { "Overdue", "Today", "Upcoming",
                                                                "Completed" };

// The task under the pointer while drawing, acted on once every column is drawn.
typedef struct Tasks_View_Hit {
    int32_t index;    // Into Tasks_View.tasks, or -1 for none
    bool check_box;   // Over the task's checkbox rather than the rest of its bar
} Tasks_View_Hit;

static void tasks_view_load(Tasks_View *view, Backend *backend, uint64_t revision, Date today);
static void tasks_view_draw_header(Tasks_View const *view, App const *app, float x, float y,
                                   float width);
static Tasks_View_Hit tasks_view_draw_column(Tasks_View const *view, App const *app,
                                             Task_Group group, int32_t count, Rectangle header,
                                             Rectangle cell);
static Tasks_View_Choice tasks_view_click(Tasks_View *view, Backend *backend, Tasks_View_Hit hit);
static Task_Group tasks_view_group(Task_Occurrence const *occurrence, Date today);

void tasks_view_init(Tasks_View *view) {
    view->revision = 0;
    view->today = date_today();
    view->task_count = 0;
    view->loaded = false;
    view->truncated = false;
    view->error[0] = '\0';
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
    tasks_view_draw_header(view, app, x, y, width);
    y += HEADER_HEIGHT_PIXELS + PADDING_PIXELS / 2;

    // One pass for the counts that the headers show.
    int32_t counts[TASK_GROUP_COUNT] = { 0 };
    for (int32_t i = 0; i < view->task_count; i++) {
        counts[tasks_view_group(&view->tasks[i], view->today)]++;
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
        Tasks_View_Hit column_hit = tasks_view_draw_column(view, app, (Task_Group)group,
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
 * The heading, and at the right what went wrong or, failing that, that some tasks are left out.
 * The columns stay below an error, so that a failed check-off does not hide the tasks.
 */
static void tasks_view_draw_header(Tasks_View const *view, App const *app, float x, float y,
                                   float width) {
    assert(width > 0.0f);

    app_draw_text(app, APP_TEXT_HEADING, STR("Tasks"), x, y + 4);
    char text[LABEL_SIZE] = "";
    Str note = str_from_cstr(view->error);
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
 * The group and its count above, and its tasks as bars in a cell below, like a weekday name
 * above its day. When not all fit, the last line counts the rest. Returns the task under the
 * pointer, if it is in this column.
 */
static Tasks_View_Hit tasks_view_draw_column(Tasks_View const *view, App const *app,
                                             Task_Group group, int32_t count, Rectangle header,
                                             Rectangle cell) {
    assert(count >= 0);
    assert(count <= view->task_count);

    char text[LABEL_SIZE] = "";
    snprintf(text, sizeof(text), "%s (%" PRId32 ")", task_group_names[group], count);
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
