/*
 * br_net — la sesión FBP/1 del navegador: PORT de la tarea de red de Flex OS
 * Ultra (brNetTask, wsHandshake, wsReadMessage, wsSendFrame, brHandleMessage
 * y brSendPending de FlexOS_BrowserApp.cpp) a C puro, sobre un transporte
 * abstracto: TCP/TLS de ESP-IDF en el reloj (watch_browser.c) y un servicio
 * en memoria en tests/host y en la vista previa. Mismo WebSocket propio
 * (RFC 6455, tramas enmascaradas, PING->PONG, CLOSE), mismos plazos (sondeo
 * 40 ms, 9 s por mensaje empezado, 10 s para el WELCOME, 5 s para que la
 * interfaz consuma un FRAME), mismo backoff 1->16 s y mismas frases.
 *
 * El bucle de la referencia esperaba con vTaskDelay; aquí br_net_step() hace
 * una vuelta y las esperas son plazos (wait_until), para que pararlo sea
 * inmediato. Solo bloquea dentro de connect/read/write del transporte.
 *
 * RELOJ (marcado en el código):
 *  - el TLS lo verifica el transporte (paquete de CA); un fallo de
 *    certificado se dice como tal;
 *  - tras el WELCOME se manda un VIEWPORT con escala y calidad (la
 *    referencia no las negociaba al conectar, ambigüedad 14);
 *  - un FRAME de otra pestaña no se pinta (ambigüedad 3);
 *  - tras ERROR 1/9 se espera el backoff antes de reconectar (17);
 *  - el texto de una tecla admite 15 bytes (12).
 * Concurrencia: lock/unlock del transporte (pueden ser NULL en las pruebas).
 * La interfaz lee el estado con br_net_lock() y consume FRAMEs con
 * br_net_frame_peek()/br_net_frame_done(): mientras haya uno sin consumir la
 * sesión no lee del socket (contrapresión de la referencia).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "br_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { BR_IO_OK = 0, BR_IO_E_CONNECT = -1, BR_IO_E_TLS = -2, BR_IO_E_DNS = -3, BR_IO_E_TIMEOUT = -4 };

typedef struct {
    void *ctx;
    /** Abre la conexión (TLS verificado si tls). BR_IO_OK o BR_IO_E_*. */
    int (*connect)(void *ctx, const char *host, uint16_t port, bool tls);
    /** Escribe hasta n bytes: >0 escritos, 0 aún nada, <0 cerrada. */
    int (*write)(void *ctx, const uint8_t *b, size_t n, uint32_t to_ms);
    /** Lee hasta n bytes: >0 leídos, 0 plazo sin datos, <0 cerrada. */
    int (*read)(void *ctx, uint8_t *b, size_t n, uint32_t to_ms);
    void (*close)(void *ctx);
    uint32_t (*now)(void *ctx);
    uint32_t (*rand)(void *ctx);
    /** ¿Hay memoria para TLS? NULL = sí. */
    bool (*mem_ok)(void *ctx);
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} br_io_t;

#define BR_CMDQ   16
#define BR_URLQ   4
#define BR_DL_MAX 6
#define BR_INFO_MAX 112

typedef struct {
    uint8_t type, ch, u1, u2, u3;
    int16_t a, b;
    uint32_t arg;
    char text[16];
} br_cmd_t;

/** Lo último que dijo el servicio de una pestaña (STATE/NAVIGATED). */
typedef struct {
    uint8_t flags, progress;
    char title[FLEXBR_TITLE_MAX];
    char url[FLEXBR_URL_MAX];
    uint32_t gen;     /* sube con cada STATE */
    uint32_t nav_gen; /* sube con cada NAVIGATED aceptado */
} br_tab_state_t;

typedef struct {
    char name[48];
    char url[FLEXBR_URL_MAX];
    uint32_t size;
} br_download_t;

