/*
 * Verificador de integridad visual (B4-C4).
 *
 * Recorre los objetos VISIBLES de la pantalla y de lv_layer_top() y, para
 * cada contenido (texto con letras, icono vectorial, indicador de batería),
 * comprueba:
 *  - ESQUINA: se ve entero dentro del área visible del panel, con sus
 *    esquinas redondeadas (flex_safe.h) y un margen de 2 px;
 *  - RECORTE: ningún contenedor que no se desplace lo corta;
 *  - PUNTOS: ningún texto está truncado con "..." por falta de sitio;
 *  - SOLAPE: no se pisa con otro contenido de la misma capa (página, panel
 *    rápido, animación de carga o aviso). Los textos se comparan por su
 *    "tinta" aproximada (el 70 % central de la línea), no por la caja de la
 *    etiqueta.
 * Además, FONDO: toda superficie con fondo (fila, mosaico, tarjeta, aviso,
 * control) que no ocupe el ancho entero de la pantalla debe caber, con su
 * propio radio, dentro de las esquinas redondeadas del panel: si no, se ve
 * mordida por la esquina.
 * En listas con desplazamiento, solo se juzga la parte que se ve dentro de la
 * lista (una fila puede asomar cortada por el borde recto de la lista al
 * desplazarse, nunca por la esquina del panel).
 *
 * Residuos: sim_check_residue() compara la imagen actual con la de un
 * redibujado completo; si difieren, alguna animación dejó píxeles viejos.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lvgl_private.h"
#include "sim.h"
#include "flex_battery_icon.h"
#include "flex_icons.h"
#include "flex_safe.h"
#include "ui_internal.h"

#define MAX_ITEMS 400
#define LINT_MARGIN 2

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *group;
    lv_area_t box; /* caja juzgada (tinta del texto o caja del icono), ya recortada a las listas */
    const char *what;
} item_t;

static item_t s_items[MAX_ITEMS];
static int s_n;
static int s_errors;

static bool hidden_or_clear(lv_obj_t *o)
{
    for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_style_opa(p, 0) == LV_OPA_TRANSP) {
            return true;
        }
    }
    return false;
}

/* Capa a la que pertenece: el hijo directo de la pantalla o de layer_top. */
static lv_obj_t *group_of(lv_obj_t *o)
{
    lv_obj_t *p = o;
    while (lv_obj_get_parent(p) && lv_obj_get_parent(lv_obj_get_parent(p))) {
        p = lv_obj_get_parent(p);
    }
    return p;
}

static void describe(lv_obj_t *o, char *buf, size_t n)
{
    if (lv_obj_check_type(o, &lv_label_class)) {
        snprintf(buf, n, "texto \"%.40s\"", lv_label_get_text(o));
    } else if (flex_battery_icon_is(o)) {
        snprintf(buf, n, "indicador de bateria");
    } else {
        snprintf(buf, n, "icono");
    }
}

/* Caja de la tinta del texto según alineación y tamaño real. */
static bool text_box(lv_obj_t *label, lv_area_t *out)
{
    const char *t = lv_label_get_text(label);
    if (!t || !t[0] || lv_obj_get_style_text_opa(label, 0) == LV_OPA_TRANSP) {
        return false;
    }
    lv_area_t c;
    lv_obj_get_content_coords(label, &c);
    const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
    const int32_t w = lv_area_get_width(&c);
    const bool wrap = lv_label_get_long_mode(label) == LV_LABEL_LONG_MODE_WRAP;
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, lv_obj_get_style_text_letter_space(label, 0),
                     lv_obj_get_style_text_line_space(label, 0), wrap ? w : LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > w) {
        sz.x = w;
    }
    int32_t x1 = c.x1;
    const lv_text_align_t al = lv_obj_get_style_text_align(label, 0);
    if (al == LV_TEXT_ALIGN_CENTER) {
        x1 = c.x1 + (w - sz.x) / 2;
    } else if (al == LV_TEXT_ALIGN_RIGHT) {
        x1 = c.x2 + 1 - sz.x;
    }
    const int32_t lh = lv_font_get_line_height(f);
    int32_t h = sz.y < lv_area_get_height(&c) ? sz.y : lv_area_get_height(&c);
    out->x1 = x1;
    out->x2 = x1 + sz.x - 1;
    /* tinta aproximada: 70 % central de cada línea */
    out->y1 = c.y1 + lh * 15 / 100;
    out->y2 = c.y1 + h - 1 - lh * 15 / 100;
    if (out->y2 < out->y1) {
        out->y2 = out->y1;
    }
    return true;
}

