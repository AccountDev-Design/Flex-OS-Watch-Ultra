/*
 * ui_screen_logic — lógica PURA del apagado de pantalla (F4.5; sin LVGL ni
 * ESP-IDF, se prueba en el PC).
 *
 * Estados: encendida -> atenuada (los últimos segundos) -> apagada. Cualquier
 * actividad vuelve a "encendida"; una retención (aviso sonando, animación de
 * carga, prueba de pantalla) la mantiene encendida. Apagada solo se enciende
 * con un evento (toque, botón, alarma, USB): el tiempo de inactividad no la
 * enciende.
 *
 * Botón lateral (PWR, GPIO10 a través de un transistor): no se supone la
 * polaridad. El nivel en reposo se aprende tras el arranque (estable 500 ms,
 * a partir de 1,5 s: al encender el reloj con ese botón aún se puede estar
 * pulsando) y una pulsación es "distinto del reposo". Una pulsación corta
 * (30 ms .. 2 s) cuenta al soltar; las largas no (a los 4-6 s el AXP2101
 * apaga el reloj por hardware).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SCR_AOD: pantalla siempre activa (fondo negro, hora atenuada, brillo bajo
 * del panel). Se trata como "apagada" para la interacción: solo un evento la
 * enciende del todo. */
typedef enum { SCR_ON = 0, SCR_DIM, SCR_OFF, SCR_AOD } scr_state_t;

#define SCR_DIM_MAX_MS 3000u

/** Duración de la fase atenuada para ese tiempo de apagado. */
uint32_t scr_dim_ms(uint32_t timeout_ms);
/** Estado siguiente según la inactividad (no enciende una pantalla apagada). */
scr_state_t scr_next(scr_state_t cur, uint32_t inactive_ms, uint32_t timeout_ms, bool hold);
/** Igual, con AOD: al agotarse el tiempo pasa a SCR_AOD en vez de SCR_OFF.
 * Con dim = false no hay fase atenuada (se pasa directamente al final). */
scr_state_t scr_next_ex(scr_state_t cur, uint32_t inactive_ms, uint32_t timeout_ms, bool hold, bool aod, bool dim);
/** Atenuación progresiva: brillo a los elapsed_ms de una fase de dim_ms que
 * baja linealmente de user a target (nunca por debajo de target). */
int scr_dim_ramp(int user, int target, uint32_t elapsed_ms, uint32_t dim_ms);

/* Pixel Shift: desplazamiento periódico de los elementos estáticos para
 * repartir el desgaste de los píxeles del AMOLED. Reduce el riesgo de
 * marcas; no lo elimina ni detecta el quemado. */
#define SCR_SHIFT_STEPS 16
/** Desplazamiento del paso step (cíclico) dentro de ±max px. El paso 0 es (0,0). */
void scr_pixel_shift(uint32_t step, int max, int *dx, int *dy);
/** Brillo atenuado (un tercio del del usuario, mínimo 5 %; nunca más que el del usuario). */
int scr_dim_level(int user_percent);

#define BTN_LEARN_AFTER_MS 1500u
#define BTN_STABLE_MS      500u
#define BTN_MIN_MS         30u
#define BTN_SHORT_MAX_MS   2000u

typedef struct {
    int idle;           /**< nivel en reposo (-1 = aún no aprendido) */
    int raw;            /**< último nivel leído */
    uint32_t raw_since; /**< desde cuándo tiene ese nivel */
    bool down;          /**< pulsado (ya filtrado) */
    uint32_t down_ms;   /**< cuándo se pulsó */
} btn_t;

void btn_init(btn_t *b);
/** Devuelve true en el instante de soltar una pulsación corta. */
bool btn_update(btn_t *b, int level, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
