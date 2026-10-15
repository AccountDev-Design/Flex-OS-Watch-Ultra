/*
 * flex_safe — ver include/flex_safe.h
 */
#include "flex_safe.h"

/* Raíz cuadrada entera por defecto (floor). */
static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

int32_t flex_corner_inset(int32_t dy)
{
    if (dy < 0) {
        return FLEX_PANEL_W; /* fuera del panel */
    }
    if (dy >= FLEX_CORNER_R) {
        return 0;
    }
    /* Centro del arco en (R, R) medido desde la esquina; la fila dy (centro
     * del píxel en dy + 0,5) corta el arco en x = R - sqrt(R^2 - (R-dy-0,5)^2).
     * En cuartos de píxel para no perder la mitad. */
    const int32_t r4 = FLEX_CORNER_R * 4;
    const int32_t d4 = r4 - (dy * 4 + 2);
    const uint32_t s4 = isqrt((uint32_t)(r4 * r4 - d4 * d4));
    const int32_t in4 = r4 - (int32_t)s4;
    return (in4 + 3) / 4; /* hacia arriba */
}

static int32_t edge_dist(int32_t y1, int32_t y2)
{
    const int32_t top = y1;
    const int32_t bottom = (FLEX_PANEL_H - 1) - y2;
    return top < bottom ? top : bottom;
}

int32_t flex_safe_left(int32_t y1, int32_t y2, int32_t m)
{
    return flex_corner_inset(edge_dist(y1, y2)) + m;
}

int32_t flex_safe_right(int32_t y1, int32_t y2, int32_t m)
{
    return (FLEX_PANEL_W - 1) - flex_corner_inset(edge_dist(y1, y2)) - m;
}

bool flex_area_visible(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t m)
{
    if (x1 < m || y1 < m || x2 > FLEX_PANEL_W - 1 - m || y2 > FLEX_PANEL_H - 1 - m) {
        return false;
    }
    return x1 >= flex_safe_left(y1, y2, m) && x2 <= flex_safe_right(y1, y2, m);
}
