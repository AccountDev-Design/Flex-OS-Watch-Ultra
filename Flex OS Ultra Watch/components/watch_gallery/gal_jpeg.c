/*
 * gal_jpeg — ver include/gal_jpeg.h.
 */
#include "gal_jpeg.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM) && !defined(GAL_JPEG_LVGL)
/* TJpgDec R0.01 de la ROM del ESP32-S3: salida RGB888 en orden R, G, B
 * (como decode_image.c de los ejemplos de ESP-IDF). */
#include "rom/tjpgd.h"
#define ROM_TJPGD 1
JRESULT gal_rom_jd_prepare(JDEC *, UINT (*)(JDEC *, BYTE *, UINT), void *, UINT, void *);
JRESULT gal_rom_jd_decomp(JDEC *, UINT (*)(JDEC *, void *, JRECT *), BYTE);
#define JD_PREPARE gal_rom_jd_prepare
#define JD_DECOMP  gal_rom_jd_decomp
#define OFS_R 0
#define OFS_G 1
#define OFS_B 2
typedef UINT in_n_t;
typedef UINT out_ret_t;
#else
/* TJpgDec R0.03 de LVGL (LV_USE_TJPGD): sin escala, salida en orden B, G, R (flex_jpeg.c). */
#include "libs/tjpgd/tjpgd.h"
#define ROM_TJPGD 0
#define JD_PREPARE jd_prepare
#define JD_DECOMP  jd_decomp
#define OFS_R 2
#define OFS_G 1
#define OFS_B 0
typedef size_t in_n_t;
typedef int out_ret_t;
#endif

typedef struct {
    const gal_jpeg_src_t *src;
    bool in_err, cancelled;
    /* geometría (en coordenadas de origen YA escaladas por TJpgDec) */
    int sw, sh;             /* tamaño escalado */
    int cx, cy, cw, ch;     /* recorte de origen */
    int ox, oy, ow, oh;     /* destino de la imagen en el lienzo */
    uint16_t *dst;
    int dw, dh;
    bool up;                /* ampliación: cada origen rellena su rectángulo */
    int16_t *xmap;          /* columna de destino de cada columna de origen (-1 = fuera) */
    uint32_t *acc;          /* banda: GAL_JPEG_BAND filas x ow x {R, G, B, n} */
    int done;               /* filas de destino ya escritas [0, done) */
    int last_top;
} jctx_t;

static void *mem_alloc(const gal_jpeg_src_t *s, size_t n)
{
    return s->alloc ? s->alloc(n) : malloc(n);
}

static void mem_free(const gal_jpeg_src_t *s, void *p)
{
    if (!p) {
        return;
    }
    if (s->free) {
        s->free(p);
    } else {
        free(p);
    }
}

static in_n_t in_cb(JDEC *jd, uint8_t *buf, in_n_t n)
{
    jctx_t *c = jd->device;
    if (c->in_err) {
        return 0;
    }
    if (c->src->cancel && c->src->cancel(c->src->ud)) {
        c->cancelled = true;
        c->in_err = true;
        return 0;
    }
    in_n_t got = 0;
    uint8_t scratch[128];
    while (got < n) {
        const size_t want = buf ? n - got : (n - got < sizeof(scratch) ? n - got : sizeof(scratch));
        const int r = c->src->read(c->src->ud, buf ? buf + got : scratch, want);
        if (r <= 0) {
            if (r < 0) {
                c->in_err = true;
            }
            break;
        }
        got += (in_n_t)r;
    }
    return got;
}

/* Última fila de origen (en el recorte) que cae en la fila de destino dy. */
static int src_last_row(const jctx_t *c, int dy)
{
    return c->cy + (int)(((int64_t)(dy + 1) * c->ch + c->oh - 1) / c->oh) - 1;
}

