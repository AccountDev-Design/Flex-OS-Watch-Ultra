/*
 * act_store — formato persistente de Bienestar e historial en archivos
 * (lógica PURA y POSIX: el mismo código corre en el reloj sobre LittleFS y en
 * el PC sobre una carpeta temporal; se prueba en tests/host).
 *
 * Registros binarios de tamaño fijo, little-endian, con versión y CRC-32
 * (ws_crc32 de watch_settings): un registro dañado se descarta, nunca se usa.
 *   [tipo 1][versión 1][datos][CRC-32 4]
 *
 * Archivos en <dir> (en el reloj, /storage/.sistema/bienestar):
 *  - dias.bin: un registro por día cerrado, en orden. Se añade uno al
 *    cambiar de día (una escritura al día).
 *  - sesiones.bin: un registro por entrenamiento terminado.
 *  - estado.bin: punto de control (día en curso y sesión en marcha). Se
 *    escribe por lotes (cada pocos minutos y en los eventos de la sesión),
 *    nunca en cada paso, de forma atómica (temporal + fsync + rename).
 * Recuperación: al arrancar act_hist_repair() quita un registro a medio
 * escribir (corte de corriente durante un añadido), los registros dañados y
 * los duplicados (mismo día o misma sesión dos veces) y recorta a los
 * últimos ACT_DAYS_KEEP días y ACT_SES_KEEP sesiones. Nunca borra el resto ni
 * formatea nada.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "act_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACT_DAY_REC_LEN   56
#define ACT_SES_REC_LEN   64
#define ACT_STATE_MAX     160
#define ACT_REC_DAY       'D'
#define ACT_REC_SES       'S'
#define ACT_REC_STATE     'E'
#define ACT_REC_VER       1
#define ACT_DAYS_KEEP     730   /* dos años: 41 KB */
#define ACT_SES_KEEP      800   /* 51 KB */
#define ACT_DAYS_MAX      (ACT_DAYS_KEEP + 60)  /* se recorta al pasar de aquí */
#define ACT_SES_MAX       (ACT_SES_KEEP + 100)

#define ACT_FILE_DAYS  "dias.bin"
#define ACT_FILE_SES   "sesiones.bin"
#define ACT_FILE_STATE "estado.bin"

/** Entrenamiento terminado, tal como se guarda y se enseña. */
typedef struct {
    uint32_t id;
    uint8_t type;
    uint8_t end_reason;   /**< act_end_t */
    uint8_t pauses, auto_pauses, auto_resumes;
    int64_t start_utc;    /**< 0 = sin hora */
    int32_t day;          /**< fecha local, o ACT_NO_DAY */
    uint32_t total_s, active_s, paused_s;
    uint32_t steps;
    uint32_t dist_m;
    uint32_t ee;          /**< ukcal/kg */
    uint16_t kcal;        /**< con el peso al terminar */
    bool kcal_valid;
    uint16_t avg_cadence, max_cadence;
    uint16_t motion_mg;
} act_ses_rec_t;

size_t act_day_pack(const act_day_t *d, uint8_t out[ACT_DAY_REC_LEN]);
bool act_day_unpack(const uint8_t in[ACT_DAY_REC_LEN], act_day_t *d);
void act_ses_from(const act_workout_t *w, const act_config_t *c, act_ses_rec_t *r);
size_t act_ses_pack(const act_ses_rec_t *r, uint8_t out[ACT_SES_REC_LEN]);
bool act_ses_unpack(const uint8_t in[ACT_SES_REC_LEN], act_ses_rec_t *r);

/**
 * Punto de control: día en curso, sesión (si está en marcha, con sus tramos
 * cerrados a now_ms) y el siguiente id. utc_s: hora del guardado (0 = sin
 * hora) para saber cuánto estuvo apagado.
 */
