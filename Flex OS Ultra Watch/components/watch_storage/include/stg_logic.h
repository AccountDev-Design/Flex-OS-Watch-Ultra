/*
 * stg_logic — lógica PURA del almacenamiento (F4.6; se prueba en el PC):
 * cuándo se permite escribir en la partición "storage" (por encima de 16 MB)
 * y el patrón de la prueba de escritura de un sector.
 *
 * Escribir por encima de 16 MB solo es seguro si las direcciones de 4 bytes
 * llegan de verdad a la mitad alta: si "dieran la vuelta", borrar el primer
 * sector de storage borraría el bootloader (docs/FLASH-32MB.md §6). Por eso
 * se exige el chip esperado, 32 MB declarados y leídos, la partición dentro
 * del chip y el veredicto "sin aliasing" de la sonda de solo lectura de B1.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flash_probe_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STG_JEDEC_GD25Q256 0xC84019u
#define STG_FLASH_32MB     (32u * 1024u * 1024u)
#define STG_SECTOR         4096u

typedef enum {
    STG_GATE_OK = 0,
    STG_GATE_NO_PARTITION,  /**< no hay partición "storage" */
    STG_GATE_CHIP,          /**< otro chip de flash */
    STG_GATE_SIZE,          /**< no son 32 MB declarados y leídos */
    STG_GATE_BOUNDS,        /**< la partición no cabe en el chip */
    STG_GATE_PROBE,         /**< la sonda no dijo "sin aliasing" */
} stg_gate_t;

typedef struct {
    uint32_t jedec_id;
    uint32_t physical_size;
    uint32_t configured_size;
    bool storage_found;
    uint32_t storage_offset;
    uint32_t storage_size;
    flash_probe_verdict_t verdict;
} stg_flash_t;

stg_gate_t stg_gate(const stg_flash_t *f);
const char *stg_gate_text(stg_gate_t g);

/** Desplazamiento (dentro de storage) del sector de prueba: el ÚLTIMO. Si las
 * direcciones dieran la vuelta, caería al final de "assets", no en el
 * bootloader. */
uint32_t stg_test_offset(uint32_t storage_size);
/** Patrón determinista (distinto de 0xFF y de 0x00) para la prueba. */
void stg_pattern(uint8_t *buf, size_t n, uint32_t seed);
uint32_t stg_crc32(const void *data, size_t n);

#ifdef __cplusplus
}
#endif
