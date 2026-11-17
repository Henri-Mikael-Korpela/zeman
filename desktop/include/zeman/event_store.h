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
    EVENT_STORE_STATEMENT_COUNT = 26,
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
bool event_store_open(Event_Store *store, char const *path, bool create);
void event_store_close(Event_Store *store);

/**
 * Inserts the event or task and returns its new id, or 0 on failure. The database sets
 * created_at.
 */
int64_t event_store_insert(Event_Store *store, Event const *event);

/**
 * Replaces the stored fields of the event with event->id. created_at is kept, because it records
 * when the event was first made. Returns false if it fails or the event is not there.
 */
bool event_store_update(Event_Store *store, Event const *event);

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
int64_t event_store_location_find_or_create(Event_Store *store, Str address, Str city);

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

/**
 * Loads every task at its occurrence due on today, with whether that occurrence is done, sorted
 * by the occurrence's start, into tasks, a slice of Task_Occurrence. Returns the number loaded,
 * or -1, and sets *truncated as event_store_load_range does; see Backend_Ops.load_tasks for
 * which tasks are kept then.
 */
int32_t event_store_load_tasks(Event_Store *store, Date today, Array_Slice tasks,
                               bool *truncated);

/**
 * Stores the image, png_bytes being a slice of bytes that passes description_image_is_valid,
 * with no item yet, and returns its id, or 0 on failure.
 */
int64_t event_store_image_insert(Event_Store *store, Array_Slice png_bytes);

/**
 * Copies the bytes of the image with id into png_bytes, a slice of bytes. Returns how many, or
 * -1 when it fails, the image is not there, or it does not fit.
 */
int32_t event_store_image_load(Event_Store *store, int64_t id, Array_Slice png_bytes);

/**
 * Loads the ids of the item's images in their order into image_ids, a slice of int64_t.
 * Returns the count, or -1, also when it has more than fit, which only happens to a database
 * changed by another program.
 */
int32_t event_store_load_event_images(Event_Store *store, int64_t event_id,
                                      Array_Slice image_ids);

/**
 * Makes image_ids, a slice of int64_t with each given once, the item's images in this order,
 * and deletes those it had that are not among them. Fails, changing nothing, if one is gone or
 * belongs to another item.
 */
bool event_store_set_event_images(Event_Store *store, int64_t event_id, Array_Slice image_ids);

/**
 * Stores a file, with the name that passes attachment_name_is_valid and the contents bytes, a
 * slice of bytes that is not empty and at most ATTACHMENT_SIZE_BYTES_MAX, with no item yet.
 * Returns its id, or 0 on failure.
 */
int64_t event_store_attachment_insert(Event_Store *store, Str name, Array_Slice bytes);

/**
 * Copies the contents of the file with id into bytes, a slice of bytes. Returns how many, or -1
 * when it fails, the file is not there, or it does not fit.
 */
int32_t event_store_attachment_load(Event_Store *store, int64_t id, Array_Slice bytes);

/**
 * Loads the item's files, without their contents, in their order into attachments, a slice of
 * Attachment. Returns the count, or -1, also when it has more than fit, or one that this program
 * would not have stored, which only a database changed by another program has.
 */
int32_t event_store_load_event_attachments(Event_Store *store, int64_t event_id,
                                           Array_Slice attachments);

/**
 * Makes attachment_ids, a slice of int64_t with each given once, the item's files in this order,
 * and deletes those it had that are not among them. Fails, changing nothing, if one is gone or
 * belongs to another item.
 */
bool event_store_set_event_attachments(Event_Store *store, int64_t event_id,
                                       Array_Slice attachment_ids);

#endif