static void flush_rows(jctx_t *c, int upto)
{
    const int bg_stride = c->ow * 4;
    for (; c->done < upto && c->done < c->oh; c->done++) {
        uint32_t *a = c->acc + (size_t)(c->done % GAL_JPEG_BAND) * (size_t)bg_stride;
        const int y = c->oy + c->done;
        if (y >= 0 && y < c->dh) {
            uint16_t *row = c->dst + (size_t)y * (size_t)c->dw;
            for (int x = 0; x < c->ow; x++) {
                const uint32_t n = a[4 * x + 3];
                const int X = c->ox + x;
                if (!n || X < 0 || X >= c->dw) {
                    continue;
                }
                const uint32_t r = (a[4 * x] + n / 2) / n, g = (a[4 * x + 1] + n / 2) / n, b = (a[4 * x + 2] + n / 2) / n;
                row[X] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
            }
        }
        memset(a, 0, (size_t)bg_stride * sizeof(uint32_t));
    }
}

static out_ret_t out_cb(JDEC *jd, void *bitmap, JRECT *r)
{
    jctx_t *c = jd->device;
    if (c->src->cancel && c->src->cancel(c->src->ud)) {
        c->cancelled = true;
        return 0;
    }
    const uint8_t *px = bitmap;
    const int bw = (int)r->right - (int)r->left + 1;
    if (c->up) {
        for (int sy = r->top; sy <= r->bottom; sy++) {
            if (sy < c->cy || sy >= c->cy + c->ch) {
                continue;
            }
            const int y0 = c->oy + (int)((int64_t)(sy - c->cy) * c->oh / c->ch);
            const int y1 = c->oy + (int)((int64_t)(sy - c->cy + 1) * c->oh / c->ch);
            const uint8_t *p = px + (size_t)(sy - r->top) * (size_t)bw * 3;
            for (int sx = r->left; sx <= r->right; sx++, p += 3) {
                if (sx < c->cx || sx >= c->cx + c->cw) {
                    continue;
                }
                const uint16_t v = (uint16_t)((p[OFS_R] & 0xF8) << 8 | (p[OFS_G] & 0xFC) << 3 | p[OFS_B] >> 3);
                const int x0 = c->ox + (int)((int64_t)(sx - c->cx) * c->ow / c->cw);
                const int x1 = c->ox + (int)((int64_t)(sx - c->cx + 1) * c->ow / c->cw);
                for (int y = y0 < 0 ? 0 : y0; y < y1 && y < c->dh; y++) {
                    uint16_t *row = c->dst + (size_t)y * (size_t)c->dw;
                    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < c->dw; x++) {
                        row[x] = v;
                    }
                }
            }
        }
        return 1;
    }
    /* Reducción: fila de bloques nueva => las filas de destino que ya no recibirán nada se escriben. */
    if ((int)r->top > c->last_top) {
        c->last_top = r->top;
        int upto = c->done;
        while (upto < c->oh && src_last_row(c, upto) < (int)r->top) {
            upto++;
        }
        flush_rows(c, upto);
    }
    for (int sy = r->top; sy <= r->bottom; sy++) {
        if (sy < c->cy || sy >= c->cy + c->ch) {
            continue;
        }
        const int dy = (int)((int64_t)(sy - c->cy) * c->oh / c->ch);
        if (dy < c->done || dy >= c->done + GAL_JPEG_BAND || dy >= c->oh) {
            continue; /* no debería pasar: la banda cubre de sobra un bloque */
        }
        uint32_t *a = c->acc + (size_t)(dy % GAL_JPEG_BAND) * (size_t)c->ow * 4;
        const uint8_t *p = px + (size_t)(sy - r->top) * (size_t)bw * 3;
        for (int sx = r->left; sx <= r->right; sx++, p += 3) {
            const int dx = sx < c->sw ? c->xmap[sx] : -1;
            if (dx < 0) {
                continue;
            }
            uint32_t *q = a + 4 * dx;
            q[0] += p[OFS_R];
            q[1] += p[OFS_G];
            q[2] += p[OFS_B];
            q[3]++;
        }
    }
    return 1;
}

void gal_jpeg_fit(int w, int h, int max_w, int max_h, int *ow, int *oh)
{
    if (w <= 0 || h <= 0 || max_w <= 0 || max_h <= 0) {
        *ow = *oh = 0;
        return;
    }
    if (w <= max_w && h <= max_h) {
        *ow = w;
        *oh = h;
        return;
    }
    if ((int64_t)w * max_h >= (int64_t)h * max_w) {
        *ow = max_w;
        *oh = (int)(((int64_t)h * max_w + w / 2) / w);
    } else {
        *oh = max_h;
        *ow = (int)(((int64_t)w * max_h + h / 2) / h);
    }
    if (*ow < 1) {
        *ow = 1;
    }
    if (*oh < 1) {
        *oh = 1;
    }
}

