/*
 * watch_weather — ver include/watch_weather.h.
 *
 * Memoria (medida con sizeof en tests/host): el motor (wx_core_t) y el
 * pronóstico en preparación (wx_data_t) viven en PSRAM; la respuesta HTTP
 * (16 KB, 6 KB la búsqueda) se pide en PSRAM al empezar cada descarga y se
 * suelta al acabar. mbedTLS reserva en PSRAM (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC).
 * En RAM interna solo queda la pila de la tarea.
 */
#include "watch_weather.h"

#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "watch_net.h"
#include "watch_time.h"
#include "wx_core.h"

static const char *TAG = "watch_weather";

#define NVS_NS       "flexwx"
/* Pila: la de los ejemplos HTTPS de ESP-IDF (esp_http_client_example), con
 * mbedTLS reservando en el montón, no en la pila. */
#define TASK_STACK   8192
#define TASK_PRIO    2
#define TICK_MS      1000
#define HTTP_TIMEOUT_MS 12000 /* FLEXWX_HTTP_TIMEOUT_MS: conexión y cada lectura */
#define HTTP_TOTAL_MS   25000 /* tope de una descarga entera */
/* Por debajo no se intenta TLS (FLEXWX_MIN_HEAP de la referencia, repartido
 * entre las dos memorias del S3). */
#define MIN_INTERNAL (20 * 1024)
#define MIN_PSRAM    (128 * 1024)

static wx_core_t *s_c;
static wx_data_t *s_parse; /* el pronóstico que se está leyendo: nunca el publicado */
static SemaphoreHandle_t s_mx;
static TaskHandle_t s_task;

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

static void wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

/* ======================= NVS ======================= */

static void save_locs(const wx_loc_t *locs, uint8_t n, int8_t sel)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "locs", locs, sizeof(wx_loc_t) * WX_LOCS);
    nvs_set_u8(h, "nloc", n);
    nvs_set_i8(h, "sel", sel);
    nvs_commit(h);
    nvs_close(h);
}

static void save_cache(const wx_data_t *w)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "cache", w, sizeof(*w));
    nvs_commit(h);
    nvs_close(h);
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return; /* primera vez: no hay nada guardado */
    }
    wx_loc_t locs[WX_LOCS];
    size_t n = sizeof(locs);
    uint8_t cnt = 0;
    int8_t sel = -1;
    if (nvs_get_blob(h, "locs", locs, &n) == ESP_OK && n == sizeof(locs)) {
        nvs_get_u8(h, "nloc", &cnt);
        nvs_get_i8(h, "sel", &sel);
        wx_core_set_locs(s_c, locs, cnt, sel);
    }
    n = sizeof(*s_parse);
    if (nvs_get_blob(h, "cache", s_parse, &n) == ESP_OK) {
        wx_core_load_cache(s_c, s_parse, n);
    }
    nvs_close(h);
}

/* ======================= Red ======================= */

static int map_open_error(esp_http_client_handle_t h, esp_err_t e)
{
    int tls_code = 0, tls_flags = 0;
    const esp_err_t last = esp_http_client_get_and_clear_last_tls_error(h, &tls_code, &tls_flags);
    if (tls_flags != 0) {
        return WXE_TLS; /* el certificado no se pudo verificar */
    }
    switch (last) {
    case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT: return WXE_TIMEOUT;
    case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED: return WXE_TLS;
    default: break;
    }
    return e == ESP_ERR_HTTP_EAGAIN ? WXE_TIMEOUT : WXE_CONNECT;
}

