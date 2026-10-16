/*
 * Flex OS Watch Ultra — B1 "Arranque verificable"
 * Waveshare ESP32-S3-Touch-AMOLED-2.06 · ESP-IDF 5.5.5 · LVGL 9.5.0
 *
 * Orden de arranque:
 *   0. ajustes guardados (NVS, watch_settings): brillo, fondo, hora...
 *   1. bus I2C compartido (BSP oficial)
 *   2. pantalla CO5300 + LVGL (watch_display) y splash de arranque (watch_ui)
 *   3. táctil FT3168 (watch_touch)        — si falla, se sigue sin táctil
 *   4. AXP2101 en solo lectura (watch_power) — si falla, batería "sin datos"
 *   5. RTC PCF85063 (watch_time; lo escribe solo al poner en hora) — si falla, hora "no disponible"
 *   6. sonda de flash de solo lectura (watch_diag)
 *   7. interfaz: del splash a la esfera (watch_ui)
 */
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp/esp-bsp.h"
#include "watch_diag.h"
#include "watch_display.h"
#include "watch_power.h"
#include "watch_settings.h"
#include "watch_time.h"
#include "watch_theme.h"
#include "watch_touch.h"
#include "watch_ui.h"

static const char *TAG = "flex_watch";

/* Brillo para la interfaz; la interfaz lo guarda en watch_settings al soltar. */
static bool ui_brightness_set(int percent)
{
    return watch_display_set_brightness(percent) == ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Flex OS Watch Ultra %s (ESP-IDF %s)", app->version, esp_get_idf_version());
    watch_diag_log_mem("inicio");

    /* Sin NVS el reloj sigue: ajustes por defecto en RAM (la interfaz avisa). */
    esp_err_t err = watch_settings_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ajustes sin guardar: %s", esp_err_to_name(err));
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    watch_theme_set_wallpaper((flex_wall_t)ws.wallpaper);

    ESP_ERROR_CHECK(bsp_i2c_init());

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(watch_display_start(&disp));
    watch_display_set_brightness(ws.brightness);
    watch_diag_log_mem("tras pantalla y LVGL");

    /* Splash animado mientras arranca el resto (la pantalla nunca queda congelada). */
    if (watch_display_lock(0)) {
        watch_ui_boot_splash();
        watch_display_unlock();
    }

    watch_ui_boot_info_t boot = {0};
    boot.brightness_get = watch_display_get_brightness;
    boot.brightness_set = ui_brightness_set;
    boot.fw_version = app->version;
    boot.idf_version = esp_get_idf_version();
    watch_display_info_t dinfo;
    watch_display_get_info(&dinfo);
    boot.disp_buf_lines = dinfo.buf_lines;
    boot.disp_buf_bytes = dinfo.buf_bytes;
    boot.disp_double_buffer = dinfo.double_buffer;
    boot.disp_buf_psram = dinfo.buf_in_psram;

    lv_indev_t *indev = NULL;
    err = watch_touch_start(disp, &indev);
    boot.touch_ok = (err == ESP_OK);
    if (!boot.touch_ok) {
        ESP_LOGE(TAG, "tactil no disponible: %s", esp_err_to_name(err));
    }

    err = watch_power_start(bsp_i2c_get_handle());
    boot.power_ok = (err == ESP_OK);
    if (!boot.power_ok) {
        ESP_LOGE(TAG, "AXP2101 no disponible: %s", esp_err_to_name(err));
    }

    watch_time_set_tz(ws.tz_min); /* el RTC guarda UTC; la zona viene de los ajustes */
    err = watch_time_start(bsp_i2c_get_handle());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "servicio de hora no disponible: %s", esp_err_to_name(err));
    }

    watch_diag_flash_probe(&boot.flash);

    if (watch_display_lock(0)) {
        watch_ui_home_create(&boot);
        watch_display_unlock();
    }
    watch_diag_log_mem("tras crear la interfaz");
}
