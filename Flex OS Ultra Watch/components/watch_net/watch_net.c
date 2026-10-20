/*
 * watch_net — ver include/watch_net.h
 *
 * Una sola tarea es dueña del estado: los comandos de la interfaz y los
 * eventos de ESP-IDF (Wi-Fi, IP, SNTP) llegan a su cola. Así no hay dos
 * tareas cambiando el estado a la vez y ningún manejador de eventos bloquea.
 */
#include "watch_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "watch_time.h"
#include "ws_logic.h"

static const char *TAG = "watch_net";

#define NVS_NS        "wifi"
#define CREDS_VER     1
#define RSSI_EVERY_MS 10000
#define TIME_MIN_UTC  1735689600LL /* 2025-01-01: una hora anterior es de un SNTP sin respuesta */
/* Pila de la tarea: esp_wifi_init/esp_wifi_start (calibración del PHY con su
 * lectura de NVS) y nvs_set_blob se ejecutan en ella. Con 4 KB y los búferes
 * de las credenciales en la pila quedaba un margen de pocos cientos de bytes
 * (ver persist()/load()): ahora los búferes son estáticos y la pila, 6 KB. */
#define NET_TASK_STACK 6144
/* RAM interna mínima para intentar encender el Wi-Fi. esp_wifi_init/start
 * reservan en RAM interna los búferes estáticos de recepción (16 x 1,6 KB),
 * las tareas del Wi-Fi y estructuras del PHY; por debajo de esto, mejor
 * decirlo en la interfaz que dejar que una reserva interna falle a medias. */
#define NET_MIN_INTERNAL (48 * 1024)
#define CREDS_RAW_MAX (1 + NET_MAX_SAVED * (2 + NET_SSID_MAX + NET_PASS_MAX))

typedef enum {
    M_ENABLE, M_DISABLE, M_SCAN, M_CONNECT, M_FORGET,                 /* de la interfaz */
    M_STA_START, M_SCAN_DONE, M_DISCONNECTED, M_GOT_IP, M_TIME_SYNCED, /* de ESP-IDF */
} msg_type_t;

typedef struct {
    uint8_t type;
    int arg;
    char ssid[NET_SSID_MAX + 1];
    char pass[NET_PASS_MAX + 1];
    bool has_pass;
} msg_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_q;
static watch_net_status_t s_st;
static net_ap_t s_aps[NET_SCAN_MAX];
static int s_naps;
static net_creds_t s_creds;

/* Solo los toca la tarea */
static bool s_wifi_ready;      /* esp_wifi_init hecho */
/* Pasos de wifi_init_once que NO se pueden repetir: esp_netif_create_default_wifi_sta()
 * hace assert() si ya existe la interfaz "WIFI_STA_DEF". Antes, si esp_wifi_init
 * fallaba (p. ej. sin RAM interna con el BLE encendido), el siguiente intento de
 * encender el Wi-Fi volvía a crearla y el reloj se reiniciaba. */
static bool s_netif_ready;
static esp_netif_t *s_sta_netif;
static bool s_handlers_ready;
/* Búferes de NVS y del escaneo: solo los usa la tarea (fuera de su pila). */
static uint8_t s_raw[CREDS_RAW_MAX + WS_BLOB_HDR];
static uint8_t s_blob[CREDS_RAW_MAX + WS_BLOB_HDR];
static net_ap_t s_scan_tmp[NET_SCAN_MAX * 2];
static bool s_sntp_on;
static int s_expect_disc;      /* desconexiones pedidas por nosotros: no son fallos */
static int s_attempt;
static int64_t s_retry_at_us;  /* 0 = sin reintento pendiente */
static net_cred_t s_trying;    /* credenciales del intento en curso (se guardan si funciona) */
static bool s_scanning;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void post(const msg_t *m)
{
    if (s_q && xQueueSend(s_q, m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cola llena: mensaje %d descartado", m->type);
    }
}

static void post_simple(msg_type_t t, int arg)
{
    msg_t m = {.type = (uint8_t)t, .arg = arg};
    post(&m);
}

/* ---- NVS (solo la tarea escribe) ---- */
static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS no disponible: el Wi-Fi no se guarda");
        return;
    }
    lock();
    const size_t n = net_creds_encode(&s_creds, s_raw, CREDS_RAW_MAX);
    const bool on = s_st.enabled;
    unlock();
    const size_t bn = ws_blob_pack(CREDS_VER, s_raw, n, s_blob, sizeof(s_blob));
    esp_err_t e = nvs_set_u8(h, "on", on ? 1 : 0);
    if (e == ESP_OK && bn) {
        e = nvs_set_blob(h, "creds", s_blob, bn);
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "no se pudo guardar: %s", esp_err_to_name(e));
    }
}

