#ifndef TASKS_VIEW_H
#define TASKS_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/app.h>
#include <zeman/protocol.h>

enum {
    // Tasks loaded for the view: as many as a calendar loads events for six weeks, which is
    // what one response from the desktop app can carry. When there are more, the view says so.
    TASKS_VIEW_COUNT_MAX = PROTOCOL_EVENT_COUNT_MAX,
    TASKS_VIEW_ERROR_SIZE = 192,
};

// Where a task's due occurrence falls from today, one column each, from the most pressing to the
// least, and the done ones last, as the Problems view ends with the completed problems.
typedef enum Task_Group {
    TASK_GROUP_OVERDUE,   // Ended before today, not done
    TASK_GROUP_TODAY,     // Covers today, not done
    TASK_GROUP_UPCOMING,  // Starts after today, not done
    TASK_GROUP_DONE,
    TASK_GROUP_COUNT,
} Task_Group;

// The text boxes of the search bar, from left to right. An empty box sets no limit, so that the
// search finds every task until the user narrows it.
typedef enum Tasks_View_Field {
    TASKS_VIEW_FIELD_NAME,       // Part of the title, in any case
    TASKS_VIEW_FIELD_FROM_DATE,  // YYYY-MM-DD
    TASKS_VIEW_FIELD_FROM_TIME,  // HH:MM, optional; the start of the day without it
    TASKS_VIEW_FIELD_TO_DATE,
    TASKS_VIEW_FIELD_TO_TIME,    // The end of the day without it
    TASKS_VIEW_FIELD_COUNT,
    TASKS_VIEW_FIELD_NONE = -1,  // No box is being edited
} Tasks_View_Field;

// The Tasks view: every task at its due occurrence, in one column per Task_Group, drawn as the
// Problems view draws its problems, and narrowed to those the search bar finds. A click on a
// task opens its day in the calendar; one on its checkbox checks it off.
typedef struct Tasks_View {
    Task_Occurrence tasks[TASKS_VIEW_COUNT_MAX];
    uint64_t revision;  // Backend revision that tasks[] was loaded at
    // The day tasks[] was loaded for, since which occurrence is due, and its group, follow it.
    Date today;
    int32_t task_count;
    int32_t field_active;  // Tasks_View_Field being edited, or TASKS_VIEW_FIELD_NONE
    bool loaded;        // False until the first load
    bool truncated;     // More tasks exist than were loaded
    char error[TASKS_VIEW_ERROR_SIZE];  // Why the last load or check-off failed, or empty
    // What is typed in each box of the search bar. As large as a title for every box, since a
    // name is the longest thing searched for; the dates and times need far less.
    char search[TASKS_VIEW_FIELD_COUNT][EVENT_TITLE_SIZE];
} Tasks_View;

// A task the user clicked this frame to see in the calendar, and whether the pointer is over a
// task, for the cursor.
typedef struct Tasks_View_Choice {
    Date date;         // Start of the clicked task's due occurrence
    int64_t task_id;   // The clicked task, or 0 for none
    bool hovered;
} Tasks_View_Choice;

void tasks_view_init(Tasks_View *view);
/**
 * Reloads the tasks when the backend's data or the day changed, then draws the search bar and
 * the tasks it finds into area, and checks a task off, or back on, when its checkbox is clicked.
 */
Tasks_View_Choice tasks_view_draw(Tasks_View *view, Backend *backend, App const *app,
                                  Rectangle area);

#endif
