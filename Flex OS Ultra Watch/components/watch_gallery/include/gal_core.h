/*
 * gal_core — el modelo de la Galería del reloj, sin red ni sistema operativo
 * (lo usan el reloj, tests/host y la vista previa).
 *
 * DE DÓNDE SALE CADA FOTO (docs/GALERIA.md). Ninguna biblioteca es nueva:
 *  - GAL_SRC_LOCAL: /Imagenes y /Paint del LittleFS del reloj (mismas
 *    carpetas que FlexOS_MediaLib y FlexOS_FS del P4);
 *  - GAL_SRC_ULTRA: la biblioteca de medios de Flex OS Ultra, por su Flex
 *    Web Server (FlexOS_MediaWeb, puerto 8080) en la misma Wi-Fi, con la
 *    sesión que da el código de 6 cifras que enseña el P4;
 *  - GAL_SRC_CLOUD: Flex Cloud (/api/cloud de Flex Developer Studio) con la
 *    credencial de dispositivo de Flex Account: la MISMA cuenta, cuota y
 *    biblioteca que el P4 y Flex Cloud Web;
 *  - GAL_SRC_PHONE: el Flex Cloud del teléfono (Flex Storage), a través de
 *    la pasarela /api/cloud del P4. El teléfono solo empareja un P4
 *    (CloudServer.kt: pairing.load()), así que el reloj no abre sesión con
 *    él directamente; solo existe si el P4 dice que su teléfono está listo.
 *
 * El modo Automático elige UNA fuente remota con reglas fijas
 * (gal_pick_resolve) y dice cuál; las demás opciones respetan la elección
 * y nunca caen a otra por detrás.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAL_ID_MAX     40   /* "fil_" + 24 (Flex Cloud) o un número (P4) */
#define GAL_NAME_MAX   64   /* FML_NAME_MAX */
#define GAL_PATH_MAX   96   /* FL_PATH_MAX */
#define GAL_SHA_HEX    65
#define GAL_LIST_MAX   200  /* FLEX_CLOUD_LIST_MAX del P4 */
#define GAL_PAGE       60   /* elementos por página de Flex Cloud */

#define GAL_DIR_PHOTO  "/Imagenes"          /* FML_DIR_PHOTO */
#define GAL_DIR_PAINT  "/Paint"             /* FFS_DIR_PAINT */
#define GAL_DIR_SYS    "/System/Galeria"
#define GAL_DIR_THUMB  "/System/Galeria/th" /* miniaturas de las fotos del reloj */
#define GAL_DIR_TMP    "/System/Galeria/tmp"/* descargas a medias (.part) */
#define GAL_ORIGIN     "/System/Galeria/origen.txt" /* de dónde vino cada descarga */

/* Fuentes */
typedef enum {
    GAL_SRC_NONE = -1,
    GAL_SRC_LOCAL = 0,
    GAL_SRC_ULTRA,
    GAL_SRC_CLOUD,
    GAL_SRC_PHONE,
    GAL_SRC_N
} gal_src_t;

/* Lo que elige la persona en Origen */
typedef enum { GAL_PICK_AUTO = 0, GAL_PICK_ULTRA, GAL_PICK_CLOUD, GAL_PICK_PHONE, GAL_PICK_N } gal_pick_t;

/* Clases (los mismos valores que FML_K_*) */
enum { GAL_K_NONE = 0, GAL_K_PHOTO = 1, GAL_K_VIDEO = 2, GAL_K_DRAW = 4 };

/* Formatos: lo que el reloj sabe abrir es JPEG baseline (TJpgDec) y los
 * dibujos de Paint; lo demás se enseña con su motivo, nunca con una imagen inventada. */
typedef enum {
    GAL_F_UNKNOWN = 0,
    GAL_F_JPEG,
    GAL_F_JPEG_PROG,
    GAL_F_PNG,
    GAL_F_GIF,
    GAL_F_BMP,
    GAL_F_WEBP,
    GAL_F_HEIC,
    GAL_F_AVIF,
    GAL_F_VIDEO,
    GAL_F_FXP,
    GAL_F_N
} gal_fmt_t;

