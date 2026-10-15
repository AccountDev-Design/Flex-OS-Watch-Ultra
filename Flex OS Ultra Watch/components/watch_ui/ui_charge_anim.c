/*
 * ui_charge_anim — animación de inicio de carga.
 *
 * Solo se reproduce cuando ui_system la pide (inicio de carga CONFIRMADO, ver
 * ui_charge_gate_update). Es limitada: entrada 220 ms, anillo hasta el % real
 * en 900 ms, salida a los 2 s (unos 2,3 s en total), y un toque la cierra.
 * Todo vive en un contenedor negro opaco, hijo de la pantalla de navegación
 * por encima de las páginas (ui_overlay_parent: así LVGL no dibuja la esfera
 * que tapa), que se borra al terminar. Las animaciones de opacidad van ligadas a s_ca y se cancelan al
 * cerrar; la del anillo va ligada al anillo y LVGL la elimina con él.
 */
#include "ui_internal.h"

#include <string.h>
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

static struct {
    lv_obj_t *root;
    lv_obj_t *arc;
    lv_obj_t *bolt;
    lv_obj_t *pct;
    lv_obj_t *txt;
} s_ca;

/* Fundido del CONTENIDO, no del contenedor: poner opacidad a un contenedor
 * con hijos obliga a LVGL a dibujarlo en una capa intermedia y mezclarla en
 * cada cuadro, a pantalla completa (era el tirón del principio que se vio en
 * el reloj). Aquí el fondo negro es opaco desde el primer cuadro (LVGL deja
 * de dibujar la esfera debajo) y solo cambian la opacidad del anillo y de los
 * textos y el color del rayo: zonas pequeñas, sin capas. */
static void set_content_opa(int32_t v)
{
    const lv_opa_t o = (lv_opa_t)v;
    lv_obj_set_style_arc_opa(s_ca.arc, o, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_ca.arc, o, LV_PART_INDICATOR);
    lv_obj_set_style_text_opa(s_ca.pct, o, 0);
    lv_obj_set_style_text_opa(s_ca.txt, o, 0);
    flex_icon_set_color(s_ca.bolt, lv_color_to_u32(lv_color_mix(flex_color(FLEX_C_OK), lv_color_black(), o)) & 0xFFFFFF);
}

/* Dos funciones distintas: lv_anim_start() sustituye una animación existente
 * con el mismo par (variable, función), y la salida programada no debe anular
 * la entrada. */
static void opa_in_cb(void *var, int32_t v)
{
    (void)var;
    set_content_opa(v);
}

static void opa_out_cb(void *var, int32_t v)
{
    (void)var;
    set_content_opa(v);
}

static void arc_cb(void *var, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)var, v);
}

static void done_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_t *root = s_ca.root;
    memset(&s_ca, 0, sizeof(s_ca));
    if (root) {
        lv_obj_delete_async(root); /* con él, el anillo y su animación */
    }
}

/* Salida desde la opacidad from. Con retraso: early_apply en false, porque
 * LVGL 9 aplica por defecto el valor inicial de una animación en el momento de
 * crearla aunque tenga retraso; la salida programada ponía el contenido a
 * opacidad completa antes de la entrada (destello visible al principio). */
static void start_out(uint32_t delay_ms, uint32_t dur_ms, int32_t from)
{
    lv_anim_delete(&s_ca, opa_out_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_ca);
    lv_anim_set_exec_cb(&a, opa_out_cb);
    lv_anim_set_values(&a, from, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, dur_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_early_apply(&a, false);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, done_cb);
    lv_anim_start(&a);
}

static void fade_out_now(uint32_t dur_ms)
{
    if (!s_ca.root) {
        return;
    }
    lv_anim_delete(&s_ca, opa_in_cb);
    start_out(0, dur_ms, lv_obj_get_style_text_opa(s_ca.pct, 0));
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    fade_out_now(T_TAP_OUT_MS);
}

bool ui_charge_anim_active(void)
{
    return s_ca.root != NULL;
}

lv_obj_t *ui_charge_anim_obj(void)
{
    return s_ca.root;
}

void ui_charge_anim_play(int percent)
{
    if (s_ca.root) {
        return;
    }
    watch_theme_init();

    /* Fondo negro opaco (AMOLED): sin velo translúcido sobre la esfera. */
    lv_obj_t *root = lv_obj_create(ui_overlay_parent());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_bg_color(root, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_CLICKED, NULL);
    s_ca.root = root;

    /* Anillo: pista oscura y avance en el verde "ok" de la paleta. */
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, RING_SIZE, RING_SIZE);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, flex_color(FLEX_C_OK), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    s_ca.arc = arc;

    s_ca.bolt = flex_icon_create(root, FLEX_ICON_BOLT, 56, FLEX_C_BLACK);
    lv_obj_align_to(s_ca.bolt, arc, LV_ALIGN_CENTER, 0, -42);
    s_ca.pct = watch_theme_label(root, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    if (percent >= 0) {
        lv_label_set_text_fmt(s_ca.pct, "%d %%", percent);
    }
    lv_obj_align_to(s_ca.pct, arc, LV_ALIGN_CENTER, 0, 18);
    s_ca.txt = watch_theme_label(root, FLEX_FONT_BODY, FLEX_C_TXT2, "Cargando");
    lv_obj_align_to(s_ca.txt, arc, LV_ALIGN_CENTER, 0, 58);
    set_content_opa(LV_OPA_TRANSP);

    /* Entrada */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_ca);
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

    /* Salida programada */
    start_out(T_HOLD_MS, T_OUT_MS, LV_OPA_COVER);
}
