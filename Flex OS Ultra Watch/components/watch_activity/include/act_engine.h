/*
 * act_engine — motor de Bienestar: une el detector de pasos, la máquina de
 * estados del entrenamiento, el día en curso y las métricas (lógica PURA,
 * sin ESP-IDF ni E/S; se prueba en el PC).
 *
 * Lo usa el servicio watch_activity (una sola instancia, en su tarea y bajo
 * su cerrojo) y la vista previa de PC. Nada aquí reserva memoria.
 *
 * Reparto sin contar dos veces:
 *  - Pasos y distancia: siempre al día (son de la persona, entrene o no) y,
 *    con una sesión ACTIVA de un tipo con pasos, también a la sesión.
 *  - Energía y tiempo de ejercicio: de UNA fuente a la vez. Con una sesión en
 *    marcha, lo de la sesión (se suma al día al terminar, o en el cambio de
 *    día, por diferencias con su "base"); sin sesión, la de los pasos.
 *  - Reanudación automática: el tren de pasos que la provoca empezó en pausa
 *    (se apuntó al día); al reanudar con fecha del primer paso se pasa a la
 *    sesión y se resta del día.
 *  - Ejercicio automático (fuera de entrenamientos, si la detección está
 *    activa): solo trenes de pasos sostenidos (>= 60 s a >= 100 pasos/min o
 *    corriendo). Un movimiento casual nunca suma minutos.
 *
 * Cambio de día: con la fecha LOCAL de watch_time. Sin hora válida los datos
 * van a un día "sin fecha" que se asigna al primer día válido (bandera
 * ACT_DAY_LATE_DATE).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "act_catalog.h"
#include "act_metrics.h"
#include "act_session.h"
#include "act_steps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACT_NO_DAY INT32_MIN

/* act_day_t.flags */
#define ACT_DAY_LATE_DATE 0x01u /**< hubo datos antes de tener fecha */
#define ACT_DAY_RESTORED  0x02u /**< el reloj se reinició ese día (puede faltar algo) */
#define ACT_DAY_NO_WEIGHT 0x04u /**< se cerró sin peso: kcal no disponible */

typedef struct {
    int32_t day;        /**< días desde 1970 (fecha local), o ACT_NO_DAY */
    uint32_t steps;
    uint32_t steps_run;
    uint32_t walk_cm;   /**< distancia estimada caminando */
    uint32_t run_cm;
    uint32_t ee;        /**< energía activa, ukcal/kg */
    uint32_t ex_wk_s;   /**< ejercicio: tiempo activo de entrenamientos */
    uint32_t ex_auto_s; /**< ejercicio: actividad sostenida detectada */
    uint32_t move_s;    /**< tiempo caminando o corriendo */
    uint32_t hours;     /**< bit h: hubo >= 1 min de movimiento en la hora h */
    uint16_t kcal;      /**< al cerrar, con el peso de entonces */
    uint8_t workouts;
    uint8_t goal_bits;  /**< al cerrar: bit 0 mover, 1 ejercicio, 2 pasos */
    uint8_t flags;
} act_day_t;

typedef struct {
    act_session_t s;
    int64_t start_utc;  /**< 0 = sin hora */
    int32_t day;
    uint32_t steps, steps_run;
    uint32_t dist_cm;
    uint32_t ee_steps;  /**< ukcal/kg por los pasos (modelo de pasos) */
    uint32_t ee_met;    /**< ukcal/kg por MET y tiempo activo */
    uint32_t motion_sum;/**< suma del movimiento (mg) por segundo activo */
    uint32_t motion_n;
    uint16_t max_cadence;
    /* lo ya sumado al día en curso (sesiones que cruzan la medianoche) */
    uint32_t base_ee, base_ex_ms;
} act_workout_t;

/* Eventos para el servicio (se acumulan hasta act_engine_take_events). */
#define ACT_EV_DAY_CLOSED   0x01u /**< closed tiene el día cerrado: guardarlo */
#define ACT_EV_SESSION_DONE 0x02u /**< done tiene la sesión terminada: guardarla */
#define ACT_EV_SESSION      0x04u /**< empezó, se pausó o se reanudó: guardar el estado */
#define ACT_EV_AUTO_PAUSE   0x08u
#define ACT_EV_AUTO_RESUME  0x10u
#define ACT_EV_AUTO_END     0x20u
#define ACT_EV_SUGGEST      0x40u /**< caminata o carrera sostenida: ¿entrenar? */
#define ACT_EV_CONFIG       0x80u /**< cambió la configuración (recientes): guardarla */

