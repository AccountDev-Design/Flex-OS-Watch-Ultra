/*
 * ui_bright — control de brillo compartido por el panel rápido y la página
 * Pantalla. Control real del panel (DCS 0x51, por watch_ui_boot_info_t),
 * mínimo 10 % y envío limitado mientras se arrastra (ui_logic.c). Al soltar,
 * el valor se guarda en los ajustes persistentes (watch_settings, F4.1).
 * Cada control guarda su estado y lo libera al borrarse.
 *
 * Sin control de brillo disponible se muestra "No disponible" y el control
 * queda desactivado: no se simula.
 */
#include "ui_internal.h"

#include "ui_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"

#define BRIGHT_H 64

typedef struct {
    lv_obj_t *val;
    int applied;
    uint32_t last_ms;
} bright_t;

static void apply(lv_obj_t *sl, bright_t *b, bool final)
{
    const watch_ui_boot_info_t *bi = ui_boot_info();
    const int v = ui_brightness_clamp(lv_slider_get_value(sl));
    const uint32_t now = lv_tick_get();
    lv_label_set_text_fmt(b->val, "%d %%", v);
    if (bi->brightness_set && ui_brightness_should_apply(v, b->applied, b->last_ms, now, final)) {
        if (bi->brightness_set(v)) {
            b->applied = v;
        }
        b->last_ms = now;
    }
    if (final && b->applied == v) {
        watch_settings_t ws;
        watch_settings_get(&ws);
        ws.brightness = (uint8_t)v;
        watch_settings_update(&ws); /* sin cambio real no escribe nada */
    }
}

static void event_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_current_target(e);
    bright_t *b = lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        lv_free(b);
        return;
    }
    apply(sl, b, code != LV_EVENT_VALUE_CHANGED);
}

static lv_obj_t *create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, bool light)
{
    const watch_ui_boot_info_t *bi = ui_boot_info();
    const int cur = bi->brightness_get ? bi->brightness_get() : -1;

    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_remove_style_all(sl);
    lv_obj_set_pos(sl, x, y);
    lv_obj_set_size(sl, w, h);
    if (light) {
        /* Panel rápido rediseñado: vidrio claro y relleno cian (design/referencias/panel_rapido.png) */
        lv_obj_set_style_bg_color(sl, flex_color(FLEX_C_WHITE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sl, FLEX_LG_LIGHT_FILL_OPA, LV_PART_MAIN);
        lv_obj_set_style_border_width(sl, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(sl, flex_color(FLEX_C_WHITE), LV_PART_MAIN);
        lv_obj_set_style_border_opa(sl, FLEX_LG_LIGHT_EDGE_OPA, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, flex_color(FLEX_QP_ACTIVE), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(sl, flex_color(FLEX_C_GLASS), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sl, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR);
    }
    lv_obj_set_style_radius(sl, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_40, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_slider_set_range(sl, UI_BRIGHT_MIN, UI_BRIGHT_MAX);

    lv_obj_t *sun = flex_icon_create(sl, FLEX_ICON_SUN, light ? 44 : 28, light ? FLEX_C_WHITE : FLEX_C_TXT);
    lv_obj_align(sun, LV_ALIGN_LEFT_MID, light ? 20 : 18, 0);
    lv_obj_t *val = watch_theme_label(sl, light ? FLEX_FONT_S20 : FLEX_FONT_BODY, light ? FLEX_C_WHITE : FLEX_C_TXT,
                                      "");
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -20, 0);

    if (!bi->brightness_set || cur < 0) {
        lv_slider_set_value(sl, UI_BRIGHT_MIN, LV_ANIM_OFF);
        lv_obj_add_state(sl, LV_STATE_DISABLED);
        lv_label_set_text(val, "No disponible");
        if (!light) {
            lv_obj_set_style_text_color(val, flex_color(FLEX_C_MUTE), 0);
        }
        return sl;
    }
    if (light) {
        lv_obj_add_flag(val, LV_OBJ_FLAG_HIDDEN); /* el diseño enseña solo la barra */
    }
    bright_t *b = lv_malloc_zeroed(sizeof(*b));
    LV_ASSERT_MALLOC(b);
    b->val = val;
    b->applied = cur;
    lv_slider_set_value(sl, ui_brightness_clamp(cur), LV_ANIM_OFF);
    lv_label_set_text_fmt(val, "%d %%", ui_brightness_clamp(cur));
    lv_obj_add_event_cb(sl, event_cb, LV_EVENT_VALUE_CHANGED, b);
    lv_obj_add_event_cb(sl, event_cb, LV_EVENT_RELEASED, b);
    lv_obj_add_event_cb(sl, event_cb, LV_EVENT_PRESS_LOST, b);
    lv_obj_add_event_cb(sl, event_cb, LV_EVENT_DELETE, b);
    return sl;
}

lv_obj_t *ui_bright_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    return create(parent, x, y, w, BRIGHT_H, false);
}

lv_obj_t *ui_bright_create_light(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return create(parent, x, y, w, h, true);
}
