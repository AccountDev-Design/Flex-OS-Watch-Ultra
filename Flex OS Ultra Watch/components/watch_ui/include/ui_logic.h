/*
 * ui_logic — decisiones PURAS de la interfaz (sin LVGL ni ESP-IDF; se prueban
 * en el PC). La interfaz solo pinta lo que estas funciones deciden.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "battery_model.h"
#include "flex_batt_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Tipo de vista del indicador para un estado confirmado del servicio de batería. */
flex_batt_kind_t ui_batt_kind(batt_state_t state, batt_level_t level);

/** Texto de estado en español. */
const char *ui_batt_state_text(batt_state_t state);

#ifdef __cplusplus
}
#endif
