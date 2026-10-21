#include <zeman/event_store.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sqlite3.h"

enum {
    // Completions are loaded for occurrences that start up to this many days before the view,
    // because an occurrence is shorter than a year (see event_repeat_gap_days_min) and one that
    // starts that early can still run into the view.
    COMPLETION_LOOKBACK_DAYS = 366,
};

// Dates are ISO text ("YYYY-MM-DD") so that they compare correctly as strings and stay
// readable with any SQLite tool. A NULL time means the event has no time of day on that end.
// created_at is local time, because the whole calendar works in local time.
static const char schema_sql[] =
    "CREATE TABLE IF NOT EXISTS events ("
    "    id INTEGER PRIMARY KEY,"
    "    title TEXT NOT NULL CHECK (length(title) > 0),"
    "    start_date TEXT NOT NULL,"
    "    start_time TEXT,"
    "    end_date TEXT NOT NULL,"
    "    end_time TEXT,"
    "    created_at TEXT NOT NULL"
    "        DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime')),"
    "    CHECK (end_date >= start_date)"
    ");"
    "CREATE INDEX IF NOT EXISTS events_by_start_date ON events (start_date);"
    "CREATE INDEX IF NOT EXISTS events_by_created_at ON events (created_at);";

// Schema versions, kept in PRAGMA user_version. The table above is version 0, and migration i
// moves a database from version i to i + 1, so that databases from earlier builds still open.
static const char *const migrations_sql[] = {
    // Repeating events. A NULL repeat_unit means that the event happens once.
    "ALTER TABLE events ADD COLUMN repeat_unit TEXT"
    "    CHECK (repeat_unit IN ('day', 'week', 'month', 'year'));"
    "ALTER TABLE events ADD COLUMN repeat_interval INTEGER NOT NULL DEFAULT 1"
    "    CHECK (repeat_interval >= 1);",
    // Repetition end dates, inclusive. NULL repeats forever.
    "ALTER TABLE events ADD COLUMN repeat_end_date TEXT"
    "    CHECK (repeat_end_date IS NULL OR repeat_end_date >= start_date);",
    // Tasks, and which of their occurrences are done. A completion is keyed by the start date
    // of its occurrence, so that each repeat of a task is checked off separately.
    "ALTER TABLE events ADD COLUMN kind TEXT NOT NULL DEFAULT 'event'"
    "    CHECK (kind IN ('event', 'task'));"
    "CREATE TABLE task_completions ("
    "    event_id INTEGER NOT NULL REFERENCES events (id) ON DELETE CASCADE,"
    "    occurrence_date TEXT NOT NULL,"
    "    completed_at TEXT NOT NULL"
    "        DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime')),"
    "    PRIMARY KEY (event_id, occurrence_date)"
    ");",
    // Locations, shared between events. Deleting a location unlinks its events rather than
    // deleting them, because the event still happens.
    "CREATE TABLE locations ("
    "    id INTEGER PRIMARY KEY,"
    "    address TEXT NOT NULL,"
    "    city TEXT NOT NULL,"
    "    CHECK (length(address) > 0 OR length(city) > 0),"
    "    UNIQUE (address, city)"
    ");"
    "ALTER TABLE events ADD COLUMN location_id INTEGER"
    "    REFERENCES locations (id) ON DELETE SET NULL;",
    // Free-text description. Empty, not NULL, for none, so that readers need no NULL case.
    "ALTER TABLE events ADD COLUMN description TEXT NOT NULL DEFAULT '';",
    // Problems. SQLite cannot change a CHECK constraint in place, so the table is rebuilt with
    // 'problem' allowed in kind, following SQLite's documented procedure; foreign keys are off
    // while migrating (see event_store_open), or dropping the old table would delete every
    // task completion. The last CHECK makes a problem fall on the day it was created.
    "CREATE TABLE events_new ("
    "    id INTEGER PRIMARY KEY,"
    "    title TEXT NOT NULL CHECK (length(title) > 0),"
    "    start_date TEXT NOT NULL,"
    "    start_time TEXT,"
    "    end_date TEXT NOT NULL,"
    "    end_time TEXT,"
    "    created_at TEXT NOT NULL"
    "        DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime')),"
    "    repeat_unit TEXT CHECK (repeat_unit IN ('day', 'week', 'month', 'year')),"
    "    repeat_interval INTEGER NOT NULL DEFAULT 1 CHECK (repeat_interval >= 1),"
    "    repeat_end_date TEXT"
    "        CHECK (repeat_end_date IS NULL OR repeat_end_date >= start_date),"
    "    kind TEXT NOT NULL DEFAULT 'event' CHECK (kind IN ('event', 'task', 'problem')),"
    "    location_id INTEGER REFERENCES locations (id) ON DELETE SET NULL,"
    "    description TEXT NOT NULL DEFAULT '',"
    "    CHECK (end_date >= start_date),"
    "    CHECK (kind <> 'problem' OR (start_date = substr(created_at, 1, 10)"
    "        AND end_date = start_date AND start_time IS NULL AND end_time IS NULL"
    "        AND repeat_unit IS NULL AND location_id IS NULL))"
    ");"
    "INSERT INTO events_new (id, title, start_date, start_time, end_date, end_time,"
    "    created_at, repeat_unit, repeat_interval, repeat_end_date, kind, location_id,"
    "    description)"
    " SELECT id, title, start_date, start_time, end_date, end_time, created_at, repeat_unit,"
    "    repeat_interval, repeat_end_date, kind, location_id, description FROM events;"
    "DROP TABLE events;"
    "ALTER TABLE events_new RENAME TO events;"
    "CREATE INDEX events_by_start_date ON events (start_date);"
    "CREATE INDEX events_by_created_at ON events (created_at);",
    // Tasks that solve problems: each row says that completing the task helps solve the
    // problem. Deleting either side deletes the link, because it means nothing without both.
    // The second index serves replacing a task's links, which looks them up by task.
    "CREATE TABLE problem_tasks ("
    "    problem_id INTEGER NOT NULL REFERENCES events (id) ON DELETE CASCADE,"
    "    task_id INTEGER NOT NULL REFERENCES events (id) ON DELETE CASCADE,"
    "    PRIMARY KEY (problem_id, task_id)"
    ");"
    "CREATE INDEX problem_tasks_by_task ON problem_tasks (task_id);",
    // Opportunities, which tasks are linked to as they are to problems. The table is rebuilt as
    // for problems, to allow 'opportunity' in kind; an opportunity happens once, so it cannot
    // repeat. The links table is renamed for both kinds of link target, which it now holds.
    "CREATE TABLE events_new ("
    "    id INTEGER PRIMARY KEY,"
    "    title TEXT NOT NULL CHECK (length(title) > 0),"
    "    start_date TEXT NOT NULL,"
    "    start_time TEXT,"
    "    end_date TEXT NOT NULL,"
    "    end_time TEXT,"
    "    created_at TEXT NOT NULL"
    "        DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime')),"
    "    repeat_unit TEXT CHECK (repeat_unit IN ('day', 'week', 'month', 'year')),"
    "    repeat_interval INTEGER NOT NULL DEFAULT 1 CHECK (repeat_interval >= 1),"
    "    repeat_end_date TEXT"
    "        CHECK (repeat_end_date IS NULL OR repeat_end_date >= start_date),"
    "    kind TEXT NOT NULL DEFAULT 'event'"
    "        CHECK (kind IN ('event', 'task', 'problem', 'opportunity')),"
    "    location_id INTEGER REFERENCES locations (id) ON DELETE SET NULL,"
    "    description TEXT NOT NULL DEFAULT '',"
    "    CHECK (end_date >= start_date),"
    "    CHECK (kind <> 'problem' OR (start_date = substr(created_at, 1, 10)"
    "        AND end_date = start_date AND start_time IS NULL AND end_time IS NULL"
    "        AND repeat_unit IS NULL AND location_id IS NULL)),"
    "    CHECK (kind <> 'opportunity' OR repeat_unit IS NULL)"
    ");"
    "INSERT INTO events_new (id, title, start_date, start_time, end_date, end_time,"
    "    created_at, repeat_unit, repeat_interval, repeat_end_date, kind, location_id,"
    "    description)"
    " SELECT id, title, start_date, start_time, end_date, end_time, created_at, repeat_unit,"
    "    repeat_interval, repeat_end_date, kind, location_id, description FROM events;"
    "DROP TABLE events;"
    "ALTER TABLE events_new RENAME TO events;"
    "CREATE INDEX events_by_start_date ON events (start_date);"
    "CREATE INDEX events_by_created_at ON events (created_at);"
    "ALTER TABLE problem_tasks RENAME TO task_links;"
    "ALTER TABLE task_links RENAME COLUMN problem_id TO target_id;"
    "DROP INDEX problem_tasks_by_task;"
    "CREATE INDEX task_links_by_task ON task_links (task_id);",
};
enum { SCHEMA_VERSION = sizeof(migrations_sql) / sizeof(migrations_sql[0]) };

