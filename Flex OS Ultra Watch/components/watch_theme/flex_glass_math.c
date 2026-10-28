/*
 * flex_glass_math — ver include/flex_glass_math.h
 */
#include "flex_glass_math.h"
#include "flex_tokens.h"

#include <stddef.h>

int flex_glass_luma565(uint16_t c)
{
    const int r = (c >> 11) & 0x1F;
    const int g = (c >> 5) & 0x3F;
    const int b = c & 0x1F;
    return ((r + g) * 5 + b * 2) >> 1;
}

uint8_t flex_glass_tint_mix(uint32_t luma_sum, int n, uint16_t tint565)
{
    if (n <= 0) {
        return FLEX_GLASS_TINT_BASE;
    }
    int dif = (int)(luma_sum / (uint32_t)n) - flex_glass_luma565(tint565);
    if (dif < 0) {
        dif = -dif;
    }
    if (dif > FLEX_GLASS_TINT_DIFF_MAX) {
        dif = FLEX_GLASS_TINT_DIFF_MAX;
    }
    return (uint8_t)(FLEX_GLASS_TINT_MIN +
                     (dif * (FLEX_GLASS_TINT_MAX - FLEX_GLASS_TINT_MIN)) / FLEX_GLASS_TINT_DIFF_MAX);
}

void flex_glass_region_luma(const uint16_t *buf, int w, int h, int x, int y, int rw, int rh,
                            uint32_t *sum, int *n)
{
    *sum = 0;
    *n = 0;
    for (int j = y; j < y + rh; j += 4) {
        if (j < 0 || j >= h) {
            continue;
        }
        for (int i = x; i < x + rw; i += 4) {
            if (i < 0 || i >= w) {
                continue;
            }
            *sum += (uint32_t)flex_glass_luma565(buf[j * w + i]);
            (*n)++;
        }
    }
}

uint32_t flex_mix_rgb(uint32_t fg, uint32_t bg, uint8_t a)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const uint32_t f = (fg >> sh) & 0xFF, b = (bg >> sh) & 0xFF;
        out |= ((f * a + b * (255u - a) + 127u) / 255u) << sh;
    }
    return out;
}

/* ---- Recetas del Flex Liquid Glass Engine ---- */

#define WHITE 0xFFFFFFu
#define BLACK 0x000000u

static void body(flex_glass_recipe_t *r, uint32_t top_rgb, uint8_t top_opa, uint32_t bot_rgb, uint8_t bot_opa)
{
    r->top_rgb = top_rgb;
    r->top_opa = top_opa;
    r->bot_rgb = bot_rgb;
    r->bot_opa = bot_opa;
}

static void flat(flex_glass_recipe_t *r, uint32_t rgb, uint8_t opa)
{
    body(r, rgb, opa, rgb, opa);
}

static void rim(flex_glass_recipe_t *r, uint8_t opa)
{
    r->rim_rgb = WHITE;
    r->rim_opa = opa;
    r->rim_w = opa ? FLEX_GLASS_RIM_W : 0;
}

/* Estándar: el promedio de los dos extremos de la receta Premium, plano. */
static void flatten(flex_glass_recipe_t *r)
{
    const uint8_t opa = (uint8_t)(((unsigned)r->top_opa + r->bot_opa) / 2u);
    const uint32_t rgb = flex_mix_rgb(r->top_rgb, r->bot_rgb, 128);
    flat(r, rgb, opa);
    r->rim_opa = (uint8_t)(r->rim_opa * 85u / 100u);
}

/* Ligero: lo que se ve sobre el negro de la página, opaco. */
static void opaque(flex_glass_recipe_t *r)
{
    if (r->top_opa == 0 && r->bot_opa == 0) {
        return; /* transparente (fantasma en reposo): no se dibuja nada */
    }
    const uint32_t top = flex_mix_rgb(r->top_rgb, BLACK, r->top_opa);
    const uint32_t bot = flex_mix_rgb(r->bot_rgb, BLACK, r->bot_opa);
    const uint32_t mid = flex_mix_rgb(top, bot, 128);
    flat(r, mid, 255);
    if (r->rim_w) {
        r->rim_rgb = flex_mix_rgb(r->rim_rgb, mid, r->rim_opa);
        r->rim_opa = 255;
    }
}

static double lum(uint32_t rgb);

