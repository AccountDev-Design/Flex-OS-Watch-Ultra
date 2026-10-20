/*
 * Servicio FBP/1 falso, en memoria y a nivel de BYTES: atiende el HTTP
 * Upgrade con su Sec-WebSocket-Accept, desenmascara las tramas del cliente,
 * contesta con tramas WebSocket reales y habla FBP/1 como server/ de Flex OS
 * Ultra (WELCOME tras un HELLO con la credencial buena, STATE + NAVIGATED +
 * FRAME al navegar, PONG, ERROR 1 con otra credencial). Implementa br_io_t
 * con un reloj virtual: un read sin datos hace correr el reloj, así los
 * plazos de la sesión (40 ms, 10 s, 12 s...) se prueban sin esperar.
 * Lo usan tests/host/test_br_net.c y la vista previa (tools/sim/sim_br.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "br_net.h"

typedef struct fbp_fake fbp_fake_t;

/** Imagen que se manda al navegar o desplazar: offset de desplazamiento -> JPEG. */
typedef const uint8_t *(*fbp_fake_img_cb)(void *user, int scroll_y, uint32_t *len, uint16_t *w, uint16_t *h);

typedef struct {
    const char *token;      /* credencial que acepta */
    const char *session_id; /* id del WELCOME ("flexphone-x.r6", "sess-1"...) */
    bool refuse_connect;    /* connect() falla */
    bool tls_bad;           /* connect() con TLS da error de certificado */
    bool bad_accept;        /* contesta el Upgrade con una clave equivocada */
    bool mute_hello;        /* no contesta al HELLO */
    bool no_frames;         /* navega pero no manda imágenes */
    bool bands;             /* manda la página en 2 bandas (como el PC) */
    uint32_t chunk;         /* entrega lo pendiente en trozos de N B por read (0 = todo) */
    uint32_t push_ms;       /* como el relay de Flex Phone: un FRAME nuevo cada N ms sin que
                               nadie lo pida, tras la primera navegación (0 = solo al pedirlo) */
    fbp_fake_img_cb img;
    void *img_user;
    /** Reloj externo (la vista previa: lv_tick). NULL = reloj virtual. */
    uint32_t (*clock)(void);
} fbp_fake_cfg_t;

typedef struct {
    int connects, hellos, navigates, scrolls, taps, keys, viewports, acks, pings, backs, reloads, req_frames;
    uint16_t hello_w, hello_h, vp_w, vp_h;
    uint8_t vp_quality, vp_scale;
    char last_url[512];
    char typed[256];
    uint8_t last_vk;
    uint16_t tap_x, tap_y;
    int scroll_y;
    char last_token[96];
    char ua[128];
    char last_host[64];     /* adónde se conectó el reloj */
    uint16_t last_port;
    bool last_tls;
    int pushes;             /* FRAME mandados por iniciativa del servicio */
} fbp_fake_log_t;

fbp_fake_t *fbp_fake_new(const fbp_fake_cfg_t *cfg);
void fbp_fake_free(fbp_fake_t *f);
const br_io_t *fbp_fake_io(fbp_fake_t *f);
fbp_fake_log_t *fbp_fake_log(fbp_fake_t *f);
fbp_fake_cfg_t *fbp_fake_cfg(fbp_fake_t *f);
/** Corta la conexión desde el servicio (CLOSE). */
void fbp_fake_drop(fbp_fake_t *f);
/** Manda un mensaje FBP cualquiera (pruebas). */
void fbp_fake_send(fbp_fake_t *f, uint8_t type, uint8_t ch, const uint8_t *payload, uint32_t n);
/** Reloj virtual (ms). */
uint32_t fbp_fake_now(fbp_fake_t *f);
void fbp_fake_advance(fbp_fake_t *f, uint32_t ms);