// Text stored in repeat_unit, indexed by Repeat_Unit. NULL is stored for REPEAT_NONE.
static const char *const repeat_unit_names[REPEAT_UNIT_COUNT] = {
    NULL, "day", "week", "month", "year",
};

// Text stored in kind, indexed by Event_Kind.
static const char *const event_kind_names[EVENT_KIND_COUNT] = { "event", "task", "problem",
                                                                 "opportunity" };

typedef enum Statement {
    STATEMENT_INSERT,
    STATEMENT_UPDATE,
    STATEMENT_DELETE,
    STATEMENT_SELECT_RANGE,
    STATEMENT_COMPLETION_INSERT,
    STATEMENT_COMPLETION_DELETE,
    STATEMENT_COMPLETION_SELECT_RANGE,
    STATEMENT_LOCATION_UPSERT,
    STATEMENT_LINK_DELETE,
    STATEMENT_LINK_INSERT,
    STATEMENT_TARGET_TASKS,
    STATEMENT_TASK_TARGETS,
    STATEMENT_COUNT,
} Statement;

// Cast because the constants come from different enums.
static_assert((int)STATEMENT_COUNT == (int)EVENT_STORE_STATEMENT_COUNT,
              "Event_Store holds every statement.");

// Insert and update share parameters 1 to 11, so that both bind with event_store_bind_fields.
static const char *const statements_sql[STATEMENT_COUNT] = {
    [STATEMENT_INSERT] =
        "INSERT INTO events"
        " (title, start_date, start_time, end_date, end_time, repeat_unit, repeat_interval,"
        "  repeat_end_date, kind, location_id, description)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);",
    [STATEMENT_UPDATE] =
        "UPDATE events SET title = ?1, start_date = ?2, start_time = ?3, end_date = ?4,"
        " end_time = ?5, repeat_unit = ?6, repeat_interval = ?7, repeat_end_date = ?8,"
        " kind = ?9, location_id = ?10, description = ?11 WHERE id = ?12;",
    [STATEMENT_DELETE] = "DELETE FROM events WHERE id = ?1;",
    // Ordered by time of day, all-day events first, because the caller lists the events of one
    // day at a time and a repeating event's first start date says nothing about that order.
    // A repeating event with an end is visible until its last occurrence ends: the end date
    // plus one occurrence's length.
    // The location comes with each event through a join, so that no second query is needed.
    // The last column is Event.tasks_done: a link target with linked tasks, none of them without
    // a checked-off occurrence (see Linked_Task). It runs only for targets, which are few.
    [STATEMENT_SELECT_RANGE] =
        "SELECT e.id, e.title, e.start_date, e.start_time, e.end_date, e.end_time,"
        "       e.created_at, e.repeat_unit, e.repeat_interval, e.repeat_end_date, e.kind,"
        "       l.id, l.address, l.city, e.description,"
        "       e.kind IN ('problem', 'opportunity')"
        "       AND EXISTS (SELECT 1 FROM task_links tl JOIN events t ON t.id = tl.task_id"
        "                   WHERE tl.target_id = e.id AND t.kind = 'task')"
        "       AND NOT EXISTS (SELECT 1 FROM task_links tl JOIN events t ON t.id = tl.task_id"
        "                       WHERE tl.target_id = e.id AND t.kind = 'task'"
        "                         AND NOT EXISTS (SELECT 1 FROM task_completions c"
        "                                         WHERE c.event_id = t.id))"
        " FROM events e LEFT JOIN locations l ON l.id = e.location_id"
        " WHERE (e.start_date <= ?2 AND (e.end_date >= ?1 OR (e.repeat_unit IS NOT NULL"
        "        AND (e.repeat_end_date IS NULL OR julianday(e.repeat_end_date)"
        "             + julianday(e.end_date) - julianday(e.start_date) >= julianday(?1)))))"
        "    OR (substr(e.created_at, 1, 10) BETWEEN ?1 AND ?2)"
        " ORDER BY e.start_time IS NOT NULL, e.start_time, e.start_date, e.id"
        " LIMIT ?3;",
    [STATEMENT_COMPLETION_INSERT] =
        "INSERT OR IGNORE INTO task_completions (event_id, occurrence_date) VALUES (?1, ?2);",
    [STATEMENT_COMPLETION_DELETE] =
        "DELETE FROM task_completions WHERE event_id = ?1 AND occurrence_date = ?2;",
    [STATEMENT_COMPLETION_SELECT_RANGE] =
        "SELECT event_id, occurrence_date FROM task_completions"
        " WHERE occurrence_date BETWEEN ?1 AND ?2 ORDER BY event_id LIMIT ?3;",
    // The no-op update makes RETURNING give the id of an existing row too; DO NOTHING would
    // return no row.
    [STATEMENT_LOCATION_UPSERT] =
        "INSERT INTO locations (address, city) VALUES (?1, ?2)"
        " ON CONFLICT (address, city) DO UPDATE SET city = excluded.city RETURNING id;",
    [STATEMENT_LINK_DELETE] = "DELETE FROM task_links WHERE task_id = ?1;",
    // Inserts nothing unless ?1 is a link target and ?2 a task, so that the caller sees a link
    // to something deleted, or of the wrong kind, as no row changed.
    [STATEMENT_LINK_INSERT] =
        "INSERT INTO task_links (target_id, task_id)"
        " SELECT p.id, t.id FROM events p, events t"
        " WHERE p.id = ?1 AND p.kind IN ('problem', 'opportunity')"
        "   AND t.id = ?2 AND t.kind = 'task';",
    // Links to an entry that is no longer a task are left out, as if they were not there; the
    // app removes them when it saves the entry (see Backend_Ops.set_task_targets).
    [STATEMENT_TARGET_TASKS] =
        "SELECT t.id, t.title,"
        "       EXISTS (SELECT 1 FROM task_completions c WHERE c.event_id = t.id)"
        " FROM task_links l JOIN events t ON t.id = l.task_id"
        " WHERE l.target_id = ?1 AND t.kind = 'task'"
        " ORDER BY t.start_date, t.id LIMIT ?2;",
    // Every target is scanned, which is fine for the thousands of entries a personal calendar
    // holds. Linked targets come first, so that the LIMIT never cuts one off. ?3 is 1 for
    // problems only. The deadline comes last, so that an opportunity can show as missed.
    [STATEMENT_TASK_TARGETS] =
        "SELECT p.id, p.title,"
        "       EXISTS (SELECT 1 FROM task_links l"
        "               WHERE l.target_id = p.id AND l.task_id = ?1) AS linked,"
        "       (SELECT count(*) FROM task_links l JOIN events t ON t.id = l.task_id"
        "        WHERE l.target_id = p.id AND t.kind = 'task'),"
        "       (SELECT count(*) FROM task_links l JOIN events t ON t.id = l.task_id"
        "        WHERE l.target_id = p.id AND t.kind = 'task'"
        "          AND EXISTS (SELECT 1 FROM task_completions c WHERE c.event_id = t.id)),"
        "       p.kind, p.end_date, p.end_time"
        " FROM events p"
        " WHERE p.kind = 'problem' OR (p.kind = 'opportunity' AND ?3 = 0)"
        " ORDER BY linked DESC, p.created_at DESC, p.id DESC LIMIT ?2;",
};

