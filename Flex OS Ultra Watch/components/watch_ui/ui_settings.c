/*
 * ui_settings — página raíz de Ajustes: lista de categorías con la cabecera
 * estándar ("volver" a la esfera).
 *
 * Solo las filas con función real se pueden tocar; el resto se marca
 * "Pendiente" y no recibe toques.
 */
#include "ui_internal.h"

#include "watch_theme.h"

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static lv_obj_t *row(lv_obj_t *list, flex_icon_t icon, uint32_t accent, const char *title, const char *value,
                     ui_page_create_t page)
{
    lv_obj_t *r = watch_theme_list_row(list, icon, accent, title, value, page != NULL);
    if (page) {
        lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)page);
    } else {
        watch_theme_row_set_pending(r);
    }
    return r;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Ajustes", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    row(list, FLEX_ICON_SUN, FLEX_C_CAT_SUN, "Pantalla", NULL, NULL);
    row(list, FLEX_ICON_BOLT, FLEX_C_CAT_GREEN, "Batería", NULL, NULL);
    row(list, FLEX_ICON_PULSE, FLEX_C_CAT_BLUE, "Diagnóstico", NULL, ui_diag_create);
    row(list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Acerca de", NULL, NULL);
    return page;
}
