#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "calendar.h"

enum {
    // Bytes including the terminator. Bounds the form input and every loaded title.
    EVENT_TITLE_SIZE = 64,
    // Bytes including the terminator for messages returned to the user interface.
    EVENT_STORE_ERROR_SIZE = 160,
};

typedef struct Event {
    int64_t id;               // 0 until the event is stored
    Date start_date;
    Date end_date;
    Date created_date;        // Local date when the event was inserted
    int32_t start_minute;     // Minute of day, or TIME_NONE
    int32_t end_minute;       // Minute of day, or TIME_NONE
    int32_t created_minute;   // Local minute of day when the event was inserted
    char title[EVENT_TITLE_SIZE];
} Event;

typedef struct EventStore {
    struct sqlite3 *database;
    struct sqlite3_stmt *statement_insert;
    struct sqlite3_stmt *statement_select_range;
    char error[EVENT_STORE_ERROR_SIZE];  // Message for the last failed call
} EventStore;

// Opens or creates the database at path and prepares all statements up front, so that later
// calls do not allocate statements. On failure, store->error explains why.
bool event_store_open(EventStore *store, const char *path);
void event_store_close(EventStore *store);

// Inserts the event and returns its new id, or 0 on failure. The database sets created_at.
int64_t event_store_insert(EventStore *store, const Event *event);

// Loads events that overlap [first, last] or were created within it, ordered by start.
// Returns the number loaded, or -1 on failure. Sets *truncated when more than
// event_count_max events matched, so that the caller can say that some are hidden.
int32_t event_store_load_range(EventStore *store, Date first, Date last,
                               Event *events, int32_t event_count_max, bool *truncated);

#endif