enum {
    COLUMN_ID,
    COLUMN_TITLE,
    COLUMN_START_DATE,
    COLUMN_START_TIME,
    COLUMN_END_DATE,
    COLUMN_END_TIME,
    COLUMN_CREATED_AT,
    COLUMN_REPEAT_UNIT,
    COLUMN_REPEAT_INTERVAL,
    COLUMN_REPEAT_END_DATE,
    COLUMN_KIND,
    COLUMN_LOCATION_ID,
    COLUMN_LOCATION_ADDRESS,
    COLUMN_LOCATION_CITY,
    COLUMN_DESCRIPTION,
    COLUMN_TASKS_DONE,
};

// Columns of STATEMENT_TASK_TARGETS.
enum {
    TARGET_COLUMN_ID,
    TARGET_COLUMN_TITLE,
    TARGET_COLUMN_LINKED,
    TARGET_COLUMN_TASK_COUNT,
    TARGET_COLUMN_TASK_DONE_COUNT,
    TARGET_COLUMN_KIND,
    TARGET_COLUMN_END_DATE,
    TARGET_COLUMN_END_TIME,
};

static bool event_store_fail(Event_Store *store, const char *context);
static int bind_id_and_limit(sqlite3_stmt *statement, int64_t id, int32_t row_count_max);
static bool event_store_load_target_tasks_row(sqlite3_stmt *statement, Linked_Task *task);
static bool event_store_load_task_targets_row(sqlite3_stmt *statement, Link_Target *target);
static bool event_store_set_task_targets_rows(Event_Store *store, int64_t task_id,
                                              const int64_t *target_ids, int32_t target_count);
static bool event_store_migrate(Event_Store *store);
static bool event_store_check_foreign_keys(Event_Store *store);
static bool event_store_migrate_step(Event_Store *store, int32_t version);
static bool event_store_write(Event_Store *store, sqlite3_stmt *statement, int result,
                              const char *context, bool row_expected);
