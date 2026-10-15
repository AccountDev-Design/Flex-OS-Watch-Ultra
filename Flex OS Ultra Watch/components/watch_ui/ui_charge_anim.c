/*
 * ui_charge_anim — animación de inicio de carga.
 *
 * Solo se reproduce cuando ui_system la pide (inicio de carga CONFIRMADO, ver
 * ui_charge_gate_update). Es limitada: entrada 220 ms, anillo hasta el % real
 * en 900 ms, salida a los 2 s (unos 2,3 s en total), y un toque la cierra.
 * Todo vive en un contenedor de lv_layer_top() que se borra al terminar: las
 * animaciones están ligadas a objetos de ese contenedor, así que LVGL las
 * elimina con él y no queda nada vivo después.
 */
#include "ui_internal.h"

#include "flex_icons.h"
#include "watch_theme.h"

#define T_IN_MS      220
#define T_RING_MS    900
#define T_RING_DELAY 120
#define T_HOLD_MS    2000
#define T_OUT_MS     250
#define T_TAP_OUT_MS 180
#define RING_SIZE    232
#define RING_W       12

static lv_obj_t *s_root;

/* Dos funciones distintas para la opacidad: lv_anim_start() sustituye una
 * animación existente con el mismo par (objeto, función), y la salida
 * programada no debe anular la entrada. */
static void opa_in_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void opa_out_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void arc_cb(void *var, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)var, v);
}

static void done_cb(lv_anim_t *a)
{
    lv_obj_t *root = a->var;
    if (root == s_root) {
        s_root = NULL;
    }
    lv_obj_delete_async(root);
}

static void fade_out(uint32_t delay_ms, uint32_t dur_ms)
{
    if (!s_root) {
        return;
    }
    lv_anim_delete(s_root, opa_in_cb);
    lv_anim_delete(s_root, opa_out_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, opa_out_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_root, 0), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, dur_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, done_cb);
    lv_anim_start(&a);
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    fade_out(0, T_TAP_OUT_MS);
}

bool ui_charge_anim_active(void)
{
    return s_root != NULL;
}

void ui_charge_anim_play(int percent)
{
    if (s_root) {
        return;
    }
    watch_theme_init();

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_bg_color(root, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(root, 225, 0); /* velo casi opaco: el anillo es lo único que se lee */
    lv_obj_set_style_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_CLICKED, NULL);
    s_root = root;

    /* Anillo: pista oscura y avance en el verde "ok" de la paleta. */
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, RING_SIZE, RING_SIZE);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, flex_color(FLEX_C_OK), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);

    lv_obj_t *bolt = flex_icon_create(root, FLEX_ICON_BOLT, 56, FLEX_C_OK);
    lv_obj_align_to(bolt, arc, LV_ALIGN_CENTER, 0, -42);
    lv_obj_t *pct = watch_theme_label(root, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    if (percent >= 0) {
        lv_label_set_text_fmt(pct, "%d %%", percent);
    }
    lv_obj_align_to(pct, arc, LV_ALIGN_CENTER, 0, 18);
    lv_obj_t *txt = watch_theme_label(root, FLEX_FONT_BODY, FLEX_C_TXT2, "Cargando");
    lv_obj_align_to(txt, arc, LV_ALIGN_CENTER, 0, 58);

    /* Entrada */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_exec_cb(&a, opa_in_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, T_IN_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    /* Anillo hasta el % real (nada si no hay dato) */
    if (percent > 0) {
        lv_anim_t r;
        lv_anim_init(&r);
        lv_anim_set_var(&r, arc);
        lv_anim_set_exec_cb(&r, arc_cb);
        lv_anim_set_values(&r, 0, percent > 100 ? 100 : percent);
        lv_anim_set_duration(&r, T_RING_MS);
        lv_anim_set_delay(&r, T_RING_DELAY);
        lv_anim_set_path_cb(&r, lv_anim_path_ease_out);
        lv_anim_start(&r);
    }

    /* Salida programada (la animación de entrada termina antes) */
    lv_anim_t o;
    lv_anim_init(&o);
    lv_anim_set_var(&o, root);
    lv_anim_set_exec_cb(&o, opa_out_cb);
    lv_anim_set_values(&o, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&o, T_OUT_MS);
    lv_anim_set_delay(&o, T_HOLD_MS);
    lv_anim_set_path_cb(&o, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&o, done_cb);
    lv_anim_start(&o);
}
