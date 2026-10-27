/*
 * wm_path — ver include/wm_path.h
 */
#include "wm_path.h"

#include <stdio.h>
#include <string.h>

static bool utf8_ok(const unsigned char *s)
{
    while (*s) {
        if (*s < 0x80) {
            s++;
            continue;
        }
        int n = (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || *s == 0xC0 || *s == 0xC1 || *s > 0xF4) {
            return false;
        }
        for (int k = 1; k <= n; k++) {
            if ((s[k] & 0xC0) != 0x80) {
                return false;
            }
        }
        s += n + 1;
    }
    return true;
}

bool wm_path_safe(const char *rel)
{
    if (!rel || rel[0] != '/') {
        return false;
    }
    if (!strcmp(rel, "/")) {
        return true;
    }
    const size_t len = strlen(rel);
    if (rel[len - 1] == '/' || !utf8_ok((const unsigned char *)rel)) {
        return false;
    }
    const char *c = rel + 1; /* inicio del componente */
    for (const char *p = rel + 1;; p++) {
        if (*p == '/' || *p == 0) {
            const size_t n = (size_t)(p - c);
            if (n == 0 || (n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.')) {
                return false;
            }
            if (*p == 0) {
                return true;
            }
            c = p + 1;
            continue;
        }
        const unsigned char u = (unsigned char)*p;
        if (u < 0x20 || u == 0x7F || strchr("\\:*?\"<>|", u)) {
            return false;
        }
    }
}

bool wm_path_join(const char *root, const char *rel, char *out, size_t n)
{
    if (!out || n == 0) {
        return false;
    }
    out[0] = 0;
    if (!root || !wm_path_safe(rel)) {
        return false;
    }
    const int w = snprintf(out, n, "%s%s", root, strcmp(rel, "/") ? rel : "");
    if (w < 0 || (size_t)w >= n) {
        out[0] = 0;
        return false;
    }
    return true;
}
