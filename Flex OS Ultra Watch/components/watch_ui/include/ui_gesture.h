/*
 * ui_gesture — reconocedor PURO de arrastres (sin LVGL ni ESP-IDF; se prueba
 * en el PC). Lo usan el gesto "volver" desde el borde izquierdo y la cortina
 * del panel rápido.
 *
 * Un arrastre tiene un eje (X o Y), un sentido (+1 o -1) y un recorrido
 * total (span). Mientras el dedo no se ha movido más que UI_DRAG_SLOP_PX no
 * se decide nada: así un toque sigue siendo un toque. Si el primer movimiento
 * claro va por el otro eje o en sentido contrario, el arrastre se rechaza y
 * no interfiere con el desplazamiento de las listas.
 *
 * Al soltar se completa si se ha recorrido al menos el 35 % o si el dedo iba
 * rápido en el sentido del gesto; si no, se cancela. La animación que lo
 * continúa dura lo que corresponde a la distancia que queda, con un mínimo
 * de 60 ms (ATR_MIN_MS de la referencia: nunca un salto instantáneo).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DRAG_SLOP_PX        12
#define UI_DRAG_COMMIT_PCT     35
#define UI_DRAG_FLING_PX_S     700   /* velocidad que completa aunque se haya recorrido poco */
#define UI_DRAG_FLING_MIN_PCT  8
#define UI_DRAG_BACKFLING_PX_S 300   /* hacia atrás con esta velocidad: cancela */
#define UI_ANIM_MIN_MS         60

/* Velocidad: entre la última muestra y la más reciente que tenga al menos
 * UI_VEL_WINDOW_MS de antigüedad, para no depender del ritmo del táctil. */
#define UI_VEL_SAMPLES   6
#define UI_VEL_WINDOW_MS 40

typedef enum { UI_AXIS_X = 0, UI_AXIS_Y } ui_axis_t;

typedef enum {
    UI_DRAG_IDLE = 0,
    UI_DRAG_PENDING,  /**< pulsado, todavía sin decidir */
    UI_DRAG_ACTIVE,   /**< arrastrando en el eje y sentido del gesto */
    UI_DRAG_REJECTED, /**< otro eje o sentido contrario: no es este gesto */
} ui_drag_state_t;

typedef enum {
    UI_DRAG_NONE = 0, /**< no llegó a ser un arrastre (toque o rechazado) */
    UI_DRAG_COMMIT,
    UI_DRAG_CANCEL,
} ui_drag_result_t;

typedef struct {
    ui_axis_t axis;
    int8_t dir;
    int32_t span;
    ui_drag_state_t state;
    int32_t x0, y0;     /* punto de la pulsación */
    int32_t travel;     /* recorrido en el sentido del gesto, 0..span */
    /* últimas muestras (posición en el sentido del gesto y tiempo) para la velocidad */
    int32_t vp[UI_VEL_SAMPLES];
    uint32_t vt[UI_VEL_SAMPLES];
    uint8_t vn;         /* muestras guardadas */
    uint8_t vhead;      /* índice de la más reciente */
} ui_drag_t;

void ui_drag_init(ui_drag_t *d, ui_axis_t axis, int8_t dir, int32_t span);
void ui_drag_press(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms);
ui_drag_state_t ui_drag_move(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms);
/** Velocidad en el sentido del gesto (px/s, negativa si vuelve). */
int32_t ui_drag_velocity(const ui_drag_t *d);
ui_drag_result_t ui_drag_release(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms);

/** Decisión al soltar, a partir del recorrido y la velocidad. */
bool ui_drag_should_commit(int32_t travel, int32_t span, int32_t velocity_px_s);

/** Duración de la animación que lleva de travel hasta el final (o hasta 0). */
uint32_t ui_drag_remaining_ms(int32_t travel, int32_t span, uint32_t full_ms, bool commit);

/** Curva de la referencia (appTrP): ease-out cúbico, 1 - (1 - u)^3, en milésimas. */
int32_t ui_ease_out_cubic_permille(int32_t u_permille);

/* ---- Gesto de Bienestar: desde el borde DERECHO de la esfera hacia la
 * izquierda. En una app ese borde es "atrás" (ui_nav); en la esfera estaba
 * libre. Además de lo de ui_drag (eje, sentido, umbral de movimiento):
 *  - tiene que empezar en la franja de UI_EDGE_W px junto al borde;
 *  - el dedo tiene que empezar a moverse pronto (un dedo apoyado en el borde
 *    que luego se arrastra despacio no es este gesto);
 *  - completa con UI_EDGE_COMMIT_PCT del ancho, o con un lanzamiento rápido
 *    que recorra al menos UI_EDGE_MIN_PCT y dure poco;
 *  - un arrastre en diagonal (mucho desvío vertical) no completa;
 *  - en modo estricto (entrenamiento en marcha: la muñeca se mueve y suda)
 *    hace falta medio ancho de recorrido, empezar antes y no vale el
 *    lanzamiento. */
#define UI_EDGE_W              24
#define UI_EDGE_START_MS       400
#define UI_EDGE_START_STRICT_MS 300
#define UI_EDGE_MIN_PCT        25
#define UI_EDGE_COMMIT_PCT     35
#define UI_EDGE_STRICT_PCT     50
#define UI_EDGE_FLING_MS       600
#define UI_EDGE_OFF_AXIS_PX    48

typedef struct {
    ui_drag_t drag;
    uint32_t t_press;
    int32_t max_off;   /* mayor desvío vertical */
    bool strict;
} ui_edge_swipe_t;

/** Pulsación: fuera de la franja del borde derecho el gesto queda rechazado. */
void ui_edge_swipe_press(ui_edge_swipe_t *g, int32_t x, int32_t y, uint32_t t_ms, int32_t screen_w, bool strict);
/** UI_DRAG_ACTIVE solo si el gesto es válido hasta ahora. */
ui_drag_state_t ui_edge_swipe_move(ui_edge_swipe_t *g, int32_t x, int32_t y, uint32_t t_ms);
/** Al soltar: true si se completa. */
bool ui_edge_swipe_release(ui_edge_swipe_t *g, int32_t x, int32_t y, uint32_t t_ms);

#ifdef __cplusplus
}
#endif
