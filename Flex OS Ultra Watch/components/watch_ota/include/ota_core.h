/*
 * ota_core — el OTA de Flex OS Ultra (FlexOS_OTA.h/.cpp) en C puro, sin
 * ESP-IDF, probado en el PC (tests/host/test_ota.c).
 *
 * Qué conserva de la referencia:
 *  - campos del manifiesto (version, url, size, md5, changelog, sig, sigalg),
 *    orden de decisiones de otaCheck(), códigos de error 0-21 con sus números,
 *    estados 0-10, límites (64 KiB mínimo, 15 s sin datos, bloques de 4 KB),
 *    progreso y velocidad (media 3:1 cada 500 ms) y la instalación paso a
 *    paso de otaInstall(), con abort ante cualquier fallo;
 *  - la instalación solo la pide el usuario; la comprobación no instala nada.
 *
 * Cambios del reloj (RELOJ:, explicados en docs/F7-OTA.md):
 *  - versión SemVer con preliberación: "0.1.0-b1" vale y es menor que "0.1.0";
 *  - JSON leído de verdad: el campo se busca en su nivel (no dentro de un
 *    objeto anidado) y un fichero inválido no se da por bueno; un valor que no
 *    cabe es un error, nunca un recorte silencioso (salvo las novedades, que
 *    solo se enseñan y se cortan sin partir un carácter);
 *  - firma obligatoria ECDSA P-256 sobre un mensaje canónico con producto,
 *    versión, tamaño y SHA-256 de la imagen (ota_sig.h); sin clave de
 *    confianza no se instala;
 *  - la imagen tiene que ser de este producto, de un ESP32-S3 y de la versión
 *    que dice el manifiesto (cabecera y esp_app_desc_t), y su tamaño el del
 *    manifiesto y caber en la ranura (no 8 MiB fijos);
 *  - batería: no empieza por debajo del 30 % sin cargador y aborta al 10 %;
 *  - textos con tildes (UTF-8), como el resto del reloj.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clave del bloque del manifiesto y project_name de la imagen. */
#define OTA_PRODUCT        "flex_os_watch_ultra"
#define OTA_SIGALG         "ecdsa-p256-sha256"

#define OTA_MANIFEST_MAX   4096  /* FLEXOS_OTA_MANIFEST_MAX (P4/S3) */
#define OTA_CHUNK          4096  /* FLEXOS_OTA_CHUNK (P4/S3) */
#define OTA_MIN_BIN        65536u
#define OTA_STALL_MS       15000 /* FLEXOS_OTA_STALL_TIMEOUT */
#define OTA_VER_MAX        24    /* 23 caracteres */
#define OTA_URL_MAX        256   /* 255 */
#define OTA_LOG_MAX        512   /* novedades (la referencia: 320) */
#define OTA_SIG_HEX        128   /* r||s, 64 B */
#define OTA_HEAD_LEN       112   /* cabecera de imagen + esp_app_desc_t hasta project_name */
#define OTA_BATT_START_PCT 30
#define OTA_BATT_ABORT_PCT 10

/* enum FlexOtaState: mismos números. */
typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,
    OTA_CONNECTING,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_INSTALLING,
    OTA_FINALIZING,
    OTA_REBOOTING,
    OTA_ERROR,
} ota_state_t;

/* enum FlexOtaError: 0-21 de la referencia; los del reloj, detrás. */
typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_NO_WIFI,
    OTA_ERR_LOWMEM,
    OTA_ERR_HTTPS,
    OTA_ERR_TLS,
    OTA_ERR_HTTP_STATUS,
    OTA_ERR_TIMEOUT,
    OTA_ERR_JSON,
    OTA_ERR_NO_BLOCK,
    OTA_ERR_BAD_VERSION,
    OTA_ERR_BAD_URL,
    OTA_ERR_BAD_SIZE,
    OTA_ERR_NO_PARTITION,
    OTA_ERR_BEGIN,
    OTA_ERR_WRITE,
    OTA_ERR_TRUNCATED,
    OTA_ERR_CORRUPT,
    OTA_ERR_CANCELLED,
    OTA_ERR_NO_CA,
    OTA_ERR_NO_SIG,
    OTA_ERR_BAD_SIG,
    OTA_ERR_ROLLBACK,
    /* RELOJ: */
    OTA_ERR_NO_KEY,      /* el firmware no lleva clave de confianza */
    OTA_ERR_WRONG_IMAGE, /* otro producto, otro chip u otra versión */
    OTA_ERR_BATTERY,
    OTA_ERR_COUNT
} ota_err_t;

