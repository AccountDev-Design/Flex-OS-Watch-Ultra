/*
 * wa_rec — ver include/wa_rec.h.
 */
#include "wa_rec.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "wa_logic.h"
#include "watch_audio.h"
#include "watch_settings.h"
#include "watch_time.h"
#include "wm_store.h"

static const char *TAG = "wa_rec";

#define CAP_FRAMES     256                       /* 10,7 ms a 24 kHz */
#define RING_BYTES     (128 * 1024)              /* 2,7 s en mono, 1,3 s en estéreo */
#define WR_BLOCK       (16 * 1024)
#define CAP_STACK      3072
#define CAP_PRIO       7
#define WR_STACK       6144
#define WR_PRIO        3
#define CHECKPOINT_MS  5000u
#define SPACE_MS       1000u
#define RESERVE_WATCH  FML_RESERVE_BYTES         /* lo que nunca se consume de la memoria interna */
#define RESERVE_SD     (4u * 1024u * 1024u)
#define MAX_FILE       0xFFFFFFFFull             /* FAT32: 4 GB - 1 por archivo (y RIFF de 32 bits) */

enum { STOP_NONE = 0, STOP_SAVE, STOP_CANCEL };

static SemaphoreHandle_t s_mx;
static wr_status_t s_st;
static wa_meter_t s_meter;
static StreamBufferHandle_t s_ring;
static volatile int s_stop;
static volatile bool s_paused;
static volatile bool s_cap_done;
static volatile bool s_cap_failed;
static TaskHandle_t s_cap, s_wr;

/* Lo de la grabación en curso (solo la tocan wr_start y la tarea de escritura). */
static int s_fd = -1;
static bool s_leased;
static char s_rel[FML_PATH_MAX];
static char s_name[FML_NAME_MAX];

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

static void set_msg(wr_state_t st, const char *msg)
{
    lock();
    s_st.state = st;
    snprintf(s_st.message, sizeof(s_st.message), "%s", msg ? msg : "");
    s_st.gen++;
    unlock();
}

static uint64_t reserve_of(wm_vol_t v)
{
    return v == WM_VOL_SD ? RESERVE_SD : RESERVE_WATCH;
}

/* ---------------- plan ---------------- */

