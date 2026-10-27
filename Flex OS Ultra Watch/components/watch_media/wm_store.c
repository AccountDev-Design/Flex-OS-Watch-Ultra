/*
 * wm_store — la biblioteca de medios en el reloj. Ver include/wm_store.h.
 *
 * Es el anfitrión de FlexOS_MediaStore (portado del P4) igual que
 * FlexOS_Ultra_MediaLib.h lo es en Flex OS Ultra: le da el disco (POSIX
 * sobre /storage o /sdcard), el cerrojo, la PSRAM, la hora y el trabajo
 * pesado de uno en uno. La lógica del catálogo (cargar, reconciliar,
 * miniaturas, publicar, borrar) es la del P4 y se prueba en el PC
 * (tests/host/flexmedia/test_mediastore.cpp).
 */
#include "wm_store.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "FlexOS_MediaThumb.h"
#include "flexfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fxp.h"
#include "watch_storage.h"
#include "watch_time.h"
#include "wm_path.h"

static const char *TAG = "wm_lib";

#define TASK_STACK       12288   /* la de "flexMedia" del P4: decodifica fotos enteras para la miniatura */
#define TASK_PRIO        2       /* por debajo de LVGL (4) y de la microSD (3) */
#define PSRAM_RESERVE    (768u * 1024u)        /* lo que wm_alloc nunca consume */
#define JOB_RESERVE      (1536u * 1024u)       /* un trabajo de miniatura deja esto libre como mínimo */
#define SAVE_DELAY_MS    1500u                 /* agrupa cambios seguidos en una escritura */
#define RETRY_MS         3000u
#define IDLE_MS          5000u

typedef struct {
    wm_vol_t vol;
    SemaphoreHandle_t mx;   /* el cerrojo del almacén (ms.lock) */
    bool live;              /* bajo mx */
    int users;              /* bajo mx: dentro de una llamada o de acquire */
    bool leased;            /* microSD: arrendamiento de la propia biblioteca */
    uint32_t epoch;         /* sube con cada apertura (otra tarjeta = otra revisión) */
    FlexMediaStore ms;
    FlexMlRec *recs;
    volatile bool scan_req;
    uint32_t last_id;
    bool deferred;
    uint32_t dirty_since;
    uint32_t save_fails;
    uint32_t retry_at;
} vstore_t;

static vstore_t s_st[WM_VOL_N];
static SemaphoreHandle_t s_heavy;
static TaskHandle_t s_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

/* ---------------- memoria y trabajo pesado ---------------- */

void *wm_alloc(size_t n)
{
    if (n == 0) {
        return NULL;
    }
    const size_t free_b = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_b < n || free_b - n < PSRAM_RESERVE) {
        return NULL;
    }
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void wm_free(void *p)
{
    heap_caps_free(p);
}

bool wm_heavy_begin(uint32_t wait_ms)
{
    return s_heavy && xSemaphoreTake(s_heavy, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void wm_heavy_end(void)
{
    if (s_heavy) {
        xSemaphoreGive(s_heavy);
    }
}

static bool heavy_begin_cb(void *ctx)
{
    (void)ctx;
    return wm_heavy_begin(50); /* la tarea de fondo no espera: se aplaza */
}

static void heavy_end_cb(void *ctx)
{
    (void)ctx;
    wm_heavy_end();
}

/* Durante una decodificación larga se cede la CPU cada ~20 ms. */
static void heavy_yield_cb(void *ctx)
{
    (void)ctx;
    static uint32_t last;
    const uint32_t t = now_ms();
    if (t - last >= 20u) {
        vTaskDelay(1);
        last = now_ms();
    }
}

static void scan_yield(void *ctx)
{
    (void)ctx;
    vTaskDelay(1); /* el recorrido nunca acapara la CPU */
}

/* ---------------- disco (POSIX) ---------------- */

/* Asa: descriptor + 1, con el bit 16 si se abrió para escribir. */
#define H_WRITE 0x10000

static bool real(vstore_t *st, const char *rel, char *out)
{
    return wm_vol_path(st->vol, rel, out, WM_PATH_MAX);
}

static void io_err(vstore_t *st)
{
    if (st->vol == WM_VOL_SD && (errno == EIO || errno == ENODEV || errno == ENXIO)) {
        wm_sd_io_error(); /* ¿la han sacado? CMD13 ya */
    }
}

static bool mkdirs_real(char *p)
{
    /* p es una ruta real; se crean los padres a partir de la raíz del volumen. */
    for (char *s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0775);
            *s = '/';
        }
    }
    return mkdir(p, 0775) == 0 || errno == EEXIST;
}