typedef struct {
    const br_io_t *io;
    int state; /* BRN_* */
    char info[BR_INFO_MAX];
    uint32_t info_gen;
    char err[BR_INFO_MAX];
    uint32_t err_gen;
    BrStats stats;
    uint32_t msgs_in;

    /* configuración (br_net_configure) */
    BrSettings set;
    char url[FLEXBR_SERVER_MAX];
    char token[FLEXBR_TOKEN_MAX];
    char device[48];
    uint32_t caps;
    bool online, stop;

    /* geometría */
    uint16_t vp_want_w, vp_want_h, vp_sent_w, vp_sent_h;
    uint32_t vp_set_ms, vp_sent_ms;
    bool vp_dirty;
    uint8_t active_ch;

    /* órdenes pendientes (solo salen en READY) */
    br_cmd_t q[BR_CMDQ];
    uint8_t qh, qn;
    char urlq[BR_URLQ][FLEXBR_URL_MAX];
    uint8_t uh, un;
    uint16_t seq;

    uint8_t *rx;
    size_t rx_cap;
    uint8_t tx[FLEXBR_URL_MAX + 64];

    /* ranura del FRAME (una, como la referencia) */
    bool fr_ready;
    FbpFrame fr;
    uint16_t fr_seq;
    uint8_t fr_ch;
    uint32_t fr_since;
    uint16_t fr_last_drawn;
    uint32_t fr_gen;

    br_tab_state_t tab[FLEXBR_MAX_TABS + 1]; /* índice = canal (1..4) */
    FbpMedia media;
    bool media_avail;
    uint32_t media_gen;
    br_download_t dl[BR_DL_MAX];
    uint8_t dl_n;
    char session_id[40];
    bool relay_warned;

    uint32_t backoff, hello_ms, last_ping, last_ack, wait_until, nav_sent_ms;
    bool nav_waiting, warned_noframes, open;
} br_net_t;

/** rx: búfer de recepción (FBP_PAYLOAD_MAX + 12 + 64 B; en PSRAM en el reloj). */
void br_net_init(br_net_t *s, const br_io_t *io, uint8_t *rx, size_t rx_cap);
/** Servidor (wss://...), credencial, ajustes, nombre del dispositivo y capacidades.
 * Si cambia el servidor o la credencial, la sesión se rehace. */
void br_net_configure(br_net_t *s, const char *url, const char *token, const BrSettings *st, const char *device,
                      uint32_t caps);
void br_net_set_online(br_net_t *s, bool online);
/** Tamaño de la zona de página y pestaña activa (canal). */
void br_net_set_viewport(br_net_t *s, uint16_t w, uint16_t h, uint8_t active_ch);
/** Una vuelta del bucle. Devuelve los ms que conviene esperar antes de la siguiente. */
uint32_t br_net_step(br_net_t *s);
/** Cierra la conexión (al salir del navegador). */
void br_net_shutdown(br_net_t *s);

bool br_net_navigate(br_net_t *s, uint8_t ch, const char *url);
bool br_net_cmd(br_net_t *s, uint8_t type, uint8_t ch);
bool br_net_tap(br_net_t *s, uint8_t ch, uint16_t x, uint16_t y);
bool br_net_scroll(br_net_t *s, uint8_t ch, int16_t dx, int16_t dy);
bool br_net_key_text(br_net_t *s, uint8_t ch, const char *utf8);
bool br_net_key(br_net_t *s, uint8_t ch, uint8_t vk);
bool br_net_media_cmd(br_net_t *s, uint8_t cmd, uint32_t arg);

void br_net_lock(br_net_t *s);
void br_net_unlock(br_net_t *s);
/** Hay un FRAME por pintar: copia sus datos (la imagen sigue en rx hasta _done). */
bool br_net_frame_peek(br_net_t *s, FbpFrame *f, uint8_t *ch, uint16_t *seq);
enum { BR_FR_DRAWN = 0, BR_FR_SKIPPED, BR_FR_BAD };
/** Libera la ranura: dibujado, no visible (se descarta) o ilegible. */
void br_net_frame_done(br_net_t *s, int result);

/** Nombre del estado para la interfaz ("Sesión activa"...). */
const char *br_net_state_name(int state);

#ifdef __cplusplus
}
#endif
