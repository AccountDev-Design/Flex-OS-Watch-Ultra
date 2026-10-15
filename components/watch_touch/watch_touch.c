/*
 * watch_touch — ver include/watch_touch.h
 */
#include "watch_touch.h"

#include "esp_check.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"

static const char *TAG = "watch_touch";

esp_err_t watch_touch_start(lv_display_t *disp, lv_indev_t **out_indev)
{
    ESP_RETURN_ON_FALSE(disp && out_indev, ESP_ERR_INVALID_ARG, TAG, "argumentos");

    /* bsp_touch_new inicializa el bus I2C compartido si aún no existe. */
    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &tp), TAG, "bsp_touch_new");

    const lvgl_port_touch_cfg_t cfg = {
        .disp = disp,
        .handle = tp,
    };
    lv_indev_t *indev = lvgl_port_add_touch(&cfg);
    ESP_RETURN_ON_FALSE(indev, ESP_FAIL, TAG, "lvgl_port_add_touch");

    *out_indev = indev;
    ESP_LOGI(TAG, "FT3168 registrado como entrada de LVGL");
    return ESP_OK;
}
