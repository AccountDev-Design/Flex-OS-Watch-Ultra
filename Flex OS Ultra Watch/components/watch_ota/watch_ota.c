/*
 * watch_ota — ver include/watch_ota.h.
 *
 * Memoria: el motor (ota_t, ~10 KB con el manifiesto y el bloque de 4 KB)
 * vive en PSRAM; mbedTLS reserva en PSRAM (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC).
 * En RAM interna, la pila de la tarea. El firmware nunca está entero en RAM:
 * va de 4 KB en 4 KB del socket a la flash.
 */
#include "watch_ota.h"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ota_sig.h"
#include "ota_trusted_key.h"
#include "watch_net.h"
#include "watch_power.h"

static const char *TAG = "watch_ota";

#define NVS_NS        "flexota" /* el de la referencia */
#define NVS_PEND      "pend"
/* La referencia subió a 16 KB por HTTPClient de Arduino; con esp_http_client
 * y mbedTLS reservando en el montón basta menos. Se registra la marca de agua
 * tras cada operación para ajustarla con datos del reloj. */
#define TASK_STACK    10240
#define TASK_PRIO     2
#define TICK_MS       1000
#define HTTP_OPEN_MS  12000 /* FLEXOS_OTA_HTTP_TIMEOUT */
#define HTTP_READ_MS  1000  /* cada lectura: el motor cuenta los 15 s sin datos */
#define MAX_REDIRECTS 5
/* Guarda de memoria (RELOJ: la referencia miraba el montón total, que con
 * PSRAM no protege nada, A11). */
#define MIN_INTERNAL  (20 * 1024)
#define MIN_PSRAM     (128 * 1024)

enum { CMD_NONE = 0, CMD_CHECK, CMD_INSTALL };

static ota_t *s_o;
static SemaphoreHandle_t s_mx;
static TaskHandle_t s_task;
static uint8_t s_cmd;
static ota_auto_t s_auto;
static uint8_t s_boot;
static bool s_pending_verify;
static char s_fp[17];
static uint32_t s_extra_gen;

/* Transporte y flash de la operación en curso (solo la tarea los toca). */
static esp_http_client_handle_t s_h;
static esp_ota_handle_t s_oh;
static const esp_partition_t *s_part;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mx);
}

static bool net_online(void)
{
    watch_net_status_t ns;
    watch_net_get(&ns);
    return ns.state == NET_CONNECTED;
}

/* ======================= ota_io_t ======================= */

static uint32_t io_now(void *c)
{
    (void)c;
    return now_ms();
}

static void io_sleep(void *c, uint32_t ms)
{
    (void)c;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static bool io_online(void *c)
{
    (void)c;
    return net_online();
}

static bool io_mem_ok(void *c)
{
    (void)c;
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= MIN_INTERNAL &&
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= MIN_PSRAM;
}

static void io_close(void *c)
{
    (void)c;
    if (s_h) {
        esp_http_client_close(s_h);
        esp_http_client_cleanup(s_h);
        s_h = NULL;
    }
}

/* RELOJ: un fallo de DNS o TCP es "No se pudo conectar"; solo un certificado
 * que no verifica es "Certificado no válido" (la referencia los mezclaba, A10). */
static int map_open_error(esp_err_t e)
{
    int tls_code = 0, tls_flags = 0;
    const esp_err_t last = esp_http_client_get_and_clear_last_tls_error(s_h, &tls_code, &tls_flags);
    if (tls_flags != 0 || last == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) {
        return OTA_ERR_TLS;
    }
    if (last == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT || e == ESP_ERR_HTTP_EAGAIN) {
        return OTA_ERR_TIMEOUT;
    }
    return OTA_ERR_HTTPS;
}

static int io_open(void *c, const char *url, int64_t *len)
{
    (void)c;
    io_close(NULL);
    const esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_OPEN_MS,
        .crt_bundle_attach = esp_crt_bundle_attach, /* TLS verificado; nunca sin CA */
        .buffer_size = 2048,
        .buffer_size_tx = OTA_URL_MAX + 256,
        .keep_alive_enable = false,
        .disable_auto_redirect = true, /* las redirecciones se siguen aquí, solo a https:// */
    };
    s_h = esp_http_client_init(&cfg);
    if (!s_h) {
        return OTA_ERR_LOWMEM;
    }
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        const esp_err_t e = esp_http_client_open(s_h, 0);
        if (e != ESP_OK) {
            const int err = map_open_error(e);
            io_close(NULL);
            return err;
        }
        if (esp_http_client_fetch_headers(s_h) < 0) {
            io_close(NULL);
            return OTA_ERR_HTTPS;
        }
        const int st = esp_http_client_get_status_code(s_h);
        if (st == 200) {
            *len = esp_http_client_is_chunked_response(s_h) ? -1 : esp_http_client_get_content_length(s_h);
            esp_http_client_set_timeout_ms(s_h, HTTP_READ_MS);
            return 0;
        }
        if (st != 301 && st != 302 && st != 303 && st != 307 && st != 308) {
            ESP_LOGW(TAG, "HTTP %d", st);
            io_close(NULL);
            return OTA_ERR_HTTP_STATUS;
        }
        char next[OTA_URL_MAX];
        if (esp_http_client_set_redirection(s_h) != ESP_OK ||
            esp_http_client_get_url(s_h, next, sizeof(next)) != ESP_OK || strncmp(next, "https://", 8) != 0) {
            io_close(NULL);
            return OTA_ERR_BAD_URL;
        }
        esp_http_client_close(s_h);
    }
    io_close(NULL);
    return OTA_ERR_HTTP_STATUS;
}

