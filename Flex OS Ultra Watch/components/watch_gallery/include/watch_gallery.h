/*
 * watch_gallery — la Galería en el reloj (ESP-IDF): la tarea del motor
 * (gal_engine), el transporte TCP/TLS de esp_transport (TLS verificado con el
 * paquete de CA de ESP-IDF), la NVS y la PSRAM.
 *
 *  - La tarea "watch_gal" nace al abrir la app y termina sola cuando la app
 *    está cerrada y no quedan transferencias; entonces suelta el motor y toda
 *    su PSRAM. Ninguna tarea de red toca LVGL: la interfaz lee instantáneas.
 *  - NVS: "flexacct" (Flex Account del reloj, las mismas claves que el P4) y
 *    "flexgal" (origen elegido y dirección del P4). La sesión del P4 (cookie
 *    "fxs") vive solo en RAM: caduca con el P4 y no se guarda.
 *  - Antes de reservar lo grande se mira la PSRAM libre y su mayor bloque (y
 *    la RAM interna que necesita un saludo TLS).
 *  - El registro (ESP_LOGI) dice la memoria y la pila MEDIDAS al abrir y al
 *    cerrar, para la prueba física (docs/GALERIA.md §10, T151-T166).
 */
#pragma once

#include <stdbool.h>
#include "gal_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** La app se abre: crea el motor y la tarea si hace falta. NULL si no hay memoria. */
gal_engine_t *watch_gallery_open(void);
/** La app se cierra o se suspende: la tarea sigue solo mientras haya transferencias. */
void watch_gallery_close(void);
/** El motor, mientras exista (con la app abierta, siempre). */
gal_engine_t *watch_gallery_engine(void);

#ifdef __cplusplus
}
#endif
