/*
 * ui_paint — app Paint (IC_PAINT de Flex OS Ultra) en el reloj (F9.11).
 *
 * Como la referencia (FlexOS_Ultra_AppPaint.h, formato FXP1 de FlexOS_FS):
 *  - los dibujos son archivos .fxp de trazos en /Paint, compatibles byte a
 *    byte: uno hecho en Flex OS Ultra se abre aquí y al revés;
 *  - cada trazo se escribe al LEVANTAR el dedo (no hay botón "guardar");
 *  - el lienzo y las miniaturas se reconstruyen desde el ARCHIVO: lo que se
 *    ve es lo que hay guardado, a cualquier escala;
 *  - 6 colores y 3 grosores de la referencia (P_PAL, P_SIZES); "Deshacer"
 *    real (quita el último trazo del archivo) y "Limpiar".
 * Reducido al reloj: lienzo de 378 × 250 px y barra de dos filas. Un dibujo
 * de otro tamaño se encaja centrado y los trazos nuevos se guardan en sus
 * coordenadas (radio incluido), así que no cambian de tamaño al volver.
 * "Limpiar" pide un segundo toque (en la referencia era inmediato).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "flexfs.h"
#include "fxp.h"
#include "watch_theme.h"

#define CV_X       16
#define CV_Y       108
#define CV_W       378
#define CV_H       250
#define TOOLS_Y1   366
#define TOOLS_Y2   410
#define SWATCH     38
#define THUMB_W    96
#define THUMB_H    64
#define LIST_MAX   16
#define CONFIRM_MS 3000
#define PAPER      0xFAFAFC /* rgb565(250,250,252) de la referencia */
#define MARGIN     0xDADEE6 /* rgb565(218,222,230) */

static const uint8_t kPal[6][3] = {{30, 30, 40},   {230, 60, 60},  {240, 150, 40},
                                   {240, 210, 50}, {80, 180, 120}, {60, 120, 235}};
static const uint8_t kSizes[3] = {3, 6, 12};

static char s_paint_path[FL_PATH_MAX];

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

void ui_paint_open(const char *path)
{
    snprintf(s_paint_path, sizeof(s_paint_path), "%s", path ? path : "");
    ui_nav_push_opts(ui_paint_canvas_create, false); /* sin gesto del borde: se dibuja hasta él */
}

/* ---------- dibujo en un lienzo LVGL ---------- */

static lv_color_t c565(uint16_t c)
{
    const uint8_t r = (uint8_t)((c >> 8) & 0xF8), g = (uint8_t)((c >> 3) & 0xFC), b = (uint8_t)((c << 3) & 0xF8);
    return lv_color_make((uint8_t)(r | r >> 5), (uint8_t)(g | g >> 6), (uint8_t)(b | b >> 5));
}

static void seg_cb(int x0, int y0, int x1, int y1, uint16_t color, int r, void *user)
{
    lv_layer_t *layer = user;
    if (x0 == x1 && y0 == y1) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c565(color);
        d.bg_opa = LV_OPA_COVER;
        d.radius = LV_RADIUS_CIRCLE;
        const lv_area_t a = {x0 - r, y0 - r, x0 + r, y0 + r};
        lv_draw_rect(layer, &d, &a);
        return;
    }
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c565(color);
    d.width = 2 * r;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x0;
    d.p1.y = y0;
    d.p2.x = x1;
    d.p2.y = y1;
    lv_draw_line(layer, &d);
}

typedef struct {
    float sc;
    int dx, dy, dw, dh; /* documento encajado dentro del lienzo */
} fit_t;

static fit_t fit(const fxp_info_t *in, int cw, int ch)
{
    fit_t f;
    const float sx = (float)cw / in->w, sy = (float)ch / in->h;
    f.sc = sx < sy ? sx : sy;
    f.dw = (int)(in->w * f.sc + 0.5f);
    f.dh = (int)(in->h * f.sc + 0.5f);
    f.dw = f.dw < 1 ? 1 : (f.dw > cw ? cw : f.dw);
    f.dh = f.dh < 1 ? 1 : (f.dh > ch ? ch : f.dh);
    f.dx = (cw - f.dw) / 2;
    f.dy = (ch - f.dh) / 2;
    return f;
}

