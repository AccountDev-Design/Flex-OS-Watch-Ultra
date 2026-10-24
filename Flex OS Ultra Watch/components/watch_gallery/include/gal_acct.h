/*
 * gal_acct — Flex Account en el reloj: PORT a C del flujo de vinculación y
 * validación de FlexOS_Account.cpp (rama claude/flexos-ota-system-blq9g9-2csacx
 * @ 5dbc949), sobre gal_http. Es lo que da a la Galería la MISMA cuenta,
 * cuota y biblioteca de Flex Cloud que el P4 y Flex Cloud Web: no hay login
 * propio ni una cuenta del reloj.
 *
 * El flujo, igual que el P4:
 *  1. el reloj genera una credencial aleatoria (32 bytes, base64url de 43
 *     caracteres) y publica SOLO su SHA-256: POST /api/devices/code
 *     {hardwareId, label, model, flexVersion, tokenHash};
 *  2. la respuesta es un sobre firmado (ES256 con la clave anclada de Flex
 *     Developer Studio, keyId fijo): sin firma válida no se acepta el código;
 *  3. la persona escribe el código en la web; el reloj pregunta cada 3 s
 *     (GET /api/devices/code?code=&tokenHash=), 10 minutos como mucho;
 *  4. aprobado: se guarda la credencial (NVS, la marca "linked" la última) y
 *     se valida con GET /api/cloud/me (Bearer, TLS verificado).
 * RELOJ:
 *  - model "ESP32-S3" y hardwareId "S3-..." (el P4 manda "ESP32-P4" y
 *    "P4-..."): la cuenta distingue el reloj del P4;
 *  - todo va por TLS verificado con el paquete de CA de ESP-IDF (el P4 usa
 *    setInsecure() para el código, protegido por la firma);
 *  - la credencial no aparece nunca en el registro ni en la pantalla, se
 *    borra de la pila al usarla y solo sale hacia el servicio de sesión de
 *    Flex Cloud por "Authorization: Bearer".
 * Un 401/403 sin un código reconocido del servicio (la página de un proxy)
 * NO desvincula (flexAccountClassify).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gal_http.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Servicio real (FlexOS_Account.cpp / FlexOS_Cloud.h). */
#define GAL_ACCT_HOST        "flex-developer-studio.ralvarezsantos980.chatgpt.site"
#define GAL_ACCT_CODE_PATH   "/api/devices/code"
#define GAL_ACCT_ME_PATH     "/api/cloud/me"
#define GAL_ACCT_ACTIVATE    "https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code="
#define GAL_ACCT_MODEL       "ESP32-S3"
#define GAL_ACCT_LABEL       "Flex OS Watch Ultra"
#define GAL_ACCT_CODE_TTL_MS (10u * 60u * 1000u)
#define GAL_ACCT_POLL_MS     3000u
#define GAL_ACCT_SETTLE_MS   3000u
#define GAL_ACCT_REVALIDATE_MS (6u * 60u * 60u * 1000u)

/** Persistencia (NVS "flexacct" en el reloj; memoria en las pruebas). */
typedef struct {
    void *ctx;
    /** Lee la clave: true y *len = bytes si existe y cabe. */
    bool (*get)(void *ctx, const char *key, void *buf, size_t *len);
    bool (*set)(void *ctx, const char *key, const void *buf, size_t len);
    bool (*erase)(void *ctx, const char *key);
    void (*random)(void *ctx, uint8_t *out, size_t n);
} gal_store_t;

typedef enum {
    GAL_ACCT_UNLINKED = 0,
    GAL_ACCT_REQUESTING, /**< pidiendo el código */
    GAL_ACCT_CODE,       /**< código a la vista, esperando la aprobación */
    GAL_ACCT_LINKED,
    GAL_ACCT_EXPIRED,    /**< el CÓDIGO caducó (no la sesión) */
    GAL_ACCT_CANCELLED,
    GAL_ACCT_ERROR,
} gal_acct_flow_t;

typedef enum {
    GAL_LINK_UNLINKED = 0,
    GAL_LINK_LINKED,         /**< validada con el servidor */
    GAL_LINK_OFFLINE,        /**< guardada; sin Wi-Fi o aún sin validar */
    GAL_LINK_UNAVAILABLE,    /**< hay Wi-Fi pero el servicio no contesta: se reintenta */
    GAL_LINK_AUTH_REQUIRED,  /**< el servidor ya no reconoce la credencial */
    GAL_LINK_TOKEN_EXPIRED,
    GAL_LINK_ERROR,          /**< la cuenta guardada está incompleta */
} gal_link_t;

