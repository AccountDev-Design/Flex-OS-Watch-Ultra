/*
 * gal_http — cliente HTTP/1.1 de la Galería sobre un transporte abstracto
 * (como br_net del navegador): TCP/TLS de ESP-IDF en el reloj
 * (watch_gallery.c), sockets del PC en las pruebas de extremo a extremo y un
 * servidor en memoria en tests/host y en la vista previa.
 *
 * Habla con los DOS servidores que ya existen, sin uno nuevo:
 *  - Flex Web Server del P4 (FlexOS_MediaWeb): Host = ip:puerto (lo exige
 *    flexHttpHostOk), sesión en la cookie "fxs" y "X-Flex: 1" en lo que
 *    cambia algo;
 *  - Flex Cloud (/api/cloud de Flex Developer Studio, o la pasarela del P4
 *    hacia el teléfono): "Authorization: Bearer" o la sesión del P4.
 *
 * Lo que hace y por qué:
 *  - keep-alive: varias miniaturas por la misma conexión (un saludo TLS
 *    cuesta segundos en el S3). Una conexión ociosa más de
 *    GAL_HTTP_IDLE_MS se cierra antes de reutilizarla (el P4 corta a los
 *    4 s); si una reutilizada resulta cerrada antes de que llegue un byte,
 *    la petición se repite UNA vez por una conexión nueva, salvo que lleve
 *    un cuerpo por flujo (no se puede rebobinar: esas van siempre por una
 *    conexión recién abierta si la otra llevaba ociosa);
 *  - cuerpo por Content-Length, chunked o hasta el cierre; nunca se lee más
 *    allá de lo que dice la cabecera;
 *  - plazos por lectura y cancelación (cancelled) comprobada en cada vuelta;
 *  - ninguna reserva de memoria: las cabeceras se montan y se leen en el
 *    búfer de la conexión (GAL_HTTP_BUF bytes).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GAL_IO_OK = 0, GAL_IO_E_CONNECT = -1, GAL_IO_E_TLS = -2, GAL_IO_E_DNS = -3, GAL_IO_E_TIMEOUT = -4 };

typedef struct {
    void *ctx;
    /** Abre la conexión (TLS verificado si tls). GAL_IO_OK o GAL_IO_E_*. */
    int (*connect)(void *ctx, const char *host, uint16_t port, bool tls, uint32_t to_ms);
    /** Escribe hasta n bytes: >0 escritos, 0 aún nada, <0 cerrada. */
    int (*write)(void *ctx, const uint8_t *b, size_t n, uint32_t to_ms);
    /** Lee hasta n bytes: >0 leídos, 0 plazo sin datos, <0 cerrada. */
    int (*read)(void *ctx, uint8_t *b, size_t n, uint32_t to_ms);
    void (*close)(void *ctx);
    uint32_t (*now)(void *ctx);
    /** ¿Se abandonó el trabajo en curso (app cerrada, otra vista)? NULL = nunca. */
    bool (*cancelled)(void *ctx);
} gal_io_t;

/* Errores (estado negativo). */
enum {
    GAL_HTTP_E_CONNECT = -1,
    GAL_HTTP_E_TLS = -2,
    GAL_HTTP_E_DNS = -3,
    GAL_HTTP_E_TIMEOUT = -4,
    GAL_HTTP_E_CLOSED = -5, /**< se cortó antes de terminar la respuesta */
    GAL_HTTP_E_PROTO = -6,  /**< respuesta ilegible */
    GAL_HTTP_E_TOOBIG = -7, /**< cabeceras o cuerpo mayores de lo aceptado */
    GAL_HTTP_E_CANCEL = -8,
    GAL_HTTP_E_BODY = -9,   /**< el cuerpo de la petición no se pudo leer (archivo) */
    GAL_HTTP_E_ARG = -10,
};

#define GAL_HTTP_BUF        4096
#define GAL_HTTP_TIMEOUT_MS 15000 /* HTTP_TIMEOUT_MS / FLEXWEB_BODY_TIMEOUT_MS de la referencia */
#define GAL_HTTP_IDLE_MS    2500  /* el P4 cierra a los 4 s de ociosidad (FLEXWEB_KEEP_IDLE_MS) */
#define GAL_HTTP_HOST_MAX   96

