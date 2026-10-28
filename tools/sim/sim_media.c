/*
 * Multimedia en la vista previa (docs/MULTIMEDIA.md): la Grabadora, Música
 * (biblioteca y Reproduciendo con el fondo desenfocado desde la portada),
 * Almacenamiento con la microSD, su página, "Guardar en" y el formateo con
 * doble confirmación.
 *
 * Los servicios (watch_media, watch_audio, wa_player, wa_rec) se sustituyen
 * aquí por otros de mentira con el MISMO contrato: la interfaz no ve la
 * diferencia. Lo que es lógica pura (estados de la microSD, textos, rutas,
 * medidor, formatos de tiempo) es el código REAL del firmware (wm_sd_logic.c,
 * wm_fsclass.c, wm_path.c, wm_text.c, wa_logic.c).
 *
 * Comprueba: que no falte ningún glifo, que las páginas no dejen memoria ni
 * temporizadores al cerrarse, que el destino elegido no cambia solo (microSD
 * elegida y sin tarjeta: la grabadora lo dice y no graba), que formatear pide
 * dos confirmaciones y que el fondo desenfocado se dibuja igual por partes
 * que de una vez (sin rastros).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FlexOS_MediaLib.h"
#include "flex_glass.h"
#include "sim.h"
#include "ui_internal.h"
#include "wa_logic.h"
#include "wa_player.h"
#include "wa_rec.h"
#include "watch_audio.h"
#include "watch_media.h"
#include "watch_settings.h"
#include "wm_store.h"
#include "wm_video.h"
#include "wm_web.h"
#include "misc/lv_area_private.h"

/* ======================= watch_media de mentira ======================= */

static wm_sd_info_t s_sd = {.state = WSD_ABSENT, .gen = 1};
static wm_vol_t s_dest = WM_VOL_WATCH;
static uint64_t s_watch_free = 9u * 1024 * 1024, s_watch_total = 16u * 1024 * 1024;
static int s_formats, s_ejects;
static uint32_t s_ticket;

#define LIB_CAP 48
static FlexMlRec s_lib[WM_VOL_N][LIB_CAP];
static int s_lib_n[WM_VOL_N];
static uint32_t s_lib_rev[WM_VOL_N] = {1, 1};
static uint32_t s_next_id = 1;

static void sd_set(wsd_state_t st, wsd_err_t err, uint64_t card, uint64_t total, uint64_t free_b)
{
    memset(&s_sd, 0, sizeof(s_sd));
    s_sd.state = st;
    s_sd.err = err;
    s_sd.card_valid = card != 0;
    s_sd.kind = card > 32ull * 1024 * 1024 * 1024 ? WM_CARD_SDXC : card ? WM_CARD_SDHC : WM_CARD_NONE;
    s_sd.card_bytes = card;
    snprintf(s_sd.name, sizeof(s_sd.name), "%s", card ? "SD64G" : "");
    s_sd.freq_khz = card ? 20000 : 0;
    s_sd.fat_bits = st == WSD_READY ? 32 : 0;
    s_sd.space_valid = st == WSD_READY;
    s_sd.total_bytes = total;
    s_sd.free_bytes = free_b;
    s_sd.can_format = st == WSD_READY || (st == WSD_ERROR && card && err != WSD_E_UNSUPPORTED && err != WSD_E_CARD);
    s_sd.gen++;
    s_lib_rev[WM_VOL_SD] += 7;
}

esp_err_t watch_media_start(void) { return ESP_OK; }
void wm_sd_info(wm_sd_info_t *out) { *out = s_sd; }
uint32_t wm_sd_gen(void) { return s_sd.gen; }
bool wm_sd_lease(void) { return s_sd.state == WSD_READY; }
void wm_sd_unlease(void) {}
void wm_sd_io_error(void) {}
void wm_sd_interest(bool on) { (void)on; }
bool wm_sd_eject(void)
{
    if (s_sd.state != WSD_READY) {
        return false;
    }
    s_ejects++;
    sd_set(WSD_ABSENT, WSD_E_NONE, 0, 0, 0);
    s_sd.ejected = true;
    return true;
}
bool wm_sd_retry(void) { return false; }
uint32_t wm_sd_format_ticket(void) { return s_sd.can_format ? (s_ticket = 0xC0FFEEu) : 0; }
bool wm_sd_format(uint32_t ticket)
{
    if (!ticket || ticket != s_ticket) {
        return false;
    }
    s_ticket = 0;
    s_formats++;
    sd_set(WSD_READY, WSD_E_NONE, s_sd.card_bytes, s_sd.card_bytes - (32u << 20), s_sd.card_bytes - (33u << 20));
    return true;
}
bool wm_sd_listen(wm_sd_listener_t cb, void *ctx) { (void)cb; (void)ctx; return true; }

bool wm_vol_info(wm_vol_t v, wm_vol_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (v == WM_VOL_WATCH) {
        out->ready = sim_stg.state == STG_MOUNTED;
        out->total_bytes = out->ready ? s_watch_total : 0;
        out->free_bytes = out->ready ? s_watch_free : 0;
        return out->ready;
    }
    if (s_sd.state != WSD_READY) {
        return false;
    }
    out->ready = true;
    out->total_bytes = s_sd.total_bytes;
    out->free_bytes = s_sd.free_bytes;
    return true;
}

