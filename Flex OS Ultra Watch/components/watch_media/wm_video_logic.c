/*
 * wm_video_logic — lo del reproductor de vídeo que no toca archivos ni
 * tareas: escala, ritmo y textos. Ver include/wm_video.h; se prueba en
 * tests/host (test_media.c).
 */
#include "wm_video.h"

int wv_pick_scale(int w, int h, int max_w, int max_h)
{
    if (w <= 0 || h <= 0 || max_w <= 0 || max_h <= 0) {
        return 0;
    }
    for (int s = 1; s <= 8; s *= 2) {
        /* TJpgDec redondea hacia arriba al escalar */
        if ((w + s - 1) / s <= max_w && (h + s - 1) / s <= max_h) {
            return s;
        }
    }
    return 0;
}

wv_pace_t wv_pace(uint32_t next, uint64_t elapsed_us, uint32_t us_per_frame, uint32_t *wait_us)
{
    *wait_us = 0;
    if (!us_per_frame) {
        return WV_P_SHOW; /* sin ritmo declarado: tan rápido como se pueda */
    }
    const uint64_t due = (uint64_t)next * us_per_frame;
    if (due > elapsed_us) {
        const uint64_t w = due - elapsed_us;
        *wait_us = w > 1000000u ? 1000000u : (uint32_t)w;
        return WV_P_WAIT;
    }
    /* Tarde: si el siguiente también ya pasó, este no se enseña (se salta). */
    if (due + us_per_frame <= elapsed_us) {
        return WV_P_SKIP;
    }
    return WV_P_SHOW;
}

uint32_t wv_frame_at(uint32_t ms, uint32_t us_per_frame)
{
    return us_per_frame ? (uint32_t)((uint64_t)ms * 1000u / us_per_frame) : 0;
}

const char *wv_err_text(int e)
{
    switch (e) {
    case -1: return "No se pudo leer el vídeo";                                     /* FLEXAVI_ERR_IO */
    case -2: return "No es un AVI válido";                                          /* FLEXAVI_ERR_FORMAT */
    case -3: return "Este AVI no es MJPEG: el reloj solo reproduce AVI MJPEG";      /* FLEXAVI_ERR_CODEC */
    case -4: return "El archivo no tiene imagen";                                   /* FLEXAVI_ERR_NOVIDEO */
    case -6: return "Fotogramas demasiado grandes para el reloj";                   /* FLEXAVI_ERR_TOOBIG */
    default: return "No se pudo reproducir el vídeo";
    }
}
