/*
 * flash_probe_logic — veredicto PURO de la sonda de flash > 16 MB (se prueba
 * en el PC).
 *
 * Idea: si el direccionamiento de 4 bytes NO funcionara, leer en A + 16 MB
 * devolvería en realidad el contenido de A (la dirección "da la vuelta").
 * Se comparan varias ventanas conocidas de la zona baja (bootloader, tabla de
 * particiones, cabecera de la app) con su gemela 16 MB más arriba:
 *   - todas iguales   -> ALIASED (inseguro escribir por encima de 16 MB)
 *   - todas distintas -> UPPER_DISTINCT (la lectura alcanza otra zona física)
 *   - mezcla          -> INCONCLUSIVE
 * Falla del lado seguro: si alguien hubiera grabado una copia idéntica en la
 * zona alta, saldría ALIASED aunque el hardware funcionara.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLASH_PROBE_NOT_RUN = 0,
    FLASH_PROBE_NOT_CONFIGURED, /**< el firmware no declara más de 16 MB */
    FLASH_PROBE_READ_ERROR,
    FLASH_PROBE_ALIASED,
    FLASH_PROBE_INCONCLUSIVE,
    FLASH_PROBE_UPPER_DISTINCT,
} flash_probe_verdict_t;

flash_probe_verdict_t flash_probe_classify(int n, const bool read_ok[], const bool equal[]);
const char *flash_probe_verdict_name(flash_probe_verdict_t v);

#ifdef __cplusplus
}
#endif
