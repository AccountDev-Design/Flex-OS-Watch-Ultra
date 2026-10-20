/*
 * watch_phone — enlace con la app Flex Phone del teléfono (F5.2/F5.3/F5.5).
 *
 * Una tarea es dueña del enlace Flex Link v2 (components/flexlink, port de
 * Flex OS Ultra) y del modelo de notificaciones/estado del teléfono. El
 * transporte es Bluetooth LE con el reloj como central y el teléfono como
 * periférico GATT (GattServer.kt): ver docs/F5-CONECTIVIDAD.md.
 *
 * La interfaz LEE por instantáneas (watch_phone_get, watch_phone_notifs) y
 * PIDE por funciones que solo encolan: nada de aquí bloquea a LVGL.
 *
 * Lo que este servicio NO hace y la interfaz debe decir:
 *  - El Wi-Fi no interviene: perder Internet no corta el enlace.
 *  - Flex Phone (FlexPhone/ en este repositorio) sirve v2 por BLE
 *    (GattServer.kt + PhoneLinkPeer.kt). Con una app antigua sin v2, el reloj
 *    conecta por BLE y explica que el teléfono no contesta al saludo.
 *  - El Browser Relay del teléfono se pide por aquí (watch_phone_relay) pero
 *    sus imágenes van por el Wi-Fi (watch_browser, br_backend).
 *  - La carga viaja cifrada por el emparejamiento BLE (las características
 *    del teléfono exigen cifrado) y autenticada por v2; no hay más capas.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "flex_phone.h"
#include "flex_phone_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Máximo de notificaciones que la interfaz copia de una vez. */
#define WATCH_PHONE_NOTIF_MAX FLP_NOTIF_MAX

typedef struct {
    uint8_t link;                      /**< FLP_LS_* del enlace */
    uint8_t chan;                      /**< FLP_TC_* del canal BLE */
    bool enabled;                      /**< el usuario lo quiere encendido (se guarda) */
    bool bonded;                       /**< hay vínculo v2 guardado */
    char peer_name[FLP_DEVNAME_MAX];   /**< teléfono vinculado o con el que se habla */
    char err[FLP_LINK_ERR_MAX];        /**< motivo REAL del último fallo ("" si no hay) */
    char chan_text[96];                /**< qué hace el canal BLE, en palabras */
    char code[FLP_LINK_CODE_LEN + 1];  /**< código de emparejamiento vivo ("" si no hay) */
    uint32_t pair_left_ms;             /**< vida que le queda al código */
    bool pair_user_ok;                 /**< ya se pulsó "Confirmar" en el reloj */
    bool pair_peer_ok;                 /**< el teléfono ya demostró el código */
    uint16_t rtt_ms;                   /**< latencia medida con PING/PONG; 0xFFFF = sin medir */
    FlexPhoneState phone;              /**< batería, red... (valid = llegó de verdad) */
    FlexPhoneCaps caps;                /**< qué concede el teléfono (valid = llegó) */
    FlexPhoneMedia media;              /**< lo que suena (valid = hay título) */
    uint16_t notif_count;
    uint32_t notif_gen;                /**< cambia con cada alta, baja o cambio */
    uint32_t put_id;                   /**< última notificación que entró o cambió */
    uint32_t put_seq;                  /**< cuántas han entrado o cambiado (para el aviso) */
    uint32_t replies_ok, replies_fail; /**< resultados REALES de las respuestas */
    FlexPhoneRelay relay;              /**< Browser Relay del teléfono (state FLP_RELAY_*) */
    bool relay_supported;              /**< el teléfono declara el servidor del navegador */
} watch_phone_status_t;

/** Arranca el servicio. Enciende el enlace si el usuario lo dejó encendido. */
esp_err_t watch_phone_start(void);

/** Instantánea del estado (en cualquier tarea). */
void watch_phone_get(watch_phone_status_t *out);
/** Copia las notificaciones, la más reciente primero. Devuelve cuántas. */
int watch_phone_notifs(FlexPhoneNotif *out, int max);
/** Copia una notificación por id. false si ya no existe. */
bool watch_phone_notif(uint32_t id, FlexPhoneNotif *out);
/** Texto del cuerpo que se puede enseñar (privacidad del modelo). false = nada. */
bool watch_phone_notif_body(const FlexPhoneNotif *n, bool on_lock, char *out, size_t outN);

void watch_phone_set_enabled(bool on);
/** Abre una sesión de emparejamiento: genera el código que se enseña. */
void watch_phone_pair_begin(void);
/** El usuario confirma en el reloj el código que ve. */
void watch_phone_pair_confirm(void);
void watch_phone_pair_cancel(void);
/** Olvida el vínculo v2 y el emparejamiento Bluetooth. */
void watch_phone_forget(void);

/** Quita una notificación del reloj y, con sesión, también del teléfono. */
void watch_phone_dismiss(uint32_t id);
/** Borra el historial del reloj (no toca el teléfono). */
void watch_phone_clear_all(void);
/**
 * Responde a una notificación. Devuelve FLNK_E_NONE si se encoló, o el
 * motivo por el que no se puede (sin sesión: FLNK_E_NOTPAIRED). El
 * resultado real llega después en replies_ok/replies_fail.
 */
uint8_t watch_phone_reply(uint32_t id, const char *text);
/** Dispara la acción idx (etiqueta actions[idx]) de una notificación. */
bool watch_phone_action(uint32_t id, uint8_t idx);
/** Hacer sonar el teléfono (start) o pararlo. false si no hay sesión o no se concede. */
bool watch_phone_find(bool start);
/** Orden multimedia FLP_MCMD_*. false si no hay sesión o no se concede. */
bool watch_phone_media(uint8_t cmd);

/**
 * Pide al teléfono que levante (start) o pare su Browser Relay
 * (FLNK_T_RELAY_START/STOP con ACK). La respuesta real llega en
 * status.relay (FLNK_T_RELAY_INFO). false si no hay sesión o el teléfono no
 * lo ofrece.
 */
bool watch_phone_relay(bool start);
/**
 * Credencial del relay derivada del vínculo (32 hex). No viaja por el aire:
 * el teléfono deriva la misma. false (y out vacío) sin vínculo. outN >= 33.
 */
bool watch_phone_relay_token(char *out, size_t outN);

/** Estado del enlace en palabras para la interfaz ("Conectado", "Buscando el teléfono"...). */
const char *watch_phone_state_text(const watch_phone_status_t *s);

#ifdef __cplusplus
}
#endif
