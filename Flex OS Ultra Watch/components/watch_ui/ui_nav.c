/*
 * ui_nav — pila de páginas de la interfaz con transiciones.
 *
 * Todas las páginas son hijas de UNA pantalla de LVGL; la raíz es la esfera.
 * Así una sola animación mueve a la vez la página que entra y la de debajo
 * con la curva de la referencia (ease-out cúbico, AppFramework.h:893) y su
 * paralaje (la de debajo se mueve un 30 %, como Ajustes en la referencia),
 * sin depender de las animaciones de carga de pantalla de LVGL, que son
 * lineales.
 *
 *  - Abrir: la página nueva entra desde la derecha en 210 ms.
 *  - Volver: la de arriba sale por la derecha en 190 ms y se borra.
 *  - Gesto volver: arrastre desde el borde izquierdo. La página sigue al dedo
 *    y al soltar se completa o se cancela (ui_gesture.c); la animación dura
 *    lo que corresponde a la distancia que falta.
 *  - Una sola animación viva a la vez. Abrir mientras se anima se ignora
 *    (un doble toque no abre dos páginas). Volver mientras se abre termina la
 *    apertura en el acto y vuelve. Las páginas de debajo quedan ocultas
 *    (LVGL no las dibuja).
 */
#include "ui_internal.h"

#include "ui_gesture.h"
#include "watch_theme.h"

#define NAV_MAX_DEPTH    6
#define NAV_PARALLAX_PCT 30
#define NAV_EDGE_W       20   /* franja del gesto volver, bajo la cabecera */

typedef enum {
    NAV_IDLE = 0,
    NAV_PUSHING,
    NAV_POPPING,
    NAV_DRAGGING,
    NAV_SETTLING, /* gesto cancelado: la página vuelve a su sitio */
} nav_op_t;

static struct {
    lv_obj_t *screen;
    lv_obj_t *pages[NAV_MAX_DEPTH];
    int depth;
    nav_op_t op;
    ui_drag_t drag;
} s_nav;

static lv_obj_t *top_page(void)
{
    return s_nav.depth > 0 ? s_nav.pages[s_nav.depth - 1] : NULL;
}

static lv_obj_t *under_page(void)
{
    return s_nav.depth > 1 ? s_nav.pages[s_nav.depth - 2] : NULL;
}

/* La página de debajo se mueve con la de arriba: -30 % cuando la de arriba
 * cubre la pantalla, 0 cuando la de arriba ha salido del todo. */
static void place(int32_t top_x)
{
    lv_obj_set_x(top_page(), top_x);
    lv_obj_t *u = under_page();
    if (u) {
        lv_obj_set_x(u, (top_x - FLEX_SCREEN_W) * NAV_PARALLAX_PCT / 100);
    }
}

static void nav_x_cb(void *var, int32_t x)
{
    (void)var;
    place(x);
}

static int32_t path_ease_out_cubic(const lv_anim_t *a)
{
    const int32_t u = a->duration > 0 ? lv_map(a->act_time, 0, a->duration, 0, 1000) : 1000;
    return lv_map(ui_ease_out_cubic_permille(u), 0, 1000, a->start_value, a->end_value);
}

/* Estado final de la operación en curso (también al terminarla de golpe). */
static void finish(void)
{
    lv_obj_t *u = under_page();
    switch (s_nav.op) {
    case NAV_PUSHING:
    case NAV_SETTLING:
        place(0);
        if (u) {
            lv_obj_add_flag(u, LV_OBJ_FLAG_HIDDEN);
        }
        break;
    case NAV_POPPING: {
        lv_obj_t *t = top_page();
        s_nav.pages[--s_nav.depth] = NULL;
        lv_obj_delete(t);
        lv_obj_set_x(top_page(), 0);
        break;
    }
    default:
        break;
    }
    s_nav.op = NAV_IDLE;
}

static void anim_done_cb(lv_anim_t *a)
{
    (void)a;
    finish();
}

static void animate(nav_op_t op, int32_t from, int32_t to, uint32_t ms)
{
    s_nav.op = op;
    lv_obj_t *u = under_page();
    if (u) {
        lv_obj_remove_flag(u, LV_OBJ_FLAG_HIDDEN);
    }
    place(from);
    lv_anim_t a;
    lv_anim_init(&a);
    /* Ligada a s_nav y no a la pantalla: lv_screen_load() borra las
     * animaciones de la pantalla que carga. */
    lv_anim_set_var(&a, &s_nav);
    lv_anim_set_exec_cb(&a, nav_x_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path_ease_out_cubic);
    lv_anim_set_completed_cb(&a, anim_done_cb);
    lv_anim_start(&a);
}

/* Termina en el acto la animación en curso (sin dejarla huérfana). */
static void settle_now(void)
{
    if (s_nav.op == NAV_PUSHING || s_nav.op == NAV_POPPING || s_nav.op == NAV_SETTLING) {
        lv_anim_delete(&s_nav, nav_x_cb);
        finish();
    }
}