typedef struct {
    act_config_t cfg;
    act_steps_t det;
    act_day_t day;
    act_workout_t wk;     /**< sesión en marcha (o la última, COMPLETED) */
    uint32_t next_id;
    uint32_t seen_steps, seen_run;
    int64_t now_ms;
    int64_t tick_ms;
    bool have_tick;
    int hour;             /**< hora local del último tick (-1 sin hora) */
    uint16_t hour_move_s;
    /* tren de pasos en curso (fuera de sesión o en pausa) */
    bool bout_on;
    uint32_t bout_steps, bout_run, bout_cm, bout_ee_bg, bout_auto_s;
    uint32_t move_ms, auto_ms, brisk_ms; /* restos en ms de los contadores en segundos */
    bool sensor_ok;       /**< llegan muestras: sin ellas no hay pausa automática */
    /* sugerencia de entrenamiento */
    bool suggest;
    uint8_t suggest_type;
    bool suggest_done;    /**< ya se ofreció en este tren */
    int64_t walk_off_ms;  /**< desde cuándo no hay tren */
    /* calibración de distancia */
    bool calib;
    bool calib_run;
    uint32_t calib_steps0;
    uint32_t events;
    act_day_t closed;
    act_workout_t done;
    uint32_t gen;
} act_engine_t;

void act_engine_init(act_engine_t *e, const act_config_t *cfg);
void act_engine_set_config(act_engine_t *e, const act_config_t *cfg);
void act_engine_feed(act_engine_t *e, const act_sample_t *s, size_t n);
/** Muestras perdidas (anillo desbordado, sensor sin datos). */
void act_engine_gap(act_engine_t *e);
/** ¿Llegan datos del IMU? Sin ellos no se pausa sola una sesión (no hay evidencia). */
void act_engine_set_sensor(act_engine_t *e, bool ok);
/**
 * Paso del reloj (~1 s). local_s: segundos locales desde 1970 (< 0 sin
 * hora); utc_s: UTC (0 sin hora).
 */
void act_engine_tick(act_engine_t *e, int64_t now_ms, int64_t local_s, int64_t utc_s);
uint32_t act_engine_take_events(act_engine_t *e);

/* Entrenamientos (false si el estado no lo permite) */
/** from_bout: incluye el tren de pasos en curso (sugerencia aceptada). */
bool act_engine_start(act_engine_t *e, uint8_t type, int64_t now_ms, int64_t utc_s, bool from_bout);
bool act_engine_pause(act_engine_t *e, int64_t now_ms);
bool act_engine_resume(act_engine_t *e, int64_t now_ms);
bool act_engine_end(act_engine_t *e, int64_t now_ms);
void act_engine_dismiss_suggest(act_engine_t *e);

/* Calibración de distancia */
void act_engine_calib_start(act_engine_t *e, bool run);
/** Pasos contados desde que empezó la calibración. */
uint32_t act_engine_calib_steps(const act_engine_t *e);
void act_engine_calib_stop(act_engine_t *e);

/** Vista para la interfaz (totales del día con la sesión incluida). */
typedef struct {
    int32_t day;
    uint32_t steps;
    uint32_t dist_m;
    bool dist_generic;     /**< zancada genérica (sin altura ni calibración) */
    uint32_t kcal;
    bool kcal_valid;       /**< hay peso */
    uint32_t ex_s;
    uint32_t move_s;
    uint8_t move_hours;
    act_rings_t rings;
    uint8_t workouts;
    /* detector */
    act_motion_t motion;
    bool walking;
    uint16_t cadence;
    uint16_t motion_mg;
    /* sesión */
    act_ses_state_t ses;
    act_pause_t pause_kind;
    act_end_t end_reason;
    uint8_t type;
    uint32_t ses_id;
    uint32_t total_ms, active_ms, paused_ms;
    uint32_t ses_steps;
    uint32_t ses_dist_m;
    uint32_t ses_kcal;
    bool ses_kcal_valid;
    uint32_t pace_s_km;    /**< 0 sin datos */
    uint16_t ses_cadence;
    uint16_t ses_motion_mg;
    uint8_t auto_pauses;
    int64_t start_utc;
    bool suggest;
    uint8_t suggest_type;
    bool calib;
    uint32_t calib_steps;
    uint32_t gen;
} act_view_t;

void act_engine_view(const act_engine_t *e, act_view_t *v);

/** Kcal de un día guardado (las suyas, o con el peso actual si se cerró sin él). */
bool act_day_kcal(const act_config_t *c, const act_day_t *d, uint32_t *kcal);

/* ---- Restauración tras reiniciar ---- */
/**
 * Pone el estado guardado (act_store) en un motor recién iniciado: el día (si
 * sigue siendo hoy, o se entrega como cerrado si ya pasó) y la sesión (vuelve
 * pausada). gap_ms: tiempo apagado si se conoce, o 0. local_day: hoy (o
 * ACT_NO_DAY).
 */
void act_engine_restore(act_engine_t *e, const act_day_t *day, const act_workout_t *wk, uint32_t next_id,
                        int64_t now_ms, int32_t local_day, uint32_t gap_ms);

#ifdef __cplusplus
}
#endif
