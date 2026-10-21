#ifndef PROBLEMS_VIEW_H
#define PROBLEMS_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/app.h>

enum {
    // Problems loaded for the view, the newest first. Many years of a person's problems; when
    // there are more, the view says that the oldest are left out.
    PROBLEMS_VIEW_COUNT_MAX = 512,
    PROBLEMS_VIEW_ERROR_SIZE = 192,
};

// The Problems view: every problem, in one column per Target_Status from the least to the most
// complete, drawn as the calendar's week view draws its days.
typedef struct Problems_View {
    Link_Target problems[PROBLEMS_VIEW_COUNT_MAX];
    uint64_t revision;      // Backend revision that problems[] was loaded at
    int32_t problem_count;
    bool loaded;            // False until the first load, and after a failed one
    bool truncated;         // More problems exist than were loaded
    char error[PROBLEMS_VIEW_ERROR_SIZE];  // Why the last load failed, or empty
} Problems_View;

void problems_view_init(Problems_View *view);
/**
 * Reloads the problems when the backend's data changed, then draws them into area.
 */
void problems_view_draw(Problems_View *view, Backend *backend, const App *app, Rectangle area);

#endif
