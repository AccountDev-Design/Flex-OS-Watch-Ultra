/*
 * watch_browser — ver include/watch_browser.h.
 *
 * Memoria: la sesión (br_net_t, 11,6 KB), las listas (br_lists_t, 72 KB) y
 * el búfer de recepción (135 KB, solo con la app abierta) en PSRAM; mbedTLS
 * reserva en PSRAM (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC). En RAM interna, la
 * pila de la tarea (8 KB, FLEXBR_NET_STACK del perfil S3).
 */
#include "watch_browser.h"

#include "br_phone_view.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "flexfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "watch_net.h"

static const char *TAG = "watch_browser";

#define NVS_NS       "flexos_br"
#define RX_CAP       (FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 64)
#define TASK_PRIO    3
#define CONNECT_MS   10000
#define PERSIST_MS   20000
#define MIN_INTERNAL (20 * 1024)
#define MIN_PSRAM    (160 * 1024)
#define BACKEND_MS   500   /* cada cuánto se mira si cambió la fuente (relay del teléfono) */
#define RELAY_ASK_MS 15000 /* como mucho una petición de RELAY_START cada 15 s */

typedef struct {
    esp_transport_handle_t t;
    SemaphoreHandle_t mx;
} io_ctx_t;

static io_ctx_t s_io_ctx;
static br_io_t s_io;
static br_net_t *s_net;
static uint8_t *s_rx;
static br_lists_t *s_lists;
static uint32_t s_saved_gen, s_saved_ms;
static TaskHandle_t s_task;
static volatile bool s_stop;
static BrSettings s_set;
static bool s_set_loaded;
static char s_device[32];
static volatile int s_probe_state; /* 0 nada, 1 en curso, 2 hecho */
static SemaphoreHandle_t s_cfg_mx;  /* s_set y s_be: la interfaz escribe, la tarea lee */
static br_backend_t s_be;           /* la fuente aplicada a la sesión */
static watch_phone_status_t s_ps;   /* espacio de trabajo (con s_cfg_mx) */
static uint32_t s_be_ms, s_relay_ask_ms;
static char s_probe_msg[96];

/* ======================= transporte ======================= */