size_t act_state_pack(const act_engine_t *e, int64_t now_ms, int64_t utc_s, uint8_t *out, size_t cap);
bool act_state_unpack(const uint8_t *in, size_t n, act_day_t *day, act_workout_t *wk, uint32_t *next_id,
                      int64_t *saved_utc);

/* ---- Archivos (dir debe existir; act_hist_mkdir la crea) ---- */
bool act_hist_mkdir(const char *dir);
bool act_state_save(const char *dir, const uint8_t *buf, size_t n);
/** Bytes leídos (0 si no hay archivo o no cabe). */
size_t act_state_load(const char *dir, uint8_t *buf, size_t cap);
/** Añade el día. Si el último registro es ese mismo día, se queda el más completo
 * (más pasos y energía; a igualdad, el nuevo) y no quedan dos. */
bool act_hist_append_day(const char *dir, const act_day_t *d);
/** Añade la sesión; no duplica si su id está entre las últimas guardadas. */
bool act_hist_append_ses(const char *dir, const act_ses_rec_t *r);

typedef struct {
    uint32_t days, sessions;     /**< registros válidos tras reparar */
    uint32_t dropped;            /**< dañados, a medias o duplicados que se quitaron */
    bool rewritten;
} act_repair_t;
bool act_hist_repair(const char *dir, act_repair_t *out);

/**
 * Días de from..to (ambos incluidos) en out[0..to-from]; los que no tienen
 * registro quedan con day = ACT_NO_DAY. Devuelve cuántos había.
 */
int act_hist_days(const char *dir, int32_t from, int32_t to, act_day_t *out, int cap);
/** Las sesiones más recientes primero, saltando las skip más nuevas. */
int act_hist_sessions(const char *dir, uint32_t skip, act_ses_rec_t *out, int max);
/** Mayor id guardado (0 si no hay): para no repetir ids si se perdió el estado. */
uint32_t act_hist_max_id(const char *dir);

/* ---- Estadísticas y logros (del historial + hoy) ---- */
enum {
    ACT_ACH_FIRST_WORKOUT = 0, /**< primer entrenamiento */
    ACT_ACH_WORKOUTS_10,
    ACT_ACH_WORKOUTS_50,
    ACT_ACH_STEPS_10K,         /**< 10 000 pasos en un día */
    ACT_ACH_STEPS_20K,
    ACT_ACH_STREAK_7,          /**< 7 días seguidos con el objetivo de pasos */
    ACT_ACH_STREAK_30,
    ACT_ACH_ALL_RINGS,         /**< un día con los tres anillos */
    ACT_ACH_DIST_100K,         /**< 100 km estimados en total */
    ACT_ACH_COUNT
};
const char *act_ach_name(int i);
const char *act_ach_desc(int i);

typedef struct {
    uint32_t days;              /**< días con registro */
    uint32_t best_steps;
    int32_t best_day;
    uint32_t total_steps;
    uint32_t total_dist_m;
    uint32_t sessions;
    uint32_t workout_s;         /**< tiempo activo total en entrenamientos */
    uint16_t streak;            /**< racha actual (hasta hoy o ayer) */
    uint16_t best_streak;
    uint32_t all_rings_days;
    uint32_t ach;               /**< bit i: logro i conseguido */
} act_stats_t;

void act_stats_init(act_stats_t *s);
/** Añade un día (en orden de fecha creciente). */
void act_stats_add_day(act_stats_t *s, const act_day_t *d, int32_t *prev_day, uint16_t *run);
/** Cierra las cuentas con hoy (today_met: objetivo de pasos de hoy cumplido). */
void act_stats_finish(act_stats_t *s, int32_t today, bool today_met, int32_t last_day, uint16_t run);
/** Recorre dias.bin entero (en flujo, sin cargarlo) y suma hoy y las sesiones. */
bool act_stats_build(const char *dir, const act_day_t *today, const act_config_t *c, act_stats_t *out);

#ifdef __cplusplus
}
#endif
