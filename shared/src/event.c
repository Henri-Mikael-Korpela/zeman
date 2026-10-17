#include "event.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    // Monthly and yearly occurrences skip months without the start's day, so finding the
    // latest one walks back over skipped months. 29 February is the worst case: leap years can
    // be 8 years apart (2096 to 2104), plus one step for a later day in the current month.
    // The form only offers an interval of 1 for months and years, which this bound covers.
    MONTH_SEARCH_STEP_MAX = 10,
    // The shortest month and year, for event_repeat_gap_days_min.
    MONTH_DAYS_MIN = 28,
    YEAR_DAYS_MIN = 365,
};

static bool event_occurrence_on_day_monthly(const Event *event, int32_t day_number,
                                            int32_t month_step, int32_t *start_day);
static bool minute_is_valid(int32_t minute_of_day);
static bool text_is_terminated(const char *text, size_t size);

int32_t event_duration_days(const Event *event) {
    int32_t duration = date_to_day_number(event->end_date) -
                       date_to_day_number(event->start_date);
    assert(duration >= 0);
    return duration;
}

int32_t event_repeat_gap_days_min(const Event *event) {
    assert(event->repeat_interval >= 1);
    assert(event->repeat_interval <= REPEAT_INTERVAL_MAX);
    switch ((RepeatUnit)event->repeat_unit) {
    case REPEAT_NONE:
        // A one-time event never repeats, so no length is too long.
        return INT32_MAX;
    case REPEAT_DAY:
        return event->repeat_interval;
    case REPEAT_WEEK:
        return 7 * event->repeat_interval;
    case REPEAT_MONTH:
        return MONTH_DAYS_MIN * event->repeat_interval;
    case REPEAT_YEAR:
        return YEAR_DAYS_MIN * event->repeat_interval;
    case REPEAT_UNIT_COUNT:
        break;
    }
    assert(false); // Not a RepeatUnit.
    return INT32_MAX;
}

// Occurrences never overlap (see event_repeat_gap_days_min), so the latest occurrence that
// starts on or before the day is the only one that can cover it.
bool event_occurrence_on_day(const Event *event, int32_t day_number, int32_t *start_day) {
    assert(event->repeat_unit >= 0);
    assert(event->repeat_unit < REPEAT_UNIT_COUNT);
    int32_t first_day = date_to_day_number(event->start_date);
    if (day_number < first_day) {
        return false;
    }

    int32_t candidate = first_day;
    switch ((RepeatUnit)event->repeat_unit) {
    case REPEAT_NONE:
        break;
    case REPEAT_DAY:
    case REPEAT_WEEK: {
        int32_t step = event->repeat_unit == REPEAT_WEEK ? 7 * event->repeat_interval
                                                         : event->repeat_interval;
        candidate = first_day + (day_number - first_day) / step * step;
        break;
    }
    case REPEAT_MONTH:
    case REPEAT_YEAR: {
        int32_t month_step = event->repeat_unit == REPEAT_YEAR ? 12 * event->repeat_interval
                                                               : event->repeat_interval;
        if (!event_occurrence_on_day_monthly(event, day_number, month_step, &candidate)) {
            return false;
        }
        break;
    }
    case REPEAT_UNIT_COUNT:
        assert(false); // Excluded by the assertions above.
        return false;
    }

    assert(candidate <= day_number);
    // The end date is inclusive: an occurrence starting on it still happens, in full. Earlier
    // occurrences cannot cover the day instead, because they end before the candidate starts.
    if (event->repeat_has_end) {
        if (candidate > date_to_day_number(event->repeat_end_date)) {
            return false;
        }
    }
    if (day_number - candidate > event_duration_days(event)) {
        return false;
    }
    *start_day = candidate;
    return true;
}

Event event_occurrence_at(const Event *event, int32_t start_day) {
    assert(start_day >= date_to_day_number(event->start_date));
    Event occurrence = *event;
    occurrence.start_date = date_from_day_number(start_day);
    occurrence.end_date = date_from_day_number(start_day + event_duration_days(event));
    assert(event_duration_days(&occurrence) == event_duration_days(event));
    return occurrence;
}

void event_format_repeat(const Event *event, char *buffer, size_t buffer_size) {
    assert(buffer_size > 0);
    switch ((RepeatUnit)event->repeat_unit) {
    case REPEAT_NONE:
        buffer[0] = '\0';
        return;
    case REPEAT_DAY:
        if (event->repeat_interval == 1) {
            snprintf(buffer, buffer_size, "every day");
        } else {
            snprintf(buffer, buffer_size, "every %" PRId32 " days", event->repeat_interval);
        }
        return;
    case REPEAT_WEEK:
        snprintf(buffer, buffer_size, "every week");
        return;
    case REPEAT_MONTH:
        snprintf(buffer, buffer_size, "every month");
        return;
    case REPEAT_YEAR:
        snprintf(buffer, buffer_size, "every year");
        return;
    case REPEAT_UNIT_COUNT:
        break;
    }
    assert(false); // Not a RepeatUnit.
    buffer[0] = '\0';
}

