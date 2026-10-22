#include <zeman/local_backend.h>

#include <assert.h>
#include <stdio.h>
#include <time.h>

static bool local_available(Local_Backend *local);
static void local_changed(Local_Backend *local);
static int32_t local_load_range(void *context, Date first, Date last, Array_Slice events,
                                bool *truncated);
static int32_t local_load_completions(void *context, Date first, Date last,
                                      Array_Slice completions, bool *truncated);
static int64_t local_insert(void *context, const Event *event);
static bool local_update(void *context, const Event *event);
static bool local_remove(void *context, int64_t id);
static bool local_set_task_done(void *context, int64_t id, Date occurrence_start, bool done);
static int64_t local_location(void *context, const char *address, const char *city);
static int32_t local_load_target_tasks(void *context, int64_t target_id, Array_Slice tasks,
                                       bool *truncated);
static int32_t local_load_task_targets(void *context, int64_t task_id, bool problems_only,
                                       Array_Slice targets, bool *truncated);
static bool local_set_task_targets(void *context, int64_t task_id, Array_Slice target_ids);
static uint64_t local_revision(void *context);
static const char *local_error(const void *context);

static const Backend_Ops local_ops = {
    .load_range = local_load_range,
    .load_completions = local_load_completions,
    .insert = local_insert,
    .update = local_update,
    .remove = local_remove,
    .set_task_done = local_set_task_done,
    .location_find_or_create = local_location,
    .load_target_tasks = local_load_target_tasks,
    .load_task_targets = local_load_task_targets,
    .set_task_targets = local_set_task_targets,
    .revision = local_revision,
    .error = local_error,
};

void local_backend_init(Local_Backend *local) {
    assert(local != NULL);
    // Starts from the clock rather than 1, so that a phone that saw the revision of an earlier
    // run of this app does not mistake a new run's revision for one it has already loaded.
    *local = (Local_Backend){ .revision = (uint64_t)time(NULL) << 20, .store_open = false };
    // Seen only when no switch is tried, as when settings.ini cannot be read: the Settings view
    // then says why and takes another path.
    snprintf(local->error, sizeof(local->error), "No database is open; choose one in Settings.");
}

bool local_backend_switch(Local_Backend *local, const char *path, bool create) {
    assert(local != NULL);
    assert(path != NULL);
    // Opened beside the current store, so that a failure leaves the current one in use.
    Event_Store store = { 0 };
    if (!event_store_open(&store, path, create)) {
        snprintf(local->error, sizeof(local->error), "%s", store.error);
        event_store_close(&store);
        return false;
    }
    local_backend_close(local);
    // The store holds only handles, which SQLite does not tie to where they are kept.
    local->store = store;
    local->store_open = true;
    local->error[0] = '\0';
    // Different data under the same ids: everything loaded from the old database is stale.
    local_changed(local);
    return true;
}

void local_backend_close(Local_Backend *local) {
    if (local->store_open) {
        event_store_close(&local->store);
        local->store_open = false;
        // Keeps local_available's promise that a closed backend says why.
        snprintf(local->error, sizeof(local->error), "The database was closed.");
    }
}

Backend local_backend(Local_Backend *local) {
    return (Backend){ .ops = &local_ops, .context = local };
}

/**
 * The store's error is copied so that it outlives the store's next call. Without a store, the
 * error already says why none is open, from local_backend_init or the switch that failed, and is
 * kept, so that every failing operation tells the user the cause rather than only the effect.
 */
static bool local_available(Local_Backend *local) {
    if (!local->store_open) {
        assert(local->error[0] != '\0');
        return false;
    }
    return true;
}

static void local_changed(Local_Backend *local) {
    local->revision++;
}

static int32_t local_load_range(void *context, Date first, Date last, Array_Slice events,
                                bool *truncated) {
    Local_Backend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_range(&local->store, first, last, events, truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int32_t local_load_completions(void *context, Date first, Date last,
                                      Array_Slice completions, bool *truncated) {
    Local_Backend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_completions(&local->store, first, last, completions,
                                                 truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int64_t local_insert(void *context, const Event *event) {
    Local_Backend *local = context;
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
    Local_Backend *local = context;
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
    Local_Backend *local = context;
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
    Local_Backend *local = context;
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
    Local_Backend *local = context;
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

static int32_t local_load_target_tasks(void *context, int64_t target_id, Array_Slice tasks,
                                       bool *truncated) {
    Local_Backend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_target_tasks(&local->store, target_id, tasks, truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static int32_t local_load_task_targets(void *context, int64_t task_id, bool problems_only,
                                       Array_Slice targets, bool *truncated) {
    Local_Backend *local = context;
    if (!local_available(local)) {
        return -1;
    }
    int32_t count = event_store_load_task_targets(&local->store, task_id, problems_only, targets,
                                                  truncated);
    if (count < 0) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
    }
    return count;
}

static bool local_set_task_targets(void *context, int64_t task_id, Array_Slice target_ids) {
    Local_Backend *local = context;
    if (!local_available(local)) {
        return false;
    }
    if (!event_store_set_task_targets(&local->store, task_id, target_ids)) {
        snprintf(local->error, sizeof(local->error), "%s", local->store.error);
        return false;
    }
    // A target's status shows wherever its links are loaded, so phones reload too.
    local_changed(local);
    return true;
}

static uint64_t local_revision(void *context) {
    const Local_Backend *local = context;
    return local->revision;
}

static const char *local_error(const void *context) {
    const Local_Backend *local = context;
    return local->error;
}
