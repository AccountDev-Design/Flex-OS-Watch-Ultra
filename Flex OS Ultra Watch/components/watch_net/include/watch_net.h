/*
 * watch_net — Wi-Fi (estación) del reloj: F5.1.
 *
 *  - El Wi-Fi está APAGADO hasta que el usuario lo enciende; el estado
 *    encendido/apagado y las redes guardadas sobreviven al reinicio (NVS,
 *    espacio "wifi").
 *  - Encendido, busca las redes y se conecta solo a la mejor guardada; si se
 *    cae, reintenta con espera exponencial (1 s ... 60 s). Una contraseña
 *    incorrecta no se reintenta.
 *  - Una red nueva solo se guarda cuando la conexión funciona.
 *  - Con conexión, pide la hora por SNTP y pone en hora el RTC.
 *  - Todo el trabajo lo hace una tarea propia; los eventos de ESP-IDF solo le
 *    envían mensajes. La interfaz lee instantáneas (watch_net_get) desde su
 *    temporizador: ninguna tarea de red toca LVGL.
 * Independiente del BLE: perder Internet no afecta al enlace con el teléfono.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "net_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_OFF = 0,    /**< radio apagada */
    NET_IDLE,       /**< encendido, sin red (ninguna guardada a la vista) */
    NET_SCANNING,   /**< buscando redes */
    NET_CONNECTING, /**< conectando a ssid */
    NET_CONNECTED,  /**< con IP */
    NET_FAILED,     /**< falló; reason dice por qué (y retry_in_s si se reintentará) */
} net_state_t;

typedef struct {
    net_state_t state;
    bool enabled;
    char ssid[NET_SSID_MAX + 1]; /**< red actual o a la que se intenta */
    char ip[16];
    int8_t rssi;
    int reason;       /**< último motivo de desconexión (0 = ninguno) */
    bool auth_failed; /**< contraseña incorrecta: no se reintenta */
    bool time_synced; /**< SNTP puso el RTC en hora en esta sesión */
    uint32_t scan_gen;/**< cambia con cada escaneo terminado */
    int retry_in_s;   /**< segundos hasta el próximo intento (0 = ninguno) */
} watch_net_status_t;

/** Arranca el servicio (sin encender la radio salvo que estuviera encendida). */
esp_err_t watch_net_start(void);
/** Instantánea del estado (en cualquier tarea). */
void watch_net_get(watch_net_status_t *out);
void watch_net_set_enabled(bool on);
/** Pide un escaneo (si está encendido). */
void watch_net_scan(void);
/** Copia las redes del último escaneo, ordenadas. Devuelve cuántas. */
int watch_net_scan_results(net_ap_t *out, int max);
/** Conecta a ssid; pass NULL = la guardada (o red abierta). */
void watch_net_connect(const char *ssid, const char *pass);
void watch_net_forget(const char *ssid);
/** Nombres de las redes guardadas (la más reciente primero). Devuelve cuántas. */
int watch_net_saved(char ssids[][NET_SSID_MAX + 1], int max);

/** Texto del estado para la interfaz ("Conectado", "Buscando redes"...). */
const char *watch_net_state_text(const watch_net_status_t *s);

#ifdef __cplusplus
}
#endif