wm_dest_status_t wm_dest_get(wm_vol_t *vol)
{
    if (vol) {
        *vol = s_dest;
    }
    if (s_dest == WM_VOL_WATCH) {
        return sim_stg.state == STG_MOUNTED ? WM_DEST_OK : WM_DEST_WATCH_NOT_READY;
    }
    return s_sd.state == WSD_READY ? WM_DEST_OK : s_sd.state == WSD_ABSENT ? WM_DEST_SD_ABSENT : WM_DEST_SD_NOT_READY;
}
void wm_dest_set(wm_vol_t vol) { s_dest = vol; }

/* ---- biblioteca ---- */

static FlexMlRec *lib_add(wm_vol_t v, const char *path, const char *title, const char *artist, uint32_t dur, uint32_t when)
{
    if (s_lib_n[v] >= LIB_CAP) {
        return NULL;
    }
    FlexMlRec *r = &s_lib[v][s_lib_n[v]++];
    memset(r, 0, sizeof(*r));
    r->id = s_next_id++;
    r->kind = FML_K_AUDIO;
    r->fmt = strstr(path, ".mp3") ? FML_F_MP3 : FML_F_WAV_PCM;
    r->flags = FML_R_PLAYABLE;
    r->durMs = dur;
    r->created = r->added = when;
    snprintf(r->path, sizeof(r->path), "%s", path);
    const char *slash = strrchr(path, '/');
    snprintf(r->name, sizeof(r->name), "%s", slash ? slash + 1 : path);
    snprintf(r->title, sizeof(r->title), "%s", title ? title : "");
    snprintf(r->artist, sizeof(r->artist), "%s", artist ? artist : "");
    s_lib_rev[v]++;
    return r;
}

esp_err_t wm_lib_start(void) { return ESP_OK; }
bool wm_lib_ready(wm_vol_t v) { return v == WM_VOL_WATCH ? sim_stg.state == STG_MOUNTED : s_sd.state == WSD_READY; }
uint32_t wm_lib_rev(wm_vol_t v) { return wm_lib_ready(v) ? s_lib_rev[v] : 0; }
bool wm_lib_busy(wm_vol_t v, uint16_t *pending)
{
    (void)v;
    if (pending) {
        *pending = 0;
    }
    return false;
}
int wm_lib_snapshot(wm_vol_t v, uint32_t mask, FlexMlRec *dst, int cap, uint32_t *rev)
{
    if (rev) {
        *rev = wm_lib_rev(v);
    }
    if (!wm_lib_ready(v)) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < s_lib_n[v] && n < cap; i++) {
        if (FML_MASK(s_lib[v][i].kind) & mask) {
            dst[n++] = s_lib[v][i];
        }
    }
    return n;
}
bool wm_lib_get(wm_vol_t v, uint32_t id, FlexMlRec *out)
{
    for (int i = 0; wm_lib_ready(v) && i < s_lib_n[v]; i++) {
        if (s_lib[v][i].id == id) {
            *out = s_lib[v][i];
            return true;
        }
    }
    return false;
}
uint32_t wm_lib_add_file(wm_vol_t v, const char *tmp_rel, const char *dir, int kind, const char *shown_name,
                         uint8_t origin, char *why, size_t why_cap)
{
    (void)tmp_rel, (void)kind, (void)origin, (void)why, (void)why_cap;
    char p[FML_PATH_MAX];
    snprintf(p, sizeof(p), "%s/%s", dir, shown_name);
    FlexMlRec *r = lib_add(v, p, NULL, NULL, 12000, 1760001000u + s_next_id);
    return r ? r->id : 0;
}
bool wm_lib_delete(wm_vol_t v, uint32_t id)
{
    for (int i = 0; i < s_lib_n[v]; i++) {
        if (s_lib[v][i].id == id) {
            memmove(&s_lib[v][i], &s_lib[v][i + 1], sizeof(FlexMlRec) * (size_t)(s_lib_n[v] - i - 1));
            s_lib_n[v]--;
            s_lib_rev[v]++;
            return true;
        }
    }
    return false;
}
bool wm_lib_trash(wm_vol_t v, uint32_t id) { return wm_lib_delete(v, id); }
bool wm_lib_rename(wm_vol_t v, uint32_t id, const char *name, char *why, size_t why_cap)
{
    (void)v, (void)id, (void)name, (void)why, (void)why_cap;
    return false;
}
void wm_lib_rescan(wm_vol_t v) { (void)v; }
FlexMediaStore *wm_lib_acquire(wm_vol_t v) { (void)v; return NULL; }
void wm_lib_release(wm_vol_t v) { (void)v; }
void wm_rec_set_active(wm_vol_t v, const char *rel) { (void)v, (void)rel; }
bool wm_heavy_begin(uint32_t wait_ms) { (void)wait_ms; return true; }
void wm_heavy_end(void) {}
void *wm_alloc(size_t n) { return n ? malloc(n) : NULL; }
void wm_free(void *p) { free(p); }

/* ---- Flex Web Server de mentira (wm_web) ---- */
static bool s_web_wanted, s_web_nowifi;
static uint32_t s_web_born, s_web_gen;
static int s_web_newcodes, s_web_drops, s_web_sessions;
static char s_web_code[8] = "482913";
static wm_web_status_t s_web_x; /* lo que se recibe: x_* */
static wm_web_status_t s_web_last;

