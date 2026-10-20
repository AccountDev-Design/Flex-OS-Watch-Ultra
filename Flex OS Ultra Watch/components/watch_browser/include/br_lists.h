/*
 * br_lists — historial y favoritos del navegador (FlexOS_BrowserApp.cpp,
 * brHistoryAdd / brBookmark* y su persistencia):
 *  - historial sin duplicados, lo más reciente arriba, con número de visitas;
 *    no guarda páginas internas (flex://); tope FLEXBR_HIST_MAX (80);
 *  - favoritos que se añaden o quitan con el mismo gesto; tope FLEXBR_BM_MAX;
 *  - fichero "FBR1" versión 1. RELOJ: campo a campo en little-endian (la
 *    referencia volcaba el struct tal cual, ambigüedad 23), con escritura
 *    atómica (temporal + renombrado) y lectura que valida todo.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "br_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BR_FILE_MAGIC 0x31524246u /* "FBR1" */
#define BR_FILE_VER   1

typedef struct {
    BrHistEntry h[FLEXBR_HIST_MAX];
    int hn;
    BrBookmark b[FLEXBR_BM_MAX];
    int bn;
    uint32_t gen; /* sube con cada cambio */
} br_lists_t;

void br_lists_init(br_lists_t *l);
/** Apunta una visita (url no interna). now_ms para ordenar. */
void br_hist_add(br_lists_t *l, const char *url, const char *title, uint32_t now_ms);
/** El título de una visita que ya está (llega después, con el STATE). */
void br_hist_title(br_lists_t *l, const char *url, const char *title);
void br_hist_clear(br_lists_t *l);
int br_bm_find(const br_lists_t *l, const char *url);
/** Añade o quita: devuelve true si queda añadido. false también si no cabe. */
bool br_bm_toggle(br_lists_t *l, const char *url, const char *title, bool *added);
void br_bm_clear(br_lists_t *l);

/** Guarda/carga historial (hist=true) o favoritos en un fichero real. */
bool br_lists_save(const br_lists_t *l, bool hist, const char *path);
bool br_lists_load(br_lists_t *l, bool hist, const char *path);

#ifdef __cplusplus
}
#endif
