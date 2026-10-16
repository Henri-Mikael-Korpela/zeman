#ifndef LOCAL_BACKEND_H
#define LOCAL_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "backend.h"
#include "event_store.h"

// A Backend over the desktop's own SQLite database. Every change made through it, by the desktop
// user interface or by a phone through the server, raises the revision, so that both see it.
typedef struct LocalBackend {
    EventStore store;
    uint64_t revision;
    bool store_open;
    char error[EVENT_STORE_ERROR_SIZE];
} LocalBackend;

// Returns false when the database cannot be opened; the backend still works then, with every
// operation failing and saying why.
bool local_backend_open(LocalBackend *local, const char *path);
void local_backend_close(LocalBackend *local);
Backend local_backend(LocalBackend *local);

#endif
