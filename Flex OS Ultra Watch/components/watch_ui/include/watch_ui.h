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
#include <stdint.h>
#include "watch_diag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool touch_ok;
    bool power_ok;
    watch_flash_report_t flash;
    /* Brillo de la pantalla (lo aporta watch_display). NULL = sin control:
     * el panel rápido lo muestra como no disponible. */
    int (*brightness_get)(void);        /**< 0..100, o < 0 si se desconoce */
    bool (*brightness_set)(int percent); /**< true si se aplicó */
    /* Apagado de pantalla (F4.5). NULL = la pantalla no se apaga nunca. */
    bool (*display_power)(bool on);      /**< DISPON/DISPOFF con el brillo del usuario */
    bool (*display_level)(int percent);  /**< brillo momentáneo (atenuar); < 0 = el del usuario */
    int (*button_level)(void);           /**< nivel del botón lateral (0/1), o -1 sin botón */
    /* Para Acerca de y Diagnóstico (NULL / 0 = no disponible) */
    const char *fw_version;              /**< versión del firmware (esp_app_desc) */
    const char *idf_version;
    uint16_t disp_buf_lines;             /**< búferes parciales de LVGL (watch_display) */
    uint32_t disp_buf_bytes;
    bool disp_double_buffer;
    bool disp_buf_psram;
    /* Motivo del último arranque (esp_reset_reason) para Device Care.
     * NULL = desconocido. reset_ok = encendido, reinicio pedido o USB. */
    const char *reset_text;
    bool reset_ok;
} watch_ui_boot_info_t;

/**
 * Muestra el splash de arranque (negro con "Flex OS" que aparece). Llamar con
 * LVGL bloqueado justo después de iniciar la pantalla, antes del resto de
 * servicios, para que la pantalla nunca se quede congelada ni en blanco.
 */
void watch_ui_boot_splash(void);

/** Arranca la interfaz y pasa del splash a la esfera. Llamar con LVGL bloqueado. */
void watch_ui_home_create(const watch_ui_boot_info_t *info);

#ifdef __cplusplus
}
#endif
