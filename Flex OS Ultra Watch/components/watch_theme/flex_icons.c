/*
 * flex_icons — ver include/flex_icons.h
 *
 * Coordenadas de cada icono en centésimas de su caja (0..100), para que el
 * mismo trazo sirva a 16 px en la barra de estado y a 64 px en un panel.
 * Radios y grosores en centésimas del lado menor de la caja.
 *
 * Recetas de la referencia (FlexOS_Ultra_QuickPanel.h:350-512 y
 * FlexOS_Ultra_AppSettings.h:179-218), con dos cambios:
 *  - se reescalan para llenar la caja (allí el glifo usa ~85 % del lado);
 *  - el engranaje ya no "recorta" el hueco pintando el color del fondo
 *    (qpIcoBg): aquí el centro es un anillo, así que el icono vale sobre
 *    vidrio, sobre un botón pulsado o sobre cualquier fondo.
 * Los ángulos de los arcos siguen el convenio de LVGL y de arcStroke() de la
 * referencia: 0° a las 3, 90° a las 6, 270° arriba.
 */
#include "flex_icons.h"

#include <math.h>

typedef struct {
    flex_icon_t icon;
    lv_color_t color;
    lv_color_t color2;
} icon_data_t;

typedef struct {
    lv_layer_t *layer;
    const lv_area_t *b;
    int32_t side; /* lado menor de la caja */
    lv_color_t color;
    lv_opa_t opa;
} pen_t;

static inline int32_t px(const pen_t *p, int32_t c)
{
    return p->b->x1 + (lv_area_get_width(p->b) - 1) * c / 100;
}

static inline int32_t py(const pen_t *p, int32_t c)
{
    return p->b->y1 + (lv_area_get_height(p->b) - 1) * c / 100;
}

/* Medida relativa al lado menor (radios, grosores), al menos min_px. */
static inline int32_t pr(const pen_t *p, int32_t c, int32_t min_px)
{
    const int32_t v = (p->side * c + 50) / 100;
    return v < min_px ? min_px : v;
}

static void tri(const pen_t *p, const int32_t pts[6])
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = p->color;
    d.opa = p->opa;
    for (int i = 0; i < 3; i++) {
        d.p[i].x = px(p, pts[2 * i]);
        d.p[i].y = py(p, pts[2 * i + 1]);
    }
    lv_draw_triangle(p->layer, &d);
}

/* Segmento con extremos redondeados; w100 = grosor en centésimas del lado. */
static void line(const pen_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w100)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = p->color;
    d.opa = p->opa;
    d.width = pr(p, w100, 2);
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = px(p, x1);
    d.p1.y = py(p, y1);
    d.p2.x = px(p, x2);
    d.p2.y = py(p, y2);
    lv_draw_line(p->layer, &d);
}

/* Arco de radio exterior r100 (el trazo crece hacia dentro), de a0 a a1 grados. */
static void arc(const pen_t *p, int32_t cx, int32_t cy, int32_t r100, int32_t w100, int32_t a0, int32_t a1,
                bool rounded)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = p->color;
    d.opa = p->opa;
    d.center.x = px(p, cx);
    d.center.y = py(p, cy);
    int32_t r = pr(p, r100, 2);
    if (a1 - a0 >= 360) {
        /* Círculo completo: el redondeo a píxel no debe sacarlo de la caja. */
        const int32_t fit = LV_MIN(LV_MIN(d.center.x - p->b->x1, p->b->x2 - d.center.x),
                                   LV_MIN(d.center.y - p->b->y1, p->b->y2 - d.center.y));
        r = LV_MIN(r, fit);
    }
    d.radius = (uint16_t)r;
    d.width = pr(p, w100, 2);
    d.start_angle = a0;
    d.end_angle = a1;
    d.rounded = rounded;
    lv_draw_arc(p->layer, &d);
}

