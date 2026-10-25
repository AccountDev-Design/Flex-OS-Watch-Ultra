/*
 * stg_usage — ver include/stg_usage.h
 */
#include "stg_usage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PATH_MAX_LEN 192

static uint64_t du_rec(char *path, size_t len, int depth, uint32_t *files, bool *truncated)
{
    if (depth > STG_DU_MAX_DEPTH) {
        *truncated = true;
        return 0;
    }
    DIR *d = opendir(path);
    if (!d) {
        return 0;
    }
    uint64_t sum = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (*files >= STG_DU_MAX_FILES) {
            *truncated = true;
            break;
        }
        const size_t n = strlen(e->d_name);
        if (len + 1 + n + 1 > PATH_MAX_LEN) {
            *truncated = true; /* ruta demasiado larga: no se cuenta (se dice) */
            continue;
        }
        path[len] = '/';
        memcpy(path + len + 1, e->d_name, n + 1);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                sum += du_rec(path, len + 1 + n, depth + 1, files, truncated);
            } else {
                sum += (uint64_t)st.st_size;
                (*files)++;
            }
        }
        path[len] = 0;
    }
    closedir(d);
    return sum;
}

uint64_t stg_du(const char *dir, uint32_t *files, bool *truncated)
{
    char path[PATH_MAX_LEN];
    const int n = snprintf(path, sizeof(path), "%s", dir);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        *truncated = true;
        return 0;
    }
    return du_rec(path, (size_t)n, 0, files, truncated);
}

void stg_usage_scan(const char *root, stg_usage_t *out)
{
    static const char *const kDocs[] = {"/Documentos", "/Notas", "/Paint", "/Imagenes"};
    memset(out, 0, sizeof(*out));
    char p[PATH_MAX_LEN];
    for (size_t i = 0; i < sizeof(kDocs) / sizeof(kDocs[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", root, kDocs[i]);
        out->docs += stg_du(p, &out->files, &out->truncated);
    }
    snprintf(p, sizeof(p), "%s/Papelera", root);
    out->trash = stg_du(p, &out->files, &out->truncated);
}