static int bind_range(sqlite3_stmt *statement, Date first, Date last, int32_t row_count_max);
static int event_store_bind_fields(sqlite3_stmt *statement, const Event *event);
static int bind_time(sqlite3_stmt *statement, int index, int32_t minute_of_day);
static int bind_date(sqlite3_stmt *statement, int index, Date date);
static bool event_store_load_range_row(sqlite3_stmt *statement, Event *event);
static bool column_parse_date(sqlite3_stmt *statement, int column, Date *date);
static bool column_parse_time(sqlite3_stmt *statement, int column, int32_t *minute_of_day);
static bool column_parse_repeat(sqlite3_stmt *statement, Event *event);
static bool column_parse_kind(sqlite3_stmt *statement, int column, int32_t *kind);
static bool column_parse_target(sqlite3_stmt *statement, Event *event);
static bool column_parse_location(sqlite3_stmt *statement, Event *event);

bool event_store_open(Event_Store *store, const char *path, bool create) {
    assert(store != NULL);
    assert(path != NULL);
    *store = (Event_Store){ 0 };

    // SQLite takes the path as UTF-8, as the user types it, which the C library's fopen on
    // Windows would not, so SQLite alone decides whether the file is there.
    int flags = SQLITE_OPEN_READWRITE;
    if (create) {
        flags |= SQLITE_OPEN_CREATE;
    }
    int result = sqlite3_open_v2(path, &store->database, flags, NULL);
    if (result == SQLITE_CANTOPEN && !create) {
        // Without create, a missing file is the usual cause, and the message says it plainly.
        snprintf(store->error, sizeof(store->error),
                 "Database file not found, or it cannot be opened.");
        return false;
    }
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot open database");
    }
    if (sqlite3_exec(store->database, schema_sql, NULL, NULL, NULL) != SQLITE_OK) {
        return event_store_fail(store, "Cannot create schema");
    }
    // Migrations run with foreign keys off, as SQLite requires for rebuilding a table, and are
    // checked afterwards instead.
    if (!event_store_migrate(store)) {
        return false;
    }
    if (!event_store_check_foreign_keys(store)) {
        return false;
    }
    // Off by default in SQLite, and needed so that deleting a task deletes its completions.
    if (sqlite3_exec(store->database, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) !=
        SQLITE_OK) {
        return event_store_fail(store, "Cannot enable foreign keys");
    }
    // Persistent: these statements live for the whole session.
    for (int32_t i = 0; i < STATEMENT_COUNT; i++) {
        result = sqlite3_prepare_v3(store->database, statements_sql[i], -1,
                                    SQLITE_PREPARE_PERSISTENT, &store->statements[i], NULL);
        if (result != SQLITE_OK) {
            return event_store_fail(store, "Cannot prepare statement");
        }
        assert(store->statements[i] != NULL);
    }
    return true;
}

void event_store_close(Event_Store *store) {
    assert(store != NULL);
    // sqlite3_finalize and sqlite3_close accept NULL, so a partly opened store closes too.
    for (int32_t i = 0; i < STATEMENT_COUNT; i++) {
        sqlite3_finalize(store->statements[i]);
    }
    int result = sqlite3_close(store->database);
    // Every statement is finalized above, so the only failure left would be a programmer error.
    assert(result == SQLITE_OK);
    (void)result; // Unused when assertions are compiled out.
    *store = (Event_Store){ 0 };
}

int64_t event_store_insert(Event_Store *store, const Event *event) {
    assert(store != NULL);
    assert(event != NULL);
    assert(event->id == 0);
    assert(event->title[0] != '\0');
    assert(event_duration_days(event) < event_repeat_gap_days_min(event));

    sqlite3_stmt *statement = store->statements[STATEMENT_INSERT];
    int result = event_store_bind_fields(statement, event);
    if (!event_store_write(store, statement, result, "Cannot save", false)) {
        return 0;
    }
    int64_t id = sqlite3_last_insert_rowid(store->database);
    assert(id > 0);
    return id;
}

bool event_store_update(Event_Store *store, const Event *event) {
    assert(store != NULL);
    assert(event->id > 0);
    assert(event->title[0] != '\0');
    assert(event_duration_days(event) < event_repeat_gap_days_min(event));

    sqlite3_stmt *statement = store->statements[STATEMENT_UPDATE];
    int result = event_store_bind_fields(statement, event);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int64(statement, 12, event->id);
    }
    return event_store_write(store, statement, result, "Cannot save", true);
}

bool event_store_delete(Event_Store *store, int64_t id) {
    assert(store != NULL);
    assert(id > 0);
    sqlite3_stmt *statement = store->statements[STATEMENT_DELETE];
    int result = sqlite3_bind_int64(statement, 1, id);
    return event_store_write(store, statement, result, "Cannot delete", true);
}

bool event_store_set_task_done(Event_Store *store, int64_t id, Date occurrence_start, bool done) {
    assert(store != NULL);
    assert(id > 0);
    Statement which = STATEMENT_COMPLETION_DELETE;
    if (done) {
        which = STATEMENT_COMPLETION_INSERT;
    }
    sqlite3_stmt *statement = store->statements[which];
    int result = sqlite3_bind_int64(statement, 1, id);
    if (result == SQLITE_OK) {
        result = bind_date(statement, 2, occurrence_start);
    }
    // No row check: marking an occurrence that is already marked the same way changes nothing.
    return event_store_write(store, statement, result, "Cannot update task", false);
}

int64_t event_store_location_find_or_create(Event_Store *store, const char *address,
                                            const char *city) {
    assert(store != NULL);
    assert(strlen(address) < LOCATION_ADDRESS_SIZE);
    assert(strlen(city) < LOCATION_CITY_SIZE);
    assert(address[0] != '\0' || city[0] != '\0');

    sqlite3_stmt *statement = store->statements[STATEMENT_LOCATION_UPSERT];
    int result = sqlite3_bind_text(statement, 1, address, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 2, city, -1, SQLITE_TRANSIENT);
    }
    int64_t id = 0;
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    if (result == SQLITE_ROW) {
        id = sqlite3_column_int64(statement, 0);
        // RETURNING rows are only final once the statement has run to completion.
        result = sqlite3_step(statement);
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_DONE || id <= 0) {
        event_store_fail(store, "Cannot save location");
        return 0;
    }
    return id;
}

