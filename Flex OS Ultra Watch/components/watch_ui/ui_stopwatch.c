/*
 * ui_stopwatch — Reloj > Cronómetro con el diseño del usuario
 * (design/referencias/reloj_cronometro.png):
 *  - esfera oscura con 60 marcas alrededor y una aguja azul que da una vuelta
 *    por minuto; el tiempo "MM:SS.cc" dentro, arriba ("H:MM:SS.cc" desde 1 h);
 *  - debajo del centro, las dos vueltas más recientes (la mejor en verde y la
 *    peor en rojo desde 3 vueltas, como la tabla de la referencia AppChrono.h);
 *  - botones: izquierda "Parcial" (en marcha, marca una vuelta) / "Reiniciar"
 *    (en pausa), desactivado parado; derecha "Iniciar" / "Pausa" / "Seguir".
 * El estado vive en ui_clock_core: sigue contando fuera de la vista. Solo se
 * redibuja deprisa (30 Hz) en marcha, y entonces solo el texto y la aguja.
 */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "watch_theme.h"

#define SW_TICK_MS 33
#define DIAL_CX    205
#define DIAL_CY    251   /* en pantalla */
#define DIAL_R     111
#define TICK_R0    124
#define TICK_R1    136
#define HAND_R     132
#define BTN_W      100
#define BTN_H      61
#define BTN_Y      413
#define BTN_R      30
#define LAP_ROWS   2

typedef struct {
    lv_obj_t *view;
    lv_obj_t *time;
    lv_obj_t *hand;
    lv_point_precise_t hp[2];
    lv_obj_t *laps[LAP_ROWS];
    lv_obj_t *left, *right;
    lv_point_precise_t ticks[60][2];
    int shown_laps, shown_first;
    sw_state_t shown_st;
    int32_t shown_cs;
    int32_t hand_deg10;
} swp_t;

/* Coordenadas dentro de la vista (que empieza en y = UI_RELOJ_TOP). */
#define VY(y) ((y) - UI_RELOJ_TOP)

static void paint_time(swp_t *p, bool force)
{
    const int64_t us = sw_elapsed(ui_sw(), ui_mono_us());
    const int32_t cs = (int32_t)(us / 10000);
    if (force || cs != p->shown_cs) {
        p->shown_cs = cs;
        char buf[24];
        sw_format(us, true, buf, sizeof(buf));
        lv_label_set_text(p->time, buf);
    }
    /* Aguja: una vuelta por minuto, en décimas de grado; solo si cambia. */
    const int32_t d10 = (int32_t)((us / 1000) % 60000 * 3600 / 60000);
    if (force || d10 != p->hand_deg10) {
        p->hand_deg10 = d10;
        const float a = (float)d10 * 3.14159265f / 1800.0f;
        p->hp[0].x = DIAL_CX - sinf(a) * 12.0f;
        p->hp[0].y = VY(DIAL_CY) + cosf(a) * 12.0f;
        p->hp[1].x = DIAL_CX + sinf(a) * HAND_R;
        p->hp[1].y = VY(DIAL_CY) - cosf(a) * HAND_R;
        lv_line_set_points(p->hand, p->hp, 2);
    }
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
        lv_obj_t *l = p->laps[r];
        if (i < 0) {
            watch_theme_set_visible(l, false);
            continue;
        }
        char t[16], buf[40];
        sw_format(sw->laps[i].split_us, true, t, sizeof(t));
        snprintf(buf, sizeof(buf), "Parcial %d   %s", sw->first_no + i, t);
        lv_label_set_text(l, buf);
        watch_theme_set_text_color(l, (bw && i == best) ? FLEX_C_OK : (bw && i == worst) ? FLEX_C_ERR : FLEX_C_TXT2);
        watch_theme_set_visible(l, true);
    }
}

