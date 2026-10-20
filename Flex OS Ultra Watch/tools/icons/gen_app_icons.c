/*
 * gen_app_icons — genera los iconos de las apps de Flex OS Watch Ultra a
 * partir del MISMO dibujo vectorial de Flex OS Ultra.
 *
 * Herramienta de PC (no es firmware). Uso:
 *   cc -O2 -std=c11 -o gen_app_icons gen_app_icons.c -lm
 *   ./gen_app_icons <carpeta_salida> [tamaño=104]
 *   ./gen_app_icons <carpeta_salida> 72 p4   (solo comparación: una muestra
 *       por píxel en el centro, como rasteriza el P4, sin antialiasing)
 *
 * ORIGEN. drawAppIcon() de la referencia (AccountDev-Design/Flex-OS-Ecosistema,
 * rama claude/flexos-ota-system-blq9g9-2csacx @ 5dbc949,
 * FlexOS_Ultra/FlexOS_Ultra_Icons.h:65-305), estilo "Plano" (gIconStyle = 0,
 * el que usa la Caja de aplicaciones, AppDrawer.h:651-656). Cada caso se
 * transcribe con las mismas figuras, proporciones y colores; solo cambia el
 * motor que pinta las primitivas.
 *
 * TAMAÑO. En el P4 los iconos miden 72 px (HOME_ICON_S y DRW_ICON_S) a ~217
 * ppi = 8,4 mm. El reloj tiene ~315 ppi: el mismo tamaño físico son 104 px.
 * El icono se dibuja en su espacio de DISEÑO de 72 px (las constantes fijas
 * en píxeles de la referencia, como un anillo de 2 px, conservan su
 * proporción) y se rasteriza a la salida con un factor 104/72.
 *
 * ANTIALIASING. Las primitivas del P4 (Gfx.h:534-700) pintan píxeles enteros.
 * Aquí cada primitiva es la MISMA figura en coordenadas continuas (convención
 * de la referencia: el píxel (x, y) es el punto (x, y)) y se muestrea 8x8
 * veces por píxel de salida; el resultado se promedia. Así el icono conserva
 * su geometría y no tiene dientes de sierra en el AMOLED. Equivalencias:
 *   fillRect(x,y,w,h)        -> rectángulo [x-0.5, x+w-0.5] x [y-0.5, y+h-0.5]
 *   fillRoundRect(...,r)     -> el mismo rectángulo con esquinas de radio r
 *   fillCircle(cx,cy,r)      -> disco de radio r (mismo área que el de píxeles)
 *   fillRing(cx,cy,rOut,t)   -> corona entre rOut-t y rOut
 *   strokeSeg(...,rad)       -> cápsula de radio rad+0.5 (grosor 2*rad+1 px)
 *   strokeSegAA(...,rad)     -> cápsula de radio rad (como su cobertura)
 *   fillTriangle(...)        -> triángulo con los mismos vértices
 *
 * FLEX PHONE. Su comentario original dice "dos arcos de enlace a la derecha
 * del aparato", pero arcStroke(..., 300, 60, ...) barre de 300 a 60 grados
 * pasando por 180 y dibuja dos "C" que abrazan el teléfono. Así se ve en el
 * P4 y así se conserva aquí: se reproduce el icono real, no el comentario.
 *
 * SALIDA (por icono): app_<nombre>.bin en RGB565A8 de LVGL 9 (plano RGB565
 * little-endian de w*h*2 bytes seguido del plano alfa de w*h bytes) y
 * <nombre>.rgba (RGBA de 8 bits, para la hoja de comparación).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DESIGN_S 72      /* tamaño de diseño de la referencia */
static int SS = 8;       /* muestras por píxel de salida y eje (1 con "p4") */
/* "round": base circular (cajón en panal del reloj, estilo Apple Watch / One
 * UI Watch). El glifo es el mismo; solo cambia el radio de iconBase. */
static int g_round;

/* Color de la referencia: rgb565(r, g, b) recibía bytes de 8 bits; aquí se
 * guardan los 8 bits (la cuantización a RGB565 se hace una vez al final). */
typedef uint32_t col_t;
#define rgb565(r, g, b) ((col_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

/* ---- Lienzo supermuestreado (premultiplicado, en flotante) ------------- */
static int g_out;           /* tamaño de salida en píxeles */
static int g_n;             /* muestras por lado = g_out * SS */
static float g_scale;       /* píxeles de salida por unidad de diseño */
static float *g_r, *g_g, *g_b, *g_a;

