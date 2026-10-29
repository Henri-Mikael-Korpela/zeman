#include <zeman/event.h>

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

static bool event_occurrence_on_day_monthly(Event const *event, int32_t day_number,
                                            int32_t month_step, int32_t *start_day);
static int32_t event_occurrence_due_latest(Event const *event, int32_t day_number);
static bool event_occurrence_due_next(Event const *event, int32_t start_day, int32_t *next_day);
static int32_t event_repeat_day_step(Event const *event);
static int32_t event_repeat_month_step(Event const *event);
static bool minute_is_valid(int32_t minute_of_day);
static bool text_is_terminated(char const *text, size_t size);

int32_t event_duration_days(Event const *event) {
    int32_t duration = date_to_day_number(event->end_date) -
                       date_to_day_number(event->start_date);
    assert(duration >= 0);
    return duration;
}

int32_t event_repeat_gap_days_min(Event const *event) {
    assert(event->repeat_interval >= 1);
    assert(event->repeat_interval <= REPEAT_INTERVAL_MAX);

    switch ((Repeat_Unit)event->repeat_unit) {
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
    assert(false); // Not a Repeat_Unit.
    return INT32_MAX;
}

/**
 * Occurrences never overlap (see event_repeat_gap_days_min), so the latest occurrence that
 * starts on or before the day is the only one that can cover it.
 */
bool event_occurrence_on_day(Event const *event, int32_t day_number, int32_t *start_day) {
    assert(event->repeat_unit >= 0);
    assert(event->repeat_unit < REPEAT_UNIT_COUNT);

    int32_t first_day = date_to_day_number(event->start_date);
    if (day_number < first_day) {
        return false;
    }

    int32_t candidate = first_day;
    switch ((Repeat_Unit)event->repeat_unit) {
    case REPEAT_NONE:
        break;
    case REPEAT_DAY:
    case REPEAT_WEEK: {
        int32_t step = event->repeat_interval;
        if (event->repeat_unit == REPEAT_WEEK) {
            step = 7 * event->repeat_interval;
        }
        candidate = first_day + (day_number - first_day) / step * step;
        break;
    }
    case REPEAT_MONTH:
    case REPEAT_YEAR: {
        int32_t month_step = event->repeat_interval;
        if (event->repeat_unit == REPEAT_YEAR) {
            month_step = 12 * event->repeat_interval;
        }
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

int32_t event_occurrence_due(Event const *event, int32_t day_number) {
    assert(event->repeat_unit >= 0);
    assert(event->repeat_unit < REPEAT_UNIT_COUNT);

    int32_t const first_day = date_to_day_number(event->start_date);
    if (day_number < first_day || event->repeat_unit == REPEAT_NONE) {
        return first_day;
    }
    int32_t start_day = first_day;
    if (event_occurrence_on_day(event, day_number, &start_day)) {
        return start_day;
    }
    // No occurrence covers the day, so the latest that starts by it is past, and the one after
    // it is next, unless the repeat ends first; then the latest is the last.
    int32_t last_day = day_number;
    if (event->repeat_has_end) {
        int32_t const end_day = date_to_day_number(event->repeat_end_date);
        if (end_day < last_day) {
            last_day = end_day;
        }
    }
    int32_t const latest_day = event_occurrence_due_latest(event, last_day);
    int32_t next_day = 0;
    if (!event_occurrence_due_next(event, latest_day, &next_day)) {
        return latest_day;
    }
    if (event->repeat_has_end && next_day > date_to_day_number(event->repeat_end_date)) {
        return latest_day;
    }
    assert(next_day > day_number);
    return next_day;
}

Event event_occurrence_at(Event const *event, int32_t start_day) {
    assert(start_day >= date_to_day_number(event->start_date));

    Event occurrence = *event;
    occurrence.start_date = date_from_day_number(start_day);
    occurrence.end_date = date_from_day_number(start_day + event_duration_days(event));
    assert(event_duration_days(&occurrence) == event_duration_days(event));
    return occurrence;
}

void event_format_repeat(Event const *event, char *buffer, size_t buffer_size) {
    assert(buffer_size > 0);

    switch ((Repeat_Unit)event->repeat_unit) {
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
    assert(false); // Not a Repeat_Unit.
    buffer[0] = '\0';
}

/**
 * Finds the latest occurrence on or before day_number for events that repeat every
 * month_step months on the start's day of the month.
 */
static bool event_occurrence_on_day_monthly(Event const *event, int32_t day_number,
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

/**
 * The latest start on or before day_number, which is not before the first, whether or not that
 * occurrence still covers the day.
 */
static int32_t event_occurrence_due_latest(Event const *event, int32_t day_number) {
    assert(event->repeat_unit != REPEAT_NONE);

    int32_t const first_day = date_to_day_number(event->start_date);
    assert(day_number >= first_day);

    int32_t const day_step = event_repeat_day_step(event);
    if (day_step > 0) {
        return first_day + (day_number - first_day) / day_step * day_step;
    }
    int32_t start_day = first_day;
    // The search bound covers the intervals the form offers, so this fails only for data made
    // elsewhere; the first occurrence is a start all the same.
    if (!event_occurrence_on_day_monthly(event, day_number, event_repeat_month_step(event),
                                         &start_day)) {
        return first_day;
    }
    return start_day;
}

/**
 * The first start after start_day, an occurrence's start, ignoring the repeat end. Returns
 * false past the last valid date, or when monthly occurrences are not found within the search
 * bound.
 */
static bool event_occurrence_due_next(Event const *event, int32_t start_day, int32_t *next_day) {
    assert(event->repeat_unit != REPEAT_NONE);
    assert(start_day >= date_to_day_number(event->start_date));

    int32_t const last_valid_day = date_to_day_number((Date){ DATE_YEAR_MAX, 12, 31 });
    int32_t const day_step = event_repeat_day_step(event);
    if (day_step > 0) {
        if (start_day > last_valid_day - day_step) {
            return false;
        }
        *next_day = start_day + day_step;
        return true;
    }
    int32_t const month_step = event_repeat_month_step(event);
    Date const first = event->start_date;
    Date const start = date_from_day_number(start_day);
    int32_t const first_month_index = first.year * 12 + (first.month - 1);
    int32_t const start_month_index = start.year * 12 + (start.month - 1);
    // Later occurrences skip months without the start's day, as in event_occurrence_on_day_monthly.
    int32_t occurrence_index = (start_month_index - first_month_index) / month_step;
    for (int32_t i = 0; i < MONTH_SEARCH_STEP_MAX; i++) {
        occurrence_index++;
        int32_t const month_index = first_month_index + occurrence_index * month_step;
        Date const occurrence = { month_index / 12, month_index % 12 + 1, first.day };
        if (occurrence.year > DATE_YEAR_MAX) {
            return false;
        }
        if (occurrence.day <= days_in_month(occurrence.year, occurrence.month)) {
            *next_day = date_to_day_number(occurrence);
            assert(*next_day > start_day);
            return true;
        }
    }
    return false;
}

/**
 * Days between starts for an event that repeats by days or weeks, or 0 for one by months.
 */
static int32_t event_repeat_day_step(Event const *event) {
    switch ((Repeat_Unit)event->repeat_unit) {
    case REPEAT_DAY:
        return event->repeat_interval;
    case REPEAT_WEEK:
        return 7 * event->repeat_interval;
    case REPEAT_MONTH:
    case REPEAT_YEAR:
        return 0;
    case REPEAT_NONE:
    case REPEAT_UNIT_COUNT:
        break;
    }
    assert(false); // Only repeating units are asked about.
    return 0;
}

/**
 * Months between starts for an event that repeats by months or years.
 */
static int32_t event_repeat_month_step(Event const *event) {
    switch ((Repeat_Unit)event->repeat_unit) {
    case REPEAT_MONTH:
        return event->repeat_interval;
    case REPEAT_YEAR:
        return 12 * event->repeat_interval;
    case REPEAT_NONE:
    case REPEAT_DAY:
    case REPEAT_WEEK:
    case REPEAT_UNIT_COUNT:
        break;
    }
    assert(false); // Only units by months are asked about.
    return 1;
}

bool event_is_valid(Event const *event) {
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
    if (event->tasks_done && !event_kind_is_target(event->kind)) {
        return false;
    }
    // A deadline happens once, so an opportunity cannot repeat.
    if (event->kind == EVENT_KIND_OPPORTUNITY && event->repeat_unit != REPEAT_NONE) {
        return false;
    }
    if (event->has_deadline) {
        if (event->kind != EVENT_KIND_TASK || event->repeat_unit != REPEAT_NONE) {
            return false;
        }
        if (!date_is_valid(event->deadline_date) || !minute_is_valid(event->deadline_minute)) {
            return false;
        }
        if (date_to_day_number(event->deadline_date) < start_day) {
            return false;
        }
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

bool event_kind_is_target(int32_t kind) {
    assert(kind >= 0);
    assert(kind < EVENT_KIND_COUNT);

    return kind == EVENT_KIND_PROBLEM || kind == EVENT_KIND_OPPORTUNITY;
}

/**
 * Compares the deadline and now as minutes since 1970, so that the date and the time of day are
 * one comparison; a deadline without a time is the last minute of its day.
 */
bool opportunity_is_missed(Date end_date, int32_t end_minute, bool tasks_done, Date_Time now) {
    assert(date_is_valid(end_date));
    assert(date_is_valid(now.date));
    assert(end_minute == TIME_NONE || minute_is_valid(end_minute));

    if (tasks_done) {
        return false;
    }
    int32_t deadline_minute = end_minute;
    if (end_minute == TIME_NONE) {
        deadline_minute = MINUTES_PER_DAY - 1;
    }
    // int64_t, because minutes since 1970 pass INT32_MAX in the year 6053.
    int64_t const deadline = (int64_t)date_to_day_number(end_date) * MINUTES_PER_DAY +
                             deadline_minute;
    int64_t const current = (int64_t)date_to_day_number(now.date) * MINUTES_PER_DAY +
                            now.minute_of_day;
    return current > deadline;
}

Target_Status target_status(int32_t task_count, int32_t task_done_count) {
    assert(task_count >= 0);
    assert(task_done_count >= 0);
    assert(task_done_count <= task_count);

    if (task_count == 0) {
        return TARGET_STATUS_NO_TASKS;
    }
    if (task_done_count == 0) {
        return TARGET_STATUS_PLANNED;
    }
    if (task_done_count < task_count) {
        return TARGET_STATUS_PARTLY_DONE;
    }
    return TARGET_STATUS_DONE;
}

Str target_status_name(Target_Status status) {
    switch (status) {
    case TARGET_STATUS_NO_TASKS:
        return STR("No planned tasks");
    case TARGET_STATUS_PLANNED:
        return STR("Planned");
    case TARGET_STATUS_PARTLY_DONE:
        return STR("Partly completed");
    case TARGET_STATUS_DONE:
        return STR("Completed");
    case TARGET_STATUS_COUNT:
        break;
    }
    assert(false); // Not a Target_Status.
    return STR("");
}

static bool minute_is_valid(int32_t minute_of_day) {
    return minute_of_day == TIME_NONE || (minute_of_day >= 0 && minute_of_day < MINUTES_PER_DAY);
}

static bool text_is_terminated(char const *text, size_t size) {
    return memchr(text, '\0', size) != NULL;
}
