#include <zeman/problems_view.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "raygui.h"

enum {
    // The calendar's padding, header and weekday heights, so that the columns sit where the
    // calendar's days do.
    PADDING_PIXELS = 16,
    HEADER_HEIGHT_PIXELS = 40,
    COLUMN_HEADER_HEIGHT_PIXELS = 32,
    // Above the first bar in a column; a day cell has its day number there instead.
    COLUMN_TOP_PIXELS = 8,
    // Room for the longest status name, a count and the "showing" note.
    LABEL_SIZE = 96,
};

static void problems_view_load(Problems_View *view, Backend *backend, uint64_t revision);
static void problems_view_draw_column(const Problems_View *view, const App *app,
                                      Target_Status status, int32_t count, Rectangle header,
                                      Rectangle cell);
static Target_Status problems_view_status(const Link_Target *problem);

void problems_view_init(Problems_View *view) {
    view->revision = 0;
    view->problem_count = 0;
    view->loaded = false;
    view->truncated = false;
    view->error[0] = '\0';
}

void problems_view_draw(Problems_View *view, Backend *backend, const App *app, Rectangle area) {
    assert(area.width > 0.0f);
    assert(area.height > 0.0f);
    // Checking a task off anywhere changes the revision, and moves its problems along.
    const uint64_t revision = backend_revision(backend);
    if (!view->loaded || revision != view->revision) {
        problems_view_load(view, backend, revision);
    }

    const float x = area.x + PADDING_PIXELS;
    const float width = area.width - 2 * PADDING_PIXELS;
    float y = area.y + PADDING_PIXELS;
    app_draw_text(app, APP_TEXT_HEADING, "Problems", x, y + 4);
    if (view->error[0] != '\0') {
        app_draw_text(app, APP_TEXT_NORMAL, view->error, x, y + HEADER_HEIGHT_PIXELS + 8);
        return;
    }
    if (view->truncated) {
        char text[LABEL_SIZE] = "";
        snprintf(text, sizeof(text), "Showing the newest %d problems.", PROBLEMS_VIEW_COUNT_MAX);
        float text_width = app_measure_text(app, APP_TEXT_DIM, text);
        app_draw_text(app, APP_TEXT_DIM, text, x + width - text_width, y + 10);
    }
    y += HEADER_HEIGHT_PIXELS + PADDING_PIXELS / 2;

    // One pass for the counts that the headers show.
    int32_t counts[TARGET_STATUS_COUNT] = { 0 };
    for (int32_t i = 0; i < view->problem_count; i++) {
        counts[problems_view_status(&view->problems[i])]++;
    }
    const float column_width = width / (float)TARGET_STATUS_COUNT;
    const float cell_y = y + COLUMN_HEADER_HEIGHT_PIXELS;
    const float cell_height = area.y + area.height - PADDING_PIXELS - cell_y;
    // Left to right from the least complete, as the statuses are ordered.
    for (int32_t status = 0; status < TARGET_STATUS_COUNT; status++) {
        const float column_x = x + (float)status * column_width;
        Rectangle header = { column_x, y, column_width, COLUMN_HEADER_HEIGHT_PIXELS };
        Rectangle cell = { column_x, cell_y, column_width, cell_height };
        problems_view_draw_column(view, app, (Target_Status)status, counts[status], header,
                                  cell);
    }
}

/**
 * The newest first, which is the backend's order for targets offered to a task not stored yet:
 * none of them are linked to it, so none are moved ahead. Opportunities are left out, because
 * this view is about problems.
 */
static void problems_view_load(Problems_View *view, Backend *backend, uint64_t revision) {
    view->revision = revision;
    view->problem_count = 0;
    view->truncated = false;
    view->error[0] = '\0';
    int32_t count = backend_load_task_targets(
        backend, 0, true, array_slice_make(view->problems, PROBLEMS_VIEW_COUNT_MAX),
        &view->truncated);
    // Marked loaded either way, so that a failure is retried at the next change, not every
    // frame, as the calendar does.
    view->loaded = true;
    if (count < 0) {
        snprintf(view->error, sizeof(view->error), "%s", backend_error(backend));
        return;
    }
    assert(count <= PROBLEMS_VIEW_COUNT_MAX);
    view->problem_count = count;
}

/**
 * The state and its count above, and the problems in that state as bars in a cell below, like
 * a weekday name above its day. When not all fit, the last line counts the rest.
 */
static void problems_view_draw_column(const Problems_View *view, const App *app,
                                      Target_Status status, int32_t count, Rectangle header,
                                      Rectangle cell) {
    assert(count >= 0);
    assert(count <= view->problem_count);
    char text[LABEL_SIZE] = "";
    snprintf(text, sizeof(text), "%s (%" PRId32 ")", target_status_name(status), count);
    float text_width = app_measure_text(app, APP_TEXT_NORMAL, text);
    app_draw_text(app, APP_TEXT_NORMAL, text, header.x + (header.width - text_width) / 2,
                  header.y + 4);
    DrawRectangleRec(cell, GetColor((unsigned int)GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL)));
    DrawRectangleLinesEx(cell, 1, GetColor((unsigned int)GuiGetStyle(DEFAULT,
                                                                     BORDER_COLOR_NORMAL)));

    const float slot_height = APP_BAR_HEIGHT_PIXELS + APP_BAR_GAP_PIXELS;
    const float top = cell.y + COLUMN_TOP_PIXELS;
    const int32_t slot_count = (int32_t)((cell.y + cell.height - 4 - top) / slot_height);
    if (slot_count <= 0) {
        return;
    }
    int32_t bar_count = count;
    if (count > slot_count) {
        bar_count = slot_count - 1;
    }
    int32_t bar_index = 0;
    for (int32_t i = 0; i < view->problem_count && bar_index < bar_count; i++) {
        const Link_Target *problem = &view->problems[i];
        assert(problem->kind == EVENT_KIND_PROBLEM);
        if (problems_view_status(problem) != status) {
            continue;
        }
        // Inset as a one-day bar is in its day cell.
        Rectangle bar = { cell.x + 4, top + (float)bar_index * slot_height, cell.width - 8,
                          APP_BAR_HEIGHT_PIXELS };
        app_draw_problem_bar(app, problem->title, status == TARGET_STATUS_DONE, bar);
        bar_index++;
    }
    if (count > bar_count) {
        snprintf(text, sizeof(text), "+%" PRId32 " more", count - bar_count);
        app_draw_text(app, APP_TEXT_SMALL, text, cell.x + 6,
                      top + (float)bar_count * slot_height + 2);
    }
}

static Target_Status problems_view_status(const Link_Target *problem) {
    Target_Status status = target_status(problem->task_count, problem->task_done_count);
    assert((int32_t)status >= 0);
    assert(status < TARGET_STATUS_COUNT);
    return status;
}