static bool parent_dirs(const char *rp)
{
    char p[WM_PATH_MAX];
    snprintf(p, sizeof(p), "%s", rp);
    char *slash = strrchr(p, '/');
    if (!slash || slash == p) {
        return true;
    }
    *slash = 0;
    struct stat sb;
    if (stat(p, &sb) == 0) {
        return S_ISDIR(sb.st_mode);
    }
    return mkdirs_real(p);
}

static void *fs_open(void *ctx, const char *path, bool write)
{
    vstore_t *st = ctx;
    char rp[WM_PATH_MAX];
    if (!real(st, path, rp) || (write && !parent_dirs(rp))) {
        return NULL;
    }
    const int fd = write ? open(rp, O_WRONLY | O_CREAT | O_TRUNC, 0664) : open(rp, O_RDONLY);
    if (fd < 0) {
        io_err(st);
        return NULL;
    }
    return (void *)(intptr_t)((fd + 1) | (write ? H_WRITE : 0));
}

static int fd_of(void *h)
{
    return (int)(((intptr_t)h & 0xFFFF) - 1);
}

static int fs_read(void *ctx, void *h, uint8_t *buf, size_t n)
{
    const ssize_t r = read(fd_of(h), buf, n);
    if (r < 0) {
        io_err(ctx);
        return -1;
    }
    return (int)r;
}

static bool fs_write(void *ctx, void *h, const uint8_t *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        const ssize_t w = write(fd_of(h), buf + done, n - done);
        if (w <= 0) {
            io_err(ctx);
            return false; /* incluido "disco lleno" (ENOSPC) */
        }
        done += (size_t)w;
    }
    return true;
}

static bool fs_seek(void *ctx, void *h, uint32_t off)
{
    (void)ctx;
    return lseek(fd_of(h), (off_t)off, SEEK_SET) == (off_t)off;
}

static void fs_close(void *ctx, void *h)
{
    (void)ctx;
    const int fd = fd_of(h);
    if ((intptr_t)h & H_WRITE) {
        fsync(fd);
    }
    close(fd);
}

static uint32_t fs_size(void *ctx, const char *path)
{
    char rp[WM_PATH_MAX];
    struct stat sb;
    if (!real(ctx, path, rp) || stat(rp, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        return 0;
    }
    return sb.st_size < 0 ? 0 : (uint32_t)sb.st_size; /* FAT32: < 4 GB por archivo */
}

static bool fs_exists(void *ctx, const char *path)
{
    char rp[WM_PATH_MAX];
    struct stat sb;
    return real(ctx, path, rp) && stat(rp, &sb) == 0;
}

static bool rm_tree(char *rp, size_t cap, int depth)
{
    struct stat sb;
    if (stat(rp, &sb) != 0) {
        return true;
    }
    if (!S_ISDIR(sb.st_mode)) {
        return unlink(rp) == 0;
    }
    if (depth > 6) {
        return false;
    }
    DIR *d = opendir(rp);
    if (!d) {
        return false;
    }
    const size_t base = strlen(rp);
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        if (snprintf(rp + base, cap - base, "/%s", e->d_name) >= (int)(cap - base)) {
            ok = false;
            continue;
        }
        ok = rm_tree(rp, cap, depth + 1) && ok;
        rp[base] = 0;
    }
    closedir(d);
    rp[base] = 0;
    return rmdir(rp) == 0 && ok;
}

static bool fs_remove(void *ctx, const char *path)
{
    char rp[WM_PATH_MAX];
    if (!real(ctx, path, rp) || !strcmp(path, "/")) {
        return false;
    }
    return rm_tree(rp, sizeof(rp), 0);
}

static bool fs_move(void *ctx, const char *from, const char *to)
{
    char a[WM_PATH_MAX], b[WM_PATH_MAX];
    struct stat sb;
    if (!real(ctx, from, a) || !real(ctx, to, b) || stat(b, &sb) == 0 || !parent_dirs(b)) {
        return false; /* NUNCA pisa (LittleFS lo haría sin avisar) */
    }
    if (rename(a, b) != 0) {
        io_err(ctx);
        return false;
    }
    return true;
}

static bool fs_trash(void *ctx, const char *path)
{
    (void)ctx;
    return ffs_trash(path); /* la papelera de Archivos (solo el reloj) */
}

