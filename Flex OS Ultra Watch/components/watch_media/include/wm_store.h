/*
 * wm_store — la biblioteca de medios del reloj: un catálogo FlexMediaStore
 * (el del P4, components/flexmedia) por volumen, y la tarea que lo mantiene
 * al día. Es la ÚNICA lista de fotos, vídeos, música y grabaciones: la leen
 * la Galería, Música, la grabadora y el Flex Web Server.
 *
 * Quién toca qué (igual que en Flex OS Ultra, FlexOS_Ultra_MediaLib.h):
 *  - la tarea "wm_media" carga el catálogo, lo reconcilia con el disco, hace
 *    las miniaturas persistentes (JPEG de 132 px en /System/Media/th) y lo
 *    guarda agrupando cambios;
 *  - la interfaz solo lee instantáneas (wm_lib_snapshot): nunca hace E/S de
 *    disco ni espera a la tarea;
 *  - la grabadora y el servidor publican archivos ya terminados
 *    (wm_lib_add_file) desde sus propias tareas.
 *
 * El catálogo de la microSD existe solo mientras está montada: al pasar a
 * REMOVING se guarda (si la tarjeta sigue ahí), se libera y se suelta su
 * arrendamiento. Sus registros viven en PSRAM: FML_CAP x 336 B = 126 KB por
 * volumen (docs/MULTIMEDIA.md §11, presupuestos).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_MediaLib.h"
#include "FlexOS_MediaStore.h"
#include "watch_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Arranca la tarea de la biblioteca (después de watch_storage y watch_media). */
esp_err_t wm_lib_start(void);

/** ¿Hay catálogo de ese volumen? (la microSD, solo montada). */
bool wm_lib_ready(wm_vol_t v);
/** Revisión: cambia con cualquier cambio visible (0 = sin catálogo). */
uint32_t wm_lib_rev(wm_vol_t v);
/** ¿Está reconciliando o con miniaturas pendientes? */
bool wm_lib_busy(wm_vol_t v, uint16_t *pending);

/** Copia los registros cuya clase está en mask (FML_MASK_*) a dst. Devuelve
 * cuántos (0 sin catálogo). No hace E/S. */
int wm_lib_snapshot(wm_vol_t v, uint32_t mask, FlexMlRec *dst, int cap, uint32_t *rev);
bool wm_lib_get(wm_vol_t v, uint32_t id, FlexMlRec *out);

/** Publica un archivo TERMINADO (ruta relativa al volumen, normalmente en
 * FML_DIR_TMP o FML_DIR_RECPART) en `dir`, con nombre único, y lo registra.
 * Devuelve el id (0 = no, con el motivo en why). */
uint32_t wm_lib_add_file(wm_vol_t v, const char *tmp_rel, const char *dir, int kind, const char *shown_name,
                         uint8_t origin, char *why, size_t why_cap);
/** Borrado explícito (lo pide la persona). */
bool wm_lib_delete(wm_vol_t v, uint32_t id);
/** A la papelera (solo el reloj: la microSD no tiene papelera). */
bool wm_lib_trash(wm_vol_t v, uint32_t id);
bool wm_lib_rename(wm_vol_t v, uint32_t id, const char *name, char *why, size_t why_cap);
/** Algo cambió en el disco por fuera (Paint, Archivos): reconciliar. */
void wm_lib_rescan(wm_vol_t v);

/** El almacén para el Flex Web Server. Solo entre acquire y release: con la
 * microSD, el acquire toma un arrendamiento (falla si no está montada). */
FlexMediaStore *wm_lib_acquire(wm_vol_t v);
void wm_lib_release(wm_vol_t v);

/** Trabajo pesado (decodificar una foto entera) de uno en uno en TODO el
 * reloj: miniaturas, visor, validación de subidas. Nunca desde LVGL. */
bool wm_heavy_begin(uint32_t wait_ms);
void wm_heavy_end(void);

/** PSRAM para los medios (con una reserva que nunca se toca). */
void *wm_alloc(size_t n);
void wm_free(void *p);

#ifdef __cplusplus
}
#endif