void wm_web_keep(void)
{
    if (!s_web_wanted) {
        s_web_born = lv_tick_get(); /* cada vez que se abre, código nuevo */
    }
    s_web_wanted = true;
}
void wm_web_release(void) { s_web_wanted = false; }
void wm_web_new_code(void)
{
    s_web_newcodes++;
    snprintf(s_web_code, sizeof(s_web_code), "%06d", 731205 + s_web_newcodes);
    s_web_born = lv_tick_get();
}
void wm_web_drop_all(void)
{
    s_web_drops++;
    s_web_sessions = 0;
    wm_web_new_code();
}
void wm_web_status(wm_web_status_t *out)
{
    wm_web_status_t n = s_web_x;
    n.gen = 0;
    n.state = !s_web_wanted ? WWEB_OFF : s_web_nowifi ? WWEB_NO_WIFI : WWEB_READY;
    if (n.state == WWEB_READY) {
        snprintf(n.ip, sizeof(n.ip), "192.168.1.50");
        n.port = WWEB_PORT;
        snprintf(n.code, sizeof(n.code), "%s", s_web_code);
        n.code_left_s = wweb_code_left_s(lv_tick_get(), s_web_born, WWEB_CODE_TTL_MS);
        n.sessions = s_web_sessions;
    }
    wm_vol_t v = WM_VOL_WATCH;
    n.dest_ok = wm_dest_get(&v) == WM_DEST_OK;
    n.dest = v;
    s_web_last.gen = 0;
    if (memcmp(&n, &s_web_last, sizeof(n)) != 0) {
        s_web_gen++;
        s_web_last = n;
    }
    n.gen = s_web_gen;
    *out = n;
}

/* ---- reproductor de vídeo de mentira (wm_video): fotogramas sintéticos ---- */
#define FV_W 160
#define FV_H 90
static bool s_wv_open, s_wv_fail;
static int s_wv_opens, s_wv_closes;
static wv_status_t s_wv;
static uint32_t s_wv_t0, s_wv_base;
static uint16_t s_wv_buf[2][FV_W * FV_H];
static int s_wv_front = -1;
static uint32_t s_wv_painted = 0xFFFFFFFFu;

static uint32_t wv_pos(void)
{
    uint32_t pos = s_wv_base + (s_wv.state == WV_PLAYING ? lv_tick_elaps(s_wv_t0) : 0);
    return pos > s_wv.dur_ms ? s_wv.dur_ms : pos;
}

bool wv_open(wm_vol_t vol, const char *rel, char *why, size_t n)
{
    (void)vol;
    (void)rel;
    if (s_wv_fail) {
        snprintf(why, n, "%s", wv_err_text(-3));
        return false;
    }
    s_wv_opens++;
    memset(&s_wv, 0, sizeof(s_wv));
    s_wv.state = WV_PLAYING;
    s_wv.w = s_wv.src_w = FV_W;
    s_wv.h = s_wv.src_h = FV_H;
    s_wv.dur_ms = 6000;
    s_wv.fps_x10 = 120;
    s_wv.gen = 1;
    s_wv_t0 = lv_tick_get();
    s_wv_base = 0;
    s_wv_front = -1;
    s_wv_painted = 0xFFFFFFFFu;
    s_wv_open = true;
    return true;
}
void wv_toggle(void)
{
    if (s_wv.state == WV_PLAYING) {
        s_wv_base = wv_pos();
        s_wv.state = WV_PAUSED;
    } else if (s_wv.state == WV_ENDED) {
        s_wv_base = 0;
        s_wv_t0 = lv_tick_get();
        s_wv.state = WV_PLAYING;
    } else if (s_wv.state == WV_PAUSED) {
        s_wv_t0 = lv_tick_get();
        s_wv.state = WV_PLAYING;
    }
    s_wv.gen++;
}
void wv_seek_ms(uint32_t ms)
{
    s_wv_base = ms;
    s_wv_t0 = lv_tick_get();
    s_wv.gen++;
}
void wv_close(void)
{
    s_wv_closes++;
    s_wv_open = false;
}
void wv_status(wv_status_t *out)
{
    if (s_wv_open && s_wv.state == WV_PLAYING) {
        s_wv.pos_ms = wv_pos();
        if (s_wv.pos_ms >= s_wv.dur_ms) {
            s_wv_base = s_wv.dur_ms;
            s_wv.state = WV_ENDED;
            s_wv.gen++;
        }
        s_wv.frame_gen = s_wv.pos_ms / 83u + 1u;
    }
    *out = s_wv;
}
const uint16_t *wv_frame_acquire(int *w, int *h, uint32_t *gen)
{
    if (!s_wv_open) {
        return NULL;
    }
    wv_status_t st;
    wv_status(&st);
    if (st.frame_gen != s_wv_painted) {
        /* pinta el fotograma en el lienzo que no está a la vista: una barra que avanza */
        s_wv_front = s_wv_front == 0 ? 1 : 0;
        const int bx = (int)(st.frame_gen * 4u % FV_W);
        for (int y = 0; y < FV_H; y++) {
            for (int x = 0; x < FV_W; x++) {
                s_wv_buf[s_wv_front][y * FV_W + x] =
                    (x >= bx && x < bx + 12) ? 0xFFFF : (uint16_t)(((y * 31 / FV_H) << 11) | ((x * 63 / FV_W) << 5) | 12);
            }
        }
        s_wv_painted = st.frame_gen;
    }
    *w = FV_W;
    *h = FV_H;
    *gen = st.frame_gen;
    return s_wv_buf[s_wv_front];
}

