/*
 * flex_jpeg — JPEG baseline a un lienzo RGB565 con el TJpgDec que trae LVGL
 * (LV_USE_TJPGD). Lo usan el navegador (F6, páginas remotas) y los fondos de
 * las esferas fotográficas (watch_theme).
 *
 * Si la imagen no mide lo que pide el destino se amplía o reduce al vecino más
 * próximo; todo se recorta al lienzo, así una imagen ajena no puede pintar fuera
 * de su zona. El progresivo lo rechaza TJpgDec. Memoria de trabajo temporal de
 * 8 KB (malloc), nada en la pila.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** jpg a canvas (cw x ch RGB565) en (dx,dy) con tamaño dw x dh. 0 = bien; <0 o código de TJpgDec si no. */
int flex_jpeg_draw(const uint8_t *jpg, uint32_t n, uint16_t *canvas, int cw, int ch, int dx, int dy, int dw, int dh);

#ifdef __cplusplus
}
#endif
