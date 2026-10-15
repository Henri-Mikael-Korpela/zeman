#include "event_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sqlite3.h"

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
};
enum { SCHEMA_VERSION = sizeof(migrations_sql) / sizeof(migrations_sql[0]) };

// Text stored in repeat_unit, indexed by RepeatUnit. NULL is stored for REPEAT_NONE.
static const char *const repeat_unit_names[REPEAT_UNIT_COUNT] = {
    NULL, "day", "week", "month", "year",
};

static const char insert_sql[] =
    "INSERT INTO events"
    " (title, start_date, start_time, end_date, end_time, repeat_unit, repeat_interval)"
    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);";

// Ordered by time of day, all-day events first, because the caller lists the events of one
// day at a time and a repeating event's first start date says nothing about that order.
static const char delete_sql[] = "DELETE FROM events WHERE id = ?1;";

static const char select_range_sql[] =
    "SELECT id, title, start_date, start_time, end_date, end_time, created_at,"
    "       repeat_unit, repeat_interval FROM events"
    " WHERE (start_date <= ?2 AND (end_date >= ?1 OR repeat_unit IS NOT NULL))"
    "    OR (substr(created_at, 1, 10) BETWEEN ?1 AND ?2)"
    " ORDER BY start_time IS NOT NULL, start_time, start_date, id"
    " LIMIT ?3;";

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
};

static bool event_store_fail(EventStore *store, const char *context);
static bool event_store_migrate(EventStore *store);
static bool event_store_migrate_step(EventStore *store, int32_t version);
static int event_store_insert_bind(sqlite3_stmt *statement, const Event *event);
static int bind_time(sqlite3_stmt *statement, int index, int32_t minute_of_day);
static bool event_store_load_range_row(sqlite3_stmt *statement, Event *event);
static bool column_parse_date(sqlite3_stmt *statement, int column, Date *date);
static bool column_parse_time(sqlite3_stmt *statement, int column, int32_t *minute_of_day);
static bool column_parse_repeat(sqlite3_stmt *statement, Event *event);

bool event_store_open(EventStore *store, const char *path) {
    assert(store != NULL);
    assert(path != NULL);
    *store = (EventStore){ 0 };

    int result = sqlite3_open_v2(path, &store->database,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot open database");
    }
    if (sqlite3_exec(store->database, schema_sql, NULL, NULL, NULL) != SQLITE_OK) {
        return event_store_fail(store, "Cannot create schema");
    }
    if (!event_store_migrate(store)) {
        return false;
    }
    // Persistent: these statements live for the whole session.
    result = sqlite3_prepare_v3(store->database, insert_sql, -1, SQLITE_PREPARE_PERSISTENT,
                                &store->statement_insert, NULL);
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot prepare insert");
    }
    result = sqlite3_prepare_v3(store->database, select_range_sql, -1,
                                SQLITE_PREPARE_PERSISTENT, &store->statement_select_range, NULL);
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot prepare select");
    }
    result = sqlite3_prepare_v3(store->database, delete_sql, -1, SQLITE_PREPARE_PERSISTENT,
                                &store->statement_delete, NULL);
    if (result != SQLITE_OK) {
        return event_store_fail(store, "Cannot prepare delete");
    }
    assert(store->statement_insert != NULL);
    assert(store->statement_select_range != NULL);
    assert(store->statement_delete != NULL);
    return true;
}

void event_store_close(EventStore *store) {
    assert(store != NULL);
    // sqlite3_finalize and sqlite3_close accept NULL, so a partly opened store closes too.
    sqlite3_finalize(store->statement_insert);
    sqlite3_finalize(store->statement_select_range);
    sqlite3_finalize(store->statement_delete);
    int result = sqlite3_close(store->database);
    // Every statement is finalized above, so the only failure left would be a programmer error.
    assert(result == SQLITE_OK);
    (void)result; // Unused when assertions are compiled out.
    *store = (EventStore){ 0 };
}

