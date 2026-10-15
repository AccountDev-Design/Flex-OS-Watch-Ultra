/*
 * watch_ui — pantallas LVGL de Flex OS Watch Ultra.
 *
 * La pantalla de inicio es la esfera (hora del RTC, fecha y batería reales).
 * Los datos de B1 que se verifican en el reloj (batería con VBAT y USB, flash,
 * memoria, coordenadas del táctil y el marco de 1 px) pasan a la pantalla de
 * diagnóstico.
 */
#pragma once

#include <stdbool.h>
#include "watch_diag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool touch_ok;
    bool power_ok;
    watch_flash_report_t flash;
} watch_ui_boot_info_t;

/** Arranca la interfaz y carga la esfera. Llamar con LVGL bloqueado. */
void watch_ui_home_create(const watch_ui_boot_info_t *info);

#ifdef __cplusplus
}
#endif
