/*
 * flex_battery_icon — ver include/flex_battery_icon.h
 */
#include "flex_battery_icon.h"

#include "flex_icons.h"
#include "flex_tokens.h"

typedef struct {
    flex_batt_kind_t kind;
    int percent;
} batt_icon_t;

static void draw_rect(lv_layer_t *layer, const lv_area_t *a, int32_t radius, lv_color_t bg, lv_opa_t bg_opa,
                      lv_color_t border, int32_t border_w, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = bg;
    d.bg_opa = LV_OPA_MIX2(bg_opa, opa);
    d.border_color = border;
    d.border_width = border_w;
    d.border_opa = border_w ? opa : LV_OPA_TRANSP;
    lv_draw_rect(layer, &d, a);
}

static void batt_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const batt_icon_t *st = lv_obj_get_user_data(obj);
    if (!st) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_opa_t opa = lv_obj_get_style_opa_recursive(obj, 0);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t w = lv_area_get_width(&a);
    const int32_t h = lv_area_get_height(&a);
    int32_t pin_w = w / 14;
    if (pin_w < 2) {
        pin_w = 2;
    }
    int32_t bw = h / 9;
    if (bw < 1) {
        bw = 1;
    }
    const lv_color_t outline = lv_color_hex(flex_batt_outline_rgb(st->kind));
    const lv_color_t fill = lv_color_hex(flex_batt_fill_rgb(st->kind));

    /* cuerpo */
    lv_area_t body = a;
    body.x2 = a.x2 - pin_w - 1;
    draw_rect(layer, &body, h / 4, outline, LV_OPA_TRANSP, outline, bw, opa);

    /* polo + */
    lv_area_t pin = {body.x2 + 1, a.y1 + h / 3, a.x2, a.y2 - h / 3};
    draw_rect(layer, &pin, 1, outline, LV_OPA_COVER, outline, 0, opa);

    /* relleno */
    lv_area_t inner = body;
    lv_area_increase(&inner, -(bw + 1), -(bw + 1));
    const int32_t fw = flex_batt_fill_width(lv_area_get_width(&inner), st->kind, st->percent);
    if (fw > 0) {
        lv_area_t f = inner;
        f.x2 = inner.x1 + fw - 1;
        draw_rect(layer, &f, h / 8, fill, LV_OPA_COVER, fill, 0, opa);
    }

    /* glifo centrado en el cuerpo */
    const flex_batt_glyph_t g = flex_batt_glyph(st->kind);
    if (g == FLEX_BATT_GLYPH_NONE) {
        return;
    }
    const int32_t gs = lv_area_get_height(&inner) + 2;
    const int32_t cx = (body.x1 + body.x2) / 2;
    const int32_t cy = (body.y1 + body.y2) / 2;
    lv_area_t gbox = {cx - gs / 2, cy - gs / 2, cx - gs / 2 + gs - 1, cy - gs / 2 + gs - 1};
    /* El glifo va oscuro solo si el relleno lo cubre entero; si no, claro,
     * que contrasta tanto sobre el relleno como sobre el hueco negro. */
    const bool over_fill = fw > 0 && (inner.x1 + fw - 1) >= gbox.x2;
    lv_color_t gc = over_fill ? lv_color_hex(FLEX_C_PAGE) : lv_color_hex(FLEX_C_TXT);
    lv_opa_t gopa = opa;
    flex_icon_t icon = FLEX_ICON_BOLT;
    switch (g) {
    case FLEX_BATT_GLYPH_BOLT_DIM:
        gopa = LV_OPA_MIX2(opa, LV_OPA_60);
        break;
    case FLEX_BATT_GLYPH_CHECK:
        icon = FLEX_ICON_CHECK;
        break;
    case FLEX_BATT_GLYPH_CROSS:
        icon = FLEX_ICON_CROSS;
        gc = lv_color_hex(FLEX_C_MUTE);
        break;
    default:
        break;
    }
    flex_icon_draw(layer, icon, &gbox, gc, gopa);
}

static void batt_delete_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_free(lv_obj_get_user_data(obj));
    lv_obj_set_user_data(obj, NULL);
}

lv_obj_t *flex_battery_icon_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    batt_icon_t *st = lv_malloc(sizeof(*st));
    LV_ASSERT_MALLOC(st);
    st->kind = FLEX_BATT_UNKNOWN;
    st->percent = -1;
    lv_obj_set_user_data(obj, st);
    lv_obj_add_event_cb(obj, batt_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, batt_delete_cb, LV_EVENT_DELETE, NULL);
    return obj;
}

bool flex_battery_icon_set(lv_obj_t *icon, flex_batt_kind_t kind, int percent)
{
    batt_icon_t *st = lv_obj_get_user_data(icon);
    if (!st || (st->kind == kind && st->percent == percent)) {
        return false;
    }
    st->kind = kind;
    st->percent = percent;
    lv_obj_invalidate(icon);
    return true;
}
