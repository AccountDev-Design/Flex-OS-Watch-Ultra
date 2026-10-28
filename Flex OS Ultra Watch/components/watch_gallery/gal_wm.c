/*
 * gal_wm — lo local de la Galería en el reloj: la biblioteca de medios de
 * watch_media (memoria del reloj y microSD). Ver gal_wm.h.
 *
 * Todo corre en la tarea de la Galería ("watch_gal"), nunca en LVGL:
 *  - la lista es una instantánea de wm_lib (sin E/S de disco);
 *  - cada lectura de la microSD va con su arrendamiento (wm_sd_lease): la
 *    tarjeta no se desmonta a mitad y, si ya no está, la lectura no empieza;
 *  - las miniaturas son las persistentes de la biblioteca (JPEG de 132 px en
 *    /System/Media/th de cada volumen): se decodifican a GAL_THUMB_SIDE, sin
 *    escribir nada;
 *  - lo descargado se publica SOLO en el destino elegido (wm_dest_get), y en
 *    la microSD por copia verificada antes de borrar el temporal.
 */
#include "gal_wm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_random.h"
#include "FlexOS_MediaStore.h"
#include "gal_jpeg.h"
#include "watch_media.h"
#include "watch_storage.h"
#include "wm_store.h"

static const char *TAG = "gal_wm";

#define COPY_CHUNK (16u * 1024u)

/* ---------------- lista ---------------- */

static int wm_list(void *ctx, gal_item_t *out, int cap)
{
    (void)ctx;
    FlexMlRec *recs = wm_alloc(sizeof(FlexMlRec) * FML_CAP); /* solo durante la llamada */
    if (!recs) {
        ESP_LOGW(TAG, "sin PSRAM para la lista local");
        return 0;
    }
    int n = 0;
    for (int v = 0; v < WM_VOL_N && n < cap; v++) {
        const int k = wm_lib_snapshot((wm_vol_t)v, FML_MASK_VISUAL, recs, FML_CAP, NULL);
        for (int i = 0; i < k && n < cap; i++) {
            if (gal_wm_item(&recs[i], v, &out[n])) {
                n++;
            }
        }
    }
    wm_free(recs);
    gal_wm_sort(out, n);
    return n;
}

static uint32_t wm_rev(void *ctx)
{
    (void)ctx;
    return wm_lib_rev(WM_VOL_WATCH) ^ (wm_lib_rev(WM_VOL_SD) * 2654435761u);
}

/* ---------------- lectura con arrendamiento ---------------- */

static bool wm_acquire(void *ctx, const gal_item_t *it, char *real, size_t n)
{
    (void)ctx;
    const wm_vol_t v = it->vol ? WM_VOL_SD : WM_VOL_WATCH;
    if (v == WM_VOL_SD && !wm_sd_lease()) {
        return false; /* no está montada: no se empieza */
    }
    if (!wm_vol_path(v, it->path, real, n)) {
        if (v == WM_VOL_SD) {
            wm_sd_unlease();
        }
        return false;
    }
    return true;
}

static void wm_release(void *ctx, const gal_item_t *it)
{
    (void)ctx;
    if (it->vol) {
        wm_sd_unlease();
    }
}

/* ---------------- miniatura de la biblioteca ---------------- */

typedef struct {
    FILE *f;
} th_src_t;

static int th_read(void *ud, uint8_t *buf, size_t n)
{
    th_src_t *s = ud;
    const size_t r = fread(buf, 1, n, s->f);
    return r ? (int)r : (ferror(s->f) ? -1 : 0);
}

static uint16_t *th_dst(void *ud, int sw, int sh, int *dw, int *dh)
{
    (void)sw;
    (void)sh;
    *dw = *dh = GAL_THUMB_SIDE;
    return ud;
}

static bool wm_thumb(void *ctx, const gal_item_t *it, uint16_t *px)
{
    (void)ctx;
    const wm_vol_t v = it->vol ? WM_VOL_SD : WM_VOL_WATCH;
    FlexMlRec r;
    if (!it->mid || !wm_lib_get(v, it->mid, &r) || !(r.flags & FML_R_THUMB)) {
        return false;
    }
    char rel[FML_PATH_MAX], real[WM_PATH_MAX];
    flexMsThumbPath(&r, rel, sizeof(rel));
    gal_item_t t = *it;
    gal_copyz(t.path, sizeof(t.path), rel);
    if (!wm_acquire(NULL, &t, real, sizeof(real))) {
        return false;
    }
    FILE *f = fopen(real, "rb");
    bool ok = false;
    if (f) {
        th_src_t s = {.f = f};
        const gal_jpeg_src_t js = {.read = th_read, .ud = &s, .alloc = wm_alloc, .free = wm_free};
        ok = gal_jpeg_decode(&js, GAL_FIT_COVER, th_dst, px, 0, NULL) == GAL_JPG_OK;
        fclose(f);
    }
    wm_release(NULL, &t);
    return ok;
}

/* ---------------- papelera y borrado ---------------- */

static bool wm_trash(void *ctx, const gal_item_t *it, char *msg, size_t n)
{
    (void)ctx;
    if (!it->mid) {
        snprintf(msg, n, "No está en la biblioteca");
        return false;
    }
    if (it->vol) {
        /* la microSD no tiene papelera: borrar es borrar (lo confirma la persona) */
        const bool ok = wm_lib_delete(WM_VOL_SD, it->mid);
        snprintf(msg, n, "%s", ok ? "Borrada de la microSD" : "No se pudo borrar de la microSD");
        return ok;
    }
    const bool ok = wm_lib_trash(WM_VOL_WATCH, it->mid);
    snprintf(msg, n, "%s", ok ? "En la papelera del reloj (Archivos › Papelera)" : "No se pudo mover a la papelera");
    return ok;
}