static void set_btn(lv_obj_t *b, const char *txt, bool enabled)
{
    lv_obj_t *l = lv_obj_get_child(b, -1);
    if (strcmp(lv_label_get_text(l), txt) != 0) {
        lv_label_set_text(l, txt);
    }
    if (enabled) {
        lv_obj_remove_state(b, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(b, LV_STATE_DISABLED);
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
        set_btn(p->left, "Parcial", true);
        set_btn(p->right, "Pausa", true);
        break;
    case SW_PAUSE:
        set_btn(p->left, "Reiniciar", true);
        set_btn(p->right, "Seguir", true);
        break;
    default:
        set_btn(p->left, "Parcial", false);
        set_btn(p->right, "Iniciar", true);
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

static lv_obj_t *button(lv_obj_t *v, int32_t x, const char *txt, bool primary, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = watch_theme_lg_button(v, FLEX_LG_DARK, x, VY(BTN_Y), BTN_W, BTN_H, BTN_R, FLEX_GLOW_BOTTOM);
    if (primary) {
        /* "Iniciar": morado con degradado, como en el diseño */
        lv_obj_set_style_bg_color(b, flex_color(FLEX_RD_START), 0);
        lv_obj_set_style_bg_grad_color(b, flex_color(FLEX_RD_START_BOT), 0);
        lv_obj_set_style_border_color(b, flex_color(0xC79BFF), 0);
        lv_obj_set_style_bg_color(b, flex_color(0x7B22C8), LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(b, flex_color(0x6A1DB0), LV_STATE_PRESSED);
    }
    lv_obj_t *l = watch_theme_label(b, FLEX_FONT_S24, FLEX_C_WHITE, txt);
    lv_obj_set_style_text_color(l, flex_color(FLEX_C_MUTE), LV_STATE_DISABLED);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

lv_obj_t *ui_stopwatch_view(lv_obj_t *page)
{
    swp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->shown_laps = -1;
    p->shown_st = (sw_state_t)-1;
    p->shown_cs = -1;
    p->hand_deg10 = -1;
    lv_obj_t *v = ui_reloj_view(page);
    p->view = v;

    /* Esfera: disco oscuro con un degradado suave */
    lv_obj_t *dial = lv_obj_create(v);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 2 * DIAL_R, 2 * DIAL_R);
    lv_obj_set_pos(dial, DIAL_CX - DIAL_R, VY(DIAL_CY) - DIAL_R);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, flex_color(FLEX_RD_DIAL), 0);
    lv_obj_set_style_bg_grad_color(dial, flex_color(FLEX_RD_DIAL_CENTER), 0);
    lv_obj_set_style_bg_grad_dir(dial, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dial, 1, 0);
    lv_obj_set_style_border_color(dial, flex_color(0x3A3D55), 0);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 60 marcas: cada 5, más largas y claras */
    for (int k = 0; k < 60; k++) {
        const float a = (float)k * 3.14159265f / 30.0f;
        const float r0 = (k % 5 == 0) ? TICK_R0 - 2 : TICK_R0 + 2;
        p->ticks[k][0].x = DIAL_CX + sinf(a) * r0;
        p->ticks[k][0].y = VY(DIAL_CY) - cosf(a) * r0;
        p->ticks[k][1].x = DIAL_CX + sinf(a) * TICK_R1;
        p->ticks[k][1].y = VY(DIAL_CY) - cosf(a) * TICK_R1;
        lv_obj_t *l = lv_line_create(v);
        lv_line_set_points(l, p->ticks[k], 2);
        lv_obj_set_style_line_width(l, k % 5 == 0 ? 3 : 2, 0);
        lv_obj_set_style_line_color(l, flex_color(FLEX_C_WHITE), 0);
        lv_obj_set_style_line_opa(l, k % 5 == 0 ? LV_OPA_60 : LV_OPA_20, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }

    p->time = watch_theme_label(v, FLEX_FONT_CAPS, FLEX_C_WHITE, "00:00.00");
    lv_obj_set_width(p->time, 2 * DIAL_R - 16);
    lv_obj_set_style_text_align(p->time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p->time, LV_ALIGN_TOP_MID, 0, VY(DIAL_CY) - 58);
    for (int r = 0; r < LAP_ROWS; r++) {
        p->laps[r] = watch_theme_label(v, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
        lv_obj_align(p->laps[r], LV_ALIGN_TOP_MID, 0, VY(DIAL_CY) + 26 + r * 26);
        lv_obj_add_flag(p->laps[r], LV_OBJ_FLAG_HIDDEN);
    }

    /* Aguja azul con su punto central (encima del texto, como en el diseño) */
    p->hand = lv_line_create(v);
    lv_obj_set_style_line_width(p->hand, 4, 0);
    lv_obj_set_style_line_color(p->hand, flex_color(FLEX_RD_HAND), 0);
    lv_obj_set_style_line_rounded(p->hand, true, 0);
    lv_obj_remove_flag(p->hand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *dot = lv_obj_create(v);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, flex_color(0x6060E0), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(dot, DIAL_CX - 6, VY(DIAL_CY) - 6);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    p->left = button(v, 75, "Parcial", false, left_cb, p);
    p->right = button(v, 235, "Iniciar", true, right_cb, p);

    paint(p, true);
    lv_obj_add_event_cb(v, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(v, tick_cb, SW_TICK_MS, p);
    return v;
}

lv_obj_t *ui_stopwatch_create(lv_obj_t *parent)
{
    return ui_reloj_page(parent, UI_RELOJ_CRONO);
}