/* ---- Gesto volver desde el borde izquierdo ---- */

static void edge_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || s_nav.depth < 2) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();

    if (code == LV_EVENT_PRESSED) {
        if (s_nav.op == NAV_IDLE) {
            ui_drag_press(&s_nav.drag, p.x, p.y, now);
        }
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (s_nav.op != NAV_IDLE && s_nav.op != NAV_DRAGGING) {
            return;
        }
        if (ui_drag_move(&s_nav.drag, p.x, p.y, now) == UI_DRAG_ACTIVE) {
            if (s_nav.op == NAV_IDLE) {
                s_nav.op = NAV_DRAGGING;
                lv_obj_remove_flag(under_page(), LV_OBJ_FLAG_HIDDEN);
            }
            place(s_nav.drag.travel);
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const ui_drag_result_t r = ui_drag_release(&s_nav.drag, p.x, p.y, now);
        if (s_nav.op != NAV_DRAGGING) {
            return;
        }
        const int32_t from = s_nav.drag.travel;
        if (r == UI_DRAG_COMMIT) {
            animate(NAV_POPPING, from, FLEX_SCREEN_W,
                    ui_drag_remaining_ms(from, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS, true));
        } else {
            animate(NAV_SETTLING, from, 0, ui_drag_remaining_ms(from, FLEX_SCREEN_W, FLEX_ANIM_APP_OPEN_MS, false));
        }
    }
}

static void add_edge(lv_obj_t *page)
{
    lv_obj_t *edge = lv_obj_create(page);
    lv_obj_remove_style_all(edge);
    lv_obj_set_pos(edge, 0, FLEX_HDR_H);
    lv_obj_set_size(edge, NAV_EDGE_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_remove_flag(edge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(edge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(edge, edge_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(edge, edge_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(edge, edge_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(edge, edge_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* ---- API ---- */

lv_obj_t *ui_page_new(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void page_timer_del_cb(lv_event_t *e)
{
    lv_timer_delete(lv_event_get_user_data(e));
}

lv_timer_t *ui_page_timer(lv_obj_t *page, lv_timer_cb_t cb, uint32_t period_ms, void *user_data)
{
    lv_timer_t *t = lv_timer_create(cb, period_ms, user_data);
    lv_obj_add_event_cb(page, page_timer_del_cb, LV_EVENT_DELETE, t);
    return t;
}

lv_obj_t *ui_nav_start(ui_page_create_t root)
{
    if (s_nav.screen) {
        return s_nav.screen;
    }
    s_nav.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_nav.screen);
    lv_obj_set_style_bg_color(s_nav.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_nav.screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_nav.screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_drag_init(&s_nav.drag, UI_AXIS_X, +1, FLEX_SCREEN_W);
    s_nav.pages[0] = root(s_nav.screen);
    s_nav.depth = s_nav.pages[0] ? 1 : 0;
    return s_nav.screen;
}

bool ui_nav_push_opts(ui_page_create_t create, bool edge_gesture)
{
    if (!s_nav.screen || s_nav.op != NAV_IDLE || s_nav.depth >= NAV_MAX_DEPTH) {
        return false;
    }
    lv_obj_t *page = create(s_nav.screen);
    if (!page) {
        return false;
    }
    if (edge_gesture) {
        add_edge(page);
    }
    s_nav.pages[s_nav.depth++] = page;
    animate(NAV_PUSHING, FLEX_SCREEN_W, 0, FLEX_ANIM_APP_OPEN_MS);
    return true;
}

bool ui_nav_push(ui_page_create_t create)
{
    return ui_nav_push_opts(create, true);
}

bool ui_nav_back(void)
{
    if (s_nav.op == NAV_POPPING || s_nav.op == NAV_DRAGGING) {
        return false;
    }
    settle_now();
    if (s_nav.depth < 2) {
        return false;
    }
    animate(NAV_POPPING, 0, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS);
    return true;
}

void ui_nav_home(void)
{
    if (s_nav.op == NAV_DRAGGING) {
        return;
    }
    settle_now();
    if (s_nav.depth < 2) {
        return;
    }
    /* Las intermedias se borran ya; la de arriba sale animada sobre la raíz. */
    lv_obj_t *top = top_page();
    for (int i = 1; i < s_nav.depth - 1; i++) {
        lv_obj_delete(s_nav.pages[i]);
        s_nav.pages[i] = NULL;
    }
    s_nav.pages[1] = top;
    s_nav.depth = 2;
    animate(NAV_POPPING, 0, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS);
}

int ui_nav_depth(void)
{
    return s_nav.depth;
}

bool ui_nav_busy(void)
{
    return s_nav.op != NAV_IDLE;
}

lv_obj_t *ui_nav_top(void)
{
    return top_page();
}

lv_obj_t *ui_nav_screen(void)
{
    return s_nav.screen;
}
