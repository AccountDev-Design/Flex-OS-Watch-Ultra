/*
 * ui_display_page — Ajustes > Pantalla y fondo de pantalla:
 *  - esfera: abre el selector de esferas (el mismo que al mantener pulsada
 *    la esfera); al aplicar, vuelve a la esfera;
 *  - pantalla siempre activa (AOD), Pixel Shift y atenuación progresiva
 *    (desactivada, suave, media, fuerte): los mismos ajustes que Device Care;
 *  - brillo real (el mismo control del panel rápido), guardado al soltar;
 *  - fondo: abre el selector (negro AMOLED o uno de los 8 fondos de Flex OS
 *    Ultra, ui_wallpaper.c). Se guarda y la esfera se rehace al momento;
 *  - apagado por inactividad: 5, 10, 15, 30 o 60 s (cada toque pasa al
 *    siguiente). Se atenúa los últimos segundos; un toque, el botón lateral,
 *    una alarma o el USB la encienden (ui_screen.c, F4.5).
 * Todo se guarda con watch_settings (F4.1). Si NVS no está disponible, la
 * nota lo dice: los cambios valen hasta reiniciar.
 */
#include "ui_internal.h"

#include "watch_settings.h"
#include "watch_theme.h"

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void wall_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_wallpaper_create);
}

/* El nombre del fondo puede cambiar en el selector: se refresca al volver. */
static void wall_refresh_cb(lv_timer_t *t)
{
    lv_obj_t *row = lv_timer_get_user_data(t);
    watch_theme_row_set_value(row, watch_theme_wallpaper_name(watch_theme_get_wallpaper()));
}

static void face_cb(lv_event_t *e)
{
    (void)e;
    ui_face_editor_open(true);
}

/* Interruptores y ciclos de AOD, Pixel Shift y atenuación: cada toque cambia
 * el ajuste guardado; el valor de la fila es el estado real. */
static const char *onoff(bool on)
{
    return on ? "Activado" : "Desactivado";
}

static void aod_cb(lv_event_t *e)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.aod = !ws.aod;
    watch_settings_update(&ws);
    watch_theme_row_set_value(lv_event_get_current_target(e),
                              ui_psave_active() ? "No con ahorro" : onoff(ws.aod));
}

static void shift_cb(lv_event_t *e)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.pshift = !ws.pshift;
    watch_settings_update(&ws);
    watch_theme_row_set_value(lv_event_get_current_target(e), onoff(ws.pshift));
}

static void dim_cb(lv_event_t *e)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.dim = (uint8_t)((ws.dim + 1) % WS_DIM_COUNT);
    watch_settings_update(&ws);
    watch_theme_row_set_value(lv_event_get_current_target(e), ui_dim_text(ws.dim));
}

static void timeout_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    size_t i = 0;
    while (i < ws_screen_timeout_count && ws_screen_timeouts[i] != ws.screen_to_s) {
        i++;
    }
    ws.screen_to_s = ws_screen_timeouts[(i + 1) % ws_screen_timeout_count];
    watch_settings_update(&ws);
    char t[8];
    lv_snprintf(t, sizeof(t), "%u s", (unsigned)ws.screen_to_s);
    watch_theme_row_set_value(row, t);
}

lv_obj_t *ui_display_page_create(lv_obj_t *parent)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Pantalla", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Esfera");
    lv_obj_t *face = watch_theme_list_row(list, FLEX_ICON_WATCH, FLEX_C_CAT_SUN, "Esfera", ui_face_name(ws.face), true);
    lv_obj_add_event_cb(face, face_cb, LV_EVENT_CLICKED, NULL);

    watch_theme_section(list, "Brillo");
    ui_bright_create(list, 0, 0, FLEX_SCREEN_W - 2 * FLEX_LIST_X);
    lv_obj_t *note = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       watch_settings_persistent()
                                           ? "Se guarda al soltar el control."
                                           : "No se puede guardar (NVS no disponible): vale hasta reiniciar.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_style_pad_left(note, 16, 0);

    watch_theme_section(list, "Fondo de la esfera");
    lv_obj_t *wall = watch_theme_list_row(list, FLEX_ICON_IMAGE, FLEX_C_CAT_SUN, "Fondo",
                                          watch_theme_wallpaper_name((flex_wall_t)ws.wallpaper), true);
    lv_obj_add_event_cb(wall, wall_cb, LV_EVENT_CLICKED, NULL);
    ui_page_timer(page, wall_refresh_cb, 500, wall);
    lv_obj_t *wnote = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                        "Negro: los píxeles se apagan y gastan menos.");
    lv_label_set_long_mode(wnote, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(wnote, LV_PCT(100));
    lv_obj_set_style_pad_left(wnote, 16, 0);

    watch_theme_section(list, "Panel");
    watch_theme_info_row(list, "Pantalla", "AMOLED 410 × 502");
    char t[8];
    lv_snprintf(t, sizeof(t), "%u s", (unsigned)ws.screen_to_s);
    lv_obj_t *off = watch_theme_list_row(list, FLEX_ICON_MOON, FLEX_C_CAT_SUN, "Apagado", t, false);
    lv_obj_add_event_cb(off, timeout_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dim = watch_theme_list_row(list, FLEX_ICON_SUN, FLEX_C_CAT_SUN, "Atenuación", ui_dim_text(ws.dim), false);
    lv_obj_add_event_cb(dim, dim_cb, LV_EVENT_CLICKED, NULL);

    watch_theme_section(list, "Protección AMOLED");
    lv_obj_t *aod = watch_theme_list_row(list, FLEX_ICON_MOON, FLEX_C_CAT_BLUE, "AOD",
                                         ui_psave_active() ? "No con ahorro" : onoff(ws.aod), false);
    lv_obj_add_event_cb(aod, aod_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sh = watch_theme_list_row(list, FLEX_ICON_GRID, FLEX_C_CAT_BLUE, "Pixel Shift", onoff(ws.pshift), false);
    lv_obj_add_event_cb(sh, shift_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pn = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                     "Pixel Shift y el AOD atenuado reducen el riesgo de marcas en el AMOLED; no lo "
                                     "eliminan.");
    lv_label_set_long_mode(pn, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(pn, LV_PCT(100));
    lv_obj_set_style_pad_left(pn, 16, 0);
    return page;
}
