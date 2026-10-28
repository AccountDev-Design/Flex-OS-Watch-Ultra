/*
 * gal_wm_map — registro de la biblioteca de medios -> elemento de la Galería.
 * Ver gal_wm.h. Sin E/S: se prueba en tests/host (test_gallery.c).
 */
#include "gal_wm.h"

#include <string.h>

gal_fmt_t gal_wm_fmt(int f)
{
    switch (f) {
    case FML_F_JPEG:      return GAL_F_JPEG;
    case FML_F_JPEG_PROG: return GAL_F_JPEG_PROG;
    case FML_F_PNG:       return GAL_F_PNG;
    case FML_F_GIF:       return GAL_F_GIF;
    case FML_F_BMP:       return GAL_F_BMP;
    case FML_F_WEBP:      return GAL_F_WEBP;
    case FML_F_HEIC:      return GAL_F_HEIC;
    case FML_F_AVIF:      return GAL_F_AVIF;
    case FML_F_AVI_MJPEG:
    case FML_F_AVI_OTHER:
    case FML_F_MP4:
    case FML_F_MOV:
    case FML_F_WEBM:
    case FML_F_MKV:       return GAL_F_VIDEO;
    case FML_F_FXP:       return GAL_F_FXP;
    default:              return GAL_F_UNKNOWN;
    }
}

bool gal_wm_item(const FlexMlRec *r, int vol, gal_item_t *out)
{
    int kind;
    switch (r->kind) {
    case FML_K_PHOTO: kind = GAL_K_PHOTO; break;
    case FML_K_VIDEO: kind = GAL_K_VIDEO; break;
    case FML_K_DRAW:  kind = GAL_K_DRAW; break;
    default:          return false; /* música y grabaciones: no son de la Galería */
    }
    if (!r->path[0] || strlen(r->path) >= sizeof(out->path)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->src = GAL_SRC_LOCAL;
    out->origin_src = GAL_SRC_NONE;
    out->vol = (uint8_t)vol;
    out->mid = r->id;
    out->lfmt = r->fmt;
    out->kind = (uint8_t)kind;
    gal_fmt_t f = gal_wm_fmt(r->fmt);
    if (f == GAL_F_UNKNOWN) {
        f = gal_fmt_from_name(r->path); /* aún sin comprobar: la extensión como pista */
    }
    out->fmt = (uint8_t)f;
    memcpy(out->path, r->path, strlen(r->path) + 1);
    /* El nombre: el que tenía al llegar (UTF-8); si no, el del disco. */
    const char *base = strrchr(r->path, '/');
    gal_copyz(out->name, sizeof(out->name), r->name[0] ? r->name : base ? base + 1 : r->path);
    out->size = r->size;
    const uint32_t t = r->created ? r->created : r->added;
    out->when = (int64_t)t * 1000;
    /* La miniatura cambia con la edición y con su versión: otra clave, otra imagen. */
    out->tver = (r->modified << 8) ^ r->thumbVer ^ r->crc;
    out->w = r->w;
    out->h = r->h;
    out->dur_ms = r->durMs;
    if (r->flags & FML_R_THUMB) {
        out->flags |= GAL_I_THUMB;
    }
    if (r->flags & FML_R_LOCKED) {
        out->flags |= GAL_I_LOCKED;
    }
    if (r->state == FML_S_PROCESSING) {
        out->flags |= GAL_I_BUSY;
    }
    if (r->state == FML_S_ERROR && r->err == FML_E_CORRUPT) {
        out->flags |= GAL_I_DAMAGED;
    }
    return true;
}

void gal_wm_sort(gal_item_t *it, int n)
{
    for (int i = 1; i < n; i++) { /* inserción: estable y sin memoria extra */
        const gal_item_t x = it[i];
        int k = i - 1;
        while (k >= 0 && it[k].when < x.when) {
            it[k + 1] = it[k];
            k--;
        }
        it[k + 1] = x;
    }
}

bool gal_wm_playable_video(const gal_item_t *it)
{
    return it->kind == GAL_K_VIDEO && it->lfmt == FML_F_AVI_MJPEG && !(it->flags & GAL_I_LOCKED);
}