/* Pinta el archivo entero en el lienzo (fondo, papel y trazos) */
static bool render(lv_obj_t *canvas, int cw, int ch, const char *path, fit_t *out)
{
    fxp_info_t in;
    lv_canvas_fill_bg(canvas, flex_color(MARGIN), LV_OPA_COVER);
    if (!fxp_info(path, &in)) {
        return false;
    }
    const fit_t f = fit(&in, cw, ch);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_t paper;
    lv_draw_rect_dsc_init(&paper);
    paper.bg_color = flex_color(PAPER);
    paper.bg_opa = LV_OPA_COVER;
    const lv_area_t pa = {f.dx, f.dy, f.dx + f.dw - 1, f.dy + f.dh - 1};
    lv_draw_rect(&layer, &paper, &pa);
    fxp_replay(path, f.sc, f.dx, f.dy, seg_cb, &layer);
    lv_canvas_finish_layer(canvas, &layer);
    if (out) {
        *out = f;
    }
    return true;
}

/* ======================= Galería ======================= */

typedef struct {
    lv_obj_t *list;
    uint16_t *thumbs;
    uint32_t gen;
    bool ready;
} pg_page_t;

static void str_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void card_cb(lv_event_t *e)
{
    const char *path = lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (!path) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        ui_file_options(path);
    } else {
        ui_paint_open(path);
    }
}

static void new_cb(lv_event_t *e)
{
    (void)e;
    char p[FL_PATH_MAX];
    if (ffs_new_name(FFS_DIR_PAINT, "Dibujo", FXP_EXT, p, sizeof(p)) && fxp_create(p, CV_W, CV_H)) {
        ui_files_changed();
        ui_paint_open(p);
    }
}

