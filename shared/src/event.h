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
    // Bytes including the terminator. The description is one line of free text.
    EVENT_DESCRIPTION_SIZE = 200,
    LOCATION_ADDRESS_SIZE = 64,
    LOCATION_CITY_SIZE = 48,
};

// A place where events and tasks happen. Locations are shared: every event saved with the same
// address and city links to the same location.
typedef struct Location {
    int64_t id;                           // 0 when an event has no location
    char address[LOCATION_ADDRESS_SIZE];  // May be empty if city is not
    char city[LOCATION_CITY_SIZE];        // May be empty if address is not
} Location;

typedef enum RepeatUnit {
    REPEAT_NONE,
    REPEAT_DAY,
    REPEAT_WEEK,
    REPEAT_MONTH,  // Same day of the month; skips months that do not have that day
    REPEAT_YEAR,   // Same date; 29 February repeats only in leap years
    REPEAT_UNIT_COUNT,
} RepeatUnit;

// What kind of calendar entry an Event is. Events and tasks share every field and rule; a task
// can also be checked off as done, once per occurrence. A problem records something that went
// wrong: it has only a title, a description and its creation time, and it always falls on the
// day it was created, so it has no times, location or repeat.
typedef enum EventKind {
    EVENT_KIND_EVENT,
    EVENT_KIND_TASK,
    EVENT_KIND_PROBLEM,
    EVENT_KIND_COUNT,
} EventKind;

// Any calendar entry: an event or a task, as its kind says.
typedef struct Event {
    int64_t id;               // 0 until the event is stored
    Location location;        // location.id is 0 for none
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
    char description[EVENT_DESCRIPTION_SIZE];  // Empty for none
} Event;

// One checked-off occurrence of a task.
typedef struct TaskCompletion {
    int64_t event_id;
    int32_t occurrence_start_day;  // Day number of the occurrence's start date
} TaskCompletion;

// Checks every rule an event must follow before it is stored: terminated texts, a title, valid
// dates and times, a known kind and repeat, and occurrences that do not overlap; a problem has
// one day and no times, repeat or location. The form enforces these already; this is for data
// from outside the program, such as the network, which the store's assertions must never see.
// That a problem falls on the day it was created is left to the caller and the database.
bool event_is_valid(const Event *event);

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
