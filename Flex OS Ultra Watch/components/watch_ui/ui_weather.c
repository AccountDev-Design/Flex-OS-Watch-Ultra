/*
 * ui_weather — app Clima (IC_CLIMA de Flex OS Ultra) en el reloj (F9.8).
 *
 * Lee el motor watch_weather (Open-Meteo) y pinta lo mismo que la app de la
 * referencia (FlexOS_Ultra_AppWeather.h), rehecho para 410x502:
 *  - portada: ciudad, temperatura, icono, condición, máxima y mínima,
 *    sensación térmica y la frase del tiempo (wx_describe, de datos reales);
 *  - 48 horas en una tira horizontal (hora, icono de día o de noche,
 *    temperatura, gráfica y probabilidad de lluvia);
 *  - 7 días con la barra de mínima y máxima sobre la escala de la semana;
 *  - arco del sol con amanecer y atardecer;
 *  - 7 tarjetas de detalle; un dato que la API manda como null se ve
 *    "No disponible", nunca como 0;
 *  - pie con la antigüedad real y la atribución a Open-Meteo.
 * Sin datos se ve el estado vacío de la referencia (añadir ubicación o
 * reintentar); jamás un pronóstico de relleno.
 * Los iconos son procedurales como en la referencia (wxDrawIcon, WK:356-422):
 * círculos, nubes y trazos dibujados con LVGL, sin imágenes.
 * RELOJ: un botón "Actualizar ahora" en vez de deslizar para actualizar; la
 * tira de horas y los días empiezan en la hora y el día EN CURSO aunque los
 * datos sean de la caché; con un error se dice cuál, no solo "Sin conexión".
 */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "watch_net.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_weather.h"

#define WX_TICK_MS 500
#define HR_COL_W   66
#define HR_H       172
#define DAY_ROW_H  46
#define SUN_H      172
#define CARD_H     112

/* colores del icono (WK:358-362) y de los acentos del clima */
#define IC_WHITE  0xF4F8FF
#define IC_GREY   0xC4D0E4
#define IC_SUN    0xFFCE48
#define IC_MOON   0xD2DCEE
#define IC_RAIN   0x78BEFA
#define IC_SNOW   0xE2F0FF
#define IC_DARK   0x96A4BE
#define IC_BOLT   0xFFD65A
#define WX_ACCENT IC_RAIN
#define WX_SUNARC 0xFFCE60
#define BAR_COLD  0x5AAAEB /* (90,170,235) */
#define BAR_HOT   0xF5A046 /* (245,160,70) */

static lv_color_t s_sky; /* fondo de las tarjetas: el "mordisco" de la luna */
static bool s_wx_pop;    /* se añadió una ciudad: volver hasta la portada */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* ======================= Dibujo ======================= */

static void circ(lv_layer_t *L, int32_t cx, int32_t cy, int32_t r, lv_color_t c, lv_opa_t a)
{
    if (r < 1 || a == 0) {
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = a;
    d.radius = LV_RADIUS_CIRCLE;
    const lv_area_t ar = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(L, &d, &ar);
}

static void rrect(lv_layer_t *L, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rad, lv_color_t c, lv_opa_t a)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = a;
    d.radius = rad;
    const lv_area_t ar = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(L, &d, &ar);
}

static void line(lv_layer_t *L, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint32_t rgb, lv_opa_t a)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = flex_color(rgb);
    d.opa = a;
    d.width = w < 2 ? 2 : w;
    d.round_start = d.round_end = 1;
    d.p1.x = x0;
    d.p1.y = y0;
    d.p2.x = x1;
    d.p2.y = y1;
    lv_draw_line(L, &d);
}

static void text_at(lv_layer_t *L, const lv_font_t *f, uint32_t rgb, lv_text_align_t al, int32_t x1, int32_t y1,
                    int32_t x2, const char *t)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = flex_color(rgb);
    d.align = al;
    d.text = t;
    d.text_local = 1; /* el texto vive en la pila: LVGL lo copia */
    const lv_area_t ar = {x1, y1, x2, y1 + lv_font_get_line_height(f)};
    lv_draw_label(L, &d, &ar);
}

/* Primitivas de WK:312-353 */
static void cloud(lv_layer_t *L, int32_t cx, int32_t cy, int32_t w, uint32_t rgb, lv_opa_t a)
{
    int32_t r = w / 6;
    if (r < 2) {
        r = 2;
    }
    const lv_color_t c = flex_color(rgb);
    rrect(L, cx - w / 2, cy - r / 2, w, r + r / 2 + 1, r, c, a);
    circ(L, cx - r, cy - r / 2, r, c, a);
    circ(L, cx + r * 3 / 2, cy - r / 3, r * 3 / 4, c, a);
    circ(L, cx + r / 4, cy - r, r * 5 / 4, c, a);
}

static void sun(lv_layer_t *L, int32_t cx, int32_t cy, int32_t r, lv_opa_t a, bool rays)
{
    const lv_color_t c = flex_color(IC_SUN);
    circ(L, cx, cy, r + r / 2, c, a / 6);
    circ(L, cx, cy, r + r / 4, c, a / 4);
    circ(L, cx, cy, r, c, a);
    if (rays) {
        for (int k = 0; k < 8; k++) {
            const float ang = (float)k * 0.785398163f;
            const float c0 = cosf(ang), s0 = sinf(ang);
            line(L, cx + (int32_t)lroundf(c0 * (r + r / 3)), cy + (int32_t)lroundf(s0 * (r + r / 3)),
                 cx + (int32_t)lroundf(c0 * (r + r * 3 / 4)), cy + (int32_t)lroundf(s0 * (r + r * 3 / 4)), r / 6,
                 IC_SUN, a);
        }
    }
}

static void moon(lv_layer_t *L, int32_t cx, int32_t cy, int32_t r, lv_opa_t a)
{
    const lv_color_t c = flex_color(IC_MOON);
    circ(L, cx, cy, r + r / 3, c, a / 7);
    circ(L, cx, cy, r, c, a);
    circ(L, cx + r / 2, cy - r / 3, r, s_sky, a); /* mordisco: fase creciente */
}

