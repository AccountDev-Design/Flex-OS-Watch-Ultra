/*
 * watch_theme — identidad visual de Flex OS Ultra en LVGL 9 para el reloj.
 *
 * Modo oscuro AMOLED: fondo negro puro (#000000) en la esfera, las páginas y
 * el panel rápido. "Liquid Glass" sobre negro = superficie con el tinte
 * adaptativo de la referencia PRECALCULADA y opaca, más el borde de luz
 * arriba/izquierda. Sin desenfoque, sin capas translúcidas a pantalla
 * completa y sin degradados: cada cuadro solo rellena colores sólidos y
 * dibuja texto (ver docs/B4-CALIDAD.md). Los 8 fondos de la referencia están
 * disponibles (watch_theme_set_wallpaper) y con ellos las tarjetas vuelven a
 * ser translúcidas.
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

/* Pantallas rediseñadas con los diseños del usuario (League Spartan Bold, la
 * letra de sus referencias; tools/gen_fonts.sh) */
LV_FONT_DECLARE(flex_spartan_16);
LV_FONT_DECLARE(flex_spartan_20);
LV_FONT_DECLARE(flex_spartan_22);
LV_FONT_DECLARE(flex_spartan_24);
LV_FONT_DECLARE(flex_spartan_32);
LV_FONT_DECLARE(flex_spartan_40);
LV_FONT_DECLARE(flex_spartan_clock_188); /* ' ', '-', '0'-'9' y ':' */
LV_FONT_DECLARE(flex_outfit_15);
#define FLEX_FONT_S16      (&flex_spartan_16)
#define FLEX_FONT_S20      (&flex_spartan_20)
#define FLEX_FONT_S22      (&flex_spartan_22)
#define FLEX_FONT_S24      (&flex_spartan_24)
#define FLEX_FONT_S32      (&flex_spartan_32)
#define FLEX_FONT_S40      (&flex_spartan_40)
#define FLEX_FONT_S_CLOCK  (&flex_spartan_clock_188)
#define FLEX_FONT_TINY     (&flex_outfit_15)  /* rótulos pequeños de los diseños */
/* Esferas fotográficas: dígitos (' ', '-', '0'-'9', ':') y rótulos en mayúsculas */
LV_FONT_DECLARE(flex_cond_156);
LV_FONT_DECLARE(flex_cond_196);
LV_FONT_DECLARE(flex_cond_248);
LV_FONT_DECLARE(flex_outfit_xl_105);
LV_FONT_DECLARE(flex_outfit_sb_101);
LV_FONT_DECLARE(flex_outfit_med_28);
LV_FONT_DECLARE(flex_outfit_med_34); /* ' '..'Z', ÁÉÍÓÚÑÜ, °: fechas, clima, cronómetro */
LV_FONT_DECLARE(flex_outfit_med_48);
#define FLEX_FONT_CAPS_S   (&flex_outfit_med_28)
#define FLEX_FONT_CAPS     (&flex_outfit_med_34)
#define FLEX_FONT_CAPS_L   (&flex_outfit_med_48)

