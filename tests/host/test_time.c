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

static wt_datetime_t mk(int y, int mo, int d, int h, int mi, int sec)
{
    wt_datetime_t dt = {y, mo, d, h, mi, sec, 0};
    return dt;
}

/* F4.2: codificación para escribir el RTC (casos E1-E9 de la auditoría). */
static void test_encode(void)
{
    uint8_t o[PCF85063_TIME_LEN];
    wt_datetime_t dt = mk(2026, 10, 15, 9, 5, 7);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_OK);
    static const uint8_t e1[PCF85063_TIME_LEN] = {0x07, 0x05, 0x09, 0x15, 0x04, 0x10, 0x26};
    CHECK(memcmp(o, e1, sizeof(e1)) == 0);

    /* 12 h, simétrico con el decodificador */
    static const struct { int h; uint8_t b; } h12[] = {{0, 0x12}, {1, 0x01}, {11, 0x11}, {12, 0x32}, {13, 0x21}, {23, 0x31}};
    for (size_t i = 0; i < sizeof(h12) / sizeof(h12[0]); i++) {
        dt = mk(2026, 10, 15, h12[i].h, 0, 0);
        pcf85063_encode(&dt, true, o);
        CHECK_EQ(o[2], h12[i].b);
    }
    /* Límites */
    dt = mk(2000, 1, 1, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_OK);
    static const uint8_t e3a[PCF85063_TIME_LEN] = {0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00};
    CHECK(memcmp(o, e3a, sizeof(e3a)) == 0);
    dt = mk(2099, 12, 31, 23, 59, 59);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_OK);
    static const uint8_t e3b[PCF85063_TIME_LEN] = {0x59, 0x59, 0x23, 0x31, 0x04, 0x12, 0x99};
    CHECK(memcmp(o, e3b, sizeof(e3b)) == 0);
    /* Fechas y horas imposibles: error y out sin tocar */
    memset(o, 0xEE, sizeof(o));
    dt = mk(2028, 2, 29, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_OK);
    memset(o, 0xEE, sizeof(o));
    dt = mk(2027, 2, 29, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_DATE);
    CHECK_EQ(o[0], 0xEE);
    dt = mk(2026, 11, 31, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_DATE);
    dt = mk(2026, 13, 1, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_DATE);
    dt = mk(2026, 0, 1, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_DATE);
    dt = mk(1999, 12, 31, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_YEAR);
    dt = mk(2100, 1, 1, 0, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_YEAR);
    dt = mk(2026, 1, 1, 24, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_TIME);
    dt = mk(2026, 1, 1, -1, 0, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_TIME);
    dt = mk(2026, 1, 1, 0, 60, 0);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_TIME);
    dt = mk(2026, 1, 1, 0, 0, 60);
    CHECK_EQ(pcf85063_encode(&dt, false, o), RTC_ENC_BAD_TIME);
    /* Se ignora el día de la semana de entrada; OS siempre a 0 */
    dt = mk(2026, 10, 15, 9, 5, 59);
    dt.weekday = 99;
    pcf85063_encode(&dt, false, o);
    CHECK_EQ(o[4], 4);
    CHECK_EQ(o[0] & PCF85063_SEC_OS, 0);
    /* Rango en segundos UTC */
    CHECK_EQ(pcf85063_encode_utc(946684799LL, false, o), RTC_ENC_BAD_YEAR);
    CHECK_EQ(pcf85063_encode_utc(4102444800LL, false, o), RTC_ENC_BAD_YEAR);
    CHECK_EQ(pcf85063_encode_utc(946684800LL, false, o), RTC_ENC_OK);
    CHECK_EQ(pcf85063_encode_utc(4102444799LL, false, o), RTC_ENC_OK);

    /* Ida y vuelta con el decodificador: cada segundo de un día (24 h y
     * 12 h) y cada día de 2000 a 2099. */
    uint8_t r[PCF85063_READ_LEN];
    int bad = 0;
    for (int mode = 0; mode < 2; mode++) {
        for (int t = 0; t < 86400; t++) {
            wt_datetime_t in = mk(2026, 10, 15, t / 3600, (t / 60) % 60, t % 60), got;
            memset(r, 0, sizeof(r));
            r[0] = mode ? PCF85063_CTRL1_12H : 0;
            if (pcf85063_encode(&in, mode, &r[4]) != RTC_ENC_OK || pcf85063_decode(r, &got) != RTC_READ_OK ||
                wt_to_seconds(&got) != wt_to_seconds(&in)) {
                bad++;
            }
        }
    }
    for (int64_t d = wt_days_from_civil(2000, 1, 1); d <= wt_days_from_civil(2099, 12, 31); d++) {
        wt_datetime_t in, got;
        wt_from_seconds(d * 86400 + 12 * 3600, &in);
        memset(r, 0, sizeof(r));
        if (pcf85063_encode(&in, false, &r[4]) != RTC_ENC_OK) {
            bad++;
            continue;
        }
        const rtc_read_t res = pcf85063_decode(r, &got);
        if ((in.year >= WT_MIN_PLAUSIBLE_YEAR ? res != RTC_READ_OK : res != RTC_READ_IMPLAUSIBLE) ||
            wt_to_seconds(&got) != wt_to_seconds(&in) || r[8] != got.weekday) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);

    /* Control_1: conserva CIE y CAP_SEL, 24 h, nunca SR ni EXT_TEST */
    static const struct { uint8_t in, stop, run; } c1[] = {
        {0x00, 0x20, 0x00}, {0x01, 0x21, 0x01}, {0x07, 0x25, 0x05}, {0xFF, 0x25, 0x05}, {0x58, 0x20, 0x00}, {0x22, 0x20, 0x00},
    };
    for (size_t i = 0; i < sizeof(c1) / sizeof(c1[0]); i++) {
        CHECK_EQ(pcf85063_ctrl1_for_set(c1[i].in, true), c1[i].stop);
        CHECK_EQ(pcf85063_ctrl1_for_set(c1[i].in, false), c1[i].run);
        CHECK((pcf85063_ctrl1_for_set(c1[i].in, true) & PCF85063_CTRL1_SR) == 0);
    }
}

