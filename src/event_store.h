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
};

typedef struct EventStore {
    struct sqlite3 *database;
    struct sqlite3_stmt *statement_insert;
    struct sqlite3_stmt *statement_select_range;
    struct sqlite3_stmt *statement_delete;
    struct sqlite3_stmt *statement_update;
    char error[EVENT_STORE_ERROR_SIZE];  // Message for the last failed call
} EventStore;

// Opens or creates the database at path, migrates its schema to the current version, and
// prepares all statements up front, so that later calls do not allocate statements.
// On failure, store->error explains why; call event_store_close even then.
bool event_store_open(EventStore *store, const char *path);
void event_store_close(EventStore *store);

// Inserts the event and returns its new id, or 0 on failure. The database sets created_at.
int64_t event_store_insert(EventStore *store, const Event *event);

// Replaces the stored fields of the event with event->id. created_at is kept, because it records
// when the event was first made. Returns false if it fails or the event is not there.
bool event_store_update(EventStore *store, const Event *event);

// Deletes the event with all its repeats. Returns false if it fails or the event is not there.
bool event_store_delete(EventStore *store, int64_t id);

// Loads events with an occurrence that may overlap [first, last], or created within it.
// Repeating events that start before last are always included, because only the caller
// works out which of their occurrences are visible.
// Returns the number loaded, or -1 on failure. Sets *truncated when more than
// event_count_max events matched, so that the caller can say that some are hidden.
int32_t event_store_load_range(EventStore *store, Date first, Date last,
                               Event *events, int32_t event_count_max, bool *truncated);

#endif
