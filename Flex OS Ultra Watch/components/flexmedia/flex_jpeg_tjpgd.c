/*
 * flex_jpeg_tjpgd — FlexOS_JPEG.h (modo flujo, filas RGB888) sobre TJpgDec.
 * Ver include/FlexOS_JPEG.h. Lo usan la tarea de medios (miniaturas de la
 * biblioteca) y la del servidor web (validar lo que sube el móvil), así que
 * no tiene estado global.
 *
 * TJpgDec entrega bloques MCU (8 o 16 filas) de izquierda a derecha y fila de
 * MCU a fila de MCU. Aquí se juntan en una banda de la altura de un MCU y,
 * cuando el decodificador pasa a la fila siguiente, se entregan las filas de
 * la banda de arriba abajo. En el PC (sin escala en el decodificador) cada d
 * filas de la banda se reducen a una con la media de caja d x d.
 */
#include "FlexOS_JPEG.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM) && !defined(FLEX_JPEG_LVGL)
/* TJpgDec R0.01 de la ROM del ESP32-S3: RGB888 en orden R, G, B y con
 * escala. LVGL enlaza su propio TJpgDec con los mismos nombres, así que la
 * ROM se llama por alias (flexmedia_rom.ld, direcciones de esp32s3.rom.ld). */
#include "rom/tjpgd.h"
#define DEC_SCALES 1
JRESULT flexmedia_rom_jd_prepare(JDEC *, UINT (*)(JDEC *, BYTE *, UINT), void *, UINT, void *);
JRESULT flexmedia_rom_jd_decomp(JDEC *, UINT (*)(JDEC *, void *, JRECT *), BYTE);
#define JD_PREPARE flexmedia_rom_jd_prepare
#define JD_DECOMP  flexmedia_rom_jd_decomp
#define OFS_R 0
#define OFS_G 1
#define OFS_B 2
typedef UINT in_n_t;
typedef UINT out_ret_t;
#else
/* TJpgDec R0.03 de LVGL (LV_USE_TJPGD): sin escala y en orden B, G, R. */
#include "libs/tjpgd/tjpgd.h"
#define DEC_SCALES 0
#define JD_PREPARE jd_prepare
#define JD_DECOMP  jd_decomp
#define OFS_R 2
#define OFS_G 1
#define OFS_B 0
typedef size_t in_n_t;
typedef int out_ret_t;
#endif

#define WORK_BYTES 8192 /* área de trabajo de TJpgDec (R0.01 pide 3100) */
#define BAND_MAX   16   /* filas de un MCU a escala 1:1 (4:2:0) */

/* ---- MJPEG sin DHT ----
 * Muchas cámaras (y ffmpeg con -f mjpeg) guardan cada fotograma de un AVI
 * MJPEG SIN tablas de Huffman: se da por hecho que son las del anexo K.3 de
 * la norma (lo que dice la especificación AVI1/MJPEG). TJpgDec no lo sabe y
 * lo rechaza. Aquí, si se llega al SOS sin haber visto un DHT, se inyecta ese
 * segmento estándar justo antes. Una tabla 2 o 3 sin definir sigue siendo
 * un error (no hay versión estándar). */
static const uint8_t kStdDht[] = {
    0xFF, 0xC4, 0x01, 0xA2,
    /* DC luminancia (clase 0, tabla 0) */
    0x00, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    /* DC crominancia (clase 0, tabla 1) */
    0x01, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    /* AC luminancia (clase 1, tabla 0) */
    0x10, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
    /* AC crominancia (clase 1, tabla 1) */
    0x11, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77,
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};
_Static_assert(sizeof(kStdDht) == 2 + 0x01A2, "DHT estándar: 418 bytes de segmento");

/* Lector con estado que recorre los segmentos de cabecera hasta el SOS y,
 * desde ahí, deja pasar los datos tal cual. */
typedef struct {
    FlexJpegReadFn rd;
    void *ctx;
    int state;              /* 0 cabecera, 1 datos */
    bool seen_dht, sos_seg;
    uint32_t seg_left;      /* bytes de la carga del segmento en curso */
    uint8_t hdr[4];
    uint8_t hdr_len, hdr_pos;
    const uint8_t *inj;
    size_t inj_len, inj_pos;
} jfilt_t;