static bool load(void)
{
    nvs_handle_t h;
    bool on = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    uint8_t u8 = 0;
    if (nvs_get_u8(h, "on", &u8) == ESP_OK) {
        on = u8 != 0;
    }
    size_t bn = sizeof(s_blob), rn = 0;
    uint8_t ver = 0;
    if (nvs_get_blob(h, "creds", s_blob, &bn) == ESP_OK &&
        ws_blob_unpack(s_blob, bn, &ver, s_raw, sizeof(s_raw), &rn) && ver == CREDS_VER) {
        lock();
        if (!net_creds_decode(&s_creds, s_raw, rn)) {
            ESP_LOGW(TAG, "redes guardadas dañadas: se ignoran");
        }
        unlock();
    }
    nvs_close(h);
    return on;
}

/* ---- ESP-IDF -> cola ---- */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            post_simple(M_STA_START, 0);
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            post_simple(M_SCAN_DONE, 0);
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t *d = data;
            post_simple(M_DISCONNECTED, d ? d->reason : 0);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        msg_t m = {.type = M_GOT_IP};
        if (e) {
            snprintf(m.ssid, sizeof(m.ssid), IPSTR, IP2STR(&e->ip_info.ip));
        }
        post(&m);
    }
}

static void on_time(struct timeval *tv)
{
    (void)tv;
    post_simple(M_TIME_SYNCED, 0);
}

/* ---- Acciones (en la tarea) ---- */
static bool wifi_init_once(void)
{
    if (s_wifi_ready) {
        return true;
    }
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "iniciando Wi-Fi: RAM interna libre %u B (bloque mayor %u B)", (unsigned)internal,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (internal < NET_MIN_INTERNAL) {
        ESP_LOGE(TAG, "RAM interna insuficiente para el Wi-Fi (%u B < %u B)", (unsigned)internal,
                 (unsigned)NET_MIN_INTERNAL);
        return false;
    }
    esp_err_t e = ESP_OK;
    /* Cada paso se hace una sola vez aunque un intento anterior fallara más
     * adelante: la interfaz y los manejadores no se pueden crear dos veces. */
    if (!s_netif_ready) {
        e = esp_netif_init();
        if (e == ESP_OK) {
            e = esp_event_loop_create_default();
            if (e == ESP_ERR_INVALID_STATE) {
                e = ESP_OK; /* ya existía */
            }
        }
        if (e == ESP_OK) {
            s_netif_ready = true;
        }
    }
    if (e == ESP_OK && !s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif) {
            e = ESP_FAIL;
        }
    }
    if (e == ESP_OK) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        e = esp_wifi_init(&cfg);
    }
    if (e == ESP_OK) {
        e = esp_wifi_set_storage(WIFI_STORAGE_RAM); /* las redes las guarda watch_net */
        if (e == ESP_OK) {
            e = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if (e != ESP_OK) {
            esp_wifi_deinit(); /* el siguiente intento empieza limpio */
        }
    }
    if (e == ESP_OK && !s_handlers_ready) {
        e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL);
        if (e == ESP_OK) {
            e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL);
        }
        if (e == ESP_OK) {
            s_handlers_ready = true;
        } else {
            esp_wifi_deinit();
        }
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no se pudo iniciar el Wi-Fi: %s", esp_err_to_name(e));
        return false;
    }
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi iniciado: RAM interna libre %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
}

static void set_state(net_state_t st)
{
    lock();
    s_st.state = st;
    if (st != NET_FAILED) {
        s_st.retry_in_s = 0;
    }
    unlock();
}

static void start_scan(void)
{
    wifi_scan_config_t sc = {.show_hidden = false};
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) {
        s_scanning = true;
        lock();
        if (s_st.state != NET_CONNECTED && s_st.state != NET_CONNECTING) {
            s_st.state = NET_SCANNING;
        }
        unlock();
    }
}