/* Punto de diseño de la muestra i (eje x o y). */
static inline float dpos(int i)
{
    return ((float)i + 0.5f) / ((float)SS * g_scale) - 0.5f;
}

/* Rango de muestras cuyo punto de diseño cae en [lo, hi]. */
static void srange(float lo, float hi, int *i0, int *i1)
{
    const float k = (float)SS * g_scale;
    int a = (int)floorf((lo + 0.5f) * k - 0.5f) - 1;
    int b = (int)ceilf((hi + 0.5f) * k - 0.5f) + 1;
    if (a < 0) {
        a = 0;
    }
    if (b > g_n - 1) {
        b = g_n - 1;
    }
    *i0 = a;
    *i1 = b;
}

static inline void blend(int sx, int sy, col_t c, float a)
{
    const size_t i = (size_t)sy * (size_t)g_n + (size_t)sx;
    const float ia = 1.0f - a;
    g_r[i] = (float)((c >> 16) & 0xFF) / 255.0f * a + g_r[i] * ia;
    g_g[i] = (float)((c >> 8) & 0xFF) / 255.0f * a + g_g[i] * ia;
    g_b[i] = (float)(c & 0xFF) / 255.0f * a + g_b[i] * ia;
    g_a[i] = a + g_a[i] * ia;
}

/* Recorrido genérico: pinta las muestras de la caja [x0,x1]x[y0,y1] (en
 * unidades de diseño) donde inside() es cierto. */
typedef int (*inside_fn)(float x, float y, const float *p);
static void fill_shape(float x0, float y0, float x1, float y1, inside_fn in, const float *p, col_t c, float a)
{
    int i0, i1, j0, j1;
    srange(x0, x1, &i0, &i1);
    srange(y0, y1, &j0, &j1);
    for (int sy = j0; sy <= j1; sy++) {
        const float y = dpos(sy);
        for (int sx = i0; sx <= i1; sx++) {
            if (in(dpos(sx), y, p)) {
                blend(sx, sy, c, a);
            }
        }
    }
}

/* ---- Figuras ------------------------------------------------------------ */
/* p: x0, y0, x1, y1, r (rectángulo continuo con esquinas de radio r) */
static int in_rrect(float x, float y, const float *p)
{
    if (x < p[0] || x >= p[2] || y < p[1] || y >= p[3]) {
        return 0;
    }
    const float r = p[4];
    if (r <= 0.0f) {
        return 1;
    }
    float cx = x, cy = y;
    if (x < p[0] + r) {
        cx = p[0] + r;
    } else if (x > p[2] - r) {
        cx = p[2] - r;
    }
    if (y < p[1] + r) {
        cy = p[1] + r;
    } else if (y > p[3] - r) {
        cy = p[3] - r;
    }
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

/* p: cx, cy, rin, rout (corona; rin < 0 = disco) */
static int in_ring(float x, float y, const float *p)
{
    const float dx = x - p[0], dy = y - p[1], d2 = dx * dx + dy * dy;
    return d2 <= p[3] * p[3] && (p[2] < 0.0f || d2 > p[2] * p[2]);
}

/* p: x0, y0, x1, y1, rad (cápsula) */
static int in_capsule(float x, float y, const float *p)
{
    const float sdx = p[2] - p[0], sdy = p[3] - p[1];
    const float l2 = sdx * sdx + sdy * sdy;
    float t = l2 > 0.0f ? ((x - p[0]) * sdx + (y - p[1]) * sdy) / l2 : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    const float qx = p[0] + t * sdx - x, qy = p[1] + t * sdy - y;
    return qx * qx + qy * qy <= p[4] * p[4];
}

/* p: x0, y0, x1, y1, x2, y2 (cerrado, como fillTriangle, Font.h:1970) */
static int in_tri(float x, float y, const float *p)
{
    const float e0 = (p[2] - p[0]) * (y - p[1]) - (p[3] - p[1]) * (x - p[0]);
    const float e1 = (p[4] - p[2]) * (y - p[3]) - (p[5] - p[3]) * (x - p[2]);
    const float e2 = (p[0] - p[4]) * (y - p[5]) - (p[1] - p[5]) * (x - p[4]);
    return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

/* ---- Primitivas con la firma de la referencia --------------------------- */
static void rrect_a(float x, float y, float w, float h, float r, col_t c, float a)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r < 0) {
        r = 0;
    }
    if (2 * r > w) {
        r = (float)((int)w / 2);
    }
    if (2 * r > h) {
        r = (float)((int)h / 2);
    }
    const float p[5] = {x - 0.5f, y - 0.5f, x + w - 0.5f, y + h - 0.5f, r};
    fill_shape(p[0], p[1], p[2], p[3], in_rrect, p, c, a);
}