static void build(pg_page_t *p)
{
    lv_obj_clean(p->list);
    if (p->thumbs) {
        heap_caps_free(p->thumbs);
        p->thumbs = NULL;
    }
    const char *why = NULL;
    p->ready = ui_fs_ready(&why);
    if (!p->ready) {
        ui_fs_notice(p->list, "Paint", why);
        return;
    }
    lv_obj_t *nb = watch_theme_button(p->list, "Nuevo dibujo", FLEX_C_PRIMARY, LV_PCT(100), 60);
    lv_obj_add_event_cb(nb, new_cb, LV_EVENT_CLICKED, NULL);
    ffs_entry_t *e = lv_malloc(sizeof(ffs_entry_t) * FFS_LIST_MAX);
    LV_ASSERT_MALLOC(e);
    const int n = ffs_list(FFS_DIR_PAINT, e, FFS_LIST_MAX);
    /* Un solo bloque en PSRAM para todas las miniaturas */
    p->thumbs = n > 0 ? heap_caps_malloc((size_t)LIST_MAX * THUMB_W * THUMB_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                      : NULL;
    int shown = 0;
    for (int i = 0; i < n && shown < LIST_MAX; i++) {
        if (e[i].dir || strcmp(fl_ext(e[i].name), FXP_EXT)) {
            continue;
        }
        char path[FL_PATH_MAX], stem[FL_NAME_MAX], t[FL_NAME_MAX + 32];
        fxp_info_t in;
        if (!fl_join(FFS_DIR_PAINT, e[i].name, path, sizeof(path))) {
            continue;
        }
        const bool valid = fxp_info(path, &in);
        lv_obj_t *c = watch_theme_tile(p->list, 0, 0, LV_PCT(100), THUMB_H + 24);
        if (p->thumbs) {
            lv_obj_t *cv = lv_canvas_create(c);
            lv_canvas_set_buffer(cv, p->thumbs + (size_t)shown * THUMB_W * THUMB_H, THUMB_W, THUMB_H,
                                 LV_COLOR_FORMAT_RGB565);
            lv_obj_align(cv, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_remove_flag(cv, LV_OBJ_FLAG_CLICKABLE);
            render(cv, THUMB_W, THUMB_H, path, NULL); /* el mismo dibujo, a escala */
        }
        lv_obj_t *col = lv_obj_create(c);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_left(col, THUMB_W + 14, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
        fl_stem(e[i].name, stem, sizeof(stem));
        ui_phone_text(stem, t, sizeof(t), FLEX_FONT_BODY);
        lv_obj_t *nm = watch_theme_label(col, FLEX_FONT_BODY, FLEX_C_TXT, t);
        lv_obj_set_width(nm, LV_PCT(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
        if (valid) {
            snprintf(t, sizeof(t), "%u %s", in.strokes, in.strokes == 1 ? "trazo" : "trazos");
        } else {
            snprintf(t, sizeof(t), "No es un dibujo válido");
        }
        watch_theme_label(col, FLEX_FONT_SMALL, valid ? FLEX_C_TXT2 : FLEX_C_WARN, t);
        char *ud = lv_malloc(strlen(path) + 1);
        LV_ASSERT_MALLOC(ud);
        strcpy(ud, path);
        lv_obj_set_user_data(c, ud);
        lv_obj_add_event_cb(c, str_free_cb, LV_EVENT_DELETE, ud);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_LONG_PRESSED, NULL);
        shown++;
    }
    lv_free(e);
    const char *hint = shown ? "Mantén pulsado un dibujo para renombrarlo o tirarlo."
                             : "No hay dibujos todavía. Pulsa «Nuevo dibujo» para crear uno.";
    lv_obj_t *h = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, hint);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(h, LV_PCT(100));
}

static void pg_tick_cb(lv_timer_t *t)
{
    pg_page_t *p = lv_timer_get_user_data(t);
    const bool ready = ui_fs_ready(NULL);
    if (p->gen != ui_files_gen() || ready != p->ready) {
        p->gen = ui_files_gen();
        build(p);
    }
}

static void pg_free_cb(lv_event_t *e)
{
    pg_page_t *p = lv_event_get_user_data(e);
    if (p->thumbs) {
        heap_caps_free(p->thumbs);
    }
    lv_free(p);
}

lv_obj_t *ui_paint_create(lv_obj_t *parent)
{
    pg_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Paint", back_cb, NULL);
    p->list = watch_theme_list(page);
    p->gen = ui_files_gen();
    build(p);
    lv_obj_add_event_cb(page, pg_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, pg_tick_cb, 500, p);
    return page;
}

/* ======================= Lienzo ======================= */

typedef struct {
    char path[FL_PATH_MAX];
    lv_obj_t *canvas, *sub, *swatch[6], *size_btn, *size_dot, *clear;
    uint16_t *buf;
    int16_t *stroke; /* FXP_MAX_PTS pares en coordenadas del documento */
    int n;
    int prev_x, prev_y; /* -1: sin trazo en curso */
    int color, size;
    fit_t f;
    uint32_t clear_at;
    bool failed;
} pc_page_t;

static void update_sub(pc_page_t *p)
{
    fxp_info_t in;
    char t[64];
    if (p->failed) {
        snprintf(t, sizeof(t), "No se pudo guardar el último trazo");
    } else if (fxp_info(p->path, &in)) {
        snprintf(t, sizeof(t), "%u %s", in.strokes, in.strokes == 1 ? "trazo guardado" : "trazos guardados");
    } else {
        snprintf(t, sizeof(t), "No es un dibujo válido");
    }
    lv_label_set_text(p->sub, t);
    lv_obj_set_style_text_color(p->sub, flex_color(p->failed ? FLEX_C_WARN : FLEX_C_TXT2), 0);
}

static uint16_t pen565(const pc_page_t *p)
{
    return fxp_rgb565(kPal[p->color][0], kPal[p->color][1], kPal[p->color][2]);
}

/* El trazo va al archivo al levantar el dedo (paintFlushStroke) */
static void flush(pc_page_t *p)
{
    if (p->n > 0) {
        int rad = (int)(kSizes[p->size] / p->f.sc + 0.5f); /* radio en unidades del documento */
        rad = rad < 1 ? 1 : (rad > 255 ? 255 : rad);
        p->failed = !fxp_append(p->path, pen565(p), (uint8_t)rad, p->stroke, (uint16_t)p->n);
        p->n = 0;
        ui_files_changed();
        update_sub(p);
    }
}

static int16_t doc_coord(int v, int off, float sc)
{
    return (int16_t)((v - off) / sc + 0.5f);
}

static void draw_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        flush(p);
        p->prev_x = p->prev_y = -1;
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !p->stroke) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t a;
    lv_obj_get_coords(p->canvas, &a);
    const int x = pt.x - a.x1, y = pt.y - a.y1;
    if (x < p->f.dx || y < p->f.dy || x >= p->f.dx + p->f.dw || y >= p->f.dy + p->f.dh) {
        return; /* fuera del papel */
    }
    const int r = kSizes[p->size];
    if (p->prev_x >= 0) {
        const int ddx = x - p->prev_x, ddy = y - p->prev_y;
        if (ddx * ddx + ddy * ddy < 4) {
            return; /* puntos casi pegados: engordan el archivo sin cambiar el trazo */
        }
    }
    lv_layer_t layer;
    lv_canvas_init_layer(p->canvas, &layer);
    seg_cb(p->prev_x >= 0 ? p->prev_x : x, p->prev_x >= 0 ? p->prev_y : y, x, y, pen565(p), r, &layer);
    lv_canvas_finish_layer(p->canvas, &layer);
    if (p->n >= FXP_MAX_PTS) {
        /* trazo larguísimo: se cierra y sigue otro desde el último punto */
        const int16_t lx = p->stroke[2 * (p->n - 1)], ly = p->stroke[2 * (p->n - 1) + 1];
        flush(p);
        p->stroke[0] = lx;
        p->stroke[1] = ly;
        p->n = 1;
    }
    p->stroke[2 * p->n] = doc_coord(x, p->f.dx, p->f.sc);
    p->stroke[2 * p->n + 1] = doc_coord(y, p->f.dy, p->f.sc);
    p->n++;
    p->prev_x = x;
    p->prev_y = y;
}

static void redraw(pc_page_t *p)
{
    if (p->canvas) {
        render(p->canvas, CV_W, CV_H, p->path, &p->f);
    }
    update_sub(p);
}

static void swatch_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target_obj(e);
    for (int i = 0; i < 6; i++) {
        const bool on = p->swatch[i] == b;
        if (on) {
            p->color = i;
        }
        lv_obj_set_style_border_width(p->swatch[i], on ? 3 : 0, 0);
    }
}