static void connect_to(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", pass ? pass : "");
    wc.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (s_scanning) {
        esp_wifi_scan_stop();
        s_scanning = false;
    }
    lock();
    const bool was_linked = s_st.state == NET_CONNECTED || s_st.state == NET_CONNECTING;
    unlock();
    if (was_linked) {
        s_expect_disc++;
        esp_wifi_disconnect();
    }
    snprintf(s_trying.ssid, sizeof(s_trying.ssid), "%s", ssid);
    snprintf(s_trying.pass, sizeof(s_trying.pass), "%s", pass ? pass : "");
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e == ESP_OK) {
        e = esp_wifi_connect();
    }
    lock();
    snprintf(s_st.ssid, sizeof(s_st.ssid), "%s", ssid);
    s_st.ip[0] = 0;
    s_st.auth_failed = false;
    s_st.reason = 0;
    s_st.state = e == ESP_OK ? NET_CONNECTING : NET_FAILED;
    s_st.retry_in_s = 0;
    unlock();
    s_retry_at_us = 0;
    ESP_LOGI(TAG, "conectando a \"%s\"", ssid);
}

static void sntp_start(void)
{
    if (s_sntp_on) {
        return;
    }
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    c.sync_cb = on_time;
    if (esp_netif_sntp_init(&c) == ESP_OK) {
        s_sntp_on = true;
    }
}

static void sntp_stop(void)
{
    if (s_sntp_on) {
        esp_netif_sntp_deinit();
        s_sntp_on = false;
    }
}

static void enable(bool on)
{
    lock();
    const bool was = s_st.enabled;
    s_st.enabled = on;
    unlock();
    if (on) {
        if (!wifi_init_once()) {
            /* Queda apagado y con el motivo a la vista: nunca a medio iniciar. */
            lock();
            s_st.enabled = false;
            s_st.state = NET_FAILED;
            s_st.reason = 0;
            s_st.retry_in_s = 0;
            unlock();
            return;
        }
        if (!was || s_st.state == NET_OFF) {
            set_state(NET_IDLE);
            const esp_err_t e = esp_wifi_start(); /* WIFI_EVENT_STA_START -> escaneo */
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(e));
                lock();
                s_st.enabled = false;
                s_st.state = NET_FAILED;
                unlock();
                return;
            }
        }
    } else if (s_wifi_ready) {
        sntp_stop();
        s_expect_disc = 0;
        esp_wifi_stop();
        s_scanning = false;
        s_retry_at_us = 0;
        lock();
        s_st.state = NET_OFF;
        s_st.enabled = false;
        s_st.ssid[0] = 0;
        s_st.ip[0] = 0;
        s_st.rssi = 0;
        s_st.reason = 0;
        s_st.auth_failed = false;
        s_st.time_synced = false;
        s_st.retry_in_s = 0;
        unlock();
    }
    if (was != on) {
        persist();
    }
    ESP_LOGI(TAG, "Wi-Fi %s", on ? "encendido" : "apagado");
}

static void scan_done(void)
{
    s_scanning = false;
    if (!s_st.enabled) {
        return; /* llegó después de apagar */
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > NET_SCAN_MAX * 2) {
        n = NET_SCAN_MAX * 2;
    }
    wifi_ap_record_t *rec = n ? calloc(n, sizeof(*rec)) : NULL;
    if (n && !rec) {
        esp_wifi_clear_ap_list();
        n = 0;
    }
    if (rec) {
        esp_wifi_scan_get_ap_records(&n, rec);
    }
    net_ap_t *aps = s_scan_tmp;
    int k = 0;
    for (int i = 0; i < n && k < (int)(sizeof(s_scan_tmp) / sizeof(s_scan_tmp[0])); i++) {
        snprintf(aps[k].ssid, sizeof(aps[k].ssid), "%s", (const char *)rec[i].ssid);
        aps[k].rssi = rec[i].rssi;
        aps[k].secure = rec[i].authmode != WIFI_AUTH_OPEN;
        aps[k].saved = false;
        k++;
    }
    free(rec);
    lock();
    k = net_scan_sort(aps, k, &s_creds);
    s_naps = k > NET_SCAN_MAX ? NET_SCAN_MAX : k;
    memcpy(s_aps, aps, (size_t)s_naps * sizeof(net_ap_t));
    s_st.scan_gen++;
    const net_state_t st = s_st.state;
    const int pick = net_pick_saved(s_aps, s_naps);
    net_cred_t c = {0};
    if (pick >= 0) {
        const int ci = net_creds_find(&s_creds, s_aps[pick].ssid);
        if (ci >= 0) {
            c = s_creds.c[ci];
        }
    }
    unlock();
    /* Sin conexión: a la mejor red guardada a la vista (salvo contraseña mala) */
    if (st != NET_CONNECTED && st != NET_CONNECTING && c.ssid[0] && !s_st.auth_failed) {
        s_attempt = 0;
        connect_to(c.ssid, c.pass);
    } else if (st == NET_SCANNING) {
        set_state(NET_IDLE);
    }
}

