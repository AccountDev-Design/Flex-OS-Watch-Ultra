/*
 * flex_thumb — ver include/flex_thumb.h
 */
#include "flex_thumb.h"

#include <math.h>
#include <stddef.h>

typedef struct {
    int16_t i0;                       /* primer píxel de la captura */
    uint8_t n;                        /* cuántos */
    uint16_t w[FLEX_THUMB_MAX_TAPS];  /* pesos: suman 256 */
} taps_t;

/* Tramo [a, a + step) de la captura que cubre el píxel i de la miniatura. */
static int make_taps(double a, double step, int sn, taps_t *t)
{
    const double b = a + step;
    int i0 = (int)floor(a);
    int i1 = (int)ceil(b) - 1;
    i0 = i0 < 0 ? 0 : i0;
    i1 = i1 > sn - 1 ? sn - 1 : i1;
    const int n = i1 - i0 + 1;
    if (n < 1 || n > FLEX_THUMB_MAX_TAPS) {
        return -1;
    }
    double wf[FLEX_THUMB_MAX_TAPS], tot = 0;
    for (int k = 0; k < n; k++) {
        const double lo = a > i0 + k ? a : i0 + k;
        const double hi = b < i0 + k + 1 ? b : i0 + k + 1;
        wf[k] = hi > lo ? hi - lo : 0;
        tot += wf[k];
    }
    int sum = 0, big = 0;
    for (int k = 0; k < n; k++) {
        t->w[k] = (uint16_t)lround(wf[k] / tot * 256.0);
        sum += t->w[k];
        big = t->w[k] > t->w[big] ? k : big;
    }
    t->w[big] = (uint16_t)(t->w[big] + 256 - sum); /* el redondeo, al de más peso */
    t->i0 = (int16_t)i0;
    t->n = (uint8_t)n;
    return 0;
}

/* Cobertura (0..1) del rectángulo redondeado en el píxel (x, y). */
static float corner_cov(int x, int y, int w, int h, int r)
{
    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
    float cx, cy;
    if (px < r) {
        cx = (float)r;
    } else if (px > w - r) {
        cx = (float)(w - r);
    } else {
        return 1.0f;
    }
    if (py < r) {
        cy = (float)r;
    } else if (py > h - r) {
        cy = (float)(h - r);
    } else {
        return 1.0f;
    }
    const float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    const float c = (float)r - d + 0.5f;
    return c <= 0 ? 0.0f : (c >= 1 ? 1.0f : c);
}

size_t flex_thumb_scratch_bytes(int dw)
{
    return dw > 0 ? (size_t)dw * sizeof(taps_t) : 0;
}

int flex_thumb_cover(const uint16_t *src, int sw, int sh, int sstride, uint16_t *dst, int dw, int dh, int r,
                     void *scratch)
{
    taps_t *cols = scratch;
    if (!src || !dst || !cols || dw < 1 || dh < 1 || dw > sw || dh > sh || sstride < sw) {
        return -1;
    }
    /* Escala uniforme que llena la miniatura; el sobrante se recorta centrado */
    const double s = (double)dw / sw > (double)dh / sh ? (double)dw / sw : (double)dh / sh;
    const double step = 1.0 / s;
    const double x0 = (sw - dw * step) / 2, y0 = (sh - dh * step) / 2;
    for (int x = 0; x < dw; x++) {
        if (make_taps(x0 + x * step, step, sw, &cols[x]) != 0) {
            return -1;
        }
    }
    for (int y = 0; y < dh; y++) {
        taps_t ty;
        if (make_taps(y0 + y * step, step, sh, &ty) != 0) {
            return -1;
        }
        uint16_t *out = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            const taps_t *tx = &cols[x];
            uint32_t rr = 0, gg = 0, bb = 0;
            for (int k = 0; k < ty.n; k++) {
                const uint16_t *row = src + (size_t)(ty.i0 + k) * sstride + tx->i0;
                const uint32_t wy = ty.w[k];
                for (int j = 0; j < tx->n; j++) {
                    const uint32_t p = row[j], wxy = wy * tx->w[j];
                    rr += (p >> 11) * wxy;
                    gg += ((p >> 5) & 0x3F) * wxy;
                    bb += (p & 0x1F) * wxy;
                }
            }
            uint32_t R = (rr + 32768) >> 16, G = (gg + 32768) >> 16, B = (bb + 32768) >> 16;
            if (r > 0 && (x < r || x >= dw - r) && (y < r || y >= dh - r)) {
                const float c = corner_cov(x, y, dw, dh, r);
                R = (uint32_t)lroundf((float)R * c);
                G = (uint32_t)lroundf((float)G * c);
                B = (uint32_t)lroundf((float)B * c);
            }
            out[x] = (uint16_t)((R << 11) | (G << 5) | B);
        }
    }
    return 0;
}
