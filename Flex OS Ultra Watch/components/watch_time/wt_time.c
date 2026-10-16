/*
 * wt_time — ver include/wt_time.h
 */
#include "wt_time.h"

#include <stdio.h>

static const char *const WD_FULL[7] = {
    "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado",
};
static const char *const WD_SHORT[7] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
static const char *const MO_FULL[12] = {
    "enero", "febrero", "marzo", "abril", "mayo", "junio",
    "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
};
static const char *const MO_SHORT[12] = {
    "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic",
};

int wt_is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int wt_days_in_month(int y, int m)
{
    static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) {
        return 0;
    }
    return DAYS[m - 1] + (m == 2 && wt_is_leap(y));
}

int64_t wt_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                 /* [0, 399] */
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

void wt_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

int wt_weekday_from_days(int64_t z)
{
    /* El 1-1-1970 fue jueves (4). */
    return (int)(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

int64_t wt_to_seconds(const wt_datetime_t *dt)
{
    return wt_days_from_civil(dt->year, dt->month, dt->day) * 86400 + dt->hour * 3600 + dt->minute * 60 +
           dt->second;
}

void wt_from_seconds(int64_t s, wt_datetime_t *dt)
{
    int64_t days = s / 86400;
    int64_t rem = s % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    wt_civil_from_days(days, &dt->year, &dt->month, &dt->day);
    dt->hour = (int)(rem / 3600);
    dt->minute = (int)(rem % 3600 / 60);
    dt->second = (int)(rem % 60);
    dt->weekday = wt_weekday_from_days(days);
}

static int valid_names(const wt_datetime_t *dt)
{
    return dt->weekday >= 0 && dt->weekday <= 6 && dt->month >= 1 && dt->month <= 12;
}

int wt_fmt_hhmm(char *buf, size_t n, const wt_datetime_t *dt)
{
    return snprintf(buf, n, "%d:%02d", dt->hour, dt->minute);
}

int wt_fmt_hhmm12(char *buf, size_t n, const wt_datetime_t *dt)
{
    const int h = dt->hour % 12 == 0 ? 12 : dt->hour % 12;
    return snprintf(buf, n, "%d:%02d", h, dt->minute);
}

const char *wt_ampm(const wt_datetime_t *dt)
{
    return dt->hour >= 12 ? "PM" : "AM";
}

void wt_utc_to_local(int64_t utc_s, int off_min, wt_datetime_t *out)
{
    wt_from_seconds(utc_s + (int64_t)off_min * 60, out);
}

int64_t wt_local_to_utc(const wt_datetime_t *local, int off_min)
{
    return wt_to_seconds(local) - (int64_t)off_min * 60;
}

int wt_fmt_utc_offset(char *buf, size_t n, int off_min)
{
    if (off_min == 0) {
        return snprintf(buf, n, "UTC");
    }
    const int a = off_min < 0 ? -off_min : off_min;
    return snprintf(buf, n, "UTC%c%02d:%02d", off_min < 0 ? '-' : '+', a / 60, a % 60);
}

int wt_fmt_date_long(char *buf, size_t n, const wt_datetime_t *dt)
{
    if (!valid_names(dt)) {
        return snprintf(buf, n, "--");
    }
    return snprintf(buf, n, "%s, %d de %s", WD_FULL[dt->weekday], dt->day, MO_FULL[dt->month - 1]);
}

int wt_fmt_date_short(char *buf, size_t n, const wt_datetime_t *dt)
{
    if (!valid_names(dt)) {
        return snprintf(buf, n, "--");
    }
    return snprintf(buf, n, "%s, %d %s", WD_SHORT[dt->weekday], dt->day, MO_SHORT[dt->month - 1]);
}
