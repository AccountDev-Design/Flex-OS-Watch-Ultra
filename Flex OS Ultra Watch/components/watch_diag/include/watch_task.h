/*
 * watch_task — tareas de app con la pila donde quepa (ver mem_logic.h).
 * Aparte de watch_diag.h para que el simulador (sin FreeRTOS) no lo vea.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Crea una tarea de app con la pila donde quepa (mem_logic.h): en PSRAM si
 * psram_first, o en RAM interna si sobra (si no, en PSRAM). Si la primera
 * memoria falla, prueba la otra: una app no deja de abrir porque la RAM
 * interna esté justa. La tarea termina con watch_task_exit(), nunca con
 * vTaskDelete(NULL) (su memoria la suelta ESP-IDF). core: tskNO_AFFINITY o
 * un núcleo. Devuelve false si no cabe en ninguna; why (si no es NULL)
 * explica con cifras qué faltó.
 */
bool watch_task_spawn(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                      BaseType_t core, bool psram_first, TaskHandle_t *out, char *why, size_t why_n);
/** Fin de una tarea creada con watch_task_spawn (no vuelve). */
void watch_task_exit(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
