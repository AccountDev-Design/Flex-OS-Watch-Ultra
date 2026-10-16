/*
 * ui_reloj — app Reloj (IC_RELOJ de Flex OS Ultra).
 *
 * En la referencia, Reloj es una app con pestañas Hora / Cronómetro /
 * Temporizador (FlexOS_Ultra_AppFramework.h:600-630), y su temporizador es
 * todavía un rótulo "próximamente". En el reloj las tres funciones de tiempo
 * tienen motor propio (F4.3: cronómetro, temporizador y alarmas), así que la
 * app las agrupa en una lista con su estado en vivo: el cronómetro y el
 * temporizador siguen contando fuera de su página y la próxima alarma sale
 * aquí sin abrirla.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_settings.h"
#include "watch_theme.h"

#define RELOJ_TICK_MS 500

typedef struct {
    lv_obj_t *al;
    lv_obj_t *sw;
    lv_obj_t *tmr;
} reloj_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static lv_obj_t *row(lv_obj_t *list, flex_icon_t icon, uint32_t accent, const char *title, ui_page_create_t page)
{
    lv_obj_t *r = watch_theme_list_row(list, icon, accent, title, "", true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)page);
    return r;
}

static void refresh(reloj_t *a)
{
    char buf[32], t[16];
    const int64_t now = ui_mono_us();
    const sw_t *sw = ui_sw();
    sw_format(sw_elapsed(sw, now), false, t, sizeof(t));
    if (sw->st == SW_RUN) {
        snprintf(buf, sizeof(buf), "%s", t);
    } else if (sw->st == SW_PAUSE) {
        snprintf(buf, sizeof(buf), "Pausa %s", t);
    } else {
        snprintf(buf, sizeof(buf), "Parado");
    }
    watch_theme_row_set_value(a->sw, buf);

    const tmr_t *tm = ui_tmr();
    tmr_format(tmr_left(tm, now), t, sizeof(t));
    if (tm->st == TMR_RUN) {
        snprintf(buf, sizeof(buf), "%s", t);
    } else if (tm->st == TMR_PAUSE) {
        snprintf(buf, sizeof(buf), "Pausa %s", t);
    } else {
        snprintf(buf, sizeof(buf), "Listo");
    }
    watch_theme_row_set_value(a->tmr, buf);

    int64_t loc;
    const int64_t next = ui_clock_local_now(&loc) ? alarms_next(ui_alarms(), loc, NULL) : -1;
    if (next >= 0) {
        watch_settings_t ws;
        watch_settings_get(&ws);
        alarm_when_text(next, loc, ws.h24, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", ui_alarms()->n ? "Ninguna activa" : "Ninguna");
    }
    watch_theme_row_set_value(a->al, buf);
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_reloj_create(lv_obj_t *parent)
{
    reloj_t *a = lv_malloc_zeroed(sizeof(*a));
    LV_ASSERT_MALLOC(a);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Reloj", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    a->al = row(list, FLEX_ICON_BELL, FLEX_C_WARN, "Alarmas", ui_alarms_create);
    a->sw = row(list, FLEX_ICON_STOPWATCH, FLEX_C_CAT_GREEN, "Cronómetro", ui_stopwatch_create);
    a->tmr = row(list, FLEX_ICON_HOURGLASS, FLEX_C_ACCENT, "Temporizador", ui_timer_create);
    refresh(a);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, a);
    ui_page_timer(page, tick_cb, RELOJ_TICK_MS, a);
    return page;
}
