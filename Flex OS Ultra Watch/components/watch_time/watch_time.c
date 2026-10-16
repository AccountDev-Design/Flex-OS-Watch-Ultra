/*
 * watch_time — ver include/watch_time.h
 *
 * Acceso al PCF85063 (solo desde la tarea "watch_time"):
 *  - Lectura: UNA transacción "puntero 00h + leer 11 bytes".
 *  - Puesta en hora: Control_1 con STOP, una escritura 03h..0Ah (marca y
 *    hora), Control_1 sin STOP, relectura y comprobación de que avanza.
 *    Nunca se escribe Control_2 (01h) ni Offset (02h), y Control_1 conserva
 *    CIE y CAP_SEL (pcf85063_ctrl1_for_set).
 * El driver i2c_master serializa el bus que se comparte con el táctil y el
 * AXP2101.
 */
#include "watch_time.h"

#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "watch_time";

#define I2C_TIMEOUT_MS   50
#define PROBE_TIMEOUT_MS 50
#define I2C_TRIES        3
#define TASK_STACK       3584
#define TASK_PRIO        2
#define TASK_CORE        0
#define TICK_CHECK_MS    1100   /* tras quitar STOP: el reloj debe haber avanzado 1 s */
#define FIRST_TICK_US    492000 /* el primer segundo llega ~0,508 s después de quitar STOP */
#define SYS_RESYNC_S     2      /* el reloj del sistema solo se corrige si difiere tanto */
#define MIN_SET_UTC      1735689600LL /* 2025-01-01 00:00 UTC */
#define MAX_SET_UTC      4102444799LL /* 2099-12-31 23:59:59 UTC */
#define TZ_MIN           (-720)
#define TZ_MAX           840

#ifdef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define RTC_SCL_HZ CONFIG_BSP_I2C_CLK_SPEED_HZ
#else
#define RTC_SCL_HZ 400000
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static watch_time_info_t s_info;
static int64_t s_base_utc;   /* segundos UTC en s_info.synced_us */
static int16_t s_tz_min = -300;
static bool s_req_pending;   /* puesta en hora pendiente */
static int64_t s_req_utc;    /* hora UTC pedida... */
static int64_t s_req_us;     /* ...en este instante de esp_timer */
static bool s_sys_set;       /* el reloj del sistema ya se ajustó alguna vez */
static bool s_osc_bad;       /* el RTC no avanzó tras ponerlo en hora: manda la hora en RAM */
static bool s_started;

/* ---- Acceso al bus (con reintentos) ---- */

static esp_err_t rd_regs(uint8_t regs[PCF85063_READ_LEN])
{
    const uint8_t reg = PCF85063_REG_CTRL1;
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < I2C_TRIES && err != ESP_OK; i++) {
        err = i2c_master_transmit_receive(s_dev, &reg, 1, regs, PCF85063_READ_LEN, I2C_TIMEOUT_MS);
    }
    return err;
}

static esp_err_t wr(const uint8_t *buf, size_t n)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < I2C_TRIES && err != ESP_OK; i++) {
        err = i2c_master_transmit(s_dev, buf, n, I2C_TIMEOUT_MS);
    }
    return err;
}

/* Reloj del sistema en UTC (time(), gettimeofday; TLS en F5). */
static void set_system_clock(int64_t utc_s, int32_t usec)
{
    struct timeval tv = {.tv_sec = (time_t)utc_s, .tv_usec = usec};
    settimeofday(&tv, NULL);
    s_sys_set = true;
}

static void log_regs(const char *what, const uint8_t *regs)
{
    ESP_LOGI(TAG, "%s 00h-0Ah: %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x", what, regs[0], regs[1],
             regs[2], regs[3], regs[4], regs[5], regs[6], regs[7], regs[8], regs[9], regs[10]);
}

/* ---- Lectura ---- */

