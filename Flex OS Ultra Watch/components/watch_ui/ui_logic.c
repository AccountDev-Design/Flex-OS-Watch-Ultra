/*
 * ui_logic — ver include/ui_logic.h
 */
#include "ui_logic.h"

#include <stdio.h>

flex_batt_kind_t ui_batt_kind(batt_state_t state, batt_level_t level)
{
    switch (state) {
    case BATT_STATE_NO_BATTERY:    return FLEX_BATT_ABSENT;
    case BATT_STATE_CHARGING:      return FLEX_BATT_CHARGING;
    case BATT_STATE_FULL:          return FLEX_BATT_FULL;
    case BATT_STATE_EXTERNAL_IDLE: return FLEX_BATT_EXTERNAL;
    case BATT_STATE_DISCHARGING:
        if (level == BATT_LEVEL_CRITICAL) {
            return FLEX_BATT_CRITICAL;
        }
        if (level == BATT_LEVEL_LOW) {
            return FLEX_BATT_LOW;
        }
        return FLEX_BATT_NORMAL;
    default:
        return FLEX_BATT_UNKNOWN;
    }
}

const char *ui_batt_state_text(batt_state_t state)
{
    switch (state) {
    case BATT_STATE_NO_BATTERY:    return "Sin batería";
    case BATT_STATE_DISCHARGING:   return "Descargando";
    case BATT_STATE_CHARGING:      return "Cargando";
    case BATT_STATE_FULL:          return "Carga completa";
    case BATT_STATE_EXTERNAL_IDLE: return "Externa sin carga";
    default:                       return "Sin datos";
    }
}

void ui_charge_gate_init(ui_charge_gate_t *g)
{
    g->seen_events = 0;
    g->primed = false;
}

bool ui_charge_gate_update(ui_charge_gate_t *g, uint32_t charge_events, batt_state_t prev_state,
                           batt_state_t state)
{
    if (!g->primed) {
        g->seen_events = charge_events;
        g->primed = true;
        return false;
    }
    if (charge_events == g->seen_events) {
        return false;
    }
    g->seen_events = charge_events;
    return state == BATT_STATE_CHARGING && prev_state != BATT_STATE_UNKNOWN;
}

void ui_face_time(watch_time_state_t state, const wt_datetime_t *dt, ui_face_time_t *out)
{
    out->valid = (state == WATCH_TIME_OK && dt != NULL);
    if (out->valid) {
        wt_fmt_hhmm(out->time, sizeof(out->time), dt);
        wt_fmt_date_long(out->date, sizeof(out->date), dt);
        /* minutos desde 1970: cambia una vez por minuto */
        out->key = wt_days_from_civil(dt->year, dt->month, dt->day) * 1440 + dt->hour * 60 + dt->minute;
        return;
    }
    snprintf(out->time, sizeof(out->time), "--:--");
    switch (state) {
    case WATCH_TIME_NOT_SET:
        snprintf(out->date, sizeof(out->date), "Hora sin configurar");
        break;
    case WATCH_TIME_NO_RTC:
        snprintf(out->date, sizeof(out->date), "Reloj RTC no disponible");
        break;
    default:
        snprintf(out->date, sizeof(out->date), "Leyendo la hora…");
        break;
    }
    out->key = -1 - (int64_t)state; /* negativo: no choca con una hora válida */
}

int ui_brightness_clamp(int percent)
{
    if (percent < UI_BRIGHT_MIN) {
        return UI_BRIGHT_MIN;
    }
    return percent > UI_BRIGHT_MAX ? UI_BRIGHT_MAX : percent;
}

bool ui_brightness_should_apply(int value, int applied, uint32_t last_ms, uint32_t now_ms, bool final)
{
    if (value == applied) {
        return false;
    }
    return final || (uint32_t)(now_ms - last_ms) >= UI_BRIGHT_APPLY_MS;
}