/* ======================= audio de mentira ======================= */

static uint8_t s_vol = 60;
esp_err_t watch_audio_start(i2c_master_bus_handle_t bus) { (void)bus; return ESP_OK; }
void wa_set_volume(uint8_t percent) { s_vol = percent > 100 ? 100 : percent; }
uint8_t wa_get_volume(void) { return s_vol; }
const char *wa_owner_name(wa_owner_t who) { return who == WA_OWNER_RECORDER ? "Grabadora" : "Música"; }

/* ---- reproductor ---- */

static wap_status_t s_ps = {.index = -1};
static wap_track_t s_plist[8];
static int s_pn;
static uint32_t s_play_t0, s_play_pos0;
static uint16_t *s_cover;

static void ps_track(int i)
{
    s_ps.index = i;
    s_ps.count = s_pn;
    s_ps.track = s_plist[i];
    s_ps.dur_ms = s_plist[i].dur_ms ? s_plist[i].dur_ms : 200000;
    s_ps.rate = 44100;
    s_ps.channels = 2;
    snprintf(s_ps.format, sizeof(s_ps.format), "MP3 256k");
    s_ps.state = WAP_PLAYING;
    s_ps.has_cover = i == 0; /* la primera trae portada; las demás no */
    s_ps.cover_gen++;
    s_ps.gen++;
    s_play_t0 = lv_tick_get();
    s_play_pos0 = 0;
}