static void disconnected(int reason)
{
    if (s_expect_disc > 0) {
        s_expect_disc--;
        return;
    }
    if (!s_st.enabled) {
        return; /* desconexión propia de esp_wifi_stop(): no es un fallo */
    }
    lock();
    const bool enabled = s_st.enabled;
    s_st.reason = reason;
    s_st.ip[0] = 0;
    if (net_reason_is_auth(reason)) {
        s_st.auth_failed = true;
        s_st.state = NET_FAILED;
        s_st.retry_in_s = 0;
        unlock();
        s_retry_at_us = 0;
        ESP_LOGW(TAG, "\"%s\": %s (no se reintenta)", s_trying.ssid, net_reason_text(reason));
        return;
    }
    const uint32_t wait = net_backoff_ms(s_attempt++);
    s_st.state = NET_FAILED;
    s_st.retry_in_s = (int)((wait + 999) / 1000);
    unlock();
    if (enabled) {
        s_retry_at_us = esp_timer_get_time() + (int64_t)wait * 1000;
    }
    ESP_LOGW(TAG, "desconectado (%d: %s); reintento en %u ms", reason, net_reason_text(reason), (unsigned)wait);
}

static void got_ip(const char *ip)
{
    if (!s_st.enabled) {
        return; /* IP de una conexión que ya se apagó */
    }
    wifi_ap_record_t ap;
    const bool ok = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    lock();
    s_st.state = NET_CONNECTED;
    snprintf(s_st.ip, sizeof(s_st.ip), "%.15s", ip); /* "255.255.255.255" */
    s_st.reason = 0;
    s_st.retry_in_s = 0;
    s_st.auth_failed = false;
    if (ok) {
        s_st.rssi = ap.rssi;
    }
    /* La red funciona: ahora sí se guarda (la primera de la lista) */
    net_creds_put(&s_creds, s_trying.ssid, s_trying.pass);
    unlock();
    s_attempt = 0;
    s_retry_at_us = 0;
    persist();
    sntp_start();
    ESP_LOGI(TAG, "conectado a \"%s\" con IP %s", s_trying.ssid, ip);
}

static void time_synced(void)
{
    const time_t now = time(NULL);
    if ((int64_t)now < TIME_MIN_UTC) {
        return;
    }
    if (watch_time_set_utc((int64_t)now) == ESP_OK) {
        lock();
        s_st.time_synced = true;
        unlock();
        ESP_LOGI(TAG, "hora de Internet (SNTP) aplicada al RTC");
    }
}

static void handle(const msg_t *m)
{
    switch (m->type) {
    case M_ENABLE:
        enable(true);
        break;
    case M_DISABLE:
        enable(false);
        break;
    case M_SCAN:
        if (s_st.enabled && !s_scanning) {
            start_scan();
        }
        break;
    case M_CONNECT:
        if (s_st.enabled) {
            const char *pass = m->has_pass ? m->pass : NULL;
            net_cred_t c = {0};
            if (!pass) {
                lock();
                const int i = net_creds_find(&s_creds, m->ssid);
                if (i >= 0) {
                    c = s_creds.c[i];
                }
                unlock();
                pass = c.pass;
            }
            s_attempt = 0;
            connect_to(m->ssid, pass);
        }
        break;
    case M_FORGET: {
        lock();
        net_creds_remove(&s_creds, m->ssid);
        const bool current = strcmp(s_st.ssid, m->ssid) == 0 && s_st.state != NET_OFF;
        for (int i = 0; i < s_naps; i++) {
            if (strcmp(s_aps[i].ssid, m->ssid) == 0) {
                s_aps[i].saved = false;
            }
        }
        s_st.scan_gen++;
        unlock();
        if (current) {
            s_expect_disc++;
            esp_wifi_disconnect();
            s_retry_at_us = 0;
            lock();
            s_st.ssid[0] = 0;
            s_st.ip[0] = 0;
            unlock();
            set_state(NET_IDLE);
        }
        persist();
    } break;
    case M_STA_START:
        if (s_st.enabled) {
            start_scan();
        }
        break;
    case M_SCAN_DONE:
        scan_done();
        break;
    case M_DISCONNECTED:
        disconnected(m->arg);
        break;
    case M_GOT_IP:
        got_ip(m->ssid);
        break;
    case M_TIME_SYNCED:
        time_synced();
        break;
    default:
        break;
    }
}

