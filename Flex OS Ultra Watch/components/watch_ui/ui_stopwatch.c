/*
 * ui_stopwatch — Cronómetro (F4.3), según la referencia (AppChrono.h):
 *  - minutos y segundos grandes (la fuente del reloj) y las centésimas
 *    debajo; desde 1 h, "H:MM:SS" en la fuente grande normal;
 *  - tabla con las 3 vueltas más recientes (vuelta, parcial, total), la mejor
 *    en verde y la peor en rojo (desde 3 vueltas, si no son iguales);
 *  - botones: izquierda "Vuelta" (en marcha) / "Reiniciar" (en pausa),
 *    desactivado si está parado; derecha "Iniciar" / "Pausa" / "Seguir".
 * El estado vive en ui_clock_core: sigue contando fuera de la página. La
 * página solo se redibuja deprisa (30 Hz, solo las centésimas) en marcha.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_theme.h"

#define SW_TICK_MS  33
#define LAP_ROWS    3
#define LAP_ROW_H   32
#define COL_W       108
#define BTN_W       150
#define BTN_H       60

typedef struct {
    lv_obj_t *mmss;
    lv_obj_t *cs;
    lv_obj_t *rows[LAP_ROWS][3];
    lv_obj_t *empty;
    lv_obj_t *left;
    lv_obj_t *right;
    int shown_laps;    /* vueltas pintadas (para no repintar la tabla cada cuadro) */
    int shown_first;
    sw_state_t shown_st;
    int64_t shown_s;   /* segundo pintado */
    bool big_hours;
} swp_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void paint_time(swp_t *p, bool force)
{
    const int64_t us = sw_elapsed(ui_sw(), ui_mono_us());
    const int64_t s = us / 1000000;
    char buf[24];
    snprintf(buf, sizeof(buf), ".%02d", (int)(us / 10000 % 100));
    lv_label_set_text(p->cs, buf);
    if (!force && s == p->shown_s) {
        return;
    }
    p->shown_s = s;
    const bool hours = s >= 3600;
    if (hours != p->big_hours) {
        p->big_hours = hours;
        lv_obj_set_style_text_font(p->mmss, hours ? FLEX_FONT_LARGE : FLEX_FONT_CLOCK, 0);
    }
    sw_format(us, false, buf, sizeof(buf));
    lv_label_set_text(p->mmss, buf);
}