int32_t event_store_load_range(Event_Store *store, Date first, Date last,
                               Event *events, int32_t event_count_max, bool *truncated) {
    assert(store != NULL);
    assert(event_count_max > 0);
    assert(truncated != NULL);

    sqlite3_stmt *statement = store->statements[STATEMENT_SELECT_RANGE];
    int result = bind_range(statement, first, last, event_count_max);
    int32_t event_count = 0;
    *truncated = false;
    // Bounded by the LIMIT: at most event_count_max + 1 rows come back.
    for (int32_t row_index = 0; result == SQLITE_OK && row_index <= event_count_max; row_index++) {
        int step = sqlite3_step(statement);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            result = step;
            break;
        }
        if (row_index == event_count_max) {
            *truncated = true;
            break;
        }
        if (!event_store_load_range_row(statement, &events[event_count])) {
            // SQLite has nothing to add here: the row is readable, but its text is not ours.
            snprintf(store->error, sizeof(store->error), "Invalid stored event, id %lld.",
                     (long long)events[event_count].id);
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            return -1;
        }
        event_count++;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);

    if (result != SQLITE_OK) {
        event_store_fail(store, "Cannot load events");
        return -1;
    }
    assert(event_count <= event_count_max);
    return event_count;
}

int32_t event_store_load_completions(Event_Store *store, Date first, Date last,
                                     Task_Completion *completions, int32_t completion_count_max,
                                     bool *truncated) {
    assert(store != NULL);
    assert(completion_count_max > 0);
    assert(truncated != NULL);

    Date lookback = date_add_days(first, -COMPLETION_LOOKBACK_DAYS);
    sqlite3_stmt *statement = store->statements[STATEMENT_COMPLETION_SELECT_RANGE];
    int result = bind_range(statement, lookback, last, completion_count_max);
    int32_t completion_count = 0;
    *truncated = false;
    // Bounded by the LIMIT: at most completion_count_max + 1 rows come back.
    for (int32_t row_index = 0;
         result == SQLITE_OK && row_index <= completion_count_max; row_index++) {
        int step = sqlite3_step(statement);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            result = step;
            break;
        }
        if (row_index == completion_count_max) {
            *truncated = true;
            break;
        }
        Date occurrence_start = { 0, 0, 0 };
        if (!column_parse_date(statement, 1, &occurrence_start)) {
            result = SQLITE_MISMATCH;
            break;
        }
        completions[completion_count] = (Task_Completion){
            .event_id = sqlite3_column_int64(statement, 0),
            .occurrence_start_day = date_to_day_number(occurrence_start),
        };
        completion_count++;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);

    if (result != SQLITE_OK) {
        event_store_fail(store, "Cannot load task completions");
        return -1;
    }
    return completion_count;
}

int32_t event_store_load_target_tasks(Event_Store *store, int64_t target_id, Linked_Task *tasks,
                                      int32_t task_count_max, bool *truncated) {
    assert(target_id > 0);
    assert(task_count_max > 0);
    sqlite3_stmt *statement = store->statements[STATEMENT_TARGET_TASKS];
    int result = bind_id_and_limit(statement, target_id, task_count_max);
    int32_t task_count = 0;
    *truncated = false;
    // Bounded by the LIMIT: at most task_count_max + 1 rows come back.
    for (int32_t row_index = 0; result == SQLITE_OK && row_index <= task_count_max; row_index++) {
        int step = sqlite3_step(statement);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            result = step;
            break;
        }
        if (row_index == task_count_max) {
            *truncated = true;
            break;
        }
        if (!event_store_load_target_tasks_row(statement, &tasks[task_count])) {
            result = SQLITE_MISMATCH;
            break;
        }
        task_count++;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_OK) {
        event_store_fail(store, "Cannot load the linked tasks");
        return -1;
    }
    assert(task_count <= task_count_max);
    return task_count;
}

int32_t event_store_load_task_targets(Event_Store *store, int64_t task_id, bool problems_only,
                                      Link_Target *targets, int32_t target_count_max,
                                      bool *truncated) {
    assert(task_id >= 0);
    assert(target_count_max > 0);
    sqlite3_stmt *statement = store->statements[STATEMENT_TASK_TARGETS];
    // A task not stored yet has id 0, which no link has, so that no target comes back linked.
    int result = bind_id_and_limit(statement, task_id, target_count_max);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 3, (int)problems_only);
    }
    int32_t target_count = 0;
    *truncated = false;
    // Bounded by the LIMIT: at most target_count_max + 1 rows come back.
    for (int32_t row_index = 0;
         result == SQLITE_OK && row_index <= target_count_max; row_index++) {
        int step = sqlite3_step(statement);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            result = step;
            break;
        }
        if (row_index == target_count_max) {
            *truncated = true;
            break;
        }
        if (!event_store_load_task_targets_row(statement, &targets[target_count])) {
            result = SQLITE_MISMATCH;
            break;
        }
        assert(!problems_only || targets[target_count].kind == EVENT_KIND_PROBLEM);
        target_count++;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_OK) {
        event_store_fail(store, "Cannot load problems and opportunities");
        return -1;
    }
    assert(target_count <= target_count_max);
    return target_count;
}

/**
 * One transaction, so that a link that cannot be made leaves the task's earlier links as they
 * were rather than half replaced.
 */
