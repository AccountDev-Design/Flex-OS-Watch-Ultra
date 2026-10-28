/*
 * wm_web — la tarea del Flex Web Server en el reloj. Ver include/wm_web.h.
 *
 * Lo que decide el servidor (rutas, sesiones, validación, límites) es
 * FlexOS_MediaWeb; aquí solo están los sockets, el disco y el catálogo del
 * destino elegido, la identidad del reloj y la vida de la tarea.
 */
#include "wm_web.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "FlexOS_MediaWeb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "watch_net.h"
#include "watch_time.h"
#include "wm_store.h"

static const char *TAG = "wm_web";

#define TASK_PRIO     3      /* como la microSD: por debajo de LVGL (4) */
#define ACCEPT_MS     250u   /* espera de una conexión nueva (se miran los avisos entre medias) */
#define NOWIFI_MS     500u
#define RETRY_MS      2000u  /* no se pudo escuchar: se reintenta */
#define SLICE_MS      200u   /* las esperas de lectura se cortan para ver si hay que abortar */
#define SEND_TIMEO_S  10

typedef struct {
    FlexWebCtx w;
    uint8_t *hdr, *io;
    int lsock;
    char ip[16];
    /* lo de la conexión en curso */
    int csock;
    wm_vol_t dest;
    FlexMediaStore *ms;      /* catálogo del destino (acquire) o NULL si no está listo */
    uint32_t free_b, total_b;/* del destino, al empezar la conexión (FAT32: se recorta a 4 GB) */
    char dest_json[160];
    uint32_t last_req_ms;
    uint32_t yield_ms;
} srv_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/* bajo s_mux */
static wm_web_status_t s_st;
static bool s_running;
static bool s_wanted;
static uint32_t s_keep_ms;
static bool s_new_code, s_drop;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ======================= disco del destino (FlexWebFs) ======================= */

static void *wfs_open(void *ctx, const char *path, bool write)
{
    srv_t *s = ctx;
    return s->ms ? s->ms->fs.open(s->ms->fs.ctx, path, write) : NULL;
}

static int wfs_read(void *ctx, void *h, uint8_t *buf, size_t n)
{
    srv_t *s = ctx;
    return s->ms ? s->ms->fs.read(s->ms->fs.ctx, h, buf, n) : -1;
}

static bool wfs_write(void *ctx, void *h, const uint8_t *buf, size_t n)
{
    srv_t *s = ctx;
    return s->ms && s->ms->fs.write(s->ms->fs.ctx, h, buf, n);
}

static bool wfs_seek(void *ctx, void *h, uint32_t off)
{
    srv_t *s = ctx;
    return s->ms && s->ms->fs.seek(s->ms->fs.ctx, h, off);
}

static void wfs_close(void *ctx, void *h)
{
    srv_t *s = ctx;
    if (s->ms) {
        s->ms->fs.close(s->ms->fs.ctx, h);
    }
}

static uint32_t wfs_size(void *ctx, const char *path)
{
    srv_t *s = ctx;
    return s->ms ? s->ms->fs.size(s->ms->fs.ctx, path) : 0;
}

static bool wfs_remove(void *ctx, const char *path)
{
    srv_t *s = ctx;
    return s->ms && s->ms->fs.remove(s->ms->fs.ctx, path);
}

static uint32_t wfs_free(void *ctx)
{
    return ((srv_t *)ctx)->free_b;
}

static uint32_t wfs_total(void *ctx)
{
    return ((srv_t *)ctx)->total_b;
}

/* ======================= catálogo del destino (FlexWebHost) ======================= */

static int h_snapshot(void *ctx, FlexMlRec *dst, int cap, uint32_t *rev)
{
    srv_t *s = ctx;
    if (!s->ms) {
        if (rev) {
            *rev = 0;
        }
        return 0;
    }
    return flexMsSnapshot(s->ms, dst, cap, rev);
}

static bool h_get(void *ctx, uint32_t id, FlexMlRec *out)
{
    srv_t *s = ctx;
    return s->ms && flexMsGet(s->ms, id, out);
}

static uint32_t h_rev(void *ctx)
{
    srv_t *s = ctx;
    return s->ms ? flexMsRev(s->ms) : 0;
}

static uint32_t h_dup(void *ctx, uint32_t size, uint32_t crc)
{
    srv_t *s = ctx;
    return s->ms ? flexMsFindDup(s->ms, size, crc) : 0;
}

