/*
 * flex_thumb — miniatura RGB565 de una captura de pantalla (lógica PURA, se
 * prueba en el PC: tests/host/test_thumb.c).
 *
 * Reduce la captura (sw x sh) a dw x dh "cubriendo": escala uniforme para que
 * la miniatura quede llena y lo que sobre en un eje se recorta por igual a
 * los dos lados (como una foto en un marco de otra proporción, sin
 * deformarla). Cada píxel de la miniatura es la MEDIA del área de la captura
 * que le corresponde (pesos fraccionarios en 8 bits por eje): sin el
 * dentado que deja tomar un píxel de cada tantos.
 *
 * Las esquinas se redondean con radio r pintando de negro (el fondo de la
 * página) lo que queda fuera, con borde suavizado: el aspecto de clip_corner
 * sin su coste por cuadro (una capa ARGB8888 por tarjeta al desplazarse).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Máximo de píxeles de la captura por píxel de la miniatura en cada eje. */
#define FLEX_THUMB_MAX_TAPS 4

/** Bytes del búfer de trabajo para una miniatura de dw píxeles de ancho (los
 * pesos de cada columna). Lo reserva quien llama, para que no ocupe RAM
 * interna fija: el selector lo pide en PSRAM solo mientras genera. */
size_t flex_thumb_scratch_bytes(int dw);

/**
 * src: sw x sh RGB565 con stride de sstride píxeles; dst: dw x dh contiguo;
 * scratch: al menos flex_thumb_scratch_bytes(dw) bytes.
 * Requiere dw <= sw, dh <= sh y una reducción de hasta FLEX_THUMB_MAX_TAPS - 1
 * veces por eje (si no, no hace nada y devuelve -1). 0 si todo bien.
 */
int flex_thumb_cover(const uint16_t *src, int sw, int sh, int sstride, uint16_t *dst, int dw, int dh, int r,
                     void *scratch);

#ifdef __cplusplus
}
#endif
