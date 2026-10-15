/*
 * flash_probe_logic — ver include/flash_probe_logic.h
 */
#include "flash_probe_logic.h"

flash_probe_verdict_t flash_probe_classify(int n, const bool read_ok[], const bool equal[])
{
    if (n <= 0) {
        return FLASH_PROBE_NOT_RUN;
    }
    int eq = 0;
    for (int i = 0; i < n; i++) {
        if (!read_ok[i]) {
            return FLASH_PROBE_READ_ERROR;
        }
        if (equal[i]) {
            eq++;
        }
    }
    if (eq == n) {
        return FLASH_PROBE_ALIASED;
    }
    if (eq == 0) {
        return FLASH_PROBE_UPPER_DISTINCT;
    }
    return FLASH_PROBE_INCONCLUSIVE;
}

const char *flash_probe_verdict_name(flash_probe_verdict_t v)
{
    switch (v) {
    case FLASH_PROBE_NOT_CONFIGURED: return "no configurada (>16 MB no declarado)";
    case FLASH_PROBE_READ_ERROR:     return "error de lectura";
    case FLASH_PROBE_ALIASED:        return "ALIASING: >16 MB repite la zona baja";
    case FLASH_PROBE_INCONCLUSIVE:   return "no concluyente";
    case FLASH_PROBE_UPPER_DISTINCT: return "lectura >16 MB distinta (sin aliasing)";
    default:                         return "no ejecutada";
    }
}