static void paint_laps(swp_t *p)
{
    const sw_t *sw = ui_sw();
    if (sw->n == p->shown_laps && sw->first_no == p->shown_first) {
        return;
    }
    p->shown_laps = sw->n;
    p->shown_first = sw->first_no;
    int best = -1, worst = -1;
    const bool bw = sw_best_worst(sw, &best, &worst);
    for (int r = 0; r < LAP_ROWS; r++) {
        const int i = sw->n - 1 - r; /* la más reciente arriba */
        for (int c = 0; c < 3; c++) {
            lv_obj_t *l = p->rows[r][c];
            if (i < 0) {
                lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            char buf[24];
            if (c == 0) {
                snprintf(buf, sizeof(buf), "Vuelta %d", sw->first_no + i);
            } else {
                sw_format(c == 1 ? sw->laps[i].split_us : sw->laps[i].total_us, true, buf, sizeof(buf));
            }
            lv_label_set_text(l, buf);
            const uint32_t col = (bw && i == best) ? FLEX_C_OK : (bw && i == worst) ? FLEX_C_ERR : FLEX_C_TXT;
            lv_obj_set_style_text_color(l, flex_color(c == 0 ? FLEX_C_TXT2 : col), 0);
            lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (sw->n) {
        lv_obj_add_flag(p->empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(p->empty, LV_OBJ_FLAG_HIDDEN);
    }
}

static void paint_buttons(swp_t *p)
{
    const sw_state_t st = ui_sw()->st;
    if (st == p->shown_st) {
        return;
    }
    p->shown_st = st;
    switch (st) {
    case SW_RUN:
        watch_theme_button_set(p->left, "Vuelta", 0);
        lv_obj_remove_state(p->left, LV_STATE_DISABLED);
        watch_theme_button_set(p->right, "Pausa", FLEX_C_DANGER);
        break;
    case SW_PAUSE:
        watch_theme_button_set(p->left, "Reiniciar", 0);
        lv_obj_remove_state(p->left, LV_STATE_DISABLED);
        watch_theme_button_set(p->right, "Seguir", FLEX_C_PRIMARY);
        break;
    default:
        watch_theme_button_set(p->left, "Vuelta", 0);
        lv_obj_add_state(p->left, LV_STATE_DISABLED);
        watch_theme_button_set(p->right, "Iniciar", FLEX_C_PRIMARY);
        break;
    }
}

static void paint(swp_t *p, bool force)
{
    if (force || ui_sw()->st == SW_RUN) {
        paint_time(p, force);
    }
    paint_laps(p);
    paint_buttons(p);
}

static void tick_cb(lv_timer_t *t)
{
    paint(lv_timer_get_user_data(t), false);
}

static void left_cb(lv_event_t *e)
{
    swp_t *p = lv_event_get_user_data(e);
    sw_t *sw = ui_sw();
    if (sw->st == SW_RUN) {
        sw_lap(sw, ui_mono_us());
    } else if (sw->st == SW_PAUSE) {
        sw_reset(sw);
    }
    paint(p, true);
}

static void right_cb(lv_event_t *e)
{
    swp_t *p = lv_event_get_user_data(e);
    sw_t *sw = ui_sw();
    if (sw->st == SW_RUN) {
        sw_pause(sw, ui_mono_us());
    } else {
        sw_start(sw, ui_mono_us());
    }
    paint(p, true);
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_stopwatch_create(lv_obj_t *parent)
{
    swp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->shown_laps = -1;
    p->shown_st = (sw_state_t)-1;
    p->shown_s = -1;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Cronómetro", back_cb, NULL);

    /* Tiempo: MM:SS grande y las centésimas debajo (al lado no caben:
     * "00:00" ya ocupa ~350 px con la fuente del reloj). */
    p->mmss = watch_theme_label(page, FLEX_FONT_CLOCK, FLEX_C_TXT, "00:00");
    lv_obj_align(p->mmss, LV_ALIGN_TOP_MID, 0, FLEX_HDR_H + 2);
    p->cs = watch_theme_label(page, FLEX_FONT_LARGE, FLEX_C_TXT2, ".00");
    lv_obj_set_width(p->cs, 80); /* ancho fijo: el número no baila */
    lv_obj_set_style_text_align(p->cs, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p->cs, LV_ALIGN_TOP_MID, 0, FLEX_HDR_H + 2 + lv_font_get_line_height(FLEX_FONT_CLOCK) + 4);

    /* Vueltas */
    const int32_t y0 = FLEX_HDR_H + 2 + lv_font_get_line_height(FLEX_FONT_CLOCK) + 4 +
                       lv_font_get_line_height(FLEX_FONT_LARGE) - 4;
    const int32_t x0 = (FLEX_SCREEN_W - 3 * COL_W) / 2;
    static const char *const head[3] = {"N.º", "Parcial", "Total"};
    for (int c = 0; c < 3; c++) {
        lv_obj_t *h = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, head[c]);
        lv_obj_set_width(h, COL_W);
        lv_obj_set_style_text_align(h, c == 0 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(h, x0 + c * COL_W, y0);
    }
    for (int r = 0; r < LAP_ROWS; r++) {
        for (int c = 0; c < 3; c++) {
            lv_obj_t *l = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT, "");
            lv_obj_set_width(l, COL_W);
            lv_obj_set_style_text_align(l, c == 0 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_pos(l, x0 + c * COL_W, y0 + (r + 1) * LAP_ROW_H);
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            p->rows[r][c] = l;
        }
    }
    p->empty = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, "Sin vueltas");
    lv_obj_align(p->empty, LV_ALIGN_TOP_MID, 0, y0 + LAP_ROW_H);

    /* Botones */
    p->left = watch_theme_button(page, "Vuelta", FLEX_C_SURF2, BTN_W, BTN_H);
    lv_obj_align(p->left, LV_ALIGN_BOTTOM_MID, -(BTN_W / 2 + 8), -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(p->left, left_cb, LV_EVENT_CLICKED, p);
    p->right = watch_theme_button(page, "Iniciar", FLEX_C_PRIMARY, BTN_W, BTN_H);
    lv_obj_align(p->right, LV_ALIGN_BOTTOM_MID, BTN_W / 2 + 8, -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(p->right, right_cb, LV_EVENT_CLICKED, p);

    paint(p, true);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, SW_TICK_MS, p);
    return page;
}
