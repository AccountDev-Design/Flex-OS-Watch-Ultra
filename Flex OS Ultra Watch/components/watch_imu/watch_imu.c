/*
 * watch_imu — ver include/watch_imu.h
 */
#include "watch_imu.h"

#include <string.h>
#include "esp_heap_caps.h"
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
#define POLL_MS          20    /* ~49 lecturas/s del ODR de 56 Hz: el contador del chip cuenta las perdidas */
#define IDLE_WAIT_MS     1000
#define TASK_STACK       3072
#define TASK_PRIO        2
#define TASK_CORE        0
#define NOTE_ACTIVE      (1u << 0)
#define STREAM_MIN_MS    POLL_MS
#define STREAM_MAX_MS    100
#define RECOVER_ERRORS   25    /* lecturas fallidas seguidas (~0,5 s): reencender */
#define RECOVER_GAP_US   2000000

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
static bool s_hdg_reset;

/* Flujo de muestras: un consumidor, anillo reservado una vez (bajo s_lock). */
static watch_imu_sample_t *s_ring;
static uint16_t s_cap, s_head, s_count;
static bool s_stream_on;
static uint16_t s_stream_ms = POLL_MS;
static TaskHandle_t s_notify;
static uint32_t s_notify_bit;
static bool s_notified;
static uint32_t s_unread_drops;
static watch_imu_stream_stats_t s_sst;

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

/* Una muestra nueva al anillo del flujo (con s_lock tomado). */
static void stream_push_locked(uint32_t index, int64_t now_us, bool restart)
{
    if (!s_stream_on || !s_ring) {
        return;
    }
    if (s_count == s_cap) {
        s_head = (uint16_t)((s_head + 1) % s_cap); /* se pierde la más antigua */
        s_count--;
        s_sst.dropped++;
        s_unread_drops++;
    }
    watch_imu_sample_t *o = &s_ring[(s_head + s_count) % s_cap];
    o->index = index;
    o->t_ms = (uint32_t)(now_us / 1000);
    for (int i = 0; i < 3; i++) {
        const int32_t v = s_snap.acc_mg[i];
        o->a_mg[i] = (int16_t)(v > INT16_MAX ? INT16_MAX : (v < INT16_MIN ? INT16_MIN : v));
    }
    o->restart = restart ? 1 : 0;
    o->pad = 0;
    s_count++;
    s_sst.pushed++;
    if (!s_notified && s_count >= s_cap / 2 && s_notify) {
        s_notified = true;
        xTaskNotify(s_notify, s_notify_bit, eSetBits);
    }
}

static void imu_task(void *arg)
{
    (void)arg;
    bool running = false;
    bool have_ts = false;
    bool restart = true;
    uint32_t last_ts = 0;
    uint32_t index = 0;
    int fails = 0;
    int64_t last_recover = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool want = s_users > 0 || s_stream_on;
        const uint32_t period = s_users > 0 ? POLL_MS : s_stream_ms;
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
            restart = true;
            fails = 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_snap.last_us = 0; /* la integración empieza de nuevo (sin hueco) */
            xSemaphoreGive(s_lock);
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
            fails = 0;
            qmi_sample_t smp;
            qmi_decode(raw, &smp);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_snap.reads++;
            if (smp.fresh) {
                /* Contador del chip: 0 = la misma muestra otra vez (el flujo
                 * no la repite); > 1 = se perdieron muestras. */
                uint32_t d = 1;
                if (have_ts) {
                    d = qmi_ts_delta(last_ts, smp.ts);
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
                const int64_t now = esp_timer_get_time();
                if (s_hdg_reset) {
                    s_hdg_reset = false;
                    imu_heading_reset(&s_snap.heading);
                }
                /* Rumbo relativo: cada muestra se integra (la interfaz solo
                 * copia instantáneas y perdería giros entre ellas). */
                if (s_snap.last_us > 0 && s_snap.samples > 1) {
                    imu_heading_update(&s_snap.heading, s_snap.acc_mg, s_snap.gyro_mdps,
                                       (uint32_t)((now - s_snap.last_us) / 1000));
                }
                s_snap.last_us = now;
                if (d > 0) {
                    index += d;
                    stream_push_locked(index, now, restart);
                    restart = false;
                }
                __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
            }
            xSemaphoreGive(s_lock);
        } else if (++fails >= RECOVER_ERRORS && esp_timer_get_time() - last_recover >= RECOVER_GAP_US) {
            /* El bus o el chip dejaron de responder: se vuelven a encender los
             * sensores (sin reconfigurar ni escribir otros registros). */
            ESP_LOGW(TAG, "%d lecturas fallidas seguidas: se reencienden los sensores", fails);
            last_recover = esp_timer_get_time();
            fails = 0;
            sensors_off();
            running = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_sst.recoveries++;
            xSemaphoreGive(s_lock);
            continue;
        }
        xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(period));
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

void watch_imu_heading_reset(void)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_hdg_reset = true;
    xSemaphoreGive(s_lock);
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

esp_err_t watch_imu_stream_open(uint16_t cap, void *notify_task, uint32_t notify_bit)
{
    if (!s_started || cap < 8) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const imu_state_t st = s_snap.state;
    const bool busy = s_stream_on;
    xSemaphoreGive(s_lock);
    if (busy || st == IMU_ABSENT || st == IMU_ERROR || !s_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ring) {
        /* Una vez: PSRAM (no es DMA ni se toca desde interrupciones); si no
         * hay, RAM interna. */
        watch_imu_sample_t *r = heap_caps_calloc(cap, sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!r) {
            r = heap_caps_calloc(cap, sizeof(*r), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!r) {
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_ring = r;
        s_cap = cap;
        xSemaphoreGive(s_lock);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_head = s_count = 0;
    s_unread_drops = 0;
    s_notified = false;
    s_notify = (TaskHandle_t)notify_task;
    s_notify_bit = notify_bit;
    s_stream_on = true;
    s_sst.cap = s_cap;
    s_sst.period_ms = s_stream_ms;
    xSemaphoreGive(s_lock);
    xTaskNotify(s_task, NOTE_ACTIVE, eSetBits);
    ESP_LOGI(TAG, "flujo de muestras abierto (%u muestras)", (unsigned)s_cap);
    return ESP_OK;
}

size_t watch_imu_stream_read(watch_imu_sample_t *out, size_t max, uint32_t *dropped)
{
    size_t n = 0;
    if (!s_started) {
        if (dropped) {
            *dropped = 0;
        }
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (s_ring && s_count && n < max) {
        out[n++] = s_ring[s_head];
        s_head = (uint16_t)((s_head + 1) % s_cap);
        s_count--;
    }
    if (dropped) {
        *dropped = s_unread_drops;
    }
    s_unread_drops = 0;
    s_notified = false;
    xSemaphoreGive(s_lock);
    return n;
}

void watch_imu_stream_set_period(uint16_t ms)
{
    if (!s_started) {
        return;
    }
    ms = ms < STREAM_MIN_MS ? STREAM_MIN_MS : (ms > STREAM_MAX_MS ? STREAM_MAX_MS : ms);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream_ms = ms;
    s_sst.period_ms = ms;
    xSemaphoreGive(s_lock);
}

void watch_imu_stream_close(void)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream_on = false;
    s_count = 0;
    s_notify = NULL;
    xSemaphoreGive(s_lock);
    if (s_task) {
        xTaskNotify(s_task, NOTE_ACTIVE, eSetBits);
    }
}

void watch_imu_stream_stats(watch_imu_stream_stats_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_sst;
    xSemaphoreGive(s_lock);
}