typedef struct {
    const char *method;  /**< "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE" */
    const char *host;
    uint16_t port;
    bool tls;
    const char *path;    /**< con la consulta ("/api/library?since=3") */
    const char *agent;   /**< User-Agent (NULL = ninguno) */
    const char *bearer;  /**< Authorization: Bearer (NULL = ninguna) */
    const char *cookie;  /**< valor de Cookie ("fxs=...") */
    bool xflex;          /**< X-Flex: 1 (CSRF del P4) */
    const char *ctype;   /**< Content-Type del cuerpo */
    int64_t range_from;  /**< Range: bytes=N- (<0 = sin Range) */
    const char *if_range;/**< If-Range (ETag) */
    const char *extra;   /**< líneas de cabecera ya formadas ("K: v\r\n...") */
    uint64_t body_len;   /**< Content-Length del cuerpo (0 = sin cuerpo) */
    const uint8_t *body; /**< cuerpo en memoria, o NULL con body_cb */
    /** Cuerpo por flujo: >0 bytes, 0 fin prematuro, <0 error. */
    int (*body_cb)(void *ud, uint8_t *buf, size_t n);
    void *body_ud;
    uint32_t timeout_ms; /**< por lectura/escritura (0 = GAL_HTTP_TIMEOUT_MS) */
} gal_req_t;

typedef struct {
    int status;             /**< 100..599, o GAL_HTTP_E_* */
    int64_t length;         /**< Content-Length (-1 = no lo dijo) */
    bool chunked;
    bool has_range;         /**< hubo Content-Range */
    uint64_t range_start, range_end, range_total;
    char etag[72];          /**< sin comillas */
    char ctype[48];
    char cookie[40];        /**< valor de la cookie "fxs" (Set-Cookie) */
    bool cookie_set;        /**< hubo Set-Cookie de "fxs" (vacía = la borró) */
    uint32_t retry_after_s;
} gal_resp_t;

typedef struct {
    const gal_io_t *io;
    bool open;
    char host[GAL_HTTP_HOST_MAX];
    uint16_t port;
    bool tls;
    uint32_t idle_since;
    uint32_t reqs, opens;   /**< peticiones y conexiones abiertas (estadística y pruebas) */
    /* respuesta en curso */
    bool in_body, body_done, keep, head;
    int64_t left;           /**< bytes que faltan (Content-Length); -1 = hasta el cierre */
    bool chunked;
    int cstate;             /**< 0 tamaño, 1 datos, 2 CRLF tras datos, 3 remolques, 4 fin */
    uint64_t chunk_left;
    uint32_t to_ms;
    size_t bn, bp;          /**< bytes en buf y posición de lectura */
    uint8_t buf[GAL_HTTP_BUF];
} gal_conn_t;

void gal_conn_init(gal_conn_t *c, const gal_io_t *io);
void gal_conn_close(gal_conn_t *c);

/**
 * Envía la petición y lee las cabeceras de la respuesta. Devuelve el estado
 * HTTP (>= 100) o un GAL_HTTP_E_*. Con un estado HTTP, el cuerpo se lee con
 * gal_http_read() y la petición se cierra SIEMPRE con gal_http_end().
 */
int gal_http_begin(gal_conn_t *c, const gal_req_t *rq, gal_resp_t *rs);
/** Cuerpo: >0 bytes, 0 fin, <0 GAL_HTTP_E_* (respuesta truncada, plazo, cancelación). */
int gal_http_read(gal_conn_t *c, uint8_t *out, size_t n);
/**
 * Cuerpo entero a out (termina en 0). *len = bytes. 0 bien, GAL_HTTP_E_TOOBIG
 * si no cabe (y la conexión se cierra), otro GAL_HTTP_E_* si se cortó.
 */
int gal_http_read_all(gal_conn_t *c, char *out, size_t cap, size_t *len);
/**
 * Termina la respuesta. Si se leyó entera y el servidor admite keep-alive,
 * la conexión queda para la siguiente; si no (o queda cuerpo sin leer de más
 * de 16 KB), se cierra.
 */
void gal_http_end(gal_conn_t *c);

/** Texto para la persona de un GAL_HTTP_E_*. */
const char *gal_http_err_text(int err);
/** Codifica s para una consulta (%XX salvo letras, cifras y -_.~). false si no cabe. */
bool gal_url_encode(const char *s, char *out, size_t n);

#ifdef __cplusplus
}
#endif
