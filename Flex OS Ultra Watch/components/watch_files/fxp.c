/*
 * fxp — ver include/fxp.h
 */
#include "fxp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "flexfs.h"

#define REAL_N (FL_PATH_MAX + 160)

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void hdr_bytes(uint8_t h[FXP_HDR_SIZE], uint16_t w, uint16_t hh, uint16_t strokes)
{
    h[0] = 'F';
    h[1] = 'X';
    h[2] = 'P';
    h[3] = '1';
    w16(h + 4, w);
    w16(h + 6, hh);
    w16(h + 8, strokes);
    w16(h + 10, 0);
}

static bool read_hdr(FILE *f, fxp_info_t *out)
{
    uint8_t h[FXP_HDR_SIZE];
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, "FXP1", 4) != 0) {
        return false;
    }
    out->w = r16(h + 4);
    out->h = r16(h + 6);
    out->strokes = r16(h + 8);
    return out->w > 0 && out->h > 0;
}

static FILE *open_v(const char *path, const char *mode)
{
    char r[REAL_N];
    return ffs_real_path(path, r, sizeof(r)) ? fopen(r, mode) : NULL;
}

/* Escribe `hdr` + (opcional) el contenido de `src` sin su último trazo en un
 * temporal y lo pone en su sitio: undo y clear nunca dejan el archivo a medias. */
static bool rewrite(const char *path, const fxp_info_t *in, uint16_t keep)
{
    char r[REAL_N], tmp[REAL_N + 8];
    if (!ffs_real_path(path, r, sizeof(r)) || snprintf(tmp, sizeof(tmp), "%s.tmp", r) >= (int)sizeof(tmp)) {
        return false;
    }
    FILE *src = keep ? fopen(r, "rb") : NULL;
    if (keep && !src) {
        return false;
    }
    FILE *dst = fopen(tmp, "wb");
    if (!dst) {
        if (src) {
            fclose(src);
        }
        return false;
    }
    uint8_t h[FXP_HDR_SIZE];
    hdr_bytes(h, in->w, in->h, keep);
    bool ok = fwrite(h, 1, sizeof(h), dst) == sizeof(h);
    if (src) {
        fxp_info_t skip;
        ok = ok && read_hdr(src, &skip);
        uint8_t buf[256];
        for (uint16_t s = 0; ok && s < keep; s++) {
            uint8_t sh[6];
            ok = fread(sh, 1, 6, src) == 6 && fwrite(sh, 1, 6, dst) == 6;
            size_t left = (size_t)r16(sh + 4) * 4;
            while (ok && left) {
                const size_t k = left > sizeof(buf) ? sizeof(buf) : left;
                ok = fread(buf, 1, k, src) == k && fwrite(buf, 1, k, dst) == k;
                left -= k;
            }
        }
        fclose(src);
    }
    if (fclose(dst) != 0 || !ok) {
        unlink(tmp);
        return false;
    }
    if (rename(tmp, r) != 0) {
        unlink(r);
        if (rename(tmp, r) != 0) {
            unlink(tmp);
            return false;
        }
    }
    return true;
}

bool fxp_create(const char *path, uint16_t w, uint16_t h)
{
    if (!w || !h) {
        return false;
    }
    const fxp_info_t in = {.w = w, .h = h};
    return rewrite(path, &in, 0);
}

bool fxp_info(const char *path, fxp_info_t *out)
{
    FILE *f = open_v(path, "rb");
    if (!f || !out) {
        if (f) {
            fclose(f);
        }
        return false;
    }
    const bool ok = read_hdr(f, out);
    fclose(f);
    return ok;
}

bool fxp_append(const char *path, uint16_t color, uint8_t radius, const int16_t *xy, uint16_t pts)
{
    if (!xy || pts == 0 || pts > FXP_MAX_PTS) {
        return false;
    }
    FILE *f = open_v(path, "r+b");
    if (!f) {
        return false;
    }
    fxp_info_t in;
    bool ok = read_hdr(f, &in) && in.strokes < 0xFFFF && fseek(f, 0, SEEK_END) == 0;
    if (ok) {
        uint8_t sh[6];
        w16(sh, color);
        sh[2] = radius;
        sh[3] = 0;
        w16(sh + 4, pts);
        ok = fwrite(sh, 1, 6, f) == 6;
        for (uint16_t i = 0; ok && i < pts; i++) {
            uint8_t p[4];
            w16(p, (uint16_t)xy[2 * i]);
            w16(p + 2, (uint16_t)xy[2 * i + 1]);
            ok = fwrite(p, 1, 4, f) == 4;
        }
    }
    if (ok) {
        /* el contador, al final: si se corta antes, el trazo nuevo no cuenta */
        uint8_t c[2];
        w16(c, (uint16_t)(in.strokes + 1));
        ok = fseek(f, 8, SEEK_SET) == 0 && fwrite(c, 1, 2, f) == 2;
    }
    return fclose(f) == 0 && ok;
}

bool fxp_replay(const char *path, float sc, int ox, int oy, fxp_seg_cb cb, void *user)
{
    if (!cb) {
        return false;
    }
    FILE *f = open_v(path, "rb");
    if (!f) {
        return false;
    }
    fxp_info_t in;
    if (!read_hdr(f, &in)) {
        fclose(f);
        return false;
    }
    for (uint16_t s = 0; s < in.strokes; s++) {
        uint8_t sh[6];
        if (fread(sh, 1, 6, f) != 6) {
            break; /* archivo cortado: se pinta lo que hay */
        }
        /* el radio también se escala: la miniatura se parece al dibujo */
        int rad = (int)(sh[2] * sc + 0.5f);
        if (rad < 1) {
            rad = 1;
        }
        const uint16_t color = r16(sh), pts = r16(sh + 4);
        int px = 0, py = 0;
        for (uint16_t i = 0; i < pts; i++) {
            uint8_t p[4];
            if (fread(p, 1, 4, f) != 4) {
                s = in.strokes;
                break;
            }
            const int x = ox + (int)((int16_t)r16(p) * sc + 0.5f);
            const int y = oy + (int)((int16_t)r16(p + 2) * sc + 0.5f);
            cb(i ? px : x, i ? py : y, x, y, color, rad, user);
            px = x;
            py = y;
        }
    }
    fclose(f);
    return true;
}

bool fxp_undo(const char *path)
{
    fxp_info_t in;
    return fxp_info(path, &in) && in.strokes > 0 && rewrite(path, &in, (uint16_t)(in.strokes - 1));
}

bool fxp_clear(const char *path)
{
    fxp_info_t in;
    return fxp_info(path, &in) && rewrite(path, &in, 0);
}
