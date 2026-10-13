#include "datetime.h"

#define SECS_PER_DAY 86400u

bool dt_is_leap(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t dt_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && dt_is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

/* Howard Hinnant's days_from_civil, specialised for years >= 1970. */
int32_t dt_days_from_civil(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t y = (int32_t)year - (month <= 2);
    int32_t era = y / 400;
    int32_t yoe = y - era * 400;
    int32_t mp = (month + 9) % 12; /* March = 0 */
    int32_t doy = (153 * mp + 2) / 5 + day - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint32_t dt_to_epoch(const datetime_t *dt)
{
    int32_t days = dt_days_from_civil(dt->year, dt->month, dt->day);
    return (uint32_t)days * SECS_PER_DAY + dt->hour * 3600u + dt->minute * 60u + dt->second;
}

void dt_from_epoch(uint32_t epoch, datetime_t *dt)
{
    uint32_t days = epoch / SECS_PER_DAY;
    uint32_t rem = epoch % SECS_PER_DAY;

    dt->hour = (uint8_t)(rem / 3600);
    dt->minute = (uint8_t)(rem % 3600 / 60);
    dt->second = (uint8_t)(rem % 60);
    /* 1970-01-01 was a Thursday (wday 3 with Monday = 0). */
    dt->wday = (uint8_t)((days + 3) % 7);

    /* Hinnant's civil_from_days. */
    int32_t z = (int32_t)days + 719468;
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t d = doy - (153 * mp + 2) / 5 + 1;
    int32_t m = mp < 10 ? mp + 3 : mp - 9;
    int32_t y = yoe + era * 400 + (m <= 2);

    dt->year = (uint16_t)y;
    dt->month = (uint8_t)m;
    dt->day = (uint8_t)d;
}

bool dt_normalize(datetime_t *dt)
{
    bool ok = true;
    if (dt->year < 2000 || dt->year > 2099) {
        dt->year = dt->year < 2000 ? 2000 : 2099;
        ok = false;
    }
    if (dt->month < 1 || dt->month > 12) {
        dt->month = dt->month < 1 ? 1 : 12;
        ok = false;
    }
    uint8_t dim = dt_days_in_month(dt->year, dt->month);
    if (dt->day < 1 || dt->day > dim) {
        dt->day = dt->day < 1 ? 1 : dim;
        ok = false;
    }
    if (dt->hour > 23) {
        dt->hour = 23;
        ok = false;
    }
    if (dt->minute > 59) {
        dt->minute = 59;
        ok = false;
    }
    if (dt->second > 59) {
        dt->second = 59;
        ok = false;
    }
    int32_t days = dt_days_from_civil(dt->year, dt->month, dt->day);
    dt->wday = (uint8_t)((days + 3) % 7);
    return ok;
}

const char *dt_wday_name(uint8_t wday)
{
    static const char *const names[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
    return wday < 7 ? names[wday] : "???";
}

const char *dt_month_name(uint8_t month)
{
    static const char *const names[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    return (month >= 1 && month <= 12) ? names[month - 1] : "???";
}
