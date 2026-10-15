/*
 * watch_theme — identidad visual de Flex OS Ultra en LVGL 9 para el reloj.
 *
 * "Liquid Glass" en B1 = tinte + brillo superior + sombra inferior + bordes
 * claro/oscuro, sobre el fondo Onyx ya generado. NO hay desenfoque: sobre un
 * fondo casi negro un desenfoque no cambiaría la imagen y costaría CPU. Cuando
 * existan fondos con detalle se añadirá el fondo pre-desenfocado una sola vez
 * (nunca desenfoque por cuadro). Ver docs/B1-ARRANQUE.md.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "flex_tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Outfit Regular (SIL OFL 1.1), generadas con tools/gen_fonts.sh */
LV_FONT_DECLARE(flex_outfit_18);
LV_FONT_DECLARE(flex_outfit_22);
LV_FONT_DECLARE(flex_outfit_28);
LV_FONT_DECLARE(flex_outfit_40);
LV_FONT_DECLARE(flex_outfit_clock_120); /* solo '-', '0'-'9' y ':' */

#define FLEX_FONT_SMALL  (&flex_outfit_18)
#define FLEX_FONT_BODY   (&flex_outfit_22)
#define FLEX_FONT_TITLE  (&flex_outfit_28)
#define FLEX_FONT_LARGE  (&flex_outfit_40)
#define FLEX_FONT_CLOCK  (&flex_outfit_clock_120)

static inline lv_color_t flex_color(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

/** Inicializa estilos. Llamar con LVGL bloqueado. */
void watch_theme_init(void);

/**
 * Crea el fondo Onyx a pantalla completa como imagen RGB565 en PSRAM
 * (generada una sola vez). Si no hay memoria, deja el fondo negro liso.
 * @return true si se generó la imagen.
 */
bool watch_theme_apply_background(lv_obj_t *screen);

/**
 * Crea una tarjeta de vidrio (rol CARD) en (x, y) de tamaño w x h, en
 * coordenadas de pantalla. El alfa del tinte se calcula con la mezcla
 * adaptativa de Flex OS Ultra a partir del fondo que queda debajo.
 */
lv_obj_t *watch_theme_glass_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/** Etiqueta con fuente y color de la paleta. */
lv_obj_t *watch_theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text);

#ifdef __cplusplus
}
#endif