bool event_store_set_task_targets(Event_Store *store, int64_t task_id, const int64_t *target_ids,
                                  int32_t target_count) {
    assert(task_id > 0);
    assert(target_count >= 0);
    assert(target_count <= LINK_COUNT_MAX);
    if (sqlite3_exec(store->database, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK) {
        return event_store_fail(store, "Cannot save the task's links");
    }
    bool ok = event_store_set_task_targets_rows(store, task_id, target_ids, target_count);
    if (ok) {
        if (sqlite3_exec(store->database, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
            ok = event_store_fail(store, "Cannot save the task's links");
        }
    }
    if (!ok) {
        // The message is already copied, so the rollback cannot overwrite it.
        sqlite3_exec(store->database, "ROLLBACK;", NULL, NULL, NULL);
    }
    return ok;
}

/**
 * Deletes the task's links, then inserts the new ones, inside the caller's transaction.
 */
static bool event_store_set_task_targets_rows(Event_Store *store, int64_t task_id,
                                              const int64_t *target_ids, int32_t target_count) {
    sqlite3_stmt *statement = store->statements[STATEMENT_LINK_DELETE];
    int result = sqlite3_bind_int64(statement, 1, task_id);
    // No row check: a task may have had no links.
    if (!event_store_write(store, statement, result, "Cannot save the task's links", false)) {
        return false;
    }
    statement = store->statements[STATEMENT_LINK_INSERT];
    for (int32_t i = 0; i < target_count; i++) {
        assert(target_ids[i] > 0);
        for (int32_t j = 0; j < i; j++) {
            assert(target_ids[j] != target_ids[i]); // The caller sends each target once.
        }
        result = sqlite3_bind_int64(statement, 1, target_ids[i]);
        if (result == SQLITE_OK) {
            result = sqlite3_bind_int64(statement, 2, task_id);
        }
        if (!event_store_write(store, statement, result, "Cannot link the task", false)) {
            return false;
        }
        // No row means the target or the task is gone, or not of its kind (see the statement).
        if (sqlite3_changes(store->database) != 1) {
            snprintf(store->error, sizeof(store->error),
                     "A chosen problem or opportunity, or the task, was deleted or changed kind "
                     "meanwhile.");
            return false;
        }
    }
    return true;
}

static bool event_store_fail(Event_Store *store, const char *context) {
    assert(store != NULL);
    assert(context != NULL);
    // sqlite3_errmsg accepts NULL and reports out of memory, which is what a NULL handle means.
    snprintf(store->error, sizeof(store->error), "%s: %s", context,
             sqlite3_errmsg(store->database));
    return false;
}

/**
 * Runs a bound write statement and resets it whatever the outcome, so that it can be reused.
 * With row_expected, a statement that changes no row fails: the row was deleted meanwhile,
 * for example from another tool.
 */
static bool event_store_write(Event_Store *store, sqlite3_stmt *statement, int result,
                              const char *context, bool row_expected) {
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_DONE) {
        return event_store_fail(store, context);
    }
    if (row_expected) {
        if (sqlite3_changes(store->database) != 1) {
            snprintf(store->error, sizeof(store->error), "It no longer exists.");
            return false;
        }
    }
    return true;
}

/**
 * Binds ?1 to the id and ?2 to one row more than wanted, as bind_range does.
 */
static int bind_id_and_limit(sqlite3_stmt *statement, int64_t id, int32_t row_count_max) {
    assert(id >= 0);
    assert(row_count_max > 0);
    int result = sqlite3_bind_int64(statement, 1, id);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 2, row_count_max + 1);
    }
    return result;
}

/**
 * Longer titles from outside this program are cut to fit, as in event_store_load_range_row.
 */
static bool event_store_load_target_tasks_row(sqlite3_stmt *statement, Linked_Task *task) {
    const char *title = (const char *)sqlite3_column_text(statement, 1);
    if (title == NULL || title[0] == '\0') {
        return false;
    }
    *task = (Linked_Task){
        .task_id = sqlite3_column_int64(statement, 0),
        .done = sqlite3_column_int(statement, 2) != 0,
    };
    snprintf(task->title, sizeof(task->title), "%s", title);
    return task->task_id > 0;
}

static bool event_store_load_task_targets_row(sqlite3_stmt *statement, Link_Target *target) {
    const char *title = (const char *)sqlite3_column_text(statement, TARGET_COLUMN_TITLE);
    if (title == NULL || title[0] == '\0') {
        return false;
    }
    int64_t task_count = sqlite3_column_int64(statement, TARGET_COLUMN_TASK_COUNT);
    int64_t task_done_count = sqlite3_column_int64(statement, TARGET_COLUMN_TASK_DONE_COUNT);
    // Counts that target_status would reject, which only a hand-edited database could hold.
    if (task_done_count < 0 || task_done_count > task_count || task_count > INT32_MAX) {
        return false;
    }
    *target = (Link_Target){
        .target_id = sqlite3_column_int64(statement, TARGET_COLUMN_ID),
        .task_count = (int32_t)task_count,
        .task_done_count = (int32_t)task_done_count,
        .linked = sqlite3_column_int(statement, TARGET_COLUMN_LINKED) != 0,
    };
    snprintf(target->title, sizeof(target->title), "%s", title);
    if (!column_parse_kind(statement, TARGET_COLUMN_KIND, &target->kind)) {
        return false;
    }
    if (!column_parse_date(statement, TARGET_COLUMN_END_DATE, &target->end_date)) {
        return false;
    }
    if (!column_parse_time(statement, TARGET_COLUMN_END_TIME, &target->end_minute)) {
        return false;
    }
    return target->target_id > 0 && event_kind_is_target(target->kind);
}

/**
 * Binds ?1 and ?2 to the dates and ?3 to one row more than wanted, so that the caller can tell
 * whether the result was cut short.
 */
static int bind_range(sqlite3_stmt *statement, Date first, Date last, int32_t row_count_max) {
    assert(date_to_day_number(first) <= date_to_day_number(last));
    int result = bind_date(statement, 1, first);
    if (result == SQLITE_OK) {
        result = bind_date(statement, 2, last);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 3, row_count_max + 1);
    }
    return result;
}

