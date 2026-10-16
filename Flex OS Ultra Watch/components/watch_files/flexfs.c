/*
 * flexfs — ver include/flexfs.h
 */
#define _DEFAULT_SOURCE /* opendir/stat/mkdir con -std=c11 en el PC */
#include "flexfs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REAL_MAX (FL_PATH_MAX + 160)
#define DEL_DEPTH 6

static char s_root[160] = "/storage"; /* WATCH_STORAGE_BASE */

void ffs_set_root(const char *root)
{
    snprintf(s_root, sizeof(s_root), "%s", root ? root : "/storage");
}

const char *ffs_root(void)
{
    return s_root;
}

/* Ruta real de una virtual. Rechaza lo que no empieza por '/' y los "..". */
static bool real(const char *vpath, char *out, size_t n)
{
    if (!vpath || vpath[0] != '/' || strstr(vpath, "/..")) {
        return false;
    }
    const int k = snprintf(out, n, "%s%s", s_root, strcmp(vpath, "/") ? vpath : "");
    return k > 0 && (size_t)k < n;
}

bool ffs_ready(void)
{
    DIR *d = opendir(s_root);
    if (!d) {
        return false;
    }
    closedir(d);
    return true;
}

bool ffs_exists(const char *path)
{
    char r[REAL_MAX];
    struct stat st;
    return real(path, r, sizeof(r)) && stat(r, &st) == 0;
}

bool ffs_is_dir(const char *path)
{
    char r[REAL_MAX];
    struct stat st;
    return real(path, r, sizeof(r)) && stat(r, &st) == 0 && S_ISDIR(st.st_mode);
}

bool ffs_mkdir(const char *path)
{
    char r[REAL_MAX];
    if (!real(path, r, sizeof(r))) {
        return false;
    }
    return mkdir(r, 0775) == 0 || ffs_is_dir(path);
}

void ffs_init_dirs(void)
{
    if (!ffs_ready()) {
        return;
    }
    ffs_mkdir(FFS_DIR_NOTAS);
    ffs_mkdir(FFS_DIR_DOCS);
    ffs_mkdir(FFS_DIR_PAINT);
    ffs_mkdir(FFS_DIR_TRASH);
}

int ffs_count(const char *dir)
{
    char r[REAL_MAX];
    if (!real(dir, r, sizeof(r))) {
        return -1;
    }
    DIR *d = opendir(r);
    if (!d) {
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
            n++;
        }
    }
    closedir(d);
    return n;
}

/* Suma de los archivos de una carpeta (un nivel: lo que se ve al abrirla) */
static uint32_t dir_size(const char *rdir)
{
    DIR *d = opendir(rdir);
    if (!d) {
        return 0;
    }
    uint32_t sum = 0;
    struct dirent *e;
    char p[REAL_MAX];
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        if (snprintf(p, sizeof(p), "%s/%s", rdir, e->d_name) < (int)sizeof(p) && stat(p, &st) == 0 &&
            S_ISREG(st.st_mode)) {
            sum += (uint32_t)st.st_size;
        }
    }
    closedir(d);
    return sum;
}

int ffs_list(const char *dir, ffs_entry_t *out, int max)
{
    char r[REAL_MAX];
    if (!out || max <= 0 || !real(dir, r, sizeof(r))) {
        return -1;
    }
    DIR *d = opendir(r);
    if (!d) {
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        const size_t nl = strlen(e->d_name);
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || nl >= FL_NAME_MAX ||
            (nl > 4 && !strcmp(e->d_name + nl - 4, ".tmp"))) {
            continue; /* los .tmp son de una escritura atómica a medias */
        }
        char p[REAL_MAX];
        struct stat st;
        if (snprintf(p, sizeof(p), "%s/%s", r, e->d_name) >= (int)sizeof(p) || stat(p, &st) != 0) {
            continue;
        }
        ffs_entry_t it = {0};
        memcpy(it.name, e->d_name, nl + 1); /* nl < FL_NAME_MAX, comprobado arriba */
        it.dir = S_ISDIR(st.st_mode);
        if (it.dir) {
            char vp[FL_PATH_MAX];
            if (fl_join(dir, it.name, vp, sizeof(vp))) {
                const int c = ffs_count(vp);
                it.items = (uint16_t)(c > 0 ? c : 0);
            }
            it.size = dir_size(p);
        } else {
            it.size = (uint32_t)st.st_size;
        }
        /* inserción ordenada: carpetas primero, luego por nombre */
        int j = n++;
        while (j > 0 && fl_cmp(it.dir, it.name, out[j - 1].dir, out[j - 1].name) < 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = it;
    }
    closedir(d);
    return n;
}

