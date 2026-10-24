/*
 * gal_engine — el motor de la Galería: fuentes, listas, miniaturas, visor y
 * transferencias. C portable: en el reloj lo mueve la tarea de watch_gallery;
 * en las pruebas de PC y en la vista previa, un bucle con un transporte de
 * pruebas o con sockets de verdad contra los servidores reales.
 *
 * HILOS. La interfaz (tarea de LVGL) solo pide y lee: cada gal_* toma el
 * cerrojo un momento, apunta la petición y despierta a la tarea (kick). La
 * tarea (gal_engine_step) hace TODO lo que bloquea (red, LittleFS,
 * decodificar) sin el cerrojo, y publica el resultado con él. Nada de lo que
 * hace la tarea toca LVGL.
 *
 * MEMORIA (PSRAM, solo con la Galería abierta; gal_engine_shed la suelta):
 *  - el motor con las dos listas: sizeof(gal_engine_t) (180 400 B en el S3);
 *  - respuestas JSON: GAL_JSON_MAX (la biblioteca del P4 cabe entera);
 *  - miniaturas: GAL_THUMBS x GAL_THUMB_SIDE^2 x 2 B;
 *  - visor: el 2x (<= GAL_VIEW_MAX_W x GAL_VIEW_MAX_H x 2 B) y su mitad, y la
 *    banda de gal_jpeg mientras decodifica;
 *  - huellas de las partes de una subida: 32 B por parte (<= 16 KB).
 * Antes de reservar lo grande se pregunta a mem_ok(): con poca memoria se
 * dice y no se intenta.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gal_acct.h"
#include "gal_core.h"
#include "gal_http.h"
#include "gal_jpeg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAL_THUMB_SIDE   112
#define GAL_THUMBS       30
#define GAL_JSON_MAX     (160u * 1024u)
#define GAL_VIEW_W       410
#define GAL_VIEW_H       502
#define GAL_VIEW_MAX_W   (2 * GAL_VIEW_W)
#define GAL_VIEW_MAX_H   (2 * GAL_VIEW_H)
#define GAL_XFERS        8
#define GAL_ORIGINS      64
#define GAL_RESERVE      (512u * 1024u) /* FML_RESERVE_BYTES: lo que nunca se llena del LittleFS */
#define GAL_ULTRA_PORT   8080           /* FLEXWEB_PORT */
#define GAL_RECHECK_MS   30000u         /* una fuente lista se vuelve a comprobar cada 30 s */
#define GAL_ULTRA_POLL_MS 10000u        /* ?since=rev al P4: barato si no cambió nada */
#define GAL_CLOUD_POLL_MS 60000u
#define GAL_AGENT        "FlexOS-Watch-Ultra/1.0 ESP32-S3"

/** Lo que el motor necesita de su plataforma. */
typedef struct {
    const gal_io_t *io_lan;   /**< conexión con el P4 (HTTP en la red local) */
    const gal_io_t *io_cloud; /**< conexión con Flex Cloud (TLS) */
    const gal_store_t *store; /**< NVS */
    void *(*alloc)(size_t n); /**< PSRAM */
    void (*free)(void *p);
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void (*kick)(void *ctx);  /**< despierta a la tarea (puede ser NULL) */
    void *ctx;
    uint32_t (*now)(void *ctx);
    /** Bytes libres de verdad en el LittleFS del reloj. */
    uint64_t (*fs_free)(void *ctx);
    /** ¿Se pueden reservar ahora n bytes de PSRAM sin dejar el sistema sin margen? NULL = sí. */
    bool (*mem_ok)(void *ctx, size_t n);
    const char *mac_hex;      /**< para el hardwareId de Flex Account */
    const char *version;
} gal_env_t;

typedef struct gal_engine gal_engine_t;

/* Estados de la lista remota */
enum { GAL_L_IDLE = 0, GAL_L_LOADING, GAL_L_READY, GAL_L_ERROR };
/* Miniaturas */
enum { GAL_TH_EMPTY = 0, GAL_TH_WANTED, GAL_TH_LOADING, GAL_TH_READY, GAL_TH_FAILED };
/* Visor */
enum { GAL_V_IDLE = 0, GAL_V_LOADING, GAL_V_READY, GAL_V_ERROR };
/* Transferencias */
enum { GAL_X_DOWNLOAD = 1, GAL_X_UPLOAD, GAL_X_TRASH };
enum { GAL_XS_FREE = 0, GAL_XS_QUEUED, GAL_XS_RUN, GAL_XS_WAIT, GAL_XS_DONE, GAL_XS_FAIL, GAL_XS_CANCEL };

