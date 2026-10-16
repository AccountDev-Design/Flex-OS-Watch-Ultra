/*
 * ui_timer — Temporizador (F4.3):
 *  - parado: rodillos de minutos (0-99) y segundos, atajos de 1, 3, 5, 10 y
 *    15 min, e "Iniciar";
 *  - en marcha o en pausa: lo que queda en grande, barra de progreso y
 *    "Pausa"/"Seguir" y "Cancelar".
 * Cuenta por instante de fin (ui_clock_core): sigue fuera de la página y no
 * se desvía. Al llegar a 0, ui_clock_core muestra el aviso "Tiempo agotado"
 * esté donde esté el usuario.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_theme.h"

#define TMR_TICK_MS 200
#define BTN_W       150
#define BTN_H       60
#define CHIP_W      60
#define CHIP_H      44

static const uint8_t PRESETS[] = {1, 3, 5, 10, 15};

typedef struct {
    lv_obj_t *idle;   /* contenedor del modo parado */
    lv_obj_t *run;    /* contenedor en marcha / pausa */
    lv_obj_t *mm;
    lv_obj_t *ss;
    lv_obj_t *start;
    lv_obj_t *left;   /* lo que queda */
    lv_obj_t *bar;
    lv_obj_t *of;     /* "de 05:00" */
    lv_obj_t *pause;
    lv_obj_t *cancel;
    tmr_state_t shown;
} tp_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static lv_obj_t *box(lv_obj_t *page)
{
    lv_obj_t *b = lv_obj_create(page);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_set_pos(b, 0, FLEX_HDR_H);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void paint(tp_t *p)
{
    const tmr_t *t = ui_tmr();
    const int64_t now = ui_mono_us();
    const bool active = t->st == TMR_RUN || t->st == TMR_PAUSE;
    if (t->st != p->shown) {
        p->shown = t->st;
        if (active) {
            lv_obj_add_flag(p->idle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(p->run, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(p->idle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(p->run, LV_OBJ_FLAG_HIDDEN);
        }
        watch_theme_button_set(p->pause, t->st == TMR_PAUSE ? "Seguir" : "Pausa",
                               t->st == TMR_PAUSE ? FLEX_C_PRIMARY : FLEX_C_SURF2);
        char buf[24], d[16];
        tmr_format(t->dur_us, d, sizeof(d));
        snprintf(buf, sizeof(buf), "de %s", d);
        lv_label_set_text(p->of, buf);
    }
    if (active) {
        char buf[16];
        tmr_format(tmr_left(t, now), buf, sizeof(buf));
        if (strcmp(lv_label_get_text(p->left), buf) != 0) {
            lv_label_set_text(p->left, buf);
        }
        lv_bar_set_value(p->bar, tmr_permille(t, now), LV_ANIM_OFF);
    }
}

static void tick_cb(lv_timer_t *t)
{
    paint(lv_timer_get_user_data(t));
}

static void start_cb(lv_event_t *e)
{
    tp_t *p = lv_event_get_user_data(e);
    const int64_t us = ((int64_t)lv_roller_get_selected(p->mm) * 60 + lv_roller_get_selected(p->ss)) * 1000000;
    tmr_start(ui_tmr(), us, ui_mono_us());
    paint(p);
}

static void preset_cb(lv_event_t *e)
{
    tp_t *p = lv_obj_get_user_data(lv_event_get_current_target(e));
    const int min = (int)(intptr_t)lv_event_get_user_data(e);
    lv_roller_set_selected(p->mm, (uint32_t)min, LV_ANIM_ON);
    lv_roller_set_selected(p->ss, 0, LV_ANIM_ON);
}

static void pause_cb(lv_event_t *e)
{
    tp_t *p = lv_event_get_user_data(e);
    tmr_t *t = ui_tmr();
    if (t->st == TMR_RUN) {
        tmr_pause(t, ui_mono_us());
    } else {
        tmr_resume(t, ui_mono_us());
    }
    paint(p);
}

static void cancel_cb(lv_event_t *e)
{
    tp_t *p = lv_event_get_user_data(e);
    tmr_cancel(ui_tmr());
    paint(p);
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_timer_create(lv_obj_t *parent)
{
    tp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->shown = (tmr_state_t)-1;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Temporizador", back_cb, NULL);

    /* ---- Parado: elegir el tiempo ---- */
    p->idle = box(page);
    static char mins[100 * 3], secs[60 * 3];
    if (!mins[0]) {
        char *q = mins;
        for (int i = 0; i < 100; i++) {
            q += sprintf(q, i < 99 ? "%02d\n" : "%02d", i);
        }
        q = secs;
        for (int i = 0; i < 60; i++) {
            q += sprintf(q, i < 59 ? "%02d\n" : "%02d", i);
        }
    }
    p->mm = ui_roller(p->idle, mins, LV_ROLLER_MODE_NORMAL, FLEX_FONT_TITLE, 96);
    p->ss = ui_roller(p->idle, secs, LV_ROLLER_MODE_INFINITE, FLEX_FONT_TITLE, 96);
    lv_obj_align(p->mm, LV_ALIGN_TOP_MID, -62, 6);
    lv_obj_align(p->ss, LV_ALIGN_TOP_MID, 62, 6);
    lv_roller_set_selected(p->mm, 5, LV_ANIM_OFF);
    lv_obj_update_layout(p->idle);
    const int32_t hr = lv_obj_get_height(p->mm);
    lv_obj_t *colon = watch_theme_label(p->idle, FLEX_FONT_TITLE, FLEX_C_TXT, ":");
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, 6 + (hr - lv_font_get_line_height(FLEX_FONT_TITLE)) / 2);
    lv_obj_t *mlab = watch_theme_label(p->idle, FLEX_FONT_SMALL, FLEX_C_MUTE, "min");
    lv_obj_align_to(mlab, p->mm, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_t *slab = watch_theme_label(p->idle, FLEX_FONT_SMALL, FLEX_C_MUTE, "s");
    lv_obj_align_to(slab, p->ss, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    const int n = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
    const int32_t gap = 8, cw = n * CHIP_W + (n - 1) * gap;
    const int32_t cy = 6 + hr + 34;
    for (int i = 0; i < n; i++) {
        char t[8];
        snprintf(t, sizeof(t), "%d", PRESETS[i]);
        lv_obj_t *c = watch_theme_button(p->idle, t, FLEX_C_SURF, CHIP_W, CHIP_H);
        lv_obj_set_pos(c, (FLEX_SCREEN_W - cw) / 2 + i * (CHIP_W + gap), cy);
        lv_obj_set_user_data(c, p);
        lv_obj_add_event_cb(c, preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PRESETS[i]);
    }
    p->start = watch_theme_button(p->idle, "Iniciar", FLEX_C_PRIMARY, 200, BTN_H);
    lv_obj_align(p->start, LV_ALIGN_BOTTOM_MID, 0, -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(p->start, start_cb, LV_EVENT_CLICKED, p);

    /* ---- En marcha o en pausa ---- */
    p->run = box(page);
    p->left = watch_theme_label(p->run, FLEX_FONT_CLOCK, FLEX_C_TXT, "00:00");
    lv_obj_align(p->left, LV_ALIGN_TOP_MID, 0, 20);
    p->bar = lv_bar_create(p->run);
    lv_obj_remove_style_all(p->bar);
    lv_obj_set_size(p->bar, 300, 10);
    lv_obj_align(p->bar, LV_ALIGN_TOP_MID, 0, 20 + lv_font_get_line_height(FLEX_FONT_CLOCK) + 18);
    lv_bar_set_range(p->bar, 0, 1000);
    lv_obj_set_style_radius(p->bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p->bar, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(p->bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(p->bar, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(p->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    p->of = watch_theme_label(p->run, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_align_to(p->of, p->bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    p->pause = watch_theme_button(p->run, "Pausa", FLEX_C_SURF2, BTN_W, BTN_H);
    lv_obj_align(p->pause, LV_ALIGN_BOTTOM_MID, -(BTN_W / 2 + 8), -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(p->pause, pause_cb, LV_EVENT_CLICKED, p);
    p->cancel = watch_theme_button(p->run, "Cancelar", FLEX_C_DANGER, BTN_W, BTN_H);
    lv_obj_align(p->cancel, LV_ALIGN_BOTTOM_MID, BTN_W / 2 + 8, -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(p->cancel, cancel_cb, LV_EVENT_CLICKED, p);

    paint(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, TMR_TICK_MS, p);
    return page;
}
