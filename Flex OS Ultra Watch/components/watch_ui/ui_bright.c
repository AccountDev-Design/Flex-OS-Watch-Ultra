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

    /* Deslizador del Flex Liquid Glass Engine: pista de vidrio; relleno blanco
     * en el panel rápido (como el brillo de un centro de control) y de acento
     * en la página Pantalla. */
    lv_obj_t *sl = flex_glass_slider(parent, x, y, w, h, light ? FLEX_C_WHITE : FLEX_C_ACCENT);
    lv_slider_set_range(sl, UI_BRIGHT_MIN, UI_BRIGHT_MAX);

    /* El sol queda sobre el relleno o sobre el vidrio según el valor: un gris
     * medio se lee sobre los dos. */
    lv_obj_t *sun = flex_icon_create(sl, FLEX_ICON_SUN, light ? 34 : 28, light ? 0x8E8E93 : FLEX_GLASS_TXT);
    lv_obj_align(sun, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_t *val = watch_theme_label(sl, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT, "");
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -20, 0);

    if (!bi->brightness_set || cur < 0) {
        lv_slider_set_value(sl, UI_BRIGHT_MIN, LV_ANIM_OFF);
        lv_obj_add_state(sl, LV_STATE_DISABLED);
        lv_label_set_text(val, "No disponible");
        lv_obj_set_style_text_color(val, flex_color(FLEX_GLASS_TXT_OFF), 0);
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