static void premium(flex_glass_role_t role, flex_glass_state_t st, uint32_t tint, flex_glass_recipe_t *r)
{
    const bool tinted = tint != FLEX_GLASS_TINT_NONE && (role == FLEX_GLASS_CONTROL || role == FLEX_GLASS_LENS);
    r->grad_end = FLEX_GLASS_SHEEN_END;
    r->text_rgb = st == FLEX_GLASS_ST_DISABLED ? FLEX_GLASS_TXT_OFF : FLEX_GLASS_TXT;
    if (tinted) {
        if (st != FLEX_GLASS_ST_DISABLED && lum(tint) > 0.40) {
            r->text_rgb = FLEX_GLASS_TXT_ON_LIGHT; /* blanco sobre amarillo o lima no se lee */
        }
        if (st == FLEX_GLASS_ST_PRESSED) {
            body(r, flex_mix_rgb(WHITE, tint, 80), 245, flex_mix_rgb(WHITE, tint, 30), 225);
            rim(r, 180);
        } else if (st == FLEX_GLASS_ST_DISABLED) {
            body(r, tint, FLEX_GLASS_TINT_TOP / 2, tint, FLEX_GLASS_TINT_BOT / 2);
            rim(r, FLEX_GLASS_TINT_RIM / FLEX_GLASS_OFF_DIV);
        } else {
            body(r, flex_mix_rgb(WHITE, tint, FLEX_GLASS_TINT_HI), FLEX_GLASS_TINT_TOP, tint, FLEX_GLASS_TINT_BOT);
            rim(r, FLEX_GLASS_TINT_RIM);
        }
        return;
    }
    switch (role) {
    case FLEX_GLASS_CONTROL:
    case FLEX_GLASS_GHOST:
        if (st == FLEX_GLASS_ST_PRESSED) {
            body(r, WHITE, FLEX_GLASS_CTL_PR_TOP, WHITE, FLEX_GLASS_CTL_PR_BOT);
            rim(r, FLEX_GLASS_CTL_PR_RIM);
        } else if (role == FLEX_GLASS_GHOST) {
            flat(r, WHITE, 0); /* vidrio solo al tocarlo */
            rim(r, 0);
        } else if (st == FLEX_GLASS_ST_DISABLED) {
            body(r, WHITE, FLEX_GLASS_CTL_TOP / FLEX_GLASS_OFF_DIV, WHITE, FLEX_GLASS_CTL_BOT / FLEX_GLASS_OFF_DIV);
            rim(r, FLEX_GLASS_CTL_RIM / FLEX_GLASS_OFF_DIV);
        } else {
            body(r, WHITE, FLEX_GLASS_CTL_TOP, WHITE, FLEX_GLASS_CTL_BOT);
            rim(r, FLEX_GLASS_CTL_RIM);
        }
        break;
    case FLEX_GLASS_BAR:
        body(r, WHITE, FLEX_GLASS_BAR_TOP, WHITE, FLEX_GLASS_BAR_BOT);
        rim(r, FLEX_GLASS_BAR_RIM);
        break;
    case FLEX_GLASS_LENS:
        body(r, WHITE, FLEX_GLASS_LENS_TOP, WHITE, FLEX_GLASS_LENS_BOT);
        rim(r, FLEX_GLASS_LENS_RIM);
        break;
    case FLEX_GLASS_SHEET:
        if (st == FLEX_GLASS_ST_PRESSED) {
            body(r, flex_mix_rgb(WHITE, FLEX_GLASS_SHEET_TOP_RGB, 24), FLEX_GLASS_SHEET_TOP,
                 flex_mix_rgb(WHITE, FLEX_GLASS_SHEET_BOT_RGB, 16), FLEX_GLASS_SHEET_BOT);
        } else {
            body(r, FLEX_GLASS_SHEET_TOP_RGB, FLEX_GLASS_SHEET_TOP, FLEX_GLASS_SHEET_BOT_RGB, FLEX_GLASS_SHEET_BOT);
        }
        rim(r, FLEX_GLASS_SHEET_RIM);
        break;
    case FLEX_GLASS_SCRIM:
        r->grad_end = 255;
        body(r, BLACK, FLEX_GLASS_SCRIM_TOP, BLACK, FLEX_GLASS_SCRIM_BOT);
        rim(r, 0);
        break;
    case FLEX_GLASS_AMBIENT:
        r->grad_end = 255;
        body(r, FLEX_GLASS_AMB_TOP_RGB, 255, FLEX_GLASS_AMB_BOT_RGB, 255);
        rim(r, 0);
        break;
    case FLEX_GLASS_CARD:
    case FLEX_GLASS_ROW:
    default:
        if (st == FLEX_GLASS_ST_PRESSED) {
            flat(r, WHITE, FLEX_GLASS_CARD_PR);
            rim(r, (uint8_t)((role == FLEX_GLASS_ROW ? FLEX_GLASS_ROW_RIM : FLEX_GLASS_CARD_RIM) * 2));
        } else if (st == FLEX_GLASS_ST_DISABLED) {
            body(r, WHITE, FLEX_GLASS_CARD_TOP / FLEX_GLASS_OFF_DIV, WHITE, FLEX_GLASS_CARD_BOT / FLEX_GLASS_OFF_DIV);
            rim(r, (role == FLEX_GLASS_ROW ? FLEX_GLASS_ROW_RIM : FLEX_GLASS_CARD_RIM) / FLEX_GLASS_OFF_DIV);
        } else {
            body(r, WHITE, FLEX_GLASS_CARD_TOP, WHITE, FLEX_GLASS_CARD_BOT);
            rim(r, role == FLEX_GLASS_ROW ? FLEX_GLASS_ROW_RIM : FLEX_GLASS_CARD_RIM);
        }
        break;
    }
}