typedef struct {
    /* servicio (configurable para las pruebas) */
    char host[GAL_HTTP_HOST_MAX];
    uint16_t port;
    bool tls;
    const uint8_t *pubkey;   /**< 65 B SEC1; NULL = la de producción */
    const char *key_id;      /**< NULL = el de producción */
    char hw_mac[16];         /**< "%04X%08X" de la MAC */
    char version[24];
    /* lo guardado */
    bool have;
    char bearer[48];
    char address[48], display[64];
    uint8_t authst;          /**< 0 bien, 1 rechazada, 2 caducada */
    uint32_t install_id;
    /* flujo de vinculación */
    uint8_t flow;            /**< gal_acct_flow_t */
    bool want_link, cancel;
    char code[9];
    char activation[192];
    char pend_bearer[48], pend_hash[65];
    uint32_t flow_ms, poll_ms;
    char stage[72], error[144];
    /* validación */
    uint8_t link;            /**< gal_link_t */
    char link_detail[96];
    bool online, want_validate, verified;
    uint8_t fails;
    uint32_t wait_from, wait_ms, last_ok, last_reject;
    uint32_t epoch;          /**< sube al olvidar: un resultado en vuelo no escribe nada */
} gal_acct_t;

/** Prepara con el servicio de producción y carga lo guardado. */
void gal_acct_init(gal_acct_t *a, const gal_store_t *st, const char *mac_hex, const char *version);
/** Pruebas: otro servidor y otra clave de firma. */
void gal_acct_set_service(gal_acct_t *a, const char *host, uint16_t port, bool tls, const uint8_t pub[65],
                          const char *key_id);
/** Pide un código nuevo (se hace en la siguiente vuelta de la tarea). */
void gal_acct_request_link(gal_acct_t *a);
void gal_acct_cancel(gal_acct_t *a);
/** Desvincula este aparato: borra la credencial (no habla con el servidor). */
void gal_acct_forget(gal_acct_t *a, const gal_store_t *st);
/**
 * Una vuelta: enlace o validación si toca (bloquea en la red: va en la tarea
 * de la Galería). scratch: >= GAL_ACCT_SCRATCH bytes para el sobre y su
 * contenido. Devuelve los ms hasta la siguiente.
 */
#define GAL_ACCT_SCRATCH (16u * 1024u)
uint32_t gal_acct_step(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, bool wifi, uint32_t now, char *scratch,
                       size_t scap);
/** Flex Cloud contestó 401/403 con un código del servicio: se revalida ya (como mucho una vez por minuto). */
void gal_acct_report_rejected(gal_acct_t *a, uint32_t now);
/** ¿Se puede usar contra Flex Cloud ahora? */
bool gal_acct_usable(const gal_acct_t *a);
/** Copia la credencial (false si no hay). El llamante la borra al usarla. */
bool gal_acct_bearer(const gal_acct_t *a, char *out, size_t n);
const char *gal_link_text(uint8_t link);

/* ---- piezas puras (pruebas) ---- */
typedef enum { GAL_SESS_OK = 0, GAL_SESS_AUTH, GAL_SESS_EXPIRED, GAL_SESS_UNAVAILABLE } gal_sess_t;
gal_sess_t gal_acct_classify(int status, const char *code);
/** Espera entre validaciones fallidas: 30 s, 60 s, 2, 4, 8 min y después 15 min. */
uint32_t gal_acct_backoff_ms(uint8_t fails);
/** Decodifica base64url (con o sin '='). Devuelve bytes o -1. */
int gal_b64url_decode(const char *in, uint8_t *out, size_t cap);
/**
 * Abre un sobre {"schema":2,"algorithm":"ES256","keyId":...,"payload":...,"signature":...}
 * con la clave pub. payload recibe el JSON firmado (termina en 0). Devuelve
 * su longitud o -1 si la firma o el formato no son válidos.
 */
int gal_acct_open_envelope(const char *env, size_t n, const uint8_t pub[65], const char *key_id, char *payload,
                           size_t cap);
/** "x@flex" (FlexOS_Account validAddress). */
bool gal_acct_valid_address(const char *a);

#ifdef __cplusplus
}
#endif