/* Devuelve true si la lectura se completó (sea cual sea la validez de la hora). */
static bool read_rtc(void)
{
    uint8_t regs[PCF85063_READ_LEN];
    const esp_err_t err = rd_regs(regs);
    const int64_t now_us = esp_timer_get_time();

    wt_datetime_t dt = {0};
    const rtc_read_t res = (err == ESP_OK) ? pcf85063_decode(regs, &dt) : RTC_READ_BAD_FIELD;
    const bool marker = (err == ESP_OK) && regs[PCF85063_REG_RAM] == PCF85063_MARKER;
    const int64_t rtc_utc = wt_to_seconds(&dt);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const watch_time_state_t prev = s_info.state;
    bool sys_fix = false;
    if (err != ESP_OK) {
        s_info.errors++;
        /* Si ya había hora válida se sigue contando con el temporizador. */
        if (s_info.state != WATCH_TIME_OK) {
            s_info.state = WATCH_TIME_NO_RTC;
        }
    } else {
        s_info.reads++;
        s_info.last_read = res;
        s_info.marker_ok = marker;
        memcpy(s_info.regs, regs, sizeof(regs));
        if (s_osc_bad && s_info.state == WATCH_TIME_OK) {
            /* El RTC no avanza: su hora se quedó atrás; se sigue con la de RAM. */
        } else if (res == RTC_READ_OK && marker) {
            s_base_utc = rtc_utc;
            s_info.synced_us = now_us;
            s_info.state = WATCH_TIME_OK;
            sys_fix = true;
        } else if (!s_req_pending) {
            /* Con una puesta en hora pendiente se sigue con la hora pedida. */
            s_info.state = WATCH_TIME_NOT_SET;
        }
    }
    const watch_time_state_t state = s_info.state;
    const uint32_t errors = s_info.errors;
    xSemaphoreGive(s_lock);

    if (sys_fix) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const int64_t d = (int64_t)tv.tv_sec - rtc_utc;
        if (!s_sys_set || d >= SYS_RESYNC_S || d <= -SYS_RESYNC_S) {
            set_system_clock(rtc_utc, 500000); /* fase del segundo desconocida: la mitad */
        }
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lectura del RTC fallida: %s (errores %lu)", esp_err_to_name(err), (unsigned long)errors);
        return false;
    }
    if (res == RTC_READ_OK || res == RTC_READ_IMPLAUSIBLE) {
        ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d UTC (%s%s)", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                 dt.second, pcf85063_read_text(res), marker ? "" : ", sin la marca de Flex OS: sin configurar");
    } else {
        ESP_LOGW(TAG, "RTC sin hora fiable: %s", pcf85063_read_text(res));
    }
    if (state != prev || res != RTC_READ_OK || !marker) {
        log_regs("registros", regs);
    }
    return true;
}

/* ---- Puesta en hora ---- */

