#include <zeman/calendar.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    // Day number arithmetic follows Howard Hinnant's days_from_civil, which counts from
    // 0000-03-01 in 400-year eras so that the leap day falls at the end of each year.
    DAYS_PER_ERA = 146097,
    DAYS_FROM_ERA_START_TO_UNIX_EPOCH = 719468,
    // 1970-01-01 was a Thursday.
    UNIX_EPOCH_WEEKDAY = 3,
};

static const char *const month_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *const month_short_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *const weekday_names[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

static const char *const weekday_short_names[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

static bool is_leap_year(int32_t year);
static bool parse_digits(const char *text, int32_t digit_count, int32_t *number);

Date date_today(void) {
    return date_time_now().date;
}

Date_Time date_time_now(void) {
    time_t now = time(NULL);
    assert(now != (time_t)-1);
    const struct tm *local = localtime(&now);
    assert(local != NULL);

    Date_Time moment = {
        .date = { local->tm_year + 1900, local->tm_mon + 1, local->tm_mday },
        .minute_of_day = local->tm_hour * 60 + local->tm_min,
    };
    assert(date_is_valid(moment.date));
    assert(moment.minute_of_day >= 0);
    assert(moment.minute_of_day < MINUTES_PER_DAY);
    return moment;
}

// "1 hour ago", "2 hours ago".
static void format_age_count(char *buffer, size_t buffer_size, int64_t count, const char *unit) {
    assert(count >= 1);
    snprintf(buffer, buffer_size, "%" PRId64 " %s%s ago", count, unit, count == 1 ? "" : "s");
}

// Whole months from earlier to later: the day of the month counts, so 31 Jan to 28 Feb is not
// yet a month.
static int32_t months_between(Date earlier, Date later) {
    int32_t months = (later.year - earlier.year) * 12 + (later.month - earlier.month);
    return later.day < earlier.day ? months - 1 : months;
}

// Minutes and hours while the time of day matters, up to a quarter of a day; after that days,
// weeks, months and years by the calendar, because "yesterday" is what people look for, and
// "20 hours ago" would not tell them which day.
void date_time_format_age(Date_Time created, Date_Time now, char *buffer, size_t buffer_size) {
    assert(date_is_valid(created.date));
    assert(date_is_valid(now.date));
    assert(created.minute_of_day >= 0 && created.minute_of_day < MINUTES_PER_DAY);
    assert(now.minute_of_day >= 0 && now.minute_of_day < MINUTES_PER_DAY);
    assert(buffer_size >= DATE_TIME_AGE_SIZE);

    const int64_t days = (int64_t)date_to_day_number(now.date) -
                         date_to_day_number(created.date);
    const int64_t minutes = days * MINUTES_PER_DAY + now.minute_of_day - created.minute_of_day;
    if (minutes < 1) {
        snprintf(buffer, buffer_size, "just now");
    } else if (minutes < 60) {
        format_age_count(buffer, buffer_size, minutes, "minute");
    } else if (minutes < 6 * 60) {
        format_age_count(buffer, buffer_size, minutes / 60, "hour");
    } else if (days == 0) {
        snprintf(buffer, buffer_size, "today");
    } else if (days == 1) {
        snprintf(buffer, buffer_size, "yesterday");
    } else if (days < 7) {
        format_age_count(buffer, buffer_size, days, "day");
    } else if (days < 14) {
        snprintf(buffer, buffer_size, "last week");
    } else if (days < 30) {
        format_age_count(buffer, buffer_size, days / 7, "week");
    } else {
        // At least 30 days is a month for the text, even when the day of the month says 29 days
        // and some hours: 1 Mar to 31 Mar.
        int32_t months = months_between(created.date, now.date);
        months = months < 1 ? 1 : months;
        if (months == 1) {
            snprintf(buffer, buffer_size, "last month");
        } else if (months < 12) {
            format_age_count(buffer, buffer_size, months, "month");
        } else if (months < 24) {
            snprintf(buffer, buffer_size, "last year");
        } else {
            format_age_count(buffer, buffer_size, months / 12, "year");
        }
    }
}

bool date_is_valid(Date date) {
    if (date.year < DATE_YEAR_MIN || date.year > DATE_YEAR_MAX) {
        return false;
    }
    if (date.month < 1 || date.month > 12) {
        return false;
    }
    if (date.day < 1) {
        return false;
    }
    return date.day <= days_in_month(date.year, date.month);
}

bool date_equal(Date a, Date b) {
    assert(date_is_valid(a));
    assert(date_is_valid(b));
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

int32_t days_in_month(int32_t year, int32_t month) {
    assert(month >= 1);
    assert(month <= 12);
    static const int32_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2) {
        if (is_leap_year(year)) {
            return 29;
        }
    }
    return days[month - 1];
}

int32_t date_to_day_number(Date date) {
    assert(date_is_valid(date));
    // Years start in March, so January and February belong to the previous year.
    int32_t year = date.year - (date.month <= 2 ? 1 : 0);
    assert(year >= 0); // DATE_YEAR_MIN keeps the era non-negative, so division truncates safely.

    int32_t era = year / 400;
    int32_t year_of_era = year - era * 400;
    int32_t month_from_march = date.month > 2 ? date.month - 3 : date.month + 9;
    int32_t day_of_year = (153 * month_from_march + 2) / 5 + date.day - 1;
    int32_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    assert(day_of_era >= 0);
    assert(day_of_era < DAYS_PER_ERA);
    return era * DAYS_PER_ERA + day_of_era - DAYS_FROM_ERA_START_TO_UNIX_EPOCH;
}

Date date_from_day_number(int32_t day_number) {
    int32_t day_from_era_start = day_number + DAYS_FROM_ERA_START_TO_UNIX_EPOCH;
    assert(day_from_era_start >= 0);

    int32_t era = day_from_era_start / DAYS_PER_ERA;
    int32_t day_of_era = day_from_era_start - era * DAYS_PER_ERA;
    int32_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int32_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int32_t month_from_march = (5 * day_of_year + 2) / 153;
    int32_t month = month_from_march < 10 ? month_from_march + 3 : month_from_march - 9;

    Date date = {
        .year = year_of_era + era * 400 + (month <= 2 ? 1 : 0),
        .month = month,
        .day = day_of_year - (153 * month_from_march + 2) / 5 + 1,
    };
    assert(date_is_valid(date));
    assert(date_to_day_number(date) == day_number);
    return date;
}

int32_t date_weekday(Date date) {
    int32_t day_number = date_to_day_number(date);
    // Normalize the remainder because day numbers before 1970 are negative.
    int32_t weekday = ((day_number % 7) + 7 + UNIX_EPOCH_WEEKDAY) % 7;
    assert(weekday >= 0);
    assert(weekday < 7);
    return weekday;
}

Date date_add_days(Date date, int32_t days) {
    assert(date_is_valid(date));
    Date result = date_from_day_number(date_to_day_number(date) + days);
    assert(date_to_day_number(result) - date_to_day_number(date) == days);
    return result;
}

Date date_add_months(Date date, int32_t months) {
    assert(date_is_valid(date));
    int32_t month_index = date.year * 12 + (date.month - 1) + months;
    assert(month_index >= DATE_YEAR_MIN * 12);
    assert(month_index < (DATE_YEAR_MAX + 1) * 12);

    Date result = { month_index / 12, month_index % 12 + 1, date.day };
    int32_t day_max = days_in_month(result.year, result.month);
    if (result.day > day_max) {
        result.day = day_max;
    }
    assert(date_is_valid(result));
    return result;
}

bool date_parse_iso(const char *text, Date *date) {
    assert(text != NULL);
    assert(date != NULL);
    // Require the fixed-width form so that stored dates also sort correctly as text.
    if (strlen(text) != DATE_ISO_SIZE - 1) {
        return false;
    }
    if (text[4] != '-' || text[7] != '-') {
        return false;
    }

    Date parsed = { 0, 0, 0 };
    if (!parse_digits(text, 4, &parsed.year)) {
        return false;
    }
    if (!parse_digits(text + 5, 2, &parsed.month)) {
        return false;
    }
    if (!parse_digits(text + 8, 2, &parsed.day)) {
        return false;
    }
    if (!date_is_valid(parsed)) {
        return false;
    }
    *date = parsed;
    return true;
}

void date_format_iso(Date date, char *buffer, size_t buffer_size) {
    assert(date_is_valid(date));
    assert(buffer_size >= DATE_ISO_SIZE);
    int written = snprintf(buffer, buffer_size, "%04" PRId32 "-%02" PRId32 "-%02" PRId32,
                           date.year, date.month, date.day);
    assert(written == DATE_ISO_SIZE - 1);
}

bool time_parse(const char *text, int32_t *minute_of_day) {
    assert(text != NULL);
    assert(minute_of_day != NULL);
    if (strlen(text) != TIME_TEXT_SIZE - 1) {
        return false;
    }
    if (text[2] != ':') {
        return false;
    }

    int32_t hour = 0;
    int32_t minute = 0;
    if (!parse_digits(text, 2, &hour)) {
        return false;
    }
    if (!parse_digits(text + 3, 2, &minute)) {
        return false;
    }
    if (hour > 23 || minute > 59) {
        return false;
    }
    *minute_of_day = hour * 60 + minute;
    assert(*minute_of_day < MINUTES_PER_DAY);
    return true;
}

void time_format(int32_t minute_of_day, char *buffer, size_t buffer_size) {
    assert(minute_of_day >= 0);
    assert(minute_of_day < MINUTES_PER_DAY);
    assert(buffer_size >= TIME_TEXT_SIZE);
    int written = snprintf(buffer, buffer_size, "%02" PRId32 ":%02" PRId32,
                           minute_of_day / 60, minute_of_day % 60);
    assert(written == TIME_TEXT_SIZE - 1);
}

const char *month_name(int32_t month) {
    assert(month >= 1);
    assert(month <= 12);
    return month_names[month - 1];
}

const char *month_short_name(int32_t month) {
    assert(month >= 1);
    assert(month <= 12);
    return month_short_names[month - 1];
}

const char *weekday_name(int32_t weekday) {
    assert(weekday >= 0);
    assert(weekday < 7);
    return weekday_names[weekday];
}

const char *weekday_short_name(int32_t weekday) {
    assert(weekday >= 0);
    assert(weekday < 7);
    return weekday_short_names[weekday];
}

static bool is_leap_year(int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// Parses exactly digit_count ASCII digits. Avoids strtol, which accepts signs and spaces.
static bool parse_digits(const char *text, int32_t digit_count, int32_t *number) {
    assert(digit_count > 0);
    assert(digit_count <= 4); // Keeps the result far from int32_t overflow.
    int32_t result = 0;
    for (int32_t i = 0; i < digit_count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        result = result * 10 + (text[i] - '0');
    }
    *number = result;
    return true;
}
