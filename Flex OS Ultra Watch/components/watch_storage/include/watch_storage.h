/*
 * watch_storage — LittleFS en la partición "storage" (16 MB por encima de
 * 16 MB; F4.6). Una tarea propia hace todas las operaciones de flash.
 *
 * Nada se escribe si la puerta (stg_gate) no lo permite. Con la puerta
 * abierta:
 *  - Al arrancar solo se intenta MONTAR (sin formatear nunca a escondidas).
 *    Con los datos de fábrica en esa zona el montaje falla: "sin formato".
 *  - Prueba de escritura de un sector (a petición, antes de formatear):
 *    borra, escribe y relee el último sector de storage y comprueba que ni
 *    el bootloader (0x0) ni la posición 16 MB más abajo cambiaron.
 *  - Formatear (a petición explícita y confirmada, solo tras pasar la prueba
 *    del sector): borra los datos de fábrica de esa zona.
 *  - Prueba de archivo: escritura atómica (temporal + fsync + rename) de
 *    /storage/flexos/prueba.txt con un contador que sobrevive al reinicio.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "stg_logic.h"
#include "stg_usage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_STORAGE_BASE "/storage"

typedef enum {
    STG_LOCKED = 0,   /**< la puerta no permite escribir: no se monta */
    STG_UNFORMATTED,  /**< el montaje falló (datos de fábrica o vacío) */
    STG_MOUNTED,
    STG_BUSY,         /**< operación en curso */
    STG_ERROR,
} stg_state_t;

/** STG_REQ_USAGE: tamaños por categoría (stg_usage), solo lectura y sin
 * pasar a STG_BUSY: las apps siguen usando el disco mientras tanto. */
typedef enum { STG_REQ_SECTOR_TEST = 1, STG_REQ_FORMAT, STG_REQ_FILE_TEST, STG_REQ_USAGE } stg_req_t;

typedef struct {
    stg_state_t state;
    stg_gate_t gate;
    size_t total;         /**< bytes (montado) */
    size_t used;
    int sector_test;      /**< 0 sin hacer, 1 bien, -1 mal */
    int file_test;        /**< 0 sin hacer, 1 bien, -1 mal */
    uint32_t file_count;  /**< contador del archivo de prueba (persistente) */
    esp_err_t last_err;
    const char *detail;   /**< texto de la última operación */
    stg_usage_t usage;    /**< tamaños por categoría (válidos si usage_gen > 0) */
    uint32_t usage_gen;   /**< cambia con cada recuento terminado */
} watch_storage_info_t;

/** Arranca la tarea y, si la puerta lo permite, intenta montar. */
esp_err_t watch_storage_start(const stg_flash_t *flash);
void watch_storage_info(watch_storage_info_t *out);
/** Pide una operación (vuelve enseguida; el resultado queda en info). */
esp_err_t watch_storage_request(stg_req_t req);

#ifdef __cplusplus
}
#endif
