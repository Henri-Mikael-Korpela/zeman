#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/array_slice.h>
#include <zeman/event.h>
#include <zeman/str.h>

// Where the calendar's data lives, as the user interface sees it. The desktop app reads and
// writes its SQLite database directly; the Android app asks the desktop app over the network.
// Both implement these operations, so that one user interface serves both.
//
// Every operation reports failure through its return value, and error() then says why.
//
// The operations that load many items take a slice of the caller's array to fill: its count is
// the room there is, and they return how many items they put in it. The type of its items is
// in each operation's comment.
typedef struct Backend_Ops {
    // Loads events with an occurrence that may overlap [first, last], or created within it,
    // into events, a slice of Event. Returns the count, or -1. Sets *truncated when more than
    // events.count matched.
    int32_t (*load_range)(void *context, Date first, Date last, Array_Slice events,
                          bool *truncated);
    // Loads completions of task occurrences that may cover a day in [first, last], into
    // completions, a slice of Task_Completion.
    int32_t (*load_completions)(void *context, Date first, Date last, Array_Slice completions,
                                bool *truncated);
    // Returns the new id, or 0.
    int64_t (*insert)(void *context, const Event *event);
    bool (*update)(void *context, const Event *event);
    bool (*remove)(void *context, int64_t id);
    bool (*set_task_done)(void *context, int64_t id, Date occurrence_start, bool done);
    // Returns the id of the location with this address and city, creating it if needed, or 0.
    int64_t (*location_find_or_create)(void *context, Str address, Str city);
    // Loads the tasks linked to a link target, in start order, into tasks, a slice of
    // Linked_Task. Returns the count, or -1. Sets *truncated when more than tasks.count are
    // linked.
    int32_t (*load_target_tasks)(void *context, int64_t target_id, Array_Slice tasks,
                                 bool *truncated);
    // Loads the link targets a task can be linked to: those linked to task_id first, so that
    // they are never cut off, then the newest. task_id is 0 for a task not stored yet. With
    // problems_only, opportunities are left out, for lists of problems alone. They go into
    // targets, a slice of Link_Target. Returns the count, or -1, and sets *truncated as
    // load_target_tasks does.
    int32_t (*load_task_targets)(void *context, int64_t task_id, bool problems_only,
                                 Array_Slice targets, bool *truncated);
    // Makes target_ids, a slice of int64_t, the targets linked to the task, replacing its
    // earlier links, all or nothing. With no ids it unlinks the entry from every target, which
    // also suits an entry that is no longer a task.
    bool (*set_task_targets)(void *context, int64_t task_id, Array_Slice target_ids);
    // A number that changes whenever the data changes, from this program or another, so that
    // the user interface knows when to reload. 0 when unknown, for example while disconnected.
    uint64_t (*revision)(void *context);
    Str (*error)(const void *context);
} Backend_Ops;

typedef struct Backend {
    const Backend_Ops *ops;
    void *context;
} Backend;

static inline int32_t backend_load_range(Backend *backend, Date first, Date last,
                                         Array_Slice events, bool *truncated) {
    return backend->ops->load_range(backend->context, first, last, events, truncated);
}

static inline int32_t backend_load_completions(Backend *backend, Date first, Date last,
                                               Array_Slice completions, bool *truncated) {
    return backend->ops->load_completions(backend->context, first, last, completions,
                                          truncated);
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

static inline int64_t backend_location_find_or_create(Backend *backend, Str address, Str city) {
    return backend->ops->location_find_or_create(backend->context, address, city);
}

static inline int32_t backend_load_target_tasks(Backend *backend, int64_t target_id,
                                                Array_Slice tasks, bool *truncated) {
    return backend->ops->load_target_tasks(backend->context, target_id, tasks, truncated);
}

static inline int32_t backend_load_task_targets(Backend *backend, int64_t task_id,
                                                bool problems_only, Array_Slice targets,
                                                bool *truncated) {
    return backend->ops->load_task_targets(backend->context, task_id, problems_only, targets,
                                           truncated);
}

static inline bool backend_set_task_targets(Backend *backend, int64_t task_id,
                                            Array_Slice target_ids) {
    return backend->ops->set_task_targets(backend->context, task_id, target_ids);
}

static inline uint64_t backend_revision(Backend *backend) {
    return backend->ops->revision(backend->context);
}

static inline Str backend_error(const Backend *backend) {
    return backend->ops->error(backend->context);
}

/**
 * Copies the error text into buffer, cut at a whole character when it does not fit: the callers
 * keep it in a fixed field of a form or a view, where a shortened message is better than none.
 */
static inline void backend_error_copy(const Backend *backend, char *buffer, size_t buffer_size) {
    (void)str_copy_truncated(backend_error(backend), buffer, buffer_size);
}

#endif
