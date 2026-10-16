/*
 * watch_imu — ver include/watch_imu.h
 */
#include "watch_imu.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "qmi8658.h"
#include "sdkconfig.h"

static const char *TAG = "watch_imu";

#define I2C_TIMEOUT_MS   50
#define PROBE_TIMEOUT_MS 50
#define POLL_MS          20    /* > ODR de 56 Hz: no se pierden muestras */
#define IDLE_WAIT_MS     1000
#define TASK_STACK       3072
#define TASK_PRIO        2
#define TASK_CORE        0
#define NOTE_ACTIVE      (1u << 0)

#ifdef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define IMU_SCL_HZ CONFIG_BSP_I2C_CLK_SPEED_HZ
#else
#define IMU_SCL_HZ 400000
#endif

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static watch_imu_snapshot_t s_snap;
static uint32_t s_gen;
static int s_users;
static bool s_started;

static void set_state(imu_state_t st)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap.state = st;
    xSemaphoreGive(s_lock);
}

static void count_error(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap.errors++;
    xSemaphoreGive(s_lock);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    const esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        count_error();
    }
    return err;
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    const esp_err_t err = i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        count_error();
    }
    return err;
}

/* Reinicio por software y configuración (sensores apagados). */
static bool configure(void)
{
    if (wr(QMI_REG_RESET, QMI_RESET_CMD) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20)); /* el reinicio tarda hasta 15 ms: nada de escrituras */
    uint8_t v = 0;
    bool done = false;
    for (int i = 0; i < 50 && !done; i++) {
        done = rd(QMI_REG_RESET_OK, &v, 1) == ESP_OK && v == QMI_RESET_DONE;
        if (!done) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (!done) {
        ESP_LOGE(TAG, "el reinicio no termino (0x4D = 0x%02x)", v);
        return false;
    }
    if (rd(QMI_REG_WHO_AM_I, &v, 1) != ESP_OK || v != QMI_WHO_AM_I) {
        ESP_LOGE(TAG, "identidad 0x%02x tras el reinicio", v);
        return false;
    }
    for (size_t i = 0; i < qmi_config_len; i++) {
        if (wr(qmi_config[i].reg, qmi_config[i].val) != ESP_OK) {
            return false;
        }
    }
    uint8_t r[9];
    if (rd(QMI_REG_CTRL1, r, sizeof(r)) != ESP_OK || !qmi_config_verify(r)) {
        ESP_LOGE(TAG, "la configuracion no se confirmo: %02x %02x %02x . %02x . %02x %02x", r[0], r[1], r[2], r[4],
                 r[6], r[7]);
        return false;
    }
    /* En reposo hasta que se pida medir. */
    return wr(QMI_REG_CTRL1, QMI_CTRL1_SLEEP) == ESP_OK;
}

static bool sensors_on(void)
{
    if (wr(QMI_REG_CTRL1, QMI_CTRL1_RUN) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(15)); /* el oscilador arranca en hasta 15 ms */
    if (wr(QMI_REG_CTRL7, QMI_CTRL7_ON) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(QMI_SETTLE_MS)); /* el giroscopio necesita ~200 ms */
    return true;
}

static void sensors_off(void)
{
    wr(QMI_REG_CTRL7, 0x00);
    wr(QMI_REG_CTRL1, QMI_CTRL1_SLEEP);
}

static void imu_task(void *arg)
{
    (void)arg;
    bool running = false;
    bool have_ts = false;
    uint32_t last_ts = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool want = s_users > 0;
        const imu_state_t st = s_snap.state;
        xSemaphoreGive(s_lock);

        if (st == IMU_ABSENT || st == IMU_ERROR) {
            vTaskDelete(NULL);
            return;
        }
        if (want && !running) {
            set_state(IMU_STARTING);
            running = sensors_on();
            have_ts = false;
            set_state(running ? IMU_RUNNING : IMU_IDLE);
            ESP_LOGI(TAG, "sensores %s", running ? "en marcha" : "NO arrancaron");
        } else if (!want && running) {
            sensors_off();
            running = false;
            set_state(IMU_IDLE);
            ESP_LOGI(TAG, "en reposo");
        }
        if (!running) {
            xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(IDLE_WAIT_MS));
            continue;
        }
        uint8_t raw[QMI_BURST_LEN];
        if (rd(QMI_REG_STATUS0, raw, sizeof(raw)) == ESP_OK) {
            qmi_sample_t smp;
            qmi_decode(raw, &smp);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_snap.reads++;
            if (smp.fresh) {
                if (have_ts) {
                    const uint32_t d = qmi_ts_delta(last_ts, smp.ts);
                    s_snap.missed += d > 1 ? d - 1 : 0;
                }
                have_ts = true;
                last_ts = smp.ts;
                for (int i = 0; i < 3; i++) {
                    s_snap.acc_mg[i] = qmi_mg(smp.a[i]);
                    s_snap.gyro_mdps[i] = qmi_mdps(smp.g[i]);
                }
                s_snap.temp_c100 = qmi_temp_c100(smp.temp);
                s_snap.samples++;
                s_snap.last_us = esp_timer_get_time();
                __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
            }
            xSemaphoreGive(s_lock);
        }
        xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t watch_imu_start(i2c_master_bus_handle_t bus)
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
    s_started = true;
    s_snap.state = IMU_ABSENT;
    if (i2c_master_probe(bus, QMI_ADDR, PROBE_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "el QMI8658 no responde en 0x%02x: sin IMU", QMI_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI_ADDR,
        .scl_speed_hz = IMU_SCL_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t id = 0, rev = 0;
    if (rd(QMI_REG_WHO_AM_I, &id, 1) != ESP_OK || id != QMI_WHO_AM_I) {
        ESP_LOGW(TAG, "en 0x%02x hay otro chip (id 0x%02x): no se le escribe nada", QMI_ADDR, id);
        s_snap.who_am_i = id;
        return ESP_ERR_NOT_FOUND;
    }
    rd(QMI_REG_REVISION, &rev, 1); /* solo para el registro: la hoja de datos se contradice */
    s_snap.who_am_i = id;
    s_snap.revision = rev;
    if (!configure()) {
        s_snap.state = IMU_ERROR;
        return ESP_FAIL;
    }
    s_snap.state = IMU_IDLE;
    ESP_LOGI(TAG, "QMI8658 (id 0x%02x, revision 0x%02x) configurado: ±4 g, ±512 dps, 56 Hz; en reposo", id, rev);
    if (xTaskCreatePinnedToCore(imu_task, "watch_imu", TASK_STACK, NULL, TASK_PRIO, &s_task, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool watch_imu_get(watch_imu_snapshot_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_lock);
    return true;
}

uint32_t watch_imu_generation(void)
{
    return __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
}

void watch_imu_set_active(bool on)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_users += on ? 1 : -1;
    if (s_users < 0) {
        s_users = 0;
    }
    xSemaphoreGive(s_lock);
    if (s_task) {
        xTaskNotify(s_task, NOTE_ACTIVE, eSetBits);
    }
}
