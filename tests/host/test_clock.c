/*
 * Pruebas de PC de las apps de tiempo (ui_clock_logic, F4.3).
 */
#include <string.h>

#include "test_util.h"
#include "ui_clock_logic.h"

#define S(x) ((int64_t)(x) * 1000000)

static void test_stopwatch(void)
{
    sw_t s;
    sw_reset(&s);
    CHECK_EQ(s.st, SW_IDLE);
    CHECK_EQ(sw_elapsed(&s, S(100)), 0);
    sw_lap(&s, S(100)); /* parado: no marca */
    CHECK_EQ(s.n, 0);
    sw_start(&s, S(100));
    CHECK_EQ(sw_elapsed(&s, S(103)), S(3));
    sw_pause(&s, S(105));
    CHECK_EQ(sw_elapsed(&s, S(200)), S(5)); /* en pausa no avanza */
    sw_start(&s, S(300));
    CHECK_EQ(sw_elapsed(&s, S(302)), S(7));
    /* vueltas: 7, 9 (+2), 15 (+6), 16 (+1) */
    sw_lap(&s, S(302));
    sw_lap(&s, S(304));
    sw_lap(&s, S(310));
    sw_lap(&s, S(311));
    CHECK_EQ(s.n, 4);
    CHECK_EQ(s.laps[0].split_us, S(7));
    CHECK_EQ(s.laps[1].split_us, S(2));
    CHECK_EQ(s.laps[2].split_us, S(6));
    CHECK_EQ(s.laps[3].total_us, S(16));
    int b = -1, w = -1;
    CHECK(sw_best_worst(&s, &b, &w));
    CHECK_EQ(b, 3);
    CHECK_EQ(w, 0);
    /* reiniciar desde pausa */
    sw_pause(&s, S(312));
    sw_start(&s, S(400)); /* reanudar conserva */
    CHECK_EQ(s.n, 4);
    sw_pause(&s, S(401));
    sw_reset(&s);
    CHECK_EQ(s.n, 0);
    CHECK_EQ(sw_elapsed(&s, S(500)), 0);

    /* Más de 20 vueltas: se descarta la más antigua, los números siguen */
    sw_start(&s, 0);
    for (int i = 1; i <= 25; i++) {
        sw_lap(&s, S(i));
    }
    CHECK_EQ(s.n, SW_MAX_LAPS);
    CHECK_EQ(s.first_no, 6);
    CHECK_EQ(s.laps[0].total_us, S(6));
    CHECK_EQ(s.laps[SW_MAX_LAPS - 1].total_us, S(25));
    CHECK(!sw_best_worst(&s, &b, &w)); /* todas iguales (1 s) */
    /* Con 2 vueltas no hay mejor ni peor */
    sw_reset(&s);
    sw_start(&s, 0);
    sw_lap(&s, S(1));
    sw_lap(&s, S(3));
    CHECK(!sw_best_worst(&s, &b, &w));

    char t[24];
    sw_format(62340000LL, true, t, sizeof(t)); /* 1:02.34 */
    CHECK(strcmp(t, "01:02.34") == 0);
    sw_format(S(3723) + 450000, true, t, sizeof(t));
    CHECK(strcmp(t, "1:02:03.45") == 0);
    sw_format(S(59) + 999999, false, t, sizeof(t));
    CHECK(strcmp(t, "00:59") == 0);
    sw_format(-5, true, t, sizeof(t));
    CHECK(strcmp(t, "00:00.00") == 0);
}

static void test_timer(void)
{
    tmr_t t;
    memset(&t, 0, sizeof(t));
    tmr_start(&t, 0, 0);
    CHECK_EQ(t.st, TMR_IDLE); /* 0 s no arranca */
    tmr_start(&t, S(300), S(1000));
    CHECK_EQ(tmr_left(&t, S(1100)), S(200));
    CHECK_EQ(tmr_permille(&t, S(1100)), 666);
    tmr_pause(&t, S(1100));
    CHECK_EQ(tmr_left(&t, S(5000)), S(200));
    tmr_resume(&t, S(5000));
    CHECK(!tmr_check(&t, S(5199)));
    CHECK(tmr_check(&t, S(5200)));
    CHECK(!tmr_check(&t, S(5201))); /* una sola vez */
    CHECK_EQ(t.st, TMR_RING);
    CHECK_EQ(tmr_left(&t, S(5300)), 0);
    tmr_add(&t, S(60), S(6000)); /* +1 min tras sonar */
    CHECK_EQ(t.st, TMR_RUN);
    CHECK_EQ(tmr_left(&t, S(6000)), S(60));
    tmr_add(&t, S(30), S(6010));
    CHECK_EQ(tmr_left(&t, S(6010)), S(80));
    tmr_cancel(&t);
    CHECK_EQ(t.st, TMR_IDLE);
    /* tope de 99:59 */
    tmr_start(&t, S(200 * 60), 0);
    CHECK_EQ(t.dur_us, TMR_MAX_US);
    char b[16];
    tmr_format(S(299) + 1, b, sizeof(b)); /* 4:59 y un poco: se ve 05:00 */
    CHECK(strcmp(b, "05:00") == 0);
    tmr_format(1, b, sizeof(b));
    CHECK(strcmp(b, "00:01") == 0);
    tmr_format(0, b, sizeof(b));
    CHECK(strcmp(b, "00:00") == 0);
    tmr_format(S(3600), b, sizeof(b));
    CHECK(strcmp(b, "1:00:00") == 0);
}

static int64_t loc(int y, int mo, int d, int h, int mi)
{
    wt_datetime_t dt = {y, mo, d, h, mi, 0, 0};
    return wt_to_seconds(&dt);
}

