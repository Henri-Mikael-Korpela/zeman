#include "event_store.h"

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
};
enum { SCHEMA_VERSION = sizeof(migrations_sql) / sizeof(migrations_sql[0]) };

// Text stored in repeat_unit, indexed by RepeatUnit. NULL is stored for REPEAT_NONE.
static const char *const repeat_unit_names[REPEAT_UNIT_COUNT] = {
    NULL, "day", "week", "month", "year",
};

// Text stored in kind, indexed by EventKind.
static const char *const event_kind_names[EVENT_KIND_COUNT] = { "event", "task" };

typedef enum Statement {
    STATEMENT_INSERT,
    STATEMENT_UPDATE,
    STATEMENT_DELETE,
    STATEMENT_SELECT_RANGE,
    STATEMENT_COMPLETION_INSERT,
    STATEMENT_COMPLETION_DELETE,
    STATEMENT_COMPLETION_SELECT_RANGE,
    STATEMENT_COUNT,
} Statement;

// Cast because the constants come from different enums.
static_assert((int)STATEMENT_COUNT == (int)EVENT_STORE_STATEMENT_COUNT,
              "EventStore holds every statement.");

// Insert and update share parameters 1 to 9, so that both bind with event_store_bind_fields.
static const char *const statements_sql[STATEMENT_COUNT] = {
    [STATEMENT_INSERT] =
        "INSERT INTO events"
        " (title, start_date, start_time, end_date, end_time, repeat_unit, repeat_interval,"
        "  repeat_end_date, kind)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);",
    [STATEMENT_UPDATE] =
        "UPDATE events SET title = ?1, start_date = ?2, start_time = ?3, end_date = ?4,"
        " end_time = ?5, repeat_unit = ?6, repeat_interval = ?7, repeat_end_date = ?8,"
        " kind = ?9 WHERE id = ?10;",
    [STATEMENT_DELETE] = "DELETE FROM events WHERE id = ?1;",
    // Ordered by time of day, all-day events first, because the caller lists the events of one
    // day at a time and a repeating event's first start date says nothing about that order.
    // A repeating event with an end is visible until its last occurrence ends: the end date
    // plus one occurrence's length.
    [STATEMENT_SELECT_RANGE] =
        "SELECT id, title, start_date, start_time, end_date, end_time, created_at,"
        "       repeat_unit, repeat_interval, repeat_end_date, kind FROM events"
        " WHERE (start_date <= ?2 AND (end_date >= ?1 OR (repeat_unit IS NOT NULL"
        "        AND (repeat_end_date IS NULL OR julianday(repeat_end_date)"
        "             + julianday(end_date) - julianday(start_date) >= julianday(?1)))))"
        "    OR (substr(created_at, 1, 10) BETWEEN ?1 AND ?2)"
        " ORDER BY start_time IS NOT NULL, start_time, start_date, id"
        " LIMIT ?3;",
    [STATEMENT_COMPLETION_INSERT] =
        "INSERT OR IGNORE INTO task_completions (event_id, occurrence_date) VALUES (?1, ?2);",
    [STATEMENT_COMPLETION_DELETE] =
        "DELETE FROM task_completions WHERE event_id = ?1 AND occurrence_date = ?2;",
    [STATEMENT_COMPLETION_SELECT_RANGE] =
        "SELECT event_id, occurrence_date FROM task_completions"
        " WHERE occurrence_date BETWEEN ?1 AND ?2 ORDER BY event_id LIMIT ?3;",
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
};

static bool event_store_fail(EventStore *store, const char *context);
static bool event_store_migrate(EventStore *store);
static bool event_store_migrate_step(EventStore *store, int32_t version);
static bool event_store_write(EventStore *store, sqlite3_stmt *statement, int result,
                              const char *context, bool row_expected);
static int bind_range(sqlite3_stmt *statement, Date first, Date last, int32_t row_count_max);
static int event_store_bind_fields(sqlite3_stmt *statement, const Event *event);
static int bind_time(sqlite3_stmt *statement, int index, int32_t minute_of_day);
static int bind_date(sqlite3_stmt *statement, int index, Date date);
static bool event_store_load_range_row(sqlite3_stmt *statement, Event *event);
static bool column_parse_date(sqlite3_stmt *statement, int column, Date *date);
static bool column_parse_time(sqlite3_stmt *statement, int column, int32_t *minute_of_day);
static bool column_parse_repeat(sqlite3_stmt *statement, Event *event);
static bool column_parse_kind(sqlite3_stmt *statement, Event *event);