static void drop(lv_layer_t *L, int32_t x, int32_t y, int32_t len, lv_opa_t a)
{
    line(L, x, y, x - len / 3, y + len, 2, IC_RAIN, a);
}

static void flake(lv_layer_t *L, int32_t x, int32_t y, int32_t r, lv_opa_t a)
{
    line(L, x - r, y, x + r, y, 2, IC_SNOW, a);
    line(L, x, y - r, x, y + r, 2, IC_SNOW, a);
    line(L, x - r * 2 / 3, y - r * 2 / 3, x + r * 2 / 3, y + r * 2 / 3, 2, IC_SNOW, a);
    line(L, x - r * 2 / 3, y + r * 2 / 3, x + r * 2 / 3, y - r * 2 / 3, 2, IC_SNOW, a);
}

static void bolt(lv_layer_t *L, int32_t x, int32_t y, int32_t h, lv_opa_t a)
{
    const int32_t w = h / 2, t = h / 8;
    line(L, x + w / 2, y, x - w / 4, y + h / 2, t, IC_BOLT, a);
    line(L, x - w / 4, y + h / 2, x + w / 4, y + h / 2, t, IC_BOLT, a);
    line(L, x + w / 4, y + h / 2, x - w / 2, y + h, t, IC_BOLT, a);
}

/* wxDrawIcon: r = radio nominal (el icono ocupa unos 2,4 r de ancho) */
static void icon(lv_layer_t *L, int32_t cx, int32_t cy, int32_t r, uint8_t vis, bool day, lv_opa_t a)
{
    switch (vis) {
    case WXV_CLEAR:
        if (day) {
            sun(L, cx, cy, r, a, true);
        } else {
            moon(L, cx, cy, r, a);
        }
        break;
    case WXV_MOSTLY_CLEAR:
        if (day) {
            sun(L, cx - r / 3, cy - r / 4, r * 4 / 5, a, true);
        } else {
            moon(L, cx - r / 3, cy - r / 4, r * 4 / 5, a);
        }
        cloud(L, cx + r / 2, cy + r / 2, r * 7 / 5, IC_WHITE, (lv_opa_t)(a * 3 / 4));
        break;
    case WXV_PARTLY:
        if (day) {
            sun(L, cx - r / 2, cy - r / 2, r * 3 / 4, a, false);
        } else {
            moon(L, cx - r / 2, cy - r / 2, r * 3 / 4, a);
        }
        cloud(L, cx + r / 4, cy + r / 3, r * 11 / 5, IC_WHITE, a);
        break;
    case WXV_CLOUDY:
        cloud(L, cx - r / 3, cy - r / 3, r * 8 / 5, IC_GREY, a);
        cloud(L, cx + r / 3, cy + r / 3, r * 11 / 5, IC_WHITE, a);
        break;
    case WXV_FOG:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_GREY, a);
        for (int i = 0; i < 3; i++) {
            line(L, cx - r, cy + r / 2 + i * (r / 2), cx + r, cy + r / 2 + i * (r / 2), r / 8, IC_DARK, a);
        }
        break;
    case WXV_DRIZZLE:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_WHITE, a);
        for (int i = 0; i < 2; i++) {
            drop(L, cx - r / 2 + i * r, cy + r / 2, r / 2, a);
        }
        break;
    case WXV_RAIN:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_WHITE, a);
        for (int i = 0; i < 3; i++) {
            drop(L, cx - r + i * r, cy + r / 2, r * 2 / 3, a);
        }
        break;
    case WXV_HEAVY_RAIN:
        cloud(L, cx, cy - r / 2, r * 12 / 5, IC_GREY, a);
        for (int i = 0; i < 4; i++) {
            drop(L, cx - r - r / 4 + i * (r * 3) / 4, cy + r / 2, r, a);
        }
        break;
    case WXV_SLEET:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_WHITE, a);
        drop(L, cx - r / 2, cy + r / 2, r * 2 / 3, a);
        flake(L, cx + r / 2, cy + r, r / 3, a);
        break;
    case WXV_SNOW:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_WHITE, a);
        flake(L, cx - r / 2, cy + r, r / 3, a);
        flake(L, cx + r / 2, cy + r * 3 / 4, r / 3, a);
        break;
    case WXV_THUNDER:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_GREY, a);
        bolt(L, cx, cy + r / 3, r, a);
        break;
    case WXV_HAIL:
        cloud(L, cx, cy - r / 2, r * 11 / 5, IC_GREY, a);
        bolt(L, cx - r / 3, cy + r / 3, r, a);
        circ(L, cx + r / 2, cy + r, r / 4, flex_color(IC_SNOW), a);
        break;
    default:
        cloud(L, cx, cy, r * 11 / 5, IC_WHITE, a);
        break;
    }
}

/* Objeto que pinta un icono: radio, estado visual y día van en user_data. */
static void icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    const uintptr_t k = (uintptr_t)lv_obj_get_user_data(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    icon(lv_event_get_layer(e), (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, (int32_t)(k & 0xFF), (uint8_t)((k >> 8) & 0xF),
         (k >> 12) & 1, LV_OPA_COVER);
}

static lv_obj_t *icon_obj(lv_obj_t *parent, int32_t size, int32_t r, uint8_t vis, bool day)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, (void *)(uintptr_t)((uint32_t)r | (uint32_t)vis << 8 | (uint32_t)day << 12));
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

/* ======================= Formatos ======================= */

static long deg(float t)
{
    return lroundf(t);
}

static void fmt_t10(char *b, size_t n, int16_t t10)
{
    if (t10 == WX_NOVAL_I16) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%ld°", lroundf(t10 / 10.0f));
    }
}

static void fmt_clock(char *b, size_t n, int32_t local, bool h24)
{
    int h, m;
    wx_hm(local, &h, &m);
    if (h24) {
        snprintf(b, n, "%d:%02d", h, m);
    } else {
        snprintf(b, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "a. m." : "p. m.");
    }
}

