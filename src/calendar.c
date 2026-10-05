#include "calendar.h"

#include <time.h>

static const char *monthNames[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *weekdayNames[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

static const char *weekdayShortNames[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

Date DateToday(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    return (Date){ t->tm_year + 1900, t->tm_mon + 1, t->tm_mday };
}

bool DateEqual(Date a, Date b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && IsLeapYear(year)) return 29;
    return days[month - 1];
}

int DayOfWeek(Date d) {
    // Sakamoto's algorithm: yields 0 = Sunday, shifted so 0 = Monday.
    static const int offsets[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = d.year - (d.month < 3);
    int w = (y + y/4 - y/100 + y/400 + offsets[d.month - 1] + d.day) % 7;
    return (w + 6) % 7;
}

Date DateAddDays(Date d, int n) {
    d.day += n;
    while (d.day < 1) {
        d.month--;
        if (d.month < 1) { d.month = 12; d.year--; }
        d.day += DaysInMonth(d.year, d.month);
    }
    while (d.day > DaysInMonth(d.year, d.month)) {
        d.day -= DaysInMonth(d.year, d.month);
        d.month++;
        if (d.month > 12) { d.month = 1; d.year++; }
    }
    return d;
}

Date DateAddMonths(Date d, int n) {
    int index = d.year*12 + (d.month - 1) + n;
    Date r = { index / 12, index % 12 + 1, d.day };
    int dim = DaysInMonth(r.year, r.month);
    if (r.day > dim) r.day = dim;
    return r;
}

const char *MonthName(int month) { return monthNames[month - 1]; }
const char *WeekdayName(int weekday) { return weekdayNames[weekday]; }
const char *WeekdayShortName(int weekday) { return weekdayShortNames[weekday]; }
