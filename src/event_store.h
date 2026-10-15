#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "calendar.h"
#include "event.h"

enum {
    // Bytes including the terminator for messages returned to the user interface.
    EVENT_STORE_ERROR_SIZE = 160,
    // Statements prepared at open; event_store.c names them and checks this count.
    EVENT_STORE_STATEMENT_COUNT = 8,
};

// One checked-off occurrence of a task.
typedef struct TaskCompletion {
    int64_t event_id;
    int32_t occurrence_start_day;  // Day number of the occurrence's start date
} TaskCompletion;

typedef struct EventStore {
    struct sqlite3 *database;
    struct sqlite3_stmt *statements[EVENT_STORE_STATEMENT_COUNT];
    char error[EVENT_STORE_ERROR_SIZE];  // Message for the last failed call
} EventStore;

// Opens or creates the database at path, migrates its schema to the current version, and
// prepares all statements up front, so that later calls do not allocate statements.
// On failure, store->error explains why; call event_store_close even then.
bool event_store_open(EventStore *store, const char *path);
void event_store_close(EventStore *store);

// Inserts the event or task and returns its new id, or 0 on failure. The database sets
// created_at.
int64_t event_store_insert(EventStore *store, const Event *event);

// Replaces the stored fields of the event with event->id. created_at is kept, because it records
// when the event was first made. Returns false if it fails or the event is not there.
bool event_store_update(EventStore *store, const Event *event);

// Deletes the event with all its repeats, and a task's completions with it.
// Returns false if it fails or the event is not there.
bool event_store_delete(EventStore *store, int64_t id);

// Loads events with an occurrence that may overlap [first, last], or created within it.
// Repeating events that start before last are always included, because only the caller
// works out which of their occurrences are visible.
// Returns the number loaded, or -1 on failure. Sets *truncated when more than
// event_count_max events matched, so that the caller can say that some are hidden.
int32_t event_store_load_range(EventStore *store, Date first, Date last,
                               Event *events, int32_t event_count_max, bool *truncated);

// Returns the id of the location with exactly this address and city, creating it if there is
// none, or 0 on failure. Either text may be empty, but not both.
int64_t event_store_location_find_or_create(EventStore *store, const char *address,
                                            const char *city);

// Marks one occurrence of a task, identified by its start date, as done or not done.
// Marking it twice the same way is not an error.
bool event_store_set_task_done(EventStore *store, int64_t id, Date occurrence_start, bool done);

// Loads the completions of occurrences that may cover a day in [first, last]: those starting
// within it or up to a year before it. Returns the number loaded, or -1 on failure, and sets
// *truncated as event_store_load_range does.
int32_t event_store_load_completions(EventStore *store, Date first, Date last,
                                     TaskCompletion *completions, int32_t completion_count_max,
                                     bool *truncated);

#endif
