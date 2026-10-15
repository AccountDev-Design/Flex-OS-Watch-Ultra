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
#include "flex_icons.h"
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

/**
 * Suelo de tinte del vidrio (gGlMinMix de la referencia): quien dibuja vidrio
 * sobre contenido que no controla (un aviso) lo sube para que el texto se lea.
 * La referencia usa 150 (Overlay.h:167) porque detrás hay un fondo
 * DESENFOCADO. Aquí no se desenfoca (coste), y con 150 la esfera se
 * transparenta a través del aviso (comprobado en la vista previa), así que
 * el suelo sube a 235.
 */
#define FLEX_GLASS_MIN_MIX_BANNER 235
void watch_theme_glass_set_min_tint(lv_obj_t *card, lv_opa_t min_mix);

/** Etiqueta con fuente y color de la paleta. */
lv_obj_t *watch_theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text);

/* ---- Componentes (F3.1) -------------------------------------------------
 * Todos los controles pulsables reaccionan al toque mientras el dedo está
 * encima (LV_STATE_PRESSED: el tinte se aclara) sin animaciones ni
 * desenfoque. LV_STATE_CHECKED = activo (acento). LV_STATE_DISABLED = sin
 * función todavía: se ve apagado y LVGL no le entrega toques. */

/**
 * Botón de vidrio: tarjeta de watch_theme_glass_card() pulsable. Quien la
 * crea escucha LV_EVENT_CLICKED.
 */
lv_obj_t *watch_theme_glass_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/** Fondo plano de página (FLEX_C_PAGE) para las pantallas de lista. */
void watch_theme_page(lv_obj_t *screen);

/**
 * Cabecera estándar de FLEX_HDR_H px: zona "volver" de FLEX_HDR_ZONE px con
 * el chevron centrado y el título a continuación, recortado con "..." antes
 * del borde. back_cb recibe LV_EVENT_CLICKED de la zona (NULL = sin zona).
 */
lv_obj_t *watch_theme_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb, void *user_data);

/** Lista vertical con desplazamiento bajo la cabecera (hasta el borde inferior). */
lv_obj_t *watch_theme_list(lv_obj_t *parent);

/**
 * Fila de lista de FLEX_ROW_H px: icono en color de acento, título, valor a
 * la derecha (NULL = sin valor) y chevron si abre otra pantalla. Pulsable:
 * quien la crea escucha LV_EVENT_CLICKED. Una fila sin función se marca con
 * watch_theme_row_set_pending().
 */
lv_obj_t *watch_theme_list_row(lv_obj_t *parent, flex_icon_t icon, uint32_t accent, const char *title,
                               const char *value, bool chevron);
void watch_theme_row_set_value(lv_obj_t *row, const char *value);
/** Marca la fila como no disponible: apagada, sin toque y con el valor "Pendiente". */
void watch_theme_row_set_pending(lv_obj_t *row);

#ifdef __cplusplus
}
#endif
