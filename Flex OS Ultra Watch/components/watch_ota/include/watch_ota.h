/*
 * watch_ota — actualizaciones del firmware por Wi-Fi (F7): el motor de
 * FlexOS_OTA (ota_core) con la red y la flash de ESP-IDF.
 *
 *  - Una tarea propia hace toda la red y la escritura en flash (reglas de oro
 *    1-6 de la referencia). La interfaz copia instantáneas bajo un cerrojo y
 *    manda órdenes de una palabra; nunca espera a la red.
 *  - HTTPS verificado con el paquete de CA de ESP-IDF; nunca sin verificar.
 *  - Solo usa el Wi-Fi si ya está conectado: no lo enciende. Comprueba 20 s
 *    después de conectar y cada 24 h; instalar lo decide siempre el usuario.
 *  - Escribe en la ranura OTA libre (ota_0/ota_1 de 6 MiB) mientras descarga,
 *    calcula SHA-256 al vuelo y verifica la firma ECDSA P-256 antes de fijar
 *    el arranque. Cualquier fallo deja la ranura en marcha intacta.
 *  - Reversión automática (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): la imagen
 *    nueva arranca "pendiente" y se confirma tras 60 s funcionando; si se
 *    reinicia antes, el cargador de arranque vuelve a la anterior.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ota_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Manifiesto del reloj: el bloque "flex_os_watch_ultra" (docs/F7-OTA.md). */
#ifndef WATCH_OTA_MANIFEST_URL
#define WATCH_OTA_MANIFEST_URL \
    "https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Watch-Ultra/main/ota/manifest.json"
#endif

#define WATCH_OTA_CONFIRM_MS 60000 /* SAFE_STABLE_MS de la referencia */

typedef struct {
    uint8_t state;       /* ota_state_t */
    uint8_t err;         /* ota_err_t */
    bool has_upd;
    uint32_t gen;        /* cambia con lo que se enseña */
    char local[OTA_VER_MAX];
    char ver[OTA_VER_MAX];
    uint32_t size;       /* del manifiesto (0 = no lo dice) */
    char changelog[OTA_LOG_MAX];
    uint32_t total, done, bps;
    uint8_t pct;
    bool have_key;
    char key_fp[17];     /* primeros 16 hex de SHA-256 de la clave pública */
    uint8_t boot;        /* ota_boot_t: aviso tras reiniciar (hasta watch_ota_boot_seen) */
    bool pending_verify; /* la imagen en marcha aún no está confirmada */
} watch_ota_status_t;

esp_err_t watch_ota_start(void);
void watch_ota_get(watch_ota_status_t *out);
/** Texto corto para la fila de Ajustes (flexOtaStatusText). */
void watch_ota_status_text(char *out, size_t n);
void watch_ota_check(void);
void watch_ota_install(void);
void watch_ota_cancel(void);
bool watch_ota_busy(void);
/** El aviso de "actualizado"/"revertido" ya se enseñó. */
void watch_ota_boot_seen(void);

#ifdef __cplusplus
}
#endif