static int rd_exact(jfilt_t *f, uint8_t *b, size_t n)
{
    size_t got = 0;
    while (got < n) {
        const int r = f->rd(f->ctx, b + got, n - got);
        if (r <= 0) {
            return r < 0 ? -1 : (int)got;
        }
        got += (size_t)r;
    }
    return (int)got;
}

static int filt_read(void *ctx, uint8_t *buf, size_t n)
{
    jfilt_t *f = ctx;
    size_t out = 0;
    while (out < n) {
        if (f->inj_pos < f->inj_len) {
            size_t k = f->inj_len - f->inj_pos;
            if (k > n - out) {
                k = n - out;
            }
            memcpy(buf + out, f->inj + f->inj_pos, k);
            f->inj_pos += k;
            out += k;
            continue;
        }
        if (f->hdr_pos < f->hdr_len) {
            buf[out++] = f->hdr[f->hdr_pos++];
            continue;
        }
        if (f->state == 1 || f->seg_left) {
            size_t want = n - out;
            if (f->state == 0 && want > f->seg_left) {
                want = f->seg_left;
            }
            const int r = f->rd(f->ctx, buf + out, want);
            if (r < 0) {
                return out ? (int)out : -1;
            }
            if (r == 0) {
                break;
            }
            out += (size_t)r;
            if (f->state == 0) {
                f->seg_left -= (uint32_t)r;
                if (!f->seg_left && f->sos_seg) {
                    f->state = 1; /* tras la cabecera del SOS vienen los datos */
                }
            }
            continue;
        }
        /* En el límite de un marcador de la cabecera. */
        uint8_t m[2];
        int r = rd_exact(f, m, 2);
        if (r < 0) {
            return out ? (int)out : -1;
        }
        if (r < 2 || m[0] != 0xFF) {
            /* No es una cabecera JPEG: se entrega tal cual y decide TJpgDec. */
            memcpy(f->hdr, m, (size_t)r);
            f->hdr_len = (uint8_t)r;
            f->hdr_pos = 0;
            f->state = 1;
            if (!r) {
                break;
            }
            continue;
        }
        if (m[1] == 0xD8 || m[1] == 0x01 || (m[1] >= 0xD0 && m[1] <= 0xD7) || m[1] == 0xFF) {
            /* Sin longitud (o relleno 0xFF: se vuelve a mirar el byte siguiente). */
            f->hdr[0] = 0xFF;
            f->hdr[1] = m[1];
            f->hdr_len = m[1] == 0xFF ? 1 : 2;
            f->hdr_pos = 0;
            if (m[1] == 0xFF) {
                /* el segundo 0xFF es el principio del marcador siguiente: se relee */
                uint8_t nx;
                if (rd_exact(f, &nx, 1) != 1) {
                    break;
                }
                f->hdr[1] = nx;
                f->hdr_len = 2;
                if (!(nx == 0xD8 || nx == 0x01 || (nx >= 0xD0 && nx <= 0xD7))) {
                    uint8_t l[2];
                    if (rd_exact(f, l, 2) != 2) {
                        break;
                    }
                    f->hdr[2] = l[0];
                    f->hdr[3] = l[1];
                    f->hdr_len = 4;
                    const uint32_t L = ((uint32_t)l[0] << 8) | l[1];
                    f->seg_left = L >= 2 ? L - 2 : 0;
                    f->seen_dht |= nx == 0xC4;
                    f->sos_seg = nx == 0xDA;
                }
            }
            continue;
        }
        uint8_t l[2];
        if (rd_exact(f, l, 2) != 2) {
            break;
        }
        const uint32_t L = ((uint32_t)l[0] << 8) | l[1];
        if (m[1] == 0xC4) {
            f->seen_dht = true;
        }
        if (m[1] == 0xDA && !f->seen_dht) {
            f->inj = kStdDht;
            f->inj_len = sizeof(kStdDht);
            f->inj_pos = 0;
            f->seen_dht = true;
        }
        f->hdr[0] = 0xFF;
        f->hdr[1] = m[1];
        f->hdr[2] = l[0];
        f->hdr[3] = l[1];
        f->hdr_len = 4;
        f->hdr_pos = 0;
        f->seg_left = L >= 2 ? L - 2 : 0;
        f->sos_seg = m[1] == 0xDA;
        if (f->sos_seg && !f->seg_left) {
            f->state = 1;
        }
    }
    return (int)out;
}

