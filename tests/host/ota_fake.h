/*
 * Servidor HTTPS y flash OTA falsos para el motor de ota_core: implementa
 * ota_io_t con un reloj virtual (un read sin datos hace correr el reloj, así
 * los 15 s sin datos se prueban sin esperar). También hace imágenes de
 * aplicación ESP32-S3 de prueba con su esp_app_desc_t, una clave ECDSA P-256
 * generada al ejecutar (nunca guardada) y manifiestos firmados con ella.
 * Lo usan tests/host/test_ota.c y la vista previa (tools/sim/sim_ota.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ota_core.h"

#define OTA_FAKE_ROUTES 4
#define OTA_FAKE_NOCUT  ((size_t)-1)

typedef struct {
    char url[OTA_URL_MAX];
    int open_err;          /* OTA_ERR_* al abrir (0 = 200 OK) */
    const uint8_t *body;
    size_t len;
    int64_t clen;          /* Content-Length anunciado (-1 = sin él) */
    size_t cut_at;         /* cierra la conexión tras N bytes */
    size_t stall_at;       /* deja de mandar tras N bytes (sin cerrar) */
    int chunk;             /* máximo por read (0 = lo pedido) */
} ota_fake_route_t;

typedef struct ota_fake {
    /* Entorno */
    bool online, mem_ok, charging;
    int batt_pct;
    uint32_t slot;
    int fail_begin, fail_finish;   /* OTA_ERR_* que devuelven (0 = bien) */
    long fail_write_at;            /* falla el write que pasa de N bytes (-1 = nunca) */
    ota_fake_route_t r[OTA_FAKE_ROUTES];
    int nr;
    /* Reloj virtual: ms por read con datos y por read vacío. ext_clock: la vista previa. */
    uint32_t now, data_ms, idle_ms;
    uint32_t (*ext_clock)(void);
    /* Conexión */
    int cur;
    size_t pos;
    bool is_open;
    /* Flash */
    uint8_t *flash;
    uint32_t fl_size, fl_written;
    bool fl_begun, fl_finished, fl_aborted;
    /* Registro */
    int opens, closes, begins, aborts, locks, unlocks, sleeps_ms;
    char last_url[OTA_URL_MAX];
    char pend[OTA_VER_MAX];
    /* Gancho tras cada read (cancelar, bajar la batería...) */
    void (*on_read)(struct ota_fake *f, void *ud);
    void *ud;
} ota_fake_t;

void ota_fake_init(ota_fake_t *f);
void ota_fake_free(ota_fake_t *f);
ota_fake_route_t *ota_fake_route(ota_fake_t *f, const char *url, const void *body, size_t len);
ota_io_t ota_fake_io(ota_fake_t *f);

/** Imagen de prueba de n bytes (cabecera, esp_app_desc_t y relleno determinista). */
void ota_fake_image(uint8_t *buf, size_t n, const char *project, const char *ver, uint16_t chip);

/** Genera (una vez) la clave de prueba y da la pública SEC1. */
void ota_fake_pubkey(uint8_t pub[65]);
/** Firma el mensaje canónico de la imagen con la clave de prueba (128 hex + 0). */
void ota_fake_sign(const char *ver, const uint8_t *img, size_t n, char sighex[OTA_SIG_HEX + 1]);
/** Manifiesto con un bloque OTA_PRODUCT; md5/sig NULL = sin el campo; size 0 = sin él. */
size_t ota_fake_manifest(char *out, size_t cap, const char *ver, const char *url, uint32_t size, const char *md5,
                         const char *sig, const char *changelog);