/* Banderas de un elemento */
#define GAL_I_LOCKED  0x01 /* protegido en el P4: ni nombre ni miniatura sin la clave */
#define GAL_I_THUMB   0x02 /* el servidor tiene miniatura */
#define GAL_I_BUSY    0x04 /* el P4 todavía lo está comprobando */
#define GAL_I_DAMAGED 0x08 /* el P4 dice que está dañado */
#define GAL_I_ONWATCH 0x10 /* (remoto) ya hay copia en el reloj */
#define GAL_I_REMOTE  0x20 /* (local) es la copia de uno remoto (origen.txt) */

typedef struct {
    char id[GAL_ID_MAX];      /**< P4 "17" · Flex Cloud "fil_..." · local "" */
    char name[GAL_NAME_MAX];  /**< para enseñar (UTF-8) */
    char path[GAL_PATH_MAX];  /**< local: ruta virtual ("/Imagenes/x.jpg") */
    char sha256[GAL_SHA_HEX]; /**< Flex Cloud: huella del original (hex) */
    uint64_t size;
    int64_t when;             /**< ms desde 1970 (0 = no se sabe) */
    uint32_t tver;            /**< versión de la miniatura (P4 "t", mtime local) */
    uint16_t w, h;
    uint32_t dur_ms;
    uint8_t src, kind, fmt, flags;
    int8_t origin_src;        /**< local con GAL_I_REMOTE: de qué fuente vino */
    char origin_id[GAL_ID_MAX];
} gal_item_t;

/* ---------- formatos ---------- */
gal_fmt_t gal_fmt_from_name(const char *name);
gal_fmt_t gal_fmt_from_mime(const char *mime);
/** El campo "fmt" de la biblioteca del P4 ("JPEG", "JPEG progresivo", "PNG"...). */
gal_fmt_t gal_fmt_from_ultra(const char *fmt);
/** ¿El reloj lo puede abrir? */
bool gal_fmt_viewable(gal_fmt_t f);
/** Por qué no se abre ("PNG: el reloj solo muestra JPEG"), o NULL si se abre. */
const char *gal_fmt_why(gal_fmt_t f);
const char *gal_fmt_name(gal_fmt_t f);
int gal_kind_of(gal_fmt_t f);

/* ---------- estado de una fuente ---------- */
typedef enum {
    GAL_ST_UNSET = 0, /**< sin configurar (sin P4 emparejado, sin cuenta, P4 sin teléfono) */
    GAL_ST_NOWIFI,
    GAL_ST_CHECKING,  /**< comprobando */
    GAL_ST_READY,
    GAL_ST_PAIR,      /**< hay que volver a emparejar o vincular */
    GAL_ST_DOWN,      /**< no contesta: se reintenta */
} gal_state_t;

typedef struct {
    uint8_t state;     /**< gal_state_t */
    uint8_t fails;     /**< fallos seguidos */
    uint32_t next_ms;  /**< próxima comprobación */
    uint32_t ready_ms; /**< desde cuándo está READY (para la estabilidad del modo Automático) */
    char detail[96];   /**< motivo legible */
} gal_health_t;

/** Comprobación correcta / fallida (con la espera de gal_backoff_ms). */
void gal_health_ok(gal_health_t *h, uint32_t now, uint32_t recheck_ms);
void gal_health_fail(gal_health_t *h, uint8_t state, const char *why, uint32_t now);

/* ---------- Automático ---------- */
#define GAL_AUTO_STABLE_MS 10000 /* una fuente preferida que vuelve tiene que aguantar 10 s */

typedef struct {
    int8_t src;    /**< gal_src_t remota o GAL_SRC_NONE */
    bool automatic;
    char why[112]; /**< qué fuente y por qué (o por qué ninguna) */
} gal_choice_t;

/**
 * Reglas (docs/GALERIA.md §4):
 *  - sin Wi-Fi: ninguna (solo lo del reloj);
 *  - Flex OS Ultra / Flex Cloud / Flex Phone: esa, esté como esté (su
 *    estado se enseña); nunca otra por detrás;
 *  - Automático, por orden: Flex OS Ultra lista (red local, directa), Flex
 *    Cloud lista (Internet) y Flex Phone lista (por el P4). La que está en
 *    uso se conserva mientras siga lista; una de más prioridad que vuelve
 *    solo la sustituye tras GAL_AUTO_STABLE_MS. Si ninguna está lista, la
 *    primera que se está comprobando, y si no, la primera configurada, con su
 *    motivo.
 * prev = la que se usaba (GAL_SRC_NONE al empezar).
 */
void gal_pick_resolve(gal_pick_t pick, const gal_health_t h[GAL_SRC_N], bool wifi, int prev, uint32_t now,
                      gal_choice_t *out);
const char *gal_src_name(int src);
const char *gal_pick_name(gal_pick_t p);
const char *gal_state_name(uint8_t st);

/* ---------- errores ---------- */
typedef enum {
    GAL_E_OK = 0,
    GAL_E_NOWIFI,
    GAL_E_NET,      /**< no conecta, se corta o no contesta */
    GAL_E_TLS,
    GAL_E_PROTO,    /**< respuesta ilegible o incompleta */
    GAL_E_AUTH,     /**< sesión del P4 caducada, credencial rechazada */
    GAL_E_LOCKED,   /**< protegido en el P4 */
    GAL_E_NOTFOUND,
    GAL_E_QUOTA,
    GAL_E_TOOBIG,
    GAL_E_RATE,
    GAL_E_BUSY,     /**< 503: el servidor está ocupado */
    GAL_E_SERVER,   /**< 5xx */
    GAL_E_NOSPACE,  /**< el reloj no tiene sitio */
    GAL_E_NOMEM,
    GAL_E_INTEGRITY,/**< tamaño, SHA-256 o CRC que no cuadra */
    GAL_E_CANCEL,
    GAL_E_FORMAT,   /**< el reloj no abre ese formato */
    GAL_E_FS,       /**< no se pudo leer o escribir en el reloj */
    GAL_E_ARG,
    GAL_E_EXPIRED,  /**< subida caducada en el servidor */
    GAL_E_N
} gal_err_t;

/**
 * Estado HTTP (o GAL_HTTP_E_*). code: el error.code de una respuesta de Flex
 * Cloud ("" si no lo trajo), o NULL si la respuesta es del Flex Web Server del
 * P4. Un 401/403 de Flex Cloud sin un código suyo no es una sesión perdida.
 */
gal_err_t gal_err_from_http(int status, const char *code);
/** ¿Se reintenta sola (con espera)? */
bool gal_err_retry(gal_err_t e);
const char *gal_err_text(gal_err_t e);
/** Espera antes del reintento número fails (1, 2...): 2, 4, 8, 16, 32 s y después 60 s. */
uint32_t gal_backoff_ms(uint8_t fails);
#define GAL_XFER_TRIES 6 /* fallos seguidos de una transferencia antes de darla por fallida */

/* ---------- lectores de las respuestas reales ---------- */

/** GET /api/hello del P4. dev tiene que ser "Flex OS Ultra" (no se habla con otro servidor). */
typedef struct {
    bool paired, owner, up;
    int lock_type;
    char dev[32];
} gal_hello_t;
bool gal_parse_hello(const char *j, size_t n, gal_hello_t *out);
bool gal_hello_is_ultra(const gal_hello_t *h);

/**
 * GET /api/library del P4 (flexMlJsonLibrary). Fotos y vídeos; un
 * protegido sin nivel de propietario es solo {"id":N,"lock":1}. Con
 * "same":1 (?since=rev sin cambios) *same = true y no toca out.
 * Devuelve cuántos dejó en out (<0 = respuesta no válida).
 */
int gal_parse_ultra_library(const char *j, size_t n, gal_item_t *out, int cap, uint32_t *rev, bool *same);

/** GET /api/fs/overview del P4: el teléfono de Flex Storage. */
typedef struct {
    char state[12]; /**< "ready", "off", "rejected", "none" */
    char name[48];
    char model[32];
} gal_phone_t;
bool gal_parse_overview(const char *j, size_t n, gal_phone_t *out);

/** GET /api/cloud/files (Flex Cloud o la pasarela): solo archivos photo/video. */
int gal_parse_cloud_list(const char *j, size_t n, uint8_t src, gal_item_t *out, int cap, char *cursor, size_t ccap);

