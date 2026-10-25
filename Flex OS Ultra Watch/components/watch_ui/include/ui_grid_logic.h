/*
 * ui_grid_logic — geometría y física PURAS del cajón de aplicaciones en panal
 * con efecto lupa ("fisheye") y desplazamiento infinito (sin LVGL ni ESP-IDF;
 * se prueba en el PC con tests/host/test_grid.c).
 *
 * Rejilla: hexagonal de filas horizontales (paso GRID_PITCH entre centros,
 * filas a GRID_PITCH·sin 60°), con coordenadas axiales (q, r):
 *     x = PITCH · (q + r / 2),   y = PITCH · sin60 · r
 * Espacio infinito sin infinitos iconos: la rejilla es periódica de periodo 4
 * en q y en r (un toro de 16 huecos). Cada celda (q, r) muestra el hueco
 * (q mod 4) + 4 · (r mod 4), así que una app NUNCA cambia de identidad al
 * desplazarse y se repite cada 4·PITCH en las seis direcciones (es el menor
 * periodo de esa subred: 4·PITCH exactos). Solo se dibujan las celdas a
 * menos de GRID_VIS_R del centro de la vista (en la rejilla, antes de la
 * lupa) y GRID_VIS_R < 2·PITCH: dos copias de una misma app están siempre a
 * 4·PITCH o más, así que nunca se ven a la vez (sin duplicados en pantalla).
 *
 * Lupa: el tamaño de cada icono depende de su distancia al punto de
 * interacción L (el dedo mientras se toca; el centro de la pantalla en
 * reposo): coseno suave de 1 en L a GRID_MIN_SCALE a GRID_LENS_R. Los iconos
 * cercanos a L se apartan un poco hacia fuera (GRID_PUSH_PX) para dejar sitio
 * a la ampliación. Cerca del borde de la zona visible encogen y se
 * desvanecen. La posición LÓGICA de cada app es la de su celda: la lupa solo
 * cambia cómo se dibuja (la selección usa lo que se ve al pulsar).
 *
 * Física: un único paso por cuadro para toda la rejilla (sin una animación por
 * icono): desplazamiento 1:1 con el dedo, inercia al soltar con frenado
 * exponencial, y al frenarse un muelle críticamente amortiguado lleva la celda
 * más cercana al centro; el punto de la lupa sigue al dedo con otro muelle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_PITCH      108.0f
#define GRID_ROW        (GRID_PITCH * 0.8660254f)
#define GRID_TORUS      4
#define GRID_SLOTS      (GRID_TORUS * GRID_TORUS)
#define GRID_VIS_R      (1.95f * GRID_PITCH) /* < 2·PITCH: sin duplicados visibles */
#define GRID_FADE_R     (1.72f * GRID_PITCH) /* desde aquí encogen y se desvanecen */
#define GRID_LENS_R     (3.0f * GRID_PITCH)
#define GRID_MIN_SCALE  0.42f
#define GRID_PUSH_PX    12.0f
#define GRID_PUSH_R     (2.6f * GRID_PITCH)
#define GRID_MAX_ITEMS  20   /* celdas que pueden caer dentro de GRID_VIS_R */

/* Física (unidades: px, s) */
#define GRID_FRICTION_TAU 0.325f /* constante de frenado de la inercia */
#define GRID_SNAP_SPEED   140.0f /* por debajo, el muelle centra la celda más cercana */
#define GRID_SNAP_OMEGA   13.0f
#define GRID_LENS_OMEGA   26.0f
#define GRID_MAX_SPEED    4000.0f
#define GRID_VEL_SAMPLES  6
#define GRID_VEL_WINDOW_MS 50

typedef struct {
    float x, y;
} grid_vec_t;

typedef struct {
    int q, r;        /* celda (identidad estable del icono) */
    int slot;        /* hueco del toro (0..GRID_SLOTS-1) */
    float x, y;      /* centro en pantalla, ya con la lupa */
    float scale;     /* 0..1 sobre el tamaño nativo del icono */
    uint8_t opa;     /* 0..255 */
} grid_item_t;

/** Hueco del toro de la celda (q, r). */
int grid_slot(int q, int r);
/** Centro de la celda en el plano de la rejilla (px, con la celda (0,0) en el origen). */
grid_vec_t grid_cell_pos(int q, int r);
/** Celda cuyo centro está más cerca del punto (x, y) del plano. */
void grid_nearest_cell(float x, float y, int *q, int *r);
/** Copia de la celda del hueco slot más cercana al punto (x, y) del plano. */
void grid_nearest_copy(int slot, float x, float y, int *q, int *r);
/** Factor de la lupa (GRID_MIN_SCALE..1) a una distancia d de su punto. */
float grid_lens_scale(float d);
/** Visibilidad (0..1) a una distancia d del centro de la vista, en la rejilla. */
float grid_visibility(float d);

/**
 * Celdas visibles. off: desplazamiento de la rejilla (la celda (0,0) se ve
 * en center + off antes de la lupa); lens: punto de la lupa en pantalla.
 * Devuelve cuántas se escribieron en out (como mucho max).
 */
int grid_layout(grid_vec_t off, grid_vec_t lens, grid_vec_t center, grid_item_t *out, int max);

/** Muelle críticamente amortiguado (semiimplícito), un paso de dt s. */
void grid_spring_step(grid_vec_t *pos, grid_vec_t *vel, grid_vec_t target, float omega, float dt);
/** Inercia: avanza pos con vel y frena vel (exponencial, GRID_FRICTION_TAU). */
void grid_inertia_step(grid_vec_t *pos, grid_vec_t *vel, float dt);
/** Desplazamiento que deja la celda más cercana al centro exactamente centrada. */
grid_vec_t grid_snap_target(grid_vec_t off);

/* Velocidad del dedo: muestras recientes y estimación robusta al ritmo del táctil. */
typedef struct {
    grid_vec_t p[GRID_VEL_SAMPLES];
    uint32_t t[GRID_VEL_SAMPLES];
    uint8_t n, head;
} grid_vel_t;
void grid_vel_reset(grid_vel_t *v);
void grid_vel_add(grid_vel_t *v, float x, float y, uint32_t t_ms);
/** px/s, limitada a GRID_MAX_SPEED; (0,0) si el dedo se detuvo antes de soltar. */
grid_vec_t grid_vel_get(const grid_vel_t *v, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