static bool del_real(const char *r, int depth)
{
    struct stat st;
    if (stat(r, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(r) == 0;
    }
    if (depth <= 0) {
        return false;
    }
    DIR *d = opendir(r);
    if (!d) {
        return false;
    }
    bool ok = true;
    struct dirent *e;
    char p[REAL_MAX];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        if (snprintf(p, sizeof(p), "%s/%s", r, e->d_name) >= (int)sizeof(p) || !del_real(p, depth - 1)) {
            ok = false;
        }
    }
    closedir(d);
    return ok && rmdir(r) == 0;
}

bool ffs_delete(const char *path)
{
    char r[REAL_MAX];
    if (!real(path, r, sizeof(r)) || !strcmp(path, "/")) {
        return false;
    }
    return del_real(r, DEL_DEPTH);
}

static bool move_real(const char *from_v, const char *to_v)
{
    char a[REAL_MAX], b[REAL_MAX];
    return real(from_v, a, sizeof(a)) && real(to_v, b, sizeof(b)) && rename(a, b) == 0;
}

bool ffs_rename(const char *path, const char *new_name, char *new_path, size_t n)
{
    if (!fl_name_ok(new_name) || !ffs_exists(path) || !strcmp(path, "/")) {
        return false;
    }
    char dir[FL_PATH_MAX], name[FL_NAME_MAX], full[FL_PATH_MAX];
    fl_parent(path, dir, sizeof(dir));
    /* Sin extensión nueva se conserva la original (FS.cpp:438-445) */
    const char *ext = ffs_is_dir(path) ? "" : fl_ext(path);
    if (!fl_ext(new_name)[0] && ext[0]) {
        if (snprintf(name, sizeof(name), "%s%s", new_name, ext) >= (int)sizeof(name)) {
            return false;
        }
    } else {
        snprintf(name, sizeof(name), "%s", new_name);
    }
    if (!fl_join(dir, name, full, sizeof(full))) {
        return false;
    }
    if (strcmp(full, path) != 0) {
        if (ffs_exists(full) || !move_real(path, full)) {
            return false; /* no se pisa nada en silencio */
        }
    }
    if (new_path && n) {
        snprintf(new_path, n, "%s", full);
    }
    return true;
}

bool ffs_trash(const char *path)
{
    const size_t tl = strlen(FFS_DIR_TRASH);
    if (!ffs_exists(path) || !strncmp(path, FFS_DIR_TRASH, tl)) {
        return false; /* la papelera no se tira a sí misma */
    }
    ffs_mkdir(FFS_DIR_TRASH);
    char enc[FL_NAME_MAX + 16], dst[FL_PATH_MAX];
    if (!fl_trash_encode(path, enc, sizeof(enc)) || !fl_join(FFS_DIR_TRASH, enc, dst, sizeof(dst))) {
        return false;
    }
    /* Colisión: se numera en vez de pisar lo que ya se tiró (FS.cpp:483-489),
     * con "@@N" (RELOJ: ver fl_trash_origin) */
    for (int k = 2; ffs_exists(dst) && k < 100; k++) {
        char alt[FL_NAME_MAX + 32];
        snprintf(alt, sizeof(alt), "%s@@%d", enc, k);
        if (!fl_join(FFS_DIR_TRASH, alt, dst, sizeof(dst))) {
            return false;
        }
    }
    return !ffs_exists(dst) && move_real(path, dst);
}

