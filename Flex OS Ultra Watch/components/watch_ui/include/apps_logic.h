/*
 * apps_logic — lógica PURA de las apps de Flex OS Ultra en el reloj (sin
 * LVGL, se prueba en el PC con tests/host).
 *
 * Calculadora: transcripción de calcKey(), calcLine(), calcCompute() y
 * calcFmt() de FlexOS_Ultra/FlexOS_Ultra_AppsBasic.h:30-151 (rama
 * claude/flexos-ota-system-blq9g9-2csacx @ 5dbc949). Mismo estado (acumulador
 * + operador pendiente + entrada en curso), mismas teclas y mismos textos:
 * la línea muestra la OPERACIÓN tal como se escribe ("25 + 10"), una
 * división por cero da "Error" (no un 0 falso) y "2 + 3 + 4" da 9.
 *
 * Calendario: la rejilla del mes de calRender() (AppsBasic.h:351-402),
 * con la semana empezando en domingo como en la referencia ("D L M M J V S").
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Calculadora ---- */
typedef struct {
    char disp[24]; /**< entrada en curso o resultado (calcDisp) */
    double acc;    /**< acumulador (calcAcc) */
    char op;       /**< 0, '+', '-', 'x' o '/' (calcOp) */
    bool fresh;    /**< el próximo dígito empieza una entrada nueva (calcFresh) */
    bool err;      /**< el display muestra "Error" (calcErr) */
} calc_t;

/** Etiquetas de la rejilla 5x4 (CALC_LBL, AppsBasic.h:38). */
extern const char *const calc_labels[5][4];

void calc_reset(calc_t *c);
/**
 * Tecla: '0'-'9', '.', 'c' (C), '\b' (DEL), 'n' (+/-), '%', '+', '-', 'x',
 * '/', '='.
 */
void calc_key(calc_t *c, char k);
/** Tecla a partir de su etiqueta ("C", "+/-", "DEL", "7"...). 0 si no existe. */
char calc_key_from_label(const char *label);
/** Línea del display: "25", "25 +", "25 + 10" o "Error" (calcLine). */
void calc_line(const calc_t *c, char *out, size_t n);
/** ¿La tecla es de la columna de operadores o "=" (naranja de marca)? */
bool calc_label_is_op(int row, int col);

/* ---- Calendario ---- */
typedef struct {
    int year;
    int month;    /**< 1..12 */
    int first_wd; /**< día de la semana del día 1 (0 = domingo) */
    int dim;      /**< días del mes */
    int rows;     /**< filas de la rejilla (4..6) */
} cal_month_t;

/** Rejilla del mes. false si month no es 1..12. */
bool cal_month(int year, int month, cal_month_t *m);
/** Suma delta meses (puede ser negativo) a year/month. */
void cal_shift(int *year, int *month, int delta);
/** Fila (0..5) y columna (0..6) del día d en la rejilla. */
void cal_cell(const cal_month_t *m, int day, int *row, int *col);

/* ---- Device Care: salud del dispositivo (FlexOS_Ultra_DeviceCare.h:760-890) ---- */
typedef enum { DC_NA = 0, DC_OK, DC_WARN, DC_BAD } dc_level_t;
typedef struct {
    dc_level_t level;
    int pct; /**< 0..100, o -1 si la métrica no tiene porcentaje */
} dc_metric_t;

/**
 * Puntuación 0..100 (dcScoreFrom): media de las métricas medidas; una con
 * porcentaje vale su porcentaje (x0,70 en aviso, x0,35 en crítico), una sin
 * él vale 100 / 62 / 25. false si no se midió nada (no se muestra número).
 */
bool dc_score(const dc_metric_t *m, int n, int *out);
/** "Todo correcto" (>= 80), "Necesita atención" (>= 55) o "Estado crítico". */
const char *dc_score_word(int score);
/** Nivel del color de la puntuación: OK >= 80, WARN >= 55, BAD el resto. */
dc_level_t dc_score_level(int score);
/** RAM interna libre: < 40 KB crítico, < 64 KB aviso (FlexOS_Mem.h:117-118). */
dc_level_t dc_sram_level(unsigned long free_b);
/**
 * PSRAM (8 MB en el reloj): el bloque libre mayor tiene que admitir el fondo
 * de pantalla (402 KB): < 512 KB libres o bloque < 402 KB crítico; < 1 MB
 * libre o bloque < 1 MB aviso.
 */
dc_level_t dc_psram_level(unsigned long free_b, unsigned long largest_b);
/** Almacenamiento ocupado: >= 95 % crítico, >= 85 % aviso (DeviceCare.h:835). */
dc_level_t dc_storage_level(int used_pct);
/** n de tot servicios bien: todos OK, falta uno aviso, más crítico (DeviceCare.h:805). */
dc_level_t dc_count_level(int ok, int tot);

#ifdef __cplusplus
}
#endif