typedef struct {
    FlexJpegReadFn rd;
    void *rd_ctx;
    bool io_err;
    FlexJpegRow888Cb cb;
    void *user;
    int post;       /* divisor que se aplica después del decodificador (PC) */
    int dw, dh;     /* salida del decodificador */
    int ow, oh;     /* salida final */
    uint8_t *band;  /* dw x BAND_MAX x 3 (orden del decodificador) */
    int band_top;   /* fila (del decodificador) de la primera de la banda */
    int band_rows;  /* filas con datos */
    uint32_t *acc;  /* ow x 3 (PC: media de caja) */
    int acc_rows;
    int out_y;
    uint8_t *row;   /* fila de salida RGB888 */
    bool stop;
} jctx_t;

static in_n_t in_cb(JDEC *jd, uint8_t *buf, in_n_t n)
{
    jctx_t *c = jd->device;
    if (c->io_err) {
        return 0;
    }
    in_n_t got = 0;
    uint8_t scratch[64];
    while (got < n) {
        const size_t want = buf ? (size_t)(n - got) : ((size_t)(n - got) < sizeof(scratch) ? (size_t)(n - got) : sizeof(scratch));
        const int r = c->rd(c->rd_ctx, buf ? buf + got : scratch, want);
        if (r <= 0) {
            if (r < 0) {
                c->io_err = true;
            }
            break;
        }
        got += (in_n_t)r;
    }
    return got;
}

/* Una fila del decodificador (src, dw píxeles en su orden de bytes) hacia
 * la salida: tal cual o acumulada para la media de caja. */
static bool push_row(jctx_t *c, const uint8_t *src, bool last)
{
    if (c->post == 1) {
        for (int x = 0; x < c->ow; x++) {
            c->row[3 * x] = src[3 * x + OFS_R];
            c->row[3 * x + 1] = src[3 * x + OFS_G];
            c->row[3 * x + 2] = src[3 * x + OFS_B];
        }
        if (c->out_y < c->oh && !c->cb(c->user, c->out_y, c->ow, c->row)) {
            return false;
        }
        c->out_y++;
        return true;
    }
    for (int x = 0; x < c->dw; x++) {
        uint32_t *a = c->acc + 3 * (x / c->post);
        a[0] += src[3 * x + OFS_R];
        a[1] += src[3 * x + OFS_G];
        a[2] += src[3 * x + OFS_B];
    }
    if (++c->acc_rows < c->post && !last) {
        return true;
    }
    for (int ox = 0; ox < c->ow; ox++) {
        const int x0 = ox * c->post;
        const int cols = (x0 + c->post <= c->dw ? c->post : c->dw - x0);
        const uint32_t n = (uint32_t)(cols * c->acc_rows);
        for (int k = 0; k < 3; k++) {
            c->row[3 * ox + k] = (uint8_t)((c->acc[3 * ox + k] + n / 2) / n);
        }
    }
    memset(c->acc, 0, (size_t)c->ow * 3 * sizeof(uint32_t));
    c->acc_rows = 0;
    if (c->out_y < c->oh && !c->cb(c->user, c->out_y, c->ow, c->row)) {
        return false;
    }
    c->out_y++;
    return true;
}

static bool flush_band(jctx_t *c, bool last_band)
{
    for (int r = 0; r < c->band_rows; r++) {
        const bool last = last_band && r == c->band_rows - 1;
        if (!push_row(c, c->band + (size_t)r * (size_t)c->dw * 3, last)) {
            return false;
        }
    }
    c->band_rows = 0;
    return true;
}

