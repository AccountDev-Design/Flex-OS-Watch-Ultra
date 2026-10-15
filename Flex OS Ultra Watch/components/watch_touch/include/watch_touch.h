/*
 * watch_touch — táctil capacitivo FT3168 (I2C 0x38, INT GPIO38, RST GPIO9).
 *
 * Usa bsp_touch_new() del BSP oficial (driver esp_lcd_touch_ft5x06, compatible
 * con FT3x68) y lo registra como dispositivo de entrada de LVGL.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t watch_touch_start(lv_display_t *disp, lv_indev_t **out_indev);

#ifdef __cplusplus
}
#endif
