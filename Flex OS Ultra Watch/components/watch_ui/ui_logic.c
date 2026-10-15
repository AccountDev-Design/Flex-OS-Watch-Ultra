/*
 * ui_logic — ver include/ui_logic.h
 */
#include "ui_logic.h"

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