static void size_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    p->size = (p->size + 1) % 3;
    const int d = 2 * kSizes[p->size];
    lv_obj_set_size(p->size_dot, d, d);
    lv_obj_center(p->size_dot);
}

static void undo_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    flush(p);
    if (fxp_undo(p->path)) {
        ui_files_changed();
    }
    redraw(p);
}

static void clear_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (p->clear_at && now - p->clear_at < CONFIRM_MS) {
        p->clear_at = 0;
        watch_theme_button_set(p->clear, "Limpiar", FLEX_C_DANGER);
        if (fxp_clear(p->path)) {
            ui_files_changed();
        }
        redraw(p);
        return;
    }
    p->clear_at = now;
    watch_theme_button_set(p->clear, "¿Seguro?", FLEX_C_DANGER);
}

static void pc_tick_cb(lv_timer_t *t)
{
    pc_page_t *p = lv_timer_get_user_data(t);
    if (p->clear_at && lv_tick_get() - p->clear_at >= CONFIRM_MS) {
        p->clear_at = 0;
        watch_theme_button_set(p->clear, "Limpiar", FLEX_C_DANGER);
    }
}

static void pc_free_cb(lv_event_t *e)
{
    pc_page_t *p = lv_event_get_user_data(e);
    flush(p); /* el trazo en curso no se pierde al salir */
    if (p->buf) {
        heap_caps_free(p->buf);
    }
    lv_free(p->stroke);
    lv_free(p);
}

