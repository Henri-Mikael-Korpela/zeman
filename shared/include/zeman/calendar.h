#ifndef CALENDAR_H
#define CALENDAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DATE_YEAR_MIN = 1,
    DATE_YEAR_MAX = 9999,
    // "YYYY-MM-DD" plus the terminator.
    DATE_ISO_SIZE = 11,
    // "HH:MM" plus the terminator.
    TIME_TEXT_SIZE = 6,
    MINUTES_PER_DAY = 24 * 60,
    // Minute of day for a date that has no time of day.
    TIME_NONE = -1,
    // The longest age text, "11 months ago" or "59 minutes ago", plus the terminator, with room.
    DATE_TIME_AGE_SIZE = 24,
};

// A date in the proleptic Gregorian calendar, in local time.
typedef struct Date {
    int32_t year;   // DATE_YEAR_MIN ... DATE_YEAR_MAX
    int32_t month;  // 1 ... 12
    int32_t day;    // 1 ... days_in_month(year, month)
} Date;

// A moment in local time, to the minute.
typedef struct Date_Time {
    Date date;
    int32_t minute_of_day;  // 0 ... MINUTES_PER_DAY - 1
} Date_Time;

Date date_today(void);
// The date and the time of day from one reading of the clock, so that they never straddle
// midnight the way two separate calls could.
Date_Time date_time_now(void);
// How long ago created was, as of now, in words: "just now", "5 minutes ago", "2 hours ago",
// "today", "yesterday", "3 days ago", "last week", "2 weeks ago", "last month", "5 months ago",
// "last year", "3 years ago". A moment in the future, from clocks that disagree, is "just now".
void date_time_format_age(Date_Time created, Date_Time now, char *buffer, size_t buffer_size);
bool date_is_valid(Date date);
bool date_equal(Date a, Date b);
int32_t days_in_month(int32_t year, int32_t month);
// Days since 1970-01-01, so that date differences are a subtraction.
int32_t date_to_day_number(Date date);
Date date_from_day_number(int32_t day_number);
// 0 = Monday ... 6 = Sunday.
int32_t date_weekday(Date date);
Date date_add_days(Date date, int32_t days);
// The day is clamped to the length of the target month.
Date date_add_months(Date date, int32_t months);
// Parses exactly "YYYY-MM-DD". Returns false for malformed or impossible dates.
bool date_parse_iso(const char *text, Date *date);
void date_format_iso(Date date, char *buffer, size_t buffer_size);
// Parses exactly "HH:MM". Returns false for malformed or out-of-range times.
bool time_parse(const char *text, int32_t *minute_of_day);
void time_format(int32_t minute_of_day, char *buffer, size_t buffer_size);
const char *month_name(int32_t month);
const char *month_short_name(int32_t month);
const char *weekday_name(int32_t weekday);
const char *weekday_short_name(int32_t weekday);

#endif
