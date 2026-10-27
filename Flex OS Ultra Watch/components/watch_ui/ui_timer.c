/*
 * ui_timer — Reloj > Temporizador con el diseño del usuario
 * (design/referencias/reloj_temporizador.png):
 *  - parado: tres rodillos (Horas 0-99, Minutos y Segundos 0-59) con el valor
 *    elegido en blanco y los vecinos apagados, separados por ":" y ".";
 *    atajos 00:10:00, 00:15:00 y 00:30:00 y "Iniciar" (desactivado con 0);
 *  - en marcha o en pausa (la referencia no lo dibuja; mismo lenguaje que el
 *    cronómetro): lo que queda dentro de la esfera oscura con un arco de
 *    progreso azul, "Pausa"/"Seguir" y "Cancelar".
 * Cuenta por instante de fin (ui_clock_core): sigue fuera de la app y no se
 * desvía. Al llegar a 0, ui_clock_core muestra el aviso "Tiempo agotado"
 * esté donde esté el usuario.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_theme.h"

#define TMR_TICK_MS 200
#define VY(y)       ((y) - UI_RELOJ_TOP)
#define COL_DX      85    /* entre columnas: centros en 120, 205 y 290 */
#define LABEL_Y     117
#define ROLLER_Y    136
#define ROLLER_W    78
#define CHIP_W      76
#define CHIP_DX     88    /* atajos centrados en 117, 205 y 293 */
#define CHIP_H      34
#define CHIP_Y      360
#define BTN_W       116
#define BTN_H       61
#define BTN_Y       413
#define RING_R      111
#define RING_CY     236

static const uint16_t PRESETS_MIN[] = {10, 15, 30};

typedef struct {
    lv_obj_t *idle, *run;
    lv_obj_t *roll[3];
    lv_obj_t *start;
    lv_obj_t *left, *arc, *of;
    lv_obj_t *pause, *cancel;
    tmr_state_t shown;
    char left_txt[16];
} tp_t;

static lv_obj_t *box(lv_obj_t *v)
{
    lv_obj_t *b = lv_obj_create(v);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, FLEX_SCREEN_W, FLEX_SCREEN_H - UI_RELOJ_TOP);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static int64_t chosen_us(const tp_t *p)
{
    return ((int64_t)lv_roller_get_selected(p->roll[0]) * 3600 + (int64_t)lv_roller_get_selected(p->roll[1]) * 60 +
            lv_roller_get_selected(p->roll[2])) *
           1000000;
}