static int event_store_bind_fields(sqlite3_stmt *statement, const Event *event) {
    assert(event->repeat_unit >= 0);
    assert(event->repeat_unit < REPEAT_UNIT_COUNT);
    assert(event->kind >= 0);
    assert(event->kind < EVENT_KIND_COUNT);
    // SQLITE_TRANSIENT copies the title; SQLITE_STATIC is enough for the string literals.
    int result = sqlite3_bind_text(statement, 1, event->title, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK) {
        result = bind_date(statement, 2, event->start_date);
    }
    if (result == SQLITE_OK) {
        result = bind_time(statement, 3, event->start_minute);
    }
    if (result == SQLITE_OK) {
        result = bind_date(statement, 4, event->end_date);
    }
    if (result == SQLITE_OK) {
        result = bind_time(statement, 5, event->end_minute);
    }
    if (result == SQLITE_OK) {
        const char *unit = repeat_unit_names[event->repeat_unit];
        if (unit == NULL) {
            result = sqlite3_bind_null(statement, 6);
        } else {
            result = sqlite3_bind_text(statement, 6, unit, -1, SQLITE_STATIC);
        }
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 7, event->repeat_interval);
    }
    if (result == SQLITE_OK) {
        assert(!event->repeat_has_end || event->repeat_unit != REPEAT_NONE);
        if (event->repeat_has_end) {
            result = bind_date(statement, 8, event->repeat_end_date);
        } else {
            result = sqlite3_bind_null(statement, 8);
        }
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 9, event_kind_names[event->kind], -1,
                                   SQLITE_STATIC);
    }
    if (result == SQLITE_OK) {
        assert(event->location.id >= 0);
        if (event->location.id == 0) {
            result = sqlite3_bind_null(statement, 10);
        } else {
            result = sqlite3_bind_int64(statement, 10, event->location.id);
        }
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 11, event->description, -1, SQLITE_TRANSIENT);
    }
    return result;
}

static int bind_time(sqlite3_stmt *statement, int index, int32_t minute_of_day) {
    if (minute_of_day == TIME_NONE) {
        return sqlite3_bind_null(statement, index);
    }
    char text[TIME_TEXT_SIZE] = "";
    time_format(minute_of_day, text, sizeof(text));
    return sqlite3_bind_text(statement, index, text, -1, SQLITE_TRANSIENT);
}

static int bind_date(sqlite3_stmt *statement, int index, Date date) {
    char text[DATE_ISO_SIZE] = "";
    date_format_iso(date, text, sizeof(text));
    // SQLITE_TRANSIENT copies the text, because the buffer is on this stack frame.
    return sqlite3_bind_text(statement, index, text, -1, SQLITE_TRANSIENT);
}

/**
 * Returns false if the row holds text this program would never write, for example after the
 * database was edited by hand, rather than showing a half-parsed event.
 */
static bool event_store_load_range_row(sqlite3_stmt *statement, Event *event) {
    assert(event != NULL);
    *event = (Event){ .id = sqlite3_column_int64(statement, COLUMN_ID) };

    const char *title = (const char *)sqlite3_column_text(statement, COLUMN_TITLE);
    if (title == NULL || title[0] == '\0') {
        return false;
    }
    // Longer titles from outside this program are cut to fit, not rejected.
    snprintf(event->title, sizeof(event->title), "%s", title);

    if (!column_parse_date(statement, COLUMN_START_DATE, &event->start_date)) {
        return false;
    }
    if (!column_parse_time(statement, COLUMN_START_TIME, &event->start_minute)) {
        return false;
    }
    if (!column_parse_date(statement, COLUMN_END_DATE, &event->end_date)) {
        return false;
    }
    if (!column_parse_time(statement, COLUMN_END_TIME, &event->end_minute)) {
        return false;
    }

    // created_at is "YYYY-MM-DD HH:MM:SS"; split it into the date and the "HH:MM" part.
    const char *created_at = (const char *)sqlite3_column_text(statement, COLUMN_CREATED_AT);
    if (created_at == NULL || strlen(created_at) < 16) {
        return false;
    }
    char created_date[DATE_ISO_SIZE] = "";
    char created_time[TIME_TEXT_SIZE] = "";
    snprintf(created_date, sizeof(created_date), "%.10s", created_at);
    snprintf(created_time, sizeof(created_time), "%.5s", created_at + 11);
    if (!date_parse_iso(created_date, &event->created_date)) {
        return false;
    }
    if (!time_parse(created_time, &event->created_minute)) {
        return false;
    }
    if (date_to_day_number(event->end_date) < date_to_day_number(event->start_date)) {
        return false;
    }
    if (!column_parse_repeat(statement, event)) {
        return false;
    }
    if (!column_parse_kind(statement, COLUMN_KIND, &event->kind)) {
        return false;
    }
    if (!column_parse_location(statement, event)) {
        return false;
    }
    if (!column_parse_target(statement, event)) {
        return false;
    }
    // Longer text from outside this program is cut to fit, as titles are.
    const char *description = (const char *)sqlite3_column_text(statement, COLUMN_DESCRIPTION);
    if (description == NULL) {
        return false;
    }
    snprintf(event->description, sizeof(event->description), "%s", description);
    return event->id > 0;
}

/**
 * A link target's rules, which the database enforces as well, checked again because drawing
 * relies on them; and whether its tasks are done, which the query computes for targets only.
 */
static bool column_parse_target(sqlite3_stmt *statement, Event *event) {
    event->tasks_done = sqlite3_column_int(statement, COLUMN_TASKS_DONE) != 0;
    assert(!event->tasks_done || event_kind_is_target(event->kind));
    if (event->kind == EVENT_KIND_OPPORTUNITY) {
        return event->repeat_unit == REPEAT_NONE;
    }
    if (event->kind != EVENT_KIND_PROBLEM) {
        return true;
    }
    if (!date_equal(event->start_date, event->created_date)) {
        return false;
    }
    if (!date_equal(event->end_date, event->start_date)) {
        return false;
    }
    return event->repeat_unit == REPEAT_NONE;
}

/**
 * No location leaves event->location zeroed, with id 0. Longer texts from outside this program
 * are cut to fit, as titles are.
 */
