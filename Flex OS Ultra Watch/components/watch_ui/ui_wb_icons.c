/*
 * ui_wb_icons — iconos de los entrenamientos de Bienestar, dibujados con las
 * primitivas de LVGL (líneas, arcos, discos y triángulos) en el evento de
 * dibujo del objeto: sin imágenes en flash ni memoria por icono (el tipo y el
 * color van en user_data). Mismo lenguaje que flex_icons.c: coordenadas en
 * centésimas de la caja y trazos redondeados, para que valgan a 28 px en una
 * fila y a 96 px en la cabecera del entrenamiento.
 */
#include "ui_wb.h"

#include "watch_theme.h"

typedef struct {
    lv_layer_t *layer;
    lv_area_t b;
    int32_t side;
    lv_color_t c;
} pen_t;

static int32_t X(const pen_t *p, int32_t v)
{
    return p->b.x1 + (lv_area_get_width(&p->b) - 1) * v / 100;
}

static int32_t Y(const pen_t *p, int32_t v)
{
    return p->b.y1 + (lv_area_get_height(&p->b) - 1) * v / 100;
}

static int32_t R(const pen_t *p, int32_t v, int32_t min)
{
    const int32_t r = (p->side * v + 50) / 100;
    return r < min ? min : r;
}

static void ln(const pen_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = p->c;
    d.width = R(p, w, 2);
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = X(p, x1);
    d.p1.y = Y(p, y1);
    d.p2.x = X(p, x2);
    d.p2.y = Y(p, y2);
    lv_draw_line(p->layer, &d);
}

/* Polilínea de n puntos (x, y alternados). */
static void poly(const pen_t *p, const int8_t *pts, int n, int32_t w)
{
    for (int i = 0; i + 1 < n; i++) {
        ln(p, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], w);
    }
}

static void ring(const pen_t *p, int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = p->c;
    d.center.x = X(p, cx);
    d.center.y = Y(p, cy);
    d.radius = (uint16_t)R(p, r, 2);
    d.width = R(p, w, 2);
    d.start_angle = a0;
    d.end_angle = a1;
    d.rounded = !(a0 == 0 && a1 == 360);
    lv_draw_arc(p->layer, &d);
}

static void disc(const pen_t *p, int32_t cx, int32_t cy, int32_t r)
{
    const int32_t rr = R(p, r, 1), x = X(p, cx), y = Y(p, cy);
    const lv_area_t a = {x - rr, y - rr, x + rr, y + rr};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->c;
    d.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(p->layer, &d, &a);
}

static void box(const pen_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t r)
{
    const lv_area_t a = {X(p, x1), Y(p, y1), X(p, x2), Y(p, y2)};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->c;
    d.radius = R(p, r, 0);
    lv_draw_rect(p->layer, &d, &a);
}

static void tri(const pen_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = p->c;
    d.p[0].x = X(p, x1);
    d.p[0].y = Y(p, y1);
    d.p[1].x = X(p, x2);
    d.p[1].y = Y(p, y2);
    d.p[2].x = X(p, x3);
    d.p[2].y = Y(p, y3);
    lv_draw_triangle(p->layer, &d);
}

