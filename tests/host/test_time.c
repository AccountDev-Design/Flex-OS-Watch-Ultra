/*
 * Pruebas de PC de la hora (B3-2): calendario, textos en español y
 * decodificación de los registros del PCF85063.
 */
#include <string.h>

#include "rtc_pcf85063.h"
#include "test_util.h"
#include "wt_time.h"

static void test_calendar(void)
{
    CHECK_EQ(wt_days_from_civil(1970, 1, 1), 0);
    CHECK_EQ(wt_days_from_civil(2000, 3, 1), 11017);
    CHECK_EQ(wt_days_from_civil(2026, 10, 15), 20741);
    CHECK_EQ(wt_days_from_civil(1, 1, 1), -719162);
    CHECK_EQ(wt_days_from_civil(9999, 12, 31), 2932896);

    /* Ida y vuelta día a día en ±8000 años, con fechas siempre válidas. */
    int bad = 0;
    for (int64_t z = -2932896; z <= 2932896; z += 7) {
        int y, m, d;
        wt_civil_from_days(z, &y, &m, &d);
        if (wt_days_from_civil(y, m, d) != z || d < 1 || d > wt_days_in_month(y, m)) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);

    /* Días de la semana conocidos (0 = domingo). */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(1970, 1, 1)), 4);  /* jueves */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(1969, 12, 31)), 3); /* miércoles */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(1969, 12, 28)), 0); /* domingo */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(2000, 1, 1)), 6);   /* sábado */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(2026, 10, 15)), 4); /* jueves */
    CHECK_EQ(wt_weekday_from_days(wt_days_from_civil(2026, 7, 4)), 6);   /* sábado */

    CHECK_EQ(wt_days_in_month(2024, 2), 29);
    CHECK_EQ(wt_days_in_month(2100, 2), 28);
    CHECK_EQ(wt_days_in_month(2000, 2), 29);
    CHECK_EQ(wt_days_in_month(2026, 4), 30);
    CHECK_EQ(wt_days_in_month(2026, 12), 31);
    CHECK_EQ(wt_days_in_month(2026, 0), 0);
    CHECK_EQ(wt_days_in_month(2026, 13), 0);
}

