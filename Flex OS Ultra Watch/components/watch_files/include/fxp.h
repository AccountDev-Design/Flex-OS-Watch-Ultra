/*
 * fxp — dibujos de Paint en el formato FXP1 de Flex OS Ultra
 * (FlexOS_FS.h:265-337, FS.cpp:690-830), byte a byte:
 *
 *   cabecera (12 B, little-endian): 'F' 'X' 'P' '1' · u16 ancho · u16 alto ·
 *                                   u16 trazos · u16 reservado (0)
 *   trazo: u16 color RGB565 · u8 radio · u8 banderas (0) · u16 puntos ·
 *          puntos × (i16 x, i16 y) en coordenadas del lienzo original
 *
 * Trazos y no mapa de bits: un dibujo son unos KB y se redibuja a cualquier
 * escala (la miniatura es el mismo dibujo). Cada trazo se escribe al
 * levantar el dedo: un corte de batería solo pierde el trazo en curso.
 * Aquí se escribe campo a campo en little-endian (la referencia volcaba
 * structs; en ESP32 es lo mismo, y así no depende del compilador).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "files_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FXP_EXT      ".fxp"
#define FXP_HDR_SIZE 12
#define FXP_MAX_PTS  512 /* PAINT_MAX_PTS: puntos por trazo */

typedef struct {
    uint16_t w, h;
    uint16_t strokes;
} fxp_info_t;

/** Segmento ya escalado y desplazado; un punto suelto llega con x0==x1, y0==y1. */
typedef void (*fxp_seg_cb)(int x0, int y0, int x1, int y1, uint16_t color, int radius, void *user);

bool fxp_create(const char *path, uint16_t w, uint16_t h);
bool fxp_info(const char *path, fxp_info_t *out);
/** Añade un trazo al final y actualiza el contador. */
bool fxp_append(const char *path, uint16_t color, uint8_t radius, const int16_t *xy, uint16_t pts);
/** Recorre los trazos llamando a cb por segmento, con escala sc y origen (ox, oy). */
bool fxp_replay(const char *path, float sc, int ox, int oy, fxp_seg_cb cb, void *user);
/** Quita el último trazo (deshacer real, sobrevive al reinicio). */
bool fxp_undo(const char *path);
/** Deja el dibujo sin trazos, con su nombre y tamaño. */
bool fxp_clear(const char *path);
/** RGB565 de un color r, g, b (el que guarda el archivo). */
static inline uint16_t fxp_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#ifdef __cplusplus
}
#endif
