/*
 * gal_local — lo que la Galería guarda y lee en el LittleFS del reloj, con
 * POSIX sobre la raíz de flexfs (la misma que Archivos, Notas y Paint):
 *  - las fotos de /Imagenes (FML_DIR_PHOTO del P4) y los dibujos de /Paint;
 *  - miniaturas persistentes de las fotos del reloj en /System/Galeria/th
 *    (RGB565 de GAL_THUMB_SIDE, con la clave dentro): decodificar una foto de
 *    12 Mpx cuesta segundos, así que se hace UNA vez, como la miniatura
 *    persistente del P4;
 *  - origen.txt: de qué fuente vino cada descarga (no se descarga dos veces y
 *    la vista combinada no la enseña duplicada);
 *  - la papelera es la de Archivos (ffs_trash): lo que se tira se puede
 *    restaurar desde allí. La Galería no borra nada para siempre.
 */
#include "gal_priv.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "files_logic.h"
#include "flexfs.h"

#define TH_MAGIC 0x31485447u /* "GTH1" */
#define IO_CHUNK (16u * 1024u)

bool gal__real(const char *vpath, char *out, size_t n)
{
    return ffs_real_path(vpath, out, n);
}

bool gal__local_acquire(gal_engine_t *e, const gal_item_t *it, char *real, size_t n)
{
    const gal_local_io_t *io = e->env.local;
    if (io && io->acquire) {
        return io->acquire(e->env.ctx, it, real, n);
    }
    return it->vol == 0 && gal__real(it->path, real, n);
}

void gal__local_release(gal_engine_t *e, const gal_item_t *it)
{
    const gal_local_io_t *io = e->env.local;
    if (io && io->release) {
        io->release(e->env.ctx, it);
    }
}

void gal__mkdirs(const char *vpath)
{
    char p[GAL_PATH_MAX];
    gal_copyz(p, sizeof(p), vpath);
    for (char *s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            if (!ffs_exists(p)) {
                ffs_mkdir(p);
            }
            *s = '/';
        }
    }
    if (!ffs_exists(p)) {
        ffs_mkdir(p);
    }
}

bool gal__unique_photo(const char *name, char *vpath, size_t n)
{
    if (!fl_join(GAL_DIR_PHOTO, name, vpath, n)) {
        return false;
    }
    for (int k = 2; ffs_exists(vpath) && k < 100; k++) {
        char alt[GAL_NAME_MAX];
        gal_name_variant(name, k, alt, sizeof(alt));
        if (!fl_join(GAL_DIR_PHOTO, alt, vpath, n)) {
            return false;
        }
    }
    return !ffs_exists(vpath);
}

/* ======================= origen.txt ======================= */

/* La tabla (GAL_ORIGINS x gal_origin_t, unos 8 KB) va al montón del motor
 * (PSRAM), NUNCA a la pila: en la pila de 12 KB de la tarea "watch_gal",
 * debajo de gal_engine_step y encima de la lectura de LittleFS, la
 * desbordaba al abrir la Galería (causa de los reinicios: -fstack-usage del
 * Xtensa daba 8224 B a esta función y 8256 B a gal__origin_add;
 * docs/GALERIA.md §13). Sin memoria, no se cargan las marcas de origen: las
 * copias se ven igual, solo sin "copia de ...". */
void gal__origin_load(gal_engine_t *e)
{
    char real[GAL_PATH_MAX + 32], line[256];
    gal_origin_t *tab = gal__alloc(e, sizeof(gal_origin_t) * GAL_ORIGINS);
    if (!tab) {
        return;
    }
    int n = 0;
    FILE *f = gal__real(GAL_ORIGIN, real, sizeof(real)) ? fopen(real, "r") : NULL;
    while (f && fgets(line, sizeof(line), f)) {
        gal_origin_t o;
        if (!gal_origin_parse(line, &o)) {
            continue; /* una línea dañada no estropea las demás */
        }
        if (n == GAL_ORIGINS) {
            memmove(tab, tab + 1, sizeof(tab[0]) * (GAL_ORIGINS - 1));
            n--;
        }
        tab[n++] = o;
    }
    if (f) {
        fclose(f);
    }
    gal__lock(e);
    memcpy(e->origins, tab, sizeof(tab[0]) * (size_t)n);
    e->norigins = n;
    gal__unlock(e);
    gal__free(e, tab);
}

