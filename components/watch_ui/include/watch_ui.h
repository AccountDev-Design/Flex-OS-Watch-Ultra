/*
 * watch_ui — pantallas LVGL de Flex OS Watch Ultra.
 *
 * B1: pantalla de inicio con la identidad de Flex OS Ultra y los datos que se
 * pueden verificar en el reloj (batería real del AXP2101, flash, memoria,
 * coordenadas del táctil y un marco de 1 px para comprobar el desfase).
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

/** Crea y carga la pantalla de inicio. Llamar con LVGL bloqueado. */
void watch_ui_home_create(const watch_ui_boot_info_t *info);

#ifdef __cplusplus
}
#endif