static int io_read(void *c, uint8_t *buf, int n)
{
    (void)c;
    if (!s_h) {
        return -1;
    }
    const int r = esp_http_client_read(s_h, (char *)buf, n);
    if (r > 0) {
        return r;
    }
    if (r == -ESP_ERR_HTTP_EAGAIN) {
        return 0; /* nada en HTTP_READ_MS: el motor lleva la cuenta de los 15 s */
    }
    return -1; /* fin o conexión cerrada */
}

static uint32_t io_slot(void *c)
{
    (void)c;
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return p ? (uint32_t)p->size : 0;
}

static int io_begin(void *c, uint32_t size)
{
    (void)c;
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part || size > s_part->size) {
        return OTA_ERR_BEGIN;
    }
    /* Borrado por partes a medida que se escribe: no se borran 6 MiB de golpe. */
    const esp_err_t e = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_oh);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(e));
        return OTA_ERR_BEGIN;
    }
    ESP_LOGI(TAG, "escribiendo en %s (0x%lx, %lu B)", s_part->label, (unsigned long)s_part->address,
             (unsigned long)size);
    return 0;
}

static int io_write(void *c, const uint8_t *d, int n)
{
    (void)c;
    const esp_err_t e = esp_ota_write(s_oh, d, (size_t)n);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(e));
        return OTA_ERR_WRITE;
    }
    return 0;
}

static int io_finish(void *c)
{
    (void)c;
    esp_err_t e = esp_ota_end(s_oh); /* valida cabecera, chip y el SHA-256 añadido por IDF */
    s_oh = 0;
    if (e == ESP_OK) {
        e = esp_ota_set_boot_partition(s_part);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "imagen rechazada: %s", esp_err_to_name(e));
        return OTA_ERR_CORRUPT;
    }
    return 0;
}

static void io_abort(void *c)
{
    (void)c;
    if (s_oh) {
        esp_ota_abort(s_oh);
        s_oh = 0;
    }
}

static void io_battery(void *c, int *pct, bool *chg)
{
    (void)c;
    watch_power_snapshot_t p;
    *pct = -1;
    *chg = false;
    if (watch_power_get(&p)) {
        *pct = p.track.percent;
        *chg = p.track.state == BATT_STATE_CHARGING || p.track.state == BATT_STATE_FULL ||
               p.track.state == BATT_STATE_EXTERNAL_IDLE;
    }
}

static void io_pend(void *c, const char *ver)
{
    (void)c;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_PEND, ver);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void io_lock(void *c)
{
    (void)c;
    lock();
}

static void io_unlock(void *c)
{
    (void)c;
    unlock();
}

static const ota_io_t kIo = {
    .now = io_now,
    .sleep = io_sleep,
    .online = io_online,
    .mem_ok = io_mem_ok,
    .open = io_open,
    .read = io_read,
    .close = io_close,
    .slot_size = io_slot,
    .fl_begin = io_begin,
    .fl_write = io_write,
    .fl_finish = io_finish,
    .fl_abort = io_abort,
    .battery = io_battery,
    .save_pend = io_pend,
    .lock = io_lock,
    .unlock = io_unlock,
};

/* ======================= Arranque ======================= */

/* El "saludo" de la referencia (flexota/pend) y, RELOJ, si se revirtió. */
static void boot_notice(const char *running)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return; /* nunca se instaló nada */
    }
    char pend[OTA_VER_MAX] = "";
    size_t n = sizeof(pend);
    if (nvs_get_str(h, NVS_PEND, pend, &n) == ESP_OK && pend[0]) {
        s_boot = (uint8_t)ota_boot_notice(pend, running, esp_ota_get_last_invalid_partition() != NULL);
        nvs_erase_key(h, NVS_PEND);
        nvs_commit(h);
        ESP_LOGI(TAG, "tras instalar %s: %s", pend,
                 s_boot == OTA_BOOT_UPDATED ? "actualizado" : s_boot == OTA_BOOT_REVERTED ? "revertido" : "sin cambios");
    }
    nvs_close(h);
}

