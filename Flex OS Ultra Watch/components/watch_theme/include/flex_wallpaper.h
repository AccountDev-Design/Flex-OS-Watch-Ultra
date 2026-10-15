/*
 * flex_wallpaper — generación PURA (sin LVGL) del fondo "Onyx" de Flex OS
 * Ultra en RGB565 nativo, para cualquier resolución.
 *
 * Reproduce wallOnyx() de la referencia: negro + halo radial #1E40B4 con
 * alfa 46 hasta r_in y caída lineal hasta 0 en r_out, centrado abajo.
 * Diferencia documentada: se añade un tramado ordenado 4x4 para que el halo,
 * muy oscuro en RGB565 (azul máximo 4/31), no muestre escalones en el AMOLED.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void flex_wallpaper_onyx(uint16_t *buf, int w, int h);

/** Empaqueta 8:8:8 en RGB565 nativo (lo que LVGL usa en LV_COLOR_FORMAT_RGB565). */
static inline uint16_t flex_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#ifdef __cplusplus
}
#endif