static void test_seconds(void)
{
    wt_datetime_t a = {2026, 10, 15, 23, 59, 59, 0}, b;
    const int64_t s = wt_to_seconds(&a);
    wt_from_seconds(s + 1, &b); /* cambio de día */
    CHECK_EQ(b.year, 2026);
    CHECK_EQ(b.month, 10);
    CHECK_EQ(b.day, 16);
    CHECK_EQ(b.hour, 0);
    CHECK_EQ(b.minute, 0);
    CHECK_EQ(b.second, 0);
    CHECK_EQ(b.weekday, 5);

    wt_datetime_t c = {2028, 12, 31, 23, 59, 59, 0};
    wt_from_seconds(wt_to_seconds(&c) + 1, &b); /* cambio de año */
    CHECK_EQ(b.year, 2029);
    CHECK_EQ(b.month, 1);
    CHECK_EQ(b.day, 1);

    wt_from_seconds(-1, &b); /* antes de 1970 */
    CHECK_EQ(b.year, 1969);
    CHECK_EQ(b.month, 12);
    CHECK_EQ(b.day, 31);
    CHECK_EQ(b.hour, 23);
    CHECK_EQ(b.second, 59);
    CHECK_EQ(b.weekday, 3);

    int bad = 0;
    for (int64_t t = -100000; t < 4000000000LL; t += 86399 * 3 + 17) {
        wt_datetime_t x;
        wt_from_seconds(t, &x);
        if (wt_to_seconds(&x) != t) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_format(void)
{
    char buf[48];
    wt_datetime_t dt = {2026, 10, 15, 9, 5, 7, 4};
    wt_fmt_hhmm(buf, sizeof(buf), &dt);
    CHECK(strcmp(buf, "9:05") == 0);
    dt.hour = 0;
    dt.minute = 0;
    wt_fmt_hhmm(buf, sizeof(buf), &dt);
    CHECK(strcmp(buf, "0:00") == 0);
    dt.hour = 23;
    dt.minute = 59;
    wt_fmt_hhmm(buf, sizeof(buf), &dt);
    CHECK(strcmp(buf, "23:59") == 0);

    wt_fmt_date_long(buf, sizeof(buf), &dt);
    CHECK(strcmp(buf, "Jueves, 15 de octubre") == 0);
    wt_fmt_date_short(buf, sizeof(buf), &dt);
    CHECK(strcmp(buf, "jue, 15 oct") == 0);

    wt_datetime_t w = {2026, 9, 30, 0, 0, 0, 3};
    wt_fmt_date_long(buf, sizeof(buf), &w);
    CHECK(strcmp(buf, "Miércoles, 30 de septiembre") == 0);
    w.weekday = 6;
    wt_fmt_date_short(buf, sizeof(buf), &w);
    CHECK(strcmp(buf, "sáb, 30 sep") == 0);

    /* Valores imposibles: nunca se lee fuera de las tablas. */
    w.weekday = 7;
    wt_fmt_date_long(buf, sizeof(buf), &w);
    CHECK(strcmp(buf, "--") == 0);
    w.weekday = 0;
    w.month = 0;
    wt_fmt_date_short(buf, sizeof(buf), &w);
    CHECK(strcmp(buf, "--") == 0);

    /* Búfer corto: se recorta sin desbordar. */
    char tiny[6];
    dt.weekday = 4;
    dt.month = 10;
    wt_fmt_date_long(tiny, sizeof(tiny), &dt);
    CHECK(strcmp(tiny, "Jueve") == 0);
}

/* Registros 00h..0Ah: Control_1, Control_2, Offset, RAM, s, min, h, día, semana, mes, año */
static void set_time(uint8_t r[PCF85063_READ_LEN], uint8_t s, uint8_t mi, uint8_t h, uint8_t d, uint8_t mo, uint8_t y)
{
    memset(r, 0, PCF85063_READ_LEN);
    r[4] = s;
    r[5] = mi;
    r[6] = h;
    r[7] = d;
    r[8] = 3; /* día de la semana del registro: se ignora */
    r[9] = mo;
    r[10] = y;
}

static void test_pcf85063(void)
{
    uint8_t r[PCF85063_READ_LEN];
    wt_datetime_t dt;

    set_time(r, 0x07, 0x05, 0x09, 0x15, 0x10, 0x26);
    memset(&dt, 0xAA, sizeof(dt));
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.year, 2026);
    CHECK_EQ(dt.month, 10);
    CHECK_EQ(dt.day, 15);
    CHECK_EQ(dt.hour, 9);
    CHECK_EQ(dt.minute, 5);
    CHECK_EQ(dt.second, 7);
    CHECK_EQ(dt.weekday, 4); /* calculado, no el 3 del registro */

    /* Bits que no son de la hora se ignoran (Control_2, offset, RAM, bit7 de minutos). */
    r[1] = 0xFF;
    r[2] = 0xFF;
    r[3] = 0xFF;
    r[5] |= 0x80;
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.minute, 5);

    /* OS: se perdió la alimentación -> no fiable, aunque los campos parezcan bien. */
    set_time(r, 0x80 | 0x07, 0x05, 0x09, 0x15, 0x10, 0x26);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OSC_STOPPED);
    /* Valores tras el arranque en frío del chip (OS = 1, 1-1-2000). */
    set_time(r, 0x80, 0x00, 0x00, 0x01, 0x01, 0x00);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OSC_STOPPED);

    /* STOP en Control_1 */
    set_time(r, 0x07, 0x05, 0x09, 0x15, 0x10, 0x26);
    r[0] = PCF85063_CTRL1_STOP;
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_STOPPED);

    /* Modo 12 h */
    set_time(r, 0x00, 0x00, 0x12, 0x15, 0x10, 0x26); /* 12 AM = 0 h */
    r[0] = PCF85063_CTRL1_12H;
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.hour, 0);
    r[6] = PCF85063_HOUR_PM | 0x12; /* 12 PM = 12 h */
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.hour, 12);
    r[6] = PCF85063_HOUR_PM | 0x11; /* 11 PM = 23 h */
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.hour, 23);
    r[6] = 0x01; /* 1 AM */
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.hour, 1);
    r[6] = 0x00; /* 0 no existe en 12 h */
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_BAD_FIELD);
    r[6] = 0x13;
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_BAD_FIELD);

    /* Campos imposibles */
    const struct { int idx; uint8_t v; } bad[] = {
        {4, 0x60}, {4, 0x0A}, {5, 0x60}, {5, 0x5A}, {6, 0x24}, {6, 0x1A}, {7, 0x00},
        {7, 0x32}, {7, 0x31 /* 31-11 */}, {9, 0x00}, {9, 0x13}, {9, 0x0A}, {10, 0xA0}, {10, 0x9A},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        set_time(r, 0x00, 0x00, 0x00, 0x15, 0x11, 0x26);
        r[bad[i].idx] = bad[i].v;
        if (bad[i].idx == 7 && bad[i].v == 0x31) {
            r[9] = 0x11; /* noviembre tiene 30 días */
        }
        CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_BAD_FIELD);
    }

    /* 29 de febrero */
    set_time(r, 0x00, 0x00, 0x00, 0x29, 0x02, 0x28);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    set_time(r, 0x00, 0x00, 0x00, 0x29, 0x02, 0x27);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_BAD_FIELD);

    /* Bordes válidos */
    set_time(r, 0x59, 0x59, 0x23, 0x31, 0x12, 0x99);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);
    CHECK_EQ(dt.year, 2099);

    /* Fecha anterior a 2025: campos válidos pero no puede ser la hora real. */
    set_time(r, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_IMPLAUSIBLE);
    CHECK_EQ(dt.year, 2000);
    set_time(r, 0x00, 0x00, 0x00, 0x31, 0x12, 0x24);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_IMPLAUSIBLE);
    set_time(r, 0x00, 0x00, 0x00, 0x01, 0x01, 0x25);
    CHECK_EQ(pcf85063_decode(r, &dt), RTC_READ_OK);

    for (int i = RTC_READ_OK; i <= RTC_READ_IMPLAUSIBLE; i++) {
        CHECK(strcmp(pcf85063_read_text((rtc_read_t)i), "?") != 0);
    }
}

void test_time_all(void)
{
    test_calendar();
    test_seconds();
    test_format();
    test_pcf85063();
}
