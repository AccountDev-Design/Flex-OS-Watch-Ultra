/*
 * act_metrics — configuración de Bienestar y modelos de las métricas
 * estimadas (lógica PURA; se prueba en el PC).
 *
 * Qué se MIDE y qué se ESTIMA (la interfaz lo dice igual):
 *  - Medido con el IMU: pasos (detector act_steps), cadencia, movimiento de
 *    la muñeca, tiempos de actividad y de pausa.
 *  - Estimado: distancia (pasos x zancada; nunca GPS), calorías activas
 *    (sin frecuencia cardíaca) y ritmo.
 *  - No disponible en este reloj: frecuencia cardíaca, SpO2, GPS, pisos
 *    subidos (no hay barómetro), brazadas y largos.
 *
 * Zancada (longitud de un paso): la del usuario si la escribió o la calibró;
 * si no, a partir de la altura (0,415 x altura al caminar; 0,65 x altura al
 * correr, aproximaciones habituales); sin altura, 0,70 m y 1,00 m genéricos
 * (marcados como genéricos).
 *
 * Calorías activas: energía NETA (por encima del reposo), lineal en el peso.
 * Por eso se acumula "energía por kg" y se multiplica por el peso al
 * enseñarla: si el peso falta no se inventa (la interfaz pide añadirlo) y al
 * añadirlo el día de hoy ya sale bien.
 *  - Caminar/correr con pasos: ACSM (Guidelines for Exercise Testing and
 *    Prescription), componente horizontal: 0,1 ml O2/kg por metro al caminar
 *    y 0,2 al correr; 5 kcal por litro de O2. Por metro: 0,0005 y 0,001
 *    kcal/kg.
 *  - Resto de entrenamientos: (MET - 1) x horas activas (MET del Compendio,
 *    act_catalog.h). Solo cuenta el tiempo ACTIVO, nunca el pausado.
 *  - Fuera de un entrenamiento solo suman los pasos confirmados (no se
 *    calcula gasto por "movimiento ligero": no hay un modelo fiable).
 *  - Nunca dos fuentes a la vez: con un entrenamiento en marcha, el gasto es
 *    el de la sesión; si no, el de los pasos (act_engine).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "act_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACT_FAV_MAX    8
#define ACT_RECENT_MAX 4

/* Rangos de los ajustes */
#define ACT_GOAL_KCAL_MIN   50
#define ACT_GOAL_KCAL_MAX   3000
#define ACT_GOAL_EX_MIN     5
#define ACT_GOAL_EX_MAX     240
#define ACT_GOAL_STEPS_MIN  500
#define ACT_GOAL_STEPS_MAX  50000
#define ACT_WEIGHT_MIN_DKG  250   /* 25,0 kg */
#define ACT_WEIGHT_MAX_DKG  2500
#define ACT_HEIGHT_MIN_CM   100
#define ACT_HEIGHT_MAX_CM   230
#define ACT_AGE_MIN         10
#define ACT_AGE_MAX         110
#define ACT_STRIDE_MIN_CM   30
#define ACT_STRIDE_MAX_CM   220
#define ACT_PAUSE_MIN_S     10
#define ACT_PAUSE_MAX_S     120
#define ACT_END_MIN_S       60
#define ACT_END_MAX_S       3600

typedef struct {
    /* Seguimiento */
    bool tracking;        /**< contar pasos en segundo plano (IMU encendido) */
    bool auto_detect;     /**< detectar caminatas/carreras y sugerir entrenar */
    uint8_t sensitivity;  /**< ACT_SENS_* (act_steps.h) */
    act_auto_cfg_t autos; /**< pausa, reanudación y finalización automáticas */
    /* Objetivos diarios */
    uint16_t goal_kcal;
    uint16_t goal_ex_min;
    uint16_t goal_steps;
    /* Datos personales (0 = sin definir) */
    uint16_t weight_dkg;  /**< décimas de kg */
    uint8_t height_cm;
    uint8_t age;
    uint8_t stride_walk_cm; /**< 0 = automática */
    uint8_t stride_run_cm;
    /* Favoritos y recientes (ids de act_catalog; 0 = vacío) */
    uint8_t fav[ACT_FAV_MAX];
    uint8_t recent[ACT_RECENT_MAX];
    /* Última calibración (informativa) */
    int8_t calib_walk_err; /**< % de error antes de calibrar (estimada vs real) */
    int8_t calib_run_err;
} act_config_t;

#define ACT_CFG_VER 1

void act_config_default(act_config_t *c);
/** Lleva cada campo a su rango. true si cambió algo. */
bool act_config_normalize(act_config_t *c);
/** Empaqueta (little-endian, sin depender del compilador). Devuelve bytes, 0 si no cabe. */
size_t act_config_pack(const act_config_t *c, uint8_t *out, size_t cap);
/** false si no tiene la longitud de la versión (los valores se normalizan). */
bool act_config_unpack(const uint8_t *in, size_t n, uint8_t ver, act_config_t *c);
bool act_config_is_fav(const act_config_t *c, uint8_t id);
/** Añade o quita de favoritos. false si está lleno. */
bool act_config_toggle_fav(act_config_t *c, uint8_t id);
/** Pone id el primero de los recientes (sin duplicados). */
void act_config_push_recent(act_config_t *c, uint8_t id);

/* ---- Zancada y distancia ---- */
typedef enum { ACT_STRIDE_USER = 0, ACT_STRIDE_HEIGHT, ACT_STRIDE_GENERIC } act_stride_src_t;
/** Longitud de paso en cm al caminar (run = false) o correr. */
uint16_t act_stride_cm(const act_config_t *c, bool run, act_stride_src_t *src);
/** Zancada calibrada con un recorrido conocido: real_m / pasos, en cm
 * (0 si los datos no sirven: < 50 pasos o fuera de rango). */
uint16_t act_calibrate_stride(uint32_t real_m, uint32_t steps);
/** Error de la estimación en % ((estimada - real) / real), acotado a ±100. */
int act_calib_error_pct(uint32_t est_m, uint32_t real_m);

/* ---- Energía (millonésimas de kcal por kg: un solo paso no se redondea a 0) ---- */
/** Por la distancia de pasos: caminar 5 / correr 10 ukcal/kg por cm (exacto). */
uint32_t act_ee_steps(uint32_t dist_cm, bool run);
/** Por MET y tiempo activo: (MET - 1) x h, en ukcal/kg. */
uint32_t act_ee_met(uint8_t met10, uint32_t active_ms);
/** kcal = energía por kg x peso. false (y 0) si no hay peso. */
bool act_kcal(const act_config_t *c, uint32_t ee_ukcal_kg, uint32_t *kcal);

/* ---- Anillos y objetivos ---- */
typedef struct {
    uint16_t pct[3];   /**< mover, ejercicio, pasos: % del objetivo (puede pasar de 100) */
    bool kcal_valid;   /**< hay peso: el anillo de mover tiene datos */
    uint8_t met_bits;  /**< bit 0 mover, 1 ejercicio, 2 pasos */
} act_rings_t;

void act_rings(const act_config_t *c, uint32_t kcal, bool kcal_valid, uint32_t ex_s, uint32_t steps,
               act_rings_t *out);

#ifdef __cplusplus
}
#endif