static wt_set_result_t write_rtc(int64_t req_utc, int64_t req_us, int64_t *set_utc, int64_t *release_us)
{
    uint8_t regs[PCF85063_READ_LEN];
    if (rd_regs(regs) != ESP_OK) {
        return WT_SET_ERR_I2C;
    }
    log_regs("antes de poner en hora", regs);
    const uint8_t c_run = pcf85063_ctrl1_for_set(regs[0], false);
    const uint8_t c_stop = pcf85063_ctrl1_for_set(regs[0], true);

    /* 1. Detener el reloj: los contadores ya no avanzan mientras se escriben. */
    const uint8_t stop[2] = {PCF85063_REG_CTRL1, c_stop};
    if (wr(stop, sizeof(stop)) != ESP_OK) {
        return WT_SET_ERR_I2C;
    }
    /* 2. Marca y hora en UNA escritura (03h..0Ah). La hora es la pedida más
     * lo que ha pasado desde la petición. */
    const int64_t utc = req_utc + (esp_timer_get_time() - req_us) / 1000000;
    uint8_t buf[2 + PCF85063_TIME_LEN];
    buf[0] = PCF85063_REG_RAM;
    buf[1] = PCF85063_MARKER;
    if (pcf85063_encode_utc(utc, false, &buf[2]) != RTC_ENC_OK) {
        /* No debería pasar (se valida al pedir): se deja como estaba. */
        const uint8_t run[2] = {PCF85063_REG_CTRL1, (uint8_t)(regs[0] & ~PCF85063_CTRL1_SR)};
        wr(run, sizeof(run));
        return WT_SET_ERR_VERIFY;
    }
    if (wr(buf, sizeof(buf)) != ESP_OK) {
        return WT_SET_ERR_I2C; /* queda detenido: nunca una hora a medio escribir */
    }
    /* 3. Arrancar. */
    const uint8_t run[2] = {PCF85063_REG_CTRL1, c_run};
    if (wr(run, sizeof(run)) != ESP_OK) {
        return WT_SET_ERR_I2C;
    }
    const int64_t t_rel = esp_timer_get_time();
    *set_utc = utc;
    *release_us = t_rel;

    /* 4. Releer: lo escrito, sin OS, en marcha y con la marca. */
    uint8_t back[PCF85063_READ_LEN];
    if (rd_regs(back) != ESP_OK) {
        return WT_SET_ERR_I2C;
    }
    wt_datetime_t dt;
    const rtc_read_t r1 = pcf85063_decode(back, &dt);
    const int64_t d1 = wt_to_seconds(&dt) - utc;
    if (r1 != RTC_READ_OK || back[PCF85063_REG_RAM] != PCF85063_MARKER || back[0] != c_run || d1 < 0 || d1 > 1) {
        log_regs("relectura NO coincide", back);
        return WT_SET_ERR_VERIFY;
    }
    /* 5. ¿Avanza? (si el cristal no oscila, la hora se quedaría quieta) */
    vTaskDelay(pdMS_TO_TICKS(TICK_CHECK_MS));
    if (rd_regs(back) != ESP_OK) {
        return WT_SET_ERR_I2C;
    }
    const rtc_read_t r2 = pcf85063_decode(back, &dt);
    const int64_t d2 = wt_to_seconds(&dt) - utc;
    log_regs("tras poner en hora", back);
    if (r2 != RTC_READ_OK || d2 < 1 || d2 > 3) {
        return WT_SET_ERR_OSC;
    }
    return WT_SET_OK;
}

static void do_set(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t req_utc = s_req_utc, req_us = s_req_us;
    xSemaphoreGive(s_lock);

    int64_t set_utc = 0, rel_us = 0;
    const wt_set_result_t res = write_rtc(req_utc, req_us, &set_utc, &rel_us);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool newer = s_req_us != req_us; /* llegó otra petición mientras tanto */
    s_info.set_result = newer ? WT_SET_PENDING : res;
    if (res == WT_SET_OK) {
        s_osc_bad = false;
        s_info.writes++;
        s_info.marker_ok = true;
        s_info.last_read = RTC_READ_OK;
        if (!newer) {
            s_req_pending = false;
            /* Fase conocida: el RTC pasa al segundo siguiente ~0,508 s después de arrancar. */
            s_base_utc = set_utc;
            s_info.synced_us = rel_us - FIRST_TICK_US;
            s_info.state = WATCH_TIME_OK;
        }
    } else {
        s_info.write_errors++;
        /* La hora pedida sigue valiendo en RAM; se reintenta más tarde. */
        if (res == WT_SET_ERR_OSC && !newer) {
            s_req_pending = false; /* reescribir no arregla un cristal parado */
            s_osc_bad = true;
        }
    }
    const int64_t base = s_base_utc, synced = s_info.synced_us;
    xSemaphoreGive(s_lock);

    if (res == WT_SET_OK && !newer) {
        const int64_t el_us = esp_timer_get_time() - synced;
        set_system_clock(base + el_us / 1000000, (int32_t)(el_us % 1000000));
        ESP_LOGI(TAG, "RTC puesto en hora y verificado (UTC %lld)", (long long)set_utc);
    } else if (res != WT_SET_OK) {
        ESP_LOGE(TAG, "no se pudo poner en hora el RTC (%d)%s", (int)res,
                 res == WT_SET_ERR_OSC ? ": el reloj no avanza" : "; se reintenta");
    }
}

