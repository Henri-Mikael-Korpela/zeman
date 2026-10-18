#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/event.h>

// Where the calendar's data lives, as the user interface sees it. The desktop app reads and
// writes its SQLite database directly; the Android app asks the desktop app over the network.
// Both implement these operations, so that one user interface serves both.
//
// Every operation reports failure through its return value, and error() then says why.
typedef struct Backend_Ops {
    // Loads events with an occurrence that may overlap [first, last], or created within it.
    // Returns the count, or -1. Sets *truncated when more than event_count_max matched.
    int32_t (*load_range)(void *context, Date first, Date last, Event *events,
                          int32_t event_count_max, bool *truncated);
    // Loads completions of task occurrences that may cover a day in [first, last].
    int32_t (*load_completions)(void *context, Date first, Date last,
                                Task_Completion *completions, int32_t completion_count_max,
                                bool *truncated);
    // Returns the new id, or 0.
    int64_t (*insert)(void *context, const Event *event);
    bool (*update)(void *context, const Event *event);
    bool (*remove)(void *context, int64_t id);
    bool (*set_task_done)(void *context, int64_t id, Date occurrence_start, bool done);
    // Returns the id of the location with this address and city, creating it if needed, or 0.
    int64_t (*location_find_or_create)(void *context, const char *address, const char *city);
    // Loads the tasks linked to a problem, in start order. Returns the count, or -1. Sets
    // *truncated when more than task_count_max are linked.
    int32_t (*load_problem_tasks)(void *context, int64_t problem_id, Problem_Task *tasks,
                                  int32_t task_count_max, bool *truncated);
    // Loads the problems a task can be linked to: those linked to task_id first, so that they
    // are never cut off, then the newest. task_id is 0 for a task not stored yet. Returns the
    // count, or -1, and sets *truncated as load_problem_tasks does.
    int32_t (*load_task_problems)(void *context, int64_t task_id, Task_Problem *problems,
                                  int32_t problem_count_max, bool *truncated);
    // Makes problem_ids the problems linked to the task, replacing its earlier links, all or
    // nothing. With no ids it unlinks the entry from every problem, which also suits an entry
    // that is no longer a task.
    bool (*set_task_problems)(void *context, int64_t task_id, const int64_t *problem_ids,
                              int32_t problem_count);
    // A number that changes whenever the data changes, from this program or another, so that
    // the user interface knows when to reload. 0 when unknown, for example while disconnected.
    uint64_t (*revision)(void *context);
    const char *(*error)(const void *context);
} Backend_Ops;

typedef struct Backend {
    const Backend_Ops *ops;
    void *context;
} Backend;

static inline int32_t backend_load_range(Backend *backend, Date first, Date last, Event *events,
                                         int32_t event_count_max, bool *truncated) {
    return backend->ops->load_range(backend->context, first, last, events, event_count_max,
                                    truncated);
}

static inline int32_t backend_load_completions(Backend *backend, Date first, Date last,
                                               Task_Completion *completions,
                                               int32_t completion_count_max, bool *truncated) {
    return backend->ops->load_completions(backend->context, first, last, completions,
                                          completion_count_max, truncated);
}

static inline int64_t backend_insert(Backend *backend, const Event *event) {
    return backend->ops->insert(backend->context, event);
}

static inline bool backend_update(Backend *backend, const Event *event) {
    return backend->ops->update(backend->context, event);
}

static inline bool backend_remove(Backend *backend, int64_t id) {
    return backend->ops->remove(backend->context, id);
}

static inline bool backend_set_task_done(Backend *backend, int64_t id, Date occurrence_start,
                                         bool done) {
    return backend->ops->set_task_done(backend->context, id, occurrence_start, done);
}

static inline int64_t backend_location_find_or_create(Backend *backend, const char *address,
                                                      const char *city) {
    return backend->ops->location_find_or_create(backend->context, address, city);
}

static inline int32_t backend_load_problem_tasks(Backend *backend, int64_t problem_id,
                                                 Problem_Task *tasks, int32_t task_count_max,
                                                 bool *truncated) {
    return backend->ops->load_problem_tasks(backend->context, problem_id, tasks, task_count_max,
                                            truncated);
}

static inline int32_t backend_load_task_problems(Backend *backend, int64_t task_id,
                                                 Task_Problem *problems,
                                                 int32_t problem_count_max, bool *truncated) {
    return backend->ops->load_task_problems(backend->context, task_id, problems,
                                            problem_count_max, truncated);
}

static inline bool backend_set_task_problems(Backend *backend, int64_t task_id,
                                             const int64_t *problem_ids, int32_t problem_count) {
    return backend->ops->set_task_problems(backend->context, task_id, problem_ids,
                                           problem_count);
}

static inline uint64_t backend_revision(Backend *backend) {
    return backend->ops->revision(backend->context);
}

static inline const char *backend_error(const Backend *backend) {
    return backend->ops->error(backend->context);
}

#endif
