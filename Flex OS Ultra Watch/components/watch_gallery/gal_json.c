/*
 * gal_json — ver include/gal_json.h.
 */
#include "gal_json.h"

#include <stdio.h>
#include <string.h>

#define MAX_DEPTH 32 /* las respuestas de las API no pasan de 5 niveles */

const char *gal_j_ws(const char *p, const char *e)
{
    while (p && p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
    }
    return p;
}

static const char *skip_str(const char *p, const char *e)
{
    if (p >= e || *p != '"') {
        return NULL;
    }
    for (p++; p < e; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            if (++p >= e) {
                return NULL;
            }
        } else if (c == '"') {
            return p + 1;
        } else if (c < 0x20) {
            return NULL; /* un control sin escapar: no es JSON */
        }
    }
    return NULL;
}

static const char *skip_num(const char *p, const char *e)
{
    const char *s = p;
    if (p < e && *p == '-') {
        p++;
    }
    const char *d = p;
    while (p < e && *p >= '0' && *p <= '9') {
        p++;
    }
    if (p == d) {
        return NULL;
    }
    if (p < e && *p == '.') {
        const char *f = ++p;
        while (p < e && *p >= '0' && *p <= '9') {
            p++;
        }
        if (p == f) {
            return NULL;
        }
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < e && (*p == '+' || *p == '-')) {
            p++;
        }
        const char *x = p;
        while (p < e && *p >= '0' && *p <= '9') {
            p++;
        }
        if (p == x) {
            return NULL;
        }
    }
    return p > s ? p : NULL;
}

static const char *skip_lit(const char *p, const char *e, const char *lit)
{
    const size_t n = strlen(lit);
    return (size_t)(e - p) >= n && memcmp(p, lit, n) == 0 ? p + n : NULL;
}

static const char *skip_val(const char *p, const char *e, int depth)
{
    p = gal_j_ws(p, e);
    if (!p || p >= e || depth > MAX_DEPTH) {
        return NULL;
    }
    switch (*p) {
    case '{':
        p = gal_j_ws(p + 1, e);
        if (p < e && *p == '}') {
            return p + 1;
        }
        for (;;) {
            p = skip_str(gal_j_ws(p, e), e);
            p = gal_j_ws(p, e);
            if (!p || p >= e || *p != ':') {
                return NULL;
            }
            p = gal_j_ws(skip_val(p + 1, e, depth + 1), e);
            if (!p || p >= e) {
                return NULL;
            }
            if (*p == '}') {
                return p + 1;
            }
            if (*p != ',') {
                return NULL;
            }
            p++;
        }
    case '[':
        p = gal_j_ws(p + 1, e);
        if (p < e && *p == ']') {
            return p + 1;
        }
        for (;;) {
            p = gal_j_ws(skip_val(p, e, depth + 1), e);
            if (!p || p >= e) {
                return NULL;
            }
            if (*p == ']') {
                return p + 1;
            }
            if (*p != ',') {
                return NULL;
            }
            p++;
        }
    case '"': return skip_str(p, e);
    case 't': return skip_lit(p, e, "true");
    case 'f': return skip_lit(p, e, "false");
    case 'n': return skip_lit(p, e, "null");
    default: return skip_num(p, e);
    }
}

const char *gal_j_skip(const char *p, const char *e)
{
    return p && e && p < e ? skip_val(p, e, 0) : NULL;
}

bool gal_j_valid(const char *p, const char *e)
{
    const char *q = gal_j_skip(p, e);
    return q && gal_j_ws(q, e) == e;
}

const char *gal_j_get(const char *obj, const char *e, const char *key, const char **ve)
{
    const char *p = gal_j_ws(obj, e);
    const size_t kl = key ? strlen(key) : 0;
    if (!p || p >= e || *p != '{' || !kl) {
        return NULL;
    }
    p = gal_j_ws(p + 1, e);
    if (p < e && *p == '}') {
        return NULL;
    }
    while (p && p < e) {
        const char *ks = p;
        const char *kend = skip_str(p, e);
        if (!kend) {
            return NULL;
        }
        p = gal_j_ws(kend, e);
        if (p >= e || *p != ':') {
            return NULL;
        }
        const char *v = gal_j_ws(p + 1, e);
        const char *vend = skip_val(v, e, 1);
        if (!vend) {
            return NULL;
        }
        /* la clave cruda entre comillas: las de las API son ASCII sin escapes */
        if ((size_t)(kend - ks) == kl + 2 && memcmp(ks + 1, key, kl) == 0) {
            if (ve) {
                *ve = vend;
            }
            return v;
        }
        p = gal_j_ws(vend, e);
        if (p >= e || *p != ',') {
            return NULL; /* '}' (no estaba) o mal formado */
        }
        p = gal_j_ws(p + 1, e);
    }
    return NULL;
}

