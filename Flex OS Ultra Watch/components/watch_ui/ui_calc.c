/*
 * ui_calc — app Calculadora (IC_CALC de Flex OS Ultra).
 *
 * Lógica: apps_logic (calcKey/calcLine de FlexOS_Ultra_AppsBasic.h,
 * transcritos). Diseño (calcRender, AppsBasic.h:220-265):
 *  - el display muestra la OPERACIÓN ("25 + 10") alineada a la derecha y, si
 *    no cabe, baja la letra (dfs-- en la referencia);
 *  - rejilla 5x4 que ocupa todo el ancho; la columna de operadores y "="
 *    conservan su NARANJA de marca, el resto son superficies;
 *  - la operación visible se conserva al salir y volver (sesión de la
 *    referencia; aquí en RAM hasta reiniciar).
 * Adaptación a 410x502: el display va en la franja de la cabecera, a la
 * derecha de "volver", para que las teclas midan 83x64 px (la zona táctil
 * mínima del reloj es 64) y la fila inferior quede dentro de las esquinas.
 */
#include "ui_internal.h"

#include "apps_logic.h"
#include "flex_safe.h"
#include "watch_theme.h"

#define CALC_ORANGE 0xF59628 /* rgb565(245,150,40), AppsBasic.h:243 */
#define GRID_X      26
#define GRID_Y      96
#define KEY_W       83
#define KEY_H       64
#define KEY_GAP     8
#define KEY_R       14 /* rad = bw/6, como mucho 14 */

static calc_t s_calc;
static bool s_calc_ready;

typedef struct {
    lv_obj_t *line;
    int32_t line_w;
} calc_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* Letra mayor que quepa (40 -> 28 -> 22); si ni así, se ve el final de la
 * línea (lo último tecleado), recortado por la izquierda. */
static void show_line(calc_page_t *p)
{
    char l[64];
    calc_line(&s_calc, l, sizeof(l));
    static const lv_font_t *const fonts[] = {FLEX_FONT_LARGE, FLEX_FONT_TITLE, FLEX_FONT_BODY};
    const lv_font_t *f = fonts[2];
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, l, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= p->line_w) {
            f = fonts[i];
            break;
        }
    }
    lv_obj_set_style_text_font(p->line, f, 0);
    lv_label_set_text(p->line, l);
    lv_obj_set_style_text_color(p->line, flex_color(s_calc.err ? FLEX_C_ERR : FLEX_C_TXT), 0);
}

static void key_cb(lv_event_t *e)
{
    calc_page_t *p = lv_event_get_user_data(e);
    const char *label = lv_obj_get_user_data(lv_event_get_current_target(e));
    calc_key(&s_calc, calc_key_from_label(label));
    show_line(p);
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_calc_create(lv_obj_t *parent)
{
    if (!s_calc_ready) {
        calc_reset(&s_calc);
        s_calc_ready = true;
    }
    calc_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, NULL, back_cb, NULL);

    /* Display en la franja de la cabecera, a la derecha de "volver". */
    const int32_t lh = lv_font_get_line_height(FLEX_FONT_LARGE);
    const int32_t ly = FLEX_SAFE_TOP + (FLEX_HDR_ZONE - lh) / 2;
    const int32_t lx = FLEX_HDR_X + FLEX_HDR_ZONE + FLEX_HDR_GAP + 24;
    p->line_w = flex_safe_right(ly, ly + lh - 1, FLEX_SAFE_MARGIN) + 1 - lx;
    p->line = watch_theme_label(page, FLEX_FONT_LARGE, FLEX_C_TXT, "0");
    lv_label_set_long_mode(p->line, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(p->line, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(p->line, p->line_w, lh);
    lv_obj_set_pos(p->line, lx, ly);

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            const char *t = calc_labels[r][c];
            const bool op = calc_label_is_op(r, c);
            lv_obj_t *k = watch_theme_button(page, t, op ? CALC_ORANGE : FLEX_C_SURF2, KEY_W, KEY_H);
            lv_obj_set_style_radius(k, KEY_R, 0);
            /* una cifra: letra grande; "+/-" y "DEL", la siguiente (fs-- de calcFontFor) */
            lv_obj_set_style_text_font(k, t[1] ? FLEX_FONT_BODY : FLEX_FONT_TITLE, 0);
            lv_obj_set_pos(k, GRID_X + c * (KEY_W + KEY_GAP), GRID_Y + r * (KEY_H + KEY_GAP));
            lv_obj_set_user_data(k, (void *)t);
            lv_obj_add_event_cb(k, key_cb, LV_EVENT_CLICKED, p);
        }
    }
    show_line(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    return page;
}