static void fmt_hour(char *b, size_t n, int32_t local, bool h24)
{
    int h, m;
    wx_hm(local, &h, &m);
    if (h24) {
        snprintf(b, n, "%d:00", h);
    } else {
        snprintf(b, n, "%d %s", h % 12 ? h % 12 : 12, h < 12 ? "a. m." : "p. m.");
    }
}

static int32_t day_sec(int32_t t)
{
    return (int32_t)(((t % 86400) + 86400) % 86400);
}

/* ======================= Página principal ======================= */

typedef struct {
    lv_obj_t *list, *foot, *hscroll;
    wx_data_t *w;
    watch_weather_status_t st;
    uint32_t gen;
    uint32_t key; /* forma de lo construido (vacío, cargando, datos...) */
    bool h24;
    int h0, d0;   /* primera hora y primer día que aún no han pasado */
    int16_t tmin10, tmax10; /* escala de la gráfica de horas */
    int16_t wmin10, wmax10; /* escala de la semana */
} wx_page_t;

static void locs_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_wx_locs_create);
}

static void search_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_wx_search_create);
}

static void retry_cb(lv_event_t *e)
{
    (void)e;
    watch_weather_refresh(true);
}

static bool is_day_at(const wx_data_t *w, int32_t t)
{
    if (!(w->have & WXF_SUN)) {
        return w->is_day;
    }
    const int32_t s = day_sec(t + w->utc_offset), sr = day_sec(w->sunrise + w->utc_offset),
                  ss = day_sec(w->sunset + w->utc_offset);
    return s >= sr && s < ss;
}

static void hourly_draw_cb(lv_event_t *e)
{
    wx_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    const wx_data_t *w = p->w;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    /* solo las columnas a la vista (LVGL recorta, pero así no se prepara nada de más) */
    const int32_t sx = lv_obj_get_scroll_x(p->hscroll), vw = lv_obj_get_width(p->hscroll);
    int i0 = p->h0 + (int)(sx / HR_COL_W) - 1, i1 = p->h0 + (int)((sx + vw) / HR_COL_W) + 1;
    i0 = i0 < p->h0 ? p->h0 : i0;
    i1 = i1 >= w->hour_count ? w->hour_count - 1 : i1;
    const int32_t gy0 = a.y1 + 112, gy1 = a.y1 + 138;
    int32_t span = p->tmax10 - p->tmin10;
    span = span < 10 ? 10 : span;
#define GY(t) (gy1 - (int32_t)((t) - p->tmin10) * (gy1 - gy0) / span)
    for (int i = i0; i <= i1; i++) {
        const wx_hour_t *h = &w->hours[i];
        const int32_t cx = a.x1 + (i - p->h0) * HR_COL_W + HR_COL_W / 2;
        char b[24];
        if (i == p->h0) {
            snprintf(b, sizeof(b), "Ahora");
        } else {
            fmt_hour(b, sizeof(b), h->t + w->utc_offset, p->h24);
        }
        text_at(L, FLEX_FONT_SMALL, FLEX_C_TXT2, LV_TEXT_ALIGN_CENTER, cx - HR_COL_W / 2, a.y1 + 4,
                cx + HR_COL_W / 2 - 1, b);
        icon(L, cx, a.y1 + 52, 13, wx_visual(h->code), is_day_at(w, h->t), LV_OPA_COVER);
        fmt_t10(b, sizeof(b), h->temp10);
        text_at(L, FLEX_FONT_BODY, FLEX_C_TXT, LV_TEXT_ALIGN_CENTER, cx - HR_COL_W / 2, a.y1 + 74,
                cx + HR_COL_W / 2 - 1, b);
        if (h->temp10 != WX_NOVAL_I16) {
            if (i + 1 < w->hour_count && w->hours[i + 1].temp10 != WX_NOVAL_I16) {
                line(L, cx, GY(h->temp10), cx + HR_COL_W, GY(w->hours[i + 1].temp10), 2, WX_ACCENT, LV_OPA_COVER);
            }
            circ(L, cx, GY(h->temp10), 3, flex_color(FLEX_C_WHITE), LV_OPA_COVER);
        }
        if (h->pop != WX_NOVAL_U8) {
            snprintf(b, sizeof(b), "%d %%", h->pop);
            drop(L, cx - 16, a.y1 + 150, 8, LV_OPA_COVER);
            text_at(L, FLEX_FONT_SMALL, FLEX_C_MUTE, LV_TEXT_ALIGN_LEFT, cx - 8, a.y1 + 146, cx + HR_COL_W / 2, b);
        }
    }
#undef GY
}

/* RELOJ: abreviados (la referencia, a 480 px, los escribe enteros): así la
 * barra de la semana conserva casi el largo de la referencia */
static const char *const kWd[7] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};

