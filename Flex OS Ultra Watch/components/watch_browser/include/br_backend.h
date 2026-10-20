/*
 * br_backend — a qué servicio FBP/1 se conecta el navegador del reloj.
 *
 * Tres fuentes, las mismas que el P4 (br_logic.h, BRSRC_*):
 *  - Flex Phone: el Browser Relay del teléfono emparejado. El reloj se lo
 *    pide por BLE (FLNK_T_RELAY_START), el teléfono contesta su ip:puerto
 *    (FLNK_T_RELAY_INFO) y el navegador abre ws://ip:puerto/v1/session por
 *    el Wi-Fi con la credencial derivada del vínculo (nunca viaja).
 *  - Nube: cloudServer de Ajustes (wss://) con la credencial de Ajustes.
 *  - PC/Ubuntu manual: server de Ajustes, como siempre.
 *
 * Es lógica pura (sin ESP-IDF ni LVGL): la usan el firmware, la vista previa
 * y las pruebas de PC.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "br_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Lo que el navegador necesita saber del enlace con el teléfono. */
typedef struct {
    bool link_ready;       /**< sesión Flex Link v2 abierta */
    bool relay_supported;  /**< el teléfono declara el servidor (o aún no dijo sus capacidades) */
    uint8_t relay_state;   /**< FLP_RELAY_* */
    bool relay_by_user;    /**< parado a propósito por la persona */
    uint8_t ip[4];
    uint16_t port;
    bool tls;
    char relay_err[64];
    char token[33];        /**< credencial derivada del vínculo ("" sin vínculo) */
} br_phone_view_t;

typedef struct {
    int src;                         /**< BRSRC_PHONE/CLOUD/MANUAL o BRSRC_NONE */
    char url[FLEXBR_SERVER_MAX];     /**< "" con BRSRC_NONE */
    char token[FLEXBR_TOKEN_MAX];
    bool insecure_ok;                /**< ws:// permitido (el relay va por la red local) */
    bool want_relay;                 /**< conviene pedir RELAY_START al teléfono */
    char why[96];                    /**< por qué no hay fuente, o qué se espera ("" si nada) */
} br_backend_t;

/** Decide la fuente y su dirección. Nunca devuelve una URL inventada. */
void br_backend_resolve(const BrSettings *st, const br_phone_view_t *ph, br_backend_t *out);
/** ¿Cambió algo que obligue a reconfigurar la sesión? */
bool br_backend_differs(const br_backend_t *a, const br_backend_t *b);

#ifdef __cplusplus
}
#endif