esp_err_t wap_init(void) { return ESP_OK; }
bool wap_play_list(const wap_track_t *list, int n, int start)
{
    s_pn = n > 8 ? 8 : n;
    memcpy(s_plist, list, sizeof(wap_track_t) * (size_t)s_pn);
    ps_track(start < s_pn ? start : 0);
    return true;
}
void wap_toggle(void)
{
    if (s_ps.state == WAP_PLAYING) {
        s_play_pos0 += lv_tick_elaps(s_play_t0);
        s_ps.state = WAP_PAUSED;
    } else if (s_ps.index >= 0) {
        s_play_t0 = lv_tick_get();
        s_ps.state = WAP_PLAYING;
    }
    s_ps.gen++;
}
void wap_pause(void) { if (s_ps.state == WAP_PLAYING) wap_toggle(); }
void wap_resume(void) { if (s_ps.state == WAP_PAUSED) wap_toggle(); }
void wap_stop(void) { s_ps.state = WAP_IDLE; s_ps.index = -1; s_ps.gen++; }
void wap_next(void) { if (s_ps.index + 1 < s_pn) ps_track(s_ps.index + 1); }
void wap_prev(void) { ps_track(s_ps.index > 0 ? s_ps.index - 1 : 0); }
void wap_seek(uint32_t ms) { s_play_pos0 = ms; s_play_t0 = lv_tick_get(); }
void wap_status(wap_status_t *out)
{
    *out = s_ps;
    if (s_ps.state == WAP_PLAYING) {
        out->pos_ms = s_play_pos0 + lv_tick_elaps(s_play_t0);
    } else {
        out->pos_ms = s_play_pos0;
    }
    if (out->dur_ms && out->pos_ms > out->dur_ms) {
        out->pos_ms = out->dur_ms;
    }
}
bool wap_cover_copy(uint16_t *dst, uint32_t *gen)
{
    if (gen) {
        *gen = s_ps.cover_gen;
    }
    if (!s_ps.has_cover) {
        return false;
    }
    if (!s_cover) {
        /* Portada sintética: degradado cálido con un disco claro (se ve el desenfoque). */
        s_cover = malloc((size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2);
        for (int y = 0; y < WAP_COVER_SIDE; y++) {
            for (int x = 0; x < WAP_COVER_SIDE; x++) {
                const int dx = x - 120, dy = y - 70;
                const bool disc = dx * dx + dy * dy < 40 * 40;
                const int r = disc ? 31 : 8 + x * 20 / WAP_COVER_SIDE;
                const int g = disc ? 58 : 6 + y * 30 / WAP_COVER_SIDE;
                const int b = disc ? 20 : 26 - y * 18 / WAP_COVER_SIDE;
                s_cover[y * WAP_COVER_SIDE + x] = (uint16_t)(r << 11 | g << 5 | b);
            }
        }
    }
    memcpy(dst, s_cover, (size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2);
    return true;
}
const char *wap_state_name(wap_state_t st)
{
    static const char *const n[] = {"Parado", "Cargando", "Reproduciendo", "En pausa", "Terminado", "Error"};
    return st <= WAP_ERROR ? n[st] : "?";
}

/* ---- grabadora ---- */

static wr_status_t s_rs;
static uint32_t s_rec_t0, s_rec_acc;
static int s_rec_starts;

esp_err_t wr_init(void) { return ESP_OK; }
void wr_plan(wr_plan_t *out)
{
    memset(out, 0, sizeof(*out));
    const wm_dest_status_t ds = wm_dest_get(&out->vol);
    if (ds != WM_DEST_OK) {
        snprintf(out->note, sizeof(out->note), "%s", wm_dest_text(ds));
        return;
    }
    out->channels = 1;
    wm_vol_info_t vi;
    wm_vol_info(out->vol, &vi);
    out->seconds_left = wa_rec_seconds_left(vi.free_bytes, out->vol == WM_VOL_SD ? 4u << 20 : 512u << 10, 0,
                                            wa_bytes_per_s(24000, 1), 0xFFFFFFFFull);
    out->ok = out->seconds_left >= 5;
}
esp_err_t wr_start(void)
{
    wr_plan_t p;
    wr_plan(&p);
    if (!p.ok) {
        s_rs.state = WR_ERROR;
        snprintf(s_rs.message, sizeof(s_rs.message), "%s", p.note);
        s_rs.gen++;
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_rs, 0, sizeof(s_rs) - sizeof(s_rs.gen));
    s_rs.state = WR_RECORDING;
    s_rs.vol = p.vol;
    s_rs.channels = 1;
    s_rs.rate = 24000;
    s_rs.seconds_left = p.seconds_left;
    s_rs.gen++;
    s_rec_t0 = lv_tick_get();
    s_rec_acc = 0;
    s_rec_starts++;
    return ESP_OK;
}
void wr_pause(void)
{
    if (s_rs.state == WR_RECORDING) {
        s_rec_acc += lv_tick_elaps(s_rec_t0);
        s_rs.state = WR_PAUSED;
        s_rs.gen++;
    }
}
void wr_resume(void)
{
    if (s_rs.state == WR_PAUSED) {
        s_rec_t0 = lv_tick_get();
        s_rs.state = WR_RECORDING;
        s_rs.gen++;
    }
}
void wr_stop(void)
{
    if (s_rs.state == WR_RECORDING || s_rs.state == WR_PAUSED) {
        char why[8];
        char name[FML_NAME_MAX];
        snprintf(name, sizeof(name), "Grabación %02d-10 18.%02d.wav", 27 + s_rec_starts % 2, s_rec_starts);
        s_rs.saved_id = wm_lib_add_file(s_rs.vol, "/System/Media/rec/x.wav", FML_DIR_REC, FML_K_AUDIO, name, 0, why, sizeof(why));
        s_rs.state = WR_SAVED;
        snprintf(s_rs.message, sizeof(s_rs.message), "Grabación guardada.");
        s_rs.gen++;
    }
}
void wr_cancel(void)
{
    if (s_rs.state == WR_RECORDING || s_rs.state == WR_PAUSED) {
        s_rs.state = WR_IDLE;
        snprintf(s_rs.message, sizeof(s_rs.message), "Grabación descartada.");
        s_rs.gen++;
    }
}
void wr_ack(void)
{
    if (s_rs.state == WR_SAVED || s_rs.state == WR_ERROR) {
        s_rs.state = WR_IDLE;
        s_rs.message[0] = 0;
        s_rs.gen++;
    }
}
void wr_status(wr_status_t *out)
{
    *out = s_rs;
    if (s_rs.state == WR_RECORDING || s_rs.state == WR_PAUSED) {
        const uint32_t ms = s_rec_acc + (s_rs.state == WR_RECORDING ? lv_tick_elaps(s_rec_t0) : 0);
        out->elapsed_ms = ms;
        out->level = s_rs.state == WR_RECORDING ? (float)(0.45 + 0.4 * sin(ms / 180.0) * sin(ms / 1100.0)) : 0.0f;
        if (out->level < 0) {
            out->level = 0.05f;
        }
    }
}

/* ======================= la batería ======================= */

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

static bool has(const char *t)
{
    return sim_find_label(ui_nav_top(), t) != NULL;
}

static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static lv_obj_t *tap_prefix(const char *prefix)
{
    lv_obj_t *l = sim_find_label_prefix(ui_nav_top(), prefix);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static const char *text_prefix(const char *prefix)
{
    lv_obj_t *l = sim_find_label_prefix(ui_nav_top(), prefix);
    return l ? lv_label_get_text(l) : "";
}

static void page(const char *out, const char *name)
{
    sim_run(300);
    sim_check_glyphs(ui_nav_top(), name);
    sim_lint(name);
    sim_save(out, name);
}

/* Botones redondos de la grabadora (sin texto): por su posición. */
static lv_obj_t *button_at(int32_t x, int32_t y)
{
    lv_obj_t *top = ui_nav_top();
    lv_obj_t *best = NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) && x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
            best = c;
        }
    }
    return best;
}

/* El QR se ve: una imagen cuadrada solo de negro y blanco puros (módulos
 * enteros, sin escalado ni suavizado), con los dos colores presentes. */
static bool qr_find(lv_obj_t *o, lv_area_t *out)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(o, &lv_image_class) && lv_obj_get_width(o) >= 120 &&
        lv_obj_get_width(o) == lv_obj_get_height(o)) {
        /* solo la parte que se ve: la recortan sus contenedores y la pantalla */
        lv_obj_get_coords(o, out);
        for (lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p)) {
            lv_area_t pc;
            lv_obj_get_coords(p, &pc);
            if (!lv_area_intersect(out, out, &pc)) {
                return false;
            }
        }
        const lv_area_t scr = {0, 0, SIM_W - 1, SIM_H - 1};
        return lv_area_intersect(out, out, &scr) && lv_area_get_height(out) >= 40;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (qr_find(lv_obj_get_child(o, (int32_t)i), out)) {
            return true;
        }
    }
    return false;
}

static bool qr_on_screen(void)
{
    lv_area_t a;
    if (!qr_find(ui_nav_top(), &a)) {
        return false;
    }
    lv_refr_now(NULL);
    int black = 0, white = 0, other = 0;
    for (int32_t y = a.y1; y <= a.y2; y++) {
        for (int32_t x = a.x1; x <= a.x2; x++) {
            const uint16_t c = sim_fb[y * SIM_W + x];
            black += c == 0x0000;
            white += c == 0xFFFF;
            other += c != 0x0000 && c != 0xFFFF;
        }
    }
    const int n = black + white + other;
    return other == 0 && black > n / 5 && white > n / 5;
}

