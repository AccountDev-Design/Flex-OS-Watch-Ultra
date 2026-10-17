/*
 * wx_core — el motor del clima (estado, cadencia, ubicaciones y búsqueda)
 * de FlexOS_Weather.cpp, sin red ni NVS ni tareas: recibe la hora (ms) y los
 * resultados de cada descarga, y dice qué descargar. Lo usan el servicio del
 * reloj (watch_weather.c, que pone la red y el cerrojo), las pruebas de PC y
 * la vista previa, así que la lógica que decide es la misma en los tres.
 *
 * Reglas de la referencia que se mantienen:
 *  - ni un dato inventado: sin descarga válida (ni caché) no hay datos;
 *  - estados WXS_* derivados, nunca un bool ambiguo;
 *  - refresco cada 25 min; tras un fallo, reintento a los 2 min, y a los
 *    15 min tras 3 fallos seguidos; primer intento 4 s tras arrancar;
 *  - un refresco manual no descarga si lo último bueno es de hace < 5 min;
 *  - sin red no se intenta nada;
 *  - cambiar de ciudad oculta los datos de la otra (nunca datos de una ciudad
 *    con el nombre de otra).
 * RELOJ: una descarga que termina cuando ya se eligió otra ciudad se descarta
 * en vez de publicarse; la reprogramación se hace al terminar el intento (no
 * contando intentos desde fuera); el texto de la búsqueda va en español.
 */
#pragma once

#include "wx_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_REFRESH_MS    (25u * 60u * 1000u)
#define WX_RETRY_MS      (2u * 60u * 1000u)
#define WX_RETRY_LONG_MS (15u * 60u * 1000u)
#define WX_RETRY_FAILS   3
#define WX_FIRST_MS      4000u
#define WX_MANUAL_MIN_MS (5u * 60u * 1000u)

typedef enum { WXJ_NONE = 0, WXJ_FETCH, WXJ_SEARCH } wx_job_kind_t;

typedef struct {
    wx_job_kind_t kind;
    wx_loc_t loc;           /* WXJ_FETCH: copia estable de la ciudad pedida */
    char query[WX_QUERY];   /* WXJ_SEARCH */
} wx_job_t;

typedef struct {
    wx_data_t data; /* publicado (válido si have) */
    bool have, fresh, ok_valid, busy;
    uint8_t err, fails;
    uint32_t gen;     /* sube cuando cambia lo que se enseña (datos o su ausencia) */
    uint32_t ok_ms;   /* ms de la última descarga buena (si ok_valid) */
    uint32_t next_ms; /* próximo intento automático */
    bool began;

    wx_loc_t locs[WX_LOCS];
    uint8_t nloc;
    int8_t sel;
    bool locs_dirty;  /* el servicio las guarda y lo baja */
    bool cache_dirty;

    wx_loc_t res[WX_RESULTS];
    uint8_t nres, qst; /* WXQ_* */
    uint32_t qgen;

    bool req_fetch, req_search;
    char query[WX_QUERY];
    wx_loc_t cmd_loc;

    int64_t sys_utc;  /* reloj del sistema anclado en sys_ms (0 = sin hora) */
    uint32_t sys_ms;
} wx_core_t;

void wx_core_init(wx_core_t *c, uint32_t now_ms);
/** Ubicaciones leídas de NVS (se sanean aquí). */
void wx_core_set_locs(wx_core_t *c, const wx_loc_t *locs, uint8_t n, int8_t sel);
/** Caché leída de NVS: si vale se publica como "de caché". */
bool wx_core_load_cache(wx_core_t *c, const wx_data_t *w, size_t n);
/** Hora UTC real del sistema (antes de 2020 se ignora: es una semilla). */
void wx_core_set_clock(wx_core_t *c, int64_t utc, uint32_t now_ms);

/** Cada ~segundo: decide si toca descargar. */
void wx_core_tick(wx_core_t *c, uint32_t now_ms, bool online);
void wx_core_refresh(wx_core_t *c, uint32_t now_ms, bool force);

/** Trabajo pendiente (la búsqueda va primero: hay alguien mirando). */
bool wx_core_take_job(wx_core_t *c, wx_job_t *job);
/** Resultado de una descarga: err WXE_NONE + data parseado, o el error. */
void wx_core_fetch_done(wx_core_t *c, uint32_t now_ms, const wx_job_t *job, int err, const wx_data_t *data);
void wx_core_search_done(wx_core_t *c, int err, const wx_loc_t *res, int n);

uint8_t wx_core_status(const wx_core_t *c);
/** Segundos desde la última descarga buena, o -1 si no se sabe. */
int32_t wx_core_age_s(const wx_core_t *c, uint32_t now_ms);
/** Hora local de la ubicación (unix + desfase), 0 si no hay datos. */
int32_t wx_core_now_local(const wx_core_t *c, uint32_t now_ms);
/** Hora UTC (del sistema o extrapolada de la respuesta), 0 si no se sabe. */
int64_t wx_core_now_utc(const wx_core_t *c, uint32_t now_ms);

void wx_core_loc_select(wx_core_t *c, uint8_t i, uint32_t now_ms);
bool wx_core_loc_add(wx_core_t *c, const wx_loc_t *l, uint32_t now_ms);
void wx_core_loc_remove(wx_core_t *c, uint8_t i, uint32_t now_ms);

void wx_core_search(wx_core_t *c, const char *q);
void wx_core_search_clear(wx_core_t *c);

#ifdef __cplusplus
}
#endif
