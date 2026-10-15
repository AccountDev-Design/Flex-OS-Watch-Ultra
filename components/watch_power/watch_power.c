/*
 * watch_power — ver include/watch_power.h
 *
 * Acceso al AXP2101: cada lectura es una transacción I2C "escribir dirección
 * de registro + leer 1 byte" (i2c_master_transmit_receive). El byte escrito es
 * solo el puntero de registro; no se modifica el contenido de ningún registro.
 * El driver i2c_master serializa las transacciones del bus que comparte con el
 * táctil, el IMU y el RTC.
 */
#include "watch_power.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "watch_power";

#define I2C_TIMEOUT_MS   50
#define TASK_STACK       3072
#define TASK_PRIO        2
#define TASK_CORE        0
/* Registro por serie: siempre que cambie algo y, como mínimo, cada 10 s. */
#define LOG_EVERY_US     (10 * 1000 * 1000)

#ifdef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define AXP_SCL_HZ CONFIG_BSP_I2C_CLK_SPEED_HZ
#else
#define AXP_SCL_HZ 400000
#endif

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static watch_power_snapshot_t s_snap;
static bool s_started;

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

/* Lee todos los registros de una muestra. VBAT ocupa dos registros que se leen
 * por separado (como XPowersLib); para no mezclar el byte alto de una
 * conversión con el bajo de otra se relee el alto y se repite si cambió. */
static esp_err_t sample(axp2101_regs_t *r)
{
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_CHIP_ID, &r->chip_id), TAG, "id");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_STATUS1, &r->status1), TAG, "status1");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_STATUS2, &r->status2), TAG, "status2");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_GAUGE_CTRL, &r->gauge_ctrl), TAG, "gauge");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_ADC_CTRL, &r->adc_ctrl), TAG, "adc");
    for (int i = 0; i < 3; i++) {
        uint8_t h2;
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_H, &r->vbat_h), TAG, "vbat_h");
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_L, &r->vbat_l), TAG, "vbat_l");
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_H, &h2), TAG, "vbat_h2");
        if (h2 == r->vbat_h) {
            break;
        }
    }
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_PERCENT, &r->percent), TAG, "percent");
    return ESP_OK;
}

static void log_sample(const watch_power_snapshot_t *s)
{
    const axp2101_regs_t *r = &s->regs;
    const axp2101_status_t *d = &s->raw;
    ESP_LOGI(TAG, "AXP2101 regs 00=%02X 01=%02X 03=%02X 18=%02X 30=%02X 34=%02X 35=%02X A4=%02X",
             r->status1, r->status2, r->chip_id, r->gauge_ctrl, r->adc_ctrl, r->vbat_h, r->vbat_l, r->percent);
    ESP_LOGI(TAG, "bateria=%s vbus_good=%d vbus_in=%d dir=%d etapa=%d adc_vbat=%d ts=%d gauge=%d | "
             "VBAT=%d mV (filtrada %d) %%=%d (mostrado %d) estado=%s nivel=%d errores=%lu",
             d->batt_present ? "si" : "no", d->vbus_good, d->vbus_in, d->dir, d->stage,
             d->vbat_adc_enabled, d->ts_adc_enabled, d->gauge_enabled,
             d->vbat_mv, s->track.vbat_mv, d->percent, s->track.percent,
             batt_state_name(s->track.state), s->track.level, (unsigned long)s->errors);
}

static void power_task(void *arg)
{
    (void)arg;
    batt_state_t last_state = BATT_STATE_UNKNOWN;
    int last_pct = -2;
    int64_t last_log = 0;

    for (;;) {
        axp2101_regs_t regs = {0};
        const esp_err_t err = sample(&regs);
        axp2101_status_t dec = {0};
        if (err == ESP_OK) {
            axp2101_decode(&regs, &dec);
        }
        const bool ok = (err == ESP_OK) && dec.chip_ok;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (ok) {
            s_snap.regs = regs;
            s_snap.raw = dec;
            s_snap.samples++;
            s_snap.last_ok_us = esp_timer_get_time();
        } else {
            s_snap.errors++;
        }
        batt_tracker_update(&s_snap.track, &dec, ok);
        const watch_power_snapshot_t copy = s_snap;
        xSemaphoreGive(s_lock);

        const int64_t now = esp_timer_get_time();
        if (copy.track.state != last_state || copy.track.percent != last_pct ||
            copy.track.charging_started || now - last_log >= LOG_EVERY_US) {
            if (ok) {
                log_sample(&copy);
            } else {
                ESP_LOGW(TAG, "lectura del AXP2101 fallida (%s, id=%02X); errores=%lu",
                         esp_err_to_name(err), regs.chip_id, (unsigned long)copy.errors);
            }
            if (copy.track.charging_started) {
                ESP_LOGI(TAG, "transicion CONFIRMADA a 'cargando'");
            }
            last_state = copy.track.state;
            last_pct = copy.track.percent;
            last_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(WATCH_POWER_PERIOD_MS));
    }
}

esp_err_t watch_power_start(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "bus I2C NULL");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "ya iniciado");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    memset(&s_snap, 0, sizeof(s_snap));
    batt_tracker_init(&s_snap.track);

    /* Comprobación de presencia: solo la dirección, sin datos. */
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, AXP2101_I2C_ADDR, I2C_TIMEOUT_MS), TAG,
                        "el AXP2101 no responde en 0x%02X", AXP2101_I2C_ADDR);

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = AXP_SCL_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add_device");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_CHIP_ID, &id), TAG, "lectura del identificador");
    if (id != AXP2101_CHIP_ID_VALUE) {
        ESP_LOGE(TAG, "identificador 0x%02X distinto de 0x%02X: no se usa", id, AXP2101_CHIP_ID_VALUE);
        return ESP_ERR_NOT_FOUND;
    }
    s_snap.driver_ok = true;
    ESP_LOGI(TAG, "AXP2101 detectado (id 0x%02X). Modo SOLO LECTURA.", id);

    BaseType_t ok = xTaskCreatePinnedToCore(power_task, "watch_power", TASK_STACK, NULL,
                                            TASK_PRIO, NULL, TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea");
    s_started = true;
    return ESP_OK;
}

bool watch_power_get(watch_power_snapshot_t *out)
{
    if (!s_started || !out) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_lock);
    return true;
}
