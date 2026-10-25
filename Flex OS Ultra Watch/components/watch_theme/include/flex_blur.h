/*
 * flex_blur — desenfoque PURO (sin LVGL ni ESP-IDF; se prueba en el PC) de un
 * fondo RGB565 a pantalla completa, para el cajón de aplicaciones.
 *
 * Desenfocar 410x502 píxeles en cada cuadro no cabe en el ESP32-S3, así que se
 * hace UNA vez por fondo y por partes:
 *  1. reducción 4x4 -> 1 (promedio por canal) a una imagen pequeña de 8 bits
 *     por canal (103x126 para la pantalla del reloj);
 *  2. tres pasadas de caja horizontales y verticales sobre la pequeña (se
 *     aproximan a una gaussiana; radio 3 a 1/4 de escala ~ 12 px reales);
 *  3. ampliación bilineal al tamaño completo con oscurecimiento (los iconos y
 *     sus bordes claros se leen encima) y tramado ordenado, a RGB565.
 * Cada paso trabaja por bandas de filas: quien lo usa reparte el trabajo en
 * varios ciclos sin bloquear la interfaz (flex_blur_step).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_BLUR_DOWN   4    /* factor de reducción */
#define FLEX_BLUR_RADIUS 3    /* radio de la caja en la imagen reducida */
#define FLEX_BLUR_PASSES 3

typedef struct {
    const uint16_t *src; /* fondo de entrada (w x h RGB565, sin relleno de fila) */
    uint16_t *dst;       /* salida (w x h RGB565) */
    uint8_t *small;      /* sw * sh * 3 bytes (flex_blur_small_bytes) */
    uint8_t *tmp;        /* una fila o columna de trabajo: max(sw, sh) * 3 bytes */
    int w, h, sw, sh;
    uint8_t dim;         /* 0..255: brillo de salida (255 = igual) */
    int stage;           /* 0 reducir, 1 desenfocar, 2 ampliar, 3 hecho */
    int row;             /* siguiente fila de la etapa en curso */
} flex_blur_t;

/** Tamaño de la imagen reducida para w x h. */
void flex_blur_small_size(int w, int h, int *sw, int *sh);
size_t flex_blur_small_bytes(int w, int h);
size_t flex_blur_tmp_bytes(int w, int h);

/** Prepara el trabajo (no reserva memoria: small y tmp los da quien llama). */
void flex_blur_begin(flex_blur_t *b, const uint16_t *src, uint16_t *dst, int w, int h, uint8_t *small,
                     uint8_t *tmp, uint8_t dim);
/** Avanza hasta rows filas de salida (o su equivalente). true al terminar. */
bool flex_blur_step(flex_blur_t *b, int rows);
/** Todo de una vez (pruebas y herramientas). */
void flex_blur_run(flex_blur_t *b);

#ifdef __cplusplus
}
#endif
