/*
 * watch_diag — diagnóstico de arranque: memoria y sonda de flash.
 *
 * La sonda de flash SOLO LEE: identificador JEDEC, tamaño físico detectado,
 * tamaño declarado por el firmware, y la comparación de aliasing descrita en
 * flash_probe_logic.h. No borra ni escribe nada.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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
    uint32_t internal_min;   /**< mínimo histórico de RAM interna libre desde el arranque */
    uint32_t psram_min;      /**< mínimo histórico de PSRAM libre */
    uint32_t alloc_fails;    /**< reservas fallidas (gancho de ESP-IDF) desde el arranque */
    uint32_t last_fail_size; /**< tamaño de la última reserva fallida (0 = ninguna) */
    uint32_t last_fail_caps; /**< capacidades pedidas en esa reserva (MALLOC_CAP_*) */
} watch_mem_report_t;

void watch_diag_flash_probe(watch_flash_report_t *out);
/** Instantánea de memoria con las API de ESP-IDF (no reserva nada). */
void watch_diag_mem(watch_mem_report_t *out);
void watch_diag_log_mem(const char *when);

/**
 * Diagnóstico de memoria (F11): instala el gancho de reservas fallidas de
 * ESP-IDF (heap_caps_register_failed_alloc_callback). Llamar una vez al
 * arrancar. El gancho solo cuenta (sin registrar ni reservar: puede llegar
 * desde cualquier tarea); watch_diag_mem_event lo informa después.
 */
void watch_diag_mem_start(void);
/**
 * Una línea de registro con la memoria y su variación desde el evento
 * anterior ("abrir app", "cerrar app"...): para ver la evolución al abrir y
 * cerrar apps y detectar fugas o fragmentación sin herramientas externas.
 */
void watch_diag_mem_event(const char *what);
/** ¿Cabe ahora una reserva de n bytes en un único bloque de PSRAM? */
bool watch_mem_psram_fits(size_t n);
/** ¿Cabe una reserva de n bytes en RAM interna, dejando min_left libres? */
bool watch_mem_internal_fits(size_t n, size_t min_left);

#ifdef __cplusplus
}
#endif
