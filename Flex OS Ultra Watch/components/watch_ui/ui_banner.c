/*
 * ui_banner — aviso emergente de vidrio en lv_layer_top().
 *
 * Tiempos de la referencia (FlexOS_Ultra_Overlay.h): entrada 220 ms, visible
 * 4200 ms, salida 180 ms, curva de salida cuadrática. Un toque lo cierra. Se
 * crea al mostrarse y se borra al ocultarse (con él, sus animaciones). Si llega
 * otro aviso mientras se ve uno, se sustituye el contenido y se reinicia la
 * espera.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "flex_battery_icon.h"
#include "watch_theme.h"

#define B_X        16
#define B_Y        14
#define B_W        (FLEX_SCREEN_W - 2 * B_X)
#define B_H        88
#define B_RADIUS   22
#define T_IN_MS    220
#define T_SHOW_MS  4200
#define T_OUT_MS   180

static struct {
    lv_obj_t *card;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *text;
} s_b;

/* Funciones distintas para entrada y salida: lv_anim_start() sustituye una
 * animación existente con el mismo par (objeto, función). */
static void y_in_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void y_out_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void out_done_cb(lv_anim_t *a)
{
    lv_obj_t *card = a->var;
    if (card == s_b.card) {
        s_b.card = NULL;
    }
    lv_obj_delete_async(card);
}

static void start_out(uint32_t delay_ms)
{
    lv_anim_delete(s_b.card, y_out_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_b.card);
    lv_anim_set_exec_cb(&a, y_out_cb);
    lv_anim_set_values(&a, B_Y, -(B_H + 12));
    lv_anim_set_duration(&a, T_OUT_MS);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, out_done_cb);
    lv_anim_start(&a);
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_b.card) {
        lv_anim_delete(s_b.card, y_in_cb);
        start_out(0);
    }
}

static void build(void)
{
    watch_theme_init();
    lv_obj_t *card = watch_theme_glass_card(lv_layer_top(), B_X, -(B_H + 12), B_W, B_H);
    lv_obj_set_style_radius(card, B_RADIUS, 0);
    watch_theme_glass_set_min_tint(card, FLEX_GLASS_MIN_MIX_BANNER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tap_cb, LV_EVENT_CLICKED, NULL);
    /* la capa de brillo hereda el radio de la tarjeta */
    lv_obj_t *sheen = lv_obj_get_child(card, 0);
    if (sheen) {
        lv_obj_set_style_radius(sheen, B_RADIUS, 0);
    }

    s_b.icon = flex_battery_icon_create(card, 44, 22);
    lv_obj_align(s_b.icon, LV_ALIGN_LEFT_MID, 0, 0);
    s_b.title = watch_theme_label(card, FLEX_FONT_BODY, FLEX_C_TXT, "");
    lv_obj_align(s_b.title, LV_ALIGN_TOP_LEFT, 60, -4);
    s_b.text = watch_theme_label(card, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_align(s_b.text, LV_ALIGN_BOTTOM_LEFT, 60, 2);
    lv_label_set_long_mode(s_b.text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_b.text, B_W - 36 - 60);
    s_b.card = card;
}

void ui_banner_show_battery(batt_alert_t alert, int percent)
{
    if (alert == BATT_ALERT_NONE) {
        return;
    }
    const bool critical = (alert == BATT_ALERT_CRITICAL);
    if (!s_b.card) {
        build();
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_b.card);
        lv_anim_set_exec_cb(&a, y_in_cb);
        lv_anim_set_values(&a, -(B_H + 12), B_Y);
        lv_anim_set_duration(&a, T_IN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    flex_battery_icon_set(s_b.icon, critical ? FLEX_BATT_CRITICAL : FLEX_BATT_LOW, percent);
    lv_label_set_text(s_b.title, critical ? "Batería crítica" : "Batería baja");
    lv_obj_set_style_text_color(s_b.title, flex_color(critical ? FLEX_C_ERR : FLEX_C_WARN), 0);
    if (percent >= 0) {
        lv_label_set_text_fmt(s_b.text, "Queda %d %%  ·  Conecta el cargador", percent);
    } else {
        lv_label_set_text(s_b.text, "Conecta el cargador");
    }
    start_out(T_IN_MS + T_SHOW_MS);
}

bool ui_banner_active(void)
{
    return s_b.card != NULL;
}
