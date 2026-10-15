/*
 * Pruebas de PC de los avisos de batería (watch_power/battery_alerts).
 */
#include "test_util.h"

#include "battery_alerts.h"

#define MIN (60u * 1000u)

static void test_alert_sequences(void)
{
    batt_alert_state_t a;
    batt_alert_init(&a);
    const batt_state_t D = BATT_STATE_DISCHARGING, C = BATT_STATE_CHARGING;

    /* Normal: nada */
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_NORMAL, 0), BATT_ALERT_NONE);
    /* Entra en LOW: un aviso, y no se repite en las siguientes lecturas */
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_LOW, 1 * MIN), BATT_ALERT_LOW);
    for (uint32_t t = 2; t < 30; t++) {
        CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_LOW, t * MIN), BATT_ALERT_NONE);
    }
    /* Empeora a CRITICAL: un aviso crítico, una sola vez */
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_CRITICAL, 30 * MIN), BATT_ALERT_CRITICAL);
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_CRITICAL, 31 * MIN), BATT_ALERT_NONE);
    /* Sin datos un momento: no rearma ni avisa */
    CHECK_EQ(batt_alert_update(&a, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, 32 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_CRITICAL, 33 * MIN), BATT_ALERT_NONE);
    /* Se enchufa: nunca avisa cargando, aunque el nivel siga bajo */
    CHECK_EQ(batt_alert_update(&a, C, BATT_LEVEL_CRITICAL, 34 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&a, C, BATT_LEVEL_LOW, 40 * MIN), BATT_ALERT_NONE);
    /* Se desenchufa todavía en LOW: ya avisado en este episodio -> nada */
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_LOW, 41 * MIN), BATT_ALERT_NONE);
    /* Recupera nivel normal (fin del episodio) y vuelve a bajar: nuevo aviso */
    CHECK_EQ(batt_alert_update(&a, C, BATT_LEVEL_NORMAL, 60 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&a, D, BATT_LEVEL_LOW, 200 * MIN), BATT_ALERT_LOW);
}

static void test_alert_external_and_cooldown(void)
{
    batt_alert_state_t a;
    batt_alert_init(&a);
    /* Con USB sin carga (externa) o completa: nada */
    CHECK_EQ(batt_alert_update(&a, BATT_STATE_EXTERNAL_IDLE, BATT_LEVEL_LOW, 0), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&a, BATT_STATE_FULL, BATT_LEVEL_LOW, 1), BATT_ALERT_NONE);
    /* Desenchufar en LOW sin haber avisado: se avisa ahora */
    CHECK_EQ(batt_alert_update(&a, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 2), BATT_ALERT_LOW);
    /* Arranque ya en CRITICAL: aviso crítico directo (sin pasar por LOW) */
    batt_alert_state_t b;
    batt_alert_init(&b);
    CHECK_EQ(batt_alert_update(&b, BATT_STATE_DISCHARGING, BATT_LEVEL_CRITICAL, 0), BATT_ALERT_CRITICAL);
    CHECK_EQ(batt_alert_update(&b, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 1), BATT_ALERT_NONE);
    /* Enfriamiento: rearmado dentro de 10 min -> suprimido y dado por avisado */
    batt_alert_state_t c;
    batt_alert_init(&c);
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 0), BATT_ALERT_LOW);
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, 2 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 5 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 15 * MIN), BATT_ALERT_NONE);
    /* ...hasta un nuevo episodio pasado el enfriamiento */
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, 20 * MIN), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&c, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, 21 * MIN), BATT_ALERT_LOW);
    /* El reloj monótono que da la vuelta (uint32) no rompe el enfriamiento */
    batt_alert_state_t w;
    batt_alert_init(&w);
    const uint32_t near_wrap = 0xFFFFFFFFu - MIN;
    CHECK_EQ(batt_alert_update(&w, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, near_wrap), BATT_ALERT_LOW);
    CHECK_EQ(batt_alert_update(&w, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, near_wrap + MIN / 2), BATT_ALERT_NONE);
    CHECK_EQ(batt_alert_update(&w, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, near_wrap + 2 * MIN), BATT_ALERT_NONE);
}

void test_alerts_all(void)
{
    test_alert_sequences();
    test_alert_external_and_cooldown();
}