static void net_task(void *arg)
{
    (void)arg;
    if (load()) {
        enable(true);
    }
    int64_t rssi_at = 0;
    for (;;) {
        TickType_t wait = pdMS_TO_TICKS(1000);
        const int64_t now = esp_timer_get_time();
        if (s_retry_at_us && s_retry_at_us > now) {
            const int64_t ms = (s_retry_at_us - now) / 1000;
            if (ms < 1000) {
                wait = pdMS_TO_TICKS(ms + 1);
            }
        }
        msg_t m;
        if (xQueueReceive(s_q, &m, wait) == pdTRUE) {
            handle(&m);
            continue;
        }
        const int64_t t = esp_timer_get_time();
        if (s_retry_at_us && t >= s_retry_at_us && s_st.enabled) {
            s_retry_at_us = 0;
            lock();
            s_st.state = NET_CONNECTING;
            s_st.retry_in_s = 0;
            unlock();
            esp_wifi_connect();
        } else if (s_retry_at_us) {
            lock();
            s_st.retry_in_s = (int)((s_retry_at_us - t + 999999) / 1000000);
            unlock();
        }
        if (s_st.state == NET_CONNECTED && t - rssi_at > RSSI_EVERY_MS * 1000LL) {
            rssi_at = t;
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                lock();
                s_st.rssi = ap.rssi;
                unlock();
            }
        }
    }
}

/* ---- API ---- */
esp_err_t watch_net_start(void)
{
    if (s_q) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(8, sizeof(msg_t));
    if (!s_lock || !s_q) {
        return ESP_ERR_NO_MEM;
    }
    net_creds_init(&s_creds);
    s_st.state = NET_OFF;
    if (xTaskCreate(net_task, "watch_net", NET_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void watch_net_get(watch_net_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    *out = s_st;
    unlock();
}

void watch_net_set_enabled(bool on)
{
    post_simple(on ? M_ENABLE : M_DISABLE, 0);
}

void watch_net_scan(void)
{
    post_simple(M_SCAN, 0);
}

int watch_net_scan_results(net_ap_t *out, int max)
{
    if (!s_lock) {
        return 0;
    }
    lock();
    const int n = s_naps < max ? s_naps : max;
    memcpy(out, s_aps, (size_t)n * sizeof(net_ap_t));
    unlock();
    return n;
}

void watch_net_connect(const char *ssid, const char *pass)
{
    msg_t m = {.type = M_CONNECT, .has_pass = pass != NULL};
    snprintf(m.ssid, sizeof(m.ssid), "%s", ssid ? ssid : "");
    snprintf(m.pass, sizeof(m.pass), "%s", pass ? pass : "");
    if (m.ssid[0]) {
        post(&m);
    }
}

void watch_net_forget(const char *ssid)
{
    msg_t m = {.type = M_FORGET};
    snprintf(m.ssid, sizeof(m.ssid), "%s", ssid ? ssid : "");
    post(&m);
}

int watch_net_saved(char ssids[][NET_SSID_MAX + 1], int max)
{
    if (!s_lock) {
        return 0;
    }
    lock();
    const int n = s_creds.n < max ? s_creds.n : max;
    for (int i = 0; i < n; i++) {
        snprintf(ssids[i], NET_SSID_MAX + 1, "%s", s_creds.c[i].ssid);
    }
    unlock();
    return n;
}