// Finds the latest occurrence on or before day_number for events that repeat every
// month_step months on the start's day of the month.
static bool event_occurrence_on_day_monthly(const Event *event, int32_t day_number,
                                            int32_t month_step, int32_t *start_day) {
    assert(month_step >= 1);
    Date first = event->start_date;
    Date day = date_from_day_number(day_number);
    int32_t months_since_first = (day.year - first.year) * 12 + (day.month - first.month);
    assert(months_since_first >= 0);

    int32_t occurrence_index = months_since_first / month_step;
    for (int32_t i = 0; i < MONTH_SEARCH_STEP_MAX && occurrence_index >= 0; i++) {
        int32_t month_index = first.year * 12 + (first.month - 1) + occurrence_index * month_step;
        Date occurrence = { month_index / 12, month_index % 12 + 1, first.day };
        if (occurrence.day <= days_in_month(occurrence.year, occurrence.month)) {
            int32_t occurrence_day = date_to_day_number(occurrence);
            if (occurrence_day <= day_number) {
                *start_day = occurrence_day;
                return true;
            }
        }
        occurrence_index--;
    }
    return false;
}

bool event_is_valid(const Event *event) {
    if (!text_is_terminated(event->title, sizeof(event->title))) {
        return false;
    }
    if (!text_is_terminated(event->description, sizeof(event->description))) {
        return false;
    }
    if (!text_is_terminated(event->location.address, sizeof(event->location.address))) {
        return false;
    }
    if (!text_is_terminated(event->location.city, sizeof(event->location.city))) {
        return false;
    }
    if (event->title[0] == '\0' || event->location.id < 0) {
        return false;
    }
    if (!date_is_valid(event->start_date) || !date_is_valid(event->end_date)) {
        return false;
    }
    int32_t start_day = date_to_day_number(event->start_date);
    if (date_to_day_number(event->end_date) < start_day) {
        return false;
    }
    if (!minute_is_valid(event->start_minute) || !minute_is_valid(event->end_minute)) {
        return false;
    }
    if (date_equal(event->start_date, event->end_date)) {
        if (event->start_minute != TIME_NONE && event->end_minute != TIME_NONE) {
            if (event->end_minute < event->start_minute) {
                return false;
            }
        }
    }
    if (event->kind < 0 || event->kind >= EVENT_KIND_COUNT) {
        return false;
    }
    if (event->repeat_unit < 0 || event->repeat_unit >= REPEAT_UNIT_COUNT) {
        return false;
    }
    if (event->repeat_interval < 1 || event->repeat_interval > REPEAT_INTERVAL_MAX) {
        return false;
    }
    if (event->repeat_has_end) {
        if (event->repeat_unit == REPEAT_NONE || !date_is_valid(event->repeat_end_date)) {
            return false;
        }
        if (date_to_day_number(event->repeat_end_date) < start_day) {
            return false;
        }
    }
    if (event->problem_done && event->kind != EVENT_KIND_PROBLEM) {
        return false;
    }
    if (event->kind == EVENT_KIND_PROBLEM) {
        if (event_duration_days(event) != 0 || event->start_minute != TIME_NONE) {
            return false;
        }
        if (event->end_minute != TIME_NONE || event->repeat_unit != REPEAT_NONE) {
            return false;
        }
        if (event->location.id != 0) {
            return false;
        }
    }
    return event_duration_days(event) < event_repeat_gap_days_min(event);
}

ProblemStatus problem_status(int32_t task_count, int32_t task_done_count) {
    assert(task_count >= 0);
    assert(task_done_count >= 0);
    assert(task_done_count <= task_count);
    if (task_count == 0) {
        return PROBLEM_STATUS_NO_TASKS;
    }
    if (task_done_count == 0) {
        return PROBLEM_STATUS_PLANNED;
    }
    if (task_done_count < task_count) {
        return PROBLEM_STATUS_PARTLY_DONE;
    }
    return PROBLEM_STATUS_DONE;
}

const char *problem_status_name(ProblemStatus status) {
    switch (status) {
    case PROBLEM_STATUS_NO_TASKS:
        return "No planned tasks";
    case PROBLEM_STATUS_PLANNED:
        return "Planned";
    case PROBLEM_STATUS_PARTLY_DONE:
        return "Partly completed";
    case PROBLEM_STATUS_DONE:
        return "Completed";
    case PROBLEM_STATUS_COUNT:
        break;
    }
    assert(false); // Not a ProblemStatus.
    return "";
}

static bool minute_is_valid(int32_t minute_of_day) {
    return minute_of_day == TIME_NONE || (minute_of_day >= 0 && minute_of_day < MINUTES_PER_DAY);
}

static bool text_is_terminated(const char *text, size_t size) {
    return memchr(text, '\0', size) != NULL;
}