static void draw(const pen_t *p, act_icon_t ic)
{
    switch (ic) {
    case ACT_IC_WALK: {
        disc(p, 56, 13, 10);
        static const int8_t arm1[] = {52, 32, 38, 46}, arm2[] = {52, 32, 64, 50};
        static const int8_t leg1[] = {47, 58, 36, 90}, leg2[] = {47, 58, 60, 74, 58, 92};
        ln(p, 53, 28, 47, 58, 10);
        poly(p, arm1, 2, 9);
        poly(p, arm2, 2, 9);
        poly(p, leg1, 2, 10);
        poly(p, leg2, 3, 10);
        break;
    }
    case ACT_IC_RUN: {
        disc(p, 64, 13, 10);
        static const int8_t a1[] = {58, 31, 74, 42, 84, 32}, a2[] = {58, 31, 42, 34, 32, 46};
        static const int8_t l1[] = {46, 55, 66, 66, 62, 90}, l2[] = {46, 55, 32, 72, 14, 72};
        ln(p, 59, 28, 46, 55, 11);
        poly(p, a1, 3, 9);
        poly(p, a2, 3, 9);
        poly(p, l1, 3, 10);
        poly(p, l2, 3, 10);
        break;
    }
    case ACT_IC_HIKE: {
        disc(p, 48, 13, 10);
        static const int8_t arm[] = {45, 32, 62, 44}, l1[] = {40, 58, 28, 90}, l2[] = {40, 58, 54, 74, 52, 92};
        ln(p, 46, 28, 40, 58, 10);
        poly(p, arm, 2, 9);
        poly(p, l1, 2, 10);
        poly(p, l2, 3, 10);
        ln(p, 66, 36, 78, 94, 7); /* bastón */
        box(p, 24, 30, 36, 52, 4); /* mochila */
        break;
    }
    case ACT_IC_BIKE: {
        ring(p, 22, 70, 20, 7, 0, 360);
        ring(p, 78, 70, 20, 7, 0, 360);
        static const int8_t frame[] = {22, 70, 42, 46, 66, 46, 78, 70}, seat[] = {42, 46, 52, 70, 66, 46};
        poly(p, frame, 4, 7);
        poly(p, seat, 3, 7);
        disc(p, 58, 14, 9);
        ln(p, 56, 26, 44, 44, 9);
        ln(p, 54, 30, 68, 40, 8);
        break;
    }
    case ACT_IC_STAIRS: {
        static const int8_t s[] = {8, 90, 8, 72, 30, 72, 30, 52, 52, 52, 52, 32, 74, 32, 74, 12, 94, 12};
        poly(p, s, 9, 9);
        break;
    }
    case ACT_IC_MACHINE: {
        disc(p, 36, 18, 9);
        static const int8_t body[] = {38, 30, 46, 60}, arm[] = {40, 40, 74, 46}, leg[] = {46, 60, 72, 64, 82, 82};
        poly(p, body, 2, 10);
        poly(p, arm, 2, 8);
        poly(p, leg, 3, 9);
        ln(p, 8, 88, 94, 88, 8);
        box(p, 36, 62, 54, 70, 3); /* asiento */
        break;
    }
    case ACT_IC_STRENGTH:
        ln(p, 22, 50, 78, 50, 9);
        box(p, 8, 28, 22, 72, 5);
        box(p, 78, 28, 92, 72, 5);
        box(p, 22, 36, 30, 64, 3);
        box(p, 70, 36, 78, 64, 3);
        break;
    case ACT_IC_HIIT:
        tri(p, 60, 4, 22, 58, 50, 58);
        tri(p, 40, 96, 78, 42, 50, 42);
        box(p, 40, 42, 60, 58, 0);
        break;
    case ACT_IC_YOGA: {
        disc(p, 50, 14, 10);
        ln(p, 50, 28, 50, 58, 10);
        static const int8_t arms[] = {22, 54, 50, 38, 78, 54}, legs[] = {28, 66, 50, 58, 72, 66};
        poly(p, arms, 3, 8);
        poly(p, legs, 3, 9);
        ln(p, 16, 76, 84, 76, 10);
        break;
    }
    case ACT_IC_DANCE: {
        disc(p, 54, 13, 10);
        static const int8_t up[] = {50, 32, 66, 18, 74, 6}, down[] = {50, 34, 30, 44};
        static const int8_t l1[] = {46, 58, 30, 90}, l2[] = {46, 58, 66, 70, 80, 62};
        ln(p, 52, 28, 46, 58, 10);
        poly(p, up, 3, 9);
        poly(p, down, 2, 9);
        poly(p, l1, 2, 10);
        poly(p, l2, 3, 10);
        break;
    }
    case ACT_IC_STRETCH: {
        disc(p, 26, 34, 10);
        static const int8_t arm[] = {34, 46, 22, 82}, l1[] = {62, 60, 54, 92}, l2[] = {62, 60, 82, 92};
        ln(p, 34, 44, 62, 60, 10);
        poly(p, arm, 2, 9);
        poly(p, l1, 2, 10);
        poly(p, l2, 2, 10);
        break;
    }
    case ACT_IC_BALL:
        ring(p, 50, 50, 44, 7, 0, 360);
        ln(p, 8, 50, 92, 50, 6);
        ln(p, 50, 8, 50, 92, 6);
        ring(p, 10, 50, 30, 6, 300, 60);
        ring(p, 90, 50, 30, 6, 120, 240);
        break;
    case ACT_IC_RACKET:
        ring(p, 40, 38, 30, 7, 0, 360);
        ln(p, 26, 38, 54, 38, 4);
        ln(p, 40, 24, 40, 52, 4);
        ln(p, 61, 59, 88, 88, 11);
        disc(p, 82, 20, 9);
        break;
    case ACT_IC_COMBAT:
        disc(p, 54, 40, 30);
        disc(p, 24, 50, 13);
        box(p, 30, 64, 76, 92, 8);
        break;
    case ACT_IC_SWIM: {
        disc(p, 74, 26, 10);
        ln(p, 18, 52, 62, 38, 9);
        ln(p, 44, 44, 58, 20, 8);
        static const int8_t w[] = {6, 80, 20, 72, 34, 80, 48, 72, 62, 80, 76, 72, 92, 80};
        poly(p, w, 7, 7);
        break;
    }
    case ACT_IC_WATER: {
        static const int8_t w1[] = {6, 30, 20, 22, 34, 30, 48, 22, 62, 30, 76, 22, 92, 30};
        static const int8_t w2[] = {6, 54, 20, 46, 34, 54, 48, 46, 62, 54, 76, 46, 92, 54};
        static const int8_t w3[] = {6, 78, 20, 70, 34, 78, 48, 70, 62, 78, 76, 70, 92, 78};
        poly(p, w1, 7, 7);
        poly(p, w2, 7, 7);
        poly(p, w3, 7, 7);
        break;
    }
    case ACT_IC_SNOW:
        ln(p, 50, 6, 50, 94, 8);
        ln(p, 12, 28, 88, 72, 8);
        ln(p, 12, 72, 88, 28, 8);
        ln(p, 38, 12, 50, 24, 6);
        ln(p, 62, 12, 50, 24, 6);
        ln(p, 38, 88, 50, 76, 6);
        ln(p, 62, 88, 50, 76, 6);
        break;
    case ACT_IC_MOUNTAIN:
        tri(p, 4, 90, 44, 16, 84, 90);
        tri(p, 48, 90, 72, 44, 96, 90);
        break;
    case ACT_IC_TARGET:
        ring(p, 44, 56, 40, 8, 0, 360);
        ring(p, 44, 56, 22, 8, 0, 360);
        disc(p, 44, 56, 7);
        ln(p, 52, 48, 90, 10, 6);
        tri(p, 92, 4, 96, 22, 78, 16);
        break;
    case ACT_IC_WHEEL:
        ring(p, 42, 66, 28, 7, 0, 360);
        disc(p, 46, 13, 9);
        {
            static const int8_t body[] = {46, 24, 46, 50, 70, 52, 78, 82}, arm[] = {46, 32, 62, 44};
            poly(p, body, 4, 9);
            poly(p, arm, 2, 8);
        }
        break;
    case ACT_IC_STAR:
    default:
        /* Estrella de cinco puntas: un pentágono y sus cinco picos. */
        tri(p, 50, 4, 38, 38, 62, 38);
        tri(p, 96, 38, 62, 38, 70, 60);
        tri(p, 78, 94, 70, 60, 50, 74);
        tri(p, 22, 94, 50, 74, 30, 60);
        tri(p, 4, 38, 30, 60, 38, 38);
        tri(p, 38, 38, 62, 38, 50, 74);
        tri(p, 30, 60, 38, 38, 50, 74);
        tri(p, 70, 60, 62, 38, 50, 74);
        break;
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const uintptr_t ud = (uintptr_t)lv_obj_get_user_data(obj);
    pen_t p;
    p.layer = lv_event_get_layer(e);
    lv_obj_get_coords(obj, &p.b);
    p.side = LV_MIN(lv_area_get_width(&p.b), lv_area_get_height(&p.b));
    p.c = lv_color_hex((uint32_t)(ud & 0xFFFFFFu));
    draw(&p, (act_icon_t)((ud >> 24) & 0x3F));
}

lv_obj_t *ui_wb_icon_create(lv_obj_t *parent, act_icon_t icon, int32_t size, uint32_t rgb)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(o, (void *)(((uintptr_t)icon << 24) | (rgb & 0xFFFFFFu)));
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void ui_wb_icon_set(lv_obj_t *obj, act_icon_t icon, uint32_t rgb)
{
    void *ud = (void *)(((uintptr_t)icon << 24) | (rgb & 0xFFFFFFu));
    if (lv_obj_get_user_data(obj) != ud) {
        lv_obj_set_user_data(obj, ud);
        lv_obj_invalidate(obj);
    }
}

lv_obj_t *ui_wb_icon_badge(lv_obj_t *parent, act_icon_t icon, int32_t d, uint32_t bg_rgb)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, d, d);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, flex_color(bg_rgb), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *i = ui_wb_icon_create(c, icon, d * 60 / 100, 0x0B0D10);
    lv_obj_center(i);
    return c;
}

uint32_t ui_wb_cat_color(act_cat_t c)
{
    static const uint32_t k[ACT_CAT_COUNT] = {
        0xB6F33A, /* caminar y correr: el verde de "empezar" */
        0x2EC4B6, 0xFF8A3D, 0xB58CFF, 0xFFC233, 0xFF5C7A, 0x45B1FF, 0x9CC4FF, 0xA6ACBA, 0x5AD1C8,
    };
    return (unsigned)c < ACT_CAT_COUNT ? k[c] : 0xA6ACBA;
}
