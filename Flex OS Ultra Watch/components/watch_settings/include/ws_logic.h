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
#define WS_FACE_COUNT      11      /* ui_face_style_t: digital, analógica, modular, Liquid Glass, Parallax y las 6
                                    * esferas de los diseños del usuario (rediseño 2026-10) */

/* Atenuación progresiva antes de apagar la pantalla (F4.5 ampliado) */
enum { WS_DIM_OFF = 0, WS_DIM_SOFT, WS_DIM_MED, WS_DIM_STRONG, WS_DIM_COUNT };
/* Política de radios de Flex Ultra Power Saving */
enum { WS_PS_RADIO_KEEP = 0, WS_PS_RADIO_WIFI, WS_PS_RADIO_ALL, WS_PS_RADIO_COUNT };
/* Estado anterior al ahorro (ps_flags) */
#define WS_PS_PREV_VALID 0x80u
#define WS_PS_PREV_AOD   0x01u
#define WS_PS_PREV_WIFI  0x02u
#define WS_PS_PREV_BT    0x04u

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
    /* Pantalla y protección AMOLED */
    bool aod;            /**< pantalla siempre activa (AOD) en vez de apagarse */
    bool pshift;         /**< Pixel Shift de la esfera y del AOD */
    uint8_t dim;         /**< atenuación antes de apagar (WS_DIM_*) */
    /* Flex Ultra Power Saving */
    bool psave;          /**< activo (sobrevive al reinicio) */
    uint8_t psave_radio; /**< WS_PS_RADIO_* */
    uint8_t ps_bright;   /**< brillo del usuario antes de activarlo */
    uint8_t ps_flags;    /**< WS_PS_PREV_*: AOD y radios antes de activarlo */
    /* Flex Liquid Glass Engine */
    uint8_t glass;       /**< WS_GLASS_*: calidad de los efectos de vidrio */
    /* Multimedia (docs/MULTIMEDIA.md) */
    uint8_t media_dest;  /**< WS_DEST_*: dónde se guarda lo nuevo. Nunca cambia solo */
    uint8_t media_vol;   /**< volumen del altavoz, % (0..100) */
    uint8_t rec_quality; /**< WS_REC_*: calidad de la grabadora */
} ws_settings_t;

/* Dónde se guardan las grabaciones, las fotos y lo que llega por la web. Si
 * la microSD elegida no está lista, la operación se detiene y lo dice: nunca
 * se guarda en otro sitio sin preguntar. */
enum { WS_DEST_WATCH = 0, WS_DEST_SD, WS_DEST_COUNT };
/* Grabadora: voz (16 kHz, 32 KB/s) o estándar (24 kHz, 48 KB/s; el perfil de
 * fábrica de la placa). Mono, PCM de 16 bits. */
enum { WS_REC_VOICE = 0, WS_REC_STANDARD, WS_REC_COUNT };
#define WS_MEDIA_VOL_DEF 60

/* Calidad de los efectos de vidrio (flex_glass): automática (Premium y baja
 * sola si el reloj no llega), o fija. El ahorro de energía usa Ligero. */
enum { WS_GLASS_AUTO = 0, WS_GLASS_PREMIUM, WS_GLASS_STANDARD, WS_GLASS_LITE, WS_GLASS_COUNT };

/** Número de fondos (= FLEX_WALL_COUNT_ de watch_theme; lo comprueba ui_wallpaper.c). */
#define WS_WALLPAPER_COUNT 9

void ws_defaults(ws_settings_t *s);
/** Nivel atenuado (% de brillo) para un modo WS_DIM_* y el brillo del usuario;
 * nunca más que el del usuario ni menos de 5 %. WS_DIM_OFF devuelve el del usuario. */
int ws_dim_level(uint8_t dim, int user_percent);
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