static out_ret_t out_cb(JDEC *jd, void *bitmap, JRECT *rc)
{
    jctx_t *c = jd->device;
    if (c->stop) {
        return 0;
    }
    if ((int)rc->top != c->band_top) {
        if (!flush_band(c, false)) {
            c->stop = true;
            return 0;
        }
        c->band_top = rc->top;
    }
    const int bw = (int)rc->right - (int)rc->left + 1;
    const int bh = (int)rc->bottom - (int)rc->top + 1;
    if (bh > BAND_MAX || rc->left >= (unsigned)c->dw) {
        return 1; /* no pasa con TJpgDec; si pasara, se ignora el bloque */
    }
    const int cw = (int)rc->left + bw <= c->dw ? bw : c->dw - (int)rc->left;
    const uint8_t *px = bitmap;
    for (int y = 0; y < bh; y++) {
        memcpy(c->band + ((size_t)y * (size_t)c->dw + rc->left) * 3, px + (size_t)y * (size_t)bw * 3, (size_t)cw * 3);
    }
    if (bh > c->band_rows) {
        c->band_rows = bh;
    }
    return 1;
}

static int map_err(JRESULT r, bool io_err)
{
    if (io_err) {
        return FLEXJPG_ERR_IO;
    }
    switch (r) {
    case JDR_OK: return FLEXJPG_OK;
    case JDR_INTR: return FLEXJPG_ERR_ABORTED;
    case JDR_INP: return FLEXJPG_ERR_TRUNCATED;
    case JDR_MEM1:
    case JDR_MEM2: return FLEXJPG_ERR_MEMORY;
    case JDR_PAR: return FLEXJPG_ERR_ARG;
    case JDR_FMT1: return FLEXJPG_ERR_BADMARKER;
    case JDR_FMT2:
    case JDR_FMT3: return FLEXJPG_ERR_UNSUPPORTED;
    default: return FLEXJPG_ERR_HUFFMAN;
    }
}

static int ceil_div(int a, int d)
{
    return (a + d - 1) / d;
}

static int pick_default(int w, int h, int max_w, int max_h, uint32_t max_px)
{
    for (int d = 1; d <= 8; d <<= 1) {
        const int ow = ceil_div(w, d), oh = ceil_div(h, d);
        if ((max_w <= 0 || ow <= max_w) && (max_h <= 0 || oh <= max_h) &&
            (!max_px || (uint64_t)ow * (uint64_t)oh <= max_px)) {
            return d;
        }
    }
    return -1;
}

