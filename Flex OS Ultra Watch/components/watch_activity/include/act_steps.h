/*
 * act_steps — procesamiento de la señal del acelerómetro y detector de pasos
 * (lógica PURA: sin ESP-IDF; se prueba en el PC con señales sintéticas).
 *
 * Entrada: muestras del QMI8658 en mili-g con el contador de muestras del
 * propio sensor (desplegado a 32 bits) y el reloj monótono al leerlas.
 *
 * Cadena, por muestra (sin reservar memoria; todo el estado cabe en
 * act_steps_t):
 *  1. Validación: un contador repetido es la MISMA muestra (se descarta); un
 *     salto de más de ACT_MAX_GAP_SAMPLES o un reloj que no cuadra es una
 *     discontinuidad (los filtros y el tren de pasos empiezan de nuevo, sin
 *     inventar lo que faltó). Valores imposibles se cuentan y se ignoran.
 *  2. Tiempo: el intervalo sale del contador del sensor (sus periodos reales),
 *     no de cuánto tardó el bucle; la marca se ajusta suavemente al reloj
 *     monótono para seguir la deriva del oscilador del sensor.
 *  3. Módulo |a|: no depende de cómo esté orientado el reloj (los ejes del
 *     chip frente a la esfera aún no se han verificado, T61).
 *  4. Gravedad: media lenta del módulo (tau 1,5 s, con la entrada acotada
 *     para que un golpe no la arrastre); queda la parte dinámica.
 *  5. Paso bajo de 2.º orden (dos polos a 4 Hz, coeficiente según el dt real):
 *     deja la cadencia de caminar y correr (0,5-3,5 Hz) y quita golpes y
 *     vibraciones.
 *  6. Picos y valles con histéresis; la amplitud pico-valle tiene que superar
 *     un umbral que se adapta a la intensidad de los pasos ya aceptados.
 *     Además el valle tiene que bajar de la gravedad (un paso oscila a los
 *     dos lados; un golpe o un aplauso es un pico aislado).
 *  7. Intervalo entre picos de ACT min_int..max_int. Un pico demasiado pronto
 *     es parte del mismo paso (nunca cuenta dos veces).
 *  8. Confirmación por regularidad: un tren de pasos empieza a contar cuando
 *     hay `confirm` candidatos seguidos con intervalos regulares (dentro de
 *     ±tol_pct de su mediana); entonces se suman todos de golpe. Movimientos
 *     aislados de la muñeca, golpes y gestos sueltos no llegan a confirmarse.
 *  9. Clase de movimiento por ventanas de 1 s con histéresis (quieto, ligero,
 *     caminar, correr).
 *
 * Límites conocidos (documentados en docs/BIENESTAR.md): el reloj va en la
 * muñeca. Movimientos rítmicos del brazo sin caminar (cepillarse, aplaudir,
 * agitar el reloj a 2-3 Hz durante varios segundos) pueden contarse; empujar
 * un carro o llevar la mano en el bolsillo puede omitir pasos. La precisión se
 * mide en el reloj (T187-T192), no aquí.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Periodo nominal: código de ODR 7 del QMI8658 con los dos sensores, 56,05 Hz
 * (qmi8658.h). Solo se usa para pasar del contador del sensor a tiempo. */
#define ACT_SAMPLE_PERIOD_US 17841
/** Más muestras seguidas perdidas que esto (> 214 ms): discontinuidad. */
#define ACT_MAX_GAP_SAMPLES  12
/** Muestras de calentamiento de los filtros tras empezar o tras un hueco. */
#define ACT_WARMUP_SAMPLES   28
#define ACT_STEP_HIST        8

typedef struct {
    uint32_t index;  /**< contador de muestras del sensor (desplegado) */
    int64_t t_ms;    /**< reloj monótono cuando se leyó */
    int16_t a[3];    /**< mili-g */
    bool restart;    /**< primera muestra tras encender (o recuperar) el sensor */
} act_sample_t;

typedef struct {
    uint16_t min_p2v_mg; /**< amplitud pico-valle mínima (señal filtrada) */
    uint16_t max_p2v_mg; /**< por encima, fuera de un tren de pasos: golpe */
    uint16_t min_int_ms; /**< 250 ms: hasta 240 pasos por minuto */
    uint16_t max_int_ms; /**< 2000 ms: más lento no es caminar */
    uint8_t confirm;     /**< candidatos regulares para empezar a contar */
    uint8_t tol_pct;     /**< tolerancia de regularidad */
} act_step_params_t;

/** Sensibilidad del podómetro (Ajustes de Bienestar). */
enum { ACT_SENS_LOW = 0, ACT_SENS_NORMAL, ACT_SENS_HIGH, ACT_SENS_COUNT };
void act_step_params_default(act_step_params_t *p, int sensitivity);

typedef enum { ACT_MOT_STILL = 0, ACT_MOT_LIGHT, ACT_MOT_WALK, ACT_MOT_RUN } act_motion_t;

typedef struct {
    act_step_params_t p;
    /* tiempo */
    bool have;
    uint32_t last_index;
    int64_t last_tms;
    int64_t t_us;           /**< tiempo del detector (contador del sensor + ajuste al reloj) */
    /* filtros */
    float grav, lp1, lp2;
    uint16_t warm;
    /* picos y valles */
    bool seek_valley;
    float ext;
    int64_t ext_t;
    float valley;
    bool have_valley;
    float amp;              /**< amplitud típica de los pasos aceptados (mg) */
    /* candidatos */
    int64_t cand_t[ACT_STEP_HIST];
    float cand_a[ACT_STEP_HIST];
    uint8_t cand_n;
    bool have_cand;
    int64_t last_cand_t;
    /* tren de pasos */
    bool walking;
    int64_t bout_start_t;   /**< primer paso del tren actual (ms) */
    int64_t last_step_t;    /**< último paso contado (ms) */
    uint16_t ints[ACT_STEP_HIST];
    uint8_t ints_n, ints_i;
    uint16_t med_int;       /**< mediana de los intervalos recientes (ms) */
    /* ventana de 1 s */
    float w_e;
    uint16_t w_n;
    int64_t w_t0;
    uint16_t rms;           /**< mg, señal filtrada, última ventana */
    uint8_t pend_cls, pend_n;
    act_motion_t motion;
    uint16_t still_s;       /**< segundos seguidos quieto */
    /* salida */
    uint32_t steps;         /**< pasos confirmados (monótono) */
    uint32_t steps_run;     /**< de ellos, a ritmo de carrera */
    /* calidad de la señal (diagnóstico) */
    uint32_t samples, dups, gaps, invalid, clipped, ignored, impacts;
} act_steps_t;

void act_steps_init(act_steps_t *d, const act_step_params_t *p);
/** Cambia los parámetros sin perder los pasos contados. */
void act_steps_set_params(act_steps_t *d, const act_step_params_t *p);
void act_steps_feed(act_steps_t *d, const act_sample_t *s);
/** Se perdieron muestras (anillo lleno, sensor parado): empieza de nuevo. */
void act_steps_gap(act_steps_t *d);
/** Pasos por minuto del tren actual (0 si no se camina). */
uint16_t act_steps_cadence(const act_steps_t *d);
/** Tiempo actual del detector en ms (0 si aún no hay muestras). */
int64_t act_steps_now(const act_steps_t *d);

#ifdef __cplusplus
}
#endif
