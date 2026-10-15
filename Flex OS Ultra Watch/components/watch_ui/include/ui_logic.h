/*
 * ui_logic — decisiones PURAS de la interfaz (sin LVGL ni ESP-IDF; se prueban
 * en el PC). La interfaz solo pinta lo que estas funciones deciden.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "battery_model.h"
#include "flex_batt_style.h"
#include "watch_time_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Tipo de vista del indicador para un estado confirmado del servicio de batería. */
flex_batt_kind_t ui_batt_kind(batt_state_t state, batt_level_t level);

/** Texto de estado en español. */
const char *ui_batt_state_text(batt_state_t state);

/**
 * Compuerta de la animación de carga. Se dispara una sola vez por cada inicio
 * de carga CONFIRMADO por el servicio (charge_events), y solo si se venía de
 * un estado conocido: al arrancar (UNKNOWN -> CHARGING) o al recuperarse de un
 * error de lectura no se anima. Los eventos anteriores a la creación de la
 * interfaz tampoco se animan.
 */
typedef struct {
    uint32_t seen_events;
    bool primed;
} ui_charge_gate_t;

void ui_charge_gate_init(ui_charge_gate_t *g);
bool ui_charge_gate_update(ui_charge_gate_t *g, uint32_t charge_events, batt_state_t prev_state,
                           batt_state_t state);

/**
 * Textos de la esfera. Con hora válida: "9:05" y "Jueves, 15 de octubre".
 * Sin ella, "--:--" y el motivo (nunca una hora inventada). key cambia solo
 * cuando cambia lo que hay que pintar (minuto, día o estado), para no
 * reescribir las etiquetas cada segundo.
 */
typedef struct {
    bool valid;
    int64_t key;
    char time[8];
    char date[40];
} ui_face_time_t;

void ui_face_time(watch_time_state_t state, const wt_datetime_t *dt, ui_face_time_t *out);

/**
 * Brillo del panel rápido. Mínimo 10 %: por debajo, a plena luz la pantalla
 * parecería apagada y no habría forma visible de recuperarla. Mientras se
 * arrastra el control, el comando al panel (DCS 0x51) se envía como mucho
 * cada UI_BRIGHT_APPLY_MS; al soltar se envía siempre el valor final.
 */
#define UI_BRIGHT_MIN      10
#define UI_BRIGHT_MAX      100
#define UI_BRIGHT_APPLY_MS 60

int ui_brightness_clamp(int percent);
bool ui_brightness_should_apply(int value, int applied, uint32_t last_ms, uint32_t now_ms, bool final);

#ifdef __cplusplus
}
#endif
