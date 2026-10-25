/*
 * watch_activity — servicio de actividad de Bienestar (pasos, entrenamientos,
 * día en curso, historial). UNA instancia, independiente de la interfaz.
 *
 *  - Una tarea propia ("watch_act", prioridad 1, núcleo 0) lee el flujo de
 *    muestras de watch_imu (el único lector del QMI8658 sigue siendo su
 *    tarea), alimenta el motor act_engine y guarda. Se despierta cuando el
 *    anillo va por la mitad (unas 32 muestras) o cada segundo: sin esperas
 *    activas ni sondeo rápido.
 *  - La interfaz solo copia instantáneas y caché (watch_activity_get,
 *    watch_activity_history) bajo un cerrojo que nunca se retiene durante la
 *    E/S: nunca espera a la flash ni al IMU. Los comandos (empezar, pausar...)
 *    cambian el estado al momento y la tarea guarda después.
 *  - Sigue contando con la app cerrada, en otra pantalla o con la pantalla
 *    apagada. Con el seguimiento desactivado el IMU vuelve al reposo (salvo
 *    durante un entrenamiento).
 *  - Persistencia: ajustes en NVS (bloque de watch_settings con versión y
 *    CRC); historial y punto de control en LittleFS (/storage/.sistema/
 *    bienestar, act_store) por lotes: el estado cada 5 min si cambió y en
 *    cada evento de la sesión, un registro por día y por entrenamiento. Sin
 *    LittleFS, el punto de control va a NVS cada 15 min y no hay historial.
 *  - Memoria: todo se reserva una vez al arrancar (anillo de muestras y caché
 *    del historial en PSRAM); nada por muestra ni por paso.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "act_engine.h"
#include "act_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WB_HIST_DAYS   28  /* días anteriores a hoy en la caché */
#define WB_HIST_RECENT 10  /* últimos entrenamientos en la caché */
#define WB_PAGE        10  /* entrenamientos por página del historial */

/* Último evento para la interfaz (avisos) */
#define WB_UI_AUTO_PAUSE  0x01u
#define WB_UI_AUTO_RESUME 0x02u
#define WB_UI_AUTO_END    0x04u
#define WB_UI_SUGGEST     0x08u

typedef struct {
    act_view_t v;
    bool started;
    bool imu_present;   /**< hay QMI8658 */
    bool imu_ok;        /**< llegan muestras ahora */
    bool storage_ok;    /**< historial en LittleFS */
    bool persistent;    /**< hay dónde guardar el día y la sesión (LittleFS o NVS) */
    uint32_t ui_seq;    /**< cambia con cada evento de ui_ev */
    uint8_t ui_ev;      /**< WB_UI_* del último */
    /* calidad de la señal y del servicio (diagnóstico) */
    uint32_t samples, dropped, gaps, dups, invalid, ignored;
    uint16_t period_ms;
    uint32_t stack_free;  /**< bytes libres mínimos de la pila de la tarea */
    uint32_t saves, save_errors;
} watch_activity_status_t;

typedef struct {
    uint32_t gen;
    bool storage;
    int32_t today;
    act_day_t days[WB_HIST_DAYS];  /**< days[WB_HIST_DAYS-1] = ayer; day = ACT_NO_DAY si no hay */
    act_ses_rec_t recent[WB_HIST_RECENT];
    uint8_t nrecent;
    act_stats_t stats;             /**< con hoy incluido */
    act_ses_rec_t last;            /**< último entrenamiento terminado (resumen) */
    bool last_valid;
} watch_activity_history_t;

esp_err_t watch_activity_start(void);
void watch_activity_get(watch_activity_status_t *out);
/** Cambia con cualquier dato visible (lectura atómica, sin cerrojo). */
uint32_t watch_activity_generation(void);
void watch_activity_config(act_config_t *out);
/** Aplica y guarda (NVS por lotes). */
void watch_activity_set_config(const act_config_t *c);

esp_err_t watch_activity_workout_start(uint8_t type, bool from_suggestion);
esp_err_t watch_activity_workout_pause(void);
esp_err_t watch_activity_workout_resume(void);
esp_err_t watch_activity_workout_end(void);
void watch_activity_suggest_dismiss(void);

void watch_activity_calib_start(bool run);
/** Termina la calibración y devuelve los pasos contados. */
uint32_t watch_activity_calib_stop(void);

/** Copia de la caché del historial. false si aún no está lista. */
bool watch_activity_history(watch_activity_history_t *out);
uint32_t watch_activity_history_gen(void);
/** Pide una página de entrenamientos (skip = cuántos de los más nuevos saltar). */
void watch_activity_sessions_request(uint32_t skip);
/** La página pedida, cuando esté (gen cambia). Devuelve cuántos. */
int watch_activity_sessions_page(uint32_t *skip, act_ses_rec_t *out, int max, uint32_t *gen);

#ifdef __cplusplus
}
#endif