static void daily_draw_cb(lv_event_t *e)
{
    wx_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    const wx_data_t *w = p->w;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int32_t W = lv_area_get_width(&a);
    const int32_t bx = a.x1 + 190, bw = W - 190 - 50; /* barra entre la mínima y la máxima */
    int32_t span = p->wmax10 - p->wmin10;
    span = span < 10 ? 10 : span;
    const lv_font_t *f = FLEX_FONT_SMALL;
    const int32_t fh = lv_font_get_line_height(f);
    for (int i = p->d0; i < w->day_count; i++) {
        const wx_day_t *d = &w->days[i];
        const int32_t ry = a.y1 + (i - p->d0) * DAY_ROW_H, my = ry + DAY_ROW_H / 2;
        if (i > p->d0) {
            line(L, a.x1, ry, a.x2, ry, 1, FLEX_C_DIVIDER, LV_OPA_COVER);
        }
        text_at(L, f, FLEX_C_TXT, LV_TEXT_ALIGN_LEFT, a.x1, my - fh / 2, a.x1 + 52,
                i == p->d0 ? "Hoy" : kWd[wx_weekday(d->date + w->utc_offset)]);
        char b[16];
        if (d->pop != WX_NOVAL_U8 && d->pop != 0) {
            snprintf(b, sizeof(b), "%d%%", d->pop);
            drop(L, a.x1 + 60, my - 5, 9, LV_OPA_COVER);
            text_at(L, f, WX_ACCENT, LV_TEXT_ALIGN_LEFT, a.x1 + 66, my - fh / 2, a.x1 + 106, b);
        }
        icon(L, a.x1 + 124, my, 12, wx_visual(d->code), true, LV_OPA_COVER); /* siempre de día */
        fmt_t10(b, sizeof(b), d->min10);
        text_at(L, f, FLEX_C_TXT2, LV_TEXT_ALIGN_RIGHT, a.x1 + 140, my - fh / 2, bx - 8, b);
        fmt_t10(b, sizeof(b), d->max10);
        text_at(L, f, FLEX_C_TXT, LV_TEXT_ALIGN_RIGHT, bx + bw + 4, my - fh / 2, a.x2, b);
        rrect(L, bx, my - 3, bw, 6, 3, flex_color(FLEX_C_TRACK), LV_OPA_COVER);
        if (d->min10 != WX_NOVAL_I16 && d->max10 != WX_NOVAL_I16) {
            int32_t x0 = bx + (int32_t)(d->min10 - p->wmin10) * bw / span;
            int32_t x1 = bx + (int32_t)(d->max10 - p->wmin10) * bw / span;
            if (x1 - x0 < 8) { /* 8 px como mínimo (AW:323) */
                x1 = x0 + 8 > bx + bw ? bx + bw : x0 + 8;
                x0 = x1 - 8;
            }
            /* degradado según la posición absoluta en la barra */
            lv_draw_rect_dsc_t g;
            lv_draw_rect_dsc_init(&g);
            g.radius = 3;
            g.bg_opa = LV_OPA_COVER;
            const lv_color_t c0 = flex_color(BAR_COLD), c1 = flex_color(BAR_HOT);
            const lv_color_t cs[2] = {lv_color_mix(c1, c0, (uint8_t)((x0 - bx) * 255 / bw)),
                                      lv_color_mix(c1, c0, (uint8_t)((x1 - bx) * 255 / bw))};
            lv_grad_init_stops(&g.bg_grad, cs, NULL, NULL, 2);
            lv_grad_horizontal_init(&g.bg_grad);
            const lv_area_t ar = {x0, my - 3, x1 - 1, my + 2};
            lv_draw_rect(L, &g, &ar);
        }
    }
}

static void sun_draw_cb(lv_event_t *e)
{
    wx_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    const wx_data_t *w = p->w;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int32_t cx = (a.x1 + a.x2) / 2, cy = a.y1 + 112, r = 100;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = r;
    d.width = 3;
    d.start_angle = 180;
    d.end_angle = 360;
    d.color = flex_color(FLEX_C_BORDER);
    lv_draw_arc(L, &d);
    line(L, cx - r - 12, cy, cx + r + 12, cy, 1, FLEX_C_BORDER, 150);
    const int64_t now = p->st.now_utc;
    if (now && w->sunset > w->sunrise && now >= w->sunrise && now <= w->sunset) {
        const float prog = (float)(now - w->sunrise) / (float)(w->sunset - w->sunrise);
        const int32_t ang = 180 + (int32_t)lroundf(prog * 180.0f);
        d.color = flex_color(WX_SUNARC);
        d.end_angle = ang > 180 ? ang : 181;
        lv_draw_arc(L, &d);
        const float rad = (float)ang * 0.0174532925f;
        const int32_t sx = cx + (int32_t)lroundf(cosf(rad) * r), sy = cy + (int32_t)lroundf(sinf(rad) * r);
        circ(L, sx, sy, 12, flex_color(0xFFD66E), LV_OPA_COVER / 4);
        circ(L, sx, sy, 7, flex_color(0xFFE08C), LV_OPA_COVER);
    }
    char b[24];
    text_at(L, FLEX_FONT_SMALL, FLEX_C_MUTE, LV_TEXT_ALIGN_LEFT, a.x1, cy + 8, cx, "Amanecer");
    text_at(L, FLEX_FONT_SMALL, FLEX_C_MUTE, LV_TEXT_ALIGN_RIGHT, cx, cy + 8, a.x2, "Atardecer");
    fmt_clock(b, sizeof(b), w->sunrise + w->utc_offset, p->h24);
    text_at(L, FLEX_FONT_BODY, FLEX_C_TXT, LV_TEXT_ALIGN_LEFT, a.x1, cy + 28, cx, b);
    fmt_clock(b, sizeof(b), w->sunset + w->utc_offset, p->h24);
    text_at(L, FLEX_FONT_BODY, FLEX_C_TXT, LV_TEXT_ALIGN_RIGHT, cx, cy + 28, a.x2, b);
}

static lv_obj_t *draw_obj(lv_obj_t *parent, int32_t w, int32_t h, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_DRAW_MAIN, ud);
    return o;
}

