/*
 * gal_fake — servidor HTTP/1.1 en memoria detrás de un gal_io_t, para las
 * pruebas de PC y la vista previa de la Galería. Lee de verdad los bytes que
 * escribe gal_http (línea de petición, cabeceras, cuerpo por Content-Length)
 * y devuelve lo que dice el manejador, con los fallos que se le pidan:
 * conexión rechazada, respuesta troceada (chunked), cortada a los N bytes,
 * "Connection: close", plazo sin datos o cierre del keep-alive por el
 * servidor. Reloj virtual: now avanza solo con gf_advance (y cada lectura
 * sin datos avanza el plazo pedido, así un "plazo vencido" no espera de verdad).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gal_http.h"

typedef struct {
    char method[8];
    char path[512];
    char host[96];
    char cookie[96];
    char auth[96];
    char ctype[64];
    char range[48];
    char if_range[80];
    char part_sha[80];
    char agent[96];
    bool xflex;
    const uint8_t *body;
    size_t body_len;
} gf_req_t;

typedef struct {
    int status;
    char ctype[64];
    char extra[512];     /* cabeceras ya formadas */
    uint8_t *body;       /* el manejador lo reserva (malloc); gal_fake lo libera */
    size_t len;
    bool chunked;
    bool close;          /* Connection: close */
    long cut_after;      /* >= 0: cierra tras enviar estos bytes de la respuesta entera */
    bool stall;          /* no contesta (el cliente agota su plazo) */
    bool no_length;      /* sin Content-Length ni chunked: hasta el cierre */
} gf_resp_t;

typedef void (*gf_handler_t)(void *ud, const gf_req_t *rq, gf_resp_t *rs);

typedef struct {
    gf_handler_t handler;
    void *ud;
    uint32_t *clock;     /* reloj virtual compartido (ms) */
    /* fallos programados */
    int fail_connect;    /* >0: las próximas N conexiones fallan con fail_code */
    int fail_code;
    bool server_closes_idle; /* el servidor cierra la conexión tras cada respuesta sin decirlo */
    /* estado */
    bool open;
    char host[96];
    uint16_t port;
    bool tls;
    uint8_t *in;
    size_t in_n, in_cap;
    uint8_t *out;
    size_t out_n, out_p;
    bool close_after, stall, dead;
    /* contadores */
    int connects, requests, closes;
    uint64_t bytes_in, bytes_out;
    gf_req_t last;
} gf_conn_t;

void gf_init(gf_conn_t *c, gf_handler_t h, void *ud, uint32_t *clock);
void gf_free(gf_conn_t *c);
gal_io_t gf_io(gf_conn_t *c);

/* Ayudas para los manejadores */
void gf_json(gf_resp_t *rs, int status, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void gf_bytes(gf_resp_t *rs, int status, const char *ctype, const void *data, size_t n);
/** Valor de un parámetro de la consulta ("" si no está). */
void gf_query(const char *path, const char *key, char *out, size_t n);
/** Lee un archivo entero (malloc). */
uint8_t *gf_read_file(const char *path, size_t *n);