void flex_glass_recipe(flex_glass_role_t role, flex_glass_profile_t p, flex_glass_state_t st, uint32_t tint,
                       flex_glass_recipe_t *out)
{
    flex_glass_recipe_t r = {0};
    premium(role, st, tint, &r);
    if (p == FLEX_GLASS_PREMIUM) {
        r.trans_ms = FLEX_GLASS_TRANS_PREMIUM;
    } else {
        if (role != FLEX_GLASS_AMBIENT) {
            flatten(&r); /* el ambiente conserva su degradado (opaco y barato) */
        }
        r.grad_end = 255;
        r.trans_ms = FLEX_GLASS_TRANS_STD;
        if (p == FLEX_GLASS_LITE) {
            r.trans_ms = 0;
            if (role == FLEX_GLASS_AMBIENT || role == FLEX_GLASS_SCRIM) {
                flat(&r, BLACK, 255); /* negro AMOLED: lo de debajo ni se dibuja */
            } else {
                opaque(&r);
            }
        }
    }
    *out = r;
}

bool flex_glass_recipe_translucent(const flex_glass_recipe_t *r)
{
    return r->top_opa < 255 || r->bot_opa < 255;
}

uint32_t flex_glass_visible(const flex_glass_recipe_t *r, uint32_t bg, bool top)
{
    return top ? flex_mix_rgb(r->top_rgb, bg, r->top_opa) : flex_mix_rgb(r->bot_rgb, bg, r->bot_opa);
}

static double lin(unsigned c)
{
    const double v = c / 255.0;
    if (v <= 0.04045) {
        return v / 12.92;
    }
    /* ((v + 0,055) / 1,055) ^ 2,4 sin libm: x^2 por la raíz quinta de x^2 (Newton) */
    const double x = (v + 0.055) / 1.055;
    const double r = x * x;
    double y = x;
    for (int i = 0; i < 24; i++) {
        const double y4 = y * y * y * y;
        y = y - (y4 * y - x * x) / (5.0 * y4);
    }
    return r * y;
}

static double lum(uint32_t rgb)
{
    return 0.2126 * lin((rgb >> 16) & 0xFF) + 0.7152 * lin((rgb >> 8) & 0xFF) + 0.0722 * lin(rgb & 0xFF);
}

int flex_contrast_x100(uint32_t a, uint32_t b)
{
    double la = lum(a), lb = lum(b);
    if (la < lb) {
        const double t = la;
        la = lb;
        lb = t;
    }
    return (int)(100.0 * (la + 0.05) / (lb + 0.05) + 0.5);
}

/* ---- Perfil automático ---- */

void flex_glass_auto_init(flex_glass_auto_t *a)
{
    a->slow = 0;
    a->cap = FLEX_GLASS_PREMIUM;
    a->drops = 0;
}

bool flex_glass_auto_frame(flex_glass_auto_t *a, uint32_t ms, bool heavy)
{
    if (!heavy) {
        return false;
    }
    if (ms <= FLEX_GLASS_AUTO_FAST_MS) {
        a->slow = 0;
        return false;
    }
    if (ms <= FLEX_GLASS_AUTO_SLOW_MS) {
        return false; /* en la franja intermedia ni suma ni reinicia */
    }
    if (++a->slow < FLEX_GLASS_AUTO_SLOW_N) {
        return false;
    }
    a->slow = 0;
    if (a->cap > FLEX_GLASS_STANDARD) {
        a->cap = (flex_glass_profile_t)(a->cap - 1);
        a->drops++;
        return true;
    }
    return false;
}

flex_glass_profile_t flex_glass_effective(uint8_t setting, bool power_saving, const flex_glass_auto_t *a)
{
    if (power_saving) {
        return FLEX_GLASS_LITE;
    }
    switch (setting) {
    case 1:
        return FLEX_GLASS_PREMIUM;
    case 2:
        return FLEX_GLASS_STANDARD;
    case 3:
        return FLEX_GLASS_LITE;
    default:
        return a ? a->cap : FLEX_GLASS_PREMIUM;
    }
}

/* ---------------- desenfoque desde la fuente ---------------- */