/* Descarga url entera a buf (siempre termina en 0). Devuelve WXE_*. */
static int http_get(const char *url, char *buf, size_t cap, size_t *len)
{
    *len = 0;
    watch_net_status_t ns;
    watch_net_get(&ns);
    if (ns.state != NET_CONNECTED) {
        return WXE_NOWIFI;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL ||
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < MIN_PSRAM) {
        return WXE_MEM;
    }
    const esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach, /* TLS verificado; nunca sin CA */
        .buffer_size = 1024,
        .buffer_size_tx = WX_URL_MAX + 256, /* la URL del pronóstico va en la primera línea */
        .keep_alive_enable = false,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        return WXE_MEM;
    }
    int err = WXE_NONE;
    const esp_err_t oe = esp_http_client_open(h, 0);
    if (oe != ESP_OK) {
        err = map_open_error(h, oe);
    } else if (esp_http_client_fetch_headers(h) < 0) {
        err = WXE_CONNECT;
    } else if (esp_http_client_get_status_code(h) != 200) {
        err = WXE_HTTP;
    } else {
        const uint32_t t0 = now_ms();
        size_t n = 0;
        for (;;) {
            if (n >= cap - 1) {
                break; /* no cabe: el parser lo rechazará (sin '}') */
            }
            const int r = esp_http_client_read(h, buf + n, (int)(cap - 1 - n));
            if (r > 0) {
                n += (size_t)r;
            } else if (r == 0) {
                if (esp_http_client_is_complete_data_received(h)) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5)); /* cede la CPU: la interfaz sigue viva */
            } else if (r == -ESP_ERR_HTTP_EAGAIN) {
                err = WXE_TIMEOUT;
                break;
            } else {
                err = WXE_CONNECT;
                break;
            }
            if (now_ms() - t0 > HTTP_TOTAL_MS) {
                err = WXE_TIMEOUT;
                break;
            }
        }
        buf[n] = 0;
        *len = n;
        if (err == WXE_NONE && n == 0) {
            err = WXE_JSON;
        }
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return err;
}

static void run_fetch(const wx_job_t *job)
{
    char url[WX_URL_MAX];
    int err = WXE_NOLOC;
    char *buf = NULL;
    if (job->loc.name[0] && wx_url_forecast(&job->loc, url, sizeof(url))) {
        buf = heap_caps_malloc(WX_HTTP_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        size_t n = 0;
        err = buf ? http_get(url, buf, WX_HTTP_BUF, &n) : WXE_MEM;
        if (err == WXE_NONE) {
            err = wx_parse_forecast(buf, n, &job->loc, s_parse);
        }
    }
    heap_caps_free(buf);
    if (err != WXE_NONE) {
        ESP_LOGW(TAG, "pronóstico: %s", wx_error_text((uint8_t)err));
    }
    lock();
    wx_core_fetch_done(s_c, now_ms(), job, err, err == WXE_NONE ? s_parse : NULL);
    unlock();
}

static void run_search(const wx_job_t *job)
{
    char url[WX_URL_MAX];
    wx_loc_t res[WX_RESULTS];
    int n = 0, err = WXE_NORESULT;
    char *buf = NULL;
    if (wx_url_geo(job->query, url, sizeof(url))) {
        buf = heap_caps_malloc(WX_GEO_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        size_t len = 0;
        err = buf ? http_get(url, buf, WX_GEO_BUF, &len) : WXE_MEM;
        if (err == WXE_NONE) {
            n = wx_parse_geo(buf, len, res, WX_RESULTS);
        }
    }
    heap_caps_free(buf);
    lock();
    wx_core_search_done(s_c, err, res, n);
    unlock();
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));
        int64_t utc = 0;
        const bool clock_ok = watch_time_now_utc(&utc);
        watch_net_status_t ns;
        watch_net_get(&ns);
        wx_job_t job;
        lock();
        if (clock_ok) {
            wx_core_set_clock(s_c, utc, now_ms());
        }
        wx_core_tick(s_c, now_ms(), ns.state == NET_CONNECTED);
        const bool have_job = wx_core_take_job(s_c, &job);
        unlock();
        if (have_job) {
            if (job.kind == WXJ_SEARCH) {
                run_search(&job);
            } else {
                run_fetch(&job);
            }
            xTaskNotifyGive(s_task); /* puede haber otro pedido detrás */
        }
        /* NVS fuera del cerrojo (escribir en flash tarda) */
        wx_loc_t locs[WX_LOCS];
        uint8_t nloc = 0;
        int8_t sel = -1;
        bool save_l = false, save_c = false;
        lock();
        if (s_c->locs_dirty) {
            s_c->locs_dirty = false;
            save_l = true;
            memcpy(locs, s_c->locs, sizeof(locs));
            nloc = s_c->nloc;
            sel = s_c->sel;
        }
        if (s_c->cache_dirty) {
            s_c->cache_dirty = false;
            save_c = true;
            *s_parse = s_c->data; /* s_parse está libre entre descargas */
        }
        unlock();
        if (save_l) {
            save_locs(locs, nloc, sel);
        }
        if (save_c) {
            save_cache(s_parse);
        }
    }
}

