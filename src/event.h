#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "calendar.h"

enum {
    // Bytes including the terminator. Bounds the form input and every loaded title.
    EVENT_TITLE_SIZE = 64,
    // "Every X days" accepts up to this many days, so that X fits a three-digit field.
    REPEAT_INTERVAL_MAX = 999,
};

typedef enum RepeatUnit {
    REPEAT_NONE,
    REPEAT_DAY,
    REPEAT_WEEK,
    REPEAT_MONTH,  // Same day of the month; skips months that do not have that day
    REPEAT_YEAR,   // Same date; 29 February repeats only in leap years
    REPEAT_UNIT_COUNT,
} RepeatUnit;

// What kind of calendar entry an Event is. Both kinds share every field and rule; a task can
// also be checked off as done, once per occurrence.
typedef enum EventKind {
    EVENT_KIND_EVENT,
    EVENT_KIND_TASK,
    EVENT_KIND_COUNT,
} EventKind;

// Any calendar entry: an event or a task, as its kind says.
typedef struct Event {
    int64_t id;               // 0 until the event is stored
    Date start_date;          // Start of the first occurrence
    Date end_date;            // End of the first occurrence
    Date created_date;        // Local date when the event was inserted
    Date repeat_end_date;     // Last day an occurrence may start, if repeat_has_end
    int32_t start_minute;     // Minute of day, or TIME_NONE
    int32_t end_minute;       // Minute of day, or TIME_NONE
    int32_t created_minute;   // Local minute of day when the event was inserted
    int32_t repeat_unit;      // RepeatUnit
    int32_t repeat_interval;  // Units between occurrences; 1 ... REPEAT_INTERVAL_MAX
    int32_t kind;             // EventKind
    bool repeat_has_end;     // False repeats forever; always false for one-time events
    char title[EVENT_TITLE_SIZE];
} Event;

// Whole days from the start date to the end date of one occurrence; 0 for a one-day event.
int32_t event_duration_days(const Event *event);

// The fewest days between the starts of two occurrences. An occurrence must be shorter than
// this, so that occurrences never overlap and at most one covers any given day.
int32_t event_repeat_gap_days_min(const Event *event);

// Finds the occurrence that covers day_number. Returns false if none does.
bool event_occurrence_on_day(const Event *event, int32_t day_number, int32_t *start_day);

// A copy of the event with its dates moved to the occurrence that starts on start_day.
Event event_occurrence_at(const Event *event, int32_t start_day);

// "every week", "every 10 days", and so on; empty for an event that does not repeat.
void event_format_repeat(const Event *event, char *buffer, size_t buffer_size);

#endif