static bool fs_mkdir(void *ctx, const char *path)
{
    char rp[WM_PATH_MAX];
    return real(ctx, path, rp) && (mkdir(rp, 0775) == 0 || errno == EEXIST);
}

static bool hidden(const char *n)
{
    /* "._foto.jpg" de macOS, "System Volume Information" de Windows... */
    return n[0] == '.' || !strcmp(n, "System Volume Information") || !strcmp(n, "$RECYCLE.BIN");
}

static int fs_list(void *ctx, const char *dir, FlexMsEntry *out, int maxn, int skip)
{
    char rp[WM_PATH_MAX];
    if (!real(ctx, dir, rp)) {
        return -1;
    }
    DIR *d = opendir(rp);
    if (!d) {
        return -1;
    }
    const size_t base = strlen(rp);
    int n = 0, seen = 0;
    struct dirent *e;
    while (n < maxn && (e = readdir(d)) != NULL) {
        if (hidden(e->d_name) || strlen(e->d_name) >= sizeof(out[0].name)) {
            continue;
        }
        if (seen++ < skip) {
            continue;
        }
        if (snprintf(rp + base, sizeof(rp) - base, "/%s", e->d_name) >= (int)(sizeof(rp) - base)) {
            continue;
        }
        struct stat sb;
        if (stat(rp, &sb) != 0) {
            rp[base] = 0;
            continue;
        }
        rp[base] = 0;
        snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
        out[n].dir = S_ISDIR(sb.st_mode);
        out[n].size = sb.st_size < 0 ? 0 : (uint32_t)sb.st_size;
        n++;
    }
    closedir(d);
    return n;
}

/* Temporal + fsync + renombrado, con la generación anterior en .bak (la
 * misma secuencia que flexFsWriteBinAtomic del P4, que flexMsLoad conoce). */
static bool fs_write_atomic(void *ctx, const char *path, const void *buf, size_t n)
{
    char rp[WM_PATH_MAX], tmp[WM_PATH_MAX + 4], bak[WM_PATH_MAX + 4];
    if (!real(ctx, path, rp) || !parent_dirs(rp)) {
        return false;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", rp);
    snprintf(bak, sizeof(bak), "%s.bak", rp);
    unlink(tmp);
    const int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        io_err(ctx);
        return false;
    }
    size_t done = 0;
    while (done < n) {
        const ssize_t w = write(fd, (const uint8_t *)buf + done, n - done);
        if (w <= 0) {
            break;
        }
        done += (size_t)w;
    }
    const bool synced = fsync(fd) == 0;
    close(fd);
    struct stat sb;
    if (done != n || !synced || stat(tmp, &sb) != 0 || (size_t)sb.st_size != n) {
        io_err(ctx);
        unlink(tmp);
        return false;
    }
    unlink(bak);
    const bool had = stat(rp, &sb) == 0;
    if (had && rename(rp, bak) != 0) {
        unlink(tmp);
        return false;
    }
    if (rename(tmp, rp) != 0) {
        if (had) {
            rename(bak, rp);
        }
        unlink(tmp);
        return false;
    }
    unlink(bak);
    return true;
}

static void lock_cb(void *ctx)
{
    xSemaphoreTake(((vstore_t *)ctx)->mx, portMAX_DELAY);
}

static void unlock_cb(void *ctx)
{
    xSemaphoreGive(((vstore_t *)ctx)->mx);
}

static uint32_t now_cb(void *ctx)
{
    (void)ctx;
    int64_t t = 0;
    return watch_time_now_utc(&t) && t > 0 ? (uint32_t)t : 0;
}

/* Dibujo de Paint (solo en el reloj) -> lienzo RGB565 sobre blanco. */
typedef struct {
    uint16_t *px;
    int ox, oy;
} paint_ctx_t;

