/*
 * ui_display_page — Ajustes > Pantalla:
 *  - brillo real (el mismo control del panel rápido), guardado al soltar;
 *  - fondo: negro AMOLED (píxeles apagados) u Onyx. Se guarda y la esfera se
 *    rehace al momento;
 *  - el apagado automático llega con F4.5 y se marca "Pendiente".
 * Todo se guarda con watch_settings (F4.1). Si NVS no está disponible, la
 * nota lo dice: los cambios valen hasta reiniciar.
 */
#include "ui_internal.h"

#include "watch_settings.h"
#include "watch_theme.h"

static const char *wall_name(uint8_t w)
{
    return w == FLEX_WALL_ONYX ? "Onyx" : "Negro";
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void wall_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.wallpaper = ws.wallpaper == FLEX_WALL_ONYX ? FLEX_WALL_BLACK : FLEX_WALL_ONYX;
    watch_settings_update(&ws);
    watch_theme_set_wallpaper((flex_wall_t)ws.wallpaper);
    watch_theme_row_set_value(row, wall_name(ws.wallpaper));
    ui_nav_replace_root(ui_face_create); /* la esfera, debajo, con el fondo nuevo */
}

lv_obj_t *ui_display_page_create(lv_obj_t *parent)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Pantalla", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

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
    lv_obj_t *wall = watch_theme_list_row(list, FLEX_ICON_IMAGE, FLEX_C_CAT_SUN, "Fondo", wall_name(ws.wallpaper),
                                          false);
    lv_obj_add_event_cb(wall, wall_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wnote = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                        "Negro: los píxeles se apagan y gastan menos.");
    lv_label_set_long_mode(wnote, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(wnote, LV_PCT(100));
    lv_obj_set_style_pad_left(wnote, 16, 0);

    watch_theme_section(list, "Panel");
    watch_theme_info_row(list, "Pantalla", "AMOLED 410 × 502");
    lv_obj_t *off = watch_theme_list_row(list, FLEX_ICON_MOON, FLEX_C_CAT_SUN, "Apagado", NULL, false);
    watch_theme_row_set_pending(off);
    return page;
}