static void fillRect(int x, int y, int w, int h, col_t c)
{
    rrect_a((float)x, (float)y, (float)w, (float)h, 0, c, 1.0f);
}
static void hLine(int x, int y, int w, col_t c)
{
    fillRect(x, y, w, 1, c);
}
static void vLine(int x, int y, int h, col_t c)
{
    fillRect(x, y, 1, h, c);
}
static void fillRoundRect(int x, int y, int w, int h, int r, col_t c)
{
    rrect_a((float)x, (float)y, (float)w, (float)h, (float)r, c, 1.0f);
}
static void fillRoundRectA(int x, int y, int w, int h, int r, col_t c, uint8_t a)
{
    rrect_a((float)x, (float)y, (float)w, (float)h, (float)r, c, (float)a / 255.0f);
}

static void disc_a(float cx, float cy, float r, col_t c, float a)
{
    const float p[4] = {cx, cy, -1.0f, r};
    fill_shape(cx - r, cy - r, cx + r, cy + r, in_ring, p, c, a);
}
static void fillCircle(int cx, int cy, int r, col_t c)
{
    if (r <= 0) {
        fillRect(cx, cy, 1, 1, c);
        return;
    }
    disc_a((float)cx, (float)cy, (float)r, c, 1.0f);
}
static void fillCircleA(int cx, int cy, int r, col_t c, uint8_t a)
{
    if (r <= 0) {
        rrect_a((float)cx, (float)cy, 1, 1, 0, c, (float)a / 255.0f);
        return;
    }
    disc_a((float)cx, (float)cy, (float)r, c, (float)a / 255.0f);
}
static void fillRing(int cx, int cy, int rOut, int t, col_t c)
{
    int rin = rOut - t;
    if (rin < 0) {
        rin = 0;
    }
    const float p[4] = {(float)cx, (float)cy, (float)rin, (float)rOut};
    fill_shape(cx - rOut, cy - rOut, cx + rOut, cy + rOut, in_ring, p, c, 1.0f);
}
static void capsule(float x0, float y0, float x1, float y1, float rad, col_t c)
{
    const float p[5] = {x0, y0, x1, y1, rad};
    fill_shape(fminf(x0, x1) - rad, fminf(y0, y1) - rad, fmaxf(x0, x1) + rad, fmaxf(y0, y1) + rad, in_capsule, p, c,
               1.0f);
}
/* Trazo de discos enteros (Gfx.h:657): grosor 2*rad+1 px. */
static void strokeSeg(float x0, float y0, float x1, float y1, int rad, col_t c)
{
    capsule(x0, y0, x1, y1, (float)rad + 0.5f, c);
}
/* Trazo antialias (Gfx.h:762): cobertura del 50 % a distancia rad. */
static void strokeSegAA(float x0, float y0, float x1, float y1, float rad, col_t c)
{
    capsule(x0, y0, x1, y1, rad, c);
}

static void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, col_t c)
{
    const float p[6] = {(float)x0, (float)y0, (float)x1, (float)y1, (float)x2, (float)y2};
    const float minx = fminf(p[0], fminf(p[2], p[4])), maxx = fmaxf(p[0], fmaxf(p[2], p[4]));
    const float miny = fminf(p[1], fminf(p[3], p[5])), maxy = fmaxf(p[1], fmaxf(p[3], p[5]));
    fill_shape(minx, miny, maxx, maxy, in_tri, p, c, 1.0f);
}

/* arcStroke (Icons.h:34): tramos de strokeSeg cada 8 grados. */
static void arcStroke(float cx, float cy, float r, float a0, float a1, int thick, col_t col)
{
    const float pr = 0.0174532925f;
    const int steps = (int)(fabsf(a1 - a0) / 8.0f) + 2;
    float pxp = 0, pyp = 0;
    int first = 1;
    int rad = thick / 2;
    if (rad < 1) {
        rad = 1;
    }
    for (int i = 0; i <= steps; i++) {
        const float a = (a0 + (a1 - a0) * i / steps) * pr;
        const float xx = cx + r * cosf(a), yy = cy + r * sinf(a);
        if (!first) {
            strokeSeg(pxp, pyp, xx, yy, rad, col);
        }
        pxp = xx;
        pyp = yy;
        first = 0;
    }
}