bool gal_jpeg_native_scale(void)
{
    return ROM_TJPGD;
}

static int map_err(JRESULT r, const jctx_t *c)
{
    if (c->cancelled) {
        return GAL_JPG_E_CANCEL;
    }
    if (c->in_err) {
        return GAL_JPG_E_INPUT;
    }
    switch (r) {
    case JDR_OK: return GAL_JPG_OK;
    case JDR_INP: return GAL_JPG_E_INPUT;
    case JDR_MEM1:
    case JDR_MEM2: return GAL_JPG_E_NOMEM;
    case JDR_FMT3: return GAL_JPG_E_PROGRESSIVE; /* SOF2 y demás: "Not supported JPEG standard" */
    default: return GAL_JPG_E_FORMAT;
    }
}

int gal_jpeg_decode(const gal_jpeg_src_t *src, gal_fit_t fit, gal_jpeg_dst_t dst, void *dst_ud, uint16_t bg,
                    gal_jpeg_info_t *info)
{
    gal_jpeg_info_t tmp;
    if (!info) {
        info = &tmp;
    }
    memset(info, 0, sizeof(*info));
    if (!src || !src->read || !dst) {
        return GAL_JPG_E_FORMAT;
    }
    jctx_t c;
    memset(&c, 0, sizeof(c));
    c.src = src;
    c.last_top = -1;
    void *work = mem_alloc(src, GAL_JPEG_WORK);
    if (!work) {
        return GAL_JPG_E_NOMEM;
    }
    JDEC jd;
    JRESULT r = JD_PREPARE(&jd, in_cb, work, GAL_JPEG_WORK, &c);
    info->tjpgd = (int)r;
    int ret = map_err(r, &c);
    if (ret == GAL_JPG_OK && (!jd.width || !jd.height)) {
        ret = GAL_JPG_E_FORMAT;
    }
    if (ret == GAL_JPG_OK && (uint64_t)jd.width * jd.height > GAL_JPEG_MAX_PIXELS) {
        ret = GAL_JPG_E_TOOBIG;
    }
    if (ret != GAL_JPG_OK) {
        mem_free(src, work);
        return ret;
    }
    info->src_w = (int)jd.width;
    info->src_h = (int)jd.height;
    c.dst = dst(dst_ud, info->src_w, info->src_h, &c.dw, &c.dh);
    if (!c.dst || c.dw <= 0 || c.dh <= 0) {
        mem_free(src, work);
        return GAL_JPG_E_SIZE;
    }
    /* Lo que hay que cubrir y con qué escala del decodificador (la mayor que no baje de eso). */
    int need_w, need_h;
    if (fit == GAL_FIT_COVER) {
        need_w = c.dw;
        need_h = c.dh;
        /* el recorte de la imagen con la proporción del lienzo */
        if ((int64_t)info->src_w * c.dh > (int64_t)info->src_h * c.dw) {
            need_w = (int)(((int64_t)c.dh * info->src_w + info->src_h - 1) / info->src_h);
        } else {
            need_h = (int)(((int64_t)c.dw * info->src_h + info->src_w - 1) / info->src_w);
        }
    } else {
        gal_jpeg_fit(info->src_w, info->src_h, c.dw, c.dh, &need_w, &need_h);
    }
    uint8_t scale = 0;
#if ROM_TJPGD
    while (scale < 3 && (info->src_w >> (scale + 1)) >= need_w && (info->src_h >> (scale + 1)) >= need_h) {
        scale++;
    }
#endif
    info->scale = scale;
    c.sw = (info->src_w + (1 << scale) - 1) >> scale;
    c.sh = (info->src_h + (1 << scale) - 1) >> scale;
    if (fit == GAL_FIT_COVER) {
        c.ow = c.dw;
        c.oh = c.dh;
        c.ox = c.oy = 0;
        if ((int64_t)c.sw * c.dh > (int64_t)c.sh * c.dw) { /* más ancha: se recortan los lados */
            c.ch = c.sh;
            c.cw = (int)(((int64_t)c.sh * c.dw + c.dh / 2) / c.dh);
            if (c.cw < 1) c.cw = 1;
            if (c.cw > c.sw) c.cw = c.sw;
            c.cx = (c.sw - c.cw) / 2;
        } else {
            c.cw = c.sw;
            c.ch = (int)(((int64_t)c.sw * c.dh + c.dw / 2) / c.dw);
            if (c.ch < 1) c.ch = 1;
            if (c.ch > c.sh) c.ch = c.sh;
            c.cy = (c.sh - c.ch) / 2;
        }
    } else {
        c.cx = c.cy = 0;
        c.cw = c.sw;
        c.ch = c.sh;
        gal_jpeg_fit(c.sw, c.sh, c.dw, c.dh, &c.ow, &c.oh);
        c.ox = (c.dw - c.ow) / 2;
        c.oy = (c.dh - c.oh) / 2;
        for (size_t i = 0; i < (size_t)c.dw * (size_t)c.dh; i++) {
            c.dst[i] = bg;
        }
    }
    info->x = c.ox;
    info->y = c.oy;
    info->w = c.ow;
    info->h = c.oh;
    c.up = c.ow > c.cw || c.oh > c.ch;
    if (!c.up) {
        c.xmap = mem_alloc(src, (size_t)c.sw * sizeof(int16_t));
        c.acc = mem_alloc(src, (size_t)GAL_JPEG_BAND * (size_t)c.ow * 4 * sizeof(uint32_t));
        if (!c.xmap || !c.acc) {
            mem_free(src, c.xmap);
            mem_free(src, c.acc);
            mem_free(src, work);
            return GAL_JPG_E_NOMEM;
        }
        memset(c.acc, 0, (size_t)GAL_JPEG_BAND * (size_t)c.ow * 4 * sizeof(uint32_t));
        for (int x = 0; x < c.sw; x++) {
            c.xmap[x] = x < c.cx || x >= c.cx + c.cw ? -1 : (int16_t)((int64_t)(x - c.cx) * c.ow / c.cw);
        }
    }
    r = JD_DECOMP(&jd, out_cb, scale);
    info->tjpgd = (int)r;
    ret = map_err(r, &c);
    if (ret == GAL_JPG_OK && !c.up) {
        flush_rows(&c, c.oh);
    }
    mem_free(src, c.xmap);
    mem_free(src, c.acc);
    mem_free(src, work);
    return ret;
}

