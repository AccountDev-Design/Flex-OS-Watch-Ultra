/*
 * flex_icons — ver include/flex_icons.h
 *
 * Coordenadas de cada icono en centésimas de su caja (0..100), para que el
 * mismo trazo sirva a 16 px en la barra de estado y a 64 px en un panel.
 */
#include "flex_icons.h"

typedef struct {
    flex_icon_t icon;
    lv_color_t color;
} icon_data_t;

static inline int32_t px(const lv_area_t *b, int32_t c)
{
    return b->x1 + (lv_area_get_width(b) - 1) * c / 100;
}

static inline int32_t py(const lv_area_t *b, int32_t c)
{
    return b->y1 + (lv_area_get_height(b) - 1) * c / 100;
}

static void tri(lv_layer_t *layer, const lv_area_t *b, const int32_t pts[6], lv_color_t color, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    d.opa = opa;
    for (int i = 0; i < 3; i++) {
        d.p[i].x = px(b, pts[2 * i]);
        d.p[i].y = py(b, pts[2 * i + 1]);
    }
    lv_draw_triangle(layer, &d);
}

static void line(lv_layer_t *layer, const lv_area_t *b, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                 lv_color_t color, lv_opa_t opa)
{
    int32_t w = lv_area_get_height(b) / 7;
    if (w < 2) {
        w = 2;
    }
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.opa = opa;
    d.width = w;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = px(b, x1);
    d.p1.y = py(b, y1);
    d.p2.x = px(b, x2);
    d.p2.y = py(b, y2);
    lv_draw_line(layer, &d);
}

void flex_icon_draw(lv_layer_t *layer, flex_icon_t icon, const lv_area_t *box, lv_color_t color, lv_opa_t opa)
{
    switch (icon) {
    case FLEX_ICON_BOLT: {
        /* Rayo de dos triángulos, como qpIcoBattSave de la referencia. */
        static const int32_t t1[6] = {62, 0, 14, 58, 52, 58};
        static const int32_t t2[6] = {48, 42, 86, 42, 38, 100};
        tri(layer, box, t1, color, opa);
        tri(layer, box, t2, color, opa);
        break;
    }
    case FLEX_ICON_CHECK:
        line(layer, box, 15, 52, 40, 78, color, opa);
        line(layer, box, 40, 78, 85, 25, color, opa);
        break;
    case FLEX_ICON_CROSS:
        line(layer, box, 18, 18, 82, 82, color, opa);
        line(layer, box, 82, 18, 18, 82, color, opa);
        break;
    default:
        break;
    }
}

static void icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const icon_data_t *d = lv_obj_get_user_data(obj);
    if (!d) {
        return;
    }
    lv_area_t box;
    lv_obj_get_content_coords(obj, &box);
    flex_icon_draw(lv_event_get_layer(e), d->icon, &box, d->color, lv_obj_get_style_opa_recursive(obj, 0));
}

static void icon_delete_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_free(lv_obj_get_user_data(obj));
    lv_obj_set_user_data(obj, NULL);
}

lv_obj_t *flex_icon_create(lv_obj_t *parent, flex_icon_t icon, int32_t size, uint32_t rgb)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    icon_data_t *d = lv_malloc(sizeof(*d));
    LV_ASSERT_MALLOC(d);
    d->icon = icon;
    d->color = lv_color_hex(rgb);
    lv_obj_set_user_data(obj, d);
    lv_obj_add_event_cb(obj, icon_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, icon_delete_cb, LV_EVENT_DELETE, NULL);
    return obj;
}

void flex_icon_set_color(lv_obj_t *obj, uint32_t rgb)
{
    icon_data_t *d = lv_obj_get_user_data(obj);
    if (d && lv_color_to_u32(d->color) != lv_color_to_u32(lv_color_hex(rgb))) {
        d->color = lv_color_hex(rgb);
        lv_obj_invalidate(obj);
    }
}