static void log_result(const char *what)
{
    lock();
    const int st = s_o->state, err = s_o->err;
    char ver[OTA_VER_MAX];
    memcpy(ver, s_o->info.ver, sizeof(ver));
    unlock();
    if (st == OTA_ERROR) {
        ESP_LOGW(TAG, "%s: fallo, codigo=%d (%s)", what, err, ota_err_text(err));
    } else if (st == OTA_AVAILABLE) {
        ESP_LOGI(TAG, "%s: disponible %s (local %s)", what, ver, s_o->local);
    } else {
        ESP_LOGI(TAG, "%s: %s", what, st == OTA_UP_TO_DATE ? "al dia" : ota_phase_text(st));
    }
    ESP_LOGI(TAG, "pila libre minima %u B, interna libre %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void task(void *arg)
{
    (void)arg;
    const uint32_t t0 = now_ms();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));
        const uint32_t now = now_ms();
        if (s_pending_verify && now - t0 >= WATCH_OTA_CONFIRM_MS) {
            const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "imagen confirmada tras %u s: %s", (unsigned)(WATCH_OTA_CONFIRM_MS / 1000),
                     esp_err_to_name(e));
            lock();
            s_pending_verify = false;
            s_extra_gen++;
            unlock();
        }
        const bool online = net_online();
        lock();
        if (s_cmd == CMD_NONE && ota_auto_due(&s_auto, now, online, s_o->state) && ota_req_check(s_o, true)) {
            s_cmd = CMD_CHECK;
        }
        const uint8_t cmd = s_cmd;
        s_cmd = CMD_NONE;
        unlock();
        if (cmd == CMD_CHECK) {
            ota_run_check(s_o, &kIo);
            log_result("comprobacion");
        } else if (cmd == CMD_INSTALL) {
            ota_run_install(s_o, &kIo);
            log_result("instalacion");
            if (s_o->state == OTA_REBOOTING) {
                ESP_LOGI(TAG, "instalada %s -> reiniciando", s_o->info.ver);
                esp_restart();
            }
        }
    }
}

esp_err_t watch_ota_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_mx = xSemaphoreCreateMutex();
    s_o = heap_caps_calloc(1, sizeof(ota_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mx || !s_o) {
        return ESP_ERR_NO_MEM;
    }
    const esp_app_desc_t *app = esp_app_get_description();
    ota_init(s_o, OTA_PRODUCT, app->version, WATCH_OTA_MANIFEST_URL, OTA_TRUSTED_KEY_SET ? kOtaTrustedKey : NULL);
    if (s_o->have_key) {
        uint8_t fp[32];
        ota_sha256(kOtaTrustedKey, sizeof(kOtaTrustedKey), fp);
        ota_hex_encode(fp, 8, s_fp);
    }
    ESP_LOGI(TAG, "version %s, clave de firma: %s%s", app->version, s_o->have_key ? "" : "ninguna",
             s_fp);

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending_verify = true;
        ESP_LOGW(TAG, "%s pendiente de confirmar: se confirma a los %u s", run->label,
                 (unsigned)(WATCH_OTA_CONFIRM_MS / 1000));
    }
    boot_notice(app->version);

    if (xTaskCreate(task, "watch_ota", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void watch_ota_get(watch_ota_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_o) {
        return;
    }
    lock();
    out->state = s_o->state;
    out->err = s_o->err;
    out->has_upd = s_o->has_upd;
    out->gen = s_o->gen + s_extra_gen;
    memcpy(out->local, s_o->local, sizeof(out->local));
    memcpy(out->ver, s_o->info.ver, sizeof(out->ver));
    out->size = s_o->info.size;
    memcpy(out->changelog, s_o->info.changelog, sizeof(out->changelog));
    out->total = s_o->total;
    out->done = s_o->done;
    out->bps = s_o->bps;
    out->pct = s_o->pct;
    out->have_key = s_o->have_key;
    memcpy(out->key_fp, s_fp, sizeof(out->key_fp));
    out->boot = s_boot;
    out->pending_verify = s_pending_verify;
    unlock();
}

void watch_ota_status_text(char *out, size_t n)
{
    if (!s_o) {
        snprintf(out, n, "No disponible");
        return;
    }
    lock();
    ota_status_text(s_o, out, n);
    unlock();
}

void watch_ota_check(void)
{
    if (!s_o) {
        return;
    }
    const bool online = net_online();
    lock();
    if (ota_req_check(s_o, online)) {
        s_cmd = CMD_CHECK;
    }
    unlock();
    xTaskNotifyGive(s_task);
}

void watch_ota_install(void)
{
    if (!s_o) {
        return;
    }
    const bool online = net_online();
    lock();
    if (ota_req_install(s_o, online)) {
        s_cmd = CMD_INSTALL;
    }
    unlock();
    xTaskNotifyGive(s_task);
}

void watch_ota_cancel(void)
{
    if (!s_o) {
        return;
    }
    lock();
    ota_req_cancel(s_o);
    unlock();
}

bool watch_ota_busy(void)
{
    if (!s_o) {
        return false;
    }
    lock();
    const bool b = ota_busy_state(s_o->state);
    unlock();
    return b;
}

void watch_ota_boot_seen(void)
{
    if (!s_o) {
        return;
    }
    lock();
    if (s_boot) {
        s_boot = OTA_BOOT_NONE;
        s_extra_gen++;
    }
    unlock();
}
