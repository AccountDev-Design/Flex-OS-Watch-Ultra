/*
 * flex_blur — ver include/flex_blur.h
 */
#include "flex_blur.h"

#include <string.h>

void flex_blur_small_size(int w, int h, int *sw, int *sh)
{
    *sw = (w + FLEX_BLUR_DOWN - 1) / FLEX_BLUR_DOWN;
    *sh = (h + FLEX_BLUR_DOWN - 1) / FLEX_BLUR_DOWN;
}

size_t flex_blur_small_bytes(int w, int h)
{
    int sw, sh;
    flex_blur_small_size(w, h, &sw, &sh);
    return (size_t)sw * (size_t)sh * 3;
}

size_t flex_blur_tmp_bytes(int w, int h)
{
    int sw, sh;
    flex_blur_small_size(w, h, &sw, &sh);
    return (size_t)(sw > sh ? sw : sh) * 3;
}

void flex_blur_begin(flex_blur_t *b, const uint16_t *src, uint16_t *dst, int w, int h, uint8_t *small,
                     uint8_t *tmp, uint8_t dim)
{
    memset(b, 0, sizeof(*b));
    b->src = src;
    b->dst = dst;
    b->small = small;
    b->tmp = tmp;
    b->w = w;
    b->h = h;
    b->dim = dim;
    flex_blur_small_size(w, h, &b->sw, &b->sh);
}

/* Una fila de la imagen reducida: promedio de su bloque 4x4 (el último
 * bloque, recortado al borde). Canales a 8 bits. */
static void down_row(flex_blur_t *b, int sy)
{
    const int y0 = sy * FLEX_BLUR_DOWN;
    const int y1 = y0 + FLEX_BLUR_DOWN > b->h ? b->h : y0 + FLEX_BLUR_DOWN;
    uint8_t *out = b->small + (size_t)sy * (size_t)b->sw * 3;
    for (int sx = 0; sx < b->sw; sx++) {
        const int x0 = sx * FLEX_BLUR_DOWN;
        const int x1 = x0 + FLEX_BLUR_DOWN > b->w ? b->w : x0 + FLEX_BLUR_DOWN;
        uint32_t r = 0, g = 0, bl = 0, n = 0;
        for (int y = y0; y < y1; y++) {
            const uint16_t *row = b->src + (size_t)y * (size_t)b->w;
            for (int x = x0; x < x1; x++) {
                const uint16_t p = row[x];
                r += (p >> 11) & 0x1F;
                g += (p >> 5) & 0x3F;
                bl += p & 0x1F;
                n++;
            }
        }
        /* 5/6 bits -> 8 bits con redondeo */
        out[3 * sx + 0] = (uint8_t)((r * 255 + n * 31 / 2) / (n * 31));
        out[3 * sx + 1] = (uint8_t)((g * 255 + n * 63 / 2) / (n * 63));
        out[3 * sx + 2] = (uint8_t)((bl * 255 + n * 31 / 2) / (n * 31));
    }
}

/* Caja de radio R sobre n muestras de 3 canales separadas por stride
 * (bytes), con los bordes extendidos. */
static void box(uint8_t *data, int n, int stride, uint8_t *tmp)
{
    const int R = FLEX_BLUR_RADIUS, win = 2 * R + 1;
    for (int i = 0; i < n; i++) {
        memcpy(tmp + 3 * i, data + (size_t)i * (size_t)stride, 3);
    }
    for (int c = 0; c < 3; c++) {
        int acc = 0;
        for (int k = -R; k <= R; k++) {
            const int j = k < 0 ? 0 : (k >= n ? n - 1 : k);
            acc += tmp[3 * j + c];
        }
        for (int i = 0; i < n; i++) {
            data[(size_t)i * (size_t)stride + c] = (uint8_t)((acc + win / 2) / win);
            const int out = i - R < 0 ? 0 : i - R;
            const int in = i + R + 1 >= n ? n - 1 : i + R + 1;
            acc += tmp[3 * in + c] - tmp[3 * out + c];
        }
    }
}

