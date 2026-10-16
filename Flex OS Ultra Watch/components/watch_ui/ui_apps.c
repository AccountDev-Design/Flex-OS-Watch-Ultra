/*
 * ui_apps — lanzador de aplicaciones: se abre deslizando la esfera hacia la
 * izquierda (y se vuelve con el gesto desde el borde izquierdo o la
 * cabecera). Mosaicos de vidrio con el estado real de cada app: cronómetro
 * y temporizador (siguen contando fuera de su página), próxima alarma,
 * sensores y Ajustes.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_gesture.h"
#include "watch_settings.h"
#include "watch_theme.h"

#define APPS_TICK_MS 500
#define TILE_H       128
#define TILE_GAP     12

typedef struct {
    lv_obj_t *sw;
    lv_obj_t *tmr;
    lv_obj_t *al;
} apps_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

/* Mosaico: icono, nombre y estado (hijo 2). */
static lv_obj_t *tile(lv_obj_t *list, flex_icon_t icon, uint32_t accent, const char *name, ui_page_create_t page,
                      int32_t w)
{
    lv_obj_t *t = watch_theme_tile(list, 0, 0, w, TILE_H);
    lv_obj_t *ic = flex_icon_create(t, icon, 40, accent);
    lv_obj_set_pos(ic, 0, 0);
    lv_obj_t *n = watch_theme_label(t, FLEX_FONT_BODY, FLEX_C_TXT, name);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(n, LV_PCT(100), lv_font_get_line_height(FLEX_FONT_BODY));
    lv_obj_set_pos(n, 0, 46);
    lv_obj_t *s = watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s, LV_PCT(100), lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    if (page) {
        lv_obj_add_event_cb(t, open_cb, LV_EVENT_CLICKED, (void *)page);
    }
    return t;
}

static void set_status(lv_obj_t *t, const char *text)
{
    lv_obj_t *s = lv_obj_get_child(t, 2);
    if (strcmp(lv_label_get_text(s), text) != 0) {
        lv_label_set_text(s, text);
    }
}

static void refresh(apps_t *a)
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
    set_status(a->sw, buf);

    const tmr_t *tm = ui_tmr();
    tmr_format(tmr_left(tm, now), t, sizeof(t));
    if (tm->st == TMR_RUN) {
        snprintf(buf, sizeof(buf), "%s", t);
    } else if (tm->st == TMR_PAUSE) {
        snprintf(buf, sizeof(buf), "Pausa %s", t);
    } else {
        snprintf(buf, sizeof(buf), "Listo");
    }
    set_status(a->tmr, buf);

    int64_t loc;
    const int64_t next = ui_clock_local_now(&loc) ? alarms_next(ui_alarms(), loc, NULL) : -1;
    if (next >= 0) {
        watch_settings_t ws;
        watch_settings_get(&ws);
        alarm_when_text(next, loc, ws.h24, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", ui_alarms()->n ? "Ninguna activa" : "Ninguna");
    }
    set_status(a->al, buf);
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_apps_create(lv_obj_t *parent)
{
    apps_t *a = lv_malloc_zeroed(sizeof(*a));
    LV_ASSERT_MALLOC(a);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Aplicaciones", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(list, TILE_GAP, 0);
    lv_obj_set_style_pad_row(list, TILE_GAP, 0);
    const int32_t w = (FLEX_SCREEN_W - 2 * FLEX_LIST_X - TILE_GAP) / 2;

    a->sw = tile(list, FLEX_ICON_STOPWATCH, FLEX_C_CAT_GREEN, "Cronómetro", ui_stopwatch_create, w);
    a->tmr = tile(list, FLEX_ICON_HOURGLASS, FLEX_C_ACCENT, "Temporizador", ui_timer_create, w);
    a->al = tile(list, FLEX_ICON_BELL, FLEX_C_WARN, "Alarmas", ui_alarms_create, w);
    /* Sensores llega con el controlador del QMI8658 (F4.4). */
    lv_obj_t *sen = tile(list, FLEX_ICON_MOTION, FLEX_C_MUTE, "Sensores", NULL, w);
    lv_obj_add_state(sen, LV_STATE_DISABLED);
    set_status(sen, "Pendiente");
    lv_obj_t *set = tile(list, FLEX_ICON_GEAR, FLEX_C_CAT_BLUE, "Ajustes", ui_settings_create, w);
    set_status(set, "Brillo, hora, zona");

    refresh(a);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, a);
    ui_page_timer(page, tick_cb, APPS_TICK_MS, a);
    return page;
}

/* ---- Gesto en la esfera: deslizar hacia la izquierda abre el lanzador ---- */

static struct {
    ui_drag_t drag;
    bool opened;
} s_swipe;

static void face_swipe_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        ui_drag_init(&s_swipe.drag, UI_AXIS_X, -1, FLEX_SCREEN_W);
        ui_drag_press(&s_swipe.drag, p.x, p.y, now);
        s_swipe.opened = false;
    } else if (code == LV_EVENT_PRESSING) {
        /* En cuanto el gesto es claro se abre (sin esperar a soltar). */
        if (!s_swipe.opened && ui_drag_move(&s_swipe.drag, p.x, p.y, now) == UI_DRAG_ACTIVE &&
            ui_nav_depth() == 1 && !ui_qp_active()) {
            s_swipe.opened = ui_nav_push(ui_apps_create);
        }
    } else {
        s_swipe.drag.state = UI_DRAG_IDLE;
    }
}

void ui_apps_attach(lv_obj_t *face_page)
{
    lv_obj_add_flag(face_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESS_LOST, NULL);
}
