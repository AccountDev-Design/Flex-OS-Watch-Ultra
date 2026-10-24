/*
 * gal_posix_io — transporte gal_io_t con sockets TCP de verdad, para la prueba
 * de extremo a extremo contra los servidores reales (gallery_e2e.sh). Sin TLS:
 * los servidores de prueba escuchan en 127.0.0.1 por HTTP; el TLS lo pone en
 * el reloj esp-tls con el paquete de CA de ESP-IDF.
 * cut_after: > 0 corta la conexión tras leer ese número de bytes (una caída
 * a mitad de una descarga o de la respuesta a una parte).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gal_http.h"

typedef struct {
    int fd;
    long cut_after;       /* bytes de lectura tras los que se corta (0 = nunca) */
    long cut_write_after; /* bytes de escritura tras los que se corta (0 = nunca) */
    long rd, wr;          /* de la conexión actual */
    uint64_t total_rd, total_wr;
    int connects, cuts;
    bool (*cancelled)(void *ud);
    void *cancel_ud;
} pio_t;

void pio_init(pio_t *p);
gal_io_t pio_io(pio_t *p);
uint32_t pio_now_ms(void);
