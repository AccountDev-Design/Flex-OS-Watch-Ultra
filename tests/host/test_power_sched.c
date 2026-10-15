/*
 * Pruebas de PC de la respuesta rápida del servicio de batería: planificador
 * de lecturas (power_sched) y confirmación con tiempo mínimo (battery_model),
 * simulando el bucle real de la tarea con un cargador de comportamiento fijo.
 */
#include <string.h>

#include "battery_model.h"
#include "power_sched.h"

/* Lecturas de una muestra completa sin el 00h (lo aporta el sondeo):
 * 01h, VBAT alto, bajo y alto otra vez, y A4h. Debe coincidir con sample(). */
#define WATCH_POWER_FULL_READS 5
#include "test_util.h"

static void test_sched_basics(void)
{
    power_sched_t ps;
    power_sched_init(&ps);
    CHECK(power_sched_need_full(&ps, 0, true, 0x08, false)); /* arranque */
    power_sched_full_done(&ps, 0, false, false);
    /* tras el arranque, ventana rápida no activa: muestras cada 3 s */
    CHECK(!power_sched_need_full(&ps, 500, true, 0x08, false));
    CHECK(!power_sched_need_full(&ps, 2500, true, 0x08, false));
    CHECK(power_sched_need_full(&ps, 3000, true, 0x08, false));
    power_sched_full_done(&ps, 3000, false, false);
    CHECK_EQ(power_sched_delay_ms(&ps, 3000, false), PWR_PROBE_MS);

    /* Enchufar: el sondeo ve VBUS y pide muestra completa en el acto. */
    CHECK(power_sched_need_full(&ps, 3500, true, 0x28, false));
    power_sched_full_done(&ps, 3500, false, true);
    CHECK_EQ(power_sched_delay_ms(&ps, 3500, true), PWR_FAST_MS);
    CHECK(power_sched_need_full(&ps, 3700, true, 0x28, true));
    /* bits fuera de la máscara no cuentan como cambio */
    power_sched_full_done(&ps, 3700, true, false);
    CHECK(!power_sched_need_full(&ps, 3750, true, 0x28 | 0x03, false));
    /* dentro de la ventana rápida: cada 200 ms */
    CHECK(power_sched_need_full(&ps, 3900, true, 0x2B, false));
    /* fuera de la ventana: vuelve a 3 s */
    power_sched_full_done(&ps, 6700, false, false);
    CHECK_EQ(power_sched_delay_ms(&ps, 9800, false), PWR_PROBE_MS);
    CHECK(!power_sched_need_full(&ps, 6700 + PWR_FULL_MS - 100, true, 0x2B, false));
    CHECK(power_sched_need_full(&ps, 6700 + PWR_FULL_MS, true, 0x2B, false));
    /* fallo del sondeo: muestra completa (registra el error) */
    CHECK(power_sched_need_full(&ps, 9900, false, 0, false));

    /* el reloj da la vuelta */
    power_sched_init(&ps);
    power_sched_full_done(&ps, 0xFFFFFF00u, true, false);
    CHECK_EQ(power_sched_delay_ms(&ps, 0xFFFFFF00u + 100, false), PWR_FAST_MS);
    CHECK_EQ(power_sched_delay_ms(&ps, 0xFFFFFF00u + PWR_FAST_WINDOW_MS + 1, false), PWR_PROBE_MS);
}

static void test_hold_table(void)
{
    CHECK_EQ(batt_hold_ms(BATT_STATE_CHARGING, BATT_STATE_EXTERNAL_IDLE), BATT_HOLD_CHARGING_EXIT_MS);
    CHECK_EQ(batt_hold_ms(BATT_STATE_DISCHARGING, BATT_STATE_EXTERNAL_IDLE), BATT_HOLD_PLUG_IDLE_MS);
    CHECK_EQ(batt_hold_ms(BATT_STATE_DISCHARGING, BATT_STATE_CHARGING), BATT_HOLD_FAST_MS);
    CHECK_EQ(batt_hold_ms(BATT_STATE_CHARGING, BATT_STATE_DISCHARGING), BATT_HOLD_FAST_MS);
    CHECK_EQ(batt_hold_ms(BATT_STATE_CHARGING, BATT_STATE_FULL), BATT_HOLD_SLOW_MS);
    CHECK_EQ(batt_hold_ms(BATT_STATE_DISCHARGING, BATT_STATE_UNKNOWN), BATT_HOLD_SLOW_MS);
}

/* Cargador simulado: VBUS desde plug_ms; empieza a cargar charge_delay_ms después. */
typedef struct {
    uint32_t plug_ms;
    uint32_t charge_delay_ms;
} charger_t;

