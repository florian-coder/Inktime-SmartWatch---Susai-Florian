/* Calendar arithmetic on Unix time (UTC, no time zones, no leap seconds). */
#ifndef DATETIME_H
#define DATETIME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;  /* 1970..2105 */
    uint8_t month;  /* 1..12 */
    uint8_t day;    /* 1..31 */
    uint8_t hour;   /* 0..23 */
    uint8_t minute; /* 0..59 */
    uint8_t second; /* 0..59 */
    uint8_t wday;   /* 0 = Monday .. 6 = Sunday */
} datetime_t;

bool dt_is_leap(uint16_t year);
uint8_t dt_days_in_month(uint16_t year, uint8_t month);
/* Days since 1970-01-01 for a civil date. */
int32_t dt_days_from_civil(uint16_t year, uint8_t month, uint8_t day);
uint32_t dt_to_epoch(const datetime_t *dt);
void dt_from_epoch(uint32_t epoch, datetime_t *dt);
/* Clamp fields into range (day clamped to month length). Returns true if valid before clamping. */
bool dt_normalize(datetime_t *dt);

const char *dt_wday_name(uint8_t wday);   /* "MON".."SUN" */
const char *dt_month_name(uint8_t month); /* "JAN".."DEC" */

#endif