static uint32_t h_commit(void *ctx, const FlexWebUpload *up, char *why, size_t cap)
{
    srv_t *s = ctx;
    if (!s->ms) {
        snprintf(why, cap, "%s no está disponible", wm_vol_name(s->dest));
        return 0;
    }
    struct FlexMsUpload u = {
        .tmpPath = up->tmpPath, .thumbTmp = up->thumbTmp, .name = up->name, .title = up->title,
        .artist = up->artist, .album = up->album, .kind = up->kind, .fmt = up->fmt, .playable = up->playable,
        .size = up->size, .crc = up->crc, .created = up->created, .durMs = up->durMs, .w = up->w, .h = up->h,
    };
    return flexMsCommitUpload(s->ms, &u, why, cap);
}

static bool h_set_thumb(void *ctx, uint32_t id, const char *tmp)
{
    srv_t *s = ctx;
    return s->ms && flexMsSetExtThumb(s->ms, id, tmp);
}

static void h_thumb_path(void *ctx, const FlexMlRec *r, char *out, size_t cap)
{
    (void)ctx;
    flexMsThumbPath(r, out, cap);
}

static bool h_verify(void *ctx, const char *secret)
{
    (void)ctx;
    (void)secret;
    return false; /* el reloj no tiene PIN de sistema: lo protegido no se abre desde la web */
}

static int h_lock_type(void *ctx)
{
    (void)ctx;
    return 0;
}

static uint32_t h_now(void *ctx)
{
    (void)ctx;
    return now_ms();
}

static uint32_t h_epoch(void *ctx)
{
    (void)ctx;
    int64_t t = 0;
    return watch_time_now_utc(&t) && t > 0 ? (uint32_t)t : 0;
}

static void h_random(void *ctx, uint8_t *out, size_t n)
{
    (void)ctx;
    esp_fill_random(out, n); /* con la radio encendida, aleatorio de verdad */
}

static void h_event(void *ctx, const FlexWebXfer *x)
{
    (void)ctx;
    uint8_t xs;
    switch (x->ev) {
    case FLEXWEB_EV_UP_START:
    case FLEXWEB_EV_UP_PROGRESS:
        xs = WWEB_X_RUN;
        break;
    case FLEXWEB_EV_UP_CHECK:
        xs = WWEB_X_CHECK;
        break;
    case FLEXWEB_EV_UP_DONE:
        xs = WWEB_X_DONE;
        break;
    case FLEXWEB_EV_UP_FAIL:
        xs = WWEB_X_FAIL;
        break;
    default:
        return; /* descargas y emparejamientos: solo cuentan los contadores */
    }
    /* Los textos se preparan fuera: dentro de la sección crítica, solo copiar. */
    char name[sizeof(s_st.x_name)], msg[sizeof(s_st.x_msg)];
    snprintf(name, sizeof(name), "%s", x->name);
    snprintf(msg, sizeof(msg), "%s", x->msg);
    portENTER_CRITICAL(&s_mux);
    s_st.x_state = xs;
    s_st.x_done = x->done;
    s_st.x_total = x->total;
    memcpy(s_st.x_name, name, sizeof(name));
    memcpy(s_st.x_msg, msg, sizeof(msg));
    s_st.gen++;
    portEXIT_CRITICAL(&s_mux);
}

static void h_yield(void *ctx)
{
    srv_t *s = ctx;
    const uint32_t t = now_ms();
    if (t - s->yield_ms >= 20u) {
        vTaskDelay(1);
        s->yield_ms = now_ms();
    }
}

static bool readable(int fd, uint32_t ms)
{
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    struct timeval tv = {.tv_sec = (long)(ms / 1000u), .tv_usec = (long)((ms % 1000u) * 1000u)};
    return select(fd + 1, &rf, NULL, NULL, &tv) > 0;
}

static bool h_others(void *ctx)
{
    srv_t *s = ctx;
    return s->lsock >= 0 && readable(s->lsock, 0);
}

static bool h_heavy_begin(void *ctx)
{
    (void)ctx;
    return wm_heavy_begin(2000); /* si no, 503 y el móvil reintenta */
}

static void h_heavy_end(void *ctx)
{
    (void)ctx;
    wm_heavy_end();
}

static bool h_remove(void *ctx, uint32_t id, char *why, size_t cap)
{
    srv_t *s = ctx;
    if (!s->ms) {
        snprintf(why, cap, "%s no está disponible", wm_vol_name(s->dest));
        return false;
    }
    /* El reloj tiene papelera (Archivos › Papelera); la microSD no: allí
     * borrar es borrar, y lo pide la persona desde su móvil. */
    const bool ok = s->dest == WM_VOL_WATCH ? flexMsTrash(s->ms, id) : flexMsDelete(s->ms, id);
    if (!ok) {
        snprintf(why, cap, "No se pudo borrar");
    }
    return ok;
}