/* drawBigChar (Font.h:2035), solo el '1' que usa el icono del calendario. */
static void drawBigChar1(int ox, int oy, int capH, int thick, col_t col)
{
    const float DW = capH * 0.60f, DH = (float)capH;
    const float m = thick * 0.5f;
    const float T = oy + m, B = oy + DH - m;
    const float MX = ox + DW * 0.5f;
    int rad = thick / 2;
    if (rad < 1) {
        rad = 1;
    }
    const float sx = MX + DW * 0.06f;
    strokeSegAA(sx, T, sx, B, (float)rad, col);
    strokeSegAA(MX - DW * 0.26f, T + DH * 0.16f, sx, T, (float)rad, col);
    strokeSegAA(MX - DW * 0.30f, B, MX + DW * 0.34f, B, (float)rad, col);
}

/* ---- Iconos: transcripción de drawAppIcon (Icons.h:65-305) -------------- */
enum {
    IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT, IC_JUEGOS,
    IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE, IC_MUSICA,
    /* Solo en el reloj (no existe en APP_REG): diseño nuevo con el lenguaje de
     * la referencia (base de iconBase + glifo blanco con sus primitivas). */
    IC_W_SENSORES = 100
};

/* iconBase, estilo "Plano" (Icons.h:77-86) */
static void iconBase(int x, int y, int S, col_t bg, int rf100)
{
    if (g_round) {
        /* Disco del mismo tamaño y brillo en su mitad superior (el rectángulo
         * redondeado de media altura dejaba esquinas fuera del círculo). */
        const float c0x = (float)x + S / 2.0f - 0.5f, c0y = (float)y + S / 2.0f - 0.5f;
        const float p[4] = {c0x, c0y, -1.0f, S / 2.0f};
        fill_shape(c0x - p[3], c0y - p[3], c0x + p[3], c0y + p[3], in_ring, p, bg, 1.0f);
        fill_shape(c0x - p[3], c0y - p[3], c0x + p[3], c0y, in_ring, p, rgb565(255, 255, 255), 22.0f / 255.0f);
        return;
    }
    const int r = S * rf100 / 100;
    fillRoundRect(x, y, S, S, r, bg);
    fillRoundRectA(x, y, S, S / 2, r, rgb565(255, 255, 255), 22); /* sutil brillo superior */
}