bool gal__origin_add(gal_engine_t *e, const gal_origin_t *o)
{
    gal__lock(e);
    if (e->norigins == GAL_ORIGINS) {
        memmove(e->origins, e->origins + 1, sizeof(e->origins[0]) * (GAL_ORIGINS - 1));
        e->norigins--;
    }
    e->origins[e->norigins++] = *o;
    const int n = e->norigins;
    gal_origin_t *tab = gal__alloc(e, sizeof(gal_origin_t) * GAL_ORIGINS); /* fuera de la pila (ver arriba) */
    if (tab) {
        memcpy(tab, e->origins, sizeof(tab[0]) * (size_t)n);
    }
    gal__unlock(e);
    if (!tab) {
        return false; /* queda en memoria; se escribirá con la próxima */
    }
    /* se reescribe entero (como mucho 64 líneas) y de forma atómica */
    gal__mkdirs(GAL_DIR_SYS);
    char real[GAL_PATH_MAX + 32], tmp[GAL_PATH_MAX + 40], line[256];
    if (!gal__real(GAL_ORIGIN, real, sizeof(real))) {
        gal__free(e, tab);
        return false;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", real);
    FILE *f = fopen(tmp, "w");
    bool ok = f != NULL;
    for (int i = 0; ok && i < n; i++) {
        ok = gal_origin_format(&tab[i], line, sizeof(line)) && fputs(line, f) >= 0;
    }
    if (f && fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        remove(real);
        ok = rename(tmp, real) == 0;
    }
    if (!ok) {
        remove(tmp);
    }
    gal__free(e, tab);
    return ok;
}

/* ======================= recorrido ======================= */

static int scan_dir(const char *vdir, gal_item_t *out, int n, int cap)
{
    char real[GAL_PATH_MAX + 32];
    if (!gal__real(vdir, real, sizeof(real))) {
        return n;
    }
    DIR *d = opendir(real);
    if (!d) {
        return n;
    }
    struct dirent *de;
    while (n < cap && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || !fl_name_ok(de->d_name)) {
            continue;
        }
        const gal_fmt_t fmt = gal_fmt_from_name(de->d_name);
        const bool paint = !strcmp(vdir, GAL_DIR_PAINT);
        if ((paint && fmt != GAL_F_FXP) || (!paint && (fmt == GAL_F_UNKNOWN || fmt == GAL_F_FXP))) {
            continue;
        }
        gal_item_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        o->origin_src = GAL_SRC_NONE;
        if (!fl_join(vdir, de->d_name, o->path, sizeof(o->path))) {
            continue;
        }
        char rp[GAL_PATH_MAX + 32];
        struct stat st;
        if (!gal__real(o->path, rp, sizeof(rp)) || stat(rp, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        o->src = GAL_SRC_LOCAL;
        o->fmt = (uint8_t)fmt;
        o->kind = (uint8_t)gal_kind_of(fmt);
        o->size = (uint64_t)st.st_size;
        o->when = (int64_t)st.st_mtime * 1000;
        o->tver = (uint32_t)st.st_mtime;
        gal_copyz(o->name, sizeof(o->name), de->d_name);
        n++;
    }
    closedir(d);
    return n;
}

void gal__local_scan(gal_engine_t *e)
{
    gal__lock(e);
    e->scanning = true;
    gal__unlock(e);
    gal__origin_load(e);
    gal_item_t *tmp = gal__alloc(e, sizeof(gal_item_t) * GAL_LIST_MAX);
    if (!tmp) {
        gal__lock(e);
        e->scanning = false;
        gal__unlock(e);
        return;
    }
    int n = 0;
    const gal_local_io_t *io = e->env.local;
    uint32_t rev = 0;
    if (io && io->list) {
        /* la biblioteca de la plataforma (memoria del reloj y microSD) */
        rev = io->rev ? io->rev(e->env.ctx) : 0;
        n = io->list(e->env.ctx, tmp, GAL_LIST_MAX);
        n = n < 0 ? 0 : n > GAL_LIST_MAX ? GAL_LIST_MAX : n;
    } else if (ffs_ready()) {
        n = scan_dir(GAL_DIR_PHOTO, tmp, n, GAL_LIST_MAX);
        n = scan_dir(GAL_DIR_PAINT, tmp, n, GAL_LIST_MAX);
    }
    /* del más reciente al más antiguo */
    for (int i = 1; i < n; i++) {
        const gal_item_t x = tmp[i];
        int k = i - 1;
        while (k >= 0 && tmp[k].when < x.when) {
            tmp[k + 1] = tmp[k];
            k--;
        }
        tmp[k + 1] = x;
    }
    gal__lock(e);
    gal_origin_apply(tmp, n, e->origins, e->norigins);
    memcpy(e->local, tmp, sizeof(gal_item_t) * (size_t)n);
    e->nl = n;
    e->local_rev = rev;
    gal__mark_onwatch(e);
    e->local_gen++;
    e->remote_gen++; /* las marcas "ya en el reloj" de la remota cambian */
    e->scanning = false;
    e->scan_ms = gal__now(e);
    gal__unlock(e);
    gal__free(e, tmp);
}

/* ======================= miniaturas persistentes ======================= */

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h = (h ^ *p) * 16777619u;
    }
    return h ? h : 1u;
}

