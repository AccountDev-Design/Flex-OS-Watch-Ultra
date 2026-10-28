/*
 * flex_id3 — ver include/flex_id3.h.
 */
#include "flex_id3.h"

#include <string.h>

#define FRAME_HEAD 256 /* lo que se lee de un marco: textos y la cabecera de APIC */

static bool read_at(const FlexMediaIO *io, uint32_t off, uint8_t *buf, uint32_t n)
{
    if (!io->seek(io->ctx, off)) {
        return false;
    }
    uint32_t got = 0;
    while (got < n) {
        const int r = io->read(io->ctx, buf + got, n - got);
        if (r <= 0) {
            return false;
        }
        got += (uint32_t)r;
    }
    return true;
}

static uint32_t syncsafe(const uint8_t *p)
{
    return (uint32_t)(p[0] & 0x7F) << 21 | (uint32_t)(p[1] & 0x7F) << 14 | (uint32_t)(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t be24(const uint8_t *p)
{
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

/* ---------------- texto ---------------- */

typedef struct {
    char *out;
    size_t cap, n;
    bool full;
} sink_t;

static void put_cp(sink_t *s, uint32_t cp)
{
    if (s->full) {
        return;
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
        cp = ' '; /* controles: un espacio */
    }
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = '?';
    }
    uint8_t b[4];
    size_t k;
    if (cp < 0x80) {
        b[0] = (uint8_t)cp;
        k = 1;
    } else if (cp < 0x800) {
        b[0] = (uint8_t)(0xC0 | cp >> 6);
        b[1] = (uint8_t)(0x80 | (cp & 0x3F));
        k = 2;
    } else if (cp < 0x10000) {
        b[0] = (uint8_t)(0xE0 | cp >> 12);
        b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (uint8_t)(0x80 | (cp & 0x3F));
        k = 3;
    } else {
        b[0] = (uint8_t)(0xF0 | cp >> 18);
        b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (uint8_t)(0x80 | (cp & 0x3F));
        k = 4;
    }
    if (s->n + k + 1 > s->cap) {
        s->full = true; /* nunca medio carácter */
        return;
    }
    memcpy(s->out + s->n, b, k);
    s->n += k;
}

size_t flexId3Text(uint8_t enc, const uint8_t *s, size_t n, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    sink_t k = {out, cap, 0, false};
    size_t i = 0;
    if (enc == 0) {
        for (; i < n && s[i]; i++) {
            put_cp(&k, s[i]);
        }
    } else if (enc == 1 || enc == 2) {
        bool le = enc == 1; /* UTF-16 sin BOM: lo habitual es little-endian */
        if (enc == 1 && n >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE) {
                le = true;
                i = 2;
            } else if (s[0] == 0xFE && s[1] == 0xFF) {
                le = false;
                i = 2;
            }
        }
        while (i + 1 < n) {
            uint32_t u = le ? (uint32_t)(s[i] | s[i + 1] << 8) : (uint32_t)(s[i] << 8 | s[i + 1]);
            i += 2;
            if (u == 0) {
                break;
            }
            if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {
                const uint32_t lo = le ? (uint32_t)(s[i] | s[i + 1] << 8) : (uint32_t)(s[i] << 8 | s[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            put_cp(&k, u);
        }
    } else if (enc == 3) {
        while (i < n && s[i]) {
            const uint8_t c = s[i];
            int len = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
            uint32_t cp = len == 0 ? c : len == 1 ? (c & 0x1Fu) : len == 2 ? (c & 0x0Fu) : (c & 0x07u);
            bool ok = len >= 0 && i + (size_t)len < n && c != 0xC0 && c != 0xC1 && c <= 0xF4;
            for (int j = 1; ok && j <= len; j++) {
                ok = (s[i + j] & 0xC0) == 0x80;
                cp = cp << 6 | (s[i + j] & 0x3Fu);
            }
            if (!ok || (len == 2 && cp < 0x800) || (len == 3 && cp < 0x10000)) {
                put_cp(&k, '?');
                i++;
                continue;
            }
            put_cp(&k, cp);
            i += (size_t)len + 1;
        }
    }
    /* sin espacios al final */
    while (k.n > 0 && out[k.n - 1] == ' ') {
        k.n--;
    }
    out[k.n] = 0;
    return k.n;
}

/* Quita la desincronización (FF 00 -> FF) de un trozo ya leído. */
static size_t unsync(uint8_t *b, size_t n)
{
    size_t w = 0;
    for (size_t r = 0; r < n; r++) {
        b[w++] = b[r];
        if (b[r] == 0xFF && r + 1 < n && b[r + 1] == 0x00) {
            r++;
        }
    }
    return w;
}

/* ---------------- portada ---------------- */

/* Salta un texto terminado en 0 (o 00 00 en UTF-16, alineado). */
static size_t skip_term(const uint8_t *b, size_t i, size_t n, uint8_t enc)
{
    if (enc == 1 || enc == 2) {
        for (; i + 1 < n; i += 2) {
            if (b[i] == 0 && b[i + 1] == 0) {
                return i + 2;
            }
        }
        return n + 1;
    }
    for (; i < n; i++) {
        if (b[i] == 0) {
            return i + 1;
        }
    }
    return n + 1;
}

static bool pic_header(const uint8_t *b, size_t n, bool v22, uint8_t *kind, uint8_t *ptype, size_t *hdr)
{
    if (n < 4) {
        return false;
    }
    const uint8_t enc = b[0];
    size_t i;
    if (v22) {
        *kind = !memcmp(b + 1, "JPG", 3) ? FLEXID3_PIC_JPEG : !memcmp(b + 1, "PNG", 3) ? FLEXID3_PIC_PNG
                                                                                       : FLEXID3_PIC_OTHER;
        i = 4;
    } else {
        const size_t m = skip_term(b, 1, n, 0);
        if (m > n) {
            return false;
        }
        const char *mime = (const char *)b + 1;
        const size_t ml = m - 2;
        *kind = (ml == 10 && !memcmp(mime, "image/jpeg", 10)) || (ml == 9 && !memcmp(mime, "image/jpg", 9))
                    ? FLEXID3_PIC_JPEG
                : (ml == 9 && !memcmp(mime, "image/png", 9)) ? FLEXID3_PIC_PNG
                                                             : FLEXID3_PIC_OTHER;
        i = m;
    }
    if (i >= n) {
        return false;
    }
    *ptype = b[i++];
    i = skip_term(b, i, n, enc);
    if (i > n) {
        return false; /* la descripción no termina dentro de lo leído */
    }
    *hdr = i;
    return true;
}

/* ---------------- etiqueta ---------------- */

static bool frame_id_ok(const uint8_t *id, int len)
{
    for (int i = 0; i < len; i++) {
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) {
            return false;
        }
    }
    return true;
}

int flexId3Read(const FlexMediaIO *io, FlexId3Info *out)
{
    if (!io || !io->read || !io->seek || !io->size || !out) {
        return FLEXID3_ERR_FORMAT;
    }
    memset(out, 0, sizeof(*out));
    const uint32_t size = io->size(io->ctx);
    uint8_t h[10];
    if (size < 10) {
        return FLEXID3_NONE;
    }
    if (!read_at(io, 0, h, 10)) {
        return FLEXID3_ERR_IO;
    }
    if (memcmp(h, "ID3", 3) || h[3] < 2 || h[3] > 4 || h[4] == 0xFF || ((h[6] | h[7] | h[8] | h[9]) & 0x80)) {
        return FLEXID3_NONE;
    }
    const uint8_t ver = h[3], flags = h[5];
    const uint32_t body = syncsafe(h + 6);
    out->version = ver;
    out->tagBytes = 10 + body + ((ver == 4 && (flags & 0x10)) ? 10 : 0);
    if (ver == 2 && (flags & 0x40)) {
        return FLEXID3_OK; /* v2.2 "comprimida": no definida, se ignora */
    }
    const bool tag_unsync = (flags & 0x80) != 0;
    uint32_t end = 10 + body;
    if (end > size) {
        end = size; /* etiqueta que dice más de lo que hay */
    }
    if (end - 10 > FLEXID3_SCAN_MAX) {
        end = 10 + FLEXID3_SCAN_MAX;
    }
    uint32_t pos = 10;
    if (ver >= 3 && (flags & 0x40)) {
        uint8_t e[4];
        if (pos + 4 > end || !read_at(io, pos, e, 4)) {
            return FLEXID3_OK;
        }
        const uint32_t ext = ver == 4 ? syncsafe(e) : be32(e) + 4;
        if (ext < 6 || ext > end - pos) {
            return FLEXID3_OK;
        }
        pos += ext;
    }
    const int idlen = ver == 2 ? 3 : 4;
    const uint32_t fh = ver == 2 ? 6 : 10;
    bool have_pic = false;
    uint8_t best_ptype = 0xFF;
    uint8_t buf[FRAME_HEAD];
    while (pos + fh <= end) {
        uint8_t f[10];
        if (!read_at(io, pos, f, fh)) {
            return FLEXID3_ERR_IO;
        }
        if (f[0] == 0 || !frame_id_ok(f, idlen)) {
            break; /* relleno o basura: fin de los marcos */
        }
        uint32_t len;
        uint16_t fflags = 0;
        if (ver == 2) {
            len = be24(f + 3);
        } else if (ver == 3) {
            len = be32(f + 4);
            fflags = (uint16_t)(f[8] << 8 | f[9]);
        } else {
            /* 2.4 dice sincroseguro; algunos programas escribían el tamaño normal. */
            len = (f[4] | f[5] | f[6] | f[7]) & 0x80 ? be32(f + 4) : syncsafe(f + 4);
            fflags = (uint16_t)(f[8] << 8 | f[9]);
        }
        const uint32_t data = pos + fh;
        if (len == 0 || len > end - data) {
            break; /* un marco que se sale de la etiqueta: se para aquí */
        }
        bool skip = false;
        uint32_t dstart = data, dlen = len;
        bool fr_unsync = tag_unsync;
        if (ver == 3) {
            skip = (fflags & 0x00C0) != 0; /* comprimido o cifrado */
            if (!skip && (fflags & 0x0020)) {
                dstart++;
                dlen--; /* byte de grupo */
            }
        } else if (ver == 4) {
            skip = (fflags & 0x000C) != 0; /* comprimido o cifrado */
            if (!skip && (fflags & 0x0040)) {
                dstart++;
                dlen = dlen ? dlen - 1 : 0;
            }
            if (!skip && (fflags & 0x0001)) {
                dstart += 4;
                dlen = dlen >= 4 ? dlen - 4 : 0; /* indicador de longitud */
            }
            fr_unsync = fr_unsync || (fflags & 0x0002);
        }
        const bool is_title = !memcmp(f, ver == 2 ? "TT2" : "TIT2", (size_t)idlen);
        const bool is_artist = !memcmp(f, ver == 2 ? "TP1" : "TPE1", (size_t)idlen);
        const bool is_album = !memcmp(f, ver == 2 ? "TAL" : "TALB", (size_t)idlen);
        const bool is_pic = !memcmp(f, ver == 2 ? "PIC" : "APIC", (size_t)idlen);
        if (!skip && dlen > 1 && (is_title || is_artist || is_album || is_pic)) {
            uint32_t n = dlen < sizeof(buf) ? dlen : (uint32_t)sizeof(buf);
            if (!read_at(io, dstart, buf, n)) {
                return FLEXID3_ERR_IO;
            }
            if (is_pic) {
                uint8_t kind = 0, ptype = 0;
                size_t hdr = 0;
                /* Una portada desincronizada no se puede leer por trozos: se ignora. */
                if (!fr_unsync && pic_header(buf, n, ver == 2, &kind, &ptype, &hdr) && hdr < dlen &&
                    (!have_pic || (ptype == 3 && best_ptype != 3))) {
                    have_pic = true;
                    best_ptype = ptype;
                    out->picKind = kind;
                    out->picOffset = dstart + (uint32_t)hdr;
                    out->picBytes = dlen - (uint32_t)hdr;
                }
            } else {
                if (fr_unsync) {
                    n = (uint32_t)unsync(buf, n);
                }
                char *dst = is_title ? out->title : is_artist ? out->artist : out->album;
                if (!dst[0] && n > 1) {
                    flexId3Text(buf[0], buf + 1, n - 1, dst, FLEXID3_TEXT_MAX);
                }
            }
        }
        pos = data + len;
    }
    return FLEXID3_OK;
}