void wr_plan(wr_plan_t *out)
{
    memset(out, 0, sizeof(*out));
    const wm_dest_status_t ds = wm_dest_get(&out->vol);
    if (ds != WM_DEST_OK) {
        snprintf(out->note, sizeof(out->note), "%s", wm_dest_text(ds));
        return;
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    out->channels = ws.rec_quality == WS_REC_STEREO ? 2 : 1;
    if (out->vol == WM_VOL_WATCH && out->channels == 2) {
        out->channels = 1;
        snprintf(out->note, sizeof(out->note),
                 "En el reloj se graba en mono: su memoria no escribe estéreo sin cortes. Para estéreo, usa la microSD.");
    }
    wm_vol_info_t vi;
    if (!wm_vol_info(out->vol, &vi)) {
        snprintf(out->note, sizeof(out->note), "%s", wm_dest_text(WM_DEST_SD_NOT_READY));
        return;
    }
    out->seconds_left = wa_rec_seconds_left(vi.free_bytes, reserve_of(out->vol), 0,
                                            wa_bytes_per_s(WA_VOICE_RATE, out->channels), MAX_FILE);
    if (out->seconds_left < 5) {
        snprintf(out->note, sizeof(out->note), "No queda espacio para grabar en %s.", wm_vol_name(out->vol));
        return;
    }
    out->ok = true;
}

/* ---------------- captura ---------------- */

static void cap_task(void *arg)
{
    (void)arg;
    int16_t *tdm = heap_caps_malloc(CAP_FRAMES * WA_TDM_SLOTS * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc(CAP_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lock();
    const bool stereo = s_st.channels == 2;
    unlock();
    uint64_t frames = 0;
    uint64_t dropped = 0;
    while (tdm && pcm && s_stop == STOP_NONE) {
        if (wa_in_read(tdm, CAP_FRAMES * WA_TDM_SLOTS * sizeof(int16_t)) != ESP_OK) {
            s_cap_failed = true;
            break;
        }
        const size_t n = wa_tdm_extract(tdm, CAP_FRAMES, pcm, stereo);
        lock();
        wa_meter_feed(&s_meter, pcm, n, now_ms());
        s_st.level = s_meter.level;
        s_st.clipping = wa_meter_clipping(&s_meter, now_ms());
        unlock();
        if (s_paused) {
            continue; /* el I2S sigue leyendo (no se desborda); lo leído se tira */
        }
        const size_t bytes = n * sizeof(int16_t);
        const size_t sent = xStreamBufferSend(s_ring, pcm, bytes, 0);
        if (sent < bytes) {
            dropped += (bytes - sent) / (stereo ? 4u : 2u); /* el disco no da abasto */
        }
        frames += CAP_FRAMES;
        lock();
        s_st.elapsed_ms = (uint32_t)(frames * 1000u / WA_VOICE_RATE);
        s_st.dropped_ms = (uint32_t)(dropped * 1000u / WA_VOICE_RATE);
        unlock();
    }
    if (!tdm || !pcm) {
        s_cap_failed = true;
    }
    heap_caps_free(tdm);
    heap_caps_free(pcm);
    s_cap_done = true;
    if (s_wr) {
        xTaskNotifyGive(s_wr);
    }
    s_cap = NULL;
    vTaskDelete(NULL);
}

/* ---------------- escritura ---------------- */

static bool write_all(const void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        const ssize_t w = write(s_fd, (const uint8_t *)buf + done, n - done);
        if (w <= 0) {
            return false;
        }
        done += (size_t)w;
    }
    return true;
}

static bool checkpoint(uint32_t rate, uint8_t ch, uint64_t bytes)
{
    uint8_t h[WA_WAV_HDR];
    wa_wav_header(h, rate, ch, bytes > WA_WAV_MAX_DATA ? WA_WAV_MAX_DATA : (uint32_t)bytes);
    const off_t end = (off_t)(WA_WAV_HDR + bytes);
    return lseek(s_fd, 0, SEEK_SET) == 0 && write_all(h, sizeof(h)) && lseek(s_fd, end, SEEK_SET) == end &&
           fsync(s_fd) == 0;
}

static void finish_audio(void)
{
    wa_in_close();
    wa_session_release(WA_OWNER_RECORDER); /* micrófonos, códecs e I2S apagados */
}

static void wr_task(void *arg)
{
    (void)arg;
    uint8_t *buf = heap_caps_malloc(WR_BLOCK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lock();
    const wm_vol_t vol = s_st.vol;
    const uint8_t ch = s_st.channels;
    const uint32_t rate = s_st.rate;
    unlock();
    size_t have = 0;
    uint64_t bytes = 0;
    uint32_t last_cp = now_ms(), last_space = 0;
    const char *why = NULL;    /* motivo de un final no pedido */
    bool card_gone = false, io_failed = !buf;
    for (;;) {
        if (buf) {
            have += xStreamBufferReceive(s_ring, buf + have, WR_BLOCK - have, pdMS_TO_TICKS(100));
        }
        const bool drained = s_cap_done && xStreamBufferIsEmpty(s_ring);
        if (buf && (have == WR_BLOCK || (drained && have > 0))) {
            if (!write_all(buf, have)) {
                const int e = errno;
                if (vol == WM_VOL_SD) {
                    wm_sd_io_error();
                }
                io_failed = true;
                why = e == ENOSPC ? "La memoria se ha llenado: se ha guardado lo grabado."
                                  : "No se pudo escribir la grabación.";
                s_stop = s_stop == STOP_CANCEL ? STOP_CANCEL : STOP_SAVE;
            } else {
                bytes += have;
            }
            have = 0;
        }
        const uint32_t t = now_ms();
        if (vol == WM_VOL_SD) {
            wm_sd_info_t si;
            wm_sd_info(&si);
            if (si.state != WSD_READY && si.state != WSD_REMOVING) {
                card_gone = true; /* retirada: no se puede ni cerrar bien */
            } else if (si.state == WSD_REMOVING && !card_gone && s_stop == STOP_NONE) {
                why = "La microSD se está expulsando: se ha guardado lo grabado.";
                s_stop = STOP_SAVE; /* expulsión pedida: se cierra bien antes de soltarla */
            }
            if (card_gone && s_stop == STOP_NONE) {
                s_stop = STOP_SAVE;
            }
        }
        if (!card_gone && !io_failed && t - last_space >= SPACE_MS) {
            last_space = t;
            wm_vol_info_t vi;
            if (wm_vol_info(vol, &vi)) {
                const uint32_t left = wa_rec_seconds_left(vi.free_bytes, reserve_of(vol), bytes,
                                                          wa_bytes_per_s(rate, ch), MAX_FILE);
                lock();
                s_st.seconds_left = left;
                s_st.bytes = bytes;
                unlock();
                if (s_stop == STOP_NONE && wa_rec_must_stop(vi.free_bytes, reserve_of(vol), bytes, WR_BLOCK, MAX_FILE)) {
                    why = bytes + WA_WAV_HDR + WR_BLOCK > MAX_FILE ? "Se alcanzó el tamaño máximo de archivo: se ha guardado."
                                                                   : "La memoria está casi llena: se ha guardado lo grabado.";
                    s_stop = STOP_SAVE;
                }
            }
        }
        if (!card_gone && !io_failed && t - last_cp >= CHECKPOINT_MS) {
            last_cp = t;
            if (vol == WM_VOL_SD) {
                checkpoint(rate, ch, bytes); /* un corte deja un WAV válido hasta aquí */
            } else {
                fsync(s_fd);
            }
        }
        /* Se sale cuando la captura terminó (s_stop la para en menos de un
         * bloque) y no queda nada por escribir, o ya no se puede escribir. */
        if (s_cap_done && (drained || card_gone || io_failed || !buf)) {
            break;
        }
    }
    if (s_cap_failed && !why) {
        why = "Los micrófonos dejaron de responder: se ha guardado lo grabado.";
    }
    finish_audio();
    heap_caps_free(buf);
    /* Cerrar y publicar. */
    bool ok_file = !card_gone;
    if (ok_file && vol == WM_VOL_SD && bytes) {
        ok_file = checkpoint(rate, ch, bytes);
    } else if (ok_file) {
        fsync(s_fd);
    }
    close(s_fd);
    s_fd = -1;
    lock();
    s_st.bytes = bytes;
    unlock();
    if (s_stop == STOP_CANCEL) {
        if (!card_gone) {
            char rp[WM_PATH_MAX];
            if (wm_vol_path(vol, s_rel, rp, sizeof(rp))) {
                unlink(rp);
            }
        }
        wm_rec_set_active(vol, NULL);
        set_msg(WR_IDLE, "Grabación descartada.");
    } else if (card_gone) {
        wm_rec_set_active(vol, NULL);
        set_msg(WR_ERROR, "Se ha retirado la microSD: lo grabado hasta los últimos segundos se recuperará al volver a "
                          "insertarla.");
    } else if (bytes == 0) {
        char rp[WM_PATH_MAX];
        if (wm_vol_path(vol, s_rel, rp, sizeof(rp))) {
            unlink(rp);
        }
        wm_rec_set_active(vol, NULL);
        set_msg(why ? WR_ERROR : WR_IDLE, why ? why : "No se grabó nada.");
    } else {
        char err[64] = "";
        const uint32_t id = wm_lib_add_file(vol, s_rel, FML_DIR_REC, FML_K_AUDIO, s_name, FML_O_REC, err, sizeof(err));
        wm_rec_set_active(vol, NULL); /* si no se publicó, la recuperación lo hará */
        lock();
        s_st.saved_id = id;
        snprintf(s_st.saved_name, sizeof(s_st.saved_name), "%s", s_name);
        unlock();
        if (id) {
            set_msg(WR_SAVED, why ? why : "Grabación guardada.");
        } else {
            char m[112];
            snprintf(m, sizeof(m), "Se grabó pero no se pudo añadir a la biblioteca (%s); se recuperará.", err);
            set_msg(WR_ERROR, m);
        }
    }
    if (s_leased) {
        wm_sd_unlease();
        s_leased = false;
    }
    ESP_LOGI(TAG, "grabación terminada: %llu B en %s", (unsigned long long)bytes, wm_vol_name(vol));
    vStreamBufferDeleteWithCaps(s_ring);
    s_ring = NULL;
    s_wr = NULL;
    vTaskDelete(NULL);
}

/* ---------------- API ---------------- */

esp_err_t wr_init(void)
{
    if (s_mx) {
        return ESP_OK;
    }
    s_mx = xSemaphoreCreateMutex();
    return s_mx ? ESP_OK : ESP_ERR_NO_MEM;
}

static void make_name(char *out, size_t n)
{
    wt_datetime_t dt;
    if (watch_time_now(&dt) == WATCH_TIME_OK) {
        snprintf(out, n, "Grabación %02d-%02d %02d.%02d.wav", dt.day, dt.month, dt.hour, dt.minute);
    } else {
        snprintf(out, n, "Grabación.wav"); /* sin hora: el almacén numera */
    }
}

static esp_err_t fail_start(const char *msg)
{
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
        char rp[WM_PATH_MAX];
        lock();
        const wm_vol_t v = s_st.vol;
        unlock();
        if (wm_vol_path(v, s_rel, rp, sizeof(rp))) {
            unlink(rp);
        }
        wm_rec_set_active(v, NULL);
    }
    if (s_leased) {
        wm_sd_unlease();
        s_leased = false;
    }
    if (s_ring) {
        vStreamBufferDeleteWithCaps(s_ring);
        s_ring = NULL;
    }
    set_msg(WR_ERROR, msg);
    return ESP_FAIL;
}

esp_err_t wr_start(void)
{
    if (!s_mx) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    const wr_state_t cur = s_st.state;
    unlock();
    if (cur != WR_IDLE && cur != WR_SAVED && cur != WR_ERROR) {
        return ESP_ERR_INVALID_STATE;
    }
    wr_plan_t plan;
    wr_plan(&plan);
    if (!plan.ok) {
        set_msg(WR_ERROR, plan.note);
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    memset(&s_st, 0, sizeof(s_st) - sizeof(s_st.gen));
    s_st.state = WR_STARTING;
    s_st.vol = plan.vol;
    s_st.channels = plan.channels;
    s_st.rate = WA_VOICE_RATE;
    s_st.seconds_left = plan.seconds_left;
    s_st.gen++;
    wa_meter_init(&s_meter);
    unlock();
    s_stop = STOP_NONE;
    s_paused = false;
    s_cap_done = false;
    s_cap_failed = false;
    /* La música se pausa: la grabadora tiene prioridad. */
    if (wa_session_acquire(WA_OWNER_RECORDER, true, NULL, NULL) != ESP_OK) {
        return fail_start("El audio está ocupado y no se ha podido liberar.");
    }
    if (wa_in_open_voice() != ESP_OK) {
        wa_session_release(WA_OWNER_RECORDER);
        return fail_start("No se pudieron encender los micrófonos.");
    }
    if (plan.vol == WM_VOL_SD) {
        if (!wm_sd_lease()) {
            finish_audio();
            return fail_start(wm_dest_text(WM_DEST_SD_NOT_READY));
        }
        s_leased = true;
    }
    snprintf(s_rel, sizeof(s_rel), FML_DIR_RECPART "/rec-%08lx.wav", (unsigned long)esp_random());
    make_name(s_name, sizeof(s_name));
    char rp[WM_PATH_MAX], dir[WM_PATH_MAX];
    if (!wm_vol_path(plan.vol, s_rel, rp, sizeof(rp)) || !wm_vol_path(plan.vol, FML_DIR_RECPART, dir, sizeof(dir))) {
        finish_audio();
        return fail_start("Ruta de grabación no válida.");
    }
    mkdir(dir, 0775); /* la crea también el catálogo; por si acaso */
    wm_rec_set_active(plan.vol, s_rel);
    s_fd = open(rp, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    uint8_t h[WA_WAV_HDR];
    /* microSD: tamaños reales que se corrigen cada 5 s; reloj: "en flujo". */
    wa_wav_header(h, WA_VOICE_RATE, plan.channels, plan.vol == WM_VOL_SD ? 0 : WA_WAV_STREAMING);
    if (s_fd < 0 || !write_all(h, sizeof(h))) {
        finish_audio();
        return fail_start("No se pudo crear el archivo de la grabación.");
    }
    s_ring = xStreamBufferCreateWithCaps(RING_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        finish_audio();
        return fail_start("No hay memoria para grabar.");
    }
    if (xTaskCreatePinnedToCoreWithCaps(wr_task, "wa_recw", WR_STACK, NULL, WR_PRIO, &s_wr, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_wr = NULL;
        finish_audio();
        return fail_start("No hay memoria para grabar.");
    }
    lock();
    s_st.state = WR_RECORDING;
    s_st.gen++;
    unlock();
    if (xTaskCreatePinnedToCore(cap_task, "wa_cap", CAP_STACK, NULL, CAP_PRIO, &s_cap, 0) != pdPASS) {
        s_cap = NULL;
        s_cap_failed = true;
        s_stop = STOP_CANCEL;
        s_cap_done = true; /* la tarea de escritura cierra, borra y suelta el audio */
        xTaskNotifyGive(s_wr);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "grabando en %s (%s, 24 kHz)", wm_vol_name(plan.vol), plan.channels == 2 ? "estéreo" : "mono");
    return ESP_OK;
}

void wr_pause(void)
{
    lock();
    if (s_st.state == WR_RECORDING) {
        s_paused = true;
        s_st.state = WR_PAUSED;
        s_st.gen++;
    }
    unlock();
}

void wr_resume(void)
{
    lock();
    if (s_st.state == WR_PAUSED) {
        s_paused = false;
        s_st.state = WR_RECORDING;
        s_st.gen++;
    }
    unlock();
}

static void request_stop(int how)
{
    lock();
    const bool active = s_st.state == WR_RECORDING || s_st.state == WR_PAUSED;
    if (active) {
        s_st.state = WR_SAVING;
        s_st.gen++;
    }
    unlock();
    if (active) {
        s_paused = false;
        s_stop = how;
    }
}

void wr_stop(void)
{
    request_stop(STOP_SAVE);
}

void wr_cancel(void)
{
    request_stop(STOP_CANCEL);
}

void wr_ack(void)
{
    lock();
    if (s_st.state == WR_SAVED || s_st.state == WR_ERROR) {
        s_st.state = WR_IDLE;
        s_st.message[0] = 0;
        s_st.gen++;
    }
    unlock();
}

void wr_status(wr_status_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    *out = s_st;
    unlock();
}