/* Una línea con "..." (DOTS solo recorta con alto fijo) */
static lv_obj_t *one_line(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, const char *t)
{
    lv_obj_t *l = watch_theme_label(parent, f, rgb, t);
    lv_obj_set_size(l, LV_PCT(100), lv_font_get_line_height(f));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

static lv_obj_t *wrap_label(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, const char *t, bool center)
{
    lv_obj_t *l = watch_theme_label(parent, f, rgb, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    if (center) {
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    return l;
}

static lv_obj_t *tile(lv_obj_t *list)
{
    lv_obj_t *t = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t, 4, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    s_sky = lv_obj_get_style_bg_color(t, LV_PART_MAIN);
    return t;
}

static void build_hero(wx_page_t *p)
{
    const wx_data_t *w = p->w;
    lv_obj_t *t = tile(p->list);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE); /* tocar la ciudad abre las ubicaciones (AW:939) */
    lv_obj_add_event_cb(t, locs_open_cb, LV_EVENT_CLICKED, NULL);
    char b[160];
    ui_phone_text(w->loc.name, b, sizeof(b), FLEX_FONT_TITLE);
    one_line(t, FLEX_FONT_TITLE, FLEX_C_TXT, b);
    if (w->loc.region[0]) {
        ui_phone_text(w->loc.region, b, sizeof(b), FLEX_FONT_SMALL);
        one_line(t, FLEX_FONT_SMALL, FLEX_C_MUTE, b);
    }
    /* temperatura gigante (cifras de la esfera) + anillo de grado + icono */
    lv_obj_t *row = lv_obj_create(t);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 128);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    snprintf(b, sizeof(b), "%ld", deg(w->temp));
    lv_obj_t *temp = watch_theme_label(row, FLEX_FONT_CLOCK, FLEX_C_TXT, b);
    lv_obj_add_flag(temp, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *ring = lv_obj_create(row);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 22, 22);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 5, 0);
    lv_obj_set_style_border_color(ring, flex_color(FLEX_C_TXT), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(ring, 20, 0);
    lv_obj_set_style_margin_left(ring, 4, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *sp = lv_obj_create(row);
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_t *ic = icon_obj(row, 116, 40, wx_visual(w->code), w->is_day);
    lv_obj_set_style_translate_y(ic, 4, 0);
    watch_theme_label(t, FLEX_FONT_TITLE, FLEX_C_TXT, wx_cond_name(w->code));
    if (w->have & WXF_MINMAX) {
        snprintf(b, sizeof(b), "Máx. %ld°  ·  Mín. %ld°", deg(w->tmax), deg(w->tmin));
        watch_theme_label(t, FLEX_FONT_BODY, FLEX_C_TXT2, b);
    }
    if (w->have & WXF_FEELS) {
        snprintf(b, sizeof(b), "Sensación térmica %ld°", deg(w->feels));
        watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    }
    wx_describe(w, p->st.now_local, b, sizeof(b));
    if (b[0]) {
        lv_obj_t *d = wrap_label(t, FLEX_FONT_SMALL, FLEX_C_TXT, b, false);
        lv_obj_set_style_pad_top(d, 6, 0);
    }
}

static void build_hourly(wx_page_t *p)
{
    const wx_data_t *w = p->w;
    if (w->hour_count - p->h0 <= 0) {
        return;
    }
    p->tmin10 = INT16_MAX;
    p->tmax10 = INT16_MIN;
    for (int i = p->h0; i < w->hour_count; i++) {
        const int16_t v = w->hours[i].temp10;
        if (v != WX_NOVAL_I16) {
            p->tmin10 = v < p->tmin10 ? v : p->tmin10;
            p->tmax10 = v > p->tmax10 ? v : p->tmax10;
        }
    }
    if (p->tmin10 > p->tmax10) {
        p->tmin10 = p->tmax10 = 0;
    }
    lv_obj_t *t = tile(p->list);
    lv_obj_set_style_pad_hor(t, 0, 0);
    p->hscroll = lv_obj_create(t);
    lv_obj_remove_style_all(p->hscroll);
    lv_obj_set_size(p->hscroll, LV_PCT(100), HR_H);
    lv_obj_set_scroll_dir(p->hscroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(p->hscroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(p->hscroll, 8, 0);
    lv_obj_add_flag(p->hscroll, LV_OBJ_FLAG_CLICKABLE);
    draw_obj(p->hscroll, (w->hour_count - p->h0) * HR_COL_W, HR_H, hourly_draw_cb, p);
    lv_obj_t *f = watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_MUTE, "Pronóstico de 48 horas  >");
    lv_obj_set_width(f, LV_PCT(100));
    lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_right(f, 16, 0);
}

static void build_daily(wx_page_t *p)
{
    const wx_data_t *w = p->w;
    const int n = w->day_count - p->d0;
    if (n <= 0) {
        return;
    }
    p->wmin10 = INT16_MAX;
    p->wmax10 = INT16_MIN;
    for (int i = p->d0; i < w->day_count; i++) {
        const wx_day_t *d = &w->days[i];
        if (d->min10 != WX_NOVAL_I16) {
            p->wmin10 = d->min10 < p->wmin10 ? d->min10 : p->wmin10;
        }
        if (d->max10 != WX_NOVAL_I16) {
            p->wmax10 = d->max10 > p->wmax10 ? d->max10 : p->wmax10;
        }
    }
    if (p->wmin10 > p->wmax10) {
        p->wmin10 = p->wmax10 = 0;
    }
    lv_obj_t *t = tile(p->list);
    watch_theme_label(t, FLEX_FONT_BODY, FLEX_C_TXT, "Pronóstico");
    draw_obj(t, LV_PCT(100), n * DAY_ROW_H, daily_draw_cb, p);
}

static void build_sun(wx_page_t *p)
{
    lv_obj_t *t = tile(p->list);
    if (!(p->w->have & WXF_SUN)) {
        watch_theme_label(t, FLEX_FONT_BODY, FLEX_C_TXT, "Amanecer");
        watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, "No disponible");
        return;
    }
    watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, "No te pierdas el atardecer");
    draw_obj(t, LV_PCT(100), SUN_H, sun_draw_cb, p);
}

static void card(lv_obj_t *grid, const char *title, bool ok, const char *value, const char *hint)
{
    lv_obj_t *c = watch_theme_tile(grid, 0, 0, LV_PCT(48), CARD_H);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 2, 0);
    watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT2, title);
    one_line(c, ok ? FLEX_FONT_TITLE : FLEX_FONT_BODY, ok ? FLEX_C_TXT : FLEX_C_MUTE, ok ? value : "No disponible");
    if (ok && hint && hint[0]) {
        one_line(c, FLEX_FONT_SMALL, FLEX_C_MUTE, hint);
    }
}

