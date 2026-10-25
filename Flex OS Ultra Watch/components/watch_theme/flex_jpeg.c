/*
 * flex_jpeg — ver include/flex_jpeg.h. Hace lo de brDecodeInto de la
 * referencia (FlexOS_BrowserApp.cpp:1359-1422) para el navegador; antes era
 * watch_ui/ui_jpeg.c y se movió aquí para que también lo usen los fondos de
 * las esferas. Se llama desde la tarea de LVGL.
 */
#include "flex_jpeg.h"

#include <stdlib.h>
#include <string.h>
#include "libs/tjpgd/tjpgd.h"

#define WORK_SZ 8192

typedef struct {
    const uint8_t *src;
    uint32_t n, off;
    uint16_t *dst;
    int cw, ch;         /* lienzo */
    int dx, dy, dw, dh; /* destino */
    int sw, sh;         /* tamaño real de la imagen */
} jctx_t;

static size_t in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    jctx_t *c = jd->device;
    const uint32_t left = c->n - c->off;
    if (n > left) {
        n = left;
    }
    if (buf) {
        memcpy(buf, c->src + c->off, n);
    }
    c->off += (uint32_t)n;
    return n;
}

static int out_cb(JDEC *jd, void *bitmap, JRECT *r)
{
    jctx_t *c = jd->device;
    const uint8_t *px = bitmap; /* JD_FORMAT 0 de LVGL: bytes B, G, R (su RGB888 nativo) */
    const int bw = r->right - r->left + 1;
    for (int sy = r->top; sy <= r->bottom; sy++) {
        /* filas de destino que cubre esta fila de origen */
        const int y0 = c->dy + sy * c->dh / c->sh, y1 = c->dy + (sy + 1) * c->dh / c->sh;
        for (int y = y0 < 0 ? 0 : y0; y < y1 && y < c->ch; y++) {
            uint16_t *row = c->dst + (size_t)y * (size_t)c->cw;
            const uint8_t *p = px + (size_t)(sy - r->top) * (size_t)bw * 3;
            for (int sx = r->left; sx <= r->right; sx++, p += 3) {
                const uint16_t v = (uint16_t)((p[2] & 0xF8) << 8 | (p[1] & 0xFC) << 3 | p[0] >> 3);
                const int x0 = c->dx + sx * c->dw / c->sw, x1 = c->dx + (sx + 1) * c->dw / c->sw;
                for (int x = x0 < 0 ? 0 : x0; x < x1 && x < c->cw; x++) {
                    row[x] = v;
                }
            }
        }
    }
    return 1;
}

int flex_jpeg_draw(const uint8_t *jpg, uint32_t n, uint16_t *canvas, int cw, int ch, int dx, int dy, int dw, int dh)
{
    if (!jpg || n < 4 || !canvas || dw <= 0 || dh <= 0) {
        return -1;
    }
    void *work = malloc(WORK_SZ);
    if (!work) {
        return -2;
    }
    jctx_t c = {.src = jpg, .n = n, .dst = canvas, .cw = cw, .ch = ch, .dx = dx, .dy = dy, .dw = dw, .dh = dh};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, in_cb, work, WORK_SZ, &c);
    if (r == JDR_OK) {
        c.sw = jd.width;
        c.sh = jd.height;
        r = c.sw && c.sh ? jd_decomp(&jd, out_cb, 0) : JDR_FMT1;
    }
    free(work);
    return r == JDR_OK ? 0 : (int)r;
}
