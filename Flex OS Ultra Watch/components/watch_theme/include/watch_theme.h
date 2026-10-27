/*
 * watch_theme — identidad visual de Flex OS Ultra en LVGL 9 para el reloj:
 * fuentes, fondos (de pantalla y de las esferas), páginas, cabeceras y listas.
 *
 * Modo oscuro AMOLED: fondo negro puro (#000000) en la esfera y las páginas.
 * El vidrio (filas, tarjetas, botones, panel rápido, avisos...) NO se define
 * aquí: todo sale del Flex Liquid Glass Engine (flex_glass.h), que es la única
 * implementación del sistema.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "flex_glass.h"
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

/* League Spartan Bold: solo la usa una esfera (rótulo de 16 px); las esferas
 * no cambian. Las demás pantallas usan la escala FLEX_TYPE_* (Outfit). */
LV_FONT_DECLARE(flex_spartan_16);
LV_FONT_DECLARE(flex_outfit_15);
#define FLEX_FONT_S16      (&flex_spartan_16)
#define FLEX_FONT_TINY     (&flex_outfit_15)  /* rótulos pequeños */
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

/* ---- Escala tipográfica del sistema (Outfit, SIL OFL 1.1) ----------------
 * Pesos regulares para casi todo; Medium solo para títulos, botones y cifras
 * que piden énfasis. El grosor no sustituye al contraste ni a la jerarquía:
 * la jerarquía sale del tamaño, del color (principal / secundario) y del
 * espacio. Medidas para el AMOLED de 410x502 a ~315 ppi (1 mm ~ 12,4 px). */
LV_FONT_DECLARE(flex_outfit_m20);
LV_FONT_DECLARE(flex_outfit_m24);
LV_FONT_DECLARE(flex_outfit_m30);
#define FLEX_TYPE_FOOTNOTE (&flex_outfit_15)        /* Regular 15: notas al pie */
#define FLEX_TYPE_CAPTION  (&flex_outfit_18)        /* Regular 18: rótulos, valores secundarios */
#define FLEX_TYPE_BODY     (&flex_outfit_22)        /* Regular 22: texto */
#define FLEX_TYPE_LABEL    (&flex_outfit_m20)       /* Medium 20: botones, pestañas */
#define FLEX_TYPE_HEADLINE (&flex_outfit_m24)       /* Medium 24: título de tarjeta, valor destacado */
#define FLEX_TYPE_TITLE    (&flex_outfit_m30)       /* Medium 30: título de pantalla */
#define FLEX_TYPE_NUMBER   (&flex_outfit_40)        /* Regular 40: cifras grandes */
#define FLEX_TYPE_DISPLAY  (&flex_outfit_clock_120) /* Regular 120: la hora (solo '-', '0'-'9' y ':') */

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

/** La esfera activa tiene imagen de fondo. */
bool watch_theme_has_background(void);
/** Píxeles (410x502 RGB565) del fondo con el que se construye ahora (la
 * esfera activa o una vista previa), o NULL con el negro. Solo para la placa
 * congelada de las esferas (flex_glass_face_plate). */
const uint16_t *watch_theme_bg_pixels(void);
/** Suelta lo que se puede regenerar (fondo de la última vista previa del
 * selector y caché de imágenes de LVGL). Devuelve los bytes de PSRAM
 * liberados por el tema. */
size_t watch_theme_release_caches(void);

/** Etiqueta con fuente y color de la paleta. */
lv_obj_t *watch_theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text);

/* ---- Componentes de página (F3.1) -------------------------------------
 * Las filas pulsables usan el material ROW del motor (flex_glass): se
 * iluminan mientras el dedo está encima (LV_STATE_PRESSED) y se apagan con
 * LV_STATE_DISABLED (LVGL no les entrega toques). Los botones, tarjetas y
 * controles son de flex_glass.h. */

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

/** Barra de progreso en píldora (datos: no es vidrio). Rango 0..1000. */
lv_obj_t *watch_theme_bar(lv_obj_t *parent, int32_t w, int32_t h, uint32_t fill_rgb, uint32_t track_rgb);

#ifdef __cplusplus
}
#endif