static inline lv_color_t flex_color(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

/** Inicializa estilos. Llamar con LVGL bloqueado. */
void watch_theme_init(void);

/**
 * Fondo de la esfera. El valor se guarda en los ajustes (watch_settings):
 * no se reordena.
 *  - FLEX_WALL_BLACK (por defecto): negro puro AMOLED. Sin imagen: no se lee
 *    nada de la PSRAM al dibujar y los píxeles quedan apagados.
 *  - los 8 fondos de Flex OS Ultra (FlexOS_Ultra_Wallpaper.h), generados por
 *    flex_wallpaper en una sola imagen RGB565 de 402 KB en PSRAM (solo el
 *    fondo activo; se libera al volver al negro).
 */
typedef enum {
    FLEX_WALL_BLACK = 0,
    FLEX_WALL_ONYX,
    FLEX_WALL_FLEX_ORIGINAL,
    FLEX_WALL_AURORA,
    FLEX_WALL_NOCTURNO,
    FLEX_WALL_HALO,
    FLEX_WALL_OCEANO,
    FLEX_WALL_VIOLETA,
    FLEX_WALL_NATURALEZA,
    FLEX_WALL_COUNT_
} flex_wall_t;

/** Luma (Rec.601) a partir de la cual un fondo es "claro": onColor() de la
 * referencia (Wallpaper.h:468) pone texto oscuro por encima de 140. */
#define FLEX_WALL_LIGHT_LUMA 140
/** Texto sobre un fondo claro (onColor de la referencia: TC(16,18,26)). */
#define FLEX_C_ON_LIGHT      0x10121A
#define FLEX_C_ON_LIGHT2     0x3A3F4E

void watch_theme_set_wallpaper(flex_wall_t w);
flex_wall_t watch_theme_get_wallpaper(void);
/** Nombre del fondo ("Negro", "Onyx", "Aurora"...). */
const char *watch_theme_wallpaper_name(flex_wall_t w);
/** Miniatura del fondo w en buf (tw x th RGB565) para el selector. */
void watch_theme_wallpaper_thumb(flex_wall_t w, uint16_t *buf, int tw, int th);

/**
 * Pone el fondo actual en screen (color negro y, con un fondo, su imagen).
 * Las tarjetas de vidrio creadas después usan el modo correspondiente.
 * @return true si se puso una imagen de fondo.
 */
bool watch_theme_apply_background(lv_obj_t *screen);

/** ¿El fondo activo es claro bajo esa región? (false con el negro) */
bool watch_theme_wall_is_light(int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Fondos propios de las esferas fotográficas (diseños del usuario,
 * tools/faces/gen_faces.py): JPEG baseline en la flash (~25-55 KB cada uno)
 * que se decodifica en el MISMO búfer de 402 KB de PSRAM que el fondo de
 * pantalla: solo hay uno vivo, el de la esfera activa.
 */
typedef enum {
    FLEX_BG_NONE = 0,
    FLEX_BG_MONTANA,
    FLEX_BG_ANALOGICA,
    FLEX_BG_GATO,
    FLEX_BG_TIERRA,
    FLEX_BG_FLORES,
    FLEX_BG_TAJ,
    FLEX_BG_COUNT_
} flex_bg_t;

/** Pone en screen el fondo propio bg (negro si no se puede decodificar o no
 * hay PSRAM; se registra). true si se puso la imagen. */
bool watch_theme_apply_image(lv_obj_t *screen, flex_bg_t bg);
/** JPEG embebido de un fondo propio (pruebas y diagnóstico). */
const uint8_t *watch_theme_image_jpeg(flex_bg_t bg, uint32_t *len);

/**
 * Vistas previas (selector de esferas): entre begin() y end() los fondos que
 * piden las esferas temporales se generan en un segundo búfer y no pisan el
 * de la esfera activa. end() solo vuelve al búfer normal (el segundo se
 * conserva para la siguiente vista previa con el mismo fondo, sin volver a
 * decodificarlo); release() lo libera (al terminar las vistas previas, y
 * también watch_theme_release_caches). Usar begin/end en el mismo tick, justo
 * alrededor de la creación de la esfera temporal.
 */
void watch_theme_preview_begin(void);
void watch_theme_preview_end(void);
void watch_theme_preview_release(void);

/**
 * Fondo actual desenfocado para el cajón de aplicaciones (flex_blur): una
 * sola copia en PSRAM, generada por partes y reutilizada mientras el fondo de
 * la esfera no cambie.
 *  - watch_theme_blur_get(): la imagen lista o NULL (sin fondo o sin generar).
 *  - watch_theme_blur_step(rows): avanza la generación (rows filas de
 *    trabajo); true si ya está lista o si no hay nada que desenfocar.
 *  - watch_theme_has_background(): la esfera activa tiene imagen de fondo.
 */
const lv_image_dsc_t *watch_theme_blur_get(void);
bool watch_theme_blur_step(int rows);
/** Un cajón vivo muestra el desenfoque (on) o se borra (off): mientras haya
 * alguno, la imagen no se libera (ni con watch_theme_release_caches). */
void watch_theme_blur_hold(bool on);
bool watch_theme_has_background(void);
/** Suelta lo que se puede regenerar (desenfoque y caché de imágenes de
 * LVGL). Devuelve los bytes de PSRAM liberados por el tema. */
size_t watch_theme_release_caches(void);

/**
 * Crea una tarjeta de vidrio (rol CARD) en (x, y) de tamaño w x h, en
 * coordenadas de pantalla. El tinte es el de la mezcla adaptativa de Flex OS
 * Ultra: sobre negro (por defecto) se precalcula y la tarjeta es OPACA (sin
 * mezcla por píxel); sobre un fondo es translúcida y se calcula con la luma
 * real del fondo que queda debajo.
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
/** Suelo del tinte de una tarjeta con texto claro sobre un fondo claro. */
#define FLEX_GLASS_MIN_MIX_LIGHT  200
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

/**
 * Mosaico del panel rápido: vidrio plano (como las filas) con el radio de las
 * tarjetas grandes, pulsable y con los tres estados. Sin disposición: quien
 * lo crea coloca su contenido.
 */
lv_obj_t *watch_theme_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * Botón en forma de pastilla con el texto centrado (hijo 0). bg_rgb es su
 * color (FLEX_C_PRIMARY para la acción principal, FLEX_C_SURF2 para la
 * secundaria, FLEX_C_DANGER para detener). Pulsable: quien lo crea escucha
 * LV_EVENT_CLICKED. Con LV_STATE_DISABLED se atenúa y no responde.
 */
lv_obj_t *watch_theme_button(lv_obj_t *parent, const char *text, uint32_t bg_rgb, int32_t w, int32_t h);
/** Cambia texto y color (NULL / 0 = no cambiar). */
void watch_theme_button_set(lv_obj_t *btn, const char *text, uint32_t bg_rgb);

/** Fondo plano de página (FLEX_C_PAGE) para las pantallas de lista. */
void watch_theme_page(lv_obj_t *screen);

/**
 * Cabecera estándar de FLEX_HDR_H px: zona "volver" de FLEX_HDR_ZONE px con
 * el chevron centrado y el título a continuación, recortado con "..." antes
 * del borde. La zona y el título se apartan de la esquina redondeada
 * (flex_safe.h). back_cb recibe LV_EVENT_CLICKED de la zona (NULL = sin zona).
 */
lv_obj_t *watch_theme_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb, void *user_data);

/**
 * Lista vertical con desplazamiento bajo la cabecera. Termina
 * FLEX_LIST_BOTTOM px antes del borde inferior para que la esquina
 * redondeada no corte el contenido de las filas.
 */
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

/** Título de sección dentro de una lista (texto pequeño apagado). */
lv_obj_t *watch_theme_section(lv_obj_t *list, const char *text);

/**
 * Fila de solo información (FLEX_INFO_ROW_H px, no pulsable): título a la
 * izquierda y valor a la derecha. El valor se cambia con
 * watch_theme_info_set(), que no reescribe si no cambia.
 */
lv_obj_t *watch_theme_info_row(lv_obj_t *list, const char *title, const char *value);
void watch_theme_info_set(lv_obj_t *row, const char *value);
/* Cambios de color solo si cambian (F11): aplicar un estilo invalida y
 * redibuja el objeto aunque el valor sea el mismo; los refrescos periódicos
 * deben usar estas. */
void watch_theme_set_text_color(lv_obj_t *obj, uint32_t rgb);
void watch_theme_set_bg_color(lv_obj_t *obj, uint32_t rgb);
/** Muestra u oculta solo si cambia (quitar HIDDEN invalida aunque ya se vea). */
void watch_theme_set_visible(lv_obj_t *obj, bool visible);
/** Marca la fila como no disponible: apagada, sin toque y con el valor "Pendiente". */
void watch_theme_row_set_pending(lv_obj_t *row);

/* ---- Liquid Glass de los diseños del usuario (rediseño 2026-10) ----------
 * Componentes compartidos por las pantallas rediseñadas; sus valores están en
 * flex_tokens.h (FLEX_LG_*). Sin sombras ni desenfoque por cuadro: degradado,
 * borde de luz y un reflejo fino en el borde (lo que se ve en las referencias).
 *  - oscuro: cristal sobre negro (Reloj, alarmas, Almacenamiento);
 *  - claro: cristal blanco translúcido sobre el degradado del panel rápido.
 * Los pulsables reaccionan con LV_STATE_PRESSED; el claro, además, se rellena
 * del color activo con LV_STATE_CHECKED. */
typedef enum { FLEX_LG_DARK = 0, FLEX_LG_LIGHT } flex_lg_t;
typedef enum { FLEX_GLOW_NONE = 0, FLEX_GLOW_TOP, FLEX_GLOW_BOTTOM } flex_glow_t;
lv_obj_t *watch_theme_lg(lv_obj_t *parent, flex_lg_t kind, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t radius, flex_glow_t glow);
/** Igual, pulsable (quien lo crea escucha LV_EVENT_CLICKED). */
lv_obj_t *watch_theme_lg_button(lv_obj_t *parent, flex_lg_t kind, int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t radius, flex_glow_t glow);
/** Interruptor de las alarmas (verde activo / gris), de w x h. */
lv_obj_t *watch_theme_switch(lv_obj_t *parent, int32_t w, int32_t h);
/** Barra de progreso redondeada de los diseños (Almacenamiento). */
lv_obj_t *watch_theme_bar(lv_obj_t *parent, int32_t w, int32_t h, uint32_t fill_rgb, uint32_t track_rgb);

#ifdef __cplusplus
}
#endif