static void err(const char *where, const char *kind, lv_obj_t *o, const lv_area_t *a, const char *extra)
{
    char d[96];
    describe(o, d, sizeof(d));
    printf("  [%s] %s: %s en (%d,%d)-(%d,%d)%s\n", where, kind, d, (int)a->x1, (int)a->y1, (int)a->x2, (int)a->y2,
           extra ? extra : "");
    s_errors++;
}

/* Superficie con fondo propio que no es de pantalla completa. */
static bool is_surface(lv_obj_t *o)
{
    if (lv_obj_get_style_bg_opa(o, 0) == LV_OPA_TRANSP) {
        return false;
    }
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    return lv_area_get_width(&c) < SIM_W && lv_obj_get_parent(o) != NULL;
}

/* FONDO: recorre las filas visibles de la superficie y comprueba su borde
 * (con su radio) contra las esquinas del panel. 1 px de tolerancia por el
 * suavizado de los bordes. */
static void check_surface(lv_obj_t *o, const char *where)
{
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    lv_area_t vis = c;
    for (lv_obj_t *p = lv_obj_get_parent(o); p && lv_obj_get_parent(p); p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
            continue;
        }
        lv_area_t pc;
        lv_obj_get_coords(p, &pc);
        if (!lv_area_intersect(&vis, &vis, &pc)) {
            return; /* no se ve */
        }
    }
    lv_area_t scr = {0, 0, SIM_W - 1, SIM_H - 1};
    if (!lv_area_intersect(&vis, &vis, &scr)) {
        return;
    }
    const int32_t w = lv_area_get_width(&c), h = lv_area_get_height(&c);
    int32_t r = lv_obj_get_style_radius(o, 0);
    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    for (int32_t y = vis.y1; y <= vis.y2; y++) {
        double in = 0;
        const double yc = y + 0.5;
        double d = -1;
        if (yc < c.y1 + r) {
            d = (c.y1 + r) - yc;
        } else if (yc > c.y2 + 1 - r) {
            d = yc - (c.y2 + 1 - r);
        }
        if (d >= 0) {
            in = r - sqrt((double)r * r - (d > r ? r : d) * (d > r ? r : d));
        }
        const int32_t x1 = (int32_t)ceil(c.x1 + in - 0.5);
        const int32_t x2 = (int32_t)floor(c.x2 - in + 0.5);
        const int32_t sl = flex_safe_left(y, y, 0) - 1, sr = flex_safe_right(y, y, 0) + 1;
        const int32_t lx = x1 > vis.x1 ? x1 : vis.x1, rx = x2 < vis.x2 ? x2 : vis.x2;
        if (lx < sl || rx > sr) {
            char extra[96];
            snprintf(extra, sizeof(extra), " (fila y=%d: x %d..%d, visible %d..%d)", (int)y, (int)lx, (int)rx,
                     (int)sl, (int)sr);
            printf("  [%s] FONDO (mordido por la esquina): superficie en (%d,%d)-(%d,%d)%s\n", where, (int)c.x1,
                   (int)c.y1, (int)c.x2, (int)c.y2, extra);
            s_errors++;
            return;
        }
    }
}

