/*
 * gal_fake_eco — Flex OS Ultra (Flex Web Server) y Flex Cloud en memoria,
 * con las MISMAS rutas, formas JSON, códigos y reglas de seguridad que el
 * código real (FlexOS_MediaWeb.cpp y cloud/src/http/app.js): Host = ip:puerto,
 * cookie "fxs", X-Flex en lo que cambia algo, código de 6 cifras con 5
 * intentos, protegidos sin nivel de propietario, Bearer de dispositivo,
 * subidas por partes con X-Part-SHA256, Range/If-Range...
 * Es para probar el motor y la interfaz sin red; que los contratos son los
 * reales lo demuestra tests/host/gallery_e2e.sh contra el servidor del P4
 * compilado en el PC y el servidor Node de Flex Cloud.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gal_fake.h"

#define ECO_MAX 64

typedef struct {
    bool used;
    uint32_t id;          /* P4: número; Flex Cloud: índice -> "fil_..." */
    char name[64];
    char kind[8];         /* "photo" "video" "audio" */
    char fmt[24];         /* "JPEG", "JPEG progresivo", "PNG"... (P4) */
    char mime[32];
    uint8_t *data;
    size_t size;
    uint8_t *thumb;
    size_t thumb_len;
    char thumb_mime[16];
    bool locked;
    bool trashed;
    uint64_t when;        /* s (P4) o ms (nube) */
    char sha[65];
} eco_file_t;

typedef struct {
    bool used;
    char id[32];
    char name[64];
    uint64_t size, chunk;
    uint32_t parts;
    uint8_t got[64];
    uint8_t *data;
    char sha[65];
    char key[128];
    char state[12];
    char file_id[32];
} eco_upload_t;

typedef struct {
    uint32_t clock;
    /* --- Flex OS Ultra --- */
    char p4_ip[24];
    uint16_t p4_port;
    char code[8];
    char session[33];     /* "" = sin sesión */
    int pair_fails;
    uint32_t rev;
    eco_file_t p4[ECO_MAX];
    uint32_t p4_next;
    char phone_state[12]; /* "ready", "off", "none" */
    bool p4_down;         /* el Flex Web Server está apagado */
    bool p4_busy;         /* contesta 503 */
    int p4_uploads, p4_deletes;
    /* --- Flex Cloud --- */
    char bearer[48];      /* credencial válida ("" = ninguna) */
    bool cloud_down, cloud_500;
    bool revoked;         /* 401 device_revoked */
    eco_file_t cl[ECO_MAX];
    eco_upload_t up[8];
    int cl_next, up_next;
    uint64_t quota_total, quota_used;
    int page;             /* elementos por página (0 = lo que pida el cliente) */
    int cut_download_at;  /* >0: la próxima descarga se corta a los N bytes del cuerpo */
    int cut_part;         /* >0: la parte N se corta a mitad la primera vez */
    int corrupt_download; /* >0: la próxima descarga entrega bytes cambiados */
    int range_requests, part_puts, completes, thumbs_served;
    /* --- teléfono (pasarela /api/cloud del P4) --- */
    eco_file_t ph[ECO_MAX];
    /* conexiones */
    gf_conn_t lan, cloud;
} eco_t;

void eco_init(eco_t *e);
void eco_free(eco_t *e);
/** Añade una foto al P4 / a Flex Cloud / al teléfono (copia data). Devuelve su id. */
uint32_t eco_p4_add(eco_t *e, const char *name, const char *kind, const char *fmt, const void *data, size_t n,
                    const void *thumb, size_t tn, bool locked);
int eco_cloud_add(eco_t *e, eco_file_t *tab, const char *name, const char *mime, const void *data, size_t n,
                  const void *thumb, size_t tn, const char *thumb_mime);
eco_file_t *eco_find_p4(eco_t *e, uint32_t id);
eco_file_t *eco_find_cl(eco_t *e, eco_file_t *tab, const char *fid);
void eco_sha_hex(const void *d, size_t n, char out[65]);