int flexJpegDecode888Stream(FlexJpegReadFn rd, void *rdCtx, int maxW, int maxH, uint32_t maxPixels,
                            FlexJpegScaleFn pick, FlexJpegInfo *info, FlexJpegRow888Cb cb, void *user,
                            FlexJpegAlloc af, FlexJpegFree ff)
{
    if (!rd || !cb) {
        return FLEXJPG_ERR_ARG;
    }
    if (!af) {
        af = malloc;
    }
    if (!ff) {
        ff = free;
    }
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    jfilt_t filt;
    memset(&filt, 0, sizeof(filt));
    filt.rd = rd;
    filt.ctx = rdCtx;
    jctx_t c;
    memset(&c, 0, sizeof(c));
    c.rd = filt_read;
    c.rd_ctx = &filt;
    c.cb = cb;
    c.user = user;
    c.band_top = -1;
    void *work = af(WORK_BYTES);
    if (!work) {
        return FLEXJPG_ERR_MEMORY;
    }
    JDEC jd;
    memset(&jd, 0, sizeof(jd));
    JRESULT jr = JD_PREPARE(&jd, in_cb, work, WORK_BYTES, &c);
    if (jr != JDR_OK) {
        if (info && jr == JDR_FMT3) {
            info->progressive = true;
        }
        ff(work);
        return map_err(jr, c.io_err);
    }
    const int w = (int)jd.width, h = (int)jd.height;
    if (info) {
        info->width = w;
        info->height = h;
        info->components = 3;
    }
    if (w <= 0 || h <= 0 || w > FLEXJPG_MAX_DIM || h > FLEXJPG_MAX_DIM) {
        ff(work);
        return FLEXJPG_ERR_TOOBIG;
    }
    const int d = pick ? pick(user, w, h) : pick_default(w, h, maxW, maxH, maxPixels);
    if (d <= 0 || (d != 1 && d != 2 && d != 4 && d != 8)) {
        ff(work);
        return pick ? FLEXJPG_ERR_ABORTED : FLEXJPG_ERR_TOOBIG;
    }
    const int scale = d == 8 ? 3 : d == 4 ? 2 : d == 2 ? 1 : 0;
#if DEC_SCALES
    c.post = 1;
    c.dw = ceil_div(w, d);
    c.dh = ceil_div(h, d);
#else
    c.post = d;
    c.dw = w;
    c.dh = h;
#endif
    c.ow = ceil_div(w, d);
    c.oh = ceil_div(h, d);
    if (info) {
        info->outWidth = c.ow;
        info->outHeight = c.oh;
        info->scaleDenom = d;
    }
    c.band = af((size_t)c.dw * BAND_MAX * 3);
    c.row = af((size_t)c.ow * 3);
    if (c.post > 1) {
        c.acc = af((size_t)c.ow * 3 * sizeof(uint32_t));
        if (c.acc) {
            memset(c.acc, 0, (size_t)c.ow * 3 * sizeof(uint32_t));
        }
    }
    int rc = FLEXJPG_ERR_MEMORY;
    if (c.band && c.row && (c.post == 1 || c.acc)) {
        memset(c.band, 0, (size_t)c.dw * BAND_MAX * 3);
#if DEC_SCALES
        jr = JD_DECOMP(&jd, out_cb, (uint8_t)scale);
#else
        (void)scale;
        jr = JD_DECOMP(&jd, out_cb, 0);
#endif
        if (jr == JDR_OK && !c.stop) {
            rc = flush_band(&c, true) ? FLEXJPG_OK : FLEXJPG_ERR_ABORTED;
        } else {
            rc = c.stop && !c.io_err ? FLEXJPG_ERR_ABORTED : map_err(jr, c.io_err);
        }
    }
    ff(c.acc);
    ff(c.row);
    ff(c.band);
    ff(work);
    return rc;
}

/* ---- cabecera sin decodificar ---- */
int flexJpegProbe(const uint8_t *data, size_t len, FlexJpegInfo *info)
{
    if (!data || len < 4 || !info) {
        return FLEXJPG_ERR_ARG;
    }
    memset(info, 0, sizeof(*info));
    if (data[0] != 0xFF || data[1] != 0xD8) {
        return FLEXJPG_ERR_BADMARKER;
    }
    size_t i = 2;
    while (i + 4 <= len) {
        if (data[i] != 0xFF) {
            return FLEXJPG_ERR_BADMARKER;
        }
        const uint8_t m = data[i + 1];
        if (m == 0xFF) {
            i++;
            continue;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;
            continue;
        }
        if (m == 0xD9 || m == 0xDA) {
            return FLEXJPG_ERR_BADMARKER; /* datos sin SOF */
        }
        const size_t seg = ((size_t)data[i + 2] << 8) | data[i + 3];
        if (seg < 2) {
            return FLEXJPG_ERR_BADMARKER;
        }
        const bool sof = m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
        if (sof) {
            if (i + 2 + 8 > len || seg < 8) {
                return FLEXJPG_ERR_TRUNCATED;
            }
            const uint8_t *s = data + i + 4;
            info->height = (s[1] << 8) | s[2];
            info->width = (s[3] << 8) | s[4];
            info->components = s[5];
            info->outWidth = info->width;
            info->outHeight = info->height;
            info->scaleDenom = 1;
            info->progressive = m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE;
            if (s[0] != 8 || (m != 0xC0 && m != 0xC1 && !info->progressive)) {
                return FLEXJPG_ERR_UNSUPPORTED; /* 12 bits, sin pérdidas o aritmético */
            }
            if (info->width <= 0 || info->height <= 0) {
                return FLEXJPG_ERR_BADMARKER;
            }
            return FLEXJPG_OK;
        }
        i += 2 + seg;
    }
    return FLEXJPG_ERR_TRUNCATED;
}