static void paint_seg(int x0, int y0, int x1, int y1, uint16_t color, int radius, void *user)
{
    (void)radius;
    paint_ctx_t *c = user;
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; guard++) {
        const int x = c->ox + x0, y = c->oy + y0;
        if ((unsigned)x < FLEXTH_SIDE && (unsigned)y < FLEXTH_SIDE) {
            c->px[y * FLEXTH_SIDE + x] = color;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static bool paint_thumb(void *ctx, const char *path, uint16_t *px, int *w, int *h)
{
    (void)ctx;
    fxp_info_t fi;
    if (!fxp_info(path, &fi) || !fi.w || !fi.h) {
        return false;
    }
    for (int i = 0; i < FLEXTH_SIDE * FLEXTH_SIDE; i++) {
        px[i] = 0xFFFF;
    }
    const float sc = fi.w >= fi.h ? (float)FLEXTH_SIDE / fi.w : (float)FLEXTH_SIDE / fi.h;
    const int fw = (int)(fi.w * sc), fh = (int)(fi.h * sc);
    paint_ctx_t pc = {.px = px, .ox = (FLEXTH_SIDE - fw) / 2, .oy = (FLEXTH_SIDE - fh) / 2};
    const bool ok = fxp_replay(path, sc, 0, 0, paint_seg, &pc);
    *w = fi.w;
    *h = fi.h;
    return ok;
}

/* ---------------- ciclo de vida de cada catálogo ---------------- */

static bool enter(vstore_t *st)
{
    if (!st->mx) {
        return false;
    }
    xSemaphoreTake(st->mx, portMAX_DELAY);
    const bool ok = st->live;
    if (ok) {
        st->users++;
    }
    xSemaphoreGive(st->mx);
    return ok;
}

static void leave(vstore_t *st)
{
    xSemaphoreTake(st->mx, portMAX_DELAY);
    if (st->users > 0) {
        st->users--;
    }
    xSemaphoreGive(st->mx);
    wake();
}

static bool vol_ready(wm_vol_t v)
{
    if (v == WM_VOL_WATCH) {
        watch_storage_info_t si;
        watch_storage_info(&si);
        return si.state == STG_MOUNTED;
    }
    wm_sd_info_t i;
    wm_sd_info(&i);
    return i.state == WSD_READY;
}

static void store_open(vstore_t *st)
{
    if (st->vol == WM_VOL_SD) {
        if (!wm_sd_lease()) {
            return;
        }
        st->leased = true;
    }
    st->recs = wm_alloc(sizeof(FlexMlRec) * FML_CAP);
    if (!st->recs) {
        ESP_LOGW(TAG, "%s: sin PSRAM para el catálogo", wm_vol_name(st->vol));
        if (st->leased) {
            wm_sd_unlease();
            st->leased = false;
        }
        return;
    }
    memset(&st->ms, 0, sizeof(st->ms));
    st->ms.fs = (FlexMsFs){fs_open, fs_read, fs_write, fs_seek, fs_close, fs_size, fs_exists, fs_remove, fs_move,
                           st->vol == WM_VOL_WATCH ? fs_trash : NULL, fs_mkdir, fs_list, fs_write_atomic, st};
    st->ms.lock = lock_cb;
    st->ms.unlock = unlock_cb;
    st->ms.lockCtx = st;
    st->ms.alloc = wm_alloc;
    st->ms.free = wm_free;
    st->ms.now = now_cb;
    st->ms.paintThumb = st->vol == WM_VOL_WATCH ? paint_thumb : NULL;
    st->ms.heavyBegin = heavy_begin_cb;
    st->ms.heavyEnd = heavy_end_cb;
    st->ms.yield = heavy_yield_cb;
    flexMsInit(&st->ms, st->recs, FML_CAP);
    flexMsMakeDirs(&st->ms);
    const int from = flexMsLoad(&st->ms); /* el bueno, su .bak o su .tmp */
    flexMsCleanTmp(&st->ms);              /* subidas y guardados que no terminaron */
    ESP_LOGI(TAG, "%s: catálogo de %u elementos (%s)", wm_vol_name(st->vol), (unsigned)st->ms.lib.n,
             from == 1 ? "archivo" : from ? "copia de respaldo" : "nuevo");
    st->scan_req = true; /* el disco manda: se reconcilia siempre al montar */
    st->epoch++;
    st->last_id = 0;
    st->deferred = false;
    st->dirty_since = 0;
    st->save_fails = 0;
    xSemaphoreTake(st->mx, portMAX_DELAY);
    st->live = true;
    xSemaphoreGive(st->mx);
}

static void store_close(vstore_t *st, bool try_save)
{
    xSemaphoreTake(st->mx, portMAX_DELAY);
    st->live = false; /* nadie nuevo entra */
    xSemaphoreGive(st->mx);
    /* Los que ya estaban dentro (el servidor web con un acquire) terminan. */
    for (int k = 0;; k++) {
        xSemaphoreTake(st->mx, portMAX_DELAY);
        const int users = st->users;
        xSemaphoreGive(st->mx);
        if (users == 0) {
            break;
        }
        if (k % 100 == 99) {
            ESP_LOGW(TAG, "%s: esperando a %d usuario(s) del catálogo", wm_vol_name(st->vol), users);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
    if (try_save && st->ms.dirty) {
        flexMsSave(&st->ms); /* si la tarjeta ya no está, falla y no pasa nada */
    }
    wm_free(st->recs);
    st->recs = NULL;
    memset(&st->ms, 0, sizeof(st->ms));
    if (st->leased) {
        wm_sd_unlease();
        st->leased = false;
    }
    ESP_LOGI(TAG, "%s: catálogo cerrado", wm_vol_name(st->vol));
}

static bool save_due(vstore_t *st, uint32_t t)
{
    if (!st->dirty_since) {
        return false;
    }
    const int32_t age = (int32_t)(t - st->dirty_since);
    const uint32_t f = st->save_fails < 20 ? st->save_fails : 20; /* si la flash falla, se espacia */
    return age >= 0 && (uint32_t)age >= SAVE_DELAY_MS * (1u + f);
}

/* Un paso de trabajo de un catálogo vivo. true = hizo algo. */
static bool store_work(vstore_t *st)
{
    bool worked = false;
    if (st->scan_req) {
        st->scan_req = false;
        if (!flexMsScan(&st->ms, scan_yield, NULL)) {
            ESP_LOGW(TAG, "%s: una carpeta no se pudo leer: no se retira nada del catálogo", wm_vol_name(st->vol));
        }
        st->last_id = 0;
        st->deferred = false;
        worked = true;
    }
    const uint32_t t = now_ms();
    if (!st->retry_at || (int32_t)(t - st->retry_at) >= 0) {
        st->retry_at = 0;
        FlexMsJob job;
        if (flexMsNextJob(&st->ms, st->last_id, &job)) {
            const uint32_t free_b = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            if (flexMsRunJob(&st->ms, &job, free_b, JOB_RESERVE) == FLEXMS_JOB_RETRY) {
                st->deferred = true;
            }
            st->last_id = job.id;
            worked = true;
        } else if (st->last_id) {
            st->last_id = 0;
            if (st->deferred) {
                st->deferred = false;
                st->retry_at = (t + RETRY_MS) | 1u; /* lo aplazado por memoria, tras una pausa */
            } else {
                worked = true; /* otro pase por si algo volvió a necesitarla */
            }
        }
    }
    if (st->ms.dirty) {
        if (!st->dirty_since) {
            st->dirty_since = now_ms() | 1u;
        }
    } else {
        st->dirty_since = 0;
    }
    if (save_due(st, now_ms())) {
        if (flexMsSave(&st->ms)) {
            st->save_fails = 0;
        } else {
            st->save_fails++;
            ESP_LOGW(TAG, "%s: no se pudo guardar el catálogo (¿memoria llena?)", wm_vol_name(st->vol));
        }
        st->dirty_since = 0;
    }
    return worked;
}

static void sd_changed(uint32_t gen, void *ctx)
{
    (void)gen;
    (void)ctx;
    wake();
}

static void lib_task(void *arg)
{
    (void)arg;
    uint32_t low = 0xFFFFFFFFu;
    for (;;) {
        bool worked = false;
        bool dirty = false;
        for (int v = 0; v < WM_VOL_N; v++) {
            vstore_t *st = &s_st[v];
            const bool ready = vol_ready((wm_vol_t)v);
            if (!st->live && ready) {
                store_open(st);
            } else if (st->live && !ready) {
                /* La microSD se expulsa (aún está: se guarda) o la han sacado
                 * (falla el guardado y no pasa nada). */
                store_close(st, v == WM_VOL_SD);
            }
            if (st->live) {
                worked = store_work(st) || worked;
                dirty = dirty || st->dirty_since;
            }
        }
        const uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        if (hw < low) {
            low = hw;
            if (hw < 2048u) {
                ESP_LOGW(TAG, "pila: quedan %u B como mínimo", (unsigned)hw);
            }
        }
        if (worked) {
            vTaskDelay(pdMS_TO_TICKS(5)); /* con trabajo, una pausa corta: la interfaz manda */
        } else {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(dirty ? 400 : IDLE_MS));
        }
    }
}

esp_err_t wm_lib_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_heavy = xSemaphoreCreateMutex();
    for (int v = 0; v < WM_VOL_N; v++) {
        s_st[v].vol = (wm_vol_t)v;
        s_st[v].mx = xSemaphoreCreateMutex();
        if (!s_st[v].mx) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_heavy) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCoreWithCaps(lib_task, "wm_media", TASK_STACK, NULL, TASK_PRIO, &s_task, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    wm_sd_listen(sd_changed, NULL);
    return ESP_OK;
}

/* ---------------- API ---------------- */

static vstore_t *st_of(wm_vol_t v)
{
    return v < WM_VOL_N ? &s_st[v] : NULL;
}

bool wm_lib_ready(wm_vol_t v)
{
    vstore_t *st = st_of(v);
    if (!st || !st->mx) {
        return false;
    }
    xSemaphoreTake(st->mx, portMAX_DELAY);
    const bool live = st->live;
    xSemaphoreGive(st->mx);
    return live;
}

uint32_t wm_lib_rev(wm_vol_t v)
{
    vstore_t *st = st_of(v);
    if (!st || !st->mx) {
        return 0;
    }
    xSemaphoreTake(st->mx, portMAX_DELAY);
    /* Con la época: cambiar de tarjeta cambia la revisión aunque el
     * contador del catálogo nuevo coincida con el del viejo. */
    const uint32_t r = st->live ? ((st->ms.lib.rev + (st->epoch << 20)) | 1u) : 0;
    xSemaphoreGive(st->mx);
    return r;
}

bool wm_lib_busy(wm_vol_t v, uint16_t *pending)
{
    vstore_t *st = st_of(v);
    if (!st || !st->mx) {
        return false;
    }
    xSemaphoreTake(st->mx, portMAX_DELAY);
    const bool busy = st->live && (st->ms.scanning || st->ms.pending || st->scan_req);
    if (pending) {
        *pending = st->live ? st->ms.pending : 0;
    }
    xSemaphoreGive(st->mx);
    return busy;
}

int wm_lib_snapshot(wm_vol_t v, uint32_t mask, FlexMlRec *dst, int cap, uint32_t *rev)
{
    vstore_t *st = st_of(v);
    if (rev) {
        *rev = 0;
    }
    if (!st || !st->mx || !dst || cap <= 0) {
        return 0;
    }
    int n = 0;
    xSemaphoreTake(st->mx, portMAX_DELAY);
    if (st->live) {
        for (int i = 0; i < st->ms.lib.n && n < cap; i++) {
            const FlexMlRec *r = &st->ms.lib.recs[i];
            if (FML_MASK(r->kind) & mask) {
                dst[n++] = *r;
            }
        }
        if (rev) {
            *rev = (st->ms.lib.rev + (st->epoch << 20)) | 1u;
        }
    }
    xSemaphoreGive(st->mx);
    return n;
}

bool wm_lib_get(wm_vol_t v, uint32_t id, FlexMlRec *out)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        return false;
    }
    const bool ok = flexMsGet(&st->ms, id, out);
    leave(st);
    return ok;
}

uint32_t wm_lib_add_file(wm_vol_t v, const char *tmp_rel, const char *dir, int kind, const char *shown_name,
                         uint8_t origin, char *why, size_t why_cap)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        if (why && why_cap) {
            snprintf(why, why_cap, "%s no está disponible", v == WM_VOL_SD ? "La microSD" : "La memoria del reloj");
        }
        return 0;
    }
    const uint32_t id = flexMsAddFileTo(&st->ms, tmp_rel, dir, kind, shown_name, origin, 0, why, why_cap);
    leave(st);
    wake();
    return id;
}

bool wm_lib_delete(wm_vol_t v, uint32_t id)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        return false;
    }
    const bool ok = flexMsDelete(&st->ms, id);
    leave(st);
    return ok;
}

bool wm_lib_trash(wm_vol_t v, uint32_t id)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        return false;
    }
    const bool ok = flexMsTrash(&st->ms, id);
    leave(st);
    return ok;
}

bool wm_lib_rename(wm_vol_t v, uint32_t id, const char *name, char *why, size_t why_cap)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        return false;
    }
    const bool ok = flexMsRename(&st->ms, id, name, why, why_cap);
    leave(st);
    return ok;
}

void wm_lib_rescan(wm_vol_t v)
{
    vstore_t *st = st_of(v);
    if (st) {
        st->scan_req = true;
        wake();
    }
}

FlexMediaStore *wm_lib_acquire(wm_vol_t v)
{
    vstore_t *st = st_of(v);
    if (!st || !enter(st)) {
        return NULL;
    }
    return &st->ms;
}

void wm_lib_release(wm_vol_t v)
{
    vstore_t *st = st_of(v);
    if (st) {
        leave(st);
    }
}