/* F4.2: zona horaria (desfase fijo) y formatos de 12 h. */
static void test_tz(void)
{
    wt_datetime_t dt;
    /* 2026-10-15 14:05:07Z en Lima (-300) = 9:05:07 del 15 */
    wt_utc_to_local(1792073107LL, -300, &dt);
    CHECK_EQ(dt.year, 2026);
    CHECK_EQ(dt.month, 10);
    CHECK_EQ(dt.day, 15);
    CHECK_EQ(dt.hour, 9);
    CHECK_EQ(dt.minute, 5);
    CHECK_EQ(dt.second, 7);
    /* Cambio de año hacia atrás */
    wt_datetime_t z = mk(2026, 1, 1, 3, 0, 0);
    wt_utc_to_local(wt_to_seconds(&z), -300, &dt);
    CHECK_EQ(dt.year, 2025);
    CHECK_EQ(dt.month, 12);
    CHECK_EQ(dt.day, 31);
    CHECK_EQ(dt.hour, 22);
    /* Ida y vuelta local -> UTC -> local para todos los desfases de 15 min */
    int bad = 0;
    for (int off = -720; off <= 840; off += 15) {
        wt_datetime_t in = mk(2026, 3, 1, 0, 30, 0), out;
        wt_utc_to_local(wt_local_to_utc(&in, off), off, &out);
        if (wt_to_seconds(&out) != wt_to_seconds(&in) || out.day != 1 || out.month != 3) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);
    char b[16];
    wt_fmt_utc_offset(b, sizeof(b), -300);
    CHECK(strcmp(b, "UTC-05:00") == 0);
    wt_fmt_utc_offset(b, sizeof(b), 330);
    CHECK(strcmp(b, "UTC+05:30") == 0);
    wt_fmt_utc_offset(b, sizeof(b), 345);
    CHECK(strcmp(b, "UTC+05:45") == 0);
    wt_fmt_utc_offset(b, sizeof(b), -570);
    CHECK(strcmp(b, "UTC-09:30") == 0);
    wt_fmt_utc_offset(b, sizeof(b), 0);
    CHECK(strcmp(b, "UTC") == 0);
    /* 12 h */
    static const struct { int h; const char *t, *ap; } h[] = {
        {0, "12:05", "AM"}, {1, "1:05", "AM"}, {11, "11:05", "AM"}, {12, "12:05", "PM"}, {13, "1:05", "PM"}, {23, "11:05", "PM"},
    };
    for (size_t i = 0; i < sizeof(h) / sizeof(h[0]); i++) {
        dt = mk(2026, 10, 15, h[i].h, 5, 0);
        wt_fmt_hhmm12(b, sizeof(b), &dt);
        CHECK(strcmp(b, h[i].t) == 0);
        CHECK(strcmp(wt_ampm(&dt), h[i].ap) == 0);
    }
}

void test_time_all(void)
{
    test_encode();
    test_tz();
    test_calendar();
    test_seconds();
    test_format();
    test_pcf85063();
}
