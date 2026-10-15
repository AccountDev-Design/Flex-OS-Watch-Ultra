/*
 * flex_glass_math — fórmulas PURAS del material Liquid Glass (nivel 50),
 * transcritas de FlexOS_Ultra/FlexOS_Ultra_Theme.h (referencia 5dbc949):
 *   glassLuma()    :265-268  luma en dominio 565 crudo ((r5+g6)*5 + b5*2) >> 1
 *   glassTintMix() :447-457  tinte entre TINT_MIN y TINT_MAX según la
 *                            diferencia de luma fondo/tinte (DIFF_MAX 128)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_GLASS_TINT_DIFF_MAX 128

int flex_glass_luma565(uint16_t c);

/** Mezcla de tinte (alfa 0..255) para un panel cuyo fondo suma luma_sum en n muestras. */
uint8_t flex_glass_tint_mix(uint32_t luma_sum, int n, uint16_t tint565);

/** Suma la luma del rectángulo (x, y, rw, rh) de un búfer RGB565 de ancho w (1 de cada 4 px). */
void flex_glass_region_luma(const uint16_t *buf, int w, int h, int x, int y, int rw, int rh,
                            uint32_t *sum, int *n);

#ifdef __cplusplus
}
#endif