/** GET /api/cloud/me */
typedef struct {
    char address[48], display[64];
    char account_id[48];
    char device_kind[16];
    uint64_t q_total, q_used, q_avail;
    char q_state[8];
    bool limited_by_device;
    uint64_t chunk_min, chunk_max, chunk_def, max_file;
} gal_me_t;
bool gal_parse_cloud_me(const char *j, size_t n, gal_me_t *out);
/** {"ok":false,"error":{"code":...,"message":...}}. false si no es un error de Flex Cloud. */
bool gal_parse_cloud_error(const char *j, size_t n, char *code, size_t cap, char *msg, size_t mcap);

/** POST /uploads, GET /uploads/:id */
#define GAL_UP_PARTS_MAX 512
typedef struct {
    char id[GAL_ID_MAX];
    uint64_t chunk;
    uint32_t parts;
    uint8_t got[GAL_UP_PARTS_MAX / 8]; /**< partes recibidas (bit n-1) */
    uint32_t got_n;
    char state[12];
    bool resumed;
    char file_id[GAL_ID_MAX];
    char sha256[GAL_SHA_HEX];
} gal_upload_t;
bool gal_parse_cloud_upload(const char *j, size_t n, gal_upload_t *out);
bool gal_upload_has(const gal_upload_t *u, uint32_t part);
/** POST /uploads/:id/complete: {"file":{...},"quota":{...}} -> id y SHA-256 del archivo. */
bool gal_parse_cloud_complete(const char *j, size_t n, char *file_id, size_t fcap, char *sha, size_t scap);
/** Tamaño de parte para un archivo: 256 KB como el P4, más si pasaría de 512 partes, dentro de [lo, hi]. */
uint64_t gal_chunk_for(uint64_t size, uint64_t lo, uint64_t hi);

/* ---------- vista (pestañas) ---------- */
typedef enum { GAL_TAB_ALL = 0, GAL_TAB_WATCH, GAL_TAB_REMOTE, GAL_TAB_N } gal_tab_t;
typedef struct {
    uint8_t list; /**< 0 = local, 1 = remota */
    uint16_t idx;
} gal_ref_t;
/**
 * Elementos de una pestaña, del más reciente al más antiguo. En "Todas", un
 * remoto del que ya hay copia en el reloj sale una sola vez (la copia, con
 * la marca de su origen). Marca GAL_I_ONWATCH en los remotos con copia.
 */
int gal_view_build(const gal_item_t *loc, int nl, gal_item_t *rem, int nr, gal_tab_t tab, gal_ref_t *out, int cap);

/* ---------- origen de las descargas (origen.txt) ---------- */
typedef struct {
    int8_t src;
    char id[GAL_ID_MAX];
    char name[GAL_NAME_MAX]; /**< nombre en /Imagenes */
    uint64_t size;
} gal_origin_t;
/** Una línea "src\tid\tsize\tnombre". false si no es válida. */
bool gal_origin_parse(const char *line, gal_origin_t *out);
bool gal_origin_format(const gal_origin_t *o, char *out, size_t n);
/** Marca en loc las copias de remotos (GAL_I_REMOTE) según la tabla. */
void gal_origin_apply(gal_item_t *loc, int nl, const gal_origin_t *tab, int nt);

/* ---------- nombres ---------- */
/** Copia con el final ('\0') siempre puesto, recortando sin partir un carácter UTF-8. */
void gal_copyz(char *d, size_t n, const char *s);
/**
 * Nombre para guardar en el reloj: sin '/', '\\', '@' ni controles, sin
 * espacios ni puntos al principio, recortado a FL_NAME_MAX sin partir un
 * carácter y conservando la extensión. "" -> "Foto.jpg".
 */
void gal_safe_name(const char *in, char *out, size_t n);
/** Variante n (>= 2) de un nombre: "foto.jpg" -> "foto (2).jpg", sin pasar de FL_NAME_MAX. */
void gal_name_variant(const char *name, int n, char *out, size_t cap);

/* ---------- textos ---------- */
/** "12 elementos", "1 elemento" */
void gal_count_text(int n, char *out, size_t cap);
/** "3,4 MB" con tamaños de más de 4 GB. */
void gal_size_text(uint64_t b, char *out, size_t cap);
/** Hex en minúsculas. */
void gal_hex(const uint8_t *b, size_t n, char *out);
/** CRC-32 de zlib (el de flexMlCrc32 del P4). */
uint32_t gal_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif
