/*
 * gal_wm — la biblioteca de medios del reloj (watch_media: memoria del reloj
 * y microSD) como lo local de la Galería (gal_local_io_t).
 *  - gal_wm_map.c: traducir un registro de la biblioteca a un elemento de la
 *    Galería y ordenar (puro: tests/host y tools/sim);
 *  - gal_wm.c: la E/S de verdad (arrendamiento de la microSD, miniaturas
 *    persistentes de la biblioteca, papelera y publicar en el destino
 *    elegido). Solo en el reloj.
 * No es API: solo lo incluye watch_gallery.
 */
#pragma once

#include <stdbool.h>
#include "FlexOS_MediaLib.h"
#include "gal_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Elemento de la Galería para un registro (fotos, vídeos y dibujos). false
 * si no es de la Galería (música, grabaciones) o su ruta no cabe. */
bool gal_wm_item(const FlexMlRec *r, int vol, gal_item_t *out);
/** Formato de la Galería para un formato real de la biblioteca. */
gal_fmt_t gal_wm_fmt(int fml_fmt);
/** Del más reciente al más antiguo (estable: a igual fecha, el orden de entrada). */
void gal_wm_sort(gal_item_t *it, int n);
/** ¿Un vídeo de la biblioteca que el reloj reproduce? (AVI con MJPEG) */
bool gal_wm_playable_video(const gal_item_t *it);

/** La E/S local del reloj (gal_wm.c). */
const gal_local_io_t *gal_wm_io(void);

#ifdef __cplusplus
}
#endif