/* ======================= API ======================= */

esp_err_t watch_weather_start(void)
{
    if (s_c) {
        return ESP_OK;
    }
    s_c = heap_caps_calloc(1, sizeof(*s_c), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_parse = heap_caps_calloc(1, sizeof(*s_parse), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mx = xSemaphoreCreateMutex();
    if (!s_c || !s_parse || !s_mx) {
        ESP_LOGE(TAG, "sin memoria");
        return ESP_ERR_NO_MEM;
    }
    wx_core_init(s_c, now_ms());
    load(); /* solo flash: la radio no se toca aquí */
    if (xTaskCreate(task, "watch_wx", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        s_c->err = WXE_MEM; /* sin tarea no habrá descargas: que se vea */
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "listo: %u ubicaciones, motor %u B + %u B en PSRAM", s_c->nloc, (unsigned)sizeof(*s_c),
             (unsigned)sizeof(*s_parse));
    return ESP_OK;
}

void watch_weather_get(watch_weather_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->sel = -1;
    if (!s_c) {
        out->err = WXE_MEM;
        out->status = WXS_ERROR;
        return;
    }
    lock();
    const uint32_t t = now_ms();
    out->status = wx_core_status(s_c);
    out->err = s_c->err;
    out->have = s_c->have;
    out->gen = s_c->gen;
    out->age_s = wx_core_age_s(s_c, t);
    out->now_local = wx_core_now_local(s_c, t);
    out->now_utc = wx_core_now_utc(s_c, t);
    out->nloc = s_c->nloc;
    out->sel = s_c->sel;
    memcpy(out->locs, s_c->locs, sizeof(out->locs));
    out->qst = s_c->qst;
    out->qgen = s_c->qgen;
    out->nres = s_c->nres;
    memcpy(out->res, s_c->res, sizeof(out->res));
    unlock();
}

bool watch_weather_data(wx_data_t *out)
{
    if (!s_c) {
        return false;
    }
    lock();
    const bool have = s_c->have;
    if (have) {
        *out = s_c->data;
    }
    unlock();
    return have;
}

void watch_weather_refresh(bool force)
{
    if (!s_c) {
        return;
    }
    lock();
    wx_core_refresh(s_c, now_ms(), force);
    unlock();
    wake();
}

void watch_weather_select(uint8_t i)
{
    if (!s_c) {
        return;
    }
    lock();
    wx_core_loc_select(s_c, i, now_ms());
    unlock();
    wake();
}

bool watch_weather_add(const wx_loc_t *l)
{
    if (!s_c) {
        return false;
    }
    lock();
    const bool ok = wx_core_loc_add(s_c, l, now_ms());
    unlock();
    wake();
    return ok;
}

void watch_weather_remove(uint8_t i)
{
    if (!s_c) {
        return;
    }
    lock();
    wx_core_loc_remove(s_c, i, now_ms());
    unlock();
    wake();
}

void watch_weather_search(const char *q)
{
    if (!s_c) {
        return;
    }
    lock();
    wx_core_search(s_c, q);
    unlock();
    wake();
}

void watch_weather_search_clear(void)
{
    if (!s_c) {
        return;
    }
    lock();
    wx_core_search_clear(s_c);
    unlock();
}