typedef struct {
    FILE *f;
    gal_engine_t *e;
    _Atomic uint32_t *done; /* progreso del visor (atómico: lo lee la interfaz) */
    gal_item_t it;          /* visor: para soltar el arrendamiento al terminar */
} file_src_t;

static int fs_read(void *ud, uint8_t *buf, size_t n)
{
    file_src_t *s = ud;
    const size_t r = fread(buf, 1, n, s->f);
    if (s->done) {
        atomic_fetch_add_explicit(s->done, (uint32_t)r, memory_order_relaxed);
    }
    return r ? (int)r : (ferror(s->f) ? -1 : 0);
}

static bool fs_abort(void *ud)
{
    return ((file_src_t *)ud)->e->abort_job;
}

static uint16_t *thumb_dst(void *ud, int sw, int sh, int *dw, int *dh)
{
    (void)sw;
    (void)sh;
    *dw = *dh = GAL_THUMB_SIDE;
    return ud;
}

bool gal__local_thumb(gal_engine_t *e, gal_thumb_t *t, uint16_t *px)
{
    const gal_local_io_t *io = e->env.local;
    if (io && io->thumb && (t->item.flags & GAL_I_THUMB)) {
        return io->thumb(e->env.ctx, &t->item, px); /* la de la biblioteca: ya hecha */
    }
    if (t->item.vol != 0 || t->item.fmt != GAL_F_JPEG) {
        return false; /* la microSD no llena el LittleFS de miniaturas: espera a la de la biblioteca */
    }
    const uint32_t key = fnv(t->key);
    char tv[GAL_PATH_MAX], tr[GAL_PATH_MAX + 32], src[GAL_PATH_MAX + 32];
    snprintf(tv, sizeof(tv), GAL_DIR_THUMB "/%08lx.th", (unsigned long)key);
    if (!gal__real(tv, tr, sizeof(tr))) {
        return false;
    }
    /* ya hecha: cabecera {magic, lado, clave} y los píxeles */
    FILE *f = fopen(tr, "rb");
    if (f) {
        uint32_t hd[3];
        const bool ok = fread(hd, sizeof(hd), 1, f) == 1 && hd[0] == TH_MAGIC && hd[1] == GAL_THUMB_SIDE &&
                        hd[2] == key && fread(px, sizeof(uint16_t), THUMB_PX, f) == THUMB_PX;
        fclose(f);
        if (ok) {
            return true;
        }
        remove(tr); /* de otra versión o dañada */
    }
    if (!gal__local_acquire(e, &t->item, src, sizeof(src))) {
        return false;
    }
    f = fopen(src, "rb");
    if (!f) {
        gal__local_release(e, &t->item);
        return false;
    }
    file_src_t fs = {.f = f, .e = e};
    gal_jpeg_src_t js = {.read = fs_read, .ud = &fs, .cancel = fs_abort, .alloc = e->env.alloc, .free = e->env.free};
    const int jr = gal_jpeg_decode(&js, GAL_FIT_COVER, thumb_dst, px, 0, NULL);
    fclose(f);
    gal__local_release(e, &t->item);
    if (jr != GAL_JPG_OK) {
        return false;
    }
    gal__mkdirs(GAL_DIR_THUMB);
    char tmp[GAL_PATH_MAX + 40];
    snprintf(tmp, sizeof(tmp), "%s.tmp", tr);
    FILE *w = fopen(tmp, "wb");
    const uint32_t hd[3] = {TH_MAGIC, GAL_THUMB_SIDE, key};
    bool ok = w && fwrite(hd, sizeof(hd), 1, w) == 1 && fwrite(px, sizeof(uint16_t), THUMB_PX, w) == THUMB_PX;
    if (w && fclose(w) != 0) {
        ok = false;
    }
    if (!ok || rename(tmp, tr) != 0) {
        remove(tmp); /* sin sitio: la miniatura vale igual, solo no se guarda */
    }
    return true;
}