static void build_details(wx_page_t *p)
{
    const wx_data_t *w = p->w;
    lv_obj_t *g = lv_obj_create(p->list);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(g, 10, 0);
    char v[32], h[40];
    const uint16_t f = w->have;
    snprintf(v, sizeof(v), "%ld %%", deg(w->humidity));
    if (f & WXF_FEELS) {
        snprintf(h, sizeof(h), "Sensación %ld°", deg(w->feels));
    } else {
        h[0] = 0;
    }
    card(g, "Humedad", f & WXF_HUMIDITY, v, h);
    snprintf(v, sizeof(v), "%ld km/h", deg(w->wind_speed));
    if (f & WXF_GUST) {
        snprintf(h, sizeof(h), "Rachas %ld km/h", deg(w->wind_gust));
    } else if (f & WXF_WINDDIR) {
        snprintf(h, sizeof(h), "Dirección %s", wx_wind_dir(w->wind_dir));
    } else {
        h[0] = 0;
    }
    card(g, "Viento", f & WXF_WIND, v, h);
    snprintf(v, sizeof(v), "%ld", deg(w->uv));
    card(g, "Índice UV", f & WXF_UV, v, wx_uv_label(w->uv));
    if (w->visibility >= 10000.0f) {
        snprintf(v, sizeof(v), "%ld km", lroundf(w->visibility / 1000.0f));
    } else {
        snprintf(v, sizeof(v), "%.1f km", (double)(w->visibility / 1000.0f));
    }
    card(g, "Visibilidad", f & WXF_VIS, v, NULL);
    snprintf(v, sizeof(v), "%.1f mm", (double)w->precip);
    if (f & WXF_POP) {
        snprintf(h, sizeof(h), "Probabilidad %d %%", w->pop);
    } else {
        h[0] = 0;
    }
    card(g, "Precipitación", f & WXF_PRECIP, v, h);
    snprintf(v, sizeof(v), "%ld°", deg(w->feels));
    if (f & WXF_CLOUD) {
        snprintf(h, sizeof(h), "%ld %% nubes", deg(w->cloud));
    } else {
        h[0] = 0;
    }
    card(g, "Sensación térmica", f & WXF_FEELS, v, h);
    snprintf(v, sizeof(v), "%ld hPa", deg(w->pressure));
    card(g, "Presión", f & WXF_PRESSURE, v, NULL);
}

static void foot_text(wx_page_t *p, char *b, size_t n)
{
    const watch_weather_status_t *s = &p->st;
    size_t o = 0;
    if (s->status == WXS_UPDATING || s->status == WXS_LOADING) {
        snprintf(b, n, "Actualizando...");
        return;
    }
    const int32_t age = s->age_s;
    if (age >= 0 && age < 60) {
        o = (size_t)snprintf(b, n, "Actualizado ahora");
    } else if (age >= 0 && age < 90 * 60) {
        o = (size_t)snprintf(b, n, "Actualizado hace %ld min", (long)(age / 60));
    } else if (age >= 0 && age < 48 * 3600) {
        o = (size_t)snprintf(b, n, "Actualizado hace %ld h", (long)(age / 3600)); /* RELOJ */
    } else if (age >= 0) {
        o = (size_t)snprintf(b, n, "Actualizado hace %ld días", (long)(age / 86400));
    } else {
        char c[24];
        fmt_clock(c, sizeof(c), p->w->obs_time + p->w->utc_offset, p->h24);
        o = (size_t)snprintf(b, n, "Actualizado a las %s", c);
    }
    if (s->status == WXS_STALE && o < n) {
        watch_net_status_t ns;
        watch_net_get(&ns);
        const char *why = s->err != WXE_NONE ? wx_error_text(s->err) : "Sin conexión";
        if (ns.state != NET_CONNECTED) {
            why = "Sin conexión";
        }
        snprintf(b + o, n - o, "  ·  %s", why);
    }
}

static void build_footer(wx_page_t *p)
{
    lv_obj_t *b1 = watch_theme_button(p->list, "Mis ubicaciones", FLEX_C_SURF2, LV_PCT(100), 56);
    lv_obj_add_event_cb(b1, locs_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *b2 = watch_theme_button(p->list, "Actualizar ahora", FLEX_C_SURF2, LV_PCT(100), 56);
    lv_obj_add_event_cb(b2, retry_cb, LV_EVENT_CLICKED, NULL);
    char t[96];
    foot_text(p, t, sizeof(t));
    p->foot = wrap_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2, t, true);
    wrap_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, "Datos meteorológicos: Open-Meteo", true);
}

static void build_empty(wx_page_t *p)
{
    const watch_weather_status_t *s = &p->st;
    lv_obj_t *box = lv_obj_create(p->list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 8, 0);
    s_sky = flex_color(FLEX_C_PAGE);
    lv_obj_t *ic = icon_obj(box, 140, 46, s->nloc ? WXV_CLOUDY : WXV_PARTLY, true);
    lv_obj_set_style_opa(ic, LV_OPA_80, 0);
    if (s->status == WXS_LOADING) {
        lv_obj_t *sp = lv_spinner_create(box);
        lv_obj_set_size(sp, 40, 40);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR);
        wrap_label(box, FLEX_FONT_BODY, FLEX_C_TXT2, "Cargando el pronóstico...", true);
        return;
    }
    wrap_label(box, FLEX_FONT_TITLE, FLEX_C_TXT, "Sin datos meteorológicos", true);
    wrap_label(box, FLEX_FONT_SMALL, FLEX_C_TXT2,
               s->nloc ? "Conéctate a Wi-Fi para obtener el pronóstico." : "Añade una ciudad para ver su clima",
               true);
    if (s->nloc && s->err != WXE_NONE) {
        wrap_label(box, FLEX_FONT_SMALL, FLEX_C_WARN, wx_error_text(s->err), true);
    }
    lv_obj_t *b1 = watch_theme_button(p->list, s->nloc ? "Reintentar" : "Añadir ubicación", FLEX_C_PRIMARY,
                                      LV_PCT(100), 60);
    lv_obj_add_event_cb(b1, s->nloc ? retry_cb : search_open_cb, LV_EVENT_CLICKED, NULL);
    if (s->nloc) {
        lv_obj_t *b2 = watch_theme_button(p->list, "Mis ubicaciones", FLEX_C_SURF2, LV_PCT(100), 60);
        lv_obj_add_event_cb(b2, locs_open_cb, LV_EVENT_CLICKED, NULL);
    }
    wrap_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, "Datos meteorológicos: Open-Meteo", true);
}

