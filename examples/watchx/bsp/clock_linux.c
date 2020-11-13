#include "clock.h"
#include <time.h>

void clock_get_value(struct clock_value_s* clock_value)
{
    struct tm* t;
    time_t tt;
    time(&tt);
    t = localtime(&tt);

    clock_value->year = t->tm_year + 1900;
    clock_value->month = t->tm_mon + 1;
    clock_value->date = t->tm_mday;
    clock_value->week = t->tm_wday;
    clock_value->hour = t->tm_hour;
    clock_value->min = t->tm_min;
    clock_value->sec = t->tm_sec;
    clock_value->ms = 0;
}

void clock_set_value(struct clock_value_s* clock_value)
{
}
