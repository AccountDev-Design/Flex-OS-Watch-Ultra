/*
 * files_logic — ver include/files_logic.h
 */
#include "files_logic.h"

#include <stdio.h>
#include <string.h>

const char *fl_base(const char *path)
{
    if (!path) {
        return "";
    }
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

const char *fl_ext(const char *name)
{
    const char *b = fl_base(name);
    const char *d = strrchr(b, '.');
    return (d && d != b) ? d : "";
}

void fl_stem(const char *name, char *out, size_t n)
{
    if (!out || !n) {
        return;
    }
    const char *b = fl_base(name);
    const char *e = fl_ext(b);
    size_t len = e[0] ? (size_t)(e - b) : strlen(b);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, b, len);
    out[len] = 0;
}

void fl_parent(const char *path, char *out, size_t n)
{
    if (!out || !n) {
        return;
    }
    const char *s = path ? strrchr(path, '/') : NULL;
    size_t len = s ? (size_t)(s - path) : 0;
    if (len == 0) {
        snprintf(out, n, "/");
        return;
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, path, len);
    out[len] = 0;
}

bool fl_join(const char *dir, const char *name, char *out, size_t n)
{
    if (!dir || !name || !out || !n) {
        return false;
    }
    const size_t dl = strlen(dir);
    const bool slash = dl > 0 && dir[dl - 1] == '/';
    const int k = snprintf(out, n, "%s%s%s", dir, slash ? "" : "/", name);
    return k > 0 && (size_t)k < n;
}

bool fl_name_ok(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    const size_t len = strlen(name);
    if (len >= FL_NAME_MAX || name[0] == ' ' || name[len - 1] == ' ') {
        return false;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == '@' || (unsigned char)*p < 0x20) {
            return false;
        }
    }
    return true;
}

bool fl_trash_encode(const char *path, char *out, size_t n)
{
    if (!path || path[0] != '/' || !out || !n) {
        return false;
    }
    size_t j = 0;
    for (size_t i = 0; path[i]; i++) {
        if (j + 1 >= n) {
            return false; /* no cabe: mejor no tirarlo que perder la ruta */
        }
        out[j++] = path[i] == '/' ? '@' : path[i];
    }
    out[j] = 0;
    return true;
}

bool fl_trash_origin(const char *trash_name, char *out, size_t n)
{
    if (!trash_name || !out || !n) {
        return false;
    }
    const char *nm = fl_base(trash_name);
    size_t len = strlen(nm);
    /* RELOJ: "@@N" final = número de colisión al tirar. La referencia ponía
     * "(N)" (FS.cpp:483-489), que al restaurar quedaba pegado al nombre
     * ("a.txt(2)") y no se distingue de un nombre real con paréntesis; '@'
     * no puede aparecer en un nombre (fl_name_ok), así que esto es exacto. */
    {
        size_t i = len;
        while (i > 0 && nm[i - 1] >= '0' && nm[i - 1] <= '9') {
            i--;
        }
        if (i < len && i >= 2 && nm[i - 1] == '@' && nm[i - 2] == '@') {
            len = i - 2;
        }
    }
    if (len == 0 || len >= n || nm[0] != '@') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = nm[i] == '@' ? '/' : nm[i];
    }
    out[len] = 0;
    return true;
}

void fl_fmt_size(uint32_t b, char *out, size_t n)
{
    if (b >= 1048576u) {
        const uint32_t ent = b / 1048576u, dec = (b % 1048576u) * 10u / 1048576u;
        snprintf(out, n, "%lu,%lu MB", (unsigned long)ent, (unsigned long)dec);
    } else if (b >= 1024u) {
        snprintf(out, n, "%lu KB", (unsigned long)(b / 1024u));
    } else {
        snprintf(out, n, "%lu B", (unsigned long)b);
    }
}

void fl_utf8_trim(char *s)
{
    if (!s) {
        return;
    }
    size_t n = strlen(s), i = n;
    /* retrocede como mucho 3 bytes de continuación hasta el inicio */
    while (i > 0 && n - i < 4 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i == 0) {
        return;
    }
    const unsigned char c = (unsigned char)s[i - 1];
    const size_t need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (c >= 0x80 && (need == 0 || n - (i - 1) < need)) {
        s[i - 1] = 0; /* inicio sin todos sus bytes (o inválido): fuera */
    }
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int fl_cmp(bool dir_a, const char *a, bool dir_b, const char *b)
{
    if (dir_a != dir_b) {
        return dir_a ? -1 : 1;
    }
    for (;; a++, b++) {
        const int ca = lower((unsigned char)*a), cb = lower((unsigned char)*b);
        if (ca != cb || !ca) {
            return ca - cb;
        }
    }
}
