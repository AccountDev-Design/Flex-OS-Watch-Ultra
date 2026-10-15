/*
 * flex_icons — iconos vectoriales de Flex OS Ultra dibujados con primitivas
 * de LVGL 9 (sin mapas de bits, como en la referencia: FlexOS_Ultra_Icons.h y
 * FlexOS_Ultra_QuickPanel.h). Cada icono cabe en su caja y se escala con ella.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_ICON_BOLT = 0,
    FLEX_ICON_CHECK,
    FLEX_ICON_CROSS,
    FLEX_ICON_COUNT_
} flex_icon_t;

/** Dibuja el icono dentro de box (llamar desde un evento de dibujo). */
void flex_icon_draw(lv_layer_t *layer, flex_icon_t icon, const lv_area_t *box, lv_color_t color, lv_opa_t opa);

/** Objeto LVGL de size x size que dibuja el icono (sin hijos ni imágenes). */
lv_obj_t *flex_icon_create(lv_obj_t *parent, flex_icon_t icon, int32_t size, uint32_t rgb);
void flex_icon_set_color(lv_obj_t *obj, uint32_t rgb);

#ifdef __cplusplus
}
#endif