bool ffs_restore(const char *trash_name)
{
    char src[FL_PATH_MAX], dst[FL_PATH_MAX], dir[FL_PATH_MAX];
    if (!fl_join(FFS_DIR_TRASH, fl_base(trash_name), src, sizeof(src)) || !ffs_exists(src) ||
        !fl_trash_origin(trash_name, dst, sizeof(dst))) {
        return false;
    }
    /* La carpeta de destino pudo borrarse mientras tanto: se recrea */
    fl_parent(dst, dir, sizeof(dir));
    if (strcmp(dir, "/")) {
        ffs_mkdir(dir);
    }
    if (ffs_exists(dst)) { /* ya hay otro con ese nombre: "nombre (2).txt" */
        char stem[FL_NAME_MAX], ext[16], alt[FL_NAME_MAX + 32];
        fl_stem(dst, stem, sizeof(stem));
        snprintf(ext, sizeof(ext), "%s", fl_ext(dst));
        for (int k = 2; k < 100; k++) {
            snprintf(alt, sizeof(alt), "%s (%d)%s", stem, k, ext);
            if (!fl_join(dir, alt, dst, sizeof(dst))) {
                return false;
            }
            if (!ffs_exists(dst)) {
                break;
            }
        }
    }
    return !ffs_exists(dst) && move_real(src, dst);
}

bool ffs_empty_trash(void)
{
    ffs_entry_t e[FFS_LIST_MAX];
    int n;
    /* por tandas: una carpeta puede tener más de FFS_LIST_MAX */
    while ((n = ffs_list(FFS_DIR_TRASH, e, FFS_LIST_MAX)) > 0) {
        for (int i = 0; i < n; i++) {
            char p[FL_PATH_MAX];
            if (!fl_join(FFS_DIR_TRASH, e[i].name, p, sizeof(p)) || !ffs_delete(p)) {
                return false;
            }
        }
    }
    return true; /* vacía o sin papelera: nada que borrar */
}

bool ffs_new_name(const char *dir, const char *base, const char *ext, char *out, size_t n)
{
    for (int k = 1; k < 1000; k++) {
        char nm[FL_PATH_MAX + 32];
        snprintf(nm, sizeof(nm), "%s %d%s", base, k, ext ? ext : "");
        if (strlen(nm) >= FL_NAME_MAX || !fl_join(dir, nm, out, n)) {
            return false;
        }
        if (!ffs_exists(out)) {
            return true;
        }
    }
    return false;
}

int ffs_read_text(const char *path, char *out, size_t n)
{
    char r[REAL_MAX];
    if (!out || !n) {
        return -1;
    }
    out[0] = 0;
    if (!real(path, r, sizeof(r))) {
        return -1;
    }
    FILE *f = fopen(r, "rb");
    if (!f) {
        return -1;
    }
    const size_t k = fread(out, 1, n - 1, f);
    fclose(f);
    out[k] = 0;
    return (int)k;
}

bool ffs_write_text(const char *path, const char *txt)
{
    char r[REAL_MAX], tmp[REAL_MAX + 8];
    if (!real(path, r, sizeof(r)) || snprintf(tmp, sizeof(tmp), "%s.tmp", r) >= (int)sizeof(tmp)) {
        return false;
    }
    const size_t len = txt ? strlen(txt) : 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    const bool ok = (len == 0 || fwrite(txt, 1, len, f) == len);
    if (fclose(f) != 0 || !ok) {
        unlink(tmp);
        return false;
    }
    if (rename(tmp, r) != 0) {
        /* sistemas que no pisan al renombrar: se borra y se reintenta */
        unlink(r);
        if (rename(tmp, r) != 0) {
            unlink(tmp);
            return false;
        }
    }
    return true;
}
