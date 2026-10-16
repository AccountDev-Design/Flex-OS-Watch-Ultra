/*
 * stg_logic — ver include/stg_logic.h
 */
#include "stg_logic.h"

stg_gate_t stg_gate(const stg_flash_t *f)
{
    if (!f->storage_found || f->storage_size < STG_SECTOR) {
        return STG_GATE_NO_PARTITION;
    }
    if (f->jedec_id != STG_JEDEC_GD25Q256) {
        return STG_GATE_CHIP;
    }
    if (f->configured_size != STG_FLASH_32MB || f->physical_size != STG_FLASH_32MB) {
        return STG_GATE_SIZE;
    }
    if ((uint64_t)f->storage_offset + f->storage_size > f->physical_size || f->storage_offset % STG_SECTOR ||
        f->storage_size % STG_SECTOR) {
        return STG_GATE_BOUNDS;
    }
    if (f->verdict != FLASH_PROBE_UPPER_DISTINCT) {
        return STG_GATE_PROBE;
    }
    return STG_GATE_OK;
}

const char *stg_gate_text(stg_gate_t g)
{
    switch (g) {
    case STG_GATE_OK:           return "Permitido";
    case STG_GATE_NO_PARTITION: return "Sin partición storage";
    case STG_GATE_CHIP:         return "Chip de flash no esperado";
    case STG_GATE_SIZE:         return "La flash no son 32 MB";
    case STG_GATE_BOUNDS:       return "Partición fuera del chip";
    case STG_GATE_PROBE:        return "Sonda de 32 MB no superada";
    default:                    return "?";
    }
}

uint32_t stg_test_offset(uint32_t storage_size)
{
    return storage_size >= STG_SECTOR ? (storage_size / STG_SECTOR - 1) * STG_SECTOR : 0;
}

void stg_pattern(uint8_t *buf, size_t n, uint32_t seed)
{
    uint32_t x = seed ? seed : 0x5A17F00Du;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        uint8_t b = (uint8_t)(x >> 24);
        if (b == 0xFF || b == 0x00) {
            b = (uint8_t)(0x5A + (i % 64)); /* nunca 0x00 ni 0xFF */
        }
        buf[i] = b;
    }
}

uint32_t stg_crc32(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}