static bool h_rename(void *ctx, uint32_t id, const char *name, char *why, size_t cap)
{
    srv_t *s = ctx;
    if (!s->ms) {
        snprintf(why, cap, "%s no está disponible", wm_vol_name(s->dest));
        return false;
    }
    return flexMsRename(s->ms, id, name, why, cap);
}

/* ======================= conexión ======================= */

typedef struct {
    srv_t *s;
    int fd;
} conn_t;

/* La microSD elegida se expulsa o se saca: la subida se corta ya. */
static bool must_abort(const srv_t *s)
{
    if (s->dest != WM_VOL_SD) {
        return false;
    }
    wm_sd_info_t i;
    wm_sd_info(&i);
    return i.state != WSD_READY;
}

static int c_read(void *ctx, uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    conn_t *c = ctx;
    uint32_t left = timeout_ms;
    for (;;) {
        if (must_abort(c->s)) {
            return -1;
        }
        const uint32_t slice = left < SLICE_MS ? left : SLICE_MS;
        if (readable(c->fd, slice)) {
            const int k = recv(c->fd, buf, n, 0);
            return k > 0 ? k : -1;
        }
        if (left <= slice) {
            return 0;
        }
        left -= slice;
    }
}

static bool c_write(void *ctx, const uint8_t *buf, size_t n)
{
    conn_t *c = ctx;
    size_t done = 0;
    while (done < n) {
        const int k = send(c->fd, buf + done, n - done, 0);
        if (k <= 0) {
            return false;
        }
        done += (size_t)k;
    }
    return true;
}

static void serve_one(srv_t *s, int fd, const char *peer)
{
    /* El destino elegido, por conexión: si la persona lo cambia, la siguiente ya lo usa. */
    wm_vol_t dest = WM_VOL_WATCH;
    const bool dest_ok = wm_dest_get(&dest) == WM_DEST_OK;
    s->dest = dest;
    s->ms = dest_ok ? wm_lib_acquire(dest) : NULL;
    wm_vol_info_t vi = {0};
    if (s->ms) {
        wm_vol_info(dest, &vi);
    }
    s->free_b = vi.free_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)vi.free_bytes;
    s->total_b = vi.total_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)vi.total_bytes;
    wweb_dest_json(s->dest_json, sizeof(s->dest_json), dest, s->ms != NULL, vi.free_bytes);
    s->w.destJson = s->dest_json;
    s->w.allowUpload = s->ms != NULL; /* nunca se guarda en el otro volumen */

    const struct timeval to = {.tv_sec = SEND_TIMEO_S, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    conn_t c = {.s = s, .fd = fd};
    const FlexWebConn conn = {.read = c_read, .write = c_write, .ctx = &c, .peer = peer};
    portENTER_CRITICAL(&s_mux);
    s_st.busy = true;
    s_st.gen++;
    portEXIT_CRITICAL(&s_mux);
    flexWebServeConn(&s->w, &conn, s->hdr, s->io);
    if (s->ms) {
        wm_lib_release(dest); /* el catálogo guarda lo nuevo enseguida */
        s->ms = NULL;
    }
    s->last_req_ms = now_ms();
}

/* ======================= la tarea ======================= */

static void close_listener(srv_t *s)
{
    if (s->lsock >= 0) {
        close(s->lsock);
        s->lsock = -1;
    }
}

static bool open_listener(srv_t *s, char *err, size_t n)
{
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) {
        snprintf(err, n, "Sin sockets libres (%d)", errno);
        return false;
    }
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(WWEB_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 2) != 0) {
        snprintf(err, n, "El puerto %d está ocupado (%d)", WWEB_PORT, errno);
        close(fd);
        return false;
    }
    s->lsock = fd;
    return true;
}

/* El estado para la pantalla: todo se calcula FUERA de la sección crítica
 * (wm_dest_get toma mutex), y dentro solo se copia. */