static void drawAppIcon(int id, int x, int y, int S)
{
    const int cx = x + S / 2, cy = y + S / 2;
    int tk = S / 12;
    if (tk < 2) {
        tk = 2;
    }
    const col_t WHITE = rgb565(255, 255, 255);
    switch (id) {
    case IC_RELOJ: {
        iconBase(x, y, S, rgb565(245, 245, 247), 22);
        fillRing(cx, cy, (int)(S * 0.36f), 2, rgb565(70, 70, 74));
        fillRect(cx - 1, y + (int)(S * 0.16f), 2, S / 12, rgb565(70, 70, 74));
        fillRect(cx - 1, y + S - (int)(S * 0.16f) - S / 12, 2, S / 12, rgb565(70, 70, 74));
        fillRect(x + (int)(S * 0.16f), cy - 1, S / 12, 2, rgb565(70, 70, 74));
        fillRect(x + S - (int)(S * 0.16f) - S / 12, cy - 1, S / 12, 2, rgb565(70, 70, 74));
        strokeSeg(cx, cy, cx - S * 0.14f, cy - S * 0.10f, tk / 2 + 1, rgb565(30, 30, 30));
        strokeSeg(cx, cy, cx + S * 0.12f, cy - S * 0.20f, tk / 2, rgb565(245, 140, 30));
        fillCircle(cx, cy, tk / 2 + 1, rgb565(30, 30, 30));
    } break;
    case IC_GALERIA: {
        iconBase(x, y, S, WHITE, 22);
        const col_t cols[8] = {rgb565(233, 64, 64),  rgb565(240, 150, 40), rgb565(240, 210, 50), rgb565(90, 200, 90),
                               rgb565(50, 190, 190), rgb565(60, 120, 235), rgb565(120, 80, 220), rgb565(220, 80, 200)};
        const float d = S * 0.17f, pr = S * 0.135f;
        for (int k = 0; k < 8; k++) {
            const float a = k * 45 * 0.0174532925f;
            fillCircle((int)(cx + d * cosf(a)), (int)(cy + d * sinf(a)), (int)pr, cols[k]);
        }
        fillCircle(cx, cy, (int)(S * 0.11f), WHITE);
    } break;
    case IC_MULTIMEDIA: {
        iconBase(x, y, S, rgb565(27, 95, 217), 22);
        fillTriangle(cx - (int)(S * 0.12f), cy - (int)(S * 0.18f), cx - (int)(S * 0.12f), cy + (int)(S * 0.18f),
                     cx + (int)(S * 0.22f), cy, WHITE);
    } break;
    case IC_ALMACEN: {
        iconBase(x, y, S, rgb565(59, 123, 217), 22);
        const col_t fol = rgb565(225, 236, 250);
        fillRoundRect(x + (int)(S * 0.20f), y + (int)(S * 0.28f), (int)(S * 0.30f), (int)(S * 0.12f), 3, fol);
        fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.36f), (int)(S * 0.64f), (int)(S * 0.34f), 5, fol);
        const col_t cl = rgb565(205, 222, 245);
        fillCircle(x + (int)(S * 0.60f), y + (int)(S * 0.34f), (int)(S * 0.10f), cl);
        fillCircle(x + (int)(S * 0.72f), y + (int)(S * 0.36f), (int)(S * 0.08f), cl);
        fillRect(x + (int)(S * 0.58f), y + (int)(S * 0.36f), (int)(S * 0.18f), (int)(S * 0.07f), cl);
    } break;
    case IC_NOTAS: {
        iconBase(x, y, S, rgb565(232, 167, 90), 22);
        fillRoundRect(x + (int)(S * 0.22f), y + (int)(S * 0.18f), (int)(S * 0.48f), (int)(S * 0.62f), 5, WHITE);
        for (int i = 0; i < 3; i++) {
            fillRect(x + (int)(S * 0.30f), y + (int)(S * 0.30f) + i * (int)(S * 0.12f), (int)(S * 0.32f), 2,
                     rgb565(180, 180, 185));
        }
        strokeSeg(x + S * 0.56f, y + S * 0.66f, x + S * 0.78f, y + S * 0.40f, tk / 2 + 1, rgb565(120, 90, 40));
        fillCircle((int)(x + S * 0.78f), (int)(y + S * 0.40f), tk / 2 + 1, rgb565(245, 210, 90));
    } break;
    case IC_NAV: {
        iconBase(x, y, S, rgb565(46, 155, 230), 22);
        fillRing(cx, cy, (int)(S * 0.30f), 2, WHITE);
        vLine(cx, cy - (int)(S * 0.30f), (int)(S * 0.60f), WHITE);
        hLine(cx - (int)(S * 0.30f), cy, (int)(S * 0.60f), WHITE);
        arcStroke(cx, cy, S * 0.18f, 90, 270, 2, WHITE);
        arcStroke(cx, cy, S * 0.18f, -90, 90, 2, WHITE);
    } break;
    case IC_PAINT: {
        iconBase(x, y, S, rgb565(241, 231, 210), 22);
        fillCircle(cx - (int)(S * 0.04f), cy + (int)(S * 0.02f), (int)(S * 0.27f), rgb565(236, 226, 205));
        fillCircle(cx + (int)(S * 0.11f), cy + (int)(S * 0.11f), (int)(S * 0.06f), rgb565(241, 231, 210));
        fillCircle(cx - (int)(S * 0.14f), cy - (int)(S * 0.06f), (int)(S * 0.045f), rgb565(230, 70, 70));
        fillCircle(cx - (int)(S * 0.02f), cy - (int)(S * 0.13f), (int)(S * 0.045f), rgb565(240, 200, 60));
        fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.07f), (int)(S * 0.045f), rgb565(70, 130, 235));
        fillCircle(cx - (int)(S * 0.16f), cy + (int)(S * 0.09f), (int)(S * 0.045f), rgb565(80, 190, 90));
        strokeSeg(cx + S * 0.02f, cy - S * 0.16f, cx + S * 0.26f, cy - S * 0.30f, tk / 2 + 1, rgb565(140, 100, 60));
    } break;
    case IC_AJUSTES: {
        iconBase(x, y, S, rgb565(138, 143, 152), 22);
        const col_t g = rgb565(70, 74, 84);
        fillCircle(cx, cy, (int)(S * 0.26f), g);
        for (int k = 0; k < 8; k++) {
            const float a = k * 45 * 0.0174532925f;
            fillCircle((int)(cx + S * 0.30f * cosf(a)), (int)(cy + S * 0.30f * sinf(a)), (int)(S * 0.075f), g);
        }
        fillCircle(cx, cy, (int)(S * 0.10f), rgb565(138, 143, 152));
    } break;
    case IC_CALC: {
        iconBase(x, y, S, rgb565(58, 58, 60), 22);
        fillRoundRect(x + (int)(S * 0.18f), y + (int)(S * 0.16f), (int)(S * 0.64f), (int)(S * 0.16f), 3,
                      rgb565(210, 210, 215));
        for (int rr = 0; rr < 3; rr++) {
            for (int cc = 0; cc < 4; cc++) {
                const col_t bc = (cc == 3) ? rgb565(245, 150, 30) : rgb565(150, 150, 155);
                fillRoundRect(x + (int)(S * 0.18f) + cc * (int)(S * 0.17f), y + (int)(S * 0.40f) + rr * (int)(S * 0.15f),
                              (int)(S * 0.12f), (int)(S * 0.10f), 2, bc);
            }
        }
    } break;
    case IC_CALEND: {
        iconBase(x, y, S, WHITE, 22);
        fillRect(x + (int)(S * 0.16f), y + (int)(S * 0.18f), (int)(S * 0.68f), (int)(S * 0.16f), rgb565(232, 70, 70));
        drawBigChar1(cx - (int)(S * 0.60f * 0.30f), y + (int)(S * 0.36f), (int)(S * 0.44f), 3, rgb565(60, 60, 64));
    } break;
    case IC_CLIMA: {
        iconBase(x, y, S, rgb565(48, 124, 226), 22);
        fillCircleA(cx + (int)(S * 0.10f), cy - (int)(S * 0.13f), (int)(S * 0.26f), rgb565(255, 214, 96), 70);
        fillCircle(cx + (int)(S * 0.10f), cy - (int)(S * 0.13f), (int)(S * 0.18f), rgb565(255, 206, 72));
        {
            int r = (int)(S * 0.13f);
            if (r < 2) {
                r = 2;
            }
            const int ccx = cx - (int)(S * 0.05f), ccy = cy + (int)(S * 0.12f);
            fillCircle(ccx - r, ccy, r, WHITE);
            fillCircle(ccx + r, ccy, (r * 4) / 5, WHITE);
            fillCircle(ccx, ccy - (r * 3) / 4, (r * 6) / 5, WHITE);
            fillRect(ccx - r - r / 2, ccy, (int)(S * 0.40f), r + 1, WHITE);
            fillCircle(ccx - r - r / 2, ccy, r / 2 + 1, WHITE);
        }
    } break;
    case IC_FLEXSTORE: {
        iconBase(x, y, S, rgb565(105, 91, 230), 22);
        const int bx = x + (int)(S * 0.22f), by = y + (int)(S * 0.34f);
        const int bw = (int)(S * 0.56f), bh = (int)(S * 0.46f);
        fillRoundRect(bx, by, bw, bh, (int)(S * 0.09f), WHITE);
        strokeSeg(cx - (int)(S * 0.16f), by + 2, cx - (int)(S * 0.10f), y + (int)(S * 0.22f), 2, WHITE);
        strokeSeg(cx - (int)(S * 0.10f), y + (int)(S * 0.22f), cx + (int)(S * 0.10f), y + (int)(S * 0.22f), 2, WHITE);
        strokeSeg(cx + (int)(S * 0.10f), y + (int)(S * 0.22f), cx + (int)(S * 0.16f), by + 2, 2, WHITE);
        fillRoundRect(cx - (int)(S * 0.16f), by + (int)(S * 0.13f), (int)(S * 0.32f), (int)(S * 0.09f),
                      (int)(S * 0.04f), rgb565(105, 91, 230));
    } break;
    case IC_FLEXPHONE: {
        iconBase(x, y, S, rgb565(38, 132, 255), 22);
        const int pw = (int)(S * 0.34f), ph = (int)(S * 0.54f);
        const int px0 = cx - pw / 2, py0 = y + (int)(S * 0.23f);
        fillRoundRect(px0, py0, pw, ph, (int)(S * 0.07f), WHITE);
        fillRoundRect(px0 + (int)(S * 0.04f), py0 + (int)(S * 0.07f), pw - (int)(S * 0.08f), ph - (int)(S * 0.16f),
                      (int)(S * 0.03f), rgb565(38, 132, 255));
        fillRoundRect(cx - (int)(S * 0.05f), py0 + ph - (int)(S * 0.07f), (int)(S * 0.10f), (int)(S * 0.03f), 1, WHITE);
        arcStroke(px0 + pw, cy, (int)(S * 0.16f), 300, 60, 2, WHITE);
        arcStroke(px0 + pw, cy, (int)(S * 0.26f), 310, 50, 2, WHITE);
    } break;
    case IC_DEVCARE: {
        iconBase(x, y, S, rgb565(20, 132, 122), 22);
        const float sw = S * 0.30f, sh = S * 0.34f;
        fillRoundRect((int)(cx - sw), (int)(cy - sh), (int)(sw * 2), (int)(sh * 1.35f), (int)(S * 0.07f), WHITE);
        fillTriangle((int)(cx - sw), (int)(cy + sh * 0.30f), (int)(cx + sw), (int)(cy + sh * 0.30f), cx,
                     (int)(cy + sh * 1.05f), WHITE);
        const col_t ac = rgb565(20, 132, 122);
        const float px0 = cx - sw * 0.78f, py0 = cy - sh * 0.10f, st = sw * 0.39f;
        strokeSeg(px0, py0, px0 + st, py0, 2, ac);
        strokeSeg(px0 + st, py0, px0 + st * 1.5f, py0 - sh * 0.44f, 2, ac);
        strokeSeg(px0 + st * 1.5f, py0 - sh * 0.44f, px0 + st * 2.1f, py0 + sh * 0.40f, 2, ac);
        strokeSeg(px0 + st * 2.1f, py0 + sh * 0.40f, px0 + st * 2.6f, py0, 2, ac);
        strokeSeg(px0 + st * 2.6f, py0, px0 + st * 4.0f, py0, 2, ac);
    } break;
    case IC_MUSICA: {
        iconBase(x, y, S, rgb565(236, 76, 108), 22);
        int hr = (int)(S * 0.085f);
        if (hr < 2) {
            hr = 2;
        }
        const int st = S / 16 + 1;
        const int x1 = x + (int)(S * 0.36f), x2 = x + (int)(S * 0.66f);
        const int yb1 = y + (int)(S * 0.70f), yb2 = y + (int)(S * 0.64f);
        const int yt1 = y + (int)(S * 0.28f), yt2 = y + (int)(S * 0.22f);
        const int bh = (int)(S * 0.09f) + 1;
        fillCircle(x1 - hr + st, yb1, hr, WHITE);
        fillCircle(x2 - hr + st, yb2, hr, WHITE);
        fillRect(x1, yt1, st, yb1 - yt1, WHITE);
        fillRect(x2, yt2, st, yb2 - yt2, WHITE);
        fillTriangle(x1, yt1, x2 + st, yt2, x2 + st, yt2 + bh, WHITE);
        fillTriangle(x1, yt1, x2 + st, yt2 + bh, x1, yt1 + bh, WHITE);
    } break;
    case IC_BRUJULA: {
        iconBase(x, y, S, rgb565(16, 42, 96), 22);
        fillCircle(cx, cy, (int)(S * 0.34f), rgb565(246, 248, 252));
        fillRing(cx, cy, (int)(S * 0.34f), (int)(S * 0.05f) + 1, rgb565(30, 66, 138));
        for (int k = 0; k < 4; k++) {
            const float a = k * 90.0f * 0.0174532925f;
            const float r0 = S * 0.29f, r1 = S * 0.22f;
            strokeSeg(cx + r0 * sinf(a), cy - r0 * cosf(a), cx + r1 * sinf(a), cy - r1 * cosf(a), (S >= 56 ? 1 : 0),
                      rgb565(96, 116, 150));
        }
        {
            const float L = S * 0.24f, Wd = S * 0.085f;
            fillTriangle(cx, cy - (int)L, cx - (int)Wd, cy, cx + (int)Wd, cy, rgb565(226, 62, 62));
            fillTriangle(cx, cy + (int)L, cx - (int)Wd, cy, cx + (int)Wd, cy, rgb565(108, 116, 132));
        }
        fillCircle(cx, cy, (int)(S * 0.055f) + 1, rgb565(30, 40, 58));
    } break;
    case IC_W_SENSORES: {
        /* Sensores (QMI8658): punto central con dos ondas a cada lado, como
         * el glifo "movimiento" del reloj, sobre verde. */
        iconBase(x, y, S, rgb565(46, 160, 92), 22);
        fillCircle(cx, cy, (int)(S * 0.07f), WHITE);
        arcStroke(cx, cy, S * 0.17f, 140, 220, 3, WHITE);
        arcStroke(cx, cy, S * 0.17f, -40, 40, 3, WHITE);
        arcStroke(cx, cy, S * 0.29f, 145, 215, 3, WHITE);
        arcStroke(cx, cy, S * 0.29f, -35, 35, 3, WHITE);
    } break;
    default:
        break; /* Modo PC, Juegos y Cámara: excluidos en el reloj (docs/INVENTARIO-APPS.md) */
    }
}

