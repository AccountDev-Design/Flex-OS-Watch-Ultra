/*
 * ui_touchtest — Ajustes > Diagnóstico > Prueba de pantalla y táctil
 * (pruebas T2 y T3 de B1):
 *  - marco de 1 px en el borde exacto del panel: si el desfase de columna
 *    fuera incorrecto, uno de los lados se vería cortado o desplazado;
 *  - un anillo sigue al dedo y se muestran sus coordenadas;
 *  - guías de esquina (prueba T44): arcos tangentes a los bordes en las dos
 *    esquinas inferiores, de radio 104, 112, 120, 128 y 136 px, cada uno de
 *    un color. Un arco de radio menor que el de la esquina real se ve cortado
 *    por el centro; el primero que se ve entero da el radio del panel
 *    (FLEX_CORNER_R en flex_safe.h).
 * Se abre sin el gesto "volver" del borde (ui_nav_push_opts) para poder
 * tocar el borde izquierdo; se vuelve con el botón de la cabecera.
 */
#include "ui_internal.h"

#include "flex_safe.h"
#include "watch_theme.h"

#define GUIDE_N 5
static const int32_t s_guide_r[GUIDE_N] = {104, 112, 120, 128, 136};
static const uint32_t s_guide_c[GUIDE_N] = {FLEX_C_ERR, FLEX_C_WARN, FLEX_C_OK, FLEX_C_ACC_SOFT, FLEX_C_WHITE};

typedef struct {
    lv_obj_t *coords;
    lv_obj_t *dot;
} touchtest_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void touch_cb(lv_event_t *e)
{
    touchtest_t *t = lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        lv_free(t);
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_obj_set_pos(t->dot, p.x - 14, p.y - 14);
        lv_obj_remove_flag(t->dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(t->coords, "x %d  ·  y %d", (int)p.x, (int)p.y);
    } else {
        lv_obj_add_flag(t->dot, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Arcos de las dos esquinas inferiores, en coordenadas de pantalla. */
static void guides_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    for (int i = 0; i < GUIDE_N; i++) {
        const int32_t r = s_guide_r[i];
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.radius = (uint16_t)r;
        d.width = 2;
        d.color = flex_color(s_guide_c[i]);
        d.opa = LV_OPA_COVER;
        d.center.y = FLEX_SCREEN_H - r;
        d.center.x = FLEX_SCREEN_W - r; /* inferior derecha: de 0 a 90 grados */
        d.start_angle = 0;
        d.end_angle = 90;
        lv_draw_arc(layer, &d);
        d.center.x = r; /* inferior izquierda: de 90 a 180 grados */
        d.start_angle = 90;
        d.end_angle = 180;
        lv_draw_arc(layer, &d);
    }
}

static void guides_create(lv_obj_t *page)
{
    lv_obj_t *g = lv_obj_create(page);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, FLEX_SCREEN_W, s_guide_r[GUIDE_N - 1]);
    lv_obj_align(g, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g, guides_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* Leyenda: el radio de cada arco, en su color. */
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < GUIDE_N; i++) {
        lv_obj_t *l = watch_theme_label(row, FLEX_FONT_BODY, s_guide_c[i], "");
        lv_label_set_text_fmt(l, "%d", (int)s_guide_r[i]);
    }
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 92);
    lv_obj_t *cap = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_text_fmt(cap, "Radio de esquina (px). Ahora: %d", FLEX_CORNER_R);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 124);
}

lv_obj_t *ui_touchtest_create(lv_obj_t *parent)
{
    touchtest_t *t = lv_malloc_zeroed(sizeof(*t));
    LV_ASSERT_MALLOC(t);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_apply_background(page);

    lv_obj_t *frame = lv_obj_create(page);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_70, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    guides_create(page);
    watch_theme_header(page, "Prueba de pantalla", back_cb, NULL);

    lv_obj_t *help = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2,
                                       "Marco azul de 1 px en los 4 bordes.\nToca las esquinas y el centro.");
    lv_obj_set_style_text_align(help, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(help, LV_ALIGN_CENTER, 0, -70);
    t->coords = watch_theme_label(page, FLEX_FONT_TITLE, FLEX_C_TXT, "Toca la pantalla");
    lv_obj_align(t->coords, LV_ALIGN_CENTER, 0, 10);

    t->dot = lv_obj_create(page);
    lv_obj_remove_style_all(t->dot);
    lv_obj_set_size(t->dot, 28, 28);
    lv_obj_set_style_radius(t->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(t->dot, 3, 0);
    lv_obj_set_style_border_color(t->dot, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_border_opa(t->dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(t->dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t->dot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(page, touch_cb, LV_EVENT_PRESSED, t);
    lv_obj_add_event_cb(page, touch_cb, LV_EVENT_PRESSING, t);
    lv_obj_add_event_cb(page, touch_cb, LV_EVENT_RELEASED, t);
    lv_obj_add_event_cb(page, touch_cb, LV_EVENT_PRESS_LOST, t);
    lv_obj_add_event_cb(page, touch_cb, LV_EVENT_DELETE, t);
    return page;
}