static uint32_t shape_key(const watch_weather_status_t *s)
{
    if (s->have) {
        return 1;
    }
    return 2u | (s->status == WXS_LOADING) << 2 | (s->nloc > 0) << 3 | (uint32_t)s->err << 4;
}

static void build(wx_page_t *p)
{
    const int32_t y = lv_obj_get_scroll_y(p->list);
    lv_obj_clean(p->list);
    p->foot = p->hscroll = NULL;
    p->key = shape_key(&p->st);
    p->gen = p->st.gen;
    if (!p->st.have || !watch_weather_data(p->w)) {
        build_empty(p);
        return;
    }
    const wx_data_t *w = p->w;
    /* la tira y los días empiezan en lo que aún no ha pasado (caché vieja) */
    const int64_t now = p->st.now_utc;
    p->h0 = 0;
    while (now && p->h0 < w->hour_count && w->hours[p->h0].t + 3600 <= now) {
        p->h0++;
    }
    p->d0 = 0;
    while (now && p->d0 < w->day_count && w->days[p->d0].date + 86400 <= now) {
        p->d0++;
    }
    build_hero(p);
    build_hourly(p);
    build_daily(p);
    build_sun(p);
    build_details(p);
    build_footer(p);
    lv_obj_update_layout(p->list);
    lv_obj_scroll_to_y(p->list, y, LV_ANIM_OFF); /* rehacer no mueve lo que se está leyendo */
}

static void wx_tick_cb(lv_timer_t *t)
{
    wx_page_t *p = lv_timer_get_user_data(t);
    watch_weather_get(&p->st);
    if (s_wx_pop && ui_nav_top() == lv_obj_get_parent(p->list)) {
        s_wx_pop = false; /* la búsqueda ya volvió hasta aquí */
    }
    if (p->st.gen != p->gen || shape_key(&p->st) != p->key) {
        build(p);
        return;
    }
    if (p->foot) {
        char b[96];
        foot_text(p, b, sizeof(b));
        if (strcmp(lv_label_get_text(p->foot), b) != 0) {
            lv_label_set_text(p->foot, b);
        }
    }
}

lv_obj_t *ui_weather_create(lv_obj_t *parent)
{
    wx_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->w = lv_malloc(sizeof(wx_data_t)); /* 1,3 KB: con LV_USE_CLIB_MALLOC, en el montón del sistema */
    LV_ASSERT_MALLOC(p->w);
    watch_settings_t ws;
    watch_settings_get(&ws);
    p->h24 = ws.h24;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Clima", back_cb, NULL);
    p->list = watch_theme_list(page);
    watch_weather_refresh(false); /* al abrir: solo si lo último tiene más de 5 min */
    watch_weather_get(&p->st);
    build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p->w);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, wx_tick_cb, WX_TICK_MS, p);
    return page;
}

/* ======================= Mis ubicaciones ======================= */

typedef struct {
    lv_obj_t *list;
    watch_weather_status_t st;
    uint32_t sig;
    int armed; /* fila con "¿Eliminar?" pendiente de confirmar, -1 = ninguna */
} wl_page_t;

static uint32_t locs_sig(const watch_weather_status_t *s)
{
    uint32_t h = (uint32_t)s->nloc * 31u + (uint32_t)(s->sel + 1);
    for (int i = 0; i < s->nloc; i++) {
        for (const char *c = s->locs[i].name; *c; c++) {
            h = h * 33u + (uint8_t)*c;
        }
    }
    return h;
}

static void wl_build(wl_page_t *p);

static void loc_row_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    watch_weather_select((uint8_t)i);
    ui_nav_back(); /* elegir y volver a la vista principal (AW:1036) */
}

static void loc_del_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target_obj(e);
    wl_page_t *p = lv_obj_get_user_data(btn);
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (p->armed != i) { /* primer toque: se pide confirmación */
        p->armed = i;
        wl_build(p);
        return;
    }
    p->armed = -1;
    watch_weather_remove((uint8_t)i);
    watch_weather_get(&p->st);
    wl_build(p);
}

static void wl_build(wl_page_t *p)
{
    lv_obj_clean(p->list);
    p->sig = locs_sig(&p->st);
    char b[160];
    for (int i = 0; i < p->st.nloc; i++) {
        const wx_loc_t *l = &p->st.locs[i];
        lv_obj_t *row = watch_theme_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 78, 0);
        lv_obj_set_style_pad_ver(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        if (i == p->st.sel) { /* la elegida: borde de acento (AW:600) */
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_FULL, 0);
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, flex_color(WX_ACCENT), 0);
        }
        lv_obj_add_event_cb(row, loc_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, flex_color(i == p->st.sel ? WX_ACCENT : FLEX_C_MUTE), 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_remove_style_all(col);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        ui_phone_text(l->name, b, sizeof(b), FLEX_FONT_BODY);
        one_line(col, FLEX_FONT_BODY, FLEX_C_TXT, b);
        if (l->region[0]) { /* al lado del botón: mejor en dos líneas que recortada */
            ui_phone_text(l->region, b, sizeof(b), FLEX_FONT_SMALL);
            wrap_label(col, FLEX_FONT_SMALL, FLEX_C_MUTE, b, false);
        }
        const bool armed = p->armed == i;
        lv_obj_t *del = watch_theme_button(row, armed ? "¿Seguro?" : "Quitar", armed ? FLEX_C_DANGER : FLEX_C_SURF2,
                                           armed ? 110 : 92, 48);
        lv_obj_set_user_data(del, p);
        lv_obj_add_event_cb(del, loc_del_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (p->st.nloc == 0) {
        wrap_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2, "Añade una ciudad para ver su clima", true);
    }
    if (p->st.nloc < WX_LOCS) {
        lv_obj_t *add = watch_theme_button(p->list, "Añadir ubicación", FLEX_C_PRIMARY, LV_PCT(100), 56);
        lv_obj_add_event_cb(add, search_open_cb, LV_EVENT_CLICKED, NULL);
    } else {
        wrap_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, "Máximo 5 ubicaciones: quita una para añadir otra.", true);
    }
}