/* ---- Salida ------------------------------------------------------------- */
static const struct {
    int id;
    const char *name;
} kIcons[] = {
    {IC_RELOJ, "app_reloj"},         {IC_ALMACEN, "app_almacen"},     {IC_NOTAS, "app_notas"},
    {IC_NAV, "app_nav"},             {IC_BRUJULA, "app_brujula"},     {IC_PAINT, "app_paint"},
    {IC_AJUSTES, "app_ajustes"},     {IC_CALC, "app_calc"},           {IC_CALEND, "app_calend"},
    {IC_CLIMA, "app_clima"},         {IC_FLEXSTORE, "app_flexstore"}, {IC_FLEXPHONE, "app_flexphone"},
    {IC_DEVCARE, "app_devcare"},     {IC_MUSICA, "app_musica"},       {IC_W_SENSORES, "app_sensores"},
};

static int write_file(const char *dir, const char *name, const char *ext, const void *data, size_t n)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.%s", dir, name, ext);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, n, f) != n) {
        fprintf(stderr, "no se pudo escribir %s\n", path);
        if (f) {
            fclose(f);
        }
        return -1;
    }
    fclose(f);
    return 0;
}

/* Cuantiza a 0..255 con redondeo. */
static uint8_t q8(float v)
{
    if (v <= 0.0f) {
        return 0;
    }
    if (v >= 1.0f) {
        return 255;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s <carpeta_salida> [tamaño=104] [p4|round]\n", argv[0]);
        return 2;
    }
    g_out = argc > 2 ? atoi(argv[2]) : 104;
    if (argc > 3 && strcmp(argv[3], "p4") == 0) {
        SS = 1;
    }
    if (argc > 3 && strcmp(argv[3], "round") == 0) {
        g_round = 1;
    }
    if (g_out < 16 || g_out > 256) {
        fprintf(stderr, "tamaño fuera de rango (16..256)\n");
        return 2;
    }
    g_n = g_out * SS;
    g_scale = (float)g_out / (float)DESIGN_S;
    const size_t ns = (size_t)g_n * (size_t)g_n;
    g_r = malloc(ns * sizeof(float));
    g_g = malloc(ns * sizeof(float));
    g_b = malloc(ns * sizeof(float));
    g_a = malloc(ns * sizeof(float));
    const size_t np = (size_t)g_out * (size_t)g_out;
    uint8_t *rgba = malloc(np * 4);
    uint8_t *bin = malloc(np * 3);
    if (!g_r || !g_g || !g_b || !g_a || !rgba || !bin) {
        fprintf(stderr, "sin memoria\n");
        return 1;
    }
    for (size_t k = 0; k < sizeof(kIcons) / sizeof(kIcons[0]); k++) {
        memset(g_r, 0, ns * sizeof(float));
        memset(g_g, 0, ns * sizeof(float));
        memset(g_b, 0, ns * sizeof(float));
        memset(g_a, 0, ns * sizeof(float));
        drawAppIcon(kIcons[k].id, 0, 0, DESIGN_S);

        /* Promedio de SS x SS muestras (premultiplicado) por píxel. */
        for (int y = 0; y < g_out; y++) {
            for (int x = 0; x < g_out; x++) {
                float r = 0, g = 0, b = 0, a = 0;
                for (int j = 0; j < SS; j++) {
                    const size_t row = (size_t)(y * SS + j) * (size_t)g_n + (size_t)x * SS;
                    for (int i = 0; i < SS; i++) {
                        r += g_r[row + i];
                        g += g_g[row + i];
                        b += g_b[row + i];
                        a += g_a[row + i];
                    }
                }
                const float inv = 1.0f / (SS * SS);
                r *= inv;
                g *= inv;
                b *= inv;
                a *= inv;
                const size_t p = (size_t)y * g_out + x;
                const uint8_t a8 = q8(a);
                /* color sin premultiplicar (lo que esperan RGB565A8 y el PNG) */
                const uint8_t r8 = a > 0 ? q8(r / a) : 0, g8 = a > 0 ? q8(g / a) : 0, b8 = a > 0 ? q8(b / a) : 0;
                rgba[p * 4 + 0] = r8;
                rgba[p * 4 + 1] = g8;
                rgba[p * 4 + 2] = b8;
                rgba[p * 4 + 3] = a8;
                const uint16_t c565 = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));
                bin[p * 2 + 0] = (uint8_t)(c565 & 0xFF);
                bin[p * 2 + 1] = (uint8_t)(c565 >> 8);
                bin[np * 2 + p] = a8;
            }
        }
        if (write_file(argv[1], kIcons[k].name, "bin", bin, np * 3) || write_file(argv[1], kIcons[k].name, "rgba", rgba, np * 4)) {
            return 1;
        }
        printf("%-10s %dx%d  %zu B\n", kIcons[k].name, g_out, g_out, np * 3);
    }
    return 0;
}
