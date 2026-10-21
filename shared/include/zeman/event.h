#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zeman/calendar.h>

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

typedef enum Repeat_Unit {
    REPEAT_NONE,
    REPEAT_DAY,
    REPEAT_WEEK,
    REPEAT_MONTH,  // Same day of the month; skips months that do not have that day
    REPEAT_YEAR,   // Same date; 29 February repeats only in leap years
    REPEAT_UNIT_COUNT,
} Repeat_Unit;

// What kind of calendar entry an Event is. Events and tasks share every field and rule; a task
// can also be checked off as done, once per occurrence. A problem records something that went
// wrong: it has only a title, a description and its creation time, and it always falls on the
// day it was created, so it has no times, location or repeat. An opportunity is a chance that
// is open from its start until its deadline, the end date and time, and is used by doing the
// tasks linked to it; it happens once, so it has no repeat. Problems and opportunities are the
// link targets: the entries that tasks can be linked to.
typedef enum Event_Kind {
    EVENT_KIND_EVENT,
    EVENT_KIND_TASK,
    EVENT_KIND_PROBLEM,
    EVENT_KIND_OPPORTUNITY,
    EVENT_KIND_COUNT,
} Event_Kind;

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
    int32_t repeat_unit;      // Repeat_Unit
    int32_t repeat_interval;  // Units between occurrences; 1 ... REPEAT_INTERVAL_MAX
    int32_t kind;             // Event_Kind
    bool repeat_has_end;     // False repeats forever; always false for one-time events
    // A link target whose linked tasks are all done (see Target_Status): a solved problem or a
    // used opportunity. Worked out when loading, so that the calendar can show it without
    // loading links; never saved, and always false for events and tasks.
    bool tasks_done;
    char title[EVENT_TITLE_SIZE];
    char description[EVENT_DESCRIPTION_SIZE];  // Empty for none
} Event;

// One checked-off occurrence of a task.
typedef struct Task_Completion {
    int64_t event_id;
    int32_t occurrence_start_day;  // Day number of the occurrence's start date
} Task_Completion;

enum {
    // Tasks linked to one link target, and targets offered to one task, loaded at once. Far
    // more than one target plausibly needs; the form says when more exist.
    LINK_COUNT_MAX = 64,
};

// A task linked to a link target: completing the task helps solve the problem, or use the
// opportunity. A task counts as done once any of its occurrences is checked off, because doing
// it once is what the target needs, and a repeating task's later occurrences are reminders.
typedef struct Linked_Task {
    int64_t task_id;
    bool done;  // An occurrence of the task is checked off
    char title[EVENT_TITLE_SIZE];
} Linked_Task;

// A link target as a task's form offers it for linking, with how far its linked tasks are done.
typedef struct Link_Target {
    int64_t target_id;
    // The deadline, so that a missed opportunity shows as missed; a problem's own day.
    Date end_date;
    int32_t end_minute;       // Minute of day, or TIME_NONE
    int32_t task_count;       // Tasks linked to the target
    int32_t task_done_count;  // Of those, the done ones (see Linked_Task)
    int32_t kind;             // EVENT_KIND_PROBLEM or EVENT_KIND_OPPORTUNITY
    bool linked;              // Linked to the task the form is for
    char title[EVENT_TITLE_SIZE];
} Link_Target;

// How far the tasks linked to a target are done.
typedef enum Target_Status {
    TARGET_STATUS_NO_TASKS,     // No tasks linked yet
    TARGET_STATUS_PLANNED,      // Tasks linked, none of them done
    TARGET_STATUS_PARTLY_DONE,  // Some linked tasks done
    TARGET_STATUS_DONE,         // Every linked task done
    TARGET_STATUS_COUNT,
} Target_Status;

/**
 * Checks every rule an event must follow before it is stored: terminated texts, a title, valid
 * dates and times, a known kind and repeat, and occurrences that do not overlap; a problem has
 * one day and no times, repeat or location, and an opportunity no repeat. The form enforces
 * these already; this is for data from outside the program, such as the network, which the
 * store's assertions must never see. That a problem falls on the day it was created is left to
 * the caller and the database.
 */
bool event_is_valid(const Event *event);

/**
 * A link target that tasks can be linked to: a problem or an opportunity.
 */
bool event_kind_is_target(int32_t kind);

/**
 * An opportunity whose deadline has passed before its linked tasks were all done. The deadline
 * is inclusive: one at 17:00 is missed from 17:01, and one without a time from the next day.
 */
bool opportunity_is_missed(Date end_date, int32_t end_minute, bool tasks_done, Date_Time now);

/**
 * Whole days from the start date to the end date of one occurrence; 0 for a one-day event.
 */
int32_t event_duration_days(const Event *event);

/**
 * The fewest days between the starts of two occurrences. An occurrence must be shorter than
 * this, so that occurrences never overlap and at most one covers any given day.
 */
int32_t event_repeat_gap_days_min(const Event *event);

/**
 * Finds the occurrence that covers day_number. Returns false if none does.
 */
bool event_occurrence_on_day(const Event *event, int32_t day_number, int32_t *start_day);

/**
 * A copy of the event with its dates moved to the occurrence that starts on start_day.
 */
Event event_occurrence_at(const Event *event, int32_t start_day);

/**
 * "every week", "every 10 days", and so on; empty for an event that does not repeat.
 */
void event_format_repeat(const Event *event, char *buffer, size_t buffer_size);

Target_Status target_status(int32_t task_count, int32_t task_done_count);
/**
 * "No planned tasks", "Planned", "Partly completed" or "Completed".
 */
const char *target_status_name(Target_Status status);

#endif