static int io_connect(void *ctx, const char *host, uint16_t port, bool tls)
{
    io_ctx_t *c = ctx;
    if (c->t) {
        esp_transport_close(c->t);
        esp_transport_destroy(c->t);
        c->t = NULL;
    }
    c->t = tls ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (!c->t) {
        return BR_IO_E_CONNECT;
    }
    if (tls) { /* RELOJ: certificado verificado (la referencia: setInsecure) */
        esp_transport_ssl_crt_bundle_attach(c->t, esp_crt_bundle_attach);
    }
    if (esp_transport_connect(c->t, host, port, CONNECT_MS) == 0) {
        return BR_IO_OK;
    }
    int ret = BR_IO_E_CONNECT;
    const esp_tls_error_handle_t e = esp_transport_get_error_handle(c->t);
    if (e) {
        if (e->esp_tls_flags != 0 || e->last_error == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) {
            ret = BR_IO_E_TLS;
        } else if (e->last_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
            ret = BR_IO_E_DNS;
        } else if (e->last_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
            ret = BR_IO_E_TIMEOUT;
        }
        ESP_LOGW(TAG, "conexión a %s:%u: %s (flags 0x%x)", host, port, esp_err_to_name(e->last_error),
                 e->esp_tls_flags);
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
    if (!c->t || s_stop) {
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

static uint32_t io_rand(void *ctx)
{
    (void)ctx;
    return esp_random();
}

static bool io_mem_ok(void *ctx)
{
    (void)ctx;
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= MIN_INTERNAL &&
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= MIN_PSRAM;
}

static void io_lock(void *ctx)
{
    xSemaphoreTake(((io_ctx_t *)ctx)->mx, portMAX_DELAY);
}

static void io_unlock(void *ctx)
{
    xSemaphoreGive(((io_ctx_t *)ctx)->mx);
}

/* ======================= ajustes (NVS) ======================= */

static int get_int(nvs_handle_t h, const char *k, int def, int lo, int hi)
{
    int32_t v;
    if (nvs_get_i32(h, k, &v) != ESP_OK || v < lo || v > hi) {
        return def; /* fuera de rango: el valor por defecto */
    }
    return (int)v;
}

static void get_str(nvs_handle_t h, const char *k, char *out, size_t n)
{
    size_t len = n;
    if (nvs_get_str(h, k, out, &len) != ESP_OK) {
        out[0] = 0;
    }
}

static void load_settings(void)
{
    flexBrSettingsDefaults(&s_set);
    s_set_loaded = true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    get_str(h, "brsrv", s_set.server, sizeof(s_set.server));
    get_str(h, "brtok", s_set.token, sizeof(s_set.token));
    get_str(h, "brtpl", s_set.searchCustom, sizeof(s_set.searchCustom));
    get_str(h, "brcloud", s_set.cloudServer, sizeof(s_set.cloudServer));
    s_set.searchEngine = (uint8_t)get_int(h, "breng", 0, 0, 3);
    s_set.profile = (uint8_t)get_int(h, "brprof", BRQ_BALANCED, 0, BRQ_N - 1);
    s_set.quality = (uint8_t)get_int(h, "brqual", 62, 20, 90);
    s_set.scalePct = (uint8_t)get_int(h, "brscale", 100, 50, 100);
    s_set.allowLocal = get_int(h, "brlocal", 0, 0, 1);
    s_set.allowInsecure = get_int(h, "brinsec", 0, 0, 1);
    s_set.saveHistory = get_int(h, "brhist", 1, 0, 1);
    s_set.telemetry = get_int(h, "brtel", 0, 0, 1);
    s_set.mediaNative = get_int(h, "brmed", 1, 0, 1);
    s_set.source = (uint8_t)get_int(h, "brsrc", BRSRC_AUTO, 0, BRSRC_N - 1);
    nvs_close(h);
}

static void save_settings(const BrSettings *st)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "brsrv", st->server);
    nvs_set_str(h, "brtok", st->token);
    nvs_set_str(h, "brtpl", st->searchCustom);
    nvs_set_str(h, "brcloud", st->cloudServer);
    nvs_set_i32(h, "breng", st->searchEngine);
    nvs_set_i32(h, "brprof", st->profile);
    nvs_set_i32(h, "brqual", st->quality);
    nvs_set_i32(h, "brscale", st->scalePct);
    nvs_set_i32(h, "brlocal", st->allowLocal);
    nvs_set_i32(h, "brinsec", st->allowInsecure);
    nvs_set_i32(h, "brhist", st->saveHistory);
    nvs_set_i32(h, "brtel", st->telemetry);
    nvs_set_i32(h, "brmed", st->mediaNative);
    nvs_set_i32(h, "brsrc", st->source);
    nvs_commit(h);
    nvs_close(h);
}

void watch_browser_get_settings(BrSettings *out)
{
    if (!s_set_loaded) {
        load_settings();
    }
    *out = s_set;
}

uint32_t watch_browser_caps(void)
{
    if (!s_set_loaded) {
        load_settings();
    }
    uint32_t c = FLEXBR_CAP_TILES | FLEXBR_CAP_TABS | FLEXBR_CAP_TLS | FLEXBR_CAP_PSRAM;
    if (s_be.src != BRSRC_NONE && s_be.url[0]) {
        c |= FLEXBR_CAP_REMOTE; /* hay servicio: el teléfono, la nube o el PC */
    }
    if (ffs_ready()) {
        c |= FLEXBR_CAP_FS;
    }
    return c;
}

static void cfg_lock(void)
{
    if (!s_cfg_mx) {
        s_cfg_mx = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_cfg_mx, portMAX_DELAY);
}

static void cfg_unlock(void)
{
    xSemaphoreGive(s_cfg_mx);
}

/*
 * Elige la fuente con el estado REAL del teléfono (br_backend) y, si cambió
 * algo, rehace la sesión. Si el teléfono puede servir y su relay está
 * parado y ask (navegador abierto), se lo pide por BLE con límite de ritmo:
 * con la app cerrada no se levanta nada en el teléfono. force: aplicar
 * aunque no cambie (ajustes nuevos).
 */
static void refresh_backend(bool force, bool ask)
{
    br_phone_view_t v;
    br_backend_t be;
    BrSettings set;
    cfg_lock();
    br_phone_view_get(&v, &s_ps);
    set = s_set;
    br_backend_resolve(&set, &v, &be);
    const bool changed = force || br_backend_differs(&be, &s_be);
    if (changed) {
        if (be.src != s_be.src) {
            ESP_LOGI(TAG, "fuente del navegador: %s%s%s", flexBrSourceName(be.src), be.why[0] ? " — " : "",
                     be.why);
        }
        s_be = be;
    }
    cfg_unlock();
    memset(&v, 0, sizeof(v)); /* la credencial no se queda en la pila */
    if (changed && s_net) {
        set.allowInsecure |= be.insecure_ok;
        br_net_configure(s_net, be.url, be.token, &set, s_device, watch_browser_caps());
        br_net_set_source(s_net, be.src, be.why);
    }
    const uint32_t now = io_now(NULL);
    if (ask && be.want_relay && (!s_relay_ask_ms || now - s_relay_ask_ms >= RELAY_ASK_MS)) {
        s_relay_ask_ms = now;
        if (watch_phone_relay(true)) {
            ESP_LOGI(TAG, "pidiendo a Flex Phone su servidor del navegador");
        }
    }
    memset(&be, 0, sizeof(be));
}

static void reconfigure(void)
{
    refresh_backend(true, s_task != NULL);
}

void watch_browser_set_settings(const BrSettings *st)
{
    cfg_lock();
    s_set = *st;
    s_set_loaded = true;
    cfg_unlock();
    save_settings(st);
    reconfigure();
}

void watch_browser_get_last(char *url, size_t n)
{
    url[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        get_str(h, "brlast", url, n);
        nvs_close(h);
    }
}

void watch_browser_set_last(const char *url)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "brlast", url ? url : "");
        nvs_commit(h);
        nvs_close(h);
    }
}

