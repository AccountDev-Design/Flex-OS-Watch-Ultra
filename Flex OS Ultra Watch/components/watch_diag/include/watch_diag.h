/*
 * watch_diag — diagnóstico de arranque: memoria y sonda de flash.
 *
 * La sonda de flash SOLO LEE: identificador JEDEC, tamaño físico detectado,
 * tamaño declarado por el firmware, y la comparación de aliasing descrita en
 * flash_probe_logic.h. No borra ni escribe nada.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "flash_probe_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t jedec_id;          /**< p. ej. 0xC84019 = GigaDevice GD25Q256 */
    uint32_t physical_size;     /**< tamaño leído del chip (SFDP/ID), bytes */
    uint32_t configured_size;   /**< tamaño declarado en la cabecera del firmware */
    bool     storage_found;     /**< partición "storage" presente */
    uint32_t storage_offset;
    uint32_t storage_size;
    flash_probe_verdict_t verdict;
} watch_flash_report_t;

typedef struct {
    uint32_t internal_free;
    uint32_t internal_largest;
    uint32_t dma_free;
    uint32_t psram_total;
    uint32_t psram_free;
    uint32_t psram_largest;
} watch_mem_report_t;

void watch_diag_flash_probe(watch_flash_report_t *out);
void watch_diag_mem(watch_mem_report_t *out);
void watch_diag_log_mem(const char *when);

#ifdef __cplusplus
}
#endif
