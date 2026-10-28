/*
 * wa_logic — lógica PURA del audio (se prueba en el PC): cabecera WAV,
 * ranuras TDM del ES7210, medidor de nivel y espacio de la grabadora.
 *
 * Captura (perfil de fábrica de Waveshare, docs/MULTIMEDIA.md §5): el ES7210
 * entrega por I2S TDM 4 ranuras de 16 bits por muestra, en este orden:
 *   ranura 0 = MIC1 (delantero izquierdo), 1 = MIC3 (referencia de eco),
 *   ranura 2 = MIC2 (delantero derecho),  3 = MIC4 (no conectado).
 * La grabadora usa MIC1 y MIC2: en mono, su media; en estéreo, los dos.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- WAV ---------------- */

#define WA_WAV_HDR 44
/* Tamaños "desconocidos" de una cabecera escrita al empezar y no corregida
 * (la memoria interna: ver wa_rec.c). Los lectores la recortan al archivo. */
#define WA_WAV_STREAMING 0xFFFFFFFFu
/* Lo máximo que cabe en los campos de 32 bits de RIFF. */
#define WA_WAV_MAX_DATA (0xFFFFFFFFu - WA_WAV_HDR)

/** Cabecera PCM de 16 bits. data_bytes = WA_WAV_STREAMING para "sin cerrar". */
void wa_wav_header(uint8_t out[WA_WAV_HDR], uint32_t rate, uint16_t channels, uint32_t data_bytes);

/** Lee el tamaño de datos de una cabecera de wa_wav_header (para las pruebas
 * y la recuperación). false si no es una cabecera PCM de 16 bits de 44 B. */
bool wa_wav_parse44(const uint8_t h[WA_WAV_HDR], uint32_t *rate, uint16_t *channels, uint32_t *data_bytes);

/* ---------------- TDM del ES7210 ---------------- */

#define WA_TDM_SLOTS     4
#define WA_TDM_SLOT_MIC1 0
#define WA_TDM_SLOT_MIC2 2

/** frames muestras TDM -> mono (media de MIC1 y MIC2) o estéreo (MIC1, MIC2).
 * Devuelve las muestras escritas en out (frames o 2*frames). */
size_t wa_tdm_extract(const int16_t *tdm, size_t frames, int16_t *out, bool stereo);

/* ---------------- medidor de nivel ---------------- */

#define WA_METER_FLOOR_DB (-60.0f)
#define WA_METER_RELEASE_MS 300u /* caída: de lleno a vacío en ~1 s */
#define WA_METER_CLIP_HOLD_MS 1500u

typedef struct {
    float level;        /**< 0..1 lo que se pinta (suavizado) */
    float peak_db;      /**< pico del último bloque, dBFS */
    float rms_db;       /**< RMS del último bloque, dBFS */
    uint32_t last_ms;
    uint32_t clip_ms;   /**< último recorte (si has_clip) */
    bool has_clip;
    bool started;
} wa_meter_t;

void wa_meter_init(wa_meter_t *m);
/** Un bloque de muestras: sube al momento, baja con WA_METER_RELEASE_MS. */
void wa_meter_feed(wa_meter_t *m, const int16_t *s, size_t n, uint32_t now_ms);
/** ¿Hubo un recorte en los últimos WA_METER_CLIP_HOLD_MS? */
bool wa_meter_clipping(const wa_meter_t *m, uint32_t now_ms);
/** dBFS -> 0..1 (WA_METER_FLOOR_DB = 0, 0 dB = 1). */
float wa_meter_map(float db);

/* ---------------- espacio de la grabadora ---------------- */

/** Bytes por segundo de PCM de 16 bits. */
uint32_t wa_bytes_per_s(uint32_t rate, uint16_t channels);
/** Segundos que caben con `free_bytes` libres dejando `reserve` sin tocar y
 * sin pasar de WA_WAV_MAX_DATA (ni de max_file, el límite del sistema de
 * archivos: FAT32 = 4 GB - 1). */
uint32_t wa_rec_seconds_left(uint64_t free_bytes, uint64_t reserve, uint64_t written, uint32_t bps,
                             uint64_t max_file);
/** ¿Hay que parar YA? (queda menos de un bloque por encima de la reserva, o
 * el archivo llegaría al límite). */
bool wa_rec_must_stop(uint64_t free_bytes, uint64_t reserve, uint64_t written, uint32_t block, uint64_t max_file);

/** "1:05", "12:34", "1:02:03". */
void wa_fmt_time(uint32_t ms, char *out, size_t n);

#ifdef __cplusplus
}
#endif
