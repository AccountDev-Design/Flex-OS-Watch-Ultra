/*
 * ui_settings — página raíz de Ajustes: lista de categorías con la cabecera
 * estándar ("volver" a la esfera). Los valores de las filas (brillo, % de
 * batería) se refrescan cada segundo y solo se reescriben si cambian.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "ui_logic.h"
#include "watch_net.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define SETTINGS_TICK_MS 1000

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *display;
    lv_obj_t *time;
    lv_obj_t *battery;
} settings_t;

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

static void refresh(settings_t *s)
{
    char buf[24];
    const watch_ui_boot_info_t *bi = ui_boot_info();
    const int bright = bi->brightness_get ? bi->brightness_get() : -1;
    if (bright >= 0) {
        snprintf(buf, sizeof(buf), "Brillo %d %%", bright);
        watch_theme_row_set_value(s->display, buf);
    } else {
        watch_theme_row_set_value(s->display, NULL);
    }
    wt_datetime_t dt;
    if (watch_time_now(&dt) == WATCH_TIME_OK) {
        watch_settings_t ws;
        watch_settings_get(&ws);
        if (ws.h24) {
            wt_fmt_hhmm(buf, sizeof(buf), &dt);
        } else {
            char h[8];
            wt_fmt_hhmm12(h, sizeof(h), &dt);
            snprintf(buf, sizeof(buf), "%s %s", h, wt_ampm(&dt));
        }
        watch_theme_row_set_value(s->time, buf);
    } else {
        watch_theme_row_set_value(s->time, "Sin configurar");
    }
    watch_net_status_t ns;
    watch_net_get(&ns);
    watch_theme_row_set_value(s->wifi, ns.state == NET_CONNECTED ? ns.ssid : watch_net_state_text(&ns));
    watch_power_snapshot_t snap;
    if (ui_system_power(&snap) && snap.track.percent >= 0) {
        snprintf(buf, sizeof(buf), "%d %%", snap.track.percent);
        watch_theme_row_set_value(s->battery, buf);
    } else {
        watch_theme_row_set_value(s->battery, "--");
    }
}

/* El brillo puede cambiar en Pantalla o en el panel rápido. */
static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void power_cb(lv_obj_t *page, const watch_power_snapshot_t *snap)
{
    (void)snap;
    refresh(lv_obj_get_user_data(page));
}

static void delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    settings_t *s = lv_malloc_zeroed(sizeof(*s));
    LV_ASSERT_MALLOC(s);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Ajustes", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    s->wifi = row(list, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Wi-Fi", "", ui_wifi_create);
    s->display = row(list, FLEX_ICON_SUN, FLEX_C_CAT_SUN, "Pantalla", "", ui_display_page_create);
    s->time = row(list, FLEX_ICON_CLOCK, FLEX_C_CAT_BLUE, "Fecha y hora", "", ui_datetime_create);
    s->battery = row(list, FLEX_ICON_BOLT, FLEX_C_CAT_GREEN, "Batería", "", ui_battery_page_create);
    row(list, FLEX_ICON_PULSE, FLEX_C_CAT_BLUE, "Diagnóstico", NULL, ui_diag_create);
    row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_BLUE, "Almacenamiento", NULL, ui_storage_create);
    const watch_ui_boot_info_t *bi = ui_boot_info();
    row(list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Acerca de", bi->fw_version, ui_about_create);
    refresh(s);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, s);
    lv_obj_set_user_data(page, s);
    ui_power_listen(page, power_cb);
    ui_page_timer(page, tick_cb, SETTINGS_TICK_MS, s);
    return page;
}
