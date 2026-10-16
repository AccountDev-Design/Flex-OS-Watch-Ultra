/*
 * net_logic — ver include/net_logic.h
 */
#include "net_logic.h"

#include <string.h>

void net_creds_init(net_creds_t *c)
{
    memset(c, 0, sizeof(*c));
}

int net_creds_find(const net_creds_t *c, const char *ssid)
{
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->c[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

bool net_creds_put(net_creds_t *c, const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > NET_SSID_MAX || (pass && strlen(pass) > NET_PASS_MAX)) {
        return false;
    }
    net_cred_t e;
    memset(&e, 0, sizeof(e));
    strcpy(e.ssid, ssid);
    strcpy(e.pass, pass ? pass : "");
    int i = net_creds_find(c, ssid);
    if (i < 0) {
        i = c->n < NET_MAX_SAVED ? c->n++ : NET_MAX_SAVED - 1; /* lleno: sale la más antigua */
    }
    memmove(&c->c[1], &c->c[0], (size_t)i * sizeof(net_cred_t));
    c->c[0] = e;
    return true;
}

bool net_creds_remove(net_creds_t *c, const char *ssid)
{
    const int i = net_creds_find(c, ssid);
    if (i < 0) {
        return false;
    }
    memmove(&c->c[i], &c->c[i + 1], (size_t)(c->n - i - 1) * sizeof(net_cred_t));
    c->n--;
    memset(&c->c[c->n], 0, sizeof(net_cred_t));
    return true;
}

size_t net_creds_encode(const net_creds_t *c, uint8_t *out, size_t cap)
{
    size_t k = 0;
    if (cap < 1 || c->n < 0 || c->n > NET_MAX_SAVED) {
        return 0;
    }
    out[k++] = (uint8_t)c->n;
    for (int i = 0; i < c->n; i++) {
        const size_t ls = strlen(c->c[i].ssid), lp = strlen(c->c[i].pass);
        if (k + 2 + ls + lp > cap) {
            return 0;
        }
        out[k++] = (uint8_t)ls;
        memcpy(out + k, c->c[i].ssid, ls);
        k += ls;
        out[k++] = (uint8_t)lp;
        memcpy(out + k, c->c[i].pass, lp);
        k += lp;
    }
    return k;
}

bool net_creds_decode(net_creds_t *c, const uint8_t *in, size_t n)
{
    net_creds_init(c);
    if (!in || n < 1 || in[0] > NET_MAX_SAVED) {
        return false;
    }
    size_t k = 1;
    net_creds_t t;
    net_creds_init(&t);
    for (int i = 0; i < in[0]; i++) {
        if (k >= n) {
            return false;
        }
        const size_t ls = in[k++];
        if (ls == 0 || ls > NET_SSID_MAX || k + ls >= n) {
            return false;
        }
        memcpy(t.c[i].ssid, in + k, ls);
        k += ls;
        const size_t lp = in[k++];
        if (lp > NET_PASS_MAX || k + lp > n) {
            return false;
        }
        memcpy(t.c[i].pass, in + k, lp);
        k += lp;
        if (memchr(t.c[i].ssid, 0, ls) || memchr(t.c[i].pass, 0, lp)) {
            return false; /* un NUL dentro: dato dañado */
        }
        t.n++;
    }
    if (k != n) {
        return false;
    }
    *c = t;
    return true;
}

uint32_t net_backoff_ms(int attempt)
{
    if (attempt < 0) {
        attempt = 0;
    }
    if (attempt >= 6) {
        return 60000; /* 1, 2, 4, 8, 16, 32 s y después 60 s */
    }
    return 1000u << attempt;
}

/* Códigos de wifi_err_reason_t (esp_wifi_types_generic.h de ESP-IDF 5.5.5) */
#define R_AUTH_EXPIRE        2
#define R_ASSOC_LEAVE        8
#define R_MIC_FAILURE        14
#define R_4WAY_TIMEOUT       15
#define R_BEACON_TIMEOUT     200
#define R_NO_AP_FOUND        201
#define R_AUTH_FAIL          202
#define R_ASSOC_FAIL         203
#define R_HANDSHAKE_TIMEOUT  204
#define R_CONNECTION_FAIL    205
#define R_NO_AP_SECURITY     210
#define R_NO_AP_AUTHMODE     211
#define R_NO_AP_RSSI         212

bool net_reason_is_auth(int r)
{
    return r == R_AUTH_FAIL || r == R_4WAY_TIMEOUT || r == R_HANDSHAKE_TIMEOUT || r == R_MIC_FAILURE ||
           r == R_AUTH_EXPIRE;
}

const char *net_reason_text(int r)
{
    if (r == 0) {
        return "";
    }
    if (net_reason_is_auth(r)) {
        return "Contraseña incorrecta";
    }
    switch (r) {
    case R_NO_AP_FOUND:
    case R_NO_AP_RSSI:
        return "Red no encontrada";
    case R_NO_AP_SECURITY:
    case R_NO_AP_AUTHMODE:
        return "Seguridad de la red no compatible";
    case R_ASSOC_FAIL:
    case R_CONNECTION_FAIL:
        return "La red rechazó la conexión";
    case R_BEACON_TIMEOUT:
        return "Señal perdida";
    case R_ASSOC_LEAVE:
        return "Desconectado";
    default:
        return "Error de conexión";
    }
}

int net_rssi_bars(int rssi)
{
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}

static int ap_cmp(const net_ap_t *a, const net_ap_t *b)
{
    if (a->saved != b->saved) {
        return a->saved ? -1 : 1;
    }
    return (int)b->rssi - (int)a->rssi;
}

int net_scan_sort(net_ap_t *aps, int n, const net_creds_t *saved)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!aps[i].ssid[0]) {
            continue; /* red oculta */
        }
        int dup = -1;
        for (int j = 0; j < m; j++) {
            if (strcmp(aps[j].ssid, aps[i].ssid) == 0) {
                dup = j;
                break;
            }
        }
        if (dup >= 0) {
            if (aps[i].rssi > aps[dup].rssi) {
                aps[dup].rssi = aps[i].rssi;
            }
            continue;
        }
        aps[m] = aps[i];
        aps[m].saved = saved && net_creds_find(saved, aps[m].ssid) >= 0;
        m++;
    }
    /* inserción: n pequeño (<= NET_SCAN_MAX) y orden estable */
    for (int i = 1; i < m; i++) {
        net_ap_t t = aps[i];
        int j = i - 1;
        while (j >= 0 && ap_cmp(&aps[j], &t) > 0) {
            aps[j + 1] = aps[j];
            j--;
        }
        aps[j + 1] = t;
    }
    return m;
}

int net_pick_saved(const net_ap_t *aps, int n)
{
    return (n > 0 && aps[0].saved) ? 0 : -1;
}
