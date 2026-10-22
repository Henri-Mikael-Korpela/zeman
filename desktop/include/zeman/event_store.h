#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zeman/array_slice.h>
#include <zeman/event.h>

enum {
    // Bytes including the terminator for messages returned to the user interface.
    EVENT_STORE_ERROR_SIZE = 160,
    // Statements prepared at open; event_store.c names them and checks this count.
    EVENT_STORE_STATEMENT_COUNT = 12,
};

typedef struct Event_Store {
    struct sqlite3 *database;
    struct sqlite3_stmt *statements[EVENT_STORE_STATEMENT_COUNT];
    char error[EVENT_STORE_ERROR_SIZE];  // Message for the last failed call
} Event_Store;

/**
 * Opens the database at path, or with create makes it when the file does not exist, migrates
 * its schema to the current version, and prepares all statements up front, so that later calls
 * do not allocate statements. On failure, store->error explains why; call event_store_close
 * even then.
 */
bool event_store_open(Event_Store *store, const char *path, bool create);
void event_store_close(Event_Store *store);

/**
 * Inserts the event or task and returns its new id, or 0 on failure. The database sets
 * created_at.
 */
int64_t event_store_insert(Event_Store *store, const Event *event);

/**
 * Replaces the stored fields of the event with event->id. created_at is kept, because it records
 * when the event was first made. Returns false if it fails or the event is not there.
 */
bool event_store_update(Event_Store *store, const Event *event);

/**
 * Deletes the event with all its repeats, and a task's completions with it.
 * Returns false if it fails or the event is not there.
 */
bool event_store_delete(Event_Store *store, int64_t id);

/**
 * Loads events with an occurrence that may overlap [first, last], or created within it, into
 * events, a slice of Event whose count is the room there is. Repeating events that start before
 * last are always included, because only the caller works out which of their occurrences are
 * visible. Returns the number loaded, or -1 on failure. Sets *truncated when more than
 * events.count events matched, so that the caller can say that some are hidden.
 */
int32_t event_store_load_range(Event_Store *store, Date first, Date last, Array_Slice events,
                               bool *truncated);

/**
 * Returns the id of the location with exactly this address and city, creating it if there is
 * none, or 0 on failure. Either text may be empty, but not both.
 */
int64_t event_store_location_find_or_create(Event_Store *store, const char *address,
                                            const char *city);

/**
 * Marks one occurrence of a task, identified by its start date, as done or not done.
 * Marking it twice the same way is not an error.
 */
bool event_store_set_task_done(Event_Store *store, int64_t id, Date occurrence_start, bool done);

/**
 * Loads the completions of occurrences that may cover a day in [first, last]: those starting
 * within it or up to a year before it. They go into completions, a slice of Task_Completion.
 * Returns the number loaded, or -1 on failure, and sets *truncated as event_store_load_range
 * does.
 */
int32_t event_store_load_completions(Event_Store *store, Date first, Date last,
                                     Array_Slice completions, bool *truncated);

/**
 * Loads the tasks linked to a link target, in start order, each with whether any occurrence is
 * done, into tasks, a slice of Linked_Task. Returns the number loaded, or -1, and sets
 * *truncated as event_store_load_range does.
 */
int32_t event_store_load_target_tasks(Event_Store *store, int64_t target_id, Array_Slice tasks,
                                      bool *truncated);

/**
 * Loads every link target, or every problem with problems_only, with how many of its linked
 * tasks are done: the ones linked to task_id first, then the newest. task_id is 0 for a task
 * not stored yet. They go into targets, a slice of Link_Target.
 */
int32_t event_store_load_task_targets(Event_Store *store, int64_t task_id, bool problems_only,
                                      Array_Slice targets, bool *truncated);

/**
 * Replaces the targets linked to the task with target_ids, a slice of int64_t with each given
 * once. Fails, changing nothing, if one of them is not a link target or the task is not a task.
 */
bool event_store_set_task_targets(Event_Store *store, int64_t task_id, Array_Slice target_ids);

#endif