bool event_store_open(EventStore *store, const char *path) {
    assert(store != NULL);
    assert(path != NULL);
    *store = (EventStore){ 0 };

    int result = sqlite3_open_v2(path, &store->database,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot open database");
    }
    // Off by default in SQLite, and needed so that deleting a task deletes its completions.
    if (sqlite3_exec(store->database, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) !=
        SQLITE_OK) {
        return event_store_fail(store, "Cannot enable foreign keys");
    }
    if (sqlite3_exec(store->database, schema_sql, NULL, NULL, NULL) != SQLITE_OK) {
        return event_store_fail(store, "Cannot create schema");
    }
    if (!event_store_migrate(store)) {
        return false;
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

void event_store_close(EventStore *store) {
    assert(store != NULL);
    // sqlite3_finalize and sqlite3_close accept NULL, so a partly opened store closes too.
    for (int32_t i = 0; i < STATEMENT_COUNT; i++) {
        sqlite3_finalize(store->statements[i]);
    }
    int result = sqlite3_close(store->database);
    // Every statement is finalized above, so the only failure left would be a programmer error.
    assert(result == SQLITE_OK);
    (void)result; // Unused when assertions are compiled out.
    *store = (EventStore){ 0 };
}

int64_t event_store_insert(EventStore *store, const Event *event) {
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

bool event_store_update(EventStore *store, const Event *event) {
    assert(store != NULL);
    assert(event->id > 0);
    assert(event->title[0] != '\0');
    assert(event_duration_days(event) < event_repeat_gap_days_min(event));

    sqlite3_stmt *statement = store->statements[STATEMENT_UPDATE];
    int result = event_store_bind_fields(statement, event);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int64(statement, 10, event->id);
    }
    return event_store_write(store, statement, result, "Cannot save", true);
}

bool event_store_delete(EventStore *store, int64_t id) {
    assert(store != NULL);
    assert(id > 0);
    sqlite3_stmt *statement = store->statements[STATEMENT_DELETE];
    int result = sqlite3_bind_int64(statement, 1, id);
    return event_store_write(store, statement, result, "Cannot delete", true);
}

bool event_store_set_task_done(EventStore *store, int64_t id, Date occurrence_start, bool done) {
    assert(store != NULL);
    assert(id > 0);
    Statement which = done ? STATEMENT_COMPLETION_INSERT : STATEMENT_COMPLETION_DELETE;
    sqlite3_stmt *statement = store->statements[which];
    int result = sqlite3_bind_int64(statement, 1, id);
    if (result == SQLITE_OK) {
        result = bind_date(statement, 2, occurrence_start);
    }
    // No row check: marking an occurrence that is already marked the same way changes nothing.
    return event_store_write(store, statement, result, "Cannot update task", false);
}

int32_t event_store_load_range(EventStore *store, Date first, Date last,
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

int32_t event_store_load_completions(EventStore *store, Date first, Date last,
                                     TaskCompletion *completions, int32_t completion_count_max,
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
        completions[completion_count] = (TaskCompletion){
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

static bool event_store_fail(EventStore *store, const char *context) {
    assert(store != NULL);
    assert(context != NULL);
    // sqlite3_errmsg accepts NULL and reports out of memory, which is what a NULL handle means.
    snprintf(store->error, sizeof(store->error), "%s: %s", context,
             sqlite3_errmsg(store->database));
    return false;
}

// Runs a bound write statement and resets it whatever the outcome, so that it can be reused.
// With row_expected, a statement that changes no row fails: the row was deleted meanwhile,
// for example from another tool.
static bool event_store_write(EventStore *store, sqlite3_stmt *statement, int result,
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

// Binds ?1 and ?2 to the dates and ?3 to one row more than wanted, so that the caller can tell
// whether the result was cut short.
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
        result = unit == NULL ? sqlite3_bind_null(statement, 6)
                              : sqlite3_bind_text(statement, 6, unit, -1, SQLITE_STATIC);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 7, event->repeat_interval);
    }
    if (result == SQLITE_OK) {
        assert(!event->repeat_has_end || event->repeat_unit != REPEAT_NONE);
        result = event->repeat_has_end ? bind_date(statement, 8, event->repeat_end_date)
                                       : sqlite3_bind_null(statement, 8);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 9, event_kind_names[event->kind], -1,
                                   SQLITE_STATIC);
    }
    return result;
}

static int bind_time(sqlite3_stmt *statement, int index, int32_t minute_of_day) {
    if (minute_of_day == TIME_NONE) {
        return sqlite3_bind_null(statement, index);
    }
    char text[TIME_TEXT_SIZE];
    time_format(minute_of_day, text, sizeof(text));
    return sqlite3_bind_text(statement, index, text, -1, SQLITE_TRANSIENT);
}

static int bind_date(sqlite3_stmt *statement, int index, Date date) {
    char text[DATE_ISO_SIZE];
    date_format_iso(date, text, sizeof(text));
    // SQLITE_TRANSIENT copies the text, because the buffer is on this stack frame.
    return sqlite3_bind_text(statement, index, text, -1, SQLITE_TRANSIENT);
}

// Returns false if the row holds text this program would never write, for example after the
// database was edited by hand, rather than showing a half-parsed event.
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
    char created_date[DATE_ISO_SIZE];
    char created_time[TIME_TEXT_SIZE];
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
    if (!column_parse_kind(statement, event)) {
        return false;
    }
    return event->id > 0;
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

// Checks the same rules as the form, because the rest of the program relies on them: a known
// unit, an interval in range, and occurrences that do not overlap.
static bool column_parse_repeat(sqlite3_stmt *statement, Event *event) {
    const char *unit = (const char *)sqlite3_column_text(statement, COLUMN_REPEAT_UNIT);
    event->repeat_unit = unit == NULL ? REPEAT_NONE : REPEAT_UNIT_COUNT;
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

static bool column_parse_kind(sqlite3_stmt *statement, Event *event) {
    const char *kind = (const char *)sqlite3_column_text(statement, COLUMN_KIND);
    if (kind == NULL) {
        return false;
    }
    for (int32_t i = 0; i < EVENT_KIND_COUNT; i++) {
        if (strcmp(kind, event_kind_names[i]) == 0) {
            event->kind = i;
            return true;
        }
    }
    return false;
}

static bool event_store_migrate(EventStore *store) {
    sqlite3_stmt *statement = NULL;
    int result = sqlite3_prepare_v2(store->database, "PRAGMA user_version;", -1, &statement,
                                    NULL);
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    int32_t version = result == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
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

// One transaction per step, so that a failed step leaves the database at the previous version.
static bool event_store_migrate_step(EventStore *store, int32_t version) {
    assert(version >= 0);
    assert(version < SCHEMA_VERSION);
    char set_version_sql[64];
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
