/*
 * watch_gallery — ver include/watch_gallery.h.
 */
#include "watch_gallery.h"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "gal_wm.h"
#include "watch_net.h"
#include "watch_storage.h"
#include "mem_logic.h"
#include "watch_task.h"

static const char *TAG = "watch_gallery";

#define TASK_STACK    (12 * 1024) /* TLS (mbedTLS) en la pila de la tarea, en PSRAM; se mide al cerrar */
#define TASK_PRIO     3
#define CONNECT_MS    10000
#define MIN_INTERNAL  (30 * 1024) /* para un saludo TLS (MBEDTLS_EXTERNAL_MEM_ALLOC deja lo grande en PSRAM) */
#define PSRAM_MARGIN  (384 * 1024) /* lo que la Galería nunca se come: el resto del sistema */
#define NS_ACCT       "flexacct"
#define NS_GAL        "flexgal"

typedef struct {
    esp_transport_handle_t t;
} io_ctx_t;

static io_ctx_t s_lan_ctx, s_cloud_ctx;
static gal_io_t s_lan_io, s_cloud_io;
static gal_store_t s_store;
static gal_env_t s_env;
static gal_engine_t *s_e;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_mx;   /* el del motor (recursivo) */
static SemaphoreHandle_t s_life; /* abrir, cerrar y el final de la tarea, de uno en uno */
static volatile bool s_open;
static char s_mac[16];
static char s_ver[32]; /* esp_app_desc_t.version */
static char s_why[112]; /* por qué no abrió (watch_gallery_open_why) */

/* ======================= transporte ======================= */

static int io_connect(void *ctx, const char *host, uint16_t port, bool tls, uint32_t to_ms)
{
    io_ctx_t *c = ctx;
    if (c->t) {
        esp_transport_close(c->t);
        esp_transport_destroy(c->t);
        c->t = NULL;
    }
    if (tls && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL) {
        ESP_LOGW(TAG, "TLS aplazado: RAM interna %u B", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return GAL_IO_E_CONNECT;
    }
    c->t = tls ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (!c->t) {
        return GAL_IO_E_CONNECT;
    }
    if (tls) { /* certificado verificado SIEMPRE (nunca setInsecure) */
        esp_transport_ssl_crt_bundle_attach(c->t, esp_crt_bundle_attach);
    }
    if (esp_transport_connect(c->t, host, port, (int)(to_ms ? to_ms : CONNECT_MS)) == 0) {
        return GAL_IO_OK;
    }
    int ret = GAL_IO_E_CONNECT;
    const esp_tls_error_handle_t e = esp_transport_get_error_handle(c->t);
    if (e) {
        if (e->esp_tls_flags != 0 || e->last_error == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) {
            ret = GAL_IO_E_TLS;
        } else if (e->last_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
            ret = GAL_IO_E_DNS;
        } else if (e->last_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
            ret = GAL_IO_E_TIMEOUT;
        }
        ESP_LOGW(TAG, "conexión a %s:%u: %s (flags 0x%x)", host, port, esp_err_to_name(e->last_error), e->esp_tls_flags);
    }
    esp_transport_destroy(c->t);
    c->t = NULL;
    return ret;
}

static int io_write(void *ctx, const uint8_t *b, size_t n, uint32_t to_ms)
{
    io_ctx_t *c = ctx;
    if (!c->t) {
        return -1;
    }
    const int r = esp_transport_write(c->t, (const char *)b, (int)n, (int)to_ms);
    return r < 0 ? -1 : r;
}

static int io_read(void *ctx, uint8_t *b, size_t n, uint32_t to_ms)
{
    io_ctx_t *c = ctx;
    if (!c->t) {
        return -1;
    }
    const int r = esp_transport_read(c->t, (char *)b, (int)n, (int)to_ms);
    return r > 0 ? r : r == ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT ? 0 : -1;
}

static void io_close(void *ctx)
{
    io_ctx_t *c = ctx;
    if (c->t) {
        esp_transport_close(c->t);
        esp_transport_destroy(c->t);
        c->t = NULL;
    }
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool io_cancelled(void *ctx)
{
    (void)ctx;
    return s_e && gal_engine_should_abort(s_e);
}

/* ======================= NVS ======================= */

/* "g.pick" -> flexgal/pick; el resto, flexacct (las claves de FlexOS_Account) */
static const char *ns_of(const char *key, const char **k)
{
    if (!strncmp(key, "g.", 2)) {
        *k = key + 2;
        return NS_GAL;
    }
    *k = key;
    return NS_ACCT;
}

static bool st_get(void *ctx, const char *key, void *buf, size_t *len)
{
    (void)ctx;
    const char *k;
    const char *ns = ns_of(key, &k);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t l = *len;
    const bool ok = nvs_get_blob(h, k, buf, &l) == ESP_OK;
    nvs_close(h);
    if (ok) {
        *len = l;
    }
    return ok;
}

static bool st_set(void *ctx, const char *key, const void *buf, size_t len)
{
    (void)ctx;
    const char *k;
    const char *ns = ns_of(key, &k);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, k, buf, len) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool st_erase(void *ctx, const char *key)
{
    (void)ctx;
    const char *k;
    const char *ns = ns_of(key, &k);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const esp_err_t r = nvs_erase_key(h, k);
    nvs_commit(h);
    nvs_close(h);
    return r == ESP_OK || r == ESP_ERR_NVS_NOT_FOUND;
}

static void st_random(void *ctx, uint8_t *out, size_t n)
{
    (void)ctx;
    esp_fill_random(out, n);
}

/* ======================= plataforma ======================= */

static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void ps_free(void *p)
{
    heap_caps_free(p);
}

static void mx_lock(void *ctx)
{
    (void)ctx;
    xSemaphoreTakeRecursive(s_mx, portMAX_DELAY);
}

static void mx_unlock(void *ctx)
{
    (void)ctx;
    xSemaphoreGiveRecursive(s_mx);
}

static void do_kick(void *ctx)
{
    (void)ctx;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

static uint32_t env_now(void *ctx)
{
    return io_now(ctx);
}

static uint64_t env_fs_free(void *ctx)
{
    (void)ctx;
    watch_storage_info_t si;
    watch_storage_info(&si);
    return si.state == STG_MOUNTED && si.total > si.used ? (uint64_t)(si.total - si.used) : 0;
}

static bool env_mem_ok(void *ctx, size_t n)
{
    (void)ctx;
    /* Un bloque donde quepa y PSRAM_MARGIN para el resto: las reservas de la
     * Galería son pequeñas (25 KB por miniatura) salvo el JSON y la foto abierta. */
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= n &&
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= n + PSRAM_MARGIN;
}

static void log_mem(const char *what)
{
    ESP_LOGI(TAG, "%s: PSRAM libre %u KB (bloque %u KB), interna %u KB (bloque %u KB)", what,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

/* ======================= la tarea ======================= */

static void task(void *arg)
{
    (void)arg;
    uint32_t wifi_ms = 0, idle_since = 0;
    for (;;) {
        const uint32_t now = io_now(NULL);
        if (!wifi_ms || now - wifi_ms >= 500) {
            wifi_ms = now | 1u;
            watch_net_status_t ns;
            watch_net_get(&ns);
            gal_set_wifi(s_e, ns.state == NET_CONNECTED);
        }
        const uint32_t wait = gal_engine_step(s_e);
        if (!s_open && !gal_engine_busy(s_e)) {
            if (!idle_since) {
                idle_since = now | 1u;
            }
            if (now - idle_since > 2000) { /* cerrada y sin transferencias: fin, y se suelta todo */
                xSemaphoreTake(s_life, portMAX_DELAY);
                if (!s_open) {
                    gal_mem_t m;
                    gal_engine_mem(s_e, &m);
                    ESP_LOGI(TAG, "fin: %u peticiones en %u conexiones, %u miniaturas, pila libre mínima %u B",
                             (unsigned)m.http_reqs, (unsigned)m.http_opens, (unsigned)m.thumbs_done,
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                    io_close(&s_lan_ctx);
                    io_close(&s_cloud_ctx);
                    gal_engine_free(s_e);
                    s_e = NULL;
                    s_task = NULL;
                    xSemaphoreGive(s_life);
                    log_mem("Galería cerrada");
                    watch_task_exit();
                }
                xSemaphoreGive(s_life);
            }
        } else {
            idle_since = 0;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait < 10 ? 10 : wait > 500 ? 500 : wait));
    }
}

gal_engine_t *watch_gallery_open(void)
{
    if (!s_life) {
        s_life = xSemaphoreCreateMutex();
        s_mx = xSemaphoreCreateRecursiveMutex();
        if (!s_life || !s_mx) {
            return NULL;
        }
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_mac, sizeof(s_mac), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        gal_copyz(s_ver, sizeof(s_ver), esp_app_get_description()->version);
        s_lan_io = (gal_io_t){.ctx = &s_lan_ctx, .connect = io_connect, .write = io_write, .read = io_read,
                              .close = io_close, .now = io_now, .cancelled = io_cancelled};
        s_cloud_io = s_lan_io;
        s_cloud_io.ctx = &s_cloud_ctx;
        s_store = (gal_store_t){.get = st_get, .set = st_set, .erase = st_erase, .random = st_random};
        s_env = (gal_env_t){.io_lan = &s_lan_io, .io_cloud = &s_cloud_io, .store = &s_store, .alloc = ps_alloc,
                            .free = ps_free, .lock = mx_lock, .unlock = mx_unlock, .kick = do_kick, .now = env_now,
                            .fs_free = env_fs_free, .mem_ok = env_mem_ok, .mac_hex = s_mac, .version = s_ver,
                            .local = gal_wm_io()}; /* reloj y microSD: la biblioteca de watch_media */
    }
    xSemaphoreTake(s_life, portMAX_DELAY);
    log_mem("Galería: antes de abrir");
    if (!s_e) {
        s_e = gal_engine_new(&s_env);
    }
    s_why[0] = 0;
    bool ok = s_e != NULL;
    if (!ok) {
        mem_why_text(s_why, sizeof(s_why), 0, true, /* el motor (~180 KB) no cupo en PSRAM */
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    if (ok && !s_task) {
        /* La pila (12 KB) en PSRAM: la RAM interna es la que el Wi-Fi y el BLE
         * necesitan, y era la que faltaba con la PSRAM casi vacía. */
        if (!watch_task_spawn(task, "watch_gal", TASK_STACK, NULL, TASK_PRIO, tskNO_AFFINITY, true, &s_task, s_why,
                              sizeof(s_why))) {
            s_task = NULL;
            ok = false;
        }
    }
    if (ok) {
        s_open = true;
        ok = gal_engine_open(s_e);
        if (!ok) {
            s_open = false; /* la tarea termina sola y suelta el motor */
        }
    }
    gal_engine_t *e = ok ? s_e : NULL;
    xSemaphoreGive(s_life);
    if (e) {
        gal_mem_t m;
        gal_engine_mem(e, &m);
        ESP_LOGI(TAG, "abierta: motor %u B + JSON %u B + miniaturas %u B en PSRAM", (unsigned)m.engine_bytes,
                 (unsigned)m.json_bytes, (unsigned)m.thumb_bytes);
        log_mem("Galería abierta");
    } else {
        ESP_LOGW(TAG, "no se pudo abrir: %s", s_why);
    }
    return e;
}

const char *watch_gallery_open_why(void)
{
    return s_why;
}

void watch_gallery_close(void)
{
    if (!s_life) {
        return;
    }
    xSemaphoreTake(s_life, portMAX_DELAY);
    if (s_e && s_open) {
        s_open = false;
        gal_engine_close(s_e);
    }
    xSemaphoreGive(s_life);
}

gal_engine_t *watch_gallery_engine(void)
{
    return s_open ? s_e : NULL;
}