static void wl_tick_cb(lv_timer_t *t)
{
    wl_page_t *p = lv_timer_get_user_data(t);
    lv_obj_t *page = lv_obj_get_parent(p->list);
    if (s_wx_pop && ui_nav_top() == page && !ui_nav_busy()) {
        s_wx_pop = false; /* se añadió una ciudad desde la búsqueda */
        ui_nav_back();
        return;
    }
    watch_weather_get(&p->st);
    if (locs_sig(&p->st) != p->sig) {
        p->armed = -1;
        wl_build(p);
    }
}

lv_obj_t *ui_wx_locs_create(lv_obj_t *parent)
{
    wl_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->armed = -1;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Mis ubicaciones", back_cb, NULL);
    p->list = watch_theme_list(page);
    watch_weather_get(&p->st);
    wl_build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, wl_tick_cb, WX_TICK_MS, p);
    return page;
}

/* ======================= Buscar ======================= */

typedef struct {
    lv_obj_t *ta, *kb, *res;
    watch_weather_status_t st;
    uint32_t qgen;
    bool searched;
    bool add_failed;
} ws_page_t;

static void ws_show_kb(ws_page_t *p, bool on)
{
    if (on) {
        lv_obj_remove_flag(p->kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->res, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(p->kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(p->res, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ws_build(ws_page_t *p);

static void ws_submit(void *ud)
{
    ws_page_t *p = ud;
    const char *q = lv_textarea_get_text(p->ta);
    while (*q == ' ') {
        q++;
    }
    if (!*q) {
        return;
    }
    watch_weather_search(q);
    p->searched = true;
    p->add_failed = false;
    watch_weather_get(&p->st);
    ws_build(p);
    ws_show_kb(p, false);
}

static void ta_click_cb(lv_event_t *e)
{
    ws_show_kb(lv_event_get_user_data(e), true);
}

static void res_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    ws_page_t *p = lv_obj_get_user_data(row);
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= p->st.nres) {
        return;
    }
    if (watch_weather_add(&p->st.res[i])) {
        s_wx_pop = true; /* si debajo está "Mis ubicaciones", también se cierra */
        watch_weather_search_clear();
        ui_nav_back();
        return;
    }
    p->add_failed = true;
    ws_build(p);
}

static void ws_build(ws_page_t *p)
{
    lv_obj_clean(p->res);
    p->qgen = p->st.qgen;
    if (p->add_failed) {
        wrap_label(p->res, FLEX_FONT_SMALL, FLEX_C_WARN, "No se pudo añadir: ya hay 5 ubicaciones.", true);
    }
    if (!p->searched) {
        return;
    }
    switch (p->st.qst) {
    case WXQ_BUSY: {
        lv_obj_t *sp = lv_spinner_create(p->res);
        lv_obj_set_size(sp, 36, 36);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_margin_left(sp, (FLEX_SCREEN_W - 2 * FLEX_LIST_X - 36) / 2, 0);
        wrap_label(p->res, FLEX_FONT_BODY, FLEX_C_TXT2, "Buscando...", true);
        break;
    }
    case WXQ_EMPTY:
        wrap_label(p->res, FLEX_FONT_BODY, FLEX_C_TXT2, "Sin resultados", true);
        break;
    case WXQ_ERROR:
        wrap_label(p->res, FLEX_FONT_BODY, FLEX_C_TXT2, "Sin resultados", true);
        wrap_label(p->res, FLEX_FONT_SMALL, FLEX_C_WARN, wx_error_text(p->st.err), true);
        break;
    case WXQ_OK: {
        char b[160];
        for (int i = 0; i < p->st.nres; i++) {
            const wx_loc_t *l = &p->st.res[i];
            lv_obj_t *row = watch_theme_tile(p->res, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_ver(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_user_data(row, p);
            lv_obj_add_event_cb(row, res_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            ui_phone_text(l->name, b, sizeof(b), FLEX_FONT_BODY);
            one_line(row, FLEX_FONT_BODY, FLEX_C_TXT, b);
            if (l->region[0]) {
                ui_phone_text(l->region, b, sizeof(b), FLEX_FONT_SMALL);
                one_line(row, FLEX_FONT_SMALL, FLEX_C_MUTE, b);
            }
        }
        break;
    }
    default: break;
    }
}

static void ws_tick_cb(lv_timer_t *t)
{
    ws_page_t *p = lv_timer_get_user_data(t);
    watch_weather_get(&p->st);
    if (p->st.qgen != p->qgen) {
        ws_build(p);
    }
}

lv_obj_t *ui_wx_search_create(lv_obj_t *parent)
{
    ws_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    watch_weather_search_clear();
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Buscar", back_cb, NULL);
    p->ta = lv_textarea_create(page);
    lv_textarea_set_one_line(p->ta, true);
    lv_textarea_set_max_length(p->ta, (WX_QUERY - 1) / 2); /* letras de hasta 2 bytes */
    lv_textarea_set_placeholder_text(p->ta, "Escribe una ciudad");
    lv_obj_set_size(p->ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 56);
    lv_obj_set_pos(p->ta, FLEX_SAFE_X, FLEX_HDR_H);
    ui_textarea_style(p->ta);
    lv_obj_add_event_cb(p->ta, ta_click_cb, LV_EVENT_CLICKED, p);
    /* resultados: debajo del campo, a toda la altura cuando no hay teclado */
    p->res = watch_theme_list(page);
    lv_obj_set_pos(p->res, 0, FLEX_HDR_H + 66);
    lv_obj_set_height(p->res, FLEX_SCREEN_H - FLEX_HDR_H - 66 - FLEX_LIST_BOTTOM);
    p->kb = ui_keyboard_create(page, p->ta, UI_KB_TEXT, ws_submit, p);
    lv_obj_set_size(p->kb, FLEX_SCREEN_W - 2 * 16, 452 - 236);
    lv_obj_set_pos(p->kb, 16, 236);
    ws_show_kb(p, true);
    watch_weather_get(&p->st);
    p->qgen = p->st.qgen;
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, ws_tick_cb, WX_TICK_MS, p);
    return page;
}