/* ---------------- Versiones ---------------- */

typedef struct {
    uint16_t v[3];
    char pre[OTA_VER_MAX]; /* "" = versión final */
} ota_ver_t;

/** X.Y.Z (0..65535, ceros a la izquierda admitidos) y, RELOJ, "-pre" SemVer
 *  (identificadores [0-9A-Za-z-] separados por puntos, no vacíos). */
bool ota_ver_parse(const char *s, ota_ver_t *out);
/** -1, 0 o 1. Con preliberación, la final es mayor (SemVer 11). */
int ota_ver_cmp(const ota_ver_t *a, const ota_ver_t *b);

/* ---------------- Utilidades ---------------- */

/** otaMd5Usable: 32 hex, no todo ceros. */
bool ota_md5_usable(const char *s);
/** otaFmtBytes: "%u B", "%u KB", "%u,%u MB" (la décima truncada). */
void ota_fmt_bytes(uint32_t b, char *out, size_t n);
/** n bytes de hex (mayúsculas o minúsculas) a bin. false si no es hex o no mide 2n. */
bool ota_hex_decode(const char *hex, uint8_t *out, size_t n);
void ota_hex_encode(const uint8_t *in, size_t n, char *out); /* out: 2n+1 */

const char *ota_err_text(int err);
const char *ota_phase_text(int st);
bool ota_busy_state(int st);

/* ---------------- Manifiesto ---------------- */

typedef struct {
    char ver[OTA_VER_MAX];
    char url[OTA_URL_MAX];
    uint32_t size;               /* 0 = no viene */
    char md5[33];
    bool md5_ok;                 /* ota_md5_usable() */
    char changelog[OTA_LOG_MAX];
    char sig[OTA_SIG_HEX + 1];   /* "" = sin firma */
} ota_info_t;

typedef struct {
    bool require_sig;  /* FLEXOS_OTA_REQUIRE_SIGNATURE (el reloj: siempre) */
    bool antirollback; /* FLEXOS_OTA_ANTIROLLBACK (el reloj: no) */
} ota_opts_t;

/**
 * otaCheck() sin red. Devuelve OTA_ERR_NONE con *available (o "al día") o el
 * error. out se rellena solo con *available.
 */
int ota_parse_manifest(const char *buf, size_t len, const char *key, const char *local_ver,
                       const ota_opts_t *opts, ota_info_t *out, bool *available);

/** Mensaje que se firma (sin terminador): "flexos-ota-v1\n<producto>\n<versión>\n<tamaño>\n<sha256 hex>". */
size_t ota_sign_message(const char *product, const char *ver, uint32_t size, const uint8_t sha[32], char *out,
                        size_t n);

/** Cabecera de imagen ESP32-S3 + esp_app_desc_t: producto y versión. OTA_ERR_NONE o OTA_ERR_WRONG_IMAGE. */
int ota_image_check(const uint8_t *head, size_t n, const char *project, const char *ver);

/* ---------------- Arranque ---------------- */

typedef enum { OTA_BOOT_NONE = 0, OTA_BOOT_UPDATED, OTA_BOOT_REVERTED } ota_boot_t;
/** flexota/pend al arrancar: pend == versión en marcha → actualizado; si no, con una ranura inválida → revertido. */
int ota_boot_notice(const char *pend, const char *running, bool has_invalid);

/* ---------------- Comprobación automática ---------------- */

