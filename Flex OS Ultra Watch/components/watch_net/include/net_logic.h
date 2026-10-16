/*
 * net_logic — lógica PURA de la conexión Wi-Fi del reloj (sin ESP-IDF, se
 * prueba en el PC con tests/host).
 *
 *  - Redes guardadas: como mucho NET_MAX_SAVED, la más reciente primero;
 *    se serializan con longitud por campo y van dentro del bloque con
 *    versión y CRC de watch_settings (ws_blob_pack): datos dañados no se usan.
 *  - Reintentos: espera exponencial 1, 2, 4... s con tope de 60 s. Una
 *    contraseña incorrecta no se reintenta.
 *  - Motivos de desconexión de ESP-IDF 5.5 (wifi_err_reason_t) en palabras.
 *  - Lista de redes del escaneo: sin nombres vacíos ni repetidos (se queda
 *    la señal más fuerte), las guardadas primero y después por señal.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_MAX_SAVED 4
#define NET_SSID_MAX  32
#define NET_PASS_MAX  64
#define NET_SCAN_MAX  16

typedef struct {
    char ssid[NET_SSID_MAX + 1];
    char pass[NET_PASS_MAX + 1]; /**< "" = red abierta */
} net_cred_t;

typedef struct {
    int n;
    net_cred_t c[NET_MAX_SAVED];
} net_creds_t;

typedef struct {
    char ssid[NET_SSID_MAX + 1];
    int8_t rssi;
    bool secure; /**< pide contraseña */
    bool saved;  /**< está en las redes guardadas */
} net_ap_t;

/* ---- Redes guardadas ---- */
void net_creds_init(net_creds_t *c);
/** Guarda o actualiza ssid y la pone la primera. false si los textos no caben. */
bool net_creds_put(net_creds_t *c, const char *ssid, const char *pass);
bool net_creds_remove(net_creds_t *c, const char *ssid);
/** Índice de ssid o -1. */
int net_creds_find(const net_creds_t *c, const char *ssid);
/** Serializa (sin cabecera). Devuelve los bytes o 0 si no cabe en cap. */
size_t net_creds_encode(const net_creds_t *c, uint8_t *out, size_t cap);
/** false si los datos no son válidos (entonces *c queda vacío). */
bool net_creds_decode(net_creds_t *c, const uint8_t *in, size_t n);

/* ---- Reintentos y motivos ---- */
/** Espera antes del reintento attempt (0 = el primero): 1 s, 2 s... tope 60 s. */
uint32_t net_backoff_ms(int attempt);
/** ¿El motivo es de contraseña o autenticación (no tiene sentido reintentar)? */
bool net_reason_is_auth(int reason);
/** Motivo en palabras ("Contraseña incorrecta", "Red no encontrada"...). */
const char *net_reason_text(int reason);

/* ---- Señal y escaneo ---- */
/** Barras de 0 a 4 según el RSSI (dBm). */
int net_rssi_bars(int rssi);
/**
 * Limpia y ordena n redes en su sitio: quita nombres vacíos y repetidos
 * (deja la más fuerte), marca las guardadas y las pone primero; dentro de
 * cada grupo, por señal. Devuelve cuántas quedan.
 */
int net_scan_sort(net_ap_t *aps, int n, const net_creds_t *saved);
/** Índice (en aps ordenadas) de la mejor red guardada, o -1. */
int net_pick_saved(const net_ap_t *aps, int n);

#ifdef __cplusplus
}
#endif