lv_obj_t *ui_paint_canvas_create(lv_obj_t *parent)
{
    pc_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    snprintf(p->path, sizeof(p->path), "%s", s_paint_path);
    p->prev_x = p->prev_y = -1;
    p->size = 1;
    p->f.sc = 1.0f;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    char stem[FL_NAME_MAX], t[FL_NAME_MAX + 32];
    fl_stem(p->path, stem, sizeof(stem));
    ui_phone_text(stem[0] ? stem : "Paint", t, sizeof(t), FLEX_FONT_TITLE);
    watch_theme_header(page, t, back_cb, NULL);
    p->sub = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_pos(p->sub, FLEX_SAFE_X + 8, FLEX_HDR_H);
    lv_obj_add_event_cb(page, pc_free_cb, LV_EVENT_DELETE, p);

    fxp_info_t in;
    const bool ok = ui_fs_ready(NULL) && fxp_info(p->path, &in);
    p->buf = ok ? heap_caps_malloc((size_t)CV_W * CV_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    p->stroke = ok ? lv_malloc(sizeof(int16_t) * 2 * FXP_MAX_PTS) : NULL;
    if (!ok || !p->buf || !p->stroke) {
        lv_label_set_text(p->sub, !ui_fs_ready(NULL) ? "El almacenamiento no está montado"
                                  : !ok          ? "Este dibujo ya no existe o no es válido"
                                                 : "Sin memoria para el lienzo");
        lv_obj_set_style_text_color(p->sub, flex_color(FLEX_C_WARN), 0);
        return page;
    }
    p->canvas = lv_canvas_create(page);
    lv_canvas_set_buffer(p->canvas, p->buf, CV_W, CV_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(p->canvas, CV_X, CV_Y);
    lv_obj_add_flag(p->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(p->canvas, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(p->canvas, draw_cb, LV_EVENT_PRESSED, p);
    lv_obj_add_event_cb(p->canvas, draw_cb, LV_EVENT_PRESSING, p);
    lv_obj_add_event_cb(p->canvas, draw_cb, LV_EVENT_RELEASED, p);
    lv_obj_add_event_cb(p->canvas, draw_cb, LV_EVENT_PRESS_LOST, p);
    redraw(p);

    /* Barra: 6 colores + grosor; Deshacer + Limpiar */
    const int x0 = (FLEX_SCREEN_W - (6 * SWATCH + 5 * 8 + 12 + 56)) / 2;
    for (int i = 0; i < 6; i++) {
        lv_obj_t *s = lv_obj_create(page);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, SWATCH, SWATCH);
        lv_obj_set_pos(s, x0 + i * (SWATCH + 8), TOOLS_Y1);
        lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s, lv_color_make(kPal[i][0], kPal[i][1], kPal[i][2]), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s, flex_color(FLEX_C_WHITE), 0);
        lv_obj_set_style_border_width(s, i == p->color ? 3 : 0, 0);
        lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s, 4);
        lv_obj_add_event_cb(s, swatch_cb, LV_EVENT_CLICKED, p);
        p->swatch[i] = s;
    }
    p->size_btn = watch_theme_glass_button(page, x0 + 6 * (SWATCH + 8) + 4, TOOLS_Y1 - 4, 56, SWATCH + 8);
    p->size_dot = lv_obj_create(p->size_btn);
    lv_obj_remove_style_all(p->size_dot);
    lv_obj_set_style_radius(p->size_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p->size_dot, flex_color(FLEX_C_TXT), 0);
    lv_obj_set_style_bg_opa(p->size_dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(p->size_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(p->size_dot, 2 * kSizes[p->size], 2 * kSizes[p->size]);
    lv_obj_center(p->size_dot);
    lv_obj_add_event_cb(p->size_btn, size_cb, LV_EVENT_CLICKED, p);
    lv_obj_t *u = watch_theme_button(page, "Deshacer", FLEX_C_SURF2, 150, 44);
    lv_obj_set_pos(u, FLEX_SCREEN_W / 2 - 156, TOOLS_Y2);
    lv_obj_add_event_cb(u, undo_cb, LV_EVENT_CLICKED, p);
    p->clear = watch_theme_button(page, "Limpiar", FLEX_C_DANGER, 150, 44);
    lv_obj_set_pos(p->clear, FLEX_SCREEN_W / 2 + 6, TOOLS_Y2);
    lv_obj_add_event_cb(p->clear, clear_cb, LV_EVENT_CLICKED, p);
    ui_page_timer(page, pc_tick_cb, 500, p);
    return page;
}