bool gal_j_obj(const char *obj, const char *e, const char *key, const char **os, const char **oe)
{
    const char *ve = NULL;
    const char *v = gal_j_get(obj, e, key, &ve);
    if (!v || (*v != '{' && *v != '[')) {
        return false;
    }
    *os = v;
    *oe = ve;
    return true;
}

const char *gal_j_first(const char *arr, const char *e)
{
    const char *p = gal_j_ws(arr, e);
    if (!p || p >= e || *p != '[') {
        return NULL;
    }
    p = gal_j_ws(p + 1, e);
    if (p >= e || *p == ']') {
        return NULL;
    }
    return skip_val(p, e, 1) ? p : NULL;
}

const char *gal_j_next(const char *it, const char *e)
{
    const char *p = gal_j_ws(skip_val(it, e, 1), e);
    if (!p || p >= e || *p != ',') {
        return NULL;
    }
    p = gal_j_ws(p + 1, e);
    return skip_val(p, e, 1) ? p : NULL;
}

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool hex4(const char *p, const char *e, uint32_t *out)
{
    if (e - p < 4) {
        return false;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        const int h = hexv(p[i]);
        if (h < 0) {
            return false;
        }
        v = v << 4 | (uint32_t)h;
    }
    *out = v;
    return true;
}

/* Longitud de la secuencia UTF-8 bien formada que empieza en p (0 si no lo es). */
static size_t utf8_len(const unsigned char *p, const unsigned char *e)
{
    const unsigned char c = p[0];
    size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || (size_t)(e - p) < n || (n == 2 && c < 0xC2) || (n == 4 && c > 0xF4)) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    if ((n == 3 && c == 0xE0 && p[1] < 0xA0) || (n == 3 && c == 0xED && p[1] >= 0xA0) ||
        (n == 4 && c == 0xF0 && p[1] < 0x90) || (n == 4 && c == 0xF4 && p[1] >= 0x90)) {
        return 0; /* sobrelargas o sustitutos codificados */
    }
    return n;
}

