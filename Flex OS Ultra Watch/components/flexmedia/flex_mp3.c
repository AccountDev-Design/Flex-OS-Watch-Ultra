/*
 * flex_mp3 — ver include/flex_mp3.h.
 */
#include "flex_mp3.h"

#include <string.h>

static const uint16_t kKbpsV1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const uint16_t kKbpsV2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
static const uint32_t kRate[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

bool flexMp3ParseHeader(const uint8_t h[4], FlexMp3Hdr *out)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) {
        return false;
    }
    const int ver = (h[1] >> 3) & 3;   /* 0 = 2.5, 1 = reservado, 2 = MPEG-2, 3 = MPEG-1 */
    const int layer = (h[1] >> 1) & 3; /* 1 = capa III */
    const int bri = h[2] >> 4;
    const int sri = (h[2] >> 2) & 3;
    const int pad = (h[2] >> 1) & 1;
    if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3 || (h[3] & 3) == 2) {
        return false; /* reservado, otra capa, formato libre o tasa inválida */
    }
    const int vi = ver == 3 ? 0 : ver == 2 ? 1 : 2;
    const uint16_t kbps = ver == 3 ? kKbpsV1[bri] : kKbpsV2[bri];
    const uint32_t sr = kRate[vi][sri];
    const uint32_t coef = ver == 3 ? 144000u : 72000u;
    out->version = ver == 3 ? 1 : ver == 2 ? 2 : 25;
    out->kbps = kbps;
    out->sampleRate = sr;
    out->channels = (h[3] >> 6) == 3 ? 1 : 2;
    out->frameBytes = (uint16_t)(coef * kbps / sr + (uint32_t)pad);
    out->samples = ver == 3 ? 1152 : 576;
    return out->frameBytes >= 24;
}

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

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ¿Hay una trama válida en off, y otras dos detrás que concuerdan? */
static bool confirm(const FlexMediaIO *io, uint32_t size, uint32_t off, const FlexMp3Hdr *first)
{
    uint32_t at = off + first->frameBytes;
    for (int k = 0; k < 2; k++) {
        uint8_t h[4];
        FlexMp3Hdr x;
        if (at + 4 > size) {
            return k > 0; /* dos tramas y fin: vale; una sola no se confirma con nada */
        }
        if (!read_at(io, at, h, 4) || !flexMp3ParseHeader(h, &x) || x.version != first->version ||
            x.sampleRate != first->sampleRate) {
            return false;
        }
        at += x.frameBytes;
    }
    return true;
}

int flexMp3Probe(const FlexMediaIO *io, FlexMp3Info *out)
{
    if (!io || !io->read || !io->seek || !io->size || !out) {
        return FLEXMP3_ERR_FORMAT;
    }
    memset(out, 0, sizeof(*out));
    const uint32_t size = io->size(io->ctx);
    if (size < 32) {
        return FLEXMP3_ERR_FORMAT;
    }
    uint8_t h[10];
    uint32_t start = 0;
    if (!read_at(io, 0, h, 10)) {
        return FLEXMP3_ERR_IO;
    }
    if (!memcmp(h, "ID3", 3) && h[3] != 0xFF && h[4] != 0xFF && !((h[6] | h[7] | h[8] | h[9]) & 0x80)) {
        const uint32_t tag = (uint32_t)h[6] << 21 | (uint32_t)h[7] << 14 | (uint32_t)h[8] << 7 | h[9];
        start = 10 + tag + ((h[5] & 0x10) ? 10 : 0);
        if (start >= size) {
            return FLEXMP3_ERR_FORMAT; /* una etiqueta sin audio detrás */
        }
    }
    /* Sincronía: ventanas de 2 KB con 3 bytes de solape. */
    uint8_t win[2048];
    const uint32_t limit = start + FLEXMP3_SCAN_MAX < size ? start + FLEXMP3_SCAN_MAX : size;
    for (uint32_t base = start; base + 4 <= limit;) {
        uint32_t n = limit - base < sizeof(win) ? limit - base : (uint32_t)sizeof(win);
        if (!read_at(io, base, win, n)) {
            return FLEXMP3_ERR_IO;
        }
        for (uint32_t i = 0; i + 4 <= n; i++) {
            FlexMp3Hdr fh;
            if (win[i] != 0xFF || !flexMp3ParseHeader(win + i, &fh)) {
                continue;
            }
            const uint32_t off = base + i;
            if (!confirm(io, size, off, &fh)) {
                continue;
            }
            out->sampleRate = fh.sampleRate;
            out->channels = fh.channels;
            out->version = fh.version;
            out->kbps = fh.kbps;
            out->dataStart = off;
            out->audioStart = off;
            /* Xing / Info (tras la información lateral) o VBRI (a 32 bytes). */
            const uint32_t side = fh.version == 1 ? (fh.channels == 1 ? 17 : 32) : (fh.channels == 1 ? 9 : 17);
            uint8_t x[18];
            if (off + 4 + side + 12 <= size && read_at(io, off + 4 + side, x, 12) &&
                (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) && (be32(x + 4) & 1)) {
                out->frames = be32(x + 8);
                out->vbr = !memcmp(x, "Xing", 4);
                out->audioStart = off + fh.frameBytes; /* la trama de la cabecera no suena */
            } else if (off + 4 + 32 + 18 <= size && read_at(io, off + 4 + 32, x, 18) && !memcmp(x, "VBRI", 4)) {
                out->frames = be32(x + 14); /* VBRI: versión(2) retardo(2) calidad(2) bytes(4) tramas(4) */
                out->vbr = true;
                out->audioStart = off + fh.frameBytes;
            }
            if (out->frames) {
                out->durMs = (uint32_t)((uint64_t)out->frames * fh.samples * 1000u / fh.sampleRate);
            } else {
                uint32_t audio = size - off;
                uint8_t t[3];
                if (size >= 128 && read_at(io, size - 128, t, 3) && !memcmp(t, "TAG", 3) && audio > 128) {
                    audio -= 128; /* ID3v1 al final */
                }
                out->durMs = (uint32_t)((uint64_t)audio * 8u / fh.kbps);
            }
            return FLEXMP3_OK;
        }
        if (n < sizeof(win)) {
            break;
        }
        base += n - 3;
    }
    return FLEXMP3_ERR_FORMAT;
}
