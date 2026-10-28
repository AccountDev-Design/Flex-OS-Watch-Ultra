/*
 * wm_video — la tarea del reproductor de vídeo. Ver include/wm_video.h.
 */
#include "wm_video.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "FlexOS_JPEG.h"
#include "FlexOS_Media.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wm_store.h"

static const char *TAG = "wm_video";

#define TASK_PRIO   3          /* por debajo de LVGL (4) */
#define ARENA_BYTES (48u * 1024u) /* TJpgDec + su banda + una fila: se reutiliza en cada fotograma */
#define FRAME_CAP0  (96u * 1024u) /* datos JPEG de un fotograma (crece si hace falta) */

typedef struct {
    wm_vol_t vol;
    char path[WM_PATH_MAX];
    int fd;
    bool leased;
    FlexAviCtx avi;
    uint8_t *jpg;
    uint32_t jpg_cap;
    uint8_t *arena;
    size_t arena_used;
    uint16_t *buf[2];
    int w, h;
} vid_t;

/* Lo compartido con la interfaz (bajo s_mx) */
static SemaphoreHandle_t s_mx;
static wv_status_t s_st;
static int s_front = -1, s_held = -1; /* lienzo publicado y lienzo que enseña la interfaz */
static uint16_t *s_bufs[2];
static TaskHandle_t s_task;
static volatile bool s_stop;
static bool s_toggle_req;
static bool s_seek_req;
static uint32_t s_seek_ms;

static void lock(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mx);
}

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

/* ---------------- E/S del archivo para flexAvi ---------------- */

static int io_read(void *ctx, void *buf, uint32_t n)
{
    vid_t *v = ctx;
    const ssize_t r = read(v->fd, buf, n);
    if (r < 0) {
        if (v->vol == WM_VOL_SD && (errno == EIO || errno == ENODEV || errno == ENXIO)) {
            wm_sd_io_error();
        }
        return -1;
    }
    return (int)r;
}

static bool io_seek(void *ctx, uint32_t off)
{
    vid_t *v = ctx;
    return lseek(v->fd, (off_t)off, SEEK_SET) == (off_t)off;
}

static uint32_t io_size(void *ctx)
{
    vid_t *v = ctx;
    struct stat sb;
    return fstat(v->fd, &sb) == 0 && sb.st_size > 0 ? (uint32_t)sb.st_size : 0;
}

/* ---------------- memoria de TJpgDec: una zona fija ---------------- */

static vid_t *s_arena_owner;

static void *arena_alloc(size_t n)
{
    vid_t *v = s_arena_owner;
    n = (n + 7u) & ~(size_t)7u;
    if (!v || v->arena_used + n > ARENA_BYTES) {
        return NULL;
    }
    void *p = v->arena + v->arena_used;
    v->arena_used += n;
    return p;
}

static void arena_free(void *p)
{
    (void)p; /* se suelta todo junto antes del siguiente fotograma */
}

typedef struct {
    uint16_t *dst;
    int w, h;
} row_dst_t;

static bool put_row(void *user, int y, int w, const uint16_t *rgb)
{
    row_dst_t *d = user;
    if (y < 0 || y >= d->h) {
        return true;
    }
    memcpy(d->dst + (size_t)y * d->w, rgb, (size_t)(w < d->w ? w : d->w) * 2);
    return !s_stop;
}

/* ---------------- estado ---------------- */

static void set_state(wv_state_t st, const char *err)
{
    lock();
    s_st.state = st;
    if (err) {
        snprintf(s_st.err, sizeof(s_st.err), "%s", err);
    }
    s_st.gen++;
    unlock();
}

static bool must_abort(const vid_t *v)
{
    if (s_stop) {
        return true;
    }
    if (v->vol != WM_VOL_SD) {
        return false;
    }
    wm_sd_info_t i;
    wm_sd_info(&i);
    return i.state != WSD_READY;
}