/* ======================= visor ======================= */

int gal__local_view_stream(gal_engine_t *e, const gal_item_t *it, gal_jpeg_src_t *src, void **ctx)
{
    char real[GAL_PATH_MAX + 32];
    *ctx = NULL;
    if (!gal__local_acquire(e, it, real, sizeof(real))) {
        return GAL_E_NOTFOUND; /* p. ej. la microSD ya no está */
    }
    file_src_t *fs = gal__alloc(e, sizeof(*fs));
    if (!fs) {
        gal__local_release(e, it);
        return GAL_E_NOMEM;
    }
    fs->f = fopen(real, "rb");
    if (!fs->f) {
        gal__free(e, fs);
        gal__local_release(e, it);
        return GAL_E_NOTFOUND;
    }
    fs->it = *it;
    fs->e = e;
    fs->done = &e->view_done;
    gal__lock(e);
    e->view_total = it->size;
    gal__unlock(e);
    src->read = fs_read;
    src->ud = fs;
    src->cancel = fs_abort;
    src->alloc = e->env.alloc;
    src->free = e->env.free;
    *ctx = fs;
    return 0;
}

void gal__local_view_end(gal_engine_t *e, void *ctx)
{
    file_src_t *fs = ctx;
    if (fs) {
        fclose(fs->f);
        gal__local_release(e, &fs->it);
        gal__free(e, fs);
    }
}

/* ======================= papelera ======================= */

void gal__local_trash(gal_engine_t *e, gal_xfer_t *x)
{
    const gal_local_io_t *io = e->env.local;
    char msg[96];
    bool ok;
    if (io && io->trash) {
        msg[0] = 0;
        ok = io->trash(e->env.ctx, &x->item, msg, sizeof(msg));
        if (!msg[0]) {
            gal_copyz(msg, sizeof(msg), ok ? "Borrado" : gal_err_text(GAL_E_FS));
        }
    } else {
        ok = ffs_trash(x->item.path);
        gal_copyz(msg, sizeof(msg), ok ? "En la papelera del reloj (Archivos › Papelera)" : gal_err_text(GAL_E_FS));
    }
    gal__lock(e);
    x->state = ok ? GAL_XS_DONE : GAL_XS_FAIL;
    gal_copyz(x->msg, sizeof(x->msg), msg);
    e->scan_req = true;
    e->xfer_gen++;
    gal__unlock(e);
}