static inline void unpack565(uint16_t c, int *r, int *g, int *b)
{
    *r = (c >> 11) & 0x1F;
    *g = (c >> 5) & 0x3F;
    *b = c & 0x1F;
}

void flex_blur_fit565(const uint16_t *src, int sw, int sh, int sstride, uint16_t *dst, int dw, int dh)
{
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sstride < sw) {
        return;
    }
    /* Zona de la fuente que se usa: la mayor con la proporción de dst, centrada. */
    int cw = sw, ch = sh;
    if ((int64_t)sw * dh > (int64_t)sh * dw) {
        cw = (int)((int64_t)sh * dw / dh);
    } else {
        ch = (int)((int64_t)sw * dh / dw);
    }
    if (cw < 1) {
        cw = 1;
    }
    if (ch < 1) {
        ch = 1;
    }
    const int ox = (sw - cw) / 2, oy = (sh - ch) / 2;
    for (int y = 0; y < dh; y++) {
        int y0 = oy + (int)((int64_t)y * ch / dh), y1 = oy + (int)((int64_t)(y + 1) * ch / dh);
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (int x = 0; x < dw; x++) {
            int x0 = ox + (int)((int64_t)x * cw / dw), x1 = ox + (int)((int64_t)(x + 1) * cw / dw);
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            uint32_t sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint16_t *row = src + (size_t)yy * sstride;
                for (int xx = x0; xx < x1; xx++) {
                    int r, g, b;
                    unpack565(row[xx], &r, &g, &b);
                    sr += (uint32_t)r;
                    sg += (uint32_t)g;
                    sb += (uint32_t)b;
                    n++;
                }
            }
            dst[(size_t)y * dw + x] = (uint16_t)(((sr + n / 2) / n) << 11 | ((sg + n / 2) / n) << 5 | ((sb + n / 2) / n));
        }
    }
}

int flex_blur_work_bytes(int w, int h, int radius)
{
    (void)radius;
    const int m = w > h ? w : h;
    return m > 0 ? m * 3 : 0;
}

/* Una pasada de caja sobre n muestras (separadas `step` en px), con los
 * extremos repetidos. line: 3*n bytes (los canales desempaquetados). */
static void box_line(uint16_t *px, int n, int step, int r, uint8_t *line)
{
    for (int i = 0; i < n; i++) {
        int cr, cg, cb;
        unpack565(px[(size_t)i * step], &cr, &cg, &cb);
        line[3 * i] = (uint8_t)cr;
        line[3 * i + 1] = (uint8_t)cg;
        line[3 * i + 2] = (uint8_t)cb;
    }
    const int win = 2 * r + 1;
    int sr = 0, sg = 0, sb = 0;
    for (int k = -r; k <= r; k++) {
        const int j = k < 0 ? 0 : (k >= n ? n - 1 : k);
        sr += line[3 * j];
        sg += line[3 * j + 1];
        sb += line[3 * j + 2];
    }
    for (int i = 0; i < n; i++) {
        const int vr = (sr + win / 2) / win, vg = (sg + win / 2) / win, vb = (sb + win / 2) / win;
        px[(size_t)i * step] = (uint16_t)(vr << 11 | vg << 5 | vb);
        const int out = i - r < 0 ? 0 : i - r;
        const int in = i + r + 1 >= n ? n - 1 : i + r + 1;
        sr += line[3 * in] - line[3 * out];
        sg += line[3 * in + 1] - line[3 * out + 1];
        sb += line[3 * in + 2] - line[3 * out + 2];
    }
}

bool flex_blur_box565(uint16_t *px, int w, int h, int radius, int passes, uint8_t *work)
{
    if (!px || !work || w <= 0 || h <= 0 || radius < 0 || passes < 0 || radius > 255) {
        return false;
    }
    if (radius == 0) {
        return true;
    }
    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < h; y++) {
            box_line(px + (size_t)y * w, w, 1, radius, work);
        }
        for (int x = 0; x < w; x++) {
            box_line(px + x, h, w, radius, work);
        }
    }
    return true;
}

void flex_blur_tone565(uint16_t *px, int n, uint8_t mul, uint8_t desat)
{
    for (int i = 0; i < n; i++) {
        int r, g, b;
        unpack565(px[i], &r, &g, &b);
        /* luma en el mismo dominio de 6 bits */
        const int r6 = r * 2, b6 = b * 2;
        const int y6 = (r6 * 77 + g * 150 + b6 * 29) >> 8;
        int nr = r6 + ((y6 - r6) * desat) / 255, ng = g + ((y6 - g) * desat) / 255, nb = b6 + ((y6 - b6) * desat) / 255;
        nr = nr * mul / 255;
        ng = ng * mul / 255;
        nb = nb * mul / 255;
        px[i] = (uint16_t)((nr >> 1) << 11 | ng << 5 | (nb >> 1));
    }
}