/** Instantánea del estado para la interfaz. */
typedef struct {
    bool open, wifi;
    gal_pick_t pick;
    gal_choice_t choice;            /**< fuente remota en uso y por qué */
    gal_health_t health[GAL_SRC_N];
    /* Flex OS Ultra */
    char ultra_host[40];
    uint16_t ultra_port;
    bool ultra_paired;              /**< hay sesión del Flex Web Server */
    char ultra_pair_msg[96];        /**< resultado del último código */
    bool ultra_pairing;
    gal_phone_t phone;              /**< el teléfono de Flex Storage del P4 */
    /* Flex Account */
    uint8_t acct_flow, acct_link;
    char acct_code[9], acct_stage[72], acct_error[144], acct_address[48], acct_detail[96];
    /* listas */
    uint32_t local_gen, remote_gen;
    int nl, nr;
    uint8_t remote_state;           /**< GAL_L_* */
    int8_t remote_src;
    bool remote_more;
    char remote_err[112];
    bool scanning;
    uint64_t q_total, q_used, q_avail; /**< cuota de Flex Cloud (o del teléfono) */
    char q_state[8];
    /* visor */
    uint8_t view_state;
    uint32_t view_gen;
    /* transferencias */
    int xfers_active;
    uint32_t xfer_gen;
} gal_status_t;

typedef struct {
    uint8_t id;           /**< número de la transferencia (1..255) */
    uint8_t op, state;    /**< GAL_X_*, GAL_XS_* */
    int8_t dest;          /**< fuente de destino (subida) o de origen (descarga) */
    char name[GAL_NAME_MAX];
    uint64_t done, total;
    char msg[96];
} gal_xfer_info_t;

/** El visor: lienzos listos para LVGL (RGB565). Válidos mientras view_gen no cambie. */
typedef struct {
    uint8_t state;
    const uint16_t *fit;   /**< encajado en la pantalla (<= 410 x 502) */
    int fit_w, fit_h;
    const uint16_t *zoom;  /**< 2x (NULL si la foto no es mayor que la pantalla o no hubo memoria) */
    int zoom_w, zoom_h;
    int src_w, src_h;
    uint64_t done, total;  /**< bytes leídos de la foto */
    char err[112];
    uint32_t gen;
} gal_view_t;

/* ---- vida ---- */
gal_engine_t *gal_engine_new(const gal_env_t *env);
void gal_engine_free(gal_engine_t *e);
/** La app se abre: reserva lo grande, recorre el reloj y comprueba las fuentes. false sin memoria. */
bool gal_engine_open(gal_engine_t *e);
/** La app se cierra o se suspende: suelta miniaturas, visor y JSON (las transferencias siguen). */
void gal_engine_close(gal_engine_t *e);
/** ¿Queda trabajo aunque la app esté cerrada (transferencias)? */
bool gal_engine_busy(gal_engine_t *e);
/** Una vuelta de la tarea. Devuelve los ms que conviene esperar. */
uint32_t gal_engine_step(gal_engine_t *e);
/** Para el transporte: ¿el trabajo en curso ya no lo quiere nadie? */
bool gal_engine_should_abort(gal_engine_t *e);

/* ---- configuración ---- */
void gal_set_wifi(gal_engine_t *e, bool connected);
void gal_set_pick(gal_engine_t *e, gal_pick_t p);
/** Dirección del Flex Web Server del P4 (la que enseña su hoja "Conectar con el móvil"). */
bool gal_set_ultra(gal_engine_t *e, const char *host, uint16_t port);
/** Empareja con el código de 6 cifras del P4 (POST /api/pair). */
bool gal_ultra_pair(gal_engine_t *e, const char *code);
/** Olvida la sesión y la dirección del P4. */
void gal_ultra_forget(gal_engine_t *e);
void gal_acct_link(gal_engine_t *e);
void gal_acct_link_cancel(gal_engine_t *e);
void gal_acct_unlink(gal_engine_t *e);
/** Pruebas: Flex Cloud y Flex Account en otro servidor (y otra clave de firma). */
void gal_engine_set_cloud(gal_engine_t *e, const char *host, uint16_t port, bool tls, const uint8_t pub[65],
                          const char *key_id);

