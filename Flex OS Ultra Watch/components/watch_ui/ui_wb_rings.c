/*
 * ui_wb_rings — los tres anillos de Bienestar (mover, ejercicio, pasos) con
 * lv_arc de LVGL: pista atenuada del mismo color, indicador con extremos
 * redondeados empezando arriba y un segundo arco más claro para lo que pasa
 * del 100 % (hasta dar otra vuelta). Sin imágenes ni lienzos.
 *
 * Animación: al abrir crecen desde 0 (700 ms) y, después, cada cambio va
 * desde lo que se ve (400 ms, ease-out). Una animación por anillo como mucho:
 * lv_anim_start sustituye la anterior del mismo anillo. Las animaciones se
 * borran con el objeto (no quedan colgando).
 */
#include "ui_wb.h"

#include "flex_glass_math.h"
#include "watch_theme.h"

#define OPEN_MS   700
#define UPDATE_MS 400
#define GAP       3

typedef struct {
    lv_obj_t *base;
    lv_obj_t *over;
    int32_t shown; /* ‰ que se ve (0..2000) */
} ring_t;

typedef struct {
    ring_t r[3];
    bool first;
} rings_t;

static const uint32_t kColor[3] = {WB_C_MOVE, WB_C_EXER, WB_C_STEPS};

static void show(ring_t *r, int32_t v)
{
    r->shown = v;
    const int32_t b = v > 1000 ? 1000 : v;
    const int32_t o = v > 1000 ? (v - 1000 > 1000 ? 1000 : v - 1000) : 0;
    if (lv_arc_get_value(r->base) != b) {
        lv_arc_set_value(r->base, (int16_t)b);
    }
    if (lv_arc_get_value(r->over) != o) {
        lv_arc_set_value(r->over, (int16_t)o);
    }
    watch_theme_set_visible(r->over, o > 0);
}

static void anim_cb(void *var, int32_t v)
{
    show((ring_t *)var, v);
}

static lv_obj_t *arc(lv_obj_t *parent, int32_t d, int32_t w, uint32_t rgb, bool track)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, d, d);
    lv_obj_center(a);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(flex_mix_rgb(rgb, FLEX_C_BLACK, 62)), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, track ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, flex_color(rgb), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return a;
}

static void delete_cb(lv_event_t *e)
{
    rings_t *rg = lv_event_get_user_data(e);
    for (int i = 0; i < 3; i++) {
        lv_anim_delete(&rg->r[i], anim_cb);
    }
    lv_free(rg);
}

lv_obj_t *ui_wb_rings_create(lv_obj_t *parent, int32_t d, int32_t w)
{
    rings_t *rg = lv_malloc_zeroed(sizeof(*rg));
    if (!rg) {
        return NULL;
    }
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, d, d);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; i++) {
        const int32_t di = d - 2 * i * (w + GAP);
        rg->r[i].base = arc(c, di, w, kColor[i], true);
        rg->r[i].over = arc(c, di, w, flex_mix_rgb(kColor[i], FLEX_C_WHITE, 170), false);
        lv_obj_add_flag(rg->r[i].over, LV_OBJ_FLAG_HIDDEN);
    }
    rg->first = true;
    lv_obj_set_user_data(c, rg);
    lv_obj_add_event_cb(c, delete_cb, LV_EVENT_DELETE, rg);
    return c;
}

void ui_wb_rings_set(lv_obj_t *rings, const act_rings_t *r, bool animate)
{
    rings_t *rg = rings ? lv_obj_get_user_data(rings) : NULL;
    if (!rg) {
        return;
    }
    const uint32_t ms = rg->first ? OPEN_MS : UPDATE_MS;
    rg->first = false;
    for (int i = 0; i < 3; i++) {
        int32_t t = (int32_t)r->pct[i] * 10;
        if (i == 0 && !r->kcal_valid) {
            t = 0; /* sin peso no hay calorías: solo la pista */
        }
        t = t > 2000 ? 2000 : t;
        ring_t *ri = &rg->r[i];
        if (t == ri->shown && !lv_anim_get(ri, anim_cb)) {
            continue;
        }
        if (!animate) {
            lv_anim_delete(ri, anim_cb);
            show(ri, t);
            continue;
        }
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ri);
        lv_anim_set_exec_cb(&a, anim_cb);
        lv_anim_set_values(&a, ri->shown, t);
        lv_anim_set_duration(&a, ms);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}
