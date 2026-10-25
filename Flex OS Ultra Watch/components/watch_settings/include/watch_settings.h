/*
 * watch_settings — ajustes persistentes en NVS con un ÚNICO escritor (F4.1).
 *
 * - Los ajustes viven en RAM. Quien los lee recibe una copia
 *   (watch_settings_get). Quien los cambia entrega la copia modificada
 *   (watch_settings_update), y el cambio se aplica al momento en RAM.
 * - Solo la tarea "watch_settings" escribe en NVS. Guarda 1,5 s después del
 *   último cambio y nunca más de 30 s después del primero (un deslizador no
 *   escribe en cada paso). Solo escribe las claves que cambiaron.
 * - watch_settings_flush() guarda ya. Es para antes de apagar la pantalla o
 *   el reloj.
 * - Si NVS no está disponible, los ajustes funcionan en RAM y
 *   watch_settings_persistent() lo dice. La interfaz avisa de que no se
 *   guardan; nunca finge que se guardaron.
 * - Bloques binarios (alarmas) con versión y CRC: datos dañados no se usan.
 *
 * Espacio "flexws" de la partición nvs (0x9000, 20 KB; partitions.csv sin
 * cambios).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ws_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef ws_settings_t watch_settings_t;

/** Bloques binarios guardados junto a los ajustes. */
typedef enum {
    WS_BLOB_ALARMS = 0,
    WS_BLOB_WELLNESS,   /**< Bienestar: ajustes, objetivos y datos personales (act_config) */
    WS_BLOB_WB_STATE,   /**< Bienestar: día en curso y sesión, solo si no hay LittleFS */
    WS_BLOB_COUNT,
} ws_blob_id_t;

#define WS_BLOB_MAX 256 /* bytes de datos por bloque */

typedef struct {
    bool nvs_ok;        /**< NVS iniciado: los cambios se guardan */
    bool loaded;        /**< se leyeron ajustes guardados (no es el primer arranque) */
    uint32_t commits;   /**< escrituras completadas desde el arranque */
    uint32_t errors;    /**< escrituras fallidas */
    esp_err_t last_err; /**< último error de NVS */
    bool pending;       /**< hay cambios sin guardar */
} watch_settings_stats_t;

/** Inicia NVS, lee los ajustes (o los valores por defecto) y arranca la tarea. */
esp_err_t watch_settings_start(void);

/** Copia de los ajustes actuales (valores por defecto si aún no se inició). */
void watch_settings_get(watch_settings_t *out);

/** Aplica una copia modificada: se normaliza, cambia ya en RAM y se guarda
 * después. Sin cambios reales no hace nada. */
void watch_settings_update(const watch_settings_t *s);

/** Cambia cada vez que cambian los ajustes (para refrescar la interfaz). */
uint32_t watch_settings_generation(void);

/** Guarda ya lo pendiente y espera hasta timeout_ms. */
esp_err_t watch_settings_flush(uint32_t timeout_ms);

bool watch_settings_persistent(void);
void watch_settings_stats(watch_settings_stats_t *out);

/** Bloque guardado (len: capacidad de entrada, bytes leídos a la salida).
 * ESP_ERR_NOT_FOUND si no hay bloque válido. */
esp_err_t watch_settings_blob_get(ws_blob_id_t id, uint8_t *ver, void *buf, size_t *len);
esp_err_t watch_settings_blob_set(ws_blob_id_t id, uint8_t ver, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif
