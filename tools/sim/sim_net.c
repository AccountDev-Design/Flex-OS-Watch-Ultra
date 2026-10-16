/*
 * Wi-Fi de la vista previa: la API de watch_net con un guion determinista
 * (no hay radio en el PC). Usa la misma lógica pura (net_logic) y los mismos
 * textos (net_text.c) que el firmware.
 *  - Encender: busca 900 ms y encuentra "Casa" (-52), "Vecino" (-70) y
 *    "Cafe libre" (abierta, -80). Si hay una guardada a la vista, conecta.
 *  - Conectar: 1,2 s. "Casa" solo con "clave1234"; si no, contraseña
 *    incorrecta. Las abiertas conectan siempre. Al conectar se guarda y la
 *    hora "se sincroniza" 1 s después.
 *  - sim_net_drop(): se pierde la señal (reintento en 1 s, vuelve a los 2 s).
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sim.h"
#include "watch_net.h"

static watch_net_status_t s_st;
static net_creds_t s_creds;
static net_ap_t s_aps[NET_SCAN_MAX];
static int s_naps;
static uint32_t s_scan_at, s_conn_at, s_time_at, s_retry_at;
static char s_try_pass[NET_PASS_MAX + 1];

void sim_net_reset(void)
{
    memset(&s_st, 0, sizeof(s_st));
    net_creds_init(&s_creds);
    s_naps = 0;
    s_scan_at = s_conn_at = s_time_at = s_retry_at = 0;
}

void sim_net_drop(void)
{
    if (s_st.state == NET_CONNECTED) {
        s_st.state = NET_FAILED;
        s_st.reason = 200; /* WIFI_REASON_BEACON_TIMEOUT */
        s_st.ip[0] = 0;
        s_st.retry_in_s = 1;
        s_retry_at = lv_tick_get() + 1000;
    }
}

static void begin_connect(const char *ssid, const char *pass)
{
    snprintf(s_st.ssid, sizeof(s_st.ssid), "%s", ssid);
    snprintf(s_try_pass, sizeof(s_try_pass), "%s", pass ? pass : "");
    s_st.state = NET_CONNECTING;
    s_st.reason = 0;
    s_st.auth_failed = false;
    s_st.retry_in_s = 0;
    s_conn_at = lv_tick_get() + 1200;
}

static void advance(void)
{
    const uint32_t now = lv_tick_get();
    if (s_scan_at && now >= s_scan_at) {
        s_scan_at = 0;
        net_ap_t a[3] = {{"Casa", -52, true, false}, {"Vecino", -70, true, false}, {"Cafe libre", -80, false, false}};
        s_naps = net_scan_sort(a, 3, &s_creds);
        memcpy(s_aps, a, sizeof(net_ap_t) * (size_t)s_naps);
        s_st.scan_gen++;
        const int pick = net_pick_saved(s_aps, s_naps);
        if (s_st.state != NET_CONNECTED && s_st.state != NET_CONNECTING && pick >= 0) {
            const int ci = net_creds_find(&s_creds, s_aps[pick].ssid);
            begin_connect(s_creds.c[ci].ssid, s_creds.c[ci].pass);
        } else if (s_st.state == NET_SCANNING) {
            s_st.state = NET_IDLE;
        }
    }
    if (s_conn_at && now >= s_conn_at) {
        s_conn_at = 0;
        const bool open = strcmp(s_st.ssid, "Cafe libre") == 0;
        if (open || (strcmp(s_st.ssid, "Casa") == 0 && strcmp(s_try_pass, "clave1234") == 0)) {
            s_st.state = NET_CONNECTED;
            snprintf(s_st.ip, sizeof(s_st.ip), "192.168.1.50");
            s_st.rssi = open ? -80 : -52;
            net_creds_put(&s_creds, s_st.ssid, s_try_pass);
            for (int i = 0; i < s_naps; i++) {
                s_aps[i].saved = net_creds_find(&s_creds, s_aps[i].ssid) >= 0;
            }
            s_st.scan_gen++;
            s_time_at = now + 1000;
        } else {
            s_st.state = NET_FAILED;
            s_st.reason = 202; /* WIFI_REASON_AUTH_FAIL */
            s_st.auth_failed = true;
        }
    }
    if (s_retry_at && now >= s_retry_at) {
        s_retry_at = 0;
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        snprintf(ssid, sizeof(ssid), "%s", s_st.ssid);
        snprintf(pass, sizeof(pass), "%s", s_try_pass);
        begin_connect(ssid, pass);
    } else if (s_retry_at) {
        s_st.retry_in_s = (int)((s_retry_at - now + 999) / 1000);
    }
    if (s_time_at && now >= s_time_at && s_st.state == NET_CONNECTED) {
        s_time_at = 0;
        s_st.time_synced = true;
    }
}

esp_err_t watch_net_start(void)
{
    return ESP_OK;
}

void watch_net_get(watch_net_status_t *out)
{
    advance();
    *out = s_st;
}

void watch_net_set_enabled(bool on)
{
    if (on && !s_st.enabled) {
        s_st.enabled = true;
        s_st.state = NET_SCANNING;
        s_scan_at = lv_tick_get() + 900;
    } else if (!on && s_st.enabled) {
        const uint32_t gen = s_st.scan_gen;
        memset(&s_st, 0, sizeof(s_st));
        s_st.scan_gen = gen;
        s_scan_at = s_conn_at = s_time_at = s_retry_at = 0;
    }
}

void watch_net_scan(void)
{
    if (s_st.enabled && !s_scan_at) {
        if (s_st.state != NET_CONNECTED && s_st.state != NET_CONNECTING) {
            s_st.state = NET_SCANNING;
        }
        s_scan_at = lv_tick_get() + 900;
    }
}

int watch_net_scan_results(net_ap_t *out, int max)
{
    advance();
    const int n = s_naps < max ? s_naps : max;
    memcpy(out, s_aps, sizeof(net_ap_t) * (size_t)n);
    return n;
}

void watch_net_connect(const char *ssid, const char *pass)
{
    if (!s_st.enabled || !ssid || !ssid[0]) {
        return;
    }
    if (!pass) {
        const int i = net_creds_find(&s_creds, ssid);
        pass = i >= 0 ? s_creds.c[i].pass : "";
    }
    s_retry_at = 0;
    begin_connect(ssid, pass);
}

void watch_net_forget(const char *ssid)
{
    net_creds_remove(&s_creds, ssid);
    for (int i = 0; i < s_naps; i++) {
        if (strcmp(s_aps[i].ssid, ssid) == 0) {
            s_aps[i].saved = false;
        }
    }
    s_st.scan_gen++;
    if (strcmp(s_st.ssid, ssid) == 0 && s_st.state == NET_CONNECTED) {
        s_st.state = NET_IDLE;
        s_st.ssid[0] = 0;
        s_st.ip[0] = 0;
    }
}

int watch_net_saved(char ssids[][NET_SSID_MAX + 1], int max)
{
    const int n = s_creds.n < max ? s_creds.n : max;
    for (int i = 0; i < n; i++) {
        snprintf(ssids[i], NET_SSID_MAX + 1, "%s", s_creds.c[i].ssid);
    }
    return n;
}