static const uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static inline uint32_t q(uint32_t v8, int bits, uint32_t th16)
{
    /* v8 (0..255) a 'bits' bits con tramado: th16 en 0..15 */
    const uint32_t max = (1u << bits) - 1;
    const uint32_t s = v8 * max * 16 + th16 * 255;
    uint32_t o = s / (255 * 16);
    return o > max ? max : o;
}

/* Fila y de salida: bilineal desde la pequeña (centros de bloque) y oscurecida. */
static void up_row(flex_blur_t *b, int y)
{
    /* posición en la pequeña, en 1/256: centro del píxel menos medio bloque */
    int32_t fy = ((2 * y + 1) * 256) / (2 * FLEX_BLUR_DOWN) - 128;
    if (fy < 0) {
        fy = 0;
    }
    int sy0 = fy >> 8;
    if (sy0 > b->sh - 1) {
        sy0 = b->sh - 1;
    }
    const int sy1 = sy0 + 1 < b->sh ? sy0 + 1 : sy0;
    const uint32_t wy = (uint32_t)(fy & 0xFF);
    const uint8_t *r0 = b->small + (size_t)sy0 * (size_t)b->sw * 3;
    const uint8_t *r1 = b->small + (size_t)sy1 * (size_t)b->sw * 3;
    uint16_t *out = b->dst + (size_t)y * (size_t)b->w;
    for (int x = 0; x < b->w; x++) {
        int32_t fx = ((2 * x + 1) * 256) / (2 * FLEX_BLUR_DOWN) - 128;
        if (fx < 0) {
            fx = 0;
        }
        int sx0 = fx >> 8;
        if (sx0 > b->sw - 1) {
            sx0 = b->sw - 1;
        }
        const int sx1 = sx0 + 1 < b->sw ? sx0 + 1 : sx0;
        const uint32_t wx = (uint32_t)(fx & 0xFF);
        uint32_t ch[3];
        for (int c = 0; c < 3; c++) {
            const uint32_t a = r0[3 * sx0 + c] * (256 - wx) + r0[3 * sx1 + c] * wx;
            const uint32_t d = r1[3 * sx0 + c] * (256 - wx) + r1[3 * sx1 + c] * wx;
            const uint32_t v = (a * (256 - wy) + d * wy) >> 16; /* 0..255 */
            ch[c] = v * b->dim / 255;
        }
        const uint32_t th = kBayer4[y & 3][x & 3];
        out[x] = (uint16_t)((q(ch[0], 5, th) << 11) | (q(ch[1], 6, th) << 5) | q(ch[2], 5, th));
    }
}

bool flex_blur_step(flex_blur_t *b, int rows)
{
    if (rows < 1) {
        rows = 1;
    }
    while (rows > 0 && b->stage < 3) {
        switch (b->stage) {
        case 0: /* cada fila pequeña lee 4 filas: cuenta como 4 */
            down_row(b, b->row++);
            rows -= FLEX_BLUR_DOWN;
            if (b->row >= b->sh) {
                b->stage = 1;
                b->row = 0;
            }
            break;
        case 1: /* la imagen pequeña es barata: todas las pasadas de una vez */
            for (int p = 0; p < FLEX_BLUR_PASSES; p++) {
                for (int sy = 0; sy < b->sh; sy++) {
                    box(b->small + (size_t)sy * (size_t)b->sw * 3, b->sw, 3, b->tmp);
                }
                for (int sx = 0; sx < b->sw; sx++) {
                    box(b->small + 3 * (size_t)sx, b->sh, b->sw * 3, b->tmp);
                }
            }
            rows -= b->sh;
            b->stage = 2;
            b->row = 0;
            break;
        default:
            up_row(b, b->row++);
            rows--;
            if (b->row >= b->h) {
                b->stage = 3;
            }
            break;
        }
    }
    return b->stage >= 3;
}

void flex_blur_run(flex_blur_t *b)
{
    while (!flex_blur_step(b, 1 << 20)) {
    }
}