void gal_rgb565_shrink(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        const int y0 = (int)((int64_t)y * sh / dh), y1 = (int)((int64_t)(y + 1) * sh / dh);
        for (int x = 0; x < dw; x++) {
            const int x0 = (int)((int64_t)x * sw / dw), x1 = (int)((int64_t)(x + 1) * sw / dw);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < (y1 > y0 ? y1 : y0 + 1); yy++) {
                const uint16_t *row = src + (size_t)yy * (size_t)sw;
                for (int xx = x0; xx < (x1 > x0 ? x1 : x0 + 1); xx++) {
                    const uint16_t p = row[xx];
                    r += p >> 11;
                    g += (p >> 5) & 63;
                    b += p & 31;
                    n++;
                }
            }
            dst[(size_t)y * dw + x] = (uint16_t)(((r + n / 2) / n) << 11 | ((g + n / 2) / n) << 5 | ((b + n / 2) / n));
        }
    }
}

const char *gal_jpeg_err_text(int err)
{
    switch (err) {
    case GAL_JPG_OK: return "";
    case GAL_JPG_E_INPUT: return "La imagen no llegó entera";
    case GAL_JPG_E_FORMAT: return "No es un JPEG válido o está dañado";
    case GAL_JPG_E_PROGRESSIVE: return "JPEG progresivo: el reloj solo muestra JPEG baseline";
    case GAL_JPG_E_NOMEM: return "No hay memoria para decodificar la imagen";
    case GAL_JPG_E_TOOBIG: return "Imagen demasiado grande";
    case GAL_JPG_E_CANCEL: return "Cancelado";
    default: return "No se pudo abrir la imagen";
    }
}