static void disc(const pen_t *p, int32_t cx, int32_t cy, int32_t r100)
{
    const int32_t r = pr(p, r100, 1);
    const int32_t x = px(p, cx), y = py(p, cy);
    const lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->color;
    d.bg_opa = p->opa;
    d.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(p->layer, &d, &a);
}

/* Rectángulo relleno (centésimas de la caja) con radio r100. */
static void box_fill(const pen_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t r100)
{
    const lv_area_t a = {px(p, x1), py(p, y1), px(p, x2), py(p, y2)};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->color;
    d.bg_opa = p->opa;
    d.radius = pr(p, r100, 0);
    lv_draw_rect(p->layer, &d, &a);
}

/* Desplazamientos (x, y) en centésimas para 8 direcciones a 45°, con
 * radio 100: cos/sin redondeados, sin coma flotante. */
static const int8_t DIR8[8][2] = {
    {100, 0}, {71, 71}, {0, 100}, {-71, 71}, {-100, 0}, {-71, -71}, {0, -100}, {71, -71},
};


static float fx(const pen_t *p, float c)
{
    return (float)p->b->x1 + (float)(lv_area_get_width(p->b) - 1) * c / 100.0f;
}

static float fy(const pen_t *p, float c)
{
    return (float)p->b->y1 + (float)(lv_area_get_height(p->b) - 1) * c / 100.0f;
}

/* Tramo horizontal [x1, x2) de la fila y, con los píxeles de los extremos
 * en proporción a lo que cubren (borde suavizado sin costuras). */
static void hspan(const pen_t *p, float x1, float x2, int32_t y)
{
    if (x2 - x1 <= 0.0f) {
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->color;
    const int32_t a = (int32_t)ceilf(x1), b = (int32_t)floorf(x2);
    if (b > a) {
        d.bg_opa = p->opa;
        const lv_area_t r = {a, y, b - 1, y};
        lv_draw_rect(p->layer, &d, &r);
    }
    const float cl = (float)a - x1, cr = x2 - (float)b;
    if (b < a) { /* el tramo entero cabe en un píxel */
        d.bg_opa = (lv_opa_t)((float)p->opa * (x2 - x1));
        const lv_area_t r = {b, y, b, y};
        lv_draw_rect(p->layer, &d, &r);
        return;
    }
    if (cl > 0.02f) {
        d.bg_opa = (lv_opa_t)((float)p->opa * cl);
        const lv_area_t r = {a - 1, y, a - 1, y};
        lv_draw_rect(p->layer, &d, &r);
    }
    if (cr > 0.02f) {
        d.bg_opa = (lv_opa_t)((float)p->opa * cr);
        const lv_area_t r = {b, y, b, y};
        lv_draw_rect(p->layer, &d, &r);
    }
}

/* Luna: el disco A menos el disco B (en centésimas de la caja), rellena fila
 * a fila. Antes se rellenaba con triángulos: cada uno suaviza sus bordes y
 * entre vecinos quedaban costuras visibles a tamaño grande (rayas). */
static void crescent(const pen_t *p, float ax, float ay, float ar, float bx, float by, float br)
{
    const float s = (float)(p->side - 1) / 100.0f;
    const float Ax = fx(p, ax), Ay = fy(p, ay), Ar = ar * s;
    const float Bx = fx(p, bx), By = fy(p, by), Br = br * s;
    const int32_t y0 = (int32_t)floorf(Ay - Ar), y1 = (int32_t)ceilf(Ay + Ar);
    for (int32_t y = y0; y <= y1; y++) {
        const float yc = (float)y + 0.5f, da = yc - Ay;
        if (da * da >= Ar * Ar) {
            continue;
        }
        const float ha = sqrtf(Ar * Ar - da * da);
        float l = Ax - ha, r = Ax + ha;
        const float db = yc - By;
        if (db * db < Br * Br) {
            const float hb = sqrtf(Br * Br - db * db), bl = Bx - hb, brr = Bx + hb;
            /* A menos B: lo que queda a cada lado del hueco */
            hspan(p, l, bl < r ? bl : r, y);
            hspan(p, brr > l ? brr : l, r, y);
        } else {
            hspan(p, l, r, y);
        }
    }
}

/* Intervalo de la fila (centro yc) dentro del disco (cx, cy, r); false si no la toca. */
static bool disc_row(float cx, float cy, float r, float yc, float *l, float *rr)
{
    const float d = yc - cy;
    if (d * d >= r * r) {
        return false;
    }
    const float h = sqrtf(r * r - d * d);
    *l = cx - h;
    *rr = cx + h;
    return true;
}

/* Hoja rellena: la lente (intersección de dos discos) entre las puntas t1 y t2
 * con la panza dada; fila a fila como la luna. Centésimas de la caja. */
static void lens_fill(const pen_t *p, float t1x, float t1y, float t2x, float t2y, float bulge)
{
    const float mx = (t1x + t2x) / 2, my = (t1y + t2y) / 2;
    const float dx = t2x - t1x, dy = t2y - t1y, L = sqrtf(dx * dx + dy * dy);
    const float nx = -dy / L, ny = dx / L, h = L / 2;
    const float r = (h * h + bulge * bulge) / (2 * bulge), c = r - bulge;
    const float s = (float)(p->side - 1) / 100.0f;
    const float ax = fx(p, mx + nx * c), ay = fy(p, my + ny * c), bx = fx(p, mx - nx * c), by = fy(p, my - ny * c);
    const float R = r * s;
    const float top = fminf(fy(p, t1y), fy(p, t2y)) - bulge * s, bot = fmaxf(fy(p, t1y), fy(p, t2y)) + bulge * s;
    for (int32_t y = (int32_t)floorf(top); y <= (int32_t)ceilf(bot); y++) {
        float l1, r1, l2, r2;
        const float yc = (float)y + 0.5f;
        if (disc_row(ax, ay, R, yc, &l1, &r1) && disc_row(bx, by, R, yc, &l2, &r2)) {
            hspan(p, fmaxf(l1, l2), fminf(r1, r2), y);
        }
    }
}

void flex_icon_draw(lv_layer_t *layer, flex_icon_t icon, const lv_area_t *box, lv_color_t color, lv_opa_t opa)
{
    flex_icon_draw2(layer, icon, box, color, lv_color_black(), opa);
}

void flex_icon_draw2(lv_layer_t *layer, flex_icon_t icon, const lv_area_t *box, lv_color_t color, lv_color_t color2,
                     lv_opa_t opa)
{
    const int32_t w = lv_area_get_width(box), h = lv_area_get_height(box);
    const pen_t p = {layer, box, w < h ? w : h, color, opa};

    switch (icon) {
    case FLEX_ICON_BOLT: {
        /* Rayo de dos triángulos, como qpIcoBattSave de la referencia. */
        static const int32_t t1[6] = {62, 0, 14, 58, 52, 58};
        static const int32_t t2[6] = {48, 42, 86, 42, 38, 100};
        tri(&p, t1);
        tri(&p, t2);
        break;
    }
    case FLEX_ICON_CHECK:
        line(&p, 15, 52, 40, 78, 14);
        line(&p, 40, 78, 85, 25, 14);
        break;
    case FLEX_ICON_CROSS:
        line(&p, 18, 18, 82, 82, 14);
        line(&p, 82, 18, 18, 82, 14);
        break;
    case FLEX_ICON_GEAR:
        /* qpIcoGear: núcleo y 8 dientes redondos; el hueco es un anillo. */
        arc(&p, 50, 50, 27, 15, 0, 360, false);
        for (int k = 0; k < 8; k++) {
            disc(&p, 50 + DIR8[k][0] * 39 / 100, 50 + DIR8[k][1] * 39 / 100, 10);
        }
        break;
    case FLEX_ICON_SUN:
        /* qpIcoSun: disco y 8 rayos de 0,32 a 0,46 del lado; aquí hasta 0,43
         * para que el extremo redondeado no salga de la caja. */
        disc(&p, 50, 50, 21);
        for (int k = 0; k < 8; k++) {
            line(&p, 50 + DIR8[k][0] * 32 / 100, 50 + DIR8[k][1] * 32 / 100,
                 50 + DIR8[k][0] * 43 / 100, 50 + DIR8[k][1] * 43 / 100, 8);
        }
        break;
    case FLEX_ICON_WIFI:
        /* qpIcoWifi: dos arcos de 225° a 315° sobre un punto, centrados abajo. */
        arc(&p, 50, 80, 62, 11, 225, 315, true);
        arc(&p, 50, 80, 38, 11, 225, 315, true);
        disc(&p, 50, 80, 8);
        break;
    case FLEX_ICON_BLUETOOTH:
        /* qpIcoBle con u = 0,46 del lado. */
        line(&p, 50, 6, 50, 94, 8);
        line(&p, 50, 6, 78, 30, 8);
        line(&p, 78, 30, 24, 70, 8);
        line(&p, 50, 94, 78, 70, 8);
        line(&p, 78, 70, 24, 30, 8);
        break;
    case FLEX_ICON_CHEVRON_LEFT:
        line(&p, 64, 14, 32, 50, 12);
        line(&p, 32, 50, 64, 86, 12);
        break;
    case FLEX_ICON_CHEVRON_RIGHT:
        line(&p, 36, 14, 68, 50, 12);
        line(&p, 68, 50, 36, 86, 12);
        break;
    case FLEX_ICON_INFO:
        /* Info de AppSettings.h: círculo, punto y barra. */
        arc(&p, 50, 50, 48, 8, 0, 360, false);
        disc(&p, 50, 29, 7);
        line(&p, 50, 46, 50, 74, 11);
        break;
    case FLEX_ICON_PULSE:
        /* Trazo de pulso (diagnóstico): línea base con un latido. */
        line(&p, 6, 56, 28, 56, 8);
        line(&p, 28, 56, 40, 24, 8);
        line(&p, 40, 24, 56, 80, 8);
        line(&p, 56, 80, 66, 44, 8);
        line(&p, 66, 44, 74, 56, 8);
        line(&p, 74, 56, 94, 56, 8);
        break;
    case FLEX_ICON_CLOCK:
        /* qpIcoClock: esfera y agujas a las 12 y a las 3 y pico. */
        arc(&p, 50, 50, 48, 8, 0, 360, false);
        line(&p, 50, 50, 50, 24, 8);
        line(&p, 50, 50, 70, 58, 8);
        break;
    case FLEX_ICON_IMAGE: {
        /* Marco con montañas y sol. */
        line(&p, 8, 18, 92, 18, 8);
        line(&p, 92, 18, 92, 82, 8);
        line(&p, 92, 82, 8, 82, 8);
        line(&p, 8, 82, 8, 18, 8);
        static const int32_t m1[6] = {16, 76, 40, 44, 62, 76};
        static const int32_t m2[6] = {46, 76, 68, 52, 86, 76};
        tri(&p, m1);
        tri(&p, m2);
        disc(&p, 70, 36, 8);
        break;
    }
    case FLEX_ICON_WATCH:
        /* Caja redonda con correa arriba y abajo y agujas. */
        arc(&p, 50, 50, 32, 9, 0, 360, false);
        line(&p, 40, 9, 60, 9, 12);
        line(&p, 40, 91, 60, 91, 12);
        line(&p, 50, 50, 50, 33, 7);
        line(&p, 50, 50, 62, 57, 7);
        break;
    case FLEX_ICON_GLOBE:
        arc(&p, 50, 50, 47, 7, 0, 360, false);
        line(&p, 50, 8, 50, 92, 6);
        line(&p, 8, 50, 92, 50, 6);
        line(&p, 16, 30, 84, 30, 6);
        line(&p, 16, 70, 84, 70, 6);
        break;
    case FLEX_ICON_BELL:
        arc(&p, 50, 54, 32, 9, 180, 360, false);
        line(&p, 22, 54, 16, 74, 9);
        line(&p, 78, 54, 84, 74, 9);
        line(&p, 10, 76, 90, 76, 9);
        disc(&p, 50, 88, 8);
        disc(&p, 50, 16, 6);
        break;
    case FLEX_ICON_HOURGLASS: {
        /* Barras arriba y abajo, cristal en dos bulbos y la arena abajo. */
        line(&p, 17, 9, 83, 9, 10);
        line(&p, 17, 91, 83, 91, 10);
        line(&p, 26, 13, 26, 24, 8);
        line(&p, 74, 13, 74, 24, 8);
        line(&p, 26, 24, 46, 48, 8);
        line(&p, 74, 24, 54, 48, 8);
        line(&p, 46, 52, 26, 76, 8);
        line(&p, 54, 52, 74, 76, 8);
        line(&p, 26, 76, 26, 87, 8);
        line(&p, 74, 76, 74, 87, 8);
        static const int32_t sand[6] = {32, 86, 68, 86, 50, 66};
        tri(&p, sand);
        static const int32_t top[6] = {40, 32, 60, 32, 50, 44};
        tri(&p, top);
        break;
    }
    case FLEX_ICON_STOPWATCH:
        arc(&p, 50, 57, 38, 8, 0, 360, false);
        line(&p, 40, 6, 60, 6, 8);
        line(&p, 50, 8, 50, 18, 8);
        line(&p, 78, 22, 84, 16, 8);
        line(&p, 50, 57, 63, 41, 8);
        break;
    case FLEX_ICON_GRID:
        disc(&p, 27, 27, 18);
        disc(&p, 73, 27, 18);
        disc(&p, 27, 73, 18);
        disc(&p, 73, 73, 18);
        break;
    case FLEX_ICON_MOTION:
        /* Punto con dos pares de ondas, a izquierda y derecha. */
        disc(&p, 50, 50, 10);
        arc(&p, 50, 50, 30, 8, 320, 40, true);
        arc(&p, 50, 50, 30, 8, 140, 220, true);
        arc(&p, 50, 50, 45, 8, 322, 38, true);
        arc(&p, 50, 50, 45, 8, 142, 218, true);
        break;
    case FLEX_ICON_MOON:
        /* Luna creciente: anillo grueso abierto arriba a la derecha. */
        arc(&p, 50, 50, 46, 20, 10, 260, true);
        break;
    case FLEX_ICON_STORAGE:
        line(&p, 16, 22, 84, 22, 16);
        line(&p, 16, 50, 84, 50, 16);
        line(&p, 16, 78, 84, 78, 16);
        break;
    case FLEX_ICON_PLUS:
        line(&p, 50, 14, 50, 86, 14);
        line(&p, 14, 50, 86, 50, 14);
        break;
    case FLEX_ICON_MINUS:
        line(&p, 14, 50, 86, 50, 14);
        break;
    case FLEX_ICON_PLAY: {
        static const int32_t t[6] = {24, 10, 24, 90, 88, 50};
        tri(&p, t);
        break;
    }
    case FLEX_ICON_PAUSE:
        line(&p, 32, 14, 32, 86, 18);
        line(&p, 68, 14, 68, 86, 18);
        break;
    case FLEX_ICON_STOP:
        /* Un rectángulo: con dos triángulos se veía la costura de la diagonal. */
        box_fill(&p, 18, 18, 82, 82, 6);
        break;
    case FLEX_ICON_FLAG: {
        line(&p, 24, 8, 24, 92, 9);
        static const int32_t f[6] = {28, 10, 86, 30, 28, 50};
        tri(&p, f);
        break;
    }
    case FLEX_ICON_UPDATE: {
        /* Flecha que baja a una bandeja: descargar e instalar. */
        line(&p, 50, 8, 50, 52, 12);
        static const int32_t h[6] = {26, 42, 74, 42, 50, 68};
        tri(&p, h);
        line(&p, 12, 66, 12, 88, 11);
        line(&p, 12, 88, 88, 88, 11);
        line(&p, 88, 88, 88, 66, 11);
        break;
    }
    case FLEX_ICON_CLOCK_SOLID: {
        /* Disco con las agujas caladas en el segundo color (las 12 y las 4). */
        disc(&p, 50, 50, 46);
        const pen_t p2 = {layer, box, p.side, color2, opa};
        line(&p2, 50, 50, 50, 20, 11);
        line(&p2, 50, 50, 70, 64, 11);
        break;
    }
    case FLEX_ICON_ALARM:
        /* Esfera, dos campanas arriba, patas y agujas. */
        arc(&p, 50, 56, 37, 8, 0, 360, false);
        arc(&p, 22, 22, 17, 9, 160, 290, true);
        arc(&p, 78, 22, 17, 9, 250, 20, true);
        line(&p, 30, 86, 20, 97, 8);
        line(&p, 70, 86, 80, 97, 8);
        line(&p, 50, 56, 50, 36, 7);
        line(&p, 50, 56, 64, 66, 7);
        break;
    case FLEX_ICON_MOON_SOLID:
        /* Luna creciente maciza (disco menos disco desplazado arriba a la derecha). */
        crescent(&p, 46, 54, 44, 70, 34, 38);
        break;
    case FLEX_ICON_MIC:
        /* Cápsula, horquilla (media circunferencia de abajo), pie y base. */
        box_fill(&p, 37, 4, 63, 58, 13);
        arc(&p, 50, 40, 31, 8, 0, 180, true);
        line(&p, 50, 72, 50, 86, 8);
        line(&p, 32, 90, 68, 90, 8);
        break;
    case FLEX_ICON_PREV: {
        line(&p, 18, 16, 18, 84, 12);
        static const int32_t t[6] = {86, 12, 86, 88, 28, 50};
        tri(&p, t);
        break;
    }
    case FLEX_ICON_NEXT: {
        line(&p, 82, 16, 82, 84, 12);
        static const int32_t t[6] = {14, 12, 14, 88, 72, 50};
        tri(&p, t);
        break;
    }
    case FLEX_ICON_VOLUME: {
        box_fill(&p, 8, 34, 30, 66, 3);
        static const int32_t c1[6] = {26, 34, 52, 12, 52, 88};
        static const int32_t c2[6] = {26, 34, 52, 88, 26, 66};
        tri(&p, c1);
        tri(&p, c2);
        arc(&p, 52, 50, 24, 8, 305, 55, true);
        arc(&p, 52, 50, 42, 8, 310, 50, true);
        break;
    }
    case FLEX_ICON_SDCARD:
        /* Tarjeta con la esquina cortada y tres contactos. */
        line(&p, 24, 8, 64, 8, 8);
        line(&p, 64, 8, 80, 24, 8);
        line(&p, 80, 24, 80, 92, 8);
        line(&p, 80, 92, 24, 92, 8);
        line(&p, 24, 92, 24, 8, 8);
        line(&p, 38, 20, 38, 34, 7);
        line(&p, 50, 20, 50, 34, 7);
        line(&p, 62, 20, 62, 34, 7);
        break;
    case FLEX_ICON_QR: {
        /* Tres marcas de posición (marco y centro) y unos módulos. */
        static const int32_t f[3][2] = {{8, 8}, {62, 8}, {8, 62}};
        for (int i = 0; i < 3; i++) {
            const int32_t x = f[i][0], y = f[i][1];
            line(&p, x, y, x + 30, y, 7);
            line(&p, x + 30, y, x + 30, y + 30, 7);
            line(&p, x + 30, y + 30, x, y + 30, 7);
            line(&p, x, y + 30, x, y, 7);
            box_fill(&p, x + 10, y + 10, x + 20, y + 20, 0);
        }
        box_fill(&p, 62, 62, 72, 72, 0);
        box_fill(&p, 82, 62, 92, 72, 0);
        box_fill(&p, 62, 82, 72, 92, 0);
        box_fill(&p, 82, 82, 92, 92, 0);
        box_fill(&p, 72, 72, 82, 82, 0);
        box_fill(&p, 45, 45, 55, 55, 0);
        break;
    }
    case FLEX_ICON_VIDEO: {
        box_fill(&p, 6, 24, 66, 76, 12);
        static const int32_t t[6] = {70, 50, 94, 26, 94, 74};
        tri(&p, t);
        break;
    }
    case FLEX_ICON_NOTE:
        disc(&p, 34, 76, 15);
        line(&p, 46, 76, 46, 12, 9);
        line(&p, 46, 12, 80, 24, 9);
        break;
    case FLEX_ICON_PHONE:
        line(&p, 28, 6, 72, 6, 8);
        line(&p, 72, 6, 72, 94, 8);
        line(&p, 72, 94, 28, 94, 8);
        line(&p, 28, 94, 28, 6, 8);
        line(&p, 44, 82, 56, 82, 8);
        break;
    case FLEX_ICON_TRASH:
        line(&p, 12, 22, 88, 22, 8);
        line(&p, 38, 10, 62, 10, 8);
        line(&p, 22, 30, 28, 90, 8);
        line(&p, 28, 90, 72, 90, 8);
        line(&p, 72, 90, 78, 30, 8);
        line(&p, 42, 40, 42, 78, 6);
        line(&p, 58, 40, 58, 78, 6);
        break;
    case FLEX_ICON_GEAR_SOLID:
        /* Engranaje macizo con hueco de verdad (se ve el fondo): anillo grueso
         * y 8 dientes redondeados. */
        arc(&p, 50, 50, 37, 21, 0, 360, false);
        for (int k = 0; k < 8; k++) {
            line(&p, 50 + DIR8[k][0] * 33 / 100, 50 + DIR8[k][1] * 33 / 100, 50 + DIR8[k][0] * 40 / 100,
                 50 + DIR8[k][1] * 40 / 100, 17);
        }
        break;
    case FLEX_ICON_BATT_LEAF: {
        /* Batería (contorno, polo y carga) con una hoja rellena delante y su
         * nervio en el segundo color: ahorro de energía. */
        line(&p, 6, 30, 74, 30, 7);
        line(&p, 74, 30, 74, 74, 7);
        line(&p, 74, 74, 6, 74, 7);
        line(&p, 6, 74, 6, 30, 7);
        line(&p, 83, 45, 83, 59, 9);
        box_fill(&p, 14, 38, 34, 66, 3); /* carga */
        lens_fill(&p, 34, 94, 84, 16, 22);
        const pen_t p2 = {layer, box, p.side, color2, opa};
        line(&p2, 42, 84, 76, 26, 3); /* nervio */
        line(&p2, 54, 64, 70, 62, 2);
        line(&p2, 62, 50, 60, 36, 2);
        break;
    }
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
    flex_icon_draw2(lv_event_get_layer(e), d->icon, &box, d->color, d->color2, lv_obj_get_style_opa_recursive(obj, 0));
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
    d->color2 = lv_color_black();
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

void flex_icon_set_color2(lv_obj_t *obj, uint32_t rgb)
{
    icon_data_t *d = lv_obj_get_user_data(obj);
    if (d && lv_color_to_u32(d->color2) != lv_color_to_u32(lv_color_hex(rgb))) {
        d->color2 = lv_color_hex(rgb);
        lv_obj_invalidate(obj);
    }
}

void flex_icon_set(lv_obj_t *obj, flex_icon_t icon)
{
    icon_data_t *d = lv_obj_get_user_data(obj);
    if (d && d->icon != icon) {
        d->icon = icon;
        lv_obj_invalidate(obj);
    }
}

static bool has_cb(lv_obj_t *obj, lv_event_cb_t cb)
{
    const uint32_t n = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == cb) {
            return true;
        }
    }
    return false;
}

bool flex_icon_is(lv_obj_t *obj)
{
    return obj && has_cb(obj, icon_draw_cb);
}