static bool column_parse_location(sqlite3_stmt *statement, Event *event) {
    assert(event->location.id == 0);
    if (sqlite3_column_type(statement, COLUMN_LOCATION_ID) == SQLITE_NULL) {
        return true;
    }
    const char *address = (const char *)sqlite3_column_text(statement, COLUMN_LOCATION_ADDRESS);
    const char *city = (const char *)sqlite3_column_text(statement, COLUMN_LOCATION_CITY);
    if (address == NULL || city == NULL) {
        return false;
    }
    event->location.id = sqlite3_column_int64(statement, COLUMN_LOCATION_ID);
    snprintf(event->location.address, sizeof(event->location.address), "%s", address);
    snprintf(event->location.city, sizeof(event->location.city), "%s", city);
    return event->location.id > 0;
}

static bool column_parse_date(sqlite3_stmt *statement, int column, Date *date) {
    const char *text = (const char *)sqlite3_column_text(statement, column);
    if (text == NULL) {
        return false;
    }
    return date_parse_iso(text, date);
}

static bool column_parse_time(sqlite3_stmt *statement, int column, int32_t *minute_of_day) {
    const char *text = (const char *)sqlite3_column_text(statement, column);
    if (text == NULL) {
        *minute_of_day = TIME_NONE;
        return true;
    }
    return time_parse(text, minute_of_day);
}

/**
 * Checks the same rules as the form, because the rest of the program relies on them: a known
 * unit, an interval in range, and occurrences that do not overlap.
 */
static bool column_parse_repeat(sqlite3_stmt *statement, Event *event) {
    const char *unit = (const char *)sqlite3_column_text(statement, COLUMN_REPEAT_UNIT);
    // REPEAT_UNIT_COUNT stands for a unit that is not known, until one matches below.
    event->repeat_unit = REPEAT_UNIT_COUNT;
    if (unit == NULL) {
        event->repeat_unit = REPEAT_NONE;
    }
    for (int32_t i = REPEAT_NONE + 1; unit != NULL && i < REPEAT_UNIT_COUNT; i++) {
        if (strcmp(unit, repeat_unit_names[i]) == 0) {
            event->repeat_unit = i;
        }
    }
    if (event->repeat_unit == REPEAT_UNIT_COUNT) {
        return false;
    }

    int64_t interval = sqlite3_column_int64(statement, COLUMN_REPEAT_INTERVAL);
    if (interval < 1 || interval > REPEAT_INTERVAL_MAX) {
        return false;
    }
    event->repeat_interval = (int32_t)interval;

    // An end date on a one-time event means nothing, so it is ignored rather than rejected.
    event->repeat_has_end = false;
    if (event->repeat_unit != REPEAT_NONE) {
        if (sqlite3_column_type(statement, COLUMN_REPEAT_END_DATE) != SQLITE_NULL) {
            if (!column_parse_date(statement, COLUMN_REPEAT_END_DATE, &event->repeat_end_date)) {
                return false;
            }
            if (date_to_day_number(event->repeat_end_date) <
                date_to_day_number(event->start_date)) {
                return false;
            }
            event->repeat_has_end = true;
        }
    }
    return event_duration_days(event) < event_repeat_gap_days_min(event);
}

static bool column_parse_kind(sqlite3_stmt *statement, int column, int32_t *kind) {
    const char *text = (const char *)sqlite3_column_text(statement, column);
    if (text == NULL) {
        return false;
    }
    for (int32_t i = 0; i < EVENT_KIND_COUNT; i++) {
        if (strcmp(text, event_kind_names[i]) == 0) {
            *kind = i;
            return true;
        }
    }
    return false;
}

static bool event_store_migrate(Event_Store *store) {
    sqlite3_stmt *statement = NULL;
    int result = sqlite3_prepare_v2(store->database, "PRAGMA user_version;", -1, &statement,
                                    NULL);
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    int32_t version = -1;
    if (result == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    if (version < 0) {
        return event_store_fail(store, "Cannot read schema version");
    }
    if (version > SCHEMA_VERSION) {
        snprintf(store->error, sizeof(store->error),
                 "Database is from a newer version of this program.");
        return false;
    }
    // Bounded by SCHEMA_VERSION, the number of migrations.
    for (int32_t step = version; step < SCHEMA_VERSION; step++) {
        if (!event_store_migrate_step(store, step)) {
            return false;
        }
    }
    return true;
}

/**
 * Reports a database whose links between tables are broken, for example by a bad migration,
 * rather than letting it fail later in confusing ways.
 */
static bool event_store_check_foreign_keys(Event_Store *store) {
    sqlite3_stmt *statement = NULL;
    int result = sqlite3_prepare_v2(store->database, "PRAGMA foreign_key_check;", -1, &statement,
                                    NULL);
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    sqlite3_finalize(statement);
    if (result == SQLITE_ROW) {
        snprintf(store->error, sizeof(store->error), "Database has broken links between tables.");
        return false;
    }
    if (result != SQLITE_DONE) {
        return event_store_fail(store, "Cannot check database links");
    }
    return true;
}

/**
 * One transaction per step, so that a failed step leaves the database at the previous version.
 */
static bool event_store_migrate_step(Event_Store *store, int32_t version) {
    assert(version >= 0);
    assert(version < SCHEMA_VERSION);
    char set_version_sql[64] = "";
    snprintf(set_version_sql, sizeof(set_version_sql), "PRAGMA user_version = %d;",
             (int)version + 1);

    bool ok = sqlite3_exec(store->database, "BEGIN;", NULL, NULL, NULL) == SQLITE_OK;
    if (ok) {
        ok = sqlite3_exec(store->database, migrations_sql[version], NULL, NULL, NULL) ==
             SQLITE_OK;
    }
    if (ok) {
        ok = sqlite3_exec(store->database, set_version_sql, NULL, NULL, NULL) == SQLITE_OK;
    }
    if (ok) {
        ok = sqlite3_exec(store->database, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
    }
    if (!ok) {
        event_store_fail(store, "Cannot migrate database");
        // The message is already copied, so the rollback cannot overwrite it.
        sqlite3_exec(store->database, "ROLLBACK;", NULL, NULL, NULL);
    }
    return ok;
}
