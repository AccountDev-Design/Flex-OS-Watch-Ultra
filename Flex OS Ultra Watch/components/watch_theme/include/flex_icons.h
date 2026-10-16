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
    FLEX_ICON_BOLT = 0,     /**< carga */
    FLEX_ICON_CHECK,        /**< completa / correcto */
    FLEX_ICON_CROSS,        /**< sin batería / error */
    FLEX_ICON_GEAR,         /**< Ajustes */
    FLEX_ICON_SUN,          /**< brillo, Pantalla */
    FLEX_ICON_WIFI,
    FLEX_ICON_BLUETOOTH,
    FLEX_ICON_CHEVRON_LEFT, /**< volver */
    FLEX_ICON_CHEVRON_RIGHT,/**< abre una pantalla */
    FLEX_ICON_INFO,         /**< Acerca de */
    FLEX_ICON_PULSE,        /**< Diagnóstico */
    FLEX_ICON_CLOCK,        /**< hora */
    FLEX_ICON_IMAGE,        /**< fondo */
    FLEX_ICON_WATCH,        /**< esfera */
    FLEX_ICON_GLOBE,        /**< zona horaria */
    FLEX_ICON_BELL,         /**< alarmas */
    FLEX_ICON_HOURGLASS,    /**< temporizador */
    FLEX_ICON_STOPWATCH,    /**< cronómetro */
    FLEX_ICON_GRID,         /**< aplicaciones */
    FLEX_ICON_MOTION,       /**< sensores */
    FLEX_ICON_MOON,         /**< apagado de pantalla */
    FLEX_ICON_STORAGE,      /**< almacenamiento */
    FLEX_ICON_PLUS,
    FLEX_ICON_MINUS,
    FLEX_ICON_PLAY,
    FLEX_ICON_PAUSE,
    FLEX_ICON_STOP,
    FLEX_ICON_FLAG,         /**< vuelta del cronómetro */
    FLEX_ICON_COUNT_
} flex_icon_t;

/** Dibuja el icono dentro de box (llamar desde un evento de dibujo). */
void flex_icon_draw(lv_layer_t *layer, flex_icon_t icon, const lv_area_t *box, lv_color_t color, lv_opa_t opa);

/** Objeto LVGL de size x size que dibuja el icono (sin hijos ni imágenes). */
lv_obj_t *flex_icon_create(lv_obj_t *parent, flex_icon_t icon, int32_t size, uint32_t rgb);
void flex_icon_set_color(lv_obj_t *obj, uint32_t rgb);
/** ¿obj es un icono de flex_icon_create()? (verificación de la interfaz) */
bool flex_icon_is(lv_obj_t *obj);

/** Cambia el glifo (solo invalida si cambia). */
void flex_icon_set(lv_obj_t *obj, flex_icon_t icon);

#ifdef __cplusplus
}
#endif
