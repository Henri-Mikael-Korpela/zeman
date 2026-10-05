#ifndef CALENDAR_H
#define CALENDAR_H

#include <stdbool.h>

typedef struct Date {
    int year;
    int month;  // 1-12
    int day;    // 1-31
} Date;

Date DateToday(void);
bool DateEqual(Date a, Date b);
int DaysInMonth(int year, int month);
int DayOfWeek(Date d);              // 0 = Monday ... 6 = Sunday
Date DateAddDays(Date d, int n);
Date DateAddMonths(Date d, int n);  // Day is clamped to the target month's length
const char *MonthName(int month);
const char *WeekdayName(int weekday);       // Full name, 0 = Monday
const char *WeekdayShortName(int weekday);  // "Mon", "Tue", ...

#endif