/* ---------------- destino elegido ---------------- */

static bool wm_dest_ready(void *ctx, char *why, size_t n)
{
    (void)ctx;
    wm_vol_t v = WM_VOL_WATCH;
    const wm_dest_status_t st = wm_dest_get(&v);
    if (st != WM_DEST_OK) {
        snprintf(why, n, "%s", wm_dest_text(st));
        return false;
    }
    return true;
}

/* Copia src (LittleFS) a dst (microSD) y comprueba que llegó entera. */
static bool copy_file(const char *src, const char *dst)
{
    uint8_t *buf = wm_alloc(COPY_CHUNK);
    if (!buf) {
        return false;
    }
    const int in = open(src, O_RDONLY);
    const int out = in >= 0 ? open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0664) : -1;
    bool ok = in >= 0 && out >= 0;
    size_t total = 0;
    while (ok) {
        const ssize_t r = read(in, buf, COPY_CHUNK);
        if (r < 0) {
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t done = 0; done < r;) {
            const ssize_t w = write(out, buf + done, (size_t)(r - done));
            if (w <= 0) {
                ok = false; /* incluido "sin sitio" */
                break;
            }
            done += w;
        }
        total += (size_t)r;
    }
    if (out >= 0) {
        ok = fsync(out) == 0 && ok;
        close(out);
    }
    if (in >= 0) {
        close(in);
    }
    wm_free(buf);
    struct stat a, b;
    ok = ok && stat(src, &a) == 0 && stat(dst, &b) == 0 && a.st_size == b.st_size && (size_t)b.st_size == total;
    if (!ok) {
        unlink(dst);
        if (errno == EIO || errno == ENODEV || errno == ENXIO) {
            wm_sd_io_error();
        }
    }
    return ok;
}

/* Origen en la biblioteca: lo que llega por la red local (el Flex Web Server
 * del P4) cuenta como "web"; lo de Flex Cloud o del teléfono, como "nube". */
static uint8_t origin_of(int src)
{
    return src == GAL_SRC_ULTRA ? FML_O_WEB : FML_O_CLOUD;
}

static bool wm_import(void *ctx, const char *tmp_real, const char *name, int kind, int src, char *final, size_t n,
                      char *why, size_t wn)
{
    (void)ctx;
    wm_vol_t v = WM_VOL_WATCH;
    const wm_dest_status_t st = wm_dest_get(&v);
    if (st != WM_DEST_OK) {
        snprintf(why, wn, "%s", wm_dest_text(st)); /* nunca al otro volumen */
        return false;
    }
    const int fk = kind == GAL_K_VIDEO ? FML_K_VIDEO : FML_K_PHOTO;
    const char *dir = fk == FML_K_VIDEO ? FML_DIR_VIDEO : FML_DIR_PHOTO;
    char wwhy[96] = "";
    uint32_t id = 0;
    if (v == WM_VOL_WATCH) {
        /* el temporal ya está en el LittleFS: se mueve (sin copiar) */
        const size_t base = strlen(WATCH_STORAGE_BASE);
        if (strncmp(tmp_real, WATCH_STORAGE_BASE, base) != 0 || tmp_real[base] != '/') {
            snprintf(why, wn, "No se pudo guardar en el reloj");
            return false;
        }
        id = wm_lib_add_file(WM_VOL_WATCH, tmp_real + base, dir, fk, name, origin_of(src), wwhy, sizeof(wwhy));
    } else {
        if (!wm_sd_lease()) {
            snprintf(why, wn, "%s", wm_dest_text(WM_DEST_SD_NOT_READY));
            return false;
        }
        char rel[FML_PATH_MAX], real[WM_PATH_MAX];
        snprintf(rel, sizeof(rel), FML_DIR_TMP "/gal-%08lx.part", (unsigned long)esp_random());
        char dirp[WM_PATH_MAX];
        if (wm_vol_path(WM_VOL_SD, FML_DIR_TMP, dirp, sizeof(dirp))) {
            mkdir(dirp, 0775);
        }
        if (!wm_vol_path(WM_VOL_SD, rel, real, sizeof(real)) || !copy_file(tmp_real, real)) {
            wm_sd_unlease();
            snprintf(why, wn, "No se pudo copiar a la microSD (¿sin sitio?)");
            return false;
        }
        id = wm_lib_add_file(WM_VOL_SD, rel, dir, fk, name, origin_of(src), wwhy, sizeof(wwhy));
        if (!id) {
            unlink(real);
        } else {
            unlink(tmp_real); /* ya está entera en la microSD */
        }
        wm_sd_unlease();
    }
    if (!id) {
        snprintf(why, wn, "%s", wwhy[0] ? wwhy : "No se pudo guardar");
        return false;
    }
    FlexMlRec r;
    if (wm_lib_get(v, id, &r)) {
        gal_copyz(final, n, r.path);
    } else {
        snprintf(final, n, "%s/%s", dir, name);
    }
    snprintf(why, wn, "Guardada en %s", v == WM_VOL_SD ? "la microSD" : "el reloj");
    return true;
}

static const gal_local_io_t kIo = {
    .list = wm_list,
    .rev = wm_rev,
    .acquire = wm_acquire,
    .release = wm_release,
    .thumb = wm_thumb,
    .trash = wm_trash,
    .dest_ready = wm_dest_ready,
    .import = wm_import,
};

const gal_local_io_t *gal_wm_io(void)
{
    return &kIo;
}
