/*
 * flex_wallpaper — ver include/flex_wallpaper.h
 */
#include "flex_wallpaper.h"
#include "flex_tokens.h"

static uint32_t isqrt32(uint32_t v)
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

/* Matriz de Bayer 4x4, valores 0..15. */
static const uint8_t kBayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

/* Canal de 8 bits -> n bits con tramado: umbral (0..15) escalado al paso. */
static inline uint8_t dither_ch(uint32_t v8x16, int shift, uint8_t th)
{
    /* v8x16 = valor en 8 bits * 16 (precisión extra); paso = 1 << shift (en 8 bits) */
    const uint32_t step16 = (1u << shift) * 16u;
    uint32_t q = v8x16 / step16;
    const uint32_t rem = v8x16 % step16;
    if (rem * 16u > (uint32_t)th * step16) {
        q++;
    }
    const uint32_t max = 0xFFu >> shift;
    return (uint8_t)(q > max ? max : q);
}

void flex_wallpaper_onyx(uint16_t *buf, int w, int h)
{
    const int cx = FLEX_ONYX_CX_480 * w / 480;
    const int cy = FLEX_ONYX_CY_800 * h / 800;
    const int r_in = FLEX_ONYX_R_IN_480 * w / 480;
    const int r_out = FLEX_ONYX_R_OUT_480 * w / 480;
    const uint32_t hr = (FLEX_ONYX_HALO_RGB >> 16) & 0xFF;
    const uint32_t hg = (FLEX_ONYX_HALO_RGB >> 8) & 0xFF;
    const uint32_t hb = FLEX_ONYX_HALO_RGB & 0xFF;

    for (int y = 0; y < h; y++) {
        const int dy = y - cy;
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            const int d = (int)isqrt32((uint32_t)(dx * dx + dy * dy));
            uint32_t a;
            if (d <= r_in) {
                a = FLEX_ONYX_ALPHA;
            } else if (d >= r_out) {
                a = 0;
            } else {
                a = (uint32_t)FLEX_ONYX_ALPHA * (uint32_t)(r_out - d) / (uint32_t)(r_out - r_in);
            }
            if (a == 0) {
                buf[y * w + x] = 0;
                continue;
            }
            /* halo * alfa sobre negro, con 4 bits extra de precisión */
            const uint8_t th = kBayer4[y & 3][x & 3];
            const uint8_t r5 = dither_ch(hr * a * 16u / 255u, 3, th);
            const uint8_t g6 = dither_ch(hg * a * 16u / 255u, 2, th);
            const uint8_t b5 = dither_ch(hb * a * 16u / 255u, 3, th);
            buf[y * w + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
}
