#ifndef LOCAL_BACKEND_H
#define LOCAL_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/backend.h>
#include <zeman/event_store.h>

// A Backend over the desktop's own SQLite database. Every change made through it, by the desktop
// user interface or by a phone through the server, raises the revision, so that both see it.
typedef struct Local_Backend {
    Event_Store store;
    uint64_t revision;
    bool store_open;
    char error[EVENT_STORE_ERROR_SIZE];
} Local_Backend;

/**
 * Starts with no database open. The backend works without one, with every operation failing
 * and saying why, until local_backend_switch opens one.
 */
void local_backend_init(Local_Backend *local);
/**
 * Opens the database at path in place of the one open now, if any, which is then closed. With
 * create, a missing file is made into a new, empty database; without, it is an error. Returns
 * false, with error set, when the database cannot be opened; the open one then stays open.
 * On success the revision rises, so that the user interface and phones reload everything.
 */
bool local_backend_switch(Local_Backend *local, const char *path, bool create);
void local_backend_close(Local_Backend *local);
Backend local_backend(Local_Backend *local);

#endif
