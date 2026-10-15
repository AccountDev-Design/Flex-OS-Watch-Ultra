/*
 * watch_time — ver include/watch_time.h
 *
 * Acceso al PCF85063: cada lectura es UNA transacción I2C "escribir el
 * puntero de registro 00h + leer 11 bytes" (i2c_master_transmit_receive). El
 * byte escrito es solo el puntero; no se modifica ningún registro del RTC. El
 * driver i2c_master serializa el bus que se comparte con el táctil y el
 * AXP2101.
 */
#include "watch_time.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "watch_time";

#define I2C_TIMEOUT_MS 50
#define PROBE_TIMEOUT_MS 50
#define TASK_STACK     3072
#define TASK_PRIO      2
#define TASK_CORE      0

#ifdef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define RTC_SCL_HZ CONFIG_BSP_I2C_CLK_SPEED_HZ
#else
#define RTC_SCL_HZ 400000
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static watch_time_info_t s_info;
static int64_t s_base_sec; /* segundos (sin zona) de la última lectura válida */
static bool s_started;

/* Devuelve true si la lectura se completó (sea cual sea la validez de la hora). */
static bool read_rtc(void)
{
    uint8_t regs[PCF85063_READ_LEN];
    const uint8_t reg = PCF85063_REG_CTRL1;
    const esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, regs, sizeof(regs), I2C_TIMEOUT_MS);
    const int64_t now_us = esp_timer_get_time();

    wt_datetime_t dt = {0};
    const rtc_read_t res = (err == ESP_OK) ? pcf85063_decode(regs, &dt) : RTC_READ_BAD_FIELD;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const watch_time_state_t prev = s_info.state;
    if (err != ESP_OK) {
        s_info.errors++;
        /* Si ya había hora válida se sigue contando con el temporizador. */
        if (s_info.state != WATCH_TIME_OK) {
            s_info.state = WATCH_TIME_NO_RTC;
        }
    } else {
        s_info.reads++;
        s_info.last_read = res;
        memcpy(s_info.regs, regs, sizeof(regs));
        if (res == RTC_READ_OK) {
            s_base_sec = wt_to_seconds(&dt);
            s_info.synced_us = now_us;
            s_info.state = WATCH_TIME_OK;
        } else {
            s_info.state = WATCH_TIME_NOT_SET;
        }
    }
    const watch_time_state_t state = s_info.state;
    const uint32_t errors = s_info.errors;
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lectura del RTC fallida: %s (errores %lu)", esp_err_to_name(err), (unsigned long)errors);
        return false;
    }
    if (res == RTC_READ_OK || res == RTC_READ_IMPLAUSIBLE) {
        ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d (%s)", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                 dt.second, pcf85063_read_text(res));
    } else {
        ESP_LOGW(TAG, "RTC sin hora fiable: %s", pcf85063_read_text(res));
    }
    if (state != prev || res != RTC_READ_OK) {
        ESP_LOGI(TAG, "registros 00h-0Ah: %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x", regs[0],
                 regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7], regs[8], regs[9], regs[10]);
    }
    return true;
}

static void time_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool ok = false;
        if (!s_info.rtc_found) {
            /* El RTC no respondió al arrancar: se vuelve a buscar. */
            if (i2c_master_probe(s_bus, PCF85063_ADDR, PROBE_TIMEOUT_MS) == ESP_OK) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.rtc_found = true;
                xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "PCF85063 encontrado en 0x%02x", PCF85063_ADDR);
            }
        }
        if (s_info.rtc_found) {
            ok = read_rtc();
        } else {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_info.state = WATCH_TIME_NO_RTC;
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? WATCH_TIME_RESYNC_MS : WATCH_TIME_RETRY_MS));
    }
}

esp_err_t watch_time_start(i2c_master_bus_handle_t bus)
{
    if (s_started) {
        return ESP_OK;
    }
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_bus = bus;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = RTC_SCL_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no se pudo añadir el RTC al bus: %s", esp_err_to_name(err));
        return err;
    }
    s_info.rtc_found = (i2c_master_probe(bus, PCF85063_ADDR, PROBE_TIMEOUT_MS) == ESP_OK);
    if (!s_info.rtc_found) {
        ESP_LOGW(TAG, "el PCF85063 no responde en 0x%02x; se reintentará cada %u s", PCF85063_ADDR,
                 (unsigned)(WATCH_TIME_RETRY_MS / 1000));
    }
    if (xTaskCreatePinnedToCore(time_task, "watch_time", TASK_STACK, NULL, TASK_PRIO, NULL, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

watch_time_state_t watch_time_now(wt_datetime_t *out)
{
    if (!s_started) {
        return WATCH_TIME_STARTING;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const watch_time_state_t st = s_info.state;
    const int64_t base = s_base_sec;
    const int64_t synced = s_info.synced_us;
    xSemaphoreGive(s_lock);
    if (st == WATCH_TIME_OK) {
        wt_from_seconds(base + (esp_timer_get_time() - synced) / 1000000, out);
    }
    return st;
}

void watch_time_info(watch_time_info_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}