static void time_task(void *arg)
{
    (void)arg;
    int64_t next_read_us = 0;
    for (;;) {
        bool ok = true;
        if (!s_info.rtc_found) {
            /* El RTC no respondió al arrancar: se vuelve a buscar. */
            if (i2c_master_probe(s_bus, PCF85063_ADDR, PROBE_TIMEOUT_MS) == ESP_OK) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.rtc_found = true;
                xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "PCF85063 encontrado en 0x%02x", PCF85063_ADDR);
                next_read_us = 0;
            }
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool pending = s_req_pending;
        xSemaphoreGive(s_lock);
        if (s_info.rtc_found && pending) {
            do_set();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            ok = !s_req_pending;
            xSemaphoreGive(s_lock);
            next_read_us = esp_timer_get_time() + (int64_t)WATCH_TIME_RESYNC_MS * 1000;
        } else if (s_info.rtc_found && esp_timer_get_time() >= next_read_us) {
            ok = read_rtc();
            next_read_us = esp_timer_get_time() + (int64_t)(ok ? WATCH_TIME_RESYNC_MS : WATCH_TIME_RETRY_MS) * 1000;
        } else if (!s_info.rtc_found) {
            ok = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (!s_req_pending) {
                s_info.state = WATCH_TIME_NO_RTC;
            }
            xSemaphoreGive(s_lock);
        }
        /* Espera hasta la próxima lectura, un reintento o una petición. */
        int64_t wait_ms = ok ? (next_read_us - esp_timer_get_time()) / 1000 : WATCH_TIME_RETRY_MS;
        if (wait_ms < 1) {
            wait_ms = 1;
        }
        xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(wait_ms));
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
    s_info.tz_min = s_tz_min;
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
    if (xTaskCreatePinnedToCore(time_task, "watch_time", TASK_STACK, NULL, TASK_PRIO, &s_task, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

bool watch_time_now_utc(int64_t *utc_s)
{
    if (!s_started) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_info.state == WATCH_TIME_OK;
    const int64_t base = s_base_utc;
    const int64_t synced = s_info.synced_us;
    xSemaphoreGive(s_lock);
    if (ok) {
        *utc_s = base + (esp_timer_get_time() - synced) / 1000000;
    }
    return ok;
}

watch_time_state_t watch_time_now(wt_datetime_t *out)
{
    if (!s_started) {
        return WATCH_TIME_STARTING;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const watch_time_state_t st = s_info.state;
    const int64_t base = s_base_utc;
    const int64_t synced = s_info.synced_us;
    const int tz = s_tz_min;
    xSemaphoreGive(s_lock);
    if (st == WATCH_TIME_OK) {
        wt_utc_to_local(base + (esp_timer_get_time() - synced) / 1000000, tz, out);
    }
    return st;
}

esp_err_t watch_time_set_utc(int64_t utc_s)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (utc_s < MIN_SET_UTC || utc_s > MAX_SET_UTC) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_req_utc = utc_s;
    s_req_us = now;
    s_req_pending = true;
    s_info.set_result = WT_SET_PENDING;
    /* La hora nueva vale ya en RAM (la interfaz no espera al bus). */
    s_base_utc = utc_s;
    s_info.synced_us = now;
    s_info.state = WATCH_TIME_OK;
    xSemaphoreGive(s_lock);
    xTaskNotify(s_task, 1, eSetBits);
    return ESP_OK;
}

void watch_time_set_tz(int off_min)
{
    if (off_min < TZ_MIN) {
        off_min = TZ_MIN;
    } else if (off_min > TZ_MAX) {
        off_min = TZ_MAX;
    }
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_tz_min = (int16_t)off_min;
    s_info.tz_min = s_tz_min;
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

int watch_time_get_tz(void)
{
    return s_tz_min;
}

void watch_time_info(watch_time_info_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        out->tz_min = s_tz_min;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}