/* Lee el fotograma siguiente; crece el búfer si no cabe (hasta FLEXAVI_FRAME_MAX). */
static int read_frame(vid_t *v)
{
    for (;;) {
        const int r = flexAviReadFrame(&v->avi, v->jpg, v->jpg_cap, NULL);
        if (r != FLEXAVI_ERR_TOOBIG) {
            return r;
        }
        const uint32_t need = v->avi.needBytes;
        if (need > FLEXAVI_FRAME_MAX) {
            return FLEXAVI_ERR_TOOBIG;
        }
        uint8_t *n = wm_alloc(need);
        if (!n) {
            return FLEXAVI_ERR_TOOBIG;
        }
        wm_free(v->jpg);
        v->jpg = n;
        v->jpg_cap = need;
    }
}

/* Lienzo donde pintar: el que no está publicado ni a la vista. Espera a que la
 * interfaz recoja el último si los dos están ocupados. */
static int back_buffer(const vid_t *v)
{
    for (;;) {
        lock();
        int b = -1;
        for (int i = 0; i < 2 && b < 0; i++) {
            if (i != s_front && i != s_held) {
                b = i;
            }
        }
        unlock();
        if (b >= 0 || must_abort(v)) {
            return b;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static bool decode_into(vid_t *v, int len, int b)
{
    v->arena_used = 0;
    s_arena_owner = v;
    row_dst_t d = {.dst = v->buf[b], .w = v->w, .h = v->h};
    FlexJpegInfo info;
    const int rc = flexJpegDecode(v->jpg, (size_t)len, WV_MAX_W, WV_MAX_H, 0, &info, put_row, &d, arena_alloc,
                                  arena_free);
    return rc == FLEXJPG_OK;
}

static void publish(int b, uint32_t pos_ms, bool shown)
{
    lock();
    s_front = b;
    s_st.pos_ms = pos_ms;
    if (shown) {
        s_st.shown++;
    }
    s_st.frame_gen++;
    unlock();
}

/* ---------------- la tarea ---------------- */

/* El archivo, su arrendamiento y la memoria de lectura: en cuanto no se lee
 * más (error, microSD retirada, cierre), para no retener la tarjeta. */
static void release_io(vid_t *v)
{
    if (v->fd >= 0) {
        close(v->fd);
        v->fd = -1;
    }
    if (v->leased) {
        wm_sd_unlease();
        v->leased = false;
    }
    wm_free(v->jpg);
    v->jpg = NULL;
    wm_free(v->arena);
    v->arena = NULL;
}

static void free_vid(vid_t *v)
{
    lock();
    s_front = s_held = -1;
    s_bufs[0] = s_bufs[1] = NULL;
    unlock();
    release_io(v);
    for (int i = 0; i < 2; i++) {
        wm_free(v->buf[i]);
    }
    wm_free(v);
}

static void play(vid_t *v)
{
    const uint32_t upf = v->avi.usPerFrame ? v->avi.usPerFrame : 66666u; /* sin dato: 15 fps */
    uint64_t base_us = 0;       /* posición al (re)empezar */
    uint64_t t0 = now_us();
    bool paused = false;
    for (;;) {
        if (must_abort(v)) {
            if (!s_stop) {
                set_state(WV_ERROR, "Se ha retirado la microSD");
            }
            return;
        }
        lock();
        const bool toggle = s_toggle_req, seek = s_seek_req;
        const uint32_t seek_ms = s_seek_ms;
        s_toggle_req = s_seek_req = false;
        const wv_state_t st = s_st.state;
        unlock();
        if (seek) {
            const int f = flexAviSeekFrame(&v->avi, wv_frame_at(seek_ms, upf));
            if (f >= 0) {
                base_us = (uint64_t)f * upf;
                t0 = now_us();
                if (st == WV_ENDED) {
                    paused = false;
                    set_state(WV_PLAYING, NULL);
                }
            }
        }
        if (toggle) {
            if (st == WV_ENDED) { /* otra vez desde el principio */
                flexAviSeekFrame(&v->avi, 0);
                base_us = 0;
                t0 = now_us();
                paused = false;
                set_state(WV_PLAYING, NULL);
            } else {
                paused = !paused;
                if (!paused) {
                    base_us = (uint64_t)v->avi.frameNo * upf;
                    t0 = now_us();
                }
                set_state(paused ? WV_PAUSED : WV_PLAYING, NULL);
            }
        }
        if (paused || st == WV_ENDED) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            continue;
        }
        uint32_t wait = 0;
        const wv_pace_t p = wv_pace(v->avi.frameNo, base_us + (now_us() - t0), upf, &wait);
        if (p == WV_P_WAIT) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait / 1000u ? wait / 1000u : 1));
            continue;
        }
        if (p == WV_P_SKIP) {
            int skipped = 0;
            while (skipped < WV_SKIP_MAX &&
                   wv_pace(v->avi.frameNo, base_us + (now_us() - t0), upf, &wait) == WV_P_SKIP &&
                   flexAviSkipFrame(&v->avi) == FLEXAVI_OK) {
                skipped++;
            }
            lock();
            s_st.dropped += (uint32_t)skipped;
            unlock();
        }
        const uint32_t frame = v->avi.frameNo;
        const int len = read_frame(v);
        if (len == FLEXAVI_ERR_EOF) {
            set_state(WV_ENDED, NULL);
            continue;
        }
        if (len < 0) {
            set_state(WV_ERROR, wv_err_text(len));
            return;
        }
        const uint32_t pos = (uint32_t)((uint64_t)frame * upf / 1000u);
        if (len == 0) { /* trozo vacío: se repite el anterior */
            lock();
            s_st.pos_ms = pos;
            unlock();
            continue;
        }
        const int b = back_buffer(v);
        if (b < 0) {
            return;
        }
        if (decode_into(v, len, b)) {
            publish(b, pos, true);
        } else {
            lock();
            s_st.dropped++; /* un fotograma dañado no para el vídeo */
            unlock();
        }
    }
}