static void visit(lv_obj_t *o, const char *where)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const bool is_label = lv_obj_check_type(o, &lv_label_class);
    const bool is_icon = flex_icon_is(o) || flex_battery_icon_is(o);
    if (!is_label && !is_icon && is_surface(o) && !hidden_or_clear(o)) {
        check_surface(o, where);
    }
    if ((is_label || is_icon) && !hidden_or_clear(o)) {
        lv_area_t box;
        bool ok = true;
        if (is_label) {
            ok = text_box(o, &box);
            if (ok && lv_label_get_long_mode(o) == LV_LABEL_LONG_MODE_DOTS &&
                ((lv_label_t *)o)->dot_begin != 0xFFFFFFFFu) {
                err(where, "PUNTOS (texto truncado)", o, &box, NULL);
            }
        } else {
            lv_obj_get_coords(o, &box);
        }
        /* Recorte por los contenedores: los que se desplazan solo limitan lo
         * que se ve; los demás no deben cortar nada. */
        bool in_view = ok;
        for (lv_obj_t *p = lv_obj_get_parent(o); ok && p && lv_obj_get_parent(p); p = lv_obj_get_parent(p)) {
            if (lv_obj_has_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
                continue;
            }
            lv_area_t pc;
            lv_obj_get_coords(p, &pc);
            if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE) &&
                (lv_obj_get_scroll_top(p) > 0 || lv_obj_get_scroll_bottom(p) > 0)) {
                lv_area_t vis;
                if (!lv_area_intersect(&vis, &box, &pc)) {
                    in_view = false; /* fuera de la parte visible de la lista */
                    break;
                }
                box = vis;
            } else if (!lv_area_is_in(&box, &pc, 0)) {
                lv_area_t vis;
                if (!lv_area_intersect(&vis, &box, &pc)) {
                    in_view = false; /* p. ej. un panel fuera de la pantalla */
                    break;
                }
                char d[64];
                snprintf(d, sizeof(d), " por su contenedor (%d,%d)-(%d,%d)", (int)pc.x1, (int)pc.y1, (int)pc.x2,
                         (int)pc.y2);
                err(where, "RECORTE", o, &box, d);
                box = vis;
            }
        }
        if (in_view) {
            lv_area_t scr = {0, 0, SIM_W - 1, SIM_H - 1};
            if (lv_area_is_in(&box, &scr, 0) || lv_area_is_on(&box, &scr)) {
                if (!flex_area_visible(box.x1, box.y1, box.x2, box.y2, LINT_MARGIN)) {
                    char d[48];
                    snprintf(d, sizeof(d), " (visible: x %d..%d en esa altura)",
                             (int)flex_safe_left(box.y1, box.y2, LINT_MARGIN),
                             (int)flex_safe_right(box.y1, box.y2, LINT_MARGIN));
                    err(where, "ESQUINA (fuera del area visible)", o, &box, d);
                }
                if (s_n < MAX_ITEMS) {
                    s_items[s_n++] = (item_t){o, group_of(o), box, NULL};
                }
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        visit(lv_obj_get_child(o, (int32_t)i), where);
    }
}

static bool is_ancestor(lv_obj_t *a, lv_obj_t *b)
{
    for (lv_obj_t *p = lv_obj_get_parent(b); p; p = lv_obj_get_parent(p)) {
        if (p == a) {
            return true;
        }
    }
    return false;
}

int sim_lint(const char *where)
{
    s_n = 0;
    s_errors = 0;
    lv_obj_t *roots[] = {lv_screen_active(), lv_layer_top()};
    for (int r = 0; r < 2; r++) {
        visit(roots[r], where);
    }
    for (int i = 0; i < s_n; i++) {
        for (int j = i + 1; j < s_n; j++) {
            if (s_items[i].group != s_items[j].group) {
                continue;
            }
            if (is_ancestor(s_items[i].obj, s_items[j].obj) || is_ancestor(s_items[j].obj, s_items[i].obj)) {
                continue;
            }
            lv_area_t x;
            if (lv_area_intersect(&x, &s_items[i].box, &s_items[j].box)) {
                char a[96], b[96], extra[220];
                describe(s_items[i].obj, a, sizeof(a));
                describe(s_items[j].obj, b, sizeof(b));
                snprintf(extra, sizeof(extra), " con %s", b);
                err(where, "SOLAPE", s_items[i].obj, &s_items[i].box, extra);
            }
        }
    }
    sim_fails += s_errors;
    return s_errors;
}

int sim_check_residue(const char *where)
{
    static uint16_t before[SIM_W * SIM_H];
    lv_refr_now(NULL);
    memcpy(before, sim_fb, sizeof(before));
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
    int diff = 0;
    int x1 = SIM_W, y1 = SIM_H, x2 = -1, y2 = -1;
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        if (before[i] != sim_fb[i]) {
            diff++;
            const int x = i % SIM_W, y = i / SIM_W;
            x1 = x < x1 ? x : x1;
            x2 = x > x2 ? x : x2;
            y1 = y < y1 ? y : y1;
            y2 = y > y2 ? y : y2;
        }
    }
    if (diff) {
        printf("  [%s] RESIDUOS: %d px distintos de un redibujado completo en (%d,%d)-(%d,%d)\n", where, diff, x1,
               y1, x2, y2);
        sim_fails++;
    }
    return diff;
}
