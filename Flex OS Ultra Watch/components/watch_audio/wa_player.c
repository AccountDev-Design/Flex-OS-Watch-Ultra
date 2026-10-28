/*
 * wa_player — ver include/wa_player.h.
 */
#include "wa_player.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FlexOS_JPEG.h"
#include "FlexOS_Media.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_id3.h"
#include "flex_mp3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mem_logic.h"
#include "minimp3.h"
#include "watch_audio.h"
#include "watch_task.h"
#include "wm_store.h"

static const char *TAG = "wa_play";

#define TASK_STACK   8192     /* en RAM interna si sobra (la decodificación no pasa por la caché de la PSRAM); si no, PSRAM */
#define TASK_PRIO    6        /* por encima de LVGL (4): el sonido no se corta al animar */
#define TASK_CORE    0        /* LVGL vive en el núcleo 1 */
#define IDLE_EXIT_MS 10000u
#define QUEUE_LEN    8
#define IN_CAP       (16 * 1024)   /* entrada de MP3 */
#define RD_CHUNK     4096
#define OUT_FRAMES   1152          /* una trama de MP3 */

enum { OP_LIST = 1, OP_TOGGLE, OP_PAUSE, OP_RESUME, OP_STOP, OP_NEXT, OP_PREV, OP_SEEK };
typedef struct {
    uint8_t op;
    uint32_t arg;
} cmd_t;

typedef enum { FMT_NONE = 0, FMT_WAV, FMT_MP3 } fmt_t;

typedef struct {
    /* archivo */
    int fd;
    uint32_t size;
    uint32_t io_pos;
    bool leased;
    wm_vol_t vol;
    /* formato */
    fmt_t fmt;
    uint32_t rate;
    uint8_t ch;
    uint32_t dur_ms;
    /* WAV */
    FlexWavInfo wi;
    FlexAudioStream as;
    uint8_t *as_work;
    /* MP3 */
    mp3dec_t *mp3;
    void *scratch;
    FlexMp3Info mi;
    uint8_t *in;
    size_t in_len;
    uint32_t file_pos;
    uint64_t samples;
    /* salida estéreo */
    int16_t *pcm;      /* lo decodificado (puede ser mono) */
    int16_t *st;       /* estéreo para el I2S */
    size_t st_len;     /* bytes en st */
} dec_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_mx;   /* estado, lista y portada */
static TaskHandle_t s_task;
static volatile bool s_preempt;
static volatile uint32_t s_sd_gen;
static wap_status_t s_st;
static wap_track_t *s_list;      /* PSRAM */
static int s_count;
static int s_index = -1;
static uint16_t *s_cover;        /* PSRAM, WAP_COVER_SIDE^2 */
static bool s_cover_ok;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mx);
}

static void set_state(wap_state_t st, const char *err)
{
    lock();
    s_st.state = st;
    snprintf(s_st.error, sizeof(s_st.error), "%s", err ? err : "");
    s_st.gen++;
    unlock();
}

const char *wap_state_name(wap_state_t st)
{
    switch (st) {
    case WAP_IDLE:    return "Parado";
    case WAP_LOADING: return "Cargando";
    case WAP_PLAYING: return "Reproduciendo";
    case WAP_PAUSED:  return "En pausa";
    case WAP_ENDED:   return "Terminado";
    case WAP_ERROR:   return "Error";
    default:          return "?";
    }
}

/* ---------------- E/S del archivo ---------------- */

static int io_read(void *c, void *buf, uint32_t n)
{
    dec_t *d = c;
    const ssize_t r = read(d->fd, buf, n);
    if (r < 0) {
        if (d->vol == WM_VOL_SD) {
            wm_sd_io_error();
        }
        return -1;
    }
    d->io_pos += (uint32_t)r;
    return (int)r;
}

static bool io_seek(void *c, uint32_t off)
{
    dec_t *d = c;
    if (d->io_pos == off) {
        return true;
    }
    if (lseek(d->fd, (off_t)off, SEEK_SET) != (off_t)off) {
        return false;
    }
    d->io_pos = off;
    return true;
}

static uint32_t io_size(void *c)
{
    return ((dec_t *)c)->size;
}