static void test_alarms(void)
{
    /* 2026-10-15 es jueves (4) */
    alarm_t a = {1, 7, 30, 0x3E}; /* L a V */
    wt_datetime_t dt = {2026, 10, 15, 7, 30, 12, 4};
    CHECK(alarm_matches(&a, &dt));
    dt.minute = 31;
    CHECK(!alarm_matches(&a, &dt));
    dt = (wt_datetime_t){2026, 10, 17, 7, 30, 0, 6}; /* sábado */
    CHECK(!alarm_matches(&a, &dt));
    a.on = 0;
    dt.weekday = 4;
    CHECK(!alarm_matches(&a, &dt));

    /* Próxima: jueves 8:00 -> viernes 7:30; viernes 8:00 -> lunes 7:30 */
    a = (alarm_t){1, 7, 30, 0x3E};
    CHECK_EQ(alarm_next_local(&a, loc(2026, 10, 15, 8, 0)), loc(2026, 10, 16, 7, 30));
    CHECK_EQ(alarm_next_local(&a, loc(2026, 10, 16, 8, 0)), loc(2026, 10, 19, 7, 30));
    /* En el mismo minuto ya cuenta como pasada (estrictamente posterior) */
    CHECK_EQ(alarm_next_local(&a, loc(2026, 10, 15, 7, 30)), loc(2026, 10, 16, 7, 30));
    CHECK_EQ(alarm_next_local(&a, loc(2026, 10, 15, 7, 29)), loc(2026, 10, 15, 7, 30));
    /* Una vez: hoy si no ha pasado, si no mañana */
    alarm_t once = {1, 23, 59, 0};
    CHECK_EQ(alarm_next_local(&once, loc(2026, 10, 15, 23, 0)), loc(2026, 10, 15, 23, 59));
    alarm_t mid = {1, 0, 0, 0};
    CHECK_EQ(alarm_next_local(&mid, loc(2026, 12, 31, 23, 59)), loc(2027, 1, 1, 0, 0));
    /* Solo domingos */
    alarm_t sun = {1, 9, 0, 0x01};
    CHECK_EQ(alarm_next_local(&sun, loc(2026, 10, 15, 12, 0)), loc(2026, 10, 18, 9, 0));
    a.on = 0;
    CHECK_EQ(alarm_next_local(&a, loc(2026, 10, 15, 8, 0)), -1);

    alarm_set_t set = {3, {{1, 7, 30, 0x3E}, {0, 6, 0, 0x7F}, {1, 22, 0, 0}}};
    int idx = -1;
    CHECK_EQ(alarms_next(&set, loc(2026, 10, 15, 8, 0), &idx), loc(2026, 10, 15, 22, 0));
    CHECK_EQ(idx, 2);
    set.a[2].on = 0;
    CHECK_EQ(alarms_next(&set, loc(2026, 10, 15, 8, 0), &idx), loc(2026, 10, 16, 7, 30));
    CHECK_EQ(idx, 0);
    set.a[0].on = 0;
    CHECK_EQ(alarms_next(&set, loc(2026, 10, 15, 8, 0), &idx), -1);
    CHECK_EQ(idx, -1);

    char b[32];
    alarm_days_text(0, b, sizeof(b));
    CHECK(strcmp(b, "Una vez") == 0);
    alarm_days_text(0x7F, b, sizeof(b));
    CHECK(strcmp(b, "Todos los días") == 0);
    alarm_days_text(0x3E, b, sizeof(b));
    CHECK(strcmp(b, "L a V") == 0);
    alarm_days_text(0x41, b, sizeof(b));
    CHECK(strcmp(b, "Fin de semana") == 0);
    alarm_days_text(0x2B, b, sizeof(b)); /* D L X V */
    CHECK(strcmp(b, "L X V D") == 0);

    const int64_t now = loc(2026, 10, 15, 8, 0);
    alarm_when_text(loc(2026, 10, 15, 22, 0), now, true, b, sizeof(b));
    CHECK(strcmp(b, "hoy 22:00") == 0);
    alarm_when_text(loc(2026, 10, 16, 7, 30), now, true, b, sizeof(b));
    CHECK(strcmp(b, "mañana 7:30") == 0);
    alarm_when_text(loc(2026, 10, 19, 7, 30), now, false, b, sizeof(b));
    CHECK(strcmp(b, "lun 7:30 AM") == 0);

    /* Empaquetado: ida y vuelta, y datos imposibles rechazados */
    uint8_t buf[32];
    set = (alarm_set_t){2, {{1, 7, 30, 0x3E}, {0, 23, 59, 0}}};
    const size_t n = alarm_pack(&set, buf, sizeof(buf));
    CHECK_EQ(n, 8);
    alarm_set_t back;
    CHECK(alarm_unpack(buf, n, &back));
    CHECK_EQ(back.n, 2);
    CHECK(memcmp(&back.a[0], &set.a[0], sizeof(alarm_t)) == 0);
    CHECK(memcmp(&back.a[1], &set.a[1], sizeof(alarm_t)) == 0);
    buf[1] = 24; /* hora imposible */
    CHECK(!alarm_unpack(buf, n, &back));
    CHECK_EQ(back.n, 0);
    CHECK(!alarm_unpack(buf, 7, &back));
    CHECK(!alarm_unpack(buf, 24, &back)); /* 6 alarmas: más que AL_MAX */
    CHECK(alarm_unpack(buf, 0, &back));
    CHECK_EQ(back.n, 0);
    CHECK_EQ(alarm_pack(&set, buf, 4), 0);
}

void test_clock_all(void)
{
    test_stopwatch();
    test_timer();
    test_alarms();
}
