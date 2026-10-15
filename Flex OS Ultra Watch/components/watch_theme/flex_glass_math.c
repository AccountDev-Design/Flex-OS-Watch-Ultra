/*
 * flex_glass_math — ver include/flex_glass_math.h
 */
#include "flex_glass_math.h"
#include "flex_tokens.h"

int flex_glass_luma565(uint16_t c)
{
    const int r = (c >> 11) & 0x1F;
    const int g = (c >> 5) & 0x3F;
    const int b = c & 0x1F;
    return ((r + g) * 5 + b * 2) >> 1;
}

uint8_t flex_glass_tint_mix(uint32_t luma_sum, int n, uint16_t tint565)
{
    if (n <= 0) {
        return FLEX_GLASS_TINT_BASE;
    }
    int dif = (int)(luma_sum / (uint32_t)n) - flex_glass_luma565(tint565);
    if (dif < 0) {
        dif = -dif;
    }
    if (dif > FLEX_GLASS_TINT_DIFF_MAX) {
        dif = FLEX_GLASS_TINT_DIFF_MAX;
    }
    return (uint8_t)(FLEX_GLASS_TINT_MIN +
                     (dif * (FLEX_GLASS_TINT_MAX - FLEX_GLASS_TINT_MIN)) / FLEX_GLASS_TINT_DIFF_MAX);
}

void flex_glass_region_luma(const uint16_t *buf, int w, int h, int x, int y, int rw, int rh,
                            uint32_t *sum, int *n)
{
    *sum = 0;
    *n = 0;
    for (int j = y; j < y + rh; j += 4) {
        if (j < 0 || j >= h) {
            continue;
        }
        for (int i = x; i < x + rw; i += 4) {
            if (i < 0 || i >= w) {
                continue;
            }
            *sum += (uint32_t)flex_glass_luma565(buf[j * w + i]);
            (*n)++;
        }
    }
}

uint32_t flex_mix_rgb(uint32_t fg, uint32_t bg, uint8_t a)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const uint32_t f = (fg >> sh) & 0xFF, b = (bg >> sh) & 0xFF;
        out |= ((f * a + b * (255u - a) + 127u) / 255u) << sh;
    }
    return out;
}