/* ---- lectura (con el cerrojo de gal_engine_lock) ---- */
void gal_status(gal_engine_t *e, gal_status_t *out);
void gal_engine_lock(gal_engine_t *e);
void gal_engine_unlock(gal_engine_t *e);
/** Con el cerrojo: las listas (0 local, 1 remota). */
const gal_item_t *gal_list(gal_engine_t *e, int which, int *n);
/** Con el cerrojo: copia los elementos de la pestaña (ver gal_view_build). */
int gal_build_view(gal_engine_t *e, gal_tab_t tab, gal_ref_t *out, int cap);
const gal_item_t *gal_ref_item(gal_engine_t *e, gal_ref_t r);

/* ---- peticiones ---- */
void gal_refresh(gal_engine_t *e);
/** La interfaz llega al final de la lista remota: la página siguiente (Flex Cloud). */
void gal_more(gal_engine_t *e);

/**
 * Miniaturas. request: se quiere la de este elemento (va a la cola, las
 * pedidas hace menos tienen prioridad). acquire: si está lista, devuelve el
 * hueco (>= 0) y su imagen GAL_THUMB_SIDE x GAL_THUMB_SIDE, y la fija hasta
 * release (no se reutiliza mientras se pinta). state: GAL_TH_*.
 */
void gal_thumb_request(gal_engine_t *e, const gal_item_t *it);
int gal_thumb_acquire(gal_engine_t *e, const gal_item_t *it, const uint16_t **px, int *state);
void gal_thumb_release(gal_engine_t *e, int slot);

/** Visor: abre el elemento (copia). close suelta los lienzos: antes, la interfaz deja de enseñarlos. */
bool gal_view_open(gal_engine_t *e, const gal_item_t *it);
void gal_view_close(gal_engine_t *e);
void gal_view_get(gal_engine_t *e, gal_view_t *out);

/**
 * Transferencias (con su motivo si no se pueden poner en cola):
 *  - download: de una fuente remota a /Imagenes del reloj;
 *  - upload: una foto del reloj a dest (GAL_SRC_ULTRA, _CLOUD o _PHONE);
 *  - trash: a la papelera de donde esté (la del reloj, la de Flex Cloud o la del P4).
 * Devuelven el número de la transferencia (> 0) o 0.
 */
int gal_download(gal_engine_t *e, const gal_item_t *it, char *why, size_t n);
int gal_upload(gal_engine_t *e, const gal_item_t *it, int dest, char *why, size_t n);
int gal_trash(gal_engine_t *e, const gal_item_t *it, char *why, size_t n);
void gal_xfer_cancel(gal_engine_t *e, int id);
void gal_xfer_retry(gal_engine_t *e, int id);
void gal_xfer_clear(gal_engine_t *e);
int gal_xfers(gal_engine_t *e, gal_xfer_info_t *out, int cap);
/** ¿Esta operación tiene sentido para este elemento ahora? (la interfaz no enseña lo que no funciona) */
bool gal_can_download(gal_engine_t *e, const gal_item_t *it);
bool gal_can_upload(gal_engine_t *e, const gal_item_t *it, int dest);
bool gal_can_trash(gal_engine_t *e, const gal_item_t *it);
/** Lo mismo con el motivo si no (op GAL_X_*; dest solo cuenta en las subidas). */
bool gal_can(gal_engine_t *e, int op, const gal_item_t *it, int dest, char *why, size_t n);

/* ---- medidas (pruebas y Device Care) ---- */
typedef struct {
    size_t engine_bytes, json_bytes, thumb_bytes, view_bytes, parts_bytes;
    uint32_t http_reqs, http_opens, thumbs_done, thumbs_failed, views_done;
} gal_mem_t;
void gal_engine_mem(gal_engine_t *e, gal_mem_t *out);

#ifdef __cplusplus
}
#endif
