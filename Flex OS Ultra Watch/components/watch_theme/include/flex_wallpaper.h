/*
 * flex_wallpaper — generación PURA (sin LVGL) de los 8 fondos de pantalla de
 * Flex OS Ultra en RGB565 nativo, para cualquier resolución.
 *
 * Origen: FlexOS_Ultra/FlexOS_Ultra_Wallpaper.h:42-315 y la tabla de
 * degradado de FlexOS_Ultra_Gfx.h:834-846 (rama
 * claude/flexos-ota-system-blq9g9-2csacx @ 5dbc949). Los fondos de la
 * referencia son procedurales (degradados, discos y halos), así que se
 * reproducen ejecutando las MISMAS capas con los MISMOS colores, posiciones,
 * radios y alfas.
 *
 * Adaptación a 410x502 (la referencia está escrita para 480x800):
 *  - las posiciones se escalan en proporción a la pantalla (x * W/480,
 *    y * H/800), así toda la composición sigue a la vista;
 *  - los radios se escalan por W/480, así los círculos siguen siendo círculos;
 *  - los degradados abarcan la pantalla entera, como en la referencia.
 * Diferencias documentadas:
 *  - cada píxel se calcula en coma flotante y se cuantiza al final con un
 *    tramado ordenado 4x4: la referencia mezcla en RGB565 capa a capa y deja
 *    escalones en los degradados oscuros, que en un AMOLED se ven;
 *  - los bordes de los discos opacos llevan medio píxel de antialiasing.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fondos de la referencia, en el orden de WALL_NAME (Wallpaper.h:44). */
typedef enum {
    FLEX_WP_FLEX_ORIGINAL = 0,
    FLEX_WP_AURORA,
    FLEX_WP_NOCTURNO,
    FLEX_WP_HALO,
    FLEX_WP_ONYX,
    FLEX_WP_OCEANO,
    FLEX_WP_VIOLETA,
    FLEX_WP_NATURALEZA,
    FLEX_WP_COUNT_
} flex_wp_t;

/** Nombre original del fondo ("Flex Original", "Aurora"...). */
const char *flex_wallpaper_name(flex_wp_t id);

/**
 * Pinta el fondo id en buf (w x h píxeles RGB565 nativos, sin relleno de
 * fila). Las filas y0..y1 (inclusive) solamente: así se puede repartir el
 * trabajo o pintar una miniatura por bandas. id fuera de rango = Flex
 * Original (la ruta segura de drawWallpaperRowsId).
 */
void flex_wallpaper_render_rows(flex_wp_t id, uint16_t *buf, int w, int h, int y0, int y1);
void flex_wallpaper_render(flex_wp_t id, uint16_t *buf, int w, int h);

/** Compatibilidad: el fondo Onyx (el primero que tuvo el reloj). */
void flex_wallpaper_onyx(uint16_t *buf, int w, int h);

/**
 * Luma media Rec.601 (0..255) de la región x,y,w,h de un RGB565 de ancho
 * stride. Sirve para elegir texto claro u oscuro sobre el fondo, como
 * onColor() de la referencia (Wallpaper.h:468, umbral 140).
 */
uint8_t flex_wallpaper_region_luma(const uint16_t *buf, int stride, int h_total, int x, int y, int w, int h);

/** Empaqueta 8:8:8 en RGB565 nativo (lo que LVGL usa en LV_COLOR_FORMAT_RGB565). */
static inline uint16_t flex_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#ifdef __cplusplus
}
#endif