static FlexMediaIO mkio(dec_t *d)
{
    FlexMediaIO io = {io_read, io_seek, io_size, d};
    return io;
}

/* ---------------- portada ---------------- */

typedef struct {
    dec_t *d;
    uint32_t left;
    int ow, oh;     /* salida del decodificador */
    int crop, cx, cy;
} cover_ctx_t;

static int cover_read(void *c, uint8_t *buf, size_t n)
{
    cover_ctx_t *cc = c;
    if (n > cc->left) {
        n = cc->left;
    }
    if (n == 0) {
        return 0;
    }
    const int r = io_read(cc->d, buf, (uint32_t)n);
    if (r > 0) {
        cc->left -= (uint32_t)r;
    }
    return r;
}

static int cover_pick(void *user, int w, int h)
{
    cover_ctx_t *cc = user;
    int d = 1;
    while (d < 8 && (w / (d * 2) >= WAP_COVER_SIDE && h / (d * 2) >= WAP_COVER_SIDE)) {
        d *= 2;
    }
    if (w / d > 2048 || h / d > 2048) {
        return 0; /* ni a 1/8 cabe razonablemente: sin portada */
    }
    cc->ow = (w + d - 1) / d;
    cc->oh = (h + d - 1) / d;
    cc->crop = cc->ow < cc->oh ? cc->ow : cc->oh; /* recorte cuadrado centrado */
    cc->cx = (cc->ow - cc->crop) / 2;
    cc->cy = (cc->oh - cc->crop) / 2;
    return d;
}

static bool cover_row(void *user, int y, int w, const uint8_t *rgb)
{
    cover_ctx_t *cc = user;
    if (cc->crop <= 0 || y < cc->cy || y >= cc->cy + cc->crop) {
        return true;
    }
    /* Vecino más cercano: las filas de destino que caen en esta fila de origen. */
    const int sy = y - cc->cy;
    const int dy0 = (sy * WAP_COVER_SIDE + cc->crop - 1) / cc->crop;
    const int dy1 = ((sy + 1) * WAP_COVER_SIDE + cc->crop - 1) / cc->crop;
    for (int dy = dy0; dy < dy1 && dy < WAP_COVER_SIDE; dy++) {
        uint16_t *row = s_cover + (size_t)dy * WAP_COVER_SIDE;
        for (int dx = 0; dx < WAP_COVER_SIDE; dx++) {
            const int sx = cc->cx + dx * cc->crop / WAP_COVER_SIDE;
            if (sx >= w) {
                break;
            }
            const uint8_t *p = rgb + sx * 3;
            row[dx] = (uint16_t)((p[0] & 0xF8) << 8 | (p[1] & 0xFC) << 3 | p[2] >> 3);
        }
    }
    return true;
}

static void load_cover(dec_t *d)
{
    bool ok = false;
    FlexMediaIO io = mkio(d);
    FlexId3Info id3;
    if (d->fmt == FMT_MP3 && flexId3Read(&io, &id3) == FLEXID3_OK && id3.picKind == FLEXID3_PIC_JPEG &&
        id3.picBytes > 0 && id3.picBytes <= FML_LIMIT_PHOTO && s_cover && io_seek(d, id3.picOffset) &&
        wm_heavy_begin(200)) {
        cover_ctx_t cc = {.d = d, .left = id3.picBytes};
        FlexJpegInfo info;
        lock();
        s_cover_ok = false; /* la interfaz no copia mientras se escribe */
        unlock();
        const int r = flexJpegDecode888Stream(cover_read, &cc, 0, 0, 0, cover_pick, &info, cover_row, &cc, wm_alloc,
                                              wm_free);
        wm_heavy_end();
        ok = r == FLEXJPG_OK;
    }
    lock();
    s_cover_ok = ok;
    s_st.has_cover = ok;
    s_st.cover_gen++;
    unlock();
}

/* ---------------- abrir y cerrar una canción ---------------- */

