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

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

rtc_enc_t pcf85063_encode(const wt_datetime_t *dt, bool mode_12h, uint8_t out[PCF85063_TIME_LEN])
{
    if (!in(dt->year, 2000, 2099)) {
        return RTC_ENC_BAD_YEAR;
    }
    if (!in(dt->month, 1, 12) || !in(dt->day, 1, wt_days_in_month(dt->year, dt->month))) {
        return RTC_ENC_BAD_DATE;
    }
    if (!in(dt->hour, 0, 23) || !in(dt->minute, 0, 59) || !in(dt->second, 0, 59)) {
        return RTC_ENC_BAD_TIME;
    }
    uint8_t hour;
    if (mode_12h) {
        const int h12 = dt->hour % 12 == 0 ? 12 : dt->hour % 12;
        hour = (uint8_t)(to_bcd(h12) | (dt->hour >= 12 ? PCF85063_HOUR_PM : 0));
    } else {
        hour = to_bcd(dt->hour);
    }
    out[0] = to_bcd(dt->second); /* bit 7 (OS) = 0 */
    out[1] = to_bcd(dt->minute);
    out[2] = hour;
    out[3] = to_bcd(dt->day);
    out[4] = (uint8_t)wt_weekday_from_days(wt_days_from_civil(dt->year, dt->month, dt->day));
    out[5] = to_bcd(dt->month);
    out[6] = to_bcd(dt->year - 2000);
    return RTC_ENC_OK;
}

rtc_enc_t pcf85063_encode_utc(int64_t utc_s, bool mode_12h, uint8_t out[PCF85063_TIME_LEN])
{
    if (utc_s < 946684800LL || utc_s > 4102444799LL) {
        return RTC_ENC_BAD_YEAR;
    }
    wt_datetime_t dt;
    wt_from_seconds(utc_s, &dt);
    return pcf85063_encode(&dt, mode_12h, out);
}

uint8_t pcf85063_ctrl1_for_set(uint8_t ctrl1_read, bool stop)
{
    return (uint8_t)((ctrl1_read & (PCF85063_CTRL1_CIE | PCF85063_CTRL1_CAP_SEL)) | (stop ? PCF85063_CTRL1_STOP : 0));
}