static void make_status(const charger_t *c, uint32_t now, axp2101_status_t *s, uint8_t *reg00)
{
    memset(s, 0, sizeof(*s));
    s->chip_ok = true;
    s->batt_present = true;
    s->gauge_enabled = true;
    s->vbat_adc_enabled = true;
    s->percent = 80;
    s->vbat_mv = 3950;
    const bool vbus = now >= c->plug_ms;
    s->vbus_present = vbus;
    if (vbus && now >= c->plug_ms + c->charge_delay_ms) {
        s->dir = AXP_DIR_CHARGE;
        s->stage = AXP_CHG_CC;
    } else {
        s->dir = vbus ? AXP_DIR_STANDBY : AXP_DIR_DISCHARGE;
        s->stage = AXP_CHG_TRICKLE;
    }
    *reg00 = (uint8_t)((vbus ? 0x20 : 0) | 0x08);
}

/* Bucle real de la tarea con el cargador simulado: devuelve cuánto tarda
 * desde que se enchufa hasta que "cargando" queda confirmado, y cuántas
 * lecturas I2C se hacen por segundo de media en reposo. */
static uint32_t run_loop(const charger_t *c, uint32_t until, int *reads_idle, bool *saw_idle_state)
{
    power_sched_t ps;
    batt_tracker_t t;
    power_sched_init(&ps);
    batt_tracker_init(&t);
    uint32_t now = 0, confirmed_at = 0;
    *reads_idle = 0;
    *saw_idle_state = false;
    while (now < until) {
        axp2101_status_t s;
        uint8_t r0;
        make_status(c, now, &s, &r0);
        /* Lecturas en reposo: de 5 s a 10 s (pasada la ventana rápida del arranque) */
        const bool counted = now >= 5000 && now < c->plug_ms;
        if (counted) {
            (*reads_idle)++; /* sondeo: 00h */
        }
        if (power_sched_need_full(&ps, now, true, r0, batt_tracker_pending(&t))) {
            const batt_state_t before = t.state;
            batt_tracker_update_at(&t, &s, true, now);
            if (counted) {
                *reads_idle += WATCH_POWER_FULL_READS;
            }
            power_sched_full_done(&ps, now, t.state != before, batt_tracker_pending(&t));
            if (t.state == BATT_STATE_EXTERNAL_IDLE && now >= c->plug_ms) {
                *saw_idle_state = true;
            }
            if (t.state == BATT_STATE_CHARGING && !confirmed_at) {
                confirmed_at = now;
            }
        }
        now += power_sched_delay_ms(&ps, now, batt_tracker_pending(&t));
    }
    return confirmed_at ? confirmed_at - c->plug_ms : 0xFFFFFFFFu;
}

static void test_end_to_end(void)
{
    int reads;
    bool idle;
    /* Enchufar a los 10 s con carga inmediata: confirmado en < 1 s. */
    charger_t c1 = {10000, 0};
    uint32_t lat = run_loop(&c1, 20000, &reads, &idle);
    CHECK(lat <= 900);
    CHECK(!idle);
    /* Lecturas I2C en reposo (de 5 s a 10 s): menos que antes, cuando eran
     * 9 lecturas cada 2 s = 4,5 por segundo. */
    CHECK(reads * 10 <= 45 * 5);
    /* El cargador tarda 0,8 s en empezar: no se muestra "externa" de paso y
     * se confirma la carga poco después de que empiece. */
    charger_t c2 = {10000, 800};
    lat = run_loop(&c2, 20000, &reads, &idle);
    CHECK(lat <= 800 + 900);
    CHECK(!idle);
    /* Enchufado sin carga (batería llena): "externa sin carga" en ~1,2-2 s. */
    charger_t c3 = {10000, 0xFFFFFFu};
    lat = run_loop(&c3, 20000, &reads, &idle);
    CHECK_EQ(lat, 0xFFFFFFFFu);
    CHECK(idle);
}

/* Fin de carga con oscilación: no debe parpadear aunque se muestree deprisa. */
static void test_no_flicker_fast(void)
{
    batt_tracker_t t;
    batt_tracker_init(&t);
    axp2101_status_t s;
    memset(&s, 0, sizeof(s));
    s.chip_ok = s.batt_present = s.vbus_present = s.gauge_enabled = true;
    s.percent = 99;
    s.dir = AXP_DIR_CHARGE;
    s.stage = AXP_CHG_CV;
    uint32_t now = 0;
    for (int i = 0; i < 5; i++, now += 200) {
        batt_tracker_update_at(&t, &s, true, now);
    }
    CHECK_EQ(t.state, BATT_STATE_CHARGING);
    /* Oscila cada 600 ms entre carga y reposo durante 6 s: sigue "cargando". */
    for (int i = 0; i < 30; i++, now += 200) {
        s.dir = ((i / 3) % 2) ? AXP_DIR_STANDBY : AXP_DIR_CHARGE;
        batt_tracker_update_at(&t, &s, true, now);
        CHECK_EQ(t.state, BATT_STATE_CHARGING);
    }
    /* Reposo estable 4 s: ahora sí, externa sin carga. */
    s.dir = AXP_DIR_STANDBY;
    for (int i = 0; i < 25; i++, now += 200) {
        batt_tracker_update_at(&t, &s, true, now);
    }
    CHECK_EQ(t.state, BATT_STATE_EXTERNAL_IDLE);
}

void test_power_sched_all(void)
{
    test_sched_basics();
    test_hold_table();
    test_end_to_end();
    test_no_flicker_fast();
}