static void publish(srv_t *s, wweb_state_t state, const char *err)
{
    wm_web_status_t n;
    memset(&n, 0, sizeof(n));
    const bool ready = state == WWEB_READY;
    n.state = state;
    n.port = WWEB_PORT;
    snprintf(n.ip, sizeof(n.ip), "%s", s->ip);
    snprintf(n.code, sizeof(n.code), "%s", ready ? s->w.code : "");
    n.code_left_s = ready ? wweb_code_left_s(now_ms(), s->w.codeBornMs, s->w.codeTtlMs) : 0;
    wm_vol_t v = WM_VOL_WATCH;
    n.dest_ok = wm_dest_get(&v) == WM_DEST_OK;
    n.dest = v;
    n.sessions = flexWebSessionCount(&s->w);
    n.uploads = s->w.uploads;
    n.downloads = s->w.downloads;
    n.rejected = s->w.rejectedDev;
    snprintf(n.err, sizeof(n.err), "%s", err ? err : "");
    portENTER_CRITICAL(&s_mux);
    /* lo de la última transferencia lo lleva h_event */
    n.x_state = s_st.x_state;
    n.x_done = s_st.x_done;
    n.x_total = s_st.x_total;
    memcpy(n.x_name, s_st.x_name, sizeof(n.x_name));
    memcpy(n.x_msg, s_st.x_msg, sizeof(n.x_msg));
    n.gen = s_st.gen;
    if (memcmp(&n, &s_st, sizeof(n)) != 0) {
        n.gen++; /* solo sube si algo cambia: la pantalla repinta por esto */
        s_st = n;
    }
    portEXIT_CRITICAL(&s_mux);
}

static srv_t *srv_new(void)
{
    srv_t *s = wm_alloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->hdr = wm_alloc(FLEXWEB_HDR_BUF);
    s->io = wm_alloc(FLEXWEB_IO_BUF);
    if (!s->hdr || !s->io) {
        wm_free(s->hdr);
        wm_free(s->io);
        wm_free(s);
        return NULL;
    }
    s->lsock = -1;
    s->csock = -1;
    s->w.fs = (FlexWebFs){wfs_open, wfs_read, wfs_write, wfs_seek, wfs_close, wfs_size, wfs_remove, wfs_free, wfs_total, s};
    s->w.host = (FlexWebHost){
        .snapshot = h_snapshot, .getRec = h_get, .rev = h_rev, .findDup = h_dup, .commit = h_commit,
        .setThumb = h_set_thumb, .thumbPath = h_thumb_path, .verifySecret = h_verify, .lockType = h_lock_type,
        .nowMs = h_now, .nowEpoch = h_epoch, .random = h_random, .event = h_event, .yield = h_yield,
        .othersWaiting = h_others, .ctx = s, .heavyBegin = h_heavy_begin, .heavyEnd = h_heavy_end,
        .removeRec = h_remove, .renameRec = h_rename,
    };
    s->w.alloc = wm_alloc;
    s->w.free = wm_free;
    s->w.port = WWEB_PORT;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s->w.devName, sizeof(s->w.devName), "%s", WWEB_DEV_NAME);
    wweb_dev_id(mac, s->w.devId, sizeof(s->w.devId));
    snprintf(s->w.devType, sizeof(s->w.devType), "%s", WWEB_DEV_TYPE);
    s->w.mediaJson = wweb_media_json();
    s->w.codeTtlMs = WWEB_CODE_TTL_MS;
    flexWebInit(&s->w); /* código nuevo, sin sesiones */
    s->last_req_ms = now_ms();
    return s;
}

static void srv_free(srv_t *s)
{
    close_listener(s);
    flexWebDropAll(&s->w);
    memset(&s->w.code, 0, sizeof(s->w.code)); /* el código no se queda en la PSRAM */
    memset(s->w.sess, 0, sizeof(s->w.sess));
    wm_free(s->hdr);
    wm_free(s->io);
    wm_free(s);
}