/* ---- en memoria, filas RGB565 ---- */
typedef struct {
    const uint8_t *p;
    size_t n, pos;
} mem_rd_t;

static int mem_read(void *ctx, uint8_t *buf, size_t n)
{
    mem_rd_t *m = ctx;
    const size_t left = m->n - m->pos;
    if (n > left) {
        n = left;
    }
    memcpy(buf, m->p + m->pos, n);
    m->pos += n;
    return (int)n;
}

typedef struct {
    FlexJpegRowCb cb;
    void *user;
    uint16_t *row;
    int cap;
} row565_t;

static bool row_to_565(void *u, int y, int w, const uint8_t *rgb)
{
    row565_t *r = u;
    if (w > r->cap) {
        w = r->cap;
    }
    for (int x = 0; x < w; x++) {
        const uint8_t *p = rgb + 3 * x;
        r->row[x] = (uint16_t)((p[0] & 0xF8) << 8 | (p[1] & 0xFC) << 3 | p[2] >> 3);
    }
    return r->cb(r->user, y, w, r->row);
}

int flexJpegDecode(const uint8_t *data, size_t len, int maxW, int maxH, uint32_t maxPixels, FlexJpegInfo *info,
                   FlexJpegRowCb cb, void *user, FlexJpegAlloc af, FlexJpegFree ff)
{
    if (!data || len < 4 || !cb) {
        return FLEXJPG_ERR_ARG;
    }
    if (!af) {
        af = malloc;
    }
    if (!ff) {
        ff = free;
    }
    FlexJpegInfo pi;
    int rc = flexJpegProbe(data, len, &pi);
    if (rc != FLEXJPG_OK) {
        if (info) {
            *info = pi;
        }
        return rc;
    }
    if (pi.progressive) {
        if (info) {
            *info = pi;
        }
        return FLEXJPG_ERR_UNSUPPORTED;
    }
    row565_t r = {.cb = cb, .user = user, .cap = pi.width};
    r.row = af((size_t)pi.width * sizeof(uint16_t));
    if (!r.row) {
        return FLEXJPG_ERR_MEMORY;
    }
    mem_rd_t m = {.p = data, .n = len};
    rc = flexJpegDecode888Stream(mem_read, &m, maxW, maxH, maxPixels, NULL, info, row_to_565, &r, af, ff);
    ff(r.row);
    return rc;
}

int flexJpegDecode888(const uint8_t *data, size_t len, int maxW, int maxH, uint32_t maxPixels, FlexJpegInfo *info,
                      FlexJpegRow888Cb cb, void *user, FlexJpegAlloc af, FlexJpegFree ff)
{
    if (!data || len < 4 || !cb) {
        return FLEXJPG_ERR_ARG;
    }
    FlexJpegInfo pi;
    const int rc = flexJpegProbe(data, len, &pi);
    if (rc != FLEXJPG_OK || pi.progressive) {
        if (info) {
            *info = pi;
        }
        return rc != FLEXJPG_OK ? rc : FLEXJPG_ERR_UNSUPPORTED;
    }
    mem_rd_t m = {.p = data, .n = len};
    return flexJpegDecode888Stream(mem_read, &m, maxW, maxH, maxPixels, NULL, info, cb, user, af, ff);
}

const char *flexJpegErrStr(int err)
{
    switch (err) {
    case FLEXJPG_OK: return "OK";
    case FLEXJPG_ERR_ARG: return "Parametros no validos";
    case FLEXJPG_ERR_TRUNCATED: return "JPEG incompleto";
    case FLEXJPG_ERR_BADMARKER: return "No es un JPEG valido";
    case FLEXJPG_ERR_UNSUPPORTED: return "JPEG no compatible (progresivo o similar)";
    case FLEXJPG_ERR_MEMORY: return "Memoria insuficiente";
    case FLEXJPG_ERR_TOOBIG: return "Imagen demasiado grande";
    case FLEXJPG_ERR_HUFFMAN: return "Datos JPEG danados";
    case FLEXJPG_ERR_ABORTED: return "Cancelado";
    case FLEXJPG_ERR_IO: return "Error de lectura";
    default: return "Error";
    }
}
