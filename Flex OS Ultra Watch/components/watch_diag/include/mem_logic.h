/*
 * mem_logic — decisiones y textos de memoria que no tocan el hardware (se
 * prueban en el PC, tests/host/test_media.c).
 *
 * El ESP32-S3 tiene dos memorias muy distintas:
 *  - RAM interna (~300 KB): la comparten el Wi-Fi, el BLE, lwIP, las pilas de
 *    las tareas del sistema y los búferes de la pantalla. Es la que se acaba.
 *  - PSRAM (8 MB): imágenes, listas, búferes grandes... y, con la PSRAM
 *    ejecutando el código (SPIRAM_XIP_FROM_PSRAM), también pilas de tareas.
 * Por eso "sin memoria" con la PSRAM casi vacía casi siempre era la RAM
 * interna: la pila de 8-12 KB de una app no cabía ahí.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RAM interna que una pila de app nunca se come: Wi-Fi, BLE, lwIP y pantalla. */
#define MEM_INTERNAL_KEEP (40u * 1024u)

/**
 * ¿Se intenta primero la RAM interna para una pila de `stack` bytes?
 * Solo si se pide (psram_first = false) y, tras ponerla, aún quedan
 * MEM_INTERNAL_KEEP libres y un bloque donde quepa. Si no, PSRAM primero.
 */
bool mem_stack_internal_first(bool psram_first, size_t stack, size_t int_free, size_t int_block);

/** "512 KB", "6,1 MB": bytes para personas (redondeo hacia abajo). */
void mem_fmt_size(size_t bytes, char *out, size_t n);

/**
 * Explicación con cifras de una reserva que no cupo (nunca "cierra otra
 * app": el usuario no ve apps en segundo plano). `need` es lo que se
 * pidió (0 = desconocido); `psram` dice en qué memoria.
 * Ej.: "Faltan 12 KB de RAM interna (hay 9 KB). PSRAM libre: 6,1 MB."
 */
void mem_why_text(char *out, size_t n, size_t need, bool psram, size_t int_free, size_t ps_free);

#ifdef __cplusplus
}
#endif