/* Atiende hasta que toca apagar. */
static void run(srv_t *s)
{
    char err[96] = "";
    uint32_t retry_at = 0;
    ESP_LOGI(TAG, "servidor en marcha (puerto %d)", WWEB_PORT);
    for (;;) {
        /* ¿Toca apagar? Nunca con una conexión en curso (aquí no la hay). */
        portENTER_CRITICAL(&s_mux);
        const wweb_life_t life = {.wanted = s_wanted, .since_keep = now_ms() - s_keep_ms,
                                  .idle_ms = now_ms() - s->last_req_ms, .busy = false};
        const bool new_code = s_new_code, drop = s_drop;
        s_new_code = s_drop = false;
        portEXIT_CRITICAL(&s_mux);
        if (wweb_should_stop(&life)) {
            break;
        }
        if (drop) {
            flexWebDropAll(&s->w);
        }
        if (new_code || drop) {
            flexWebNewCode(&s->w);
        }
        /* Wi-Fi y dirección: una IP nueva es otro QR (y fuera las sesiones). */
        watch_net_status_t ns;
        watch_net_get(&ns);
        if (ns.state != NET_CONNECTED || !ns.ip[0]) {
            close_listener(s);
            s->ip[0] = 0;
            publish(s, WWEB_NO_WIFI, NULL);
            vTaskDelay(pdMS_TO_TICKS(NOWIFI_MS));
            continue;
        }
        if (strcmp(ns.ip, s->ip) != 0) {
            close_listener(s);
            snprintf(s->ip, sizeof(s->ip), "%s", ns.ip);
            snprintf(s->w.ip, sizeof(s->w.ip), "%s", ns.ip);
            flexWebDropAll(&s->w);
            flexWebNewCode(&s->w);
        }
        if (s->lsock < 0) {
            if ((int32_t)(now_ms() - retry_at) < 0 || !open_listener(s, err, sizeof(err))) {
                if (!retry_at || (int32_t)(now_ms() - retry_at) >= 0) {
                    ESP_LOGW(TAG, "%s", err);
                    retry_at = now_ms() + RETRY_MS;
                }
                publish(s, WWEB_ERROR, err);
                vTaskDelay(pdMS_TO_TICKS(ACCEPT_MS));
                continue;
            }
            retry_at = 0;
            err[0] = 0;
        }
        publish(s, WWEB_READY, NULL);
        if (!readable(s->lsock, ACCEPT_MS)) {
            continue;
        }
        struct sockaddr_in pa;
        socklen_t pl = sizeof(pa);
        const int fd = accept(s->lsock, (struct sockaddr *)&pa, &pl);
        if (fd < 0) {
            continue;
        }
        char peer[16];
        inet_ntoa_r(pa.sin_addr, peer, sizeof(peer));
        s->csock = fd;
        serve_one(s, fd, peer);
        close(fd);
        s->csock = -1;
    }
    ESP_LOGI(TAG, "servidor apagado (%u subidas, %u descargas, %u rechazadas)", (unsigned)s->w.uploads,
             (unsigned)s->w.downloads, (unsigned)s->w.rejectedDev);
}

static void web_task(void *arg)
{
    (void)arg;
    for (;;) {
        srv_t *s = srv_new();
        if (s) {
            run(s);
            srv_free(s);
        } else {
            static const char nomem[] = "Sin memoria para el servidor";
            ESP_LOGW(TAG, "%s", nomem);
            portENTER_CRITICAL(&s_mux);
            s_st.state = WWEB_ERROR;
            memcpy(s_st.err, nomem, sizeof(nomem));
            s_st.gen++;
            portEXIT_CRITICAL(&s_mux);
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
        }
        /* ¿Lo han vuelto a pedir mientras se apagaba? Entonces sigue. */
        portENTER_CRITICAL(&s_mux);
        const bool again = s_wanted && now_ms() - s_keep_ms < WWEB_KEEP_MS;
        if (!again) {
            s_running = false;
            const uint32_t gen = s_st.gen;
            memset(&s_st, 0, sizeof(s_st));
            s_st.state = WWEB_OFF;
            s_st.gen = gen + 1;
        }
        portEXIT_CRITICAL(&s_mux);
        if (!again) {
            break;
        }
    }
    vTaskDeleteWithCaps(NULL);
}

/* ======================= API ======================= */

void wm_web_keep(void)
{
    portENTER_CRITICAL(&s_mux);
    s_wanted = true;
    s_keep_ms = now_ms();
    const bool start = !s_running;
    if (start) {
        s_running = true;
        s_st.state = WWEB_STARTING;
        s_st.err[0] = 0;
        s_st.gen++;
    }
    portEXIT_CRITICAL(&s_mux);
    if (start && xTaskCreatePinnedToCoreWithCaps(web_task, "wm_web", WWEB_STACK, NULL, TASK_PRIO, NULL,
                                                 tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        static const char nomem[] = "Sin memoria para el servidor";
        portENTER_CRITICAL(&s_mux);
        s_running = false;
        s_st.state = WWEB_ERROR;
        memcpy(s_st.err, nomem, sizeof(nomem));
        s_st.gen++;
        portEXIT_CRITICAL(&s_mux);
    }
}

void wm_web_release(void)
{
    portENTER_CRITICAL(&s_mux);
    s_wanted = false;
    portEXIT_CRITICAL(&s_mux);
}

void wm_web_new_code(void)
{
    portENTER_CRITICAL(&s_mux);
    s_new_code = true;
    portEXIT_CRITICAL(&s_mux);
}

void wm_web_drop_all(void)
{
    portENTER_CRITICAL(&s_mux);
    s_drop = true;
    portEXIT_CRITICAL(&s_mux);
}

void wm_web_status(wm_web_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_st;
    portEXIT_CRITICAL(&s_mux);
}