int64_t event_store_insert(EventStore *store, const Event *event) {
    assert(store != NULL);
    assert(store->statement_insert != NULL);
    assert(event != NULL);
    assert(event->id == 0);
    assert(event->title[0] != '\0');
    assert(event->repeat_unit >= 0);
    assert(event->repeat_unit < REPEAT_UNIT_COUNT);
    assert(event_duration_days(event) < event_repeat_gap_days_min(event));

    sqlite3_stmt *statement = store->statement_insert;
    int result = event_store_insert_bind(statement, event);
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    // Reset before reporting, so that the statement is reusable whatever the outcome.
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_DONE) {
        event_store_fail(store, "Cannot save event");
        return 0;
    }

    int64_t id = sqlite3_last_insert_rowid(store->database);
    assert(id > 0);
    return id;
}

bool event_store_delete(EventStore *store, int64_t id) {
    assert(store != NULL);
    assert(store->statement_delete != NULL);
    assert(id > 0);

    sqlite3_stmt *statement = store->statement_delete;
    int result = sqlite3_bind_int64(statement, 1, id);
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    if (result != SQLITE_DONE) {
        return event_store_fail(store, "Cannot delete event");
    }
    // Zero rows means the event was already gone, for example deleted from another tool.
    if (sqlite3_changes(store->database) != 1) {
        snprintf(store->error, sizeof(store->error), "Event no longer exists.");
        return false;
    }
    return true;
}

int32_t event_store_load_range(EventStore *store, Date first, Date last,
                               Event *events, int32_t event_count_max, bool *truncated) {
    assert(store != NULL);
    assert(store->statement_select_range != NULL);
    assert(date_to_day_number(first) <= date_to_day_number(last));
    assert(event_count_max > 0);
    assert(truncated != NULL);

    char first_iso[DATE_ISO_SIZE];
    char last_iso[DATE_ISO_SIZE];
    date_format_iso(first, first_iso, sizeof(first_iso));
    date_format_iso(last, last_iso, sizeof(last_iso));

    sqlite3_stmt *statement = store->statement_select_range;
    int result = sqlite3_bind_text(statement, 1, first_iso, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 2, last_iso, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK) {
        // One extra row tells us whether the result was cut short.
        result = sqlite3_bind_int(statement, 3, event_count_max + 1);
    }

    int32_t event_count = 0;
    *truncated = false;
    // Bounded by the LIMIT bound above: at most event_count_max + 1 rows come back.
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

static bool event_store_fail(EventStore *store, const char *context) {
    assert(store != NULL);
    assert(context != NULL);
    // sqlite3_errmsg accepts NULL and reports out of memory, which is what a NULL handle means.
    snprintf(store->error, sizeof(store->error), "%s: %s", context,
             sqlite3_errmsg(store->database));
    return false;
}

static int event_store_insert_bind(sqlite3_stmt *statement, const Event *event) {
    char start_iso[DATE_ISO_SIZE];
    char end_iso[DATE_ISO_SIZE];
    date_format_iso(event->start_date, start_iso, sizeof(start_iso));
    date_format_iso(event->end_date, end_iso, sizeof(end_iso));

    // SQLITE_TRANSIENT copies the text, because the buffers are on this stack frame.
    int result = sqlite3_bind_text(statement, 1, event->title, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 2, start_iso, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK) {
        result = bind_time(statement, 3, event->start_minute);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_text(statement, 4, end_iso, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK) {
        result = bind_time(statement, 5, event->end_minute);
    }
    if (result == SQLITE_OK) {
        // SQLITE_STATIC: the names are string literals.
        const char *unit = repeat_unit_names[event->repeat_unit];
        result = unit == NULL ? sqlite3_bind_null(statement, 6)
                              : sqlite3_bind_text(statement, 6, unit, -1, SQLITE_STATIC);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_bind_int(statement, 7, event->repeat_interval);
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
    return event_duration_days(event) < event_repeat_gap_days_min(event);
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
