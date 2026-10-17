#include "local_backend.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

static bool local_available(LocalBackend *local);
static void local_changed(LocalBackend *local);
static int32_t local_load_range(void *context, Date first, Date last, Event *events,
                                int32_t event_count_max, bool *truncated);
static int32_t local_load_completions(void *context, Date first, Date last,
                                      TaskCompletion *completions, int32_t completion_count_max,
                                      bool *truncated);
static int64_t local_insert(void *context, const Event *event);
static bool local_update(void *context, const Event *event);
static bool local_remove(void *context, int64_t id);
static bool local_set_task_done(void *context, int64_t id, Date occurrence_start, bool done);
static int64_t local_location(void *context, const char *address, const char *city);
static int32_t local_load_problem_tasks(void *context, int64_t problem_id, ProblemTask *tasks,
                                        int32_t task_count_max, bool *truncated);
static int32_t local_load_task_problems(void *context, int64_t task_id, TaskProblem *problems,
                                        int32_t problem_count_max, bool *truncated);
static bool local_set_task_problems(void *context, int64_t task_id, const int64_t *problem_ids,
                                    int32_t problem_count);
static uint64_t local_revision(void *context);
static const char *local_error(const void *context);

static const BackendOps local_ops = {
    .load_range = local_load_range,
    .load_completions = local_load_completions,
    .insert = local_insert,
    .update = local_update,
    .remove = local_remove,
    .set_task_done = local_set_task_done,
    .location_find_or_create = local_location,
    .load_problem_tasks = local_load_problem_tasks,
    .load_task_problems = local_load_task_problems,
    .set_task_problems = local_set_task_problems,
    .revision = local_revision,
    .error = local_error,
};

bool local_backend_open(LocalBackend *local, const char *path) {
    assert(path != NULL);
    // Starts from the clock rather than 1, so that a phone that saw the revision of an earlier
    // run of this app does not mistake a new run's revision for one it has already loaded.
    *local = (LocalBackend){ .revision = (uint64_t)time(NULL) << 20 };
    local->store_open = event_store_open(&local->store, path);
    if (!local->store_open) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        event_store_close(&local->store);
    }
    return local->store_open;
}

void local_backend_close(LocalBackend *local) {
    if (local->store_open) {
        event_store_close(&local->store);
        local->store_open = false;
    }
}

Backend local_backend(LocalBackend *local) {
    return (Backend){ .ops = &local_ops, .context = local };
}

// The store's error is copied so that it outlives the store's next call.
static bool local_available(LocalBackend *local) {
    if (!local->store_open) {
        snprintf(local->error, sizeof(local->error), "No database: events cannot be loaded or "
                                                     "saved.");
        return false;
    }
    return true;
}

static void local_changed(LocalBackend *local) {
    local->revision++;
}

static int32_t local_load_range(void *context, Date first, Date last, Event *events,
                                int32_t event_count_max, bool *truncated) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_range(&local->store, first, last, events, event_count_max,
                                           truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int32_t local_load_completions(void *context, Date first, Date last,
                                      TaskCompletion *completions, int32_t completion_count_max,
                                      bool *truncated) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_completions(&local->store, first, last, completions,
                                                 completion_count_max, truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int64_t local_insert(void *context, const Event *event) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return 0;
    }
    int64_t id = event_store_insert(&local->store, event);
    if (id == 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        return 0;
    }
    local_changed(local);
    return id;
}

static bool local_update(void *context, const Event *event) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return false;
    }
    if (!event_store_update(&local->store, event)) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        return false;
    }
    local_changed(local);
    return true;
}

static bool local_remove(void *context, int64_t id) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return false;
    }
    if (!event_store_delete(&local->store, id)) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        // The event may be gone already, which is a change the user interface should see.
        local_changed(local);
        return false;
    }
    local_changed(local);
    return true;
}

static bool local_set_task_done(void *context, int64_t id, Date occurrence_start, bool done) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return false;
    }
    if (!event_store_set_task_done(&local->store, id, occurrence_start, done)) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        return false;
    }
    local_changed(local);
    return true;
}

static int64_t local_location(void *context, const char *address, const char *city) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return 0;
    }
    int64_t id = event_store_location_find_or_create(&local->store, address, city);
    if (id == 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    // No revision change: a location alone shows nowhere until an event links to it.
    return id;
}

static int32_t local_load_problem_tasks(void *context, int64_t problem_id, ProblemTask *tasks,
                                        int32_t task_count_max, bool *truncated) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_problem_tasks(&local->store, problem_id, tasks,
                                                   task_count_max, truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int32_t local_load_task_problems(void *context, int64_t task_id, TaskProblem *problems,
                                        int32_t problem_count_max, bool *truncated) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_task_problems(&local->store, task_id, problems,
                                                   problem_count_max, truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static bool local_set_task_problems(void *context, int64_t task_id, const int64_t *problem_ids,
                                    int32_t problem_count) {
    LocalBackend *local = context;
    if (!local_available(local)) {
        return false;
    }
    if (!event_store_set_task_problems(&local->store, task_id, problem_ids, problem_count)) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        return false;
    }
    // A problem's status shows wherever its links are loaded, so phones reload too.
    local_changed(local);
    return true;
}

static uint64_t local_revision(void *context) {
    const LocalBackend *local = context;
    return local->revision;
}

static const char *local_error(const void *context) {
    const LocalBackend *local = context;
    return local->error;
}