bool gal_j_str(const char *p, const char *e, char *out, size_t n)
{
    if (!out || !n) {
        return false;
    }
    out[0] = 0;
    p = gal_j_ws(p, e);
    if (!p || p >= e || *p != '"') {
        return false;
    }
    size_t w = 0;
    bool full = false;
    for (p++; p < e;) {
        unsigned char u8[4];
        size_t ul = 0;
        const unsigned char c = (unsigned char)*p;
        if (c == '"') {
            out[w] = 0;
            return true;
        }
        if (c < 0x20) {
            break;
        }
        if (c == '\\') {
            if (p + 1 >= e) {
                break;
            }
            const char x = p[1];
            p += 2;
            uint32_t cp;
            switch (x) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break;
            case 't': cp = '\t'; break;
            case 'u':
                if (!hex4(p, e, &cp)) {
                    goto bad;
                }
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t lo;
                    if (e - p < 6 || p[0] != '\\' || p[1] != 'u' || !hex4(p + 2, e, &lo) || lo < 0xDC00 || lo > 0xDFFF) {
                        goto bad;
                    }
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    goto bad;
                }
                if (cp == 0) {
                    goto bad; /* un 0 cortaría la cadena en C */
                }
                break;
            default: goto bad;
            }
            if (cp < 0x80) {
                u8[ul++] = (unsigned char)cp;
            } else if (cp < 0x800) {
                u8[ul++] = (unsigned char)(0xC0 | cp >> 6);
                u8[ul++] = (unsigned char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                u8[ul++] = (unsigned char)(0xE0 | cp >> 12);
                u8[ul++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                u8[ul++] = (unsigned char)(0x80 | (cp & 0x3F));
            } else {
                u8[ul++] = (unsigned char)(0xF0 | cp >> 18);
                u8[ul++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                u8[ul++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                u8[ul++] = (unsigned char)(0x80 | (cp & 0x3F));
            }
        } else {
            ul = utf8_len((const unsigned char *)p, (const unsigned char *)e);
            if (ul) {
                memcpy(u8, p, ul);
                p += ul;
            } else {
                u8[0] = '?'; /* byte suelto: se sustituye, no se copia */
                ul = 1;
                p++;
            }
        }
        if (!full && w + ul < n) {
            memcpy(out + w, u8, ul);
            w += ul;
        } else {
            full = true; /* recortado sin partir un carácter; se sigue validando */
        }
    }
bad:
    out[0] = 0;
    return false;
}

bool gal_j_u64(const char *p, const char *e, uint64_t *out)
{
    p = gal_j_ws(p, e);
    if (!p || p >= e || *p < '0' || *p > '9') {
        return false;
    }
    uint64_t v = 0;
    for (; p < e && *p >= '0' && *p <= '9'; p++) {
        const uint64_t d = (uint64_t)(*p - '0');
        if (v > (UINT64_MAX - d) / 10) {
            return false;
        }
        v = v * 10 + d;
    }
    if (p < e && (*p == '.' || *p == 'e' || *p == 'E')) {
        return false;
    }
    *out = v;
    return true;
}

bool gal_j_i64(const char *p, const char *e, int64_t *out)
{
    p = gal_j_ws(p, e);
    const bool neg = p && p < e && *p == '-';
    uint64_t v;
    if (!gal_j_u64(neg ? p + 1 : p, e, &v) || v > (uint64_t)INT64_MAX) {
        return false;
    }
    *out = neg ? -(int64_t)v : (int64_t)v;
    return true;
}

bool gal_j_bool(const char *p, const char *e, bool *out)
{
    p = gal_j_ws(p, e);
    if (!p || p >= e) {
        return false;
    }
    if (skip_lit(p, e, "true")) {
        *out = true;
        return true;
    }
    if (skip_lit(p, e, "false")) {
        *out = false;
        return true;
    }
    uint64_t v;
    if (gal_j_u64(p, e, &v) && v <= 1) {
        *out = v == 1;
        return true;
    }
    return false;
}

bool gal_j_is_null(const char *p, const char *e)
{
    p = gal_j_ws(p, e);
    return p && skip_lit(p, e, "null") != NULL;
}

bool gal_j_get_str(const char *obj, const char *e, const char *key, char *out, size_t n)
{
    const char *v = gal_j_get(obj, e, key, NULL);
    if (!v) {
        if (out && n) {
            out[0] = 0;
        }
        return false;
    }
    return gal_j_str(v, e, out, n);
}

bool gal_j_get_u64(const char *obj, const char *e, const char *key, uint64_t *out)
{
    const char *v = gal_j_get(obj, e, key, NULL);
    return v && gal_j_u64(v, e, out);
}

bool gal_j_get_i64(const char *obj, const char *e, const char *key, int64_t *out)
{
    const char *v = gal_j_get(obj, e, key, NULL);
    return v && gal_j_i64(v, e, out);
}

bool gal_j_get_bool(const char *obj, const char *e, const char *key, bool *out)
{
    const char *v = gal_j_get(obj, e, key, NULL);
    return v && gal_j_bool(v, e, out);
}

bool gal_j_quote(const char *s, char *out, size_t n)
{
    size_t w = 0;
    if (!out || n < 3) {
        return false;
    }
    out[w++] = '"';
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        char esc[8];
        const char *t = NULL;
        size_t tl = 1;
        switch (*p) {
        case '"': t = "\\\""; tl = 2; break;
        case '\\': t = "\\\\"; tl = 2; break;
        case '\n': t = "\\n"; tl = 2; break;
        case '\r': t = "\\r"; tl = 2; break;
        case '\t': t = "\\t"; tl = 2; break;
        default:
            if (*p < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", *p);
                t = esc;
                tl = 6;
            }
        }
        if (w + tl + 2 > n) {
            out[0] = 0;
            return false;
        }
        if (t) {
            memcpy(out + w, t, tl);
        } else {
            out[w] = (char)*p;
        }
        w += tl;
    }
    out[w++] = '"';
    out[w] = 0;
    return true;
}
