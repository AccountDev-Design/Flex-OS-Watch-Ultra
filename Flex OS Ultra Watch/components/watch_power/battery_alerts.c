/*
 * battery_alerts — ver include/battery_alerts.h
 */
#include "battery_alerts.h"

#include <string.h>

void batt_alert_init(batt_alert_state_t *a)
{
    memset(a, 0, sizeof(*a));
    a->alerted = BATT_LEVEL_NORMAL;
}

static int severity(batt_level_t lv)
{
    switch (lv) {
    case BATT_LEVEL_LOW:      return 1;
    case BATT_LEVEL_CRITICAL: return 2;
    default:                  return 0;
    }
}

batt_alert_t batt_alert_update(batt_alert_state_t *a, batt_state_t state, batt_level_t level, uint32_t now_ms)
{
    /* Nivel normal: termina el episodio y se rearman los avisos. Un nivel
     * desconocido (sin datos) no cambia nada. */
    if (level == BATT_LEVEL_NORMAL) {
        a->alerted = BATT_LEVEL_NORMAL;
        return BATT_ALERT_NONE;
    }
    if (level == BATT_LEVEL_UNKNOWN || state != BATT_STATE_DISCHARGING) {
        return BATT_ALERT_NONE;
    }
    if (severity(level) <= severity(a->alerted)) {
        return BATT_ALERT_NONE; /* ya avisado en este episodio */
    }

    const batt_alert_t alert = (level == BATT_LEVEL_CRITICAL) ? BATT_ALERT_CRITICAL : BATT_ALERT_LOW;
    a->alerted = level;
    if (a->fired[alert] && (uint32_t)(now_ms - a->last_ms[alert]) < BATT_ALERT_COOLDOWN_MS) {
        return BATT_ALERT_NONE; /* enfriamiento: se da por avisado */
    }
    a->fired[alert] = true;
    a->last_ms[alert] = now_ms;
    return alert;
}
