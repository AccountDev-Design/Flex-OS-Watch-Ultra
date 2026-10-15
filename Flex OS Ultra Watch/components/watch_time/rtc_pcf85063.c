/*
 * rtc_pcf85063 — ver include/rtc_pcf85063.h
 */
#include "rtc_pcf85063.h"

#include <stdbool.h>

/* BCD de dos cifras con la máscara del registro; -1 si una cifra pasa de 9. */
static int bcd(uint8_t v, uint8_t mask)
{
    v &= mask;
    const int hi = v >> 4, lo = v & 0x0F;
    if (hi > 9 || lo > 9) {
        return -1;
    }
    return hi * 10 + lo;
}

static bool in(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

rtc_read_t pcf85063_decode(const uint8_t regs[PCF85063_READ_LEN], wt_datetime_t *out)
{
    const uint8_t ctrl1 = regs[PCF85063_REG_CTRL1];
    const uint8_t *t = &regs[PCF85063_REG_SECONDS];

    if (t[0] & PCF85063_SEC_OS) {
        return RTC_READ_OSC_STOPPED;
    }
    if (ctrl1 & PCF85063_CTRL1_STOP) {
        return RTC_READ_STOPPED;
    }

    const int sec = bcd(t[0], 0x7F);
    const int min = bcd(t[1], 0x7F);
    int hour;
    if (ctrl1 & PCF85063_CTRL1_12H) {
        const int h12 = bcd(t[2], 0x1F);
        if (!in(h12, 1, 12)) {
            return RTC_READ_BAD_FIELD;
        }
        hour = (h12 % 12) + ((t[2] & PCF85063_HOUR_PM) ? 12 : 0);
    } else {
        hour = bcd(t[2], 0x3F);
    }
    const int day = bcd(t[3], 0x3F);
    const int month = bcd(t[5], 0x1F);
    const int yy = bcd(t[6], 0xFF);

    if (!in(sec, 0, 59) || !in(min, 0, 59) || !in(hour, 0, 23) || !in(month, 1, 12) || !in(yy, 0, 99)) {
        return RTC_READ_BAD_FIELD;
    }
    const int year = 2000 + yy;
    if (!in(day, 1, wt_days_in_month(year, month))) {
        return RTC_READ_BAD_FIELD;
    }

    out->year = year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = min;
    out->second = sec;
    out->weekday = wt_weekday_from_days(wt_days_from_civil(year, month, day));
    return year < WT_MIN_PLAUSIBLE_YEAR ? RTC_READ_IMPLAUSIBLE : RTC_READ_OK;
}

const char *pcf85063_read_text(rtc_read_t r)
{
    switch (r) {
    case RTC_READ_OK:          return "ok";
    case RTC_READ_OSC_STOPPED: return "oscilador parado (OS=1)";
    case RTC_READ_STOPPED:     return "reloj detenido (STOP=1)";
    case RTC_READ_BAD_FIELD:   return "registro fuera de rango";
    case RTC_READ_IMPLAUSIBLE: return "fecha anterior a 2025";
    default:                   return "?";
    }
}
