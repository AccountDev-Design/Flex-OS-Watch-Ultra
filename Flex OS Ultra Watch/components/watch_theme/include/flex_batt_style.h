/*
 * flex_batt_style — aspecto PURO del indicador de batería (sin LVGL; se prueba
 * en el PC). El tema no conoce el servicio de batería: watch_ui traduce el
 * estado del AXP2101 a uno de estos tipos de vista.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_BATT_UNKNOWN = 0, /**< sin datos o error de lectura: solo contorno */
    FLEX_BATT_NORMAL,      /**< descargando, nivel normal */
    FLEX_BATT_LOW,         /**< descargando, nivel bajo */
    FLEX_BATT_CRITICAL,    /**< descargando, nivel crítico */
    FLEX_BATT_CHARGING,    /**< carga activa confirmada: rayo */
    FLEX_BATT_FULL,        /**< el cargador indica carga terminada: marca de completado */
    FLEX_BATT_EXTERNAL,    /**< USB presente sin carga activa: rayo atenuado */
    FLEX_BATT_ABSENT,      /**< el PMIC no detecta batería: contorno tachado */
} flex_batt_kind_t;

typedef enum {
    FLEX_BATT_GLYPH_NONE = 0,
    FLEX_BATT_GLYPH_BOLT,
    FLEX_BATT_GLYPH_BOLT_DIM,
    FLEX_BATT_GLYPH_CHECK,
    FLEX_BATT_GLYPH_CROSS,
} flex_batt_glyph_t;

/** Color 0xRRGGBB del relleno (y del texto de estado) para un tipo de vista. */
uint32_t flex_batt_fill_rgb(flex_batt_kind_t kind);
/** Color del contorno. */
uint32_t flex_batt_outline_rgb(flex_batt_kind_t kind);
/** Glifo superpuesto. */
flex_batt_glyph_t flex_batt_glyph(flex_batt_kind_t kind);
/** Ancho del relleno en px dentro de un hueco de inner_w px; 0 = sin relleno. */
int flex_batt_fill_width(int inner_w, flex_batt_kind_t kind, int percent);

#ifdef __cplusplus
}
#endif
