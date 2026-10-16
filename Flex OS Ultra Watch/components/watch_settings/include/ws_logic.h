/*
 * ws_logic — lógica PURA de los ajustes persistentes (se prueba en el PC):
 * valores por defecto, normalización, cuándo guardar y empaquetado de
 * bloques binarios con versión y CRC.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Versión del esquema de claves en NVS (clave "ver"). */
#define WS_SCHEMA_VER 1

#define WS_BRIGHT_MIN      10
#define WS_BRIGHT_MAX      100
#define WS_BRIGHT_DEF      70      /* el mismo inicial de watch_display */
#define WS_TZ_MIN          (-720)  /* UTC-12:00 */
#define WS_TZ_MAX          840     /* UTC+14:00 */
#define WS_TZ_STEP         15
#define WS_TZ_DEF          (-300)  /* Lima, UTC-5 sin horario de verano (referencia) */
#define WS_SCREEN_TO_DEF   15      /* s */
#define WS_FACE_COUNT      3

/** Opciones de apagado de pantalla (s), en orden. */
extern const uint8_t ws_screen_timeouts[];
extern const size_t ws_screen_timeout_count;

typedef struct {
    uint8_t brightness;  /**< % (WS_BRIGHT_MIN..WS_BRIGHT_MAX) */
    uint8_t screen_to_s; /**< apagado de pantalla, uno de ws_screen_timeouts */
    bool h24;            /**< formato de 24 h (D18) */
    int16_t tz_min;      /**< desfase de la zona horaria respecto a UTC, en min (D17) */
    uint8_t wallpaper;   /**< flex_wall_t: 0 = negro AMOLED, 1 = Onyx, 2..8 = resto de fondos de Flex OS Ultra */
    uint8_t face;        /**< estilo de esfera (0..WS_FACE_COUNT-1) */
} ws_settings_t;

/** Número de fondos (= FLEX_WALL_COUNT_ de watch_theme; lo comprueba ui_wallpaper.c). */
#define WS_WALLPAPER_COUNT 9

void ws_defaults(ws_settings_t *s);
/** Lleva cada campo a un valor válido (el más cercano). true si cambió algo. */
bool ws_normalize(ws_settings_t *s);
bool ws_equal(const ws_settings_t *a, const ws_settings_t *b);

/* ---- Cuándo guardar ---- */
#define WS_QUIET_MS 1500u   /* sin cambios durante 1,5 s (referencia: AppFramework.h:1346) */
#define WS_MAX_MS   30000u  /* como mucho 30 s desde el primer cambio sin guardar */

/** Instante (ms) en que hay que guardar: 1,5 s tras el último cambio, sin
 * pasar de 30 s desde el primero. */
int64_t ws_commit_deadline(int64_t first_dirty_ms, int64_t last_change_ms);

/* ---- Bloques binarios con cabecera (alarmas, etc.) ---- */
#define WS_BLOB_MAGIC 0x5746u /* "FW" */
#define WS_BLOB_HDR   9       /* magic (2) + versión (1) + longitud (2) + CRC-32 (4) */

uint32_t ws_crc32(const void *data, size_t n);
/** Empaqueta payload con cabecera en out. Devuelve los bytes escritos o 0 si no cabe. */
size_t ws_blob_pack(uint8_t ver, const void *payload, size_t len, uint8_t *out, size_t cap);
/** Valida y desempaqueta. false si la magia, la longitud o el CRC no cuadran
 * (nunca se usan datos dañados). */
bool ws_blob_unpack(const uint8_t *in, size_t n, uint8_t *ver, void *payload, size_t cap, size_t *len);

#ifdef __cplusplus
}
#endif
