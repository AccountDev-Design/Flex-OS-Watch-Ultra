/*
 * flex_wallpaper — ver include/flex_wallpaper.h
 *
 * Cada fondo es una lista de capas (las llamadas de su función wall*() en la
 * referencia, en el mismo orden). Cada píxel evalúa la lista entera en coma
 * flotante y se cuantiza una sola vez con tramado.
 */
#include "flex_wallpaper.h"

#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#define REF_W 480.0f
#define REF_H 800.0f

typedef struct {
    float r, g, b;
} col3_t;

typedef enum {
    L_DIAG3,       /* wallDiag3(lo, mid, hi)                         c0 c1 c2 */
    L_CORNERS4,    /* wallCorners4(tl, tr, bl, br)                   c0 c1 c2 c3 */
    L_BLACK,       /* hLine negro a pantalla completa (wallOnyx) */
    L_RAD_DISC,    /* wlutDisc(a=rIn, b=rOut, al) + wallRadial(cx, cy, c0) */
    L_RAD_RING,    /* wlutRing(a=r, b=hw, al) + wallRadial(cx, cy, c0) */
    L_DISC,        /* wallDisc(cx, cy, a=r, c0, c1, dx, dy): disco opaco con degradado */
    L_CIRCLE_A,    /* fillCircleA(cx, cy, a=r, c0, al) */
} ltype_t;

typedef struct {
    uint8_t type;
    int16_t cx, cy;   /* coordenadas de la referencia (480x800) */
    int16_t a, b;     /* radios o parámetros de la LUT */
    uint8_t al;       /* alfa (0..255) */
    int8_t dx, dy;    /* dirección del degradado del disco */
    uint32_t c0, c1, c2, c3;
} layer_t;

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

/* ---- 0. Flex Original (Wallpaper.h:207 + Gfx.h:834): degradado violeta ->
 * azul -> verde y las dos manchas del escritorio (blobs = true en Home.h). */
static const layer_t kFlexOriginal[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(112, 46, 230), RGB(40, 150, 245), RGB(80, 224, 74), 0},
    {L_CIRCLE_A, 360, 150, 220, 0, 60, 0, 0, RGB(150, 235, 180), 0, 0, 0},
    {L_CIRCLE_A, 90, 560, 260, 0, 55, 0, 0, RGB(150, 160, 240), 0, 0, 0},
};
/* ---- 1. Aurora (Wallpaper.h:224) ---- */
static const layer_t kAurora[] = {
    {L_CORNERS4, 0, 0, 0, 0, 0, 0, 0, RGB(205, 238, 236), RGB(48, 132, 240), RGB(72, 208, 206), RGB(24, 86, 220)},
    {L_RAD_DISC, 330, 205, 236, 258, 90, 0, 0, RGB(255, 255, 255), 0, 0, 0},
    {L_DISC, 330, 205, 246, 0, 0, -1, -1, RGB(26, 68, 205), RGB(104, 176, 252), 0, 0},
    {L_RAD_DISC, 96, 486, 244, 264, 105, 0, 0, RGB(255, 255, 255), 0, 0, 0},
    {L_DISC, 96, 486, 250, 0, 0, 1, -1, RGB(86, 220, 176), RGB(186, 248, 220), 0, 0},
    {L_RAD_DISC, 430, 760, 150, 330, 60, 0, 0, RGB(140, 205, 255), 0, 0, 0},
};
/* ---- 2. Nocturno (Wallpaper.h:233) ---- */
static const layer_t kNocturno[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(4, 5, 14), RGB(8, 9, 26), RGB(5, 6, 18), 0},
    {L_DISC, 340, 110, 300, 0, 0, -1, -1, RGB(10, 12, 38), RGB(62, 70, 152), 0, 0},
    {L_RAD_RING, 340, 110, 300, 6, 225, 0, 0, RGB(182, 192, 255), 0, 0, 0},
    {L_DISC, 92, 620, 330, 0, 0, 1, -1, RGB(9, 10, 34), RGB(70, 78, 160), 0, 0},
    {L_RAD_RING, 92, 620, 330, 7, 240, 0, 0, RGB(196, 204, 255), 0, 0, 0},
    {L_DISC, 456, 468, 186, 0, 0, -1, 1, RGB(8, 9, 30), RGB(48, 54, 124), 0, 0},
    {L_RAD_RING, 456, 468, 186, 5, 205, 0, 0, RGB(164, 174, 250), 0, 0, 0},
    {L_RAD_DISC, 286, 300, 70, 260, 34, 0, 0, RGB(120, 134, 235), 0, 0, 0},
};
/* ---- 3. Halo (Wallpaper.h:244) ---- */
static const layer_t kHalo[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(2, 2, 7), RGB(5, 5, 16), RGB(2, 2, 8), 0},
    {L_RAD_DISC, 72, 430, 230, 430, 70, 0, 0, RGB(46, 74, 205), 0, 0, 0},
    {L_RAD_DISC, 206, 402, 112, 132, 255, 0, 0, RGB(17, 22, 58), 0, 0, 0},
    {L_RAD_RING, 206, 402, 118, 26, 190, 0, 0, RGB(74, 112, 236), 0, 0, 0},
    {L_RAD_RING, 206, 402, 122, 5, 235, 0, 0, RGB(176, 198, 255), 0, 0, 0},
    {L_RAD_RING, 206, 402, 252, 34, 150, 0, 0, RGB(58, 92, 224), 0, 0, 0},
    {L_RAD_RING, 206, 402, 256, 5, 200, 0, 0, RGB(158, 182, 255), 0, 0, 0},
    {L_RAD_RING, 206, 402, 392, 44, 120, 0, 0, RGB(44, 74, 206), 0, 0, 0},
    {L_RAD_RING, 206, 402, 396, 5, 165, 0, 0, RGB(140, 168, 250), 0, 0, 0},
};
/* ---- 4. Onyx (Wallpaper.h:256) ---- */
static const layer_t kOnyx[] = {
    {L_BLACK, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {L_RAD_DISC, 240, 690, 60, 380, 46, 0, 0, RGB(30, 64, 180), 0, 0, 0},
};
/* ---- 5..7. Océano, Violeta, Naturaleza (Wallpaper.h:261-272) ---- */
static const layer_t kOceano[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(3, 32, 72), RGB(10, 116, 168), RGB(38, 206, 192), 0},
    {L_RAD_DISC, 400, 180, 70, 300, 50, 0, 0, RGB(180, 244, 236), 0, 0, 0},
};
static const layer_t kVioleta[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(26, 8, 58), RGB(122, 40, 180), RGB(232, 124, 204), 0},
    {L_RAD_DISC, 90, 250, 80, 320, 46, 0, 0, RGB(255, 210, 240), 0, 0, 0},
};
static const layer_t kNaturaleza[] = {
    {L_DIAG3, 0, 0, 0, 0, 0, 0, 0, RGB(8, 44, 22), RGB(58, 138, 58), RGB(186, 222, 122), 0},
    {L_RAD_DISC, 380, 640, 80, 300, 44, 0, 0, RGB(236, 250, 190), 0, 0, 0},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct {
    const char *name;
    const layer_t *layers;
    size_t n;
} kWalls[FLEX_WP_COUNT_] = {
    {"Flex Original", kFlexOriginal, N(kFlexOriginal)}, {"Aurora", kAurora, N(kAurora)},
    {"Nocturno", kNocturno, N(kNocturno)},              {"Halo", kHalo, N(kHalo)},
    {"Onyx", kOnyx, N(kOnyx)},                          {"Océano", kOceano, N(kOceano)},
    {"Violeta", kVioleta, N(kVioleta)},                 {"Naturaleza", kNaturaleza, N(kNaturaleza)},
};

const char *flex_wallpaper_name(flex_wp_t id)
{
    return ((unsigned)id < FLEX_WP_COUNT_) ? kWalls[id].name : kWalls[0].name;
}

static inline col3_t col(uint32_t rgb)
{
    const col3_t c = {(float)((rgb >> 16) & 0xFF), (float)((rgb >> 8) & 0xFF), (float)(rgb & 0xFF)};
    return c;
}

/* mix565(a, b, t) de la referencia en continuo: t = 0 -> a, t = 1 -> b */
static inline col3_t mixc(col3_t a, col3_t b, float t)
{
    const col3_t c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    return c;
}

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Matriz de Bayer 4x4, valores 0..15. */
static const uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

/* Canal 0..255 -> nivel (v * inv_step) con tramado. */
static inline uint32_t dither(float v, float inv_step, int max, float th)
{
    const float q = v * inv_step;
    int base = (int)q;
    if (q - (float)base > th) {
        base++;
    }
    return (uint32_t)(base < 0 ? 0 : (base > max ? max : base));
}

/* ---- Capas preparadas para la resolución de destino --------------------
 * Todo lo que no depende del píxel se calcula una vez: posiciones y radios
 * escalados, colores en coma flotante y, para los halos y anillos, una tabla
 * de alfa indexada por la distancia AL CUADRADO (como wlut de la referencia,
 * Wallpaper.h:60, pero con su propio paso por capa: <= 0,2 px en el radio
 * mayor de Halo en vez de ~8 px). Así el bucle por píxel no hace raíces ni
 * divisiones salvo en el medio píxel del borde de un disco. */
#define LUT_N 2048

typedef struct {
    uint8_t type;
    float cx, cy;
    float lim2;        /* d^2 a partir de la cual la capa no pinta */
    uint8_t shift;     /* índice de la tabla = (int)d^2 >> shift */
    const uint8_t *lut;
    col3_t c0, c1, c2, c3;
    float r, rin2;     /* discos: radio y (r - 0.5)^2 (interior opaco) */
    float gx, gy;      /* disco: tt = ddx * gx + ddy * gy + 128 */
    float al;          /* alfa 0..1 (fillCircleA) */
    /* sin tabla (sin memoria): parámetros para calcular el alfa directamente */
    float a, b, al255;
} prep_t;

static float radial_alpha(uint8_t type, float d, float a, float b, float al255)
{
    if (type == L_RAD_DISC) { /* a = rIn, b = rOut */
        if (d >= b) {
            return 0.0f;
        }
        return (d <= a) ? al255 : al255 * (b - d) / (b - a);
    }
    const float k = fabsf(d - a); /* L_RAD_RING: a = r, b = hw */
    return k < b ? al255 * (b - k) / b : 0.0f;
}

static void prepare(const layer_t *l, prep_t *p, float sx, float sy, uint8_t *lut_mem)
{
    const float s = sx;
    p->type = l->type;
    p->cx = (float)l->cx * sx;
    p->cy = (float)l->cy * sy;
    p->c0 = col(l->c0);
    p->c1 = col(l->c1);
    p->c2 = col(l->c2);
    p->c3 = col(l->c3);
    p->lut = NULL;
    if (l->type == L_RAD_DISC || l->type == L_RAD_RING) {
        p->a = l->a * s;
        p->b = l->b * s;
        p->al255 = (float)l->al;
        const float rmax = (l->type == L_RAD_DISC) ? p->b : p->a + p->b;
        p->lim2 = rmax * rmax;
        uint8_t sh = 0;
        while (((uint32_t)p->lim2 >> sh) >= LUT_N) {
            sh++;
        }
        p->shift = sh;
        if (lut_mem) {
            for (uint32_t i = 0; i < LUT_N; i++) {
                /* centro de la celda de la tabla */
                const float d = sqrtf(((float)(i << sh)) + (float)(1u << sh) * 0.5f);
                lut_mem[i] = (uint8_t)(radial_alpha(l->type, d, p->a, p->b, p->al255) + 0.5f);
            }
            p->lut = lut_mem;
        }
    } else if (l->type == L_DISC || l->type == L_CIRCLE_A) {
        p->r = l->a * s;
        p->lim2 = (p->r + 0.5f) * (p->r + 0.5f);
        p->rin2 = p->r > 0.5f ? (p->r - 0.5f) * (p->r - 0.5f) : 0.0f;
        const float den = p->r * (float)(abs(l->dx) + abs(l->dy));
        const float k = 127.0f / (den < 1.0f ? 1.0f : den);
        p->gx = (float)l->dx * k;
        p->gy = (float)l->dy * k;
        p->al = (float)l->al / 255.0f;
    }
}

void flex_wallpaper_render_rows(flex_wp_t id, uint16_t *buf, int w, int h, int y0, int y1)
{
    if (!buf || w <= 1 || h <= 1) {
        return;
    }
    if ((unsigned)id >= FLEX_WP_COUNT_) {
        id = FLEX_WP_FLEX_ORIGINAL; /* RUTA SEGURA (drawWallpaperRowsId) */
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > h - 1) {
        y1 = h - 1;
    }
    const size_t n = kWalls[id].n;
    const float sx = (float)w / REF_W, sy = (float)h / REF_H;
    prep_t P[12];
    if (n > sizeof(P) / sizeof(P[0])) {
        return;
    }
    /* Tablas de los halos: un bloque temporal (<= 9 x 2 KB). Sin memoria se
     * calcula el alfa con raíz por píxel: más lento, mismo dibujo. */
    size_t nrad = 0;
    for (size_t k = 0; k < n; k++) {
        nrad += (kWalls[id].layers[k].type == L_RAD_DISC || kWalls[id].layers[k].type == L_RAD_RING);
    }
    uint8_t *luts = nrad ? malloc(nrad * LUT_N) : NULL;
    size_t li = 0;
    for (size_t k = 0; k < n; k++) {
        const layer_t *l = &kWalls[id].layers[k];
        const bool rad = (l->type == L_RAD_DISC || l->type == L_RAD_RING);
        prepare(l, &P[k], sx, sy, (rad && luts) ? luts + (li++) * LUT_N : NULL);
    }
    const float ktx = 255.0f / (float)(w - 1), kty = 255.0f / (float)(h - 1);
    const float k2 = 2.0f / 255.0f, k1 = 1.0f / 255.0f;

    for (int y = y0; y <= y1; y++) {
        const float ty = (float)(h - 1 - y) * kty; /* 255 arriba */
        const float tyc = (float)y * kty * k1;     /* corners4: 0 arriba, 0..1 */
        uint16_t *row = buf + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            const float tx = (float)x * ktx;
            col3_t c = {0, 0, 0};
            for (size_t k = 0; k < n; k++) {
                const prep_t *p = &P[k];
                switch (p->type) {
                case L_DIAG3: {
                    const float t = (tx + ty) * 0.5f;
                    c = (t < 128.0f) ? mixc(p->c0, p->c1, t * k2) : mixc(p->c1, p->c2, (t - 128.0f) * k2);
                } break;
                case L_CORNERS4:
                    c = mixc(mixc(p->c0, p->c2, tyc), mixc(p->c1, p->c3, tyc), tx * k1);
                    break;
                case L_BLACK:
                    c.r = c.g = c.b = 0.0f;
                    break;
                default: {
                    const float ddx = (float)x - p->cx, ddy = (float)y - p->cy;
                    const float d2 = ddx * ddx + ddy * ddy;
                    if (d2 >= p->lim2) {
                        break;
                    }
                    if (p->type == L_RAD_DISC || p->type == L_RAD_RING) {
                        const float a = p->lut ? (float)p->lut[(uint32_t)d2 >> p->shift]
                                               : radial_alpha(p->type, sqrtf(d2), p->a, p->b, p->al255);
                        if (a > 0.0f) {
                            c = mixc(c, p->c0, a * k1);
                        }
                    } else {
                        /* disco: opaco por dentro; raíz solo en el medio píxel del borde */
                        const float cov = d2 <= p->rin2 ? 1.0f : clamp01(p->r + 0.5f - sqrtf(d2));
                        if (p->type == L_DISC) {
                            float tt = ddx * p->gx + ddy * p->gy + 128.0f;
                            tt = tt < 0.0f ? 0.0f : (tt > 255.0f ? 255.0f : tt);
                            c = mixc(c, mixc(p->c0, p->c1, tt * k1), cov);
                        } else {
                            c = mixc(c, p->c0, cov * p->al);
                        }
                    }
                } break;
                }
            }
            const float th = ((float)kBayer4[y & 3][x & 3] + 0.5f) / 16.0f;
            row[x] = (uint16_t)((dither(c.r, 0.125f, 31, th) << 11) | (dither(c.g, 0.25f, 63, th) << 5) |
                                dither(c.b, 0.125f, 31, th));
        }
    }
    free(luts);
}

void flex_wallpaper_render(flex_wp_t id, uint16_t *buf, int w, int h)
{
    flex_wallpaper_render_rows(id, buf, w, h, 0, h - 1);
}

void flex_wallpaper_onyx(uint16_t *buf, int w, int h)
{
    flex_wallpaper_render(FLEX_WP_ONYX, buf, w, h);
}

uint8_t flex_wallpaper_region_luma(const uint16_t *buf, int stride, int h_total, int x, int y, int w, int h)
{
    if (!buf) {
        return 0;
    }
    uint32_t sum = 0, cnt = 0;
    for (int j = y < 0 ? 0 : y; j < y + h && j < h_total; j += 2) {
        for (int i = x < 0 ? 0 : x; i < x + w && i < stride; i += 2) {
            const uint16_t p = buf[(size_t)j * (size_t)stride + (size_t)i];
            const uint32_t r = ((p >> 11) & 0x1F) * 255u / 31u, g = ((p >> 5) & 0x3F) * 255u / 63u,
                           b = (p & 0x1F) * 255u / 31u;
            sum += (r * 77u + g * 151u + b * 28u) >> 8;
            cnt++;
        }
    }
    return cnt ? (uint8_t)(sum / cnt) : 0;
}
