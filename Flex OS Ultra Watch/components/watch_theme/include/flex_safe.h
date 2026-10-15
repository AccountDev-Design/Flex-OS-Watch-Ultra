/*
 * flex_safe — área VISIBLE del panel con sus esquinas redondeadas (lógica
 * PURA, se prueba en el PC).
 *
 * El AMOLED de 410x502 no es rectangular a la vista: sus cuatro esquinas son
 * arcos grandes. En las fotos del reloj (2026-10-15) el icono de batería del
 * panel rápido quedaba cortado en la esquina superior derecha y el texto de
 * una sección en la inferior izquierda. Ajustando un círculo tangente a los
 * dos bordes a los puntos de corte de esas fotos sale un radio de ~113-126 px;
 * se usa FLEX_CORNER_R = 124 hasta medirlo con las guías de la prueba de
 * pantalla (prueba T44). Todo lo que se coloca cerca de una esquina pregunta
 * aquí cuánto margen necesita.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_PANEL_W     410
#define FLEX_PANEL_H     502
#define FLEX_CORNER_R    124   /* ESTIMADO de fotos; pendiente de T44 */
#define FLEX_SAFE_MARGIN 4     /* aire mínimo entre el contenido y el borde visible */

/**
 * Margen horizontal que el arco de esquina quita a la fila situada a dy
 * píxeles del borde superior (o inferior): 0 si dy >= FLEX_CORNER_R.
 * Redondeado hacia arriba (nunca promete un píxel que no se ve).
 */
int32_t flex_corner_inset(int32_t dy);

/**
 * Primera x visible (con margen m) para todo lo que ocupe las filas y1..y2:
 * manda la fila más cercana al borde superior o inferior.
 */
int32_t flex_safe_left(int32_t y1, int32_t y2, int32_t m);
/** Última x visible (inclusive) para las filas y1..y2, con margen m. */
int32_t flex_safe_right(int32_t y1, int32_t y2, int32_t m);

/** ¿Se ve entero el rectángulo (inclusive) con un margen m hasta el borde visible? */
bool flex_area_visible(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t m);

#ifdef __cplusplus
}
#endif
