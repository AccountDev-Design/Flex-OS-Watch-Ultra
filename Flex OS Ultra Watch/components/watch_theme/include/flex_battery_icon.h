/*
 * flex_battery_icon — indicador de batería del tema Flex OS Ultra.
 *
 * Un único objeto LVGL que dibuja contorno, polo, relleno proporcional y glifo
 * (rayo, completado, tachado) con primitivas vectoriales, siguiendo el trazo
 * de drawBattery() de la referencia (FlexOS_Ultra_Icons.h:342). Solo se
 * invalida cuando cambia el tipo de vista o el porcentaje.
 */
#pragma once

#include "lvgl.h"
#include "flex_batt_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Crea el indicador. Proporción recomendada 2:1 (p. ej. 34 x 17). */
lv_obj_t *flex_battery_icon_create(lv_obj_t *parent, int32_t w, int32_t h);

/** Actualiza la vista. percent = -1 si no hay dato. Devuelve true si cambió algo. */
bool flex_battery_icon_set(lv_obj_t *icon, flex_batt_kind_t kind, int percent);

/** ¿obj es un indicador de flex_battery_icon_create()? (verificación de la interfaz) */
bool flex_battery_icon_is(lv_obj_t *obj);

/** Tipo de vista que muestra ahora (pruebas y diagnóstico). */
flex_batt_kind_t flex_battery_icon_kind(lv_obj_t *icon);

/**
 * Monocromo blanco (sobre fondos de color, p. ej. el panel rápido rediseñado):
 * contorno y relleno blancos; el nivel bajo y el crítico conservan su color
 * de aviso y el glifo (rayo, completado, tachado) sigue diciendo el estado.
 */
void flex_battery_icon_set_mono(lv_obj_t *icon, bool mono);

#ifdef __cplusplus
}
#endif