static void dec_close(dec_t *d)
{
    if (d->fd >= 0) {
        close(d->fd);
        d->fd = -1;
    }
    if (d->leased) {
        wm_sd_unlease();
        d->leased = false;
    }
    wm_free(d->as_work);
    d->as_work = NULL;
    wm_free(d->in);
    d->in = NULL;
    heap_caps_free(d->scratch);
    d->scratch = NULL;
    wm_free(d->mp3);
    d->mp3 = NULL;
    d->fmt = FMT_NONE;
}

/* "No hay memoria" con las cifras (solo la tarea del reproductor la usa). */
static const char *nomem(size_t need)
{
    static char why[112];
    mem_why_text(why, sizeof(why), need, true, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return why;
}

static const char *dec_open(dec_t *d, const wap_track_t *t)
{
    dec_close(d);
    memset(d, 0, offsetof(dec_t, pcm)); /* los búferes de salida se conservan */
    d->fd = -1;
    d->vol = (wm_vol_t)t->vol;
    if (d->vol == WM_VOL_SD) {
        if (!wm_sd_lease()) {
            return "La microSD no está disponible.";
        }
        d->leased = true;
        s_sd_gen = wm_sd_gen();
    }
    char rp[WM_PATH_MAX];
    if (!wm_vol_path(d->vol, t->path, rp, sizeof(rp))) {
        return "Ruta no válida.";
    }
    d->fd = open(rp, O_RDONLY);
    struct stat sb;
    if (d->fd < 0 || fstat(d->fd, &sb) != 0) {
        return "No se encuentra el archivo.";
    }
    d->size = sb.st_size < 0 ? 0 : (uint32_t)sb.st_size;
    FlexMediaIO io = mkio(d);
    uint8_t head[12] = {0};
    if (io_read(d, head, sizeof(head)) < 4) {
        return "No se pudo leer el archivo.";
    }
    if (!memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4)) {
        const int r = flexWavParse(&io, &d->wi);
        if (r == FLEXWAV_ERR_CODEC) {
            return "Este WAV usa un formato que el reloj no reproduce.";
        }
        if (r != FLEXWAV_OK) {
            return "El archivo WAV está dañado.";
        }
        const size_t need = flexAsWorkBytes(&d->wi);
        d->as_work = need ? wm_alloc(need) : NULL;
        if (!d->as_work) {
            return need ? nomem(need) : "Formato WAV no admitido.";
        }
        if (!flexAsOpen(&d->as, &io, &d->wi, d->as_work, need)) {
            return "El archivo WAV está dañado.";
        }
        d->fmt = FMT_WAV;
        d->rate = d->wi.sampleRate;
        d->ch = (uint8_t)d->wi.channels;
        d->dur_ms = flexAsDurMs(&d->as);
        return NULL;
    }
    if (flexMp3Probe(&io, &d->mi) != FLEXMP3_OK) {
        return "No es un MP3 válido o está dañado.";
    }
    d->mp3 = wm_alloc(sizeof(mp3dec_t));
    /* El área de trabajo en RAM interna si sobra holgadamente; si no, PSRAM. */
    const size_t sb_n = mp3dec_scratch_bytes();
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > sb_n + 48 * 1024) {
        d->scratch = heap_caps_malloc(sb_n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!d->scratch) {
        d->scratch = heap_caps_malloc(sb_n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    d->in = wm_alloc(IN_CAP);
    if (!d->mp3 || !d->scratch || !d->in) {
        return nomem(sizeof(mp3dec_t) + sb_n + IN_CAP);
    }
    mp3dec_init(d->mp3);
    d->mp3->flex_scratch = d->scratch;
    d->fmt = FMT_MP3;
    d->rate = d->mi.sampleRate;
    d->ch = d->mi.channels;
    d->dur_ms = d->mi.durMs;
    d->file_pos = d->mi.audioStart; /* sin la trama de la cabecera VBR (silencio) */
    d->in_len = 0;
    d->samples = 0;
    return NULL;
}

static uint32_t dec_pos_ms(const dec_t *d)
{
    if (d->fmt == FMT_WAV) {
        return flexAsPosMs(&d->as);
    }
    if (d->fmt == FMT_MP3 && d->rate) {
        return (uint32_t)(d->samples * 1000u / d->rate);
    }
    return 0;
}

static void dec_seek(dec_t *d, uint32_t ms)
{
    if (d->fmt == FMT_WAV) {
        flexAsSeekMs(&d->as, ms);
    } else if (d->fmt == FMT_MP3 && d->dur_ms) {
        if (ms > d->dur_ms) {
            ms = d->dur_ms;
        }
        /* Proporcional a los bytes de audio: exacto en CBR, aproximado en VBR. */
        const uint32_t span = d->size > d->mi.audioStart ? d->size - d->mi.audioStart : 0;
        d->file_pos = d->mi.audioStart + (uint32_t)((uint64_t)span * ms / d->dur_ms);
        d->in_len = 0;
        mp3dec_init(d->mp3);
        d->mp3->flex_scratch = d->scratch;
        d->samples = (uint64_t)ms * d->rate / 1000u;
    }
}

/* ---------------- decodificar un trozo ---------------- */

typedef struct {
    int16_t *dst;
    size_t cap, len; /* bytes */
} sink_t;

static int as_sink(void *ctx, const void *pcm, size_t n)
{
    sink_t *s = ctx;
    const size_t room = s->cap - s->len;
    if (n > room) {
        n = room;
    }
    memcpy((uint8_t *)s->dst + s->len, pcm, n);
    s->len += n;
    return (int)n;
}

/* Deja en d->st un trozo estéreo. 1 = hay audio, 0 = fin, -1 = error. */
static int dec_step(dec_t *d)
{
    size_t frames = 0;
    if (d->fmt == FMT_WAV) {
        sink_t s = {d->pcm, OUT_FRAMES * 2 * sizeof(int16_t), 0};
        const uint32_t budget = OUT_FRAMES * d->ch * 2u;
        const int r = flexAsPump(&d->as, as_sink, &s, budget);
        if (r < 0) {
            return -1;
        }
        if (s.len == 0) {
            return d->as.ended ? 0 : 1; /* nada esta vuelta pero no ha terminado */
        }
        frames = s.len / (d->ch * 2u);
    } else if (d->fmt == FMT_MP3) {
        for (int guard = 0; guard < 64; guard++) {
            /* Rellenar la entrada por trozos. */
            while (d->in_len + RD_CHUNK <= IN_CAP && d->file_pos < d->size) {
                if (!io_seek(d, d->file_pos)) {
                    return -1;
                }
                uint32_t want = RD_CHUNK;
                if (want > d->size - d->file_pos) {
                    want = d->size - d->file_pos;
                }
                const int r = io_read(d, d->in + d->in_len, want);
                if (r < 0) {
                    return -1;
                }
                if (r == 0) {
                    break;
                }
                d->in_len += (size_t)r;
                d->file_pos += (uint32_t)r;
            }
            if (d->in_len == 0) {
                return 0;
            }
            mp3dec_frame_info_t fi;
            const int n = mp3dec_decode_frame(d->mp3, d->in, (int)d->in_len, d->pcm, &fi);
            if (fi.frame_bytes == 0) {
                if (d->file_pos >= d->size) {
                    return 0; /* lo que queda no es una trama: fin */
                }
                d->in_len = 0; /* basura en toda la ventana: se descarta y se sigue */
                continue;
            }
            memmove(d->in, d->in + fi.frame_bytes, d->in_len - (size_t)fi.frame_bytes);
            d->in_len -= (size_t)fi.frame_bytes;
            if (n <= 0) {
                continue; /* trama de etiqueta o sin audio */
            }
            if (fi.hz != (int)d->rate || fi.channels != d->ch) {
                /* Cambio de formato a mitad (raro): se sigue con el nuevo. */
                if (fi.hz != (int)d->rate && wa_out_open((uint32_t)fi.hz) != ESP_OK) {
                    return -1;
                }
                d->rate = (uint32_t)fi.hz;
                d->ch = (uint8_t)fi.channels;
            }
            frames = (size_t)n;
            d->samples += (uint64_t)n;
            break;
        }
        if (frames == 0) {
            return 0;
        }
    } else {
        return 0;
    }
    if (d->ch == 1) {
        for (size_t i = 0; i < frames; i++) {
            d->st[2 * i] = d->st[2 * i + 1] = d->pcm[i];
        }
    } else {
        memcpy(d->st, d->pcm, frames * 4);
    }
    d->st_len = frames * 4;
    return 1;
}

/* ---------------- la tarea ---------------- */

static void publish_track(const dec_t *d, const wap_track_t *t)
{
    lock();
    s_st.track = *t;
    s_st.index = s_index;
    s_st.count = s_count;
    s_st.rate = d->rate;
    s_st.channels = d->ch;
    s_st.dur_ms = d->dur_ms ? d->dur_ms : t->dur_ms;
    s_st.pos_ms = dec_pos_ms(d);
    if (d->fmt == FMT_MP3) {
        snprintf(s_st.format, sizeof(s_st.format), "MP3 %uk", (unsigned)d->mi.kbps);
    } else if (d->fmt == FMT_WAV) {
        snprintf(s_st.format, sizeof(s_st.format), "%s", d->wi.format == FLEXWAV_FMT_IMA ? "WAV IMA" : "WAV PCM");
    } else {
        s_st.format[0] = 0;
    }
    s_st.gen++;
    unlock();
}

static bool get_track(int idx, wap_track_t *out)
{
    lock();
    const bool ok = s_list && idx >= 0 && idx < s_count;
    if (ok) {
        *out = s_list[idx];
        s_index = idx;
    }
    unlock();
    return ok;
}

static void on_preempt(void *ctx)
{
    (void)ctx;
    s_preempt = true;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

static void sd_changed(uint32_t gen, void *ctx)
{
    (void)ctx;
    s_sd_gen = gen;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

/* Suelta el audio (amplificador y códec apagados) sin cerrar la canción. */
static void audio_off(bool *have)
{
    if (*have) {
        wa_out_close();
        wa_session_release(WA_OWNER_PLAYER);
        *have = false;
    }
}

static const char *audio_on(bool *have, uint32_t rate)
{
    if (!*have) {
        const esp_err_t e = wa_session_acquire(WA_OWNER_PLAYER, false, on_preempt, NULL);
        if (e != ESP_OK) {
            return "La grabadora está usando el audio.";
        }
        *have = true;
    }
    if (wa_out_open(rate) != ESP_OK) {
        audio_off(have);
        return "No se pudo encender el altavoz.";
    }
    return NULL;
}

static void stop_song(dec_t *d, bool *have, bool *loaded)
{
    audio_off(have);
    dec_close(d);
    *loaded = false;
}

/* Interna si hay (más rápida); si no, PSRAM: suena igual, no se queda mudo. */
static void *buf_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void player_task(void *arg)
{
    (void)arg;
    dec_t d;
    memset(&d, 0, sizeof(d));
    d.fd = -1;
    d.pcm = buf_alloc(OUT_FRAMES * 2 * sizeof(int16_t));
    d.st = buf_alloc(OUT_FRAMES * 2 * sizeof(int16_t));
    bool have_audio = false, loaded = false;
    wap_state_t st = WAP_IDLE;
    uint32_t idle_since = now_ms(), last_pos_pub = 0;
    if (!d.pcm || !d.st) {
        set_state(WAP_ERROR, nomem(OUT_FRAMES * 4 * sizeof(int16_t)));
    }
    for (bool run = d.pcm && d.st; run;) {
        /* Una orden por vuelta: sin esperar si suena (una vuelta = una trama,
         * ~26 ms); si no, se duerme hasta la próxima o medio segundo. */
        cmd_t c = {0};
        const bool got = xQueueReceive(s_q, &c, st == WAP_PLAYING ? 0 : pdMS_TO_TICKS(500)) == pdTRUE;
        int target = -2; /* -2 ninguna canción nueva, -1 parar */
        bool want_play = st == WAP_PLAYING;
        if (got) {
            switch (c.op) {
            case OP_LIST:   target = (int)c.arg; want_play = true; break;
            case OP_TOGGLE: want_play = !want_play; break;
            case OP_PAUSE:  want_play = false; break;
            case OP_RESUME: want_play = true; break;
            case OP_STOP:   target = -1; break;
            case OP_NEXT:   target = s_index + 1; want_play = true; break;
            case OP_PREV:
                if (loaded && dec_pos_ms(&d) > 3000) {
                    dec_seek(&d, 0); /* como en todos los reproductores: al principio */
                } else {
                    target = s_index > 0 ? s_index - 1 : 0;
                }
                want_play = true;
                break;
            case OP_SEEK:
                if (loaded) {
                    dec_seek(&d, c.arg);
                    lock();
                    s_st.pos_ms = dec_pos_ms(&d);
                    s_st.gen++;
                    unlock();
                }
                break;
            default: break;
            }
        }
        /* La microSD de la canción se va: se para con su motivo. */
        if (loaded && d.vol == WM_VOL_SD) {
            wm_sd_info_t si;
            wm_sd_info(&si);
            if (si.state != WSD_READY) {
                stop_song(&d, &have_audio, &loaded);
                st = WAP_ERROR;
                set_state(st, "La microSD se ha expulsado o retirado.");
                idle_since = now_ms();
                continue;
            }
        }
        /* La grabadora pidió el audio: pausa conservando la posición. */
        if (s_preempt) {
            s_preempt = false;
            audio_off(&have_audio);
            if (st == WAP_PLAYING) {
                st = WAP_PAUSED;
                lock();
                s_st.state = st;
                s_st.preempted = true;
                s_st.gen++;
                unlock();
            }
            want_play = false;
        }
        if (target == -1) {
            stop_song(&d, &have_audio, &loaded);
            st = WAP_IDLE;
            set_state(st, NULL);
            idle_since = now_ms();
            continue;
        }
        if (target >= 0) {
            wap_track_t t;
            if (!get_track(target, &t)) {
                stop_song(&d, &have_audio, &loaded);
                st = WAP_ENDED; /* después de la última */
                set_state(st, NULL);
                idle_since = now_ms();
                continue;
            }
            st = WAP_LOADING;
            set_state(st, NULL);
            const char *err = dec_open(&d, &t);
            loaded = err == NULL;
            publish_track(&d, &t);
            if (!loaded) {
                stop_song(&d, &have_audio, &loaded);
                st = WAP_ERROR;
                set_state(st, err);
                idle_since = now_ms();
                continue;
            }
            load_cover(&d);
        }
        if (want_play && loaded && st != WAP_PLAYING) {
            const char *err = audio_on(&have_audio, d.rate);
            if (err) {
                st = WAP_PAUSED;
                set_state(st, err);
                idle_since = now_ms();
                continue;
            }
            st = WAP_PLAYING;
            lock();
            s_st.state = st;
            s_st.preempted = false;
            s_st.error[0] = 0;
            s_st.gen++;
            unlock();
        } else if (!want_play && st == WAP_PLAYING) {
            audio_off(&have_audio); /* en pausa el amplificador y el códec se apagan */
            st = WAP_PAUSED;
            set_state(st, NULL);
            idle_since = now_ms();
        }
        if (st == WAP_PLAYING) {
            const int r = dec_step(&d);
            if (r > 0 && d.st_len) {
                if (wa_out_write(d.st, d.st_len) != ESP_OK) {
                    stop_song(&d, &have_audio, &loaded);
                    st = WAP_ERROR;
                    set_state(st, "Error del audio.");
                    idle_since = now_ms();
                    continue;
                }
                d.st_len = 0;
            } else if (r == 0) {
                const cmd_t nx = {OP_NEXT, 0};
                xQueueSendToFront(s_q, &nx, 0); /* fin de la canción: la siguiente */
            } else if (r < 0) {
                const bool sd = d.vol == WM_VOL_SD;
                stop_song(&d, &have_audio, &loaded);
                st = WAP_ERROR;
                set_state(st, sd ? "No se pudo leer el archivo (¿se ha retirado la microSD?)."
                                 : "No se pudo leer el archivo.");
                idle_since = now_ms();
                continue;
            }
            const uint32_t t = now_ms();
            if (t - last_pos_pub >= 200) {
                last_pos_pub = t;
                lock();
                s_st.pos_ms = dec_pos_ms(&d); /* la posición no sube gen: la interfaz la lee sola */
                unlock();
            }
        } else if ((st == WAP_IDLE || st == WAP_ENDED || st == WAP_ERROR) &&
                   (uint32_t)(now_ms() - idle_since) > IDLE_EXIT_MS) {
            /* Parada un rato: la tarea termina (sin audio ni canción abiertos).
             * Se decide con el cerrojo: una orden que llega después ve
             * s_task == NULL y crea otra tarea. */
            lock();
            if (uxQueueMessagesWaiting(s_q) == 0) {
                s_task = NULL;
                run = false;
            }
            unlock();
        }
    }
    stop_song(&d, &have_audio, &loaded);
    heap_caps_free(d.pcm);
    heap_caps_free(d.st);
    ESP_LOGI(TAG, "reproductor en reposo (pila mínima libre %u B)", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    lock();
    if (s_task == xTaskGetCurrentTaskHandle()) {
        s_task = NULL; /* salida por falta de memoria */
    }
    unlock();
    watch_task_exit();
}

/* ---------------- API ---------------- */

esp_err_t wap_init(void)
{
    if (s_q) {
        return ESP_OK;
    }
    s_q = xQueueCreate(QUEUE_LEN, sizeof(cmd_t));
    s_mx = xSemaphoreCreateMutex();
    s_list = heap_caps_calloc(WAP_LIST_MAX, sizeof(wap_track_t), MALLOC_CAP_SPIRAM);
    s_cover = heap_caps_malloc((size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2, MALLOC_CAP_SPIRAM);
    if (!s_q || !s_mx || !s_list) {
        return ESP_ERR_NO_MEM;
    }
    s_st.index = -1;
    wm_sd_listen(sd_changed, NULL);
    return ESP_OK;
}

static void ensure_task(void)
{
    lock();
    const bool need = s_task == NULL;
    unlock();
    if (!need) {
        return;
    }
    TaskHandle_t h = NULL;
    char why[112];
    if (watch_task_spawn(player_task, "wa_play", TASK_STACK, NULL, TASK_PRIO, TASK_CORE, false, &h, why, sizeof(why))) {
        lock();
        s_task = h;
        unlock();
    } else {
        set_state(WAP_ERROR, why);
    }
}

static void send(uint8_t op, uint32_t arg)
{
    if (!s_q) {
        return;
    }
    const cmd_t c = {op, arg};
    if (xQueueSend(s_q, &c, 0) != pdTRUE) {
        ESP_LOGW(TAG, "órdenes de más: se descarta una");
    }
    ensure_task();
    lock();
    TaskHandle_t t = s_task;
    unlock();
    if (t) {
        xTaskNotifyGive(t);
    }
}

bool wap_play_list(const wap_track_t *list, int n, int start)
{
    if (!s_list || !list || n <= 0) {
        return false;
    }
    if (n > WAP_LIST_MAX) {
        n = WAP_LIST_MAX;
    }
    if (start < 0 || start >= n) {
        start = 0;
    }
    lock();
    memcpy(s_list, list, sizeof(wap_track_t) * (size_t)n);
    s_count = n;
    unlock();
    send(OP_LIST, (uint32_t)start);
    return true;
}

void wap_toggle(void) { send(OP_TOGGLE, 0); }
void wap_pause(void) { send(OP_PAUSE, 0); }
void wap_resume(void) { send(OP_RESUME, 0); }
void wap_stop(void) { send(OP_STOP, 0); }
void wap_next(void) { send(OP_NEXT, 0); }
void wap_prev(void) { send(OP_PREV, 0); }
void wap_seek(uint32_t ms) { send(OP_SEEK, ms); }

void wap_status(wap_status_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        out->index = -1;
        return;
    }
    lock();
    *out = s_st;
    unlock();
}

bool wap_cover_copy(uint16_t *dst, uint32_t *gen)
{
    if (!s_mx || !s_cover || !dst) {
        return false;
    }
    lock();
    const bool ok = s_cover_ok;
    if (ok) {
        memcpy(dst, s_cover, (size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2);
    }
    if (gen) {
        *gen = s_st.cover_gen;
    }
    unlock();
    return ok;
}
