/*
 * ui_display_page — Ajustes > Pantalla: brillo real (el mismo control del
 * panel rápido) y datos del panel. El apagado automático depende del
 * servicio de energía (F4) y se marca "Pendiente".
 */
#include "ui_internal.h"

#include "watch_theme.h"

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

lv_obj_t *ui_display_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Pantalla", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Brillo");
    ui_bright_create(list, 0, 0, FLEX_SCREEN_W - 2 * FLEX_LIST_X);
    lv_obj_t *note = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       "Todavía no se guarda: al reiniciar vuelve al brillo inicial.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_style_pad_left(note, 6, 0);

    watch_theme_section(list, "Panel");
    watch_theme_info_row(list, "Pantalla", "AMOLED 410 × 502");
    watch_theme_info_row(list, "Fondo", "Onyx");
    lv_obj_t *off = watch_theme_list_row(list, FLEX_ICON_CLOCK, FLEX_C_CAT_SUN, "Apagado", NULL, false);
    watch_theme_row_set_pending(off);
    return page;
}