#define OTA_AUTO_FIRST_MS  20000u          /* tras conectar el Wi-Fi */
#define OTA_AUTO_EVERY_MS  (24u * 3600000u)

typedef struct {
    bool was_online;
    uint32_t online_since;
    bool checked;      /* ya hubo una comprobación en este arranque */
    uint32_t last;
} ota_auto_t;

/** true si toca comprobar ahora (y lo apunta). Solo con Wi-Fi y en IDLE/UP_TO_DATE/ERROR. */
bool ota_auto_due(ota_auto_t *a, uint32_t now, bool online, int state);

/* ---------------- Motor ---------------- */

/* Transporte y flash abstractos: watch_ota.c (ESP-IDF), el doble de las
 * pruebas y la vista previa. Todo se llama desde la tarea del OTA. */
typedef struct {
    void *ctx;
    uint32_t (*now)(void *ctx);
    void (*sleep)(void *ctx, uint32_t ms);
    bool (*online)(void *ctx);
    bool (*mem_ok)(void *ctx);
    /** GET https. 0 = respuesta 200 y *len = Content-Length (-1 si no viene); si no, OTA_ERR_*. */
    int (*open)(void *ctx, const char *url, int64_t *len);
    /** >0 bytes leídos; 0 = nada todavía (ya ha esperado un poco); <0 = conexión cerrada. */
    int (*read)(void *ctx, uint8_t *buf, int n);
    void (*close)(void *ctx);
    uint32_t (*slot_size)(void *ctx); /* 0 = sin ranura OTA libre */
    int (*fl_begin)(void *ctx, uint32_t size);              /* 0 u OTA_ERR_BEGIN */
    int (*fl_write)(void *ctx, const uint8_t *d, int n);    /* 0 u OTA_ERR_WRITE */
    int (*fl_finish)(void *ctx);                            /* valida y fija el arranque: 0 u OTA_ERR_CORRUPT */
    void (*fl_abort)(void *ctx);
    void (*battery)(void *ctx, int *pct, bool *charging);  /* pct < 0 = no se sabe */
    void (*save_pend)(void *ctx, const char *ver);
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} ota_io_t;

typedef struct {
    /* Configuración (ota_init) */
    char key[32];
    char local[OTA_VER_MAX];
    char url[OTA_URL_MAX];        /* del manifiesto */
    ota_opts_t opts;
    uint8_t pubkey[65];
    bool have_key;
    /* Publicado: se escribe con io->lock (o el cerrojo del servicio) */
    uint8_t state;
    uint8_t err;
    bool has_upd;
    ota_info_t info;
    uint32_t total, done, bps;
    uint8_t pct;
    uint32_t gen;                 /* sube con cada cambio de estado, error o datos */
    /* Órdenes */
    volatile bool cancel;
    /* Búferes (el ota_t vive en PSRAM) */
    ota_info_t scratch;           /* lo que se está leyendo: nunca lo publicado */
    char manifest[OTA_MANIFEST_MAX];
    uint8_t chunk[OTA_CHUNK];
    uint8_t head[OTA_HEAD_LEN];
} ota_t;

/** pubkey: SEC1 sin comprimir (65 B) o NULL si el firmware no lleva clave. */
void ota_init(ota_t *o, const char *key, const char *local_ver, const char *manifest_url, const uint8_t *pubkey);

/* Desde la interfaz, con el cerrojo del servicio tomado. true = hay que despertar a la tarea. */
bool ota_req_check(ota_t *o, bool online);
bool ota_req_install(ota_t *o, bool online);
void ota_req_cancel(ota_t *o);

/* En la tarea. ota_run_install termina en OTA_REBOOTING (el servicio reinicia) o en OTA_ERROR. */
void ota_run_check(ota_t *o, const ota_io_t *io);
void ota_run_install(ota_t *o, const ota_io_t *io);

/** flexOtaStatusText(): texto corto para la fila de Ajustes. */
void ota_status_text(const ota_t *o, char *out, size_t n);

#ifdef __cplusplus
}
#endif
