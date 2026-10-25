/*
 * act_catalog — catálogo de entrenamientos de Bienestar (lógica PURA: sin
 * ESP-IDF ni LVGL; se prueba en el PC).
 *
 * La lista sigue los tipos de entrenamiento de Apple Watch (Workout y
 * HKWorkoutActivityType), agrupados en las categorías de Bienestar. Tener un
 * tipo NO significa poder medirlo todo: cada tipo declara lo que el QMI8658
 * de la muñeca puede aportar de verdad (act_type_t.flags):
 *  - ACT_F_STEPS: el detector de pasos sirve (caminar, correr...). Los pasos
 *    se muestran en la sesión.
 *  - ACT_F_DIST: además tiene sentido estimar la distancia con la zancada.
 *    Nunca es GPS.
 *  - ACT_F_AUTO: elegible para pausa, reanudación y finalización
 *    automáticas. Solo los de pasos: en ciclismo, yoga o fuerza la muñeca
 *    puede estar quieta mientras se entrena y una "pausa" sería falsa.
 *  - ACT_F_MOTION: se enseña el nivel de movimiento de la muñeca (medido).
 *  - ACT_F_DURATION: solo duración (y calorías estimadas por MET). Golf,
 *    pesca, vela, tiro con arco...: no se inventa rendimiento.
 *  - ACT_F_WATER: aviso de agua (el reloj no declara resistencia al agua).
 *
 * Los identificadores (act_type_t.id) se guardan en el historial: NUNCA se
 * reordenan ni se reutilizan; un tipo nuevo se añade al final con otro id.
 *
 * Calorías: MET aproximados del Compendio de Actividades Físicas (Ainsworth
 * y col., 2011, y su actualización de 2024) para un esfuerzo moderado. Los de
 * caminar y correr usan las ecuaciones del ACSM con la distancia estimada
 * (act_metrics.h). Sin frecuencia cardíaca todo es una estimación.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACT_CAT_WALKRUN = 0, /**< Caminar y correr */
    ACT_CAT_MACHINES,    /**< Senderismo, ciclismo, escaleras, elíptica y remo */
    ACT_CAT_STRENGTH,    /**< Fuerza, funcional, core, HIIT y cardio */
    ACT_CAT_MINDBODY,    /**< Yoga, pilates, baile y flexibilidad */
    ACT_CAT_BALL,        /**< Pelota y raqueta */
    ACT_CAT_COMBAT,      /**< Combate */
    ACT_CAT_WATER,       /**< Natación y agua */
    ACT_CAT_OUTDOOR,     /**< Invierno, montaña y exterior */
    ACT_CAT_OTHER,       /**< Otras */
    ACT_CAT_ADAPTED,     /**< Adaptadas */
    ACT_CAT_COUNT
} act_cat_t;

/* Iconos (ui_wb_icons.c los dibuja con primitivas de LVGL) */
typedef enum {
    ACT_IC_WALK = 0,
    ACT_IC_RUN,
    ACT_IC_HIKE,
    ACT_IC_BIKE,
    ACT_IC_STAIRS,
    ACT_IC_MACHINE,
    ACT_IC_STRENGTH,
    ACT_IC_HIIT,
    ACT_IC_YOGA,
    ACT_IC_DANCE,
    ACT_IC_STRETCH,
    ACT_IC_BALL,
    ACT_IC_RACKET,
    ACT_IC_COMBAT,
    ACT_IC_SWIM,
    ACT_IC_WATER,
    ACT_IC_SNOW,
    ACT_IC_MOUNTAIN,
    ACT_IC_TARGET,
    ACT_IC_STAR,
    ACT_IC_WHEEL,
    ACT_IC_COUNT
} act_icon_t;

#define ACT_F_STEPS    0x01u
#define ACT_F_DIST     0x02u
#define ACT_F_AUTO     0x04u
#define ACT_F_MOTION   0x08u
#define ACT_F_DURATION 0x10u
#define ACT_F_WATER    0x20u
#define ACT_F_RUN      0x40u /**< zancada de correr por defecto */

/** Modelo de calorías de un tipo. */
typedef enum {
    ACT_KCAL_MET = 0, /**< (MET - 1) x peso x horas activas */
    ACT_KCAL_STEPS,   /**< ACSM con la distancia de cada paso (caminar 0,1 / correr 0,2 ml/kg/m) */
} act_kcal_model_t;

typedef struct {
    uint8_t id;       /**< estable: se guarda en el historial */
    uint8_t cat;      /**< act_cat_t */
    uint8_t icon;     /**< act_icon_t */
    uint8_t flags;    /**< ACT_F_* */
    uint8_t met10;    /**< MET x 10 (Compendio, esfuerzo moderado) */
    uint8_t kcal;     /**< act_kcal_model_t */
    const char *name; /**< UTF-8 */
} act_type_t;

/** Tipo "Otro": el que se usa si un id guardado ya no existe. */
#define ACT_TYPE_OTHER 79
#define ACT_TYPE_WALK_OUT 1
#define ACT_TYPE_WALK_IN  2
#define ACT_TYPE_RUN_OUT  3
#define ACT_TYPE_RUN_IN   4

size_t act_catalog_count(void);
/** i-ésimo tipo (orden de presentación), o NULL. */
const act_type_t *act_catalog_at(size_t i);
/** Tipo con ese id; NULL si no existe. */
const act_type_t *act_catalog_find(uint8_t id);
/** Igual, pero nunca NULL (tipo "Otro" si no existe). */
const act_type_t *act_catalog_get(uint8_t id);
const char *act_cat_name(act_cat_t c);
act_icon_t act_cat_icon(act_cat_t c);
/** Tipos de una categoría, en orden; devuelve cuántos (hasta max). */
size_t act_catalog_in_cat(act_cat_t c, uint8_t *ids, size_t max);
/**
 * Búsqueda por nombre: sin distinguir mayúsculas ni tildes, por palabras que
 * EMPIEZAN por lo escrito ("cor" encuentra "Correr al aire libre" y "Core";
 * "aire" encuentra los de "al aire libre"). Devuelve cuántos (hasta max).
 */
size_t act_catalog_search(const char *query, uint8_t *ids, size_t max);
/** Pasa a minúsculas ASCII y quita tildes/diéresis/ñ de un UTF-8 (para buscar). */
void act_fold(const char *in, char *out, size_t n);

#ifdef __cplusplus
}
#endif
