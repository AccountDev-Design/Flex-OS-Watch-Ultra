/*
 * wa_player — el reproductor de música compartido (docs/MULTIMEDIA.md §6).
 *
 * Sigue sonando al salir de la app (como en Flex OS Ultra y en el Apple
 * Watch): es un servicio con su propia tarea, no parte de la pantalla. La app
 * Música, la Galería y la Grabadora le mandan órdenes y leen su estado; nunca
 * esperan a la tarea ni le tocan los búferes.
 *
 *  - WAV (PCM de 8/16 bits e IMA ADPCM) con el FlexAudioStream del P4, y MP3
 *    con minimp3 (área de trabajo de 16 KB en RAM interna si sobra, si no en
 *    PSRAM). Se lee POR TROZOS: nunca el archivo entero en memoria.
 *  - La tarea "wa_play" existe solo mientras hay algo que reproducir: tras
 *    10 s parada se termina sola y suelta su pila y el audio.
 *  - De la microSD toma un arrendamiento por canción: si la tarjeta se
 *    expulsa o se retira, la canción se para con un error y lo suelta.
 *  - Si la grabadora pide el audio, el reproductor se pausa y lo suelta
 *    (la posición se conserva para seguir después).
 *  - La portada (etiqueta ID3, JPEG) la decodifica la tarea, a WAP_COVER_SIDE
 *    px en RGB565; la interfaz copia los píxeles con wap_cover_copy().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_MediaLib.h"
#include "esp_err.h"
#include "watch_media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAP_LIST_MAX   256
#define WAP_COVER_SIDE 200

typedef enum { WAP_IDLE = 0, WAP_LOADING, WAP_PLAYING, WAP_PAUSED, WAP_ENDED, WAP_ERROR } wap_state_t;

typedef struct {
    uint8_t vol;                 /**< wm_vol_t */
    uint32_t id;                 /**< id en la biblioteca (0 = no está) */
    char path[FML_PATH_MAX];     /**< relativa al volumen */
    char name[FML_NAME_MAX];     /**< para enseñar si no hay título */
    char title[FML_TITLE_MAX];
    char artist[FML_ARTIST_MAX];
    char album[FML_ALBUM_MAX];
    uint32_t dur_ms;             /**< la del catálogo (0 = no se sabe) */
} wap_track_t;

typedef struct {
    wap_state_t state;
    int index, count;            /**< en la lista (-1 sin lista) */
    wap_track_t track;
    uint32_t pos_ms, dur_ms;
    uint32_t rate;
    uint8_t channels;
    char format[12];             /**< "MP3 320k", "WAV PCM"... */
    char error[96];
    bool preempted;              /**< pausada porque la grabadora pidió el audio */
    uint32_t gen;                /**< cambia con cada cambio visible (no con la posición) */
    uint32_t cover_gen;          /**< cambia cuando cambia la portada */
    bool has_cover;
} wap_status_t;

esp_err_t wap_init(void);

/** Copia la lista (como mucho WAP_LIST_MAX) y empieza por `start`. */
bool wap_play_list(const wap_track_t *list, int n, int start);
void wap_toggle(void);
void wap_pause(void);
void wap_resume(void);
void wap_stop(void);
void wap_next(void);
void wap_prev(void);
void wap_seek(uint32_t ms);

void wap_status(wap_status_t *out);
/** Copia la portada (WAP_COVER_SIDE^2 píxeles RGB565). false si no hay. */
bool wap_cover_copy(uint16_t *dst, uint32_t *gen);

const char *wap_state_name(wap_state_t st);

#ifdef __cplusplus
}
#endif