const char *watch_browser_device(void)
{
    if (!s_device[0]) {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_device, sizeof(s_device), "flexwatch-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return s_device;
}

bool watch_browser_online(void)
{
    watch_net_status_t ns;
    watch_net_get(&ns);
    return ns.state == NET_CONNECTED;
}

/* ======================= listas ======================= */

static bool list_path(bool hist, char *out, size_t n)
{
    if (!ffs_ready()) {
        return false;
    }
    char dir[200];
    snprintf(dir, sizeof(dir), "%s/.sistema", ffs_root());
    mkdir(dir, 0775);
    return snprintf(out, n, "%s/%s", dir, hist ? "br_hist.bin" : "br_bm.bin") < (int)n;
}

br_lists_t *watch_browser_lists(void)
{
    if (!s_lists) {
        s_lists = heap_caps_calloc(1, sizeof(*s_lists), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_lists) {
            return NULL;
        }
        char p[260];
        if (list_path(true, p, sizeof(p))) {
            br_lists_load(s_lists, true, p);
        }
        if (list_path(false, p, sizeof(p))) {
            br_lists_load(s_lists, false, p);
        }
        s_saved_gen = s_lists->gen;
    }
    return s_lists;
}

void watch_browser_persist(bool force)
{
    if (!s_lists || s_lists->gen == s_saved_gen) {
        return;
    }
    const uint32_t now = io_now(NULL);
    if (!force && now - s_saved_ms < PERSIST_MS) {
        return; /* la flash no se gasta en cada visita */
    }
    char p[260];
    if (list_path(true, p, sizeof(p))) {
        br_lists_save(s_lists, true, p);
    }
    if (list_path(false, p, sizeof(p))) {
        br_lists_save(s_lists, false, p);
    }
    s_saved_gen = s_lists->gen;
    s_saved_ms = now;
}

/* ======================= tarea ======================= */

static void task(void *arg)
{
    (void)arg;
    UBaseType_t min_stack = ~(UBaseType_t)0;
    while (!s_stop) {
        const uint32_t now = io_now(NULL);
        if (now - s_be_ms >= BACKEND_MS) {
            s_be_ms = now;
            refresh_backend(false, true); /* el relay del teléfono va y viene solo */
        }
        br_net_set_online(s_net, watch_browser_online());
        uint32_t w = br_net_step(s_net);
        const UBaseType_t st = uxTaskGetStackHighWaterMark(NULL);
        if (st < min_stack) { /* tras la conexión TLS, el mínimo real de pila */
            min_stack = st;
            if (st < 2048) {
                ESP_LOGW(TAG, "pila de la sesion: solo %u B libres de %u", (unsigned)st, (unsigned)FLEXBR_NET_STACK);
            }
        }
        if (w > 100) {
            w = 100; /* se mira el Wi-Fi y la orden de parar a menudo */
        }
        vTaskDelay(pdMS_TO_TICKS(w ? w : 1));
    }
    br_net_shutdown(s_net);
    ESP_LOGI(TAG, "sesion cerrada: pila minima libre %u de %u B", (unsigned)min_stack, (unsigned)FLEXBR_NET_STACK);
    /* La tarea suelta lo suyo al terminar: cerrar no espera en la de LVGL. */
    heap_caps_free(s_rx);
    s_rx = NULL;
    __atomic_store_n(&s_task, (TaskHandle_t)NULL, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

/* Lo que la sesión necesita de verdad (F11) antes de reservar nada:
 *  - RAM interna: la pila de la tarea en UN bloque y, además, MIN_INTERNAL
 *    libres para lwIP, el Wi-Fi, el BLE y la interfaz (no se le da todo);
 *  - PSRAM: el búfer de recepción en un bloque contiguo y MIN_PSRAM de margen
 *    (TLS de mbedTLS, lienzo de la página, el resto del sistema).
 * Se mide con las API de ESP-IDF; why dice qué falta y cuánto. */
static char s_mem_why[96];

bool watch_browser_mem_check(char *why, size_t n)
{
    const size_t in_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t in_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t ps_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t ps_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const size_t need_ps = (s_rx ? 0 : RX_CAP) + (s_net ? 0 : sizeof(*s_net));
    const char *msg = NULL;
    if (in_block < FLEXBR_NET_STACK + 512 || in_free < FLEXBR_NET_STACK + MIN_INTERNAL) {
        snprintf(s_mem_why, sizeof(s_mem_why), "Falta RAM interna: %u KB libres (bloque %u KB)",
                 (unsigned)(in_free / 1024), (unsigned)(in_block / 1024));
        msg = s_mem_why;
    } else if (ps_block < (s_rx ? 0 : RX_CAP) || ps_free < need_ps + MIN_PSRAM) {
        snprintf(s_mem_why, sizeof(s_mem_why), "Falta PSRAM: %u KB libres (bloque %u KB)", (unsigned)(ps_free / 1024),
                 (unsigned)(ps_block / 1024));
        msg = s_mem_why;
    }
    if (msg) {
        ESP_LOGW(TAG, "sesion no abierta: %s", msg);
        if (why && n) {
            snprintf(why, n, "%s", msg);
        }
        return false;
    }
    return true;
}

esp_err_t watch_browser_open(void)
{
    if (s_task) {
        /* Cerrándose (cierre asíncrono): se reabrirá cuando termine. */
        return s_stop ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (!watch_browser_mem_check(NULL, 0)) {
        return ESP_ERR_NO_MEM; /* nada reservado: el reloj sigue igual */
    }
    if (!s_set_loaded) {
        load_settings();
    }
    watch_browser_device();
    watch_browser_lists();
    if (!s_io_ctx.mx) {
        s_io_ctx.mx = xSemaphoreCreateMutex();
    }
    if (!s_net) {
        s_net = heap_caps_calloc(1, sizeof(*s_net), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_rx) {
        s_rx = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_net || !s_rx || !s_io_ctx.mx) {
        ESP_LOGE(TAG, "sin memoria para el navegador");
        return ESP_ERR_NO_MEM;
    }
    s_io = (br_io_t){.ctx = &s_io_ctx, .connect = io_connect, .write = io_write, .read = io_read,
                     .close = io_close, .now = io_now, .rand = io_rand, .mem_ok = io_mem_ok,
                     .lock = io_lock, .unlock = io_unlock};
    br_net_init(s_net, &s_io, s_rx, RX_CAP);
    s_relay_ask_ms = 0; /* al abrir, si hace falta, se pide el relay enseguida */
    reconfigure();
    s_stop = false;
    if (xTaskCreate(task, "watch_br", FLEXBR_NET_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        heap_caps_free(s_rx); /* lo reservado se devuelve: nada queda a medias */
        s_rx = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "sesión abierta: %u B de sesión + %u B de recepción en PSRAM", (unsigned)sizeof(*s_net),
             (unsigned)RX_CAP);
    return ESP_OK;
}

void watch_browser_close(void)
{
    watch_browser_persist(true);
    if (!s_task) {
        return;
    }
    /* Sin esperar: antes se esperaba hasta 3 s DENTRO de la tarea de LVGL
     * (la interfaz se congelaba al cerrar el navegador). La tarea de red
     * termina sola (como mucho 100 ms de espera más lo que tarde en cerrar el
     * socket) y libera el búfer de recepción (task()). */
    s_stop = true;
}

void watch_browser_source(watch_browser_source_t *out)
{
    if (!s_set_loaded) {
        load_settings();
    }
    if (!s_task) {
        refresh_backend(false, false); /* sin la tarea nadie la refresca */
    }
    cfg_lock();
    out->src = s_be.src;
    out->pref = s_set.source;
    snprintf(out->why, sizeof(out->why), "%s", s_be.why);
    out->relay_state = s_ps.relay.state;
    memcpy(out->relay_ip, s_ps.relay.ip, 4);
    out->relay_port = s_ps.relay.port;
    out->phone_possible = s_ps.bonded && (s_set.source == BRSRC_AUTO || s_set.source == BRSRC_PHONE);
    cfg_unlock();
}

br_net_t *watch_browser_net(void)
{
    return s_task && !s_stop ? s_net : NULL;
}

/* ======================= probar el servidor ======================= */

static void probe_task(void *arg)
{
    (void)arg;
    bool tls;
    char host[FLEXBR_HOST_MAX], path[128], url[300];
    uint16_t port;
    char srv[FLEXBR_SERVER_MAX], why[96];
    cfg_lock();
    snprintf(srv, sizeof(srv), "%s", s_be.url); /* la fuente EN USO: teléfono, nube o PC */
    snprintf(why, sizeof(why), "%s", s_be.why);
    cfg_unlock();
    if (!flexBrParseWsUrl(srv, &tls, host, sizeof(host), &port, path, sizeof(path))) {
        snprintf(s_probe_msg, sizeof(s_probe_msg), "%s", why[0] ? why : "Configura antes la dirección del servidor");
    } else if (!watch_browser_online()) {
        snprintf(s_probe_msg, sizeof(s_probe_msg), "Sin Wi-Fi");
    } else {
        snprintf(url, sizeof(url), "%s://%s:%u/v1/health", tls ? "https" : "http", host, (unsigned)port);
        const esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 6000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t h = esp_http_client_init(&cfg);
        if (!h) {
            snprintf(s_probe_msg, sizeof(s_probe_msg), "No se pudo abrir la conexión");
        } else {
            if (esp_http_client_perform(h) == ESP_OK) {
                const int code = esp_http_client_get_status_code(h);
                if (code == 200) {
                    snprintf(s_probe_msg, sizeof(s_probe_msg), "Servidor accesible");
                } else {
                    snprintf(s_probe_msg, sizeof(s_probe_msg), "El servidor respondió %d", code);
                }
            } else {
                snprintf(s_probe_msg, sizeof(s_probe_msg), "El servidor no responde");
            }
            esp_http_client_cleanup(h);
        }
    }
    s_probe_state = 2;
    vTaskDelete(NULL);
}

void watch_browser_probe_start(void)
{
    if (s_probe_state == 1) {
        return;
    }
    if (!s_set_loaded) {
        load_settings();
    }
    if (!s_task) {
        refresh_backend(false, false); /* sin la app abierta, la fuente se calcula ahora */
    }
    s_probe_state = 1;
    if (xTaskCreate(probe_task, "watch_br_probe", 7168, NULL, 2, NULL) != pdPASS) {
        snprintf(s_probe_msg, sizeof(s_probe_msg), "No se pudo abrir la conexión");
        s_probe_state = 2;
    }
}

int watch_browser_probe_result(char *msg, size_t n)
{
    if (s_probe_state != 2) {
        return 0;
    }
    snprintf(msg, n, "%s", s_probe_msg);
    s_probe_state = 0;
    return 1;
}
