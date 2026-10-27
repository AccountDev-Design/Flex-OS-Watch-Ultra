/*
 * gal_priv — el estado interno del motor, compartido por gal_engine.c
 * (peticiones y planificación), gal_net.c (red) y gal_local.c (LittleFS).
 * No es API: nada fuera de watch_gallery lo incluye.
 */
#pragma once

#include <stdatomic.h>
#include "gal_engine.h"

#define THUMB_PX ((size_t)GAL_THUMB_SIDE * GAL_THUMB_SIDE)

typedef struct {
    char key[GAL_ID_MAX + GAL_PATH_MAX + 24]; /* "src|id|tver" o "0|ruta|tamaño|mtime" */
    uint8_t state;
    uint8_t pins;
    uint32_t lru;     /* última vez que se pidió o se pintó */
    uint32_t fail_ms;
    gal_item_t item;  /* copia de lo que se pidió */
    uint16_t *px;     /* GAL_THUMB_SIDE^2 */
} gal_thumb_t;

typedef struct {
    uint8_t id, op, state;
    int8_t dest;
    gal_item_t item;   /* copia */
    uint64_t done, total;
    uint8_t fails;
    uint32_t next_ms;
    bool cancel;
    char msg[96];
    char up_id[GAL_ID_MAX]; /* subida a Flex Cloud en curso (para cancelarla en el servidor) */
} gal_xfer_t;

struct gal_engine {
    gal_env_t env;
    bool open, wifi;
    /* configuración */
    uint8_t pick;
    char ultra_host[40];
    uint16_t ultra_port;
    char cookie[40];        /* sesión del Flex Web Server: solo en RAM */
    char pair_code[8];
    bool pair_req, pairing;
    char pair_msg[96];
    gal_hello_t hello;
    gal_phone_t phone;
    /* Flex Cloud y Flex Account */
    char cloud_host[GAL_HTTP_HOST_MAX];
    uint16_t cloud_port;
    bool cloud_tls;
    gal_acct_t acct;
    uint8_t acct_req[3];    /* vincular, cancelar, olvidar: los aplica la tarea */
    gal_me_t me;
    bool me_valid;
    uint32_t me_ms;
    /* fuentes */
    gal_health_t health[GAL_SRC_N];
    gal_choice_t choice;
    int8_t active;          /* fuente remota de la lista */
    /* listas */
    gal_item_t local[GAL_LIST_MAX];
    int nl;
    uint32_t local_gen;
    bool scan_req, scanning;
    uint32_t scan_ms;       /* último recorrido (gal_refresh no lo repite enseguida) */
    gal_item_t remote[GAL_LIST_MAX];
    int nr;
    uint32_t remote_gen;
    uint8_t remote_state;
    int8_t remote_src;
    char remote_err[112];
    char cursor[64];
    bool remote_more, more_req, list_req;
    uint32_t list_ms, ultra_rev;
    gal_origin_t origins[GAL_ORIGINS];
    int norigins;
    /* miniaturas */
    gal_thumb_t thumbs[GAL_THUMBS];
    uint16_t *thumb_mem;
    uint32_t thumbs_done, thumbs_failed;
    /* visor */
    gal_item_t view_item;
    bool view_req;
    uint8_t view_state;
    uint32_t view_gen;      /* sube con cada apertura y cierre */
    uint16_t *view_zoom, *view_fit;
    size_t view_zoom_sz, view_fit_sz;
    int fit_w, fit_h, zoom_w, zoom_h, src_w, src_h;
    /* Progreso del visor: lo escribe la tarea sin cerrojo (cada lectura del
     * flujo) y lo lee la interfaz. 32 bits atómicos: en el Xtensa de 32 bits un
     * uint64_t se escribe en dos mitades y la interfaz podía leer una mezcla
     * (ThreadSanitizer, tests/host/gal_race.c). Una foto no pasa de 4 GB. */
    _Atomic uint32_t view_done;
    uint64_t view_total;
    char view_err[112];
    uint32_t views_done;
    /* transferencias */
    gal_xfer_t xq[GAL_XFERS];
    uint8_t next_xid;
    uint32_t xfer_gen;
    uint8_t *parts;         /* huellas de las partes de la subida en curso */
    size_t parts_bytes;
    /* trabajo en curso que se puede abandonar */
    uint32_t job_gen;       /* la tarea apunta qué generación del visor/lista está sirviendo */
    atomic_bool abort_job; /* aviso de cortar: lo escribe la interfaz y lo lee la tarea */
    /* red */
    gal_conn_t lan, cloud;
    char *json;
};

/* gal_engine.c */
void gal__lock(gal_engine_t *e);
void gal__unlock(gal_engine_t *e);
uint32_t gal__now(gal_engine_t *e);
void *gal__alloc(gal_engine_t *e, size_t n);
void gal__free(gal_engine_t *e, void *p);
bool gal__mem_ok(gal_engine_t *e, size_t n);
gal_xfer_t *gal__xfer(gal_engine_t *e, int id);
/** Con el cerrojo: marca en la lista remota lo que ya tiene copia en el reloj. */
void gal__mark_onwatch(gal_engine_t *e);

/* gal_net.c (la tarea, sin el cerrojo salvo donde se dice) */
void gal__net_health(gal_engine_t *e, int src, uint32_t now);
void gal__net_pair(gal_engine_t *e);
void gal__net_list(gal_engine_t *e, bool more);
bool gal__net_thumb(gal_engine_t *e, gal_thumb_t *t, uint16_t *px);
int gal__net_view_stream(gal_engine_t *e, const gal_item_t *it, gal_jpeg_src_t *src, void **ctx);
void gal__net_view_end(gal_engine_t *e, void *ctx);
void gal__net_xfer(gal_engine_t *e, gal_xfer_t *x);
/** Base de la API de Flex Cloud para una fuente (nube o pasarela del P4). */
bool gal__cloud_base(gal_engine_t *e, int src, const char **host, uint16_t *port, bool *tls, const char **prefix);

/* gal_local.c */
void gal__local_scan(gal_engine_t *e);
bool gal__local_thumb(gal_engine_t *e, gal_thumb_t *t, uint16_t *px);
int gal__local_view_stream(gal_engine_t *e, const gal_item_t *it, gal_jpeg_src_t *src, void **ctx);
void gal__local_view_end(gal_engine_t *e, void *ctx);
void gal__local_trash(gal_engine_t *e, gal_xfer_t *x);
void gal__origin_load(gal_engine_t *e);
bool gal__origin_add(gal_engine_t *e, const gal_origin_t *o);
/** Ruta real de una virtual ("/Imagenes/x.jpg"). */
bool gal__real(const char *vpath, char *out, size_t n);
/** Crea la carpeta (y sus padres) si falta. */
void gal__mkdirs(const char *vpath);
/** Primer nombre libre en /Imagenes para name. */
bool gal__unique_photo(const char *name, char *vpath, size_t n);