/* El texto está y se ve (ni él ni sus contenedores ocultos). */
static bool shown(const char *t)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), t);
    for (lv_obj_t *o = l; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    return l != NULL;
}

/* El fotograma del vídeo a la vista (la única imagen RGB565 de la página). */
static bool video_frame(lv_area_t *out)
{
    lv_obj_t *top = ui_nav_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (lv_obj_check_type(c, &lv_image_class) && !lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) && lv_image_get_src(c)) {
            lv_obj_get_coords(c, out);
            return true;
        }
    }
    return false;
}

/* Un redibujado completo da la misma imagen que la que dejaron los dibujos por
 * partes: el fondo desenfocado no arrastra nada. */
static void same_as_full(const char *where)
{
    static uint16_t before[SIM_W * SIM_H];
    lv_refr_now(NULL);
    memcpy(before, sim_fb, sizeof(before));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    int diff = 0, x1 = SIM_W, y1 = SIM_H, x2 = -1, y2 = -1;
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        if (before[i] != sim_fb[i]) {
            const int x = i % SIM_W, y = i / SIM_W;
            diff++;
            x1 = x < x1 ? x : x1;
            x2 = x > x2 ? x : x2;
            y1 = y < y1 ? y : y1;
            y2 = y > y2 ? y : y2;
        }
    }
    EXPECT(diff == 0, "%s: el redibujado completo cambia %d px en (%d,%d)-(%d,%d) (rastro del dibujo por partes)",
           where, diff, x1, y1, x2, y2);
}

