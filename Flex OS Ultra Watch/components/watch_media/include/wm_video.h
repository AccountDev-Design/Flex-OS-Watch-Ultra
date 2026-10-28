/*
 * wm_video — reproductor de vídeo AVI MJPEG del reloj (docs/MULTIMEDIA.md §8).
 *
 * El demultiplexor es el de Flex OS Ultra (flexAvi*, components/flexmedia,
 * probado en tests/host) y cada fotograma lo decodifica TJpgDec (la ROM del
 * S3) directamente a RGB565 al tamaño de la pantalla:
 *  - una tarea "wm_video" (pila en PSRAM) que nace al abrir un vídeo y
 *    termina al cerrarlo; lee por trozos (nunca el archivo entero) y, con la
 *    microSD, con su arrendamiento: si se expulsa o se saca, el vídeo se
 *    detiene con su motivo y suelta la tarjeta;
 *  - ritmo por el reloj del sistema (us por fotograma del AVI): si la
 *    decodificación va tarde, se SALTAN fotogramas (8 bytes de cabecera,
 *    sin decodificar) en vez de ir a cámara lenta;
 *  - dos lienzos RGB565 del tamaño del fotograma (solo mientras suena): la
 *    tarea pinta en el que la interfaz no está enseñando;
 *  - sin sonido: el AVI que prepara la página del móvil solo lleva imagen,
 *    y las pistas de audio de otros AVI se saltan sin leerlas.
 * Nada de esto toca LVGL: la interfaz recoge el último fotograma.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "watch_media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WV_MAX_W      410   /* el fotograma nunca pasa de la pantalla */
#define WV_MAX_H      410
#define WV_STACK      8192
#define WV_SKIP_MAX   30    /* fotogramas saltados de una vez como mucho */

typedef enum {
    WV_IDLE = 0,
    WV_OPENING,
    WV_PLAYING,
    WV_PAUSED,
    WV_ENDED,
    WV_ERROR,
} wv_state_t;

typedef struct {
    wv_state_t state;
    uint16_t w, h;          /**< tamaño del fotograma decodificado */
    uint16_t src_w, src_h;  /**< tamaño en el archivo */
    uint32_t pos_ms, dur_ms;
    uint32_t shown, dropped;/**< fotogramas enseñados y saltados por ir tarde */
    uint16_t fps_x10;       /**< del archivo, x10 */
    char err[96];
    uint32_t gen;           /**< cambia con el estado o la posición */
    uint32_t frame_gen;     /**< cambia con cada fotograma nuevo */
} wv_status_t;

/** Abre y empieza a reproducir (ruta relativa al volumen). false con el motivo. */
bool wv_open(wm_vol_t vol, const char *rel, char *why, size_t n);
void wv_toggle(void);
void wv_seek_ms(uint32_t ms);
/** Cierra: antes, la interfaz deja de enseñar el fotograma. */
void wv_close(void);
void wv_status(wv_status_t *out);
/** El último fotograma (lo fija para la interfaz hasta el siguiente acquire o wv_close). */
const uint16_t *wv_frame_acquire(int *w, int *h, uint32_t *gen);

/* ---- lógica sin hardware (wm_video_logic.c; tests/host) ---- */

/** Divisor de TJpgDec (1, 2, 4 u 8) para que w x h quepa en max_w x max_h (0 = ni a 1/8). */
int wv_pick_scale(int w, int h, int max_w, int max_h);

typedef enum { WV_P_SHOW = 0, WV_P_WAIT, WV_P_SKIP } wv_pace_t;
/**
 * Qué hacer con el fotograma `next` (el que toca leer) a `elapsed_us` de
 * reproducción con `us_per_frame`: esperar `*wait_us` si aún no es su
 * momento, saltarlo si va más de un fotograma tarde, o enseñarlo.
 */
wv_pace_t wv_pace(uint32_t next, uint64_t elapsed_us, uint32_t us_per_frame, uint32_t *wait_us);
/** Fotograma que corresponde a ms (para buscar). */
uint32_t wv_frame_at(uint32_t ms, uint32_t us_per_frame);
/** Texto del motivo de un error de flexAvi para la persona. */
const char *wv_err_text(int avi_err);

#ifdef __cplusplus
}
#endif
