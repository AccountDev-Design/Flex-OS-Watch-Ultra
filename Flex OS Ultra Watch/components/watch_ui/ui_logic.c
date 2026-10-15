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