void sim_suite_media(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(300);
    const watch_storage_info_t saved = sim_stg;
    sim_stg.state = STG_MOUNTED;
    sim_stg.gate = STG_GATE_OK;
    sim_stg.total = 16u << 20;
    sim_stg.used = 7u << 20;
    s_dest = WM_VOL_WATCH;
    sd_set(WSD_ABSENT, WSD_E_NONE, 0, 0, 0);
    memset(s_lib_n, 0, sizeof(s_lib_n));
    lib_add(WM_VOL_WATCH, "/Musica/Clair de lune.mp3", "Clair de lune", "Debussy", 301000, 1760000000);
    lib_add(WM_VOL_WATCH, "/Musica/Nana.wav", "", "", 95000, 1760000100);
    lib_add(WM_VOL_WATCH, FML_DIR_REC "/Grabación 26-10 09.12.wav", NULL, NULL, 65000, 1760000200);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* ---- Grabadora en reposo, grabando, en pausa y guardada ---- */
    ui_nav_push(ui_recorder_create);
    page(out, "m_grabadora_reposo");
    EXPECT(has("Grabadora") && has("0:00"), "grabadora: falta el título o el tiempo");
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Reloj ·") != NULL, "grabadora: el chip no dice el destino");
    const int32_t cx = SIM_W / 2, by = 384 + 52;
    click(button_at(cx, by));
    sim_run(3200);
    EXPECT(s_rs.state == WR_RECORDING, "grabadora: no empieza a grabar");
    EXPECT(has("0:03") || has("0:02"), "grabadora: el tiempo no avanza");
    sim_save(out, "m_grabadora_grabando");
    sim_check_glyphs(ui_nav_top(), "grabando");
    click(button_at(cx + 52 + 24 + 30, by)); /* pausa */
    sim_run(400);
    EXPECT(s_rs.state == WR_PAUSED, "grabadora: no se pausa");
    sim_save(out, "m_grabadora_pausa");
    click(button_at(cx - 52 - 24 - 30, by)); /* descartar: primer toque, no descarta */
    sim_run(200);
    EXPECT(s_rs.state == WR_PAUSED, "grabadora: un toque en la papelera descarta sin confirmar");
    EXPECT(has("Toca otra vez la papelera para descartar la grabación"), "grabadora: no pide confirmación");
    sim_run(4300); /* la confirmación caduca */
    click(button_at(cx, by)); /* parar y guardar */
    sim_run(400);
    EXPECT(s_rs.state == WR_SAVED && has("Grabación guardada."), "grabadora: no guarda");
    sim_save(out, "m_grabadora_guardada");

    /* microSD elegida y sin tarjeta: lo dice y NO graba en el reloj. */
    s_dest = WM_VOL_SD;
    sim_run(700);
    EXPECT(sim_find_label_prefix(ui_nav_top(), "microSD: sin tarjeta") != NULL, "grabadora: el chip no avisa");
    const int starts = s_rec_starts;
    click(button_at(cx, by));
    sim_run(400);
    EXPECT(s_rec_starts == starts && s_rs.state != WR_RECORDING, "grabadora: graba sin el destino elegido");
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Has elegido guardar en la microSD") != NULL,
           "grabadora: no explica por qué no graba");
    EXPECT(s_dest == WM_VOL_SD, "grabadora: cambió el destino sola");
    page(out, "m_grabadora_sin_sd");
    s_dest = WM_VOL_WATCH;

    /* Grabaciones: la de antes y la nueva, de la más nueva a la más antigua. */
    wr_ack();
    sim_run(600);
    click(button_at(cx - 52 - 24 - 30, by));
    sim_run(400);
    EXPECT(has("Grabaciones"), "grabaciones: no se abre la lista");
    page(out, "m_grabaciones");
    {
        /* Estéreo en la microSD: el interruptor cambia el ajuste de la grabadora */
        lv_obj_t *l = sim_find_label(ui_nav_top(), "Estéreo (microSD)");
        lv_obj_t *r = l ? lv_obj_get_parent(l) : NULL, *sw = NULL;
        for (uint32_t i = 0; r && i < lv_obj_get_child_count(r); i++) {
            lv_obj_t *c = lv_obj_get_child(r, (int32_t)i);
            if (lv_obj_check_type(c, &lv_switch_class)) {
                sw = c;
            }
        }
        EXPECT(sw != NULL, "grabaciones: falta el interruptor de estéreo");
        if (sw) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
            lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
            watch_settings_t ws;
            watch_settings_get(&ws);
            EXPECT(ws.rec_quality == WS_REC_STEREO, "grabaciones: el interruptor no cambia la grabadora");
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
            lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
            watch_settings_get(&ws);
            EXPECT(ws.rec_quality == WS_REC_MONO, "grabaciones: no vuelve a mono");
        }
    }
    click(tap_of("Grabación 26-10 09.12"));
    sim_run(400);
    EXPECT(s_ps.index >= 0 && s_ps.state == WAP_PLAYING, "grabaciones: tocar una no la reproduce");
    ui_nav_back();
    sim_run(300);
    ui_nav_back();
    sim_run(300);
    ui_nav_back();
    sim_run(400);

    /* ---- Música: raíz, canciones y Reproduciendo con el fondo desenfocado ---- */
    ui_nav_push(ui_music_create);
    page(out, "m_musica");
    EXPECT(has("Canciones") && has("En el teléfono") && has("Grabaciones"), "música: faltan secciones");
    click(tap_of("Canciones"));
    sim_run(400);
    EXPECT(has("Clair de lune") && has("Nana"), "canciones: faltan temas");
    EXPECT(!has("Grabación 26-10 09.12"), "canciones: mezcla las grabaciones con la música");
    page(out, "m_canciones");
    click(tap_of("Clair de lune"));
    sim_run(600);
    EXPECT(has("Clair de lune") && has("Debussy"), "reproduciendo: falta el título o el artista");
    page(out, "m_reproduciendo");
    same_as_full("reproduciendo con portada");
    sim_run(1500);
    same_as_full("reproduciendo tras avanzar");
    wap_next(); /* la siguiente no tiene portada: el fondo se quita */
    sim_run(500);
    page(out, "m_reproduciendo_sin_portada");
    same_as_full("reproduciendo sin portada");
    ui_nav_back();
    sim_run(300);
    ui_nav_back();
    sim_run(300);
    page(out, "m_musica_sonando");

    /* ---- Recibir desde el móvil: QR, código, caducidad, Wi-Fi y destino ---- */
    click(tap_of("Añadir desde el móvil"));
    sim_run(600);
    EXPECT(s_web_wanted, "desde el móvil: no arranca el servidor");
    EXPECT(has("482 913") && has("192.168.1.50:8080"), "desde el móvil: falta el código o la dirección");
    EXPECT(has("Caduca en 10:00") || has("Caduca en 9:59"), "desde el móvil: falta la caducidad");
    EXPECT(qr_on_screen(), "desde el móvil: no se ve el QR (o no es nítido)");
    page(out, "m_desde_movil");
    same_as_full("desde el móvil");
    s_web_sessions = 1;
    s_web_x.x_state = WWEB_X_RUN;
    s_web_x.x_done = 450000;
    s_web_x.x_total = 1000000;
    snprintf(s_web_x.x_name, sizeof(s_web_x.x_name), "Playa.jpg");
    sim_run(400);
    EXPECT(has("Recibiendo · 45 %") && has("Playa.jpg"), "desde el móvil: no enseña lo que llega");
    EXPECT(sim_find_label_prefix(ui_nav_top(), "1 móvil conectado") != NULL, "desde el móvil: no cuenta los móviles");
    page(out, "m_desde_movil_recibiendo");
    s_web_x.x_state = WWEB_X_DONE;
    sim_run(400);
    EXPECT(has("Guardado en Reloj"), "desde el móvil: no dice dónde se guardó");
    s_web_born = lv_tick_get() - WWEB_CODE_TTL_MS; /* caduca */
    sim_run(400);
    EXPECT(has("El código ha caducado.") && !qr_on_screen(), "desde el móvil: enseña un QR caducado");
    page(out, "m_desde_movil_caducado");
    click(tap_of("Código nuevo"));
    sim_run(400);
    EXPECT(s_web_newcodes == 1 && has("731 206") && qr_on_screen(), "desde el móvil: el código nuevo no llega");
    click(tap_of("Desconectar móviles"));
    sim_run(400);
    EXPECT(s_web_drops == 1 && s_web_sessions == 0, "desde el móvil: no desconecta");
    s_web_nowifi = true;
    sim_run(400);
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Conecta el reloj a una Wi-Fi") != NULL && !qr_on_screen(),
           "desde el móvil: sin Wi-Fi no lo dice");
    page(out, "m_desde_movil_sin_wifi");
    s_web_nowifi = false;
    s_dest = WM_VOL_SD; /* microSD elegida y sin tarjeta */
    sim_run(400);
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Has elegido guardar en la microSD") != NULL,
           "desde el móvil: no avisa del destino que no está");
    s_dest = WM_VOL_WATCH;
    s_web_x.x_state = WWEB_X_NONE;
    ui_nav_back();
    sim_run(300);
    EXPECT(!s_web_wanted, "desde el móvil: al salir sigue pidiendo el servidor");

    /* ---- Vídeo AVI MJPEG: fotogramas, mandos, pausa, final y cierre ---- */
    ui_video_open(1, "/Videos/clip.avi", "clip.avi");
    sim_run(500);
    EXPECT(s_wv_opens == 1 && s_wv_open, "vídeo: no se abrió");
    {
        lv_area_t a;
        EXPECT(video_frame(&a), "vídeo: no se ve el fotograma");
        EXPECT(lv_area_get_width(&a) == FV_W && lv_area_get_height(&a) == FV_H, "vídeo: el fotograma no es 1:1");
    }
    const uint32_t fg0 = s_wv.frame_gen;
    sim_run(400);
    EXPECT(s_wv.frame_gen > fg0 + 2, "vídeo: no avanza");
    page(out, "m_video");
    same_as_full("vídeo");
    sim_run(3200);
    EXPECT(!shown("Vídeo"), "vídeo: los mandos no se ocultan solos reproduciendo");
    sim_save(out, "m_video_sin_mandos");
    wv_toggle(); /* la pausa (como el botón) */
    sim_run(400);
    EXPECT(s_wv.state == WV_PAUSED && shown("Vídeo"), "vídeo: en pausa los mandos no vuelven");
    const uint32_t fp = s_wv.frame_gen;
    sim_run(500);
    EXPECT(s_wv.frame_gen == fp, "vídeo: en pausa sigue avanzando");
    wv_toggle();
    sim_run(6500);
    EXPECT(s_wv.state == WV_ENDED && has("6:00") == false && has("0:06"), "vídeo: no llega al final");
    page(out, "m_video_final");
    ui_nav_back();
    sim_run(300);
    EXPECT(s_wv_closes == 1 && !s_wv_open, "vídeo: al salir no se cierra");
    s_wv_fail = true; /* un AVI que no es MJPEG */
    ui_video_open(0, "/Videos/otro.avi", "otro.avi");
    sim_run(400);
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Este AVI no es MJPEG") != NULL, "vídeo: no dice por qué no se abre");
    page(out, "m_video_error");
    ui_nav_back();
    sim_run(300);
    EXPECT(s_wv_closes == 1, "vídeo: cierra lo que no llegó a abrir");
    s_wv_fail = false;
    ui_nav_back();
    sim_run(400);

    /* ---- Almacenamiento con microSD, su página y formatear ---- */
    sd_set(WSD_READY, WSD_E_NONE, 63864569856ull, 63831015424ull, 41230000000ull);
    ui_nav_push(ui_storage_create);
    sim_run(1200);
    EXPECT(has("microSD"), "almacenamiento: falta la microSD");
    page(out, "m_almacenamiento");
    click(tap_of("microSD"));
    sim_run(500);
    EXPECT(has("Expulsar") && has("Formatear en FAT32"), "microSD: faltan acciones");
    page(out, "m_microsd");
    click(tap_of("Guardar en"));
    sim_run(400);
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Lo nuevo (grabaciones") != NULL, "microSD: no abre Guardar en");
    page(out, "m_guardar_en");
    click(tap_of("microSD"));
    sim_run(300);
    EXPECT(s_dest == WM_VOL_SD, "guardar en: no cambia el destino al tocarlo");
    ui_nav_back();
    sim_run(300);
    click(tap_of("Formatear en FAT32"));
    sim_run(400);
    page(out, "m_formatear");
    const int f_before = s_formats;
    click(tap_of("Formatear"));
    sim_run(200);
    EXPECT(s_formats == f_before, "formatear: un solo toque formatea");
    EXPECT(has("Sí, borrar todo"), "formatear: no pide la segunda confirmación");
    sim_save(out, "m_formatear_confirmar");
    click(tap_of("Sí, borrar todo"));
    sim_run(400);
    EXPECT(s_formats == f_before + 1, "formatear: la doble confirmación no formatea");
    ui_nav_back();
    sim_run(300);
    /* exFAT de fábrica: se dice, y no se toca nada. */
    sd_set(WSD_ERROR, WSD_E_EXFAT, 128043712512ull, 0, 0);
    sim_run(900);
    page(out, "m_microsd_exfat");
    EXPECT(sim_find_label_prefix(ui_nav_top(), "Formato exFAT") != NULL, "microSD: no explica exFAT");
    click(tap_of("Expulsar"));
    sim_run(200);
    ui_nav_back();
    sim_run(300);
    ui_nav_back();
    sim_run(500);
    s_dest = WM_VOL_WATCH;

    /* Sin fugas: las páginas sueltan su memoria y sus temporizadores. */
    EXPECT(sim_timer_count() == timers0, "multimedia: quedan %d temporizadores", sim_timer_count() - timers0);
    EXPECT(sim_mem_blocks() <= blocks0 + 2, "multimedia: quedan %d bloques de LVGL", (int)(sim_mem_blocks() - blocks0));
    (void)text_prefix;
    (void)tap_prefix;
    sim_stg = saved;
    printf("multimedia: %s\n", sim_fails == f0 ? "bien" : "FALLOS");
}