static void video_task(void *arg)
{
    vid_t *v = arg;
    const FlexMediaIO io = {.read = io_read, .seek = io_seek, .size = io_size, .ctx = v};
    int rc = flexAviOpen(&v->avi, &io);
    if (rc == FLEXAVI_OK) {
        /* el primer fotograma dice el tamaño de los lienzos */
        const int len = read_frame(v);
        FlexJpegInfo pi;
        if (len <= 0 || flexJpegProbe(v->jpg, (size_t)len, &pi) != FLEXJPG_OK) {
            rc = len < 0 ? len : FLEXAVI_ERR_FORMAT;
        } else {
            const int s = wv_pick_scale(pi.width, pi.height, WV_MAX_W, WV_MAX_H);
            v->w = s ? (pi.width + s - 1) / s : 0;
            v->h = s ? (pi.height + s - 1) / s : 0;
            for (int i = 0; i < 2 && v->w; i++) {
                v->buf[i] = wm_alloc((size_t)v->w * v->h * 2);
                if (v->buf[i]) {
                    memset(v->buf[i], 0, (size_t)v->w * v->h * 2);
                }
            }
            v->arena = wm_alloc(ARENA_BYTES);
            if (!v->w || !v->buf[0] || !v->buf[1] || !v->arena) {
                set_state(WV_ERROR, "Sin memoria para el vídeo");
                rc = 1;
            } else {
                lock();
                s_bufs[0] = v->buf[0];
                s_bufs[1] = v->buf[1];
                s_st.w = (uint16_t)v->w;
                s_st.h = (uint16_t)v->h;
                s_st.src_w = (uint16_t)pi.width;
                s_st.src_h = (uint16_t)pi.height;
                s_st.dur_ms = flexAviDurationMs(&v->avi);
                s_st.fps_x10 = v->avi.usPerFrame ? (uint16_t)(10000000u / v->avi.usPerFrame) : 0;
                unlock();
                flexAviSeekFrame(&v->avi, 0);
            }
        }
    }
    if (rc == FLEXAVI_OK) {
        set_state(WV_PLAYING, NULL);
        ESP_LOGI(TAG, "vídeo %dx%d (de %ux%u), %u ms", v->w, v->h, (unsigned)s_st.src_w, (unsigned)s_st.src_h,
                 (unsigned)s_st.dur_ms);
        play(v);
    } else if (rc < 0) {
        set_state(WV_ERROR, wv_err_text(rc));
    }
    release_io(v);
    /* El último fotograma sigue a la vista: los lienzos se sueltan al cerrar. */
    while (!s_stop) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "vídeo cerrado: %u enseñados, %u saltados", (unsigned)s_st.shown, (unsigned)s_st.dropped);
    free_vid(v);
    lock();
    s_task = NULL;
    unlock();
    vTaskDeleteWithCaps(NULL);
}