static void paint_start(tp_t *p)
{
    if (chosen_us(p) > 0) {
        lv_obj_remove_state(p->start, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(p->start, LV_STATE_DISABLED);
    }
}

static void set_label(lv_obj_t *btn, const char *t)
{
    lv_obj_t *l = lv_obj_get_child(btn, -1);
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void paint(tp_t *p)
{
    const tmr_t *t = ui_tmr();
    const int64_t now = ui_mono_us();
    const bool active = t->st == TMR_RUN || t->st == TMR_PAUSE;
    if (t->st != p->shown) {
        p->shown = t->st;
        watch_theme_set_visible(p->idle, !active);
        watch_theme_set_visible(p->run, active);
        set_label(p->pause, t->st == TMR_PAUSE ? "Seguir" : "Pausa");
        char buf[24], d[16];
        tmr_format(t->dur_us, d, sizeof(d));
        snprintf(buf, sizeof(buf), "de %s", d);
        lv_label_set_text(p->of, buf);
        paint_start(p);
    }
    if (active) {
        char buf[16];
        tmr_format(tmr_left(t, now), buf, sizeof(buf));
        if (strcmp(p->left_txt, buf) != 0) {
            snprintf(p->left_txt, sizeof(p->left_txt), "%s", buf);
            lv_label_set_text(p->left, buf);
        }
        /* lo que queda: el arco se vacía en el sentido de las agujas */
        const int32_t v = 1000 - tmr_permille(t, now);
        if (lv_arc_get_value(p->arc) != v) {
            lv_arc_set_value(p->arc, v);
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    paint(lv_timer_get_user_data(t));
}

static void start_cb(lv_event_t *e)
{
    tp_t *p = lv_event_get_user_data(e);
    const int64_t us = chosen_us(p);
    if (us > 0) {
        tmr_start(ui_tmr(), us, ui_mono_us());
    }
    paint(p);
}

static void preset_cb(lv_event_t *e)
{
    tp_t *p = lv_obj_get_user_data(lv_event_get_current_target(e));
    const int min = (int)(intptr_t)lv_event_get_user_data(e);
    lv_roller_set_selected(p->roll[0], (uint32_t)(min / 60), LV_ANIM_ON);
    lv_roller_set_selected(p->roll[1], (uint32_t)(min % 60), LV_ANIM_ON);
    lv_roller_set_selected(p->roll[2], 0, LV_ANIM_ON);
    paint_start(p);
}

static void roller_cb(lv_event_t *e)
{
    paint_start(lv_event_get_user_data(e));
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

/* Rodillo del diseño: elegido en blanco, vecinos apagados, sin fondo. */
static lv_obj_t *roller(lv_obj_t *parent, const char *opts, lv_roller_mode_t mode)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_text_font(r, FLEX_FONT_CAPS_S, 0);
    lv_obj_set_style_text_color(r, flex_color(FLEX_RD_FADED), 0);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(r, 22, 0);
    lv_obj_set_style_anim_duration(r, 160, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, flex_color(FLEX_C_WHITE), LV_PART_SELECTED);
    lv_obj_set_style_text_font(r, FLEX_FONT_CAPS_S, LV_PART_SELECTED);
    lv_roller_set_options(r, opts, mode);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, ROLLER_W);
    return r;
}

/* Píldoras del motor: la acción principal (iniciar, seguir) con el tinte
 * morado del diseño; el resto, vidrio sin tinte. */
static lv_obj_t *button(lv_obj_t *parent, int32_t x, const char *txt, bool primary, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = flex_glass_button(parent, txt, primary ? FLEX_TINT_PURPLE : FLEX_GLASS_TINT_NONE, BTN_W, BTN_H);
    lv_obj_set_pos(b, x, VY(BTN_Y));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

lv_obj_t *ui_timer_view(lv_obj_t *page)
{
    tp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->shown = (tmr_state_t)-1;
    lv_obj_t *v = ui_reloj_view(page);

    /* ---- Parado: elegir el tiempo ---- */
    p->idle = box(v);
    static char hours[100 * 3], mins[60 * 3];
    if (!hours[0]) {
        char *q = hours;
        for (int i = 0; i < 100; i++) {
            q += sprintf(q, i < 99 ? "%02d\n" : "%02d", i);
        }
        q = mins;
        for (int i = 0; i < 60; i++) {
            q += sprintf(q, i < 59 ? "%02d\n" : "%02d", i);
        }
    }
    static const char *const kLabels[3] = {"Horas", "Minutos", "Segundos"};
    for (int c = 0; c < 3; c++) {
        lv_obj_t *l = watch_theme_label(p->idle, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT2, kLabels[c]);
        lv_obj_align(l, LV_ALIGN_TOP_MID, (c - 1) * COL_DX, VY(LABEL_Y));
        p->roll[c] = roller(p->idle, c == 0 ? hours : mins, LV_ROLLER_MODE_INFINITE); /* 99 sobre 00, como en el diseño */
        lv_obj_align(p->roll[c], LV_ALIGN_TOP_MID, (c - 1) * COL_DX, VY(ROLLER_Y));
        lv_obj_add_event_cb(p->roll[c], roller_cb, LV_EVENT_VALUE_CHANGED, p);
    }
    lv_obj_update_layout(p->idle);
    const int32_t rh = lv_obj_get_height(p->roll[0]);
    const int32_t sep_y = VY(ROLLER_Y) + (rh - lv_font_get_line_height(FLEX_FONT_CAPS_S)) / 2;
    lv_obj_t *colon = watch_theme_label(p->idle, FLEX_FONT_CAPS_S, FLEX_C_WHITE, ":");
    lv_obj_align(colon, LV_ALIGN_TOP_MID, -COL_DX / 2, sep_y);
    lv_obj_t *dot = watch_theme_label(p->idle, FLEX_FONT_CAPS_S, FLEX_C_WHITE, ".");
    lv_obj_align(dot, LV_ALIGN_TOP_MID, COL_DX / 2, sep_y);

    const int n = (int)(sizeof(PRESETS_MIN) / sizeof(PRESETS_MIN[0]));
    for (int i = 0; i < n; i++) {
        char t[12];
        snprintf(t, sizeof(t), "%02d:%02d:00", PRESETS_MIN[i] / 60, PRESETS_MIN[i] % 60);
        const int32_t cx = FLEX_SCREEN_W / 2 + (i - 1) * CHIP_DX;
        lv_obj_t *c = flex_glass_panel(p->idle, FLEX_GLASS_CONTROL, cx - CHIP_W / 2, VY(CHIP_Y), CHIP_W, CHIP_H,
                                       LV_RADIUS_CIRCLE);
        flex_glass_interactive(c);
        lv_obj_t *l = watch_theme_label(c, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT, t);
        lv_obj_center(l);
        lv_obj_set_ext_click_area(c, 12); /* zona táctil mayor que el dibujo (FLEX_TOUCH_MIN) */
        lv_obj_set_user_data(c, p);
        lv_obj_add_event_cb(c, preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PRESETS_MIN[i]);
    }
    p->start = button(p->idle, (FLEX_SCREEN_W - BTN_W) / 2, "Iniciar", true, start_cb, p);

    /* ---- En marcha o en pausa ---- */
    p->run = box(v);
    p->arc = lv_arc_create(p->run);
    lv_obj_set_size(p->arc, 2 * RING_R, 2 * RING_R);
    lv_obj_set_pos(p->arc, FLEX_SCREEN_W / 2 - RING_R, VY(RING_CY) - RING_R);
    lv_arc_set_range(p->arc, 0, 1000);
    lv_arc_set_bg_angles(p->arc, 0, 360);
    lv_arc_set_rotation(p->arc, 270);
    lv_obj_remove_style(p->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(p->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(p->arc, flex_color(FLEX_RD_DIAL), 0);
    lv_obj_set_style_bg_opa(p->arc, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->arc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_arc_color(p->arc, flex_color(0x34374F), LV_PART_MAIN);
    lv_obj_set_style_arc_width(p->arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(p->arc, flex_color(FLEX_RD_HAND), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(p->arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(p->arc, true, LV_PART_INDICATOR);
    lv_arc_set_value(p->arc, 1000);
    p->left = watch_theme_label(p->run, FLEX_FONT_CAPS_L, FLEX_C_WHITE, "00:00");
    lv_obj_align(p->left, LV_ALIGN_TOP_MID, 0, VY(RING_CY) - 34);
    p->of = watch_theme_label(p->run, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_align(p->of, LV_ALIGN_TOP_MID, 0, VY(RING_CY) + 26);
    p->pause = button(p->run, 67, "Pausa", true, pause_cb, p);
    p->cancel = button(p->run, 227, "Cancelar", false, cancel_cb, p);

    paint(p);
    lv_obj_add_event_cb(v, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(v, tick_cb, TMR_TICK_MS, p);
    return v;
}

lv_obj_t *ui_timer_create(lv_obj_t *parent)
{
    return ui_reloj_page(parent, UI_RELOJ_TEMP);
}