/* ---------------- API ---------------- */

bool wv_open(wm_vol_t vol, const char *rel, char *why, size_t n)
{
    if (!s_mx) {
        s_mx = xSemaphoreCreateMutex();
        if (!s_mx) {
            snprintf(why, n, "Sin memoria");
            return false;
        }
    }
    lock();
    const bool busy = s_task != NULL;
    unlock();
    if (busy) {
        snprintf(why, n, "Ya hay un vídeo abierto");
        return false;
    }
    vid_t *v = wm_alloc(sizeof(*v));
    if (!v) {
        snprintf(why, n, "Sin memoria para el vídeo");
        return false;
    }
    memset(v, 0, sizeof(*v));
    v->fd = -1;
    v->vol = vol;
    if (vol == WM_VOL_SD) {
        if (!wm_sd_lease()) {
            wm_free(v);
            snprintf(why, n, "La microSD no está lista");
            return false;
        }
        v->leased = true;
    }
    v->jpg = wm_alloc(FRAME_CAP0);
    v->jpg_cap = v->jpg ? FRAME_CAP0 : 0;
    if (!wm_vol_path(vol, rel, v->path, sizeof(v->path)) || !v->jpg || (v->fd = open(v->path, O_RDONLY)) < 0) {
        snprintf(why, n, "%s", v->jpg ? "No se encuentra el vídeo" : "Sin memoria para el vídeo");
        free_vid(v);
        return false;
    }
    lock();
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = WV_OPENING;
    s_front = s_held = -1;
    s_toggle_req = s_seek_req = false;
    s_stop = false;
    unlock();
    TaskHandle_t t = NULL;
    if (xTaskCreatePinnedToCoreWithCaps(video_task, "wm_video", WV_STACK, v, TASK_PRIO, &t, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        snprintf(why, n, "Sin memoria para el vídeo");
        free_vid(v);
        lock();
        s_st.state = WV_IDLE;
        unlock();
        return false;
    }
    lock();
    s_task = t;
    unlock();
    return true;
}

static void wake(void)
{
    lock();
    TaskHandle_t t = s_task;
    unlock();
    if (t) {
        xTaskNotifyGive(t);
    }
}

void wv_toggle(void)
{
    if (!s_mx) {
        return;
    }
    lock();
    s_toggle_req = true;
    unlock();
    wake();
}

void wv_seek_ms(uint32_t ms)
{
    if (!s_mx) {
        return;
    }
    lock();
    s_seek_req = true;
    s_seek_ms = ms;
    unlock();
    wake();
}

void wv_close(void)
{
    if (!s_mx) {
        return;
    }
    lock();
    s_held = -1; /* la interfaz ya no lo enseña */
    s_stop = true;
    unlock();
    wake();
}

void wv_status(wv_status_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    *out = s_st;
    unlock();
}

const uint16_t *wv_frame_acquire(int *w, int *h, uint32_t *gen)
{
    if (!s_mx) {
        return NULL;
    }
    lock();
    const uint16_t *p = NULL;
    if (s_front >= 0 && s_bufs[s_front] && !s_stop) {
        s_held = s_front;
        p = s_bufs[s_front];
        *w = s_st.w;
        *h = s_st.h;
        *gen = s_st.frame_gen;
    }
    unlock();
    return p;
}
