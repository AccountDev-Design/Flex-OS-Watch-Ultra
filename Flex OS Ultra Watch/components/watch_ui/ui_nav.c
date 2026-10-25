/*
 * ui_nav — pila de páginas de la interfaz con transiciones y gestos globales.
 *
 * Todas las páginas son hijas de UNA pantalla de LVGL; la raíz es la esfera.
 * Así una sola animación mueve a la vez la página que entra y la de debajo
 * con la curva de la referencia (ease-out cúbico, AppFramework.h:893) y su
 * paralaje (la de debajo se mueve un 30 %, como Ajustes en la referencia),
 * sin depender de las animaciones de carga de pantalla de LVGL, que son
 * lineales.
 *
 *  - Abrir: la página nueva entra desde la derecha en 210 ms. El cajón de
 *    aplicaciones sube desde abajo o baja desde arriba siguiendo al dedo
 *    (ui_nav_drag_open_begin_dir).
 *  - Volver: la de arriba sale por la derecha en 190 ms y se borra.
 *  - Gestos de borde en cada página abierta (los cuatro siguen al dedo y al
 *    soltar se completan o se cancelan con ui_gesture; la animación dura lo
 *    que corresponde a la distancia que falta):
 *      · borde DERECHO hacia la izquierda: atrás un nivel;
 *      · borde IZQUIERDO hacia la derecha: sale de la app al cajón de
 *        aplicaciones (en el propio cajón, vuelve a la esfera);
 *      · borde INFERIOR hacia arriba: a la esfera SIN cerrar la app. Sus
 *        páginas quedan suspendidas (ocultas, con su estado) y se retoman al
 *        abrir otra vez esa app desde el cajón (ui_nav_resume);
 *      · borde SUPERIOR hacia abajo: panel rápido (ui_qp) encima de la
 *        página, que sigue debajo tal cual.
 *  - En la ESFERA (la raíz), el borde derecho hacia la izquierda abre las
 *    tarjetas de Bienestar siguiendo al dedo (ui_wb_cards_create). Es el
 *    único gesto de ese borde en la esfera (allí no hay "atrás"); en una app
 *    el mismo borde sigue siendo "atrás". El gesto lo valida
 *    ui_edge_swipe (franja de UI_EDGE_W px, arranque rápido, recorrido
 *    mínimo; más estricto con un entrenamiento en marcha) y la franja queda
 *    entre la del panel rápido y la del cajón, sin pisarlas.
 *  - Una página puede marcarse "con cambios sin guardar" (ui_nav_set_dirty):
 *    atrás y salir al cajón no la descartan a la primera; avisan y hay que
 *    repetir el gesto. Ir a la esfera no pierde nada (suspende).
 *  - Una sola animación viva a la vez. Abrir mientras se anima se ignora
 *    (un doble toque no abre dos páginas). Volver mientras se abre termina la
 *    apertura en el acto y vuelve. Las páginas de debajo quedan ocultas
 *    (LVGL no las dibuja).
 */
#include "ui_internal.h"

#include "ui_gesture.h"
#include "watch_diag.h"
#include "watch_theme.h"

#define NAV_MAX_DEPTH    6
#define NAV_PARALLAX_PCT 30
#define NAV_EDGE_W       20   /* franjas laterales, bajo la cabecera */
#define NAV_EDGE_BOTTOM  30   /* franja inferior (por debajo de teclados y botones) */
#define NAV_EDGE_TOP     22   /* franja superior (encima del botón de la cabecera) */
#define NAV_DIRTY_MS     3000 /* ventana para repetir el gesto y descartar */
/* Franja de Bienestar en la esfera: por debajo de la del panel rápido
 * (QP_PULL_H, 64 px, ui_qp.c) y por encima de la del cajón (FACE_PULL_H,
 * 56 px, ui_apps.c). */
#define NAV_ROOT_EDGE_TOP    64
#define NAV_ROOT_EDGE_BOTTOM 56

typedef enum {
    NAV_IDLE = 0,
    NAV_PUSHING,
    NAV_POPPING,
    NAV_EXITING,  /* al cajón de aplicaciones */
    NAV_HOMING,   /* a la esfera suspendiendo la app */
    NAV_DRAGGING,
    NAV_SETTLING, /* gesto cancelado: la página vuelve a su sitio */
} nav_op_t;

typedef enum { EDGE_LEFT = 0, EDGE_RIGHT, EDGE_BOTTOM, EDGE_TOP } nav_edge_t;

static struct {
    lv_obj_t *screen;
    lv_obj_t *pages[NAV_MAX_DEPTH];
    ui_page_create_t creates[NAV_MAX_DEPTH];
    int depth;
    /* App suspendida al ir a la esfera con el gesto inferior */
    lv_obj_t *susp[NAV_MAX_DEPTH];
    ui_page_create_t susp_creates[NAV_MAX_DEPTH];
    int susp_n;
    nav_op_t op;
    nav_op_t drag_commit; /* lo que hace el arrastre en curso si se completa */
    bool vertical;        /* la animación mueve la página en Y */
    bool drag_from_top;   /* apertura arrastrada: la página baja desde arriba */
    bool drag_side;       /* apertura arrastrada desde el borde derecho (Bienestar) */
    ui_edge_swipe_t wb;   /* gesto de Bienestar en la esfera */
    bool wb_opening;
    int8_t dir;           /* +1: la de arriba sale por la derecha; -1: por la izquierda */
    lv_obj_t *reveal;     /* página que se descubre debajo */
    bool reveal_temp;     /* cajón creado para el gesto "salir" (no está en la pila) */
    ui_drag_t drag;
    nav_edge_t edge;
    ui_page_create_t root_pending; /* raíz que rehacer al terminar la animación */
    uint32_t ev_suspend, ev_resume;
    lv_obj_t *dirty_warned; /* página con aviso de cambios sin guardar */
    uint32_t dirty_at;
} s_nav;

static bool do_replace_root(ui_page_create_t root);
static void add_edges(lv_obj_t *page, bool is_drawer);
static void add_root_edge(lv_obj_t *root);

static lv_obj_t *top_page(void)
{
    return s_nav.depth > 0 ? s_nav.pages[s_nav.depth - 1] : NULL;
}

static lv_obj_t *under_page(void)
{
    return s_nav.depth > 1 ? s_nav.pages[s_nav.depth - 2] : NULL;
}

static bool is_drawer(int i)
{
    return i > 0 && i < s_nav.depth && s_nav.creates[i] == ui_apps_create;
}

/* Paralaje: la de debajo está desplazada un 30 % hacia el lado contrario al
 * que sale la de arriba, y llega a 0 cuando la de arriba ha salido del todo.
 * En vertical (cajón desde arriba, esfera por abajo) la de debajo no se mueve. */
static void place(int32_t v)
{
    lv_obj_t *t = top_page();
    if (s_nav.vertical) {
        lv_obj_set_y(t, v);
        return;
    }
    lv_obj_set_x(t, v);
    if (s_nav.reveal) {
        const int32_t r = s_nav.dir > 0 ? (v - FLEX_SCREEN_W) : (v + FLEX_SCREEN_W);
        lv_obj_set_x(s_nav.reveal, r * NAV_PARALLAX_PCT / 100);
    }
}

static void nav_v_cb(void *var, int32_t v)
{
    (void)var;
    place(v);
}

static int32_t path_ease_out_cubic(const lv_anim_t *a)
{
    const int32_t u = a->duration > 0 ? lv_map(a->act_time, 0, a->duration, 0, 1000) : 1000;
    return lv_map(ui_ease_out_cubic_permille(u), 0, 1000, a->start_value, a->end_value);
}

static void reset_pos(lv_obj_t *o)
{
    if (o) {
        lv_obj_set_pos(o, 0, 0);
    }
}

static void show(lv_obj_t *o, bool on)
{
    if (!o) {
        return;
    }
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void discard_suspended(void)
{
    for (int i = 0; i < s_nav.susp_n; i++) {
        lv_obj_delete(s_nav.susp[i]);
        s_nav.susp[i] = NULL;
        s_nav.susp_creates[i] = NULL;
    }
    s_nav.susp_n = 0;
}

/* Lleva las páginas de la app a la lista de suspendidas y deja la esfera. */
static void suspend_pages(void)
{
    int first = 1;
    for (int i = s_nav.depth - 1; i >= 1; i--) {
        if (is_drawer(i)) {
            first = i + 1;
            break;
        }
    }
    discard_suspended();
    for (int i = first; i < s_nav.depth; i++) {
        lv_obj_t *p = s_nav.pages[i];
        show(p, false);
        reset_pos(p);
        s_nav.susp[s_nav.susp_n] = p;
        s_nav.susp_creates[s_nav.susp_n] = s_nav.creates[i];
        s_nav.susp_n++;
        lv_obj_send_event(p, (lv_event_code_t)s_nav.ev_suspend, NULL);
    }
    /* Lo que hubiera debajo de la app (el cajón) se rehace al abrirlo. */
    for (int i = 1; i < first; i++) {
        lv_obj_delete(s_nav.pages[i]);
    }
    for (int i = 1; i < NAV_MAX_DEPTH; i++) {
        s_nav.pages[i] = NULL;
        s_nav.creates[i] = NULL;
    }
    s_nav.depth = 1;
    show(s_nav.pages[0], true);
    reset_pos(s_nav.pages[0]);
}

/* Estado final de la operación en curso (también al terminarla de golpe). */
static void finish(void)
{
    lv_obj_t *t = top_page();
    switch (s_nav.op) {
    case NAV_PUSHING:
    case NAV_SETTLING:
        reset_pos(t);
        if (s_nav.reveal && s_nav.reveal != t) {
            reset_pos(s_nav.reveal);
            show(s_nav.reveal, false);
        }
        if (s_nav.reveal_temp && s_nav.reveal) {
            lv_obj_delete(s_nav.reveal);
        }
        break;
    case NAV_POPPING:
        s_nav.pages[s_nav.depth - 1] = NULL;
        s_nav.creates[s_nav.depth - 1] = NULL;
        s_nav.depth--;
        lv_obj_delete(t);
        reset_pos(top_page());
        show(top_page(), true);
        break;
    case NAV_EXITING: {
        lv_obj_t *drawer = s_nav.reveal;
        for (int i = 1; i < s_nav.depth; i++) {
            if (s_nav.pages[i] != drawer) {
                lv_obj_delete(s_nav.pages[i]);
            }
            s_nav.pages[i] = NULL;
            s_nav.creates[i] = NULL;
        }
        s_nav.pages[1] = drawer;
        s_nav.creates[1] = ui_apps_create;
        s_nav.depth = 2;
        lv_obj_move_to_index(drawer, 1);
        reset_pos(drawer);
        show(drawer, true);
        show(s_nav.pages[0], false);
        break;
    }
    case NAV_HOMING:
        suspend_pages();
        break;
    default:
        break;
    }
    /* Evolución de la memoria al abrir y cerrar (registro; sin reservar nada). */
    static const char *const kOpText[] = {
        [NAV_PUSHING] = "abrir", [NAV_POPPING] = "volver", [NAV_EXITING] = "al cajon", [NAV_HOMING] = "a la esfera",
    };
    if (s_nav.op < (int)(sizeof(kOpText) / sizeof(kOpText[0])) && kOpText[s_nav.op]) {
        watch_diag_mem_event(kOpText[s_nav.op]);
    }
    s_nav.op = NAV_IDLE;
    s_nav.reveal = NULL;
    s_nav.reveal_temp = false;
    s_nav.vertical = false;
    s_nav.drag_from_top = false;
    s_nav.drag_side = false;
    s_nav.dir = 1;
    if (s_nav.root_pending) {
        ui_page_create_t root = s_nav.root_pending;
        s_nav.root_pending = NULL;
        do_replace_root(root);
    }
}

static void anim_done_cb(lv_anim_t *a)
{
    (void)a;
    finish();
}

static uint32_t anim_ms(uint32_t ms)
{
    /* Ahorro de energía: transiciones más cortas (menos cuadros que dibujar). */
    return ui_psave_active() ? ms / 2 : ms;
}

static void animate(nav_op_t op, int32_t from, int32_t to, uint32_t ms)
{
    s_nav.op = op;
    show(s_nav.reveal, true);
    place(from);
    lv_anim_t a;
    lv_anim_init(&a);
    /* Ligada a s_nav y no a la pantalla: lv_screen_load() borra las
     * animaciones de la pantalla que carga. */
    lv_anim_set_var(&a, &s_nav);
    lv_anim_set_exec_cb(&a, nav_v_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, anim_ms(ms));
    lv_anim_set_path_cb(&a, path_ease_out_cubic);
    lv_anim_set_completed_cb(&a, anim_done_cb);
    lv_anim_start(&a);
}

/* Termina en el acto la animación en curso (sin dejarla huérfana). */
static void settle_now(void)
{
    if (s_nav.op != NAV_IDLE && s_nav.op != NAV_DRAGGING) {
        lv_anim_delete(&s_nav, nav_v_cb);
        finish();
    }
}

/* ---- Cambios sin guardar ---- */

static bool page_dirty(lv_obj_t *p)
{
    return p && lv_obj_has_state(p, LV_STATE_USER_1);
}

/* true si se puede descartar la página de arriba; si no, avisa y hay que
 * repetir el gesto antes de NAV_DIRTY_MS. */
static bool may_discard_top(void)
{
    lv_obj_t *t = top_page();
    if (!page_dirty(t)) {
        return true;
    }
    if (s_nav.dirty_warned == t && lv_tick_elaps(s_nav.dirty_at) < NAV_DIRTY_MS) {
        s_nav.dirty_warned = NULL;
        return true;
    }
    s_nav.dirty_warned = t;
    s_nav.dirty_at = lv_tick_get();
    ui_banner_show_info("Cambios sin guardar", "Repite el gesto para descartar");
    return false;
}

/* ---- Gestos de borde ---- */

static int32_t edge_span(nav_edge_t e)
{
    return e == EDGE_BOTTOM ? FLEX_SCREEN_H : FLEX_SCREEN_W;
}

static int32_t edge_sign(nav_edge_t e)
{
    return e == EDGE_LEFT ? 1 : -1;
}

/* Al decidirse el arrastre: qué página se descubre y qué hará al soltar. */
static bool drag_begin(nav_edge_t e)
{
    const int top = s_nav.depth - 1;
    s_nav.reveal = NULL;
    s_nav.reveal_temp = false;
    s_nav.vertical = false;
    s_nav.dir = 1;
    switch (e) {
    case EDGE_RIGHT:
        s_nav.dir = -1;
        s_nav.reveal = under_page();
        s_nav.drag_commit = NAV_POPPING;
        break;
    case EDGE_LEFT:
        if (is_drawer(top)) {
            s_nav.reveal = under_page(); /* en el propio cajón: a la esfera */
            s_nav.drag_commit = NAV_POPPING;
            break;
        }
        for (int i = top - 1; i >= 1 && !s_nav.reveal; i--) {
            if (is_drawer(i)) {
                s_nav.reveal = s_nav.pages[i];
            }
        }
        s_nav.drag_commit = NAV_EXITING;
        if (!s_nav.reveal) {
            /* App abierta sin pasar por el cajón (p. ej. Ajustes desde el
             * panel): se descubre un cajón nuevo debajo. */
            lv_obj_t *d = ui_apps_create(s_nav.screen);
            if (!d) {
                return false;
            }
            add_edges(d, true);
            lv_obj_move_to_index(d, s_nav.depth - 1); /* justo debajo de la de arriba */
            s_nav.reveal = d;
            s_nav.reveal_temp = true;
            s_nav.drag_commit = NAV_EXITING;
        }
        break;
    case EDGE_BOTTOM:
        s_nav.vertical = true;
        s_nav.reveal = s_nav.pages[0];
        s_nav.drag_commit = NAV_HOMING;
        break;
    default:
        return false;
    }
    show(s_nav.reveal, true);
    return s_nav.reveal != NULL;
}

static void edge_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const nav_edge_t edge = (nav_edge_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || s_nav.depth < 2) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();

    if (code == LV_EVENT_PRESSED) {
        if (s_nav.op == NAV_IDLE && !ui_qp_active()) {
            s_nav.edge = edge;
            ui_drag_init(&s_nav.drag, edge == EDGE_BOTTOM ? UI_AXIS_Y : UI_AXIS_X, (int8_t)edge_sign(edge),
                         edge_span(edge));
            ui_drag_press(&s_nav.drag, p.x, p.y, now);
        } else {
            s_nav.drag.state = UI_DRAG_IDLE;
        }
        return;
    }
    if (s_nav.edge != edge) {
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (s_nav.op != NAV_IDLE && s_nav.op != NAV_DRAGGING) {
            return;
        }
        if (s_nav.drag.state == UI_DRAG_IDLE) {
            return;
        }
        if (ui_drag_move(&s_nav.drag, p.x, p.y, now) == UI_DRAG_ACTIVE) {
            if (s_nav.op == NAV_IDLE) {
                if (!drag_begin(edge)) {
                    s_nav.drag.state = UI_DRAG_REJECTED;
                    return;
                }
                s_nav.op = NAV_DRAGGING;
            }
            place(edge_sign(edge) * s_nav.drag.travel);
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const ui_drag_result_t r = ui_drag_release(&s_nav.drag, p.x, p.y, now);
        if (s_nav.op != NAV_DRAGGING) {
            return;
        }
        const int32_t span = edge_span(edge);
        const int32_t from = edge_sign(edge) * s_nav.drag.travel;
        bool commit = r == UI_DRAG_COMMIT;
        if (commit && s_nav.drag_commit != NAV_HOMING && !may_discard_top()) {
            commit = false;
        }
        if (commit) {
            animate(s_nav.drag_commit, from, edge_sign(edge) * span,
                    ui_drag_remaining_ms(s_nav.drag.travel, span, FLEX_ANIM_APP_CLOSE_MS, true));
        } else {
            animate(NAV_SETTLING, from, 0,
                    ui_drag_remaining_ms(s_nav.drag.travel, span, FLEX_ANIM_APP_OPEN_MS, false));
        }
    }
}

static lv_obj_t *edge_strip(lv_obj_t *page, int32_t x, int32_t y, int32_t w, int32_t h, lv_event_cb_t cb,
                            void *ud)
{
    lv_obj_t *s = lv_obj_create(page);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_size(s, w, h);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s, cb, LV_EVENT_PRESSED, ud);
    lv_obj_add_event_cb(s, cb, LV_EVENT_PRESSING, ud);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, ud);
    lv_obj_add_event_cb(s, cb, LV_EVENT_PRESS_LOST, ud);
    return s;
}

static void add_edges(lv_obj_t *page, bool drawer)
{
    (void)drawer;
    const int32_t side_h = FLEX_SCREEN_H - FLEX_HDR_H - NAV_EDGE_BOTTOM;
    edge_strip(page, 0, FLEX_HDR_H, NAV_EDGE_W, side_h, edge_cb, (void *)(uintptr_t)EDGE_LEFT);
    edge_strip(page, FLEX_SCREEN_W - NAV_EDGE_W, FLEX_HDR_H, NAV_EDGE_W, side_h, edge_cb,
               (void *)(uintptr_t)EDGE_RIGHT);
    edge_strip(page, 0, FLEX_SCREEN_H - NAV_EDGE_BOTTOM, FLEX_SCREEN_W, NAV_EDGE_BOTTOM, edge_cb,
               (void *)(uintptr_t)EDGE_BOTTOM);
    edge_strip(page, 0, 0, FLEX_SCREEN_W, NAV_EDGE_TOP, ui_qp_pull_cb, NULL);
}

/* ---- Gesto de Bienestar en la esfera ---- */

static void root_edge_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    ui_edge_swipe_t *g = &s_nav.wb;
    if (code == LV_EVENT_PRESSED) {
        s_nav.wb_opening = false;
        ui_edge_swipe_press(g, p.x, p.y, now, FLEX_SCREEN_W, ui_wb_gesture_strict());
        if (s_nav.depth != 1 || s_nav.op != NAV_IDLE || ui_qp_active()) {
            g->drag.state = UI_DRAG_REJECTED; /* solo en la esfera y sin otra cosa en marcha */
        }
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (ui_edge_swipe_move(g, p.x, p.y, now) != UI_DRAG_ACTIVE) {
            return;
        }
        if (!s_nav.wb_opening) {
            if (s_nav.depth != 1 || s_nav.op != NAV_IDLE || ui_qp_active()) {
                g->drag.state = UI_DRAG_REJECTED;
                return;
            }
            ui_face_moved(); /* la esfera no ofrece la pulsación larga */
            s_nav.wb_opening = ui_nav_drag_open_begin_side(ui_wb_cards_create);
            if (!s_nav.wb_opening) {
                g->drag.state = UI_DRAG_REJECTED;
                return;
            }
        }
        ui_nav_drag_open_move(g->drag.travel);
        return;
    }
    /* RELEASED o PRESS_LOST (un dedo que se pierde nunca completa) */
    const int32_t travel = g->drag.travel;
    const bool commit = ui_edge_swipe_release(g, p.x, p.y, now) && code == LV_EVENT_RELEASED;
    if (s_nav.wb_opening) {
        s_nav.wb_opening = false;
        ui_nav_drag_open_end(commit, travel);
    }
}

static void add_root_edge(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    lv_obj_t *s = edge_strip(root, FLEX_SCREEN_W - UI_EDGE_W, NAV_ROOT_EDGE_TOP, UI_EDGE_W,
                             FLEX_SCREEN_H - NAV_ROOT_EDGE_TOP - NAV_ROOT_EDGE_BOTTOM, root_edge_cb, NULL);
    lv_obj_move_foreground(s); /* encima del contenido de la esfera */
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
    s_nav.ev_suspend = lv_event_register_id();
    s_nav.ev_resume = lv_event_register_id();
    s_nav.dir = 1;
    s_nav.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_nav.screen);
    lv_obj_set_style_bg_color(s_nav.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_nav.screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_nav.screen, LV_OBJ_FLAG_SCROLLABLE);
    s_nav.pages[0] = root(s_nav.screen);
    s_nav.creates[0] = root;
    s_nav.depth = s_nav.pages[0] ? 1 : 0;
    add_root_edge(s_nav.pages[0]);
    return s_nav.screen;
}

static bool push(ui_page_create_t create, bool edge_gesture, bool from_top)
{
    if (!s_nav.screen || s_nav.op != NAV_IDLE || s_nav.depth >= NAV_MAX_DEPTH) {
        return false;
    }
    lv_obj_t *page = create(s_nav.screen);
    if (!page) {
        return false;
    }
    if (edge_gesture) {
        add_edges(page, create == ui_apps_create);
    }
    /* Las páginas van debajo de las capas globales (panel rápido, animación
     * de carga), que son hijas de la misma pantalla para que LVGL no dibuje
     * lo que tapan. */
    lv_obj_move_to_index(page, s_nav.depth);
    s_nav.reveal = top_page();
    s_nav.pages[s_nav.depth] = page;
    s_nav.creates[s_nav.depth] = create;
    s_nav.depth++;
    s_nav.dir = 1;
    s_nav.vertical = from_top;
    if (from_top) {
        animate(NAV_PUSHING, -FLEX_SCREEN_H, 0, FLEX_ANIM_APP_OPEN_MS);
    } else {
        animate(NAV_PUSHING, FLEX_SCREEN_W, 0, FLEX_ANIM_APP_OPEN_MS);
    }
    return true;
}

bool ui_nav_push_opts(ui_page_create_t create, bool edge_gesture)
{
    return push(create, edge_gesture, false);
}

bool ui_nav_push(ui_page_create_t create)
{
    return push(create, true, false);
}

bool ui_nav_push_from_top(ui_page_create_t create)
{
    return push(create, true, true);
}

/* Sin desplazamiento: la página aparece ya en su sitio y es ella la que anima
 * su entrada (el selector de esferas, que sale de la esfera con un zoom). */
bool ui_nav_push_instant(ui_page_create_t create)
{
    if (!push(create, true, false)) {
        return false;
    }
    lv_anim_delete(&s_nav, nav_v_cb);
    finish();
    return true;
}

/* ---- Abrir arrastrando (cajón desde la esfera) ----
 * La página nueva sube desde el borde inferior (o baja desde arriba)
 * siguiendo al dedo; al soltar se completa (PUSHING) o vuelve por donde vino y
 * se borra (POPPING en vertical): nunca queda a medias. */
bool ui_nav_drag_open_begin(ui_page_create_t create)
{
    return ui_nav_drag_open_begin_dir(create, false);
}

/* Posición de la página que se abre arrastrando, para un recorrido dado. */
static int32_t drag_open_y(int32_t travel)
{
    travel = LV_CLAMP(0, travel, FLEX_SCREEN_H);
    return s_nav.drag_from_top ? travel - FLEX_SCREEN_H : FLEX_SCREEN_H - travel;
}

bool ui_nav_drag_open_begin_dir(ui_page_create_t create, bool from_top)
{
    if (!s_nav.screen || s_nav.op != NAV_IDLE || s_nav.depth >= NAV_MAX_DEPTH) {
        return false;
    }
    lv_obj_t *page = create(s_nav.screen);
    if (!page) {
        return false;
    }
    add_edges(page, create == ui_apps_create);
    lv_obj_move_to_index(page, s_nav.depth);
    s_nav.reveal = top_page();
    s_nav.pages[s_nav.depth] = page;
    s_nav.creates[s_nav.depth] = create;
    s_nav.depth++;
    s_nav.dir = 1;
    s_nav.vertical = true;
    s_nav.drag_from_top = from_top;
    s_nav.op = NAV_DRAGGING;
    place(drag_open_y(0));
    return true;
}

/* Igual desde el borde derecho: la página entra hacia la izquierda con el
 * dedo y la de debajo se desplaza con el mismo paralaje que al abrir. */
bool ui_nav_drag_open_begin_side(ui_page_create_t create)
{
    if (!s_nav.screen || s_nav.op != NAV_IDLE || s_nav.depth >= NAV_MAX_DEPTH) {
        return false;
    }
    lv_obj_t *page = create(s_nav.screen);
    if (!page) {
        return false;
    }
    add_edges(page, false);
    lv_obj_move_to_index(page, s_nav.depth);
    s_nav.reveal = top_page();
    s_nav.pages[s_nav.depth] = page;
    s_nav.creates[s_nav.depth] = create;
    s_nav.depth++;
    s_nav.dir = 1;
    s_nav.vertical = false;
    s_nav.drag_side = true;
    s_nav.op = NAV_DRAGGING;
    show(s_nav.reveal, true);
    place(FLEX_SCREEN_W);
    return true;
}

void ui_nav_drag_open_move(int32_t travel)
{
    if (s_nav.op == NAV_DRAGGING && s_nav.drag_side) {
        place(FLEX_SCREEN_W - LV_CLAMP(0, travel, FLEX_SCREEN_W));
    } else if (s_nav.op == NAV_DRAGGING && s_nav.vertical) {
        place(drag_open_y(travel));
    }
}

void ui_nav_drag_open_end(bool commit, int32_t travel)
{
    if (s_nav.op == NAV_DRAGGING && s_nav.drag_side) {
        travel = LV_CLAMP(0, travel, FLEX_SCREEN_W);
        if (commit) {
            animate(NAV_PUSHING, FLEX_SCREEN_W - travel, 0,
                    ui_drag_remaining_ms(travel, FLEX_SCREEN_W, FLEX_ANIM_APP_OPEN_MS, true));
        } else {
            animate(NAV_POPPING, FLEX_SCREEN_W - travel, FLEX_SCREEN_W,
                    ui_drag_remaining_ms(travel, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS, false));
        }
        return;
    }
    if (s_nav.op != NAV_DRAGGING || !s_nav.vertical) {
        return;
    }
    travel = LV_CLAMP(0, travel, FLEX_SCREEN_H);
    if (commit) {
        animate(NAV_PUSHING, drag_open_y(travel), 0,
                ui_drag_remaining_ms(travel, FLEX_SCREEN_H, FLEX_ANIM_APP_OPEN_MS, true));
    } else {
        animate(NAV_POPPING, drag_open_y(travel), drag_open_y(0),
                ui_drag_remaining_ms(travel, FLEX_SCREEN_H, FLEX_ANIM_APP_CLOSE_MS, false));
    }
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
    s_nav.reveal = under_page();
    s_nav.dir = 1;
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
    ui_page_create_t top_create = s_nav.creates[s_nav.depth - 1];
    for (int i = 1; i < s_nav.depth - 1; i++) {
        lv_obj_delete(s_nav.pages[i]);
        s_nav.pages[i] = NULL;
        s_nav.creates[i] = NULL;
    }
    s_nav.pages[1] = top;
    s_nav.creates[1] = top_create;
    s_nav.depth = 2;
    s_nav.reveal = s_nav.pages[0];
    s_nav.dir = 1;
    animate(NAV_POPPING, 0, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS);
}

bool ui_nav_back_to(ui_page_create_t create)
{
    if (s_nav.op == NAV_DRAGGING || !create) {
        return false;
    }
    int k = -1;
    for (int i = s_nav.depth - 2; i >= 0 && k < 0; i--) {
        if (s_nav.creates[i] == create) {
            k = i;
        }
    }
    if (k < 0) {
        return false;
    }
    settle_now();
    if (k >= s_nav.depth - 1) {
        return true;
    }
    /* Como ui_nav_home: las intermedias se borran ya y la de arriba sale
     * animada sobre la elegida. */
    lv_obj_t *top = top_page();
    ui_page_create_t top_create = s_nav.creates[s_nav.depth - 1];
    for (int i = k + 1; i < s_nav.depth - 1; i++) {
        lv_obj_delete(s_nav.pages[i]);
        s_nav.pages[i] = NULL;
        s_nav.creates[i] = NULL;
    }
    s_nav.pages[k + 1] = top;
    s_nav.creates[k + 1] = top_create;
    for (int i = k + 2; i < NAV_MAX_DEPTH; i++) {
        s_nav.pages[i] = NULL;
        s_nav.creates[i] = NULL;
    }
    s_nav.depth = k + 2;
    s_nav.reveal = s_nav.pages[k];
    s_nav.dir = 1;
    animate(NAV_POPPING, 0, FLEX_SCREEN_W, FLEX_ANIM_APP_CLOSE_MS);
    return true;
}

/* Vuelve un nivel (o a la esfera) sin animación: la página de arriba ya ha
 * hecho su propia salida (el zoom del selector hasta la esfera elegida). */
bool ui_nav_back_instant(void)
{
    if (s_nav.op == NAV_DRAGGING) {
        return false;
    }
    settle_now();
    if (s_nav.depth < 2) {
        return false;
    }
    s_nav.reveal = under_page();
    s_nav.op = NAV_POPPING;
    finish();
    return true;
}

void ui_nav_home_instant(void)
{
    if (s_nav.op == NAV_DRAGGING) {
        return;
    }
    settle_now();
    while (s_nav.depth > 1 && ui_nav_back_instant()) {
    }
}

bool ui_nav_suspend(bool animated)
{
    if (s_nav.op == NAV_DRAGGING) {
        return false;
    }
    settle_now();
    if (s_nav.depth < 2) {
        return false;
    }
    s_nav.reveal = s_nav.pages[0];
    s_nav.vertical = true;
    if (animated) {
        animate(NAV_HOMING, 0, -FLEX_SCREEN_H, FLEX_ANIM_APP_CLOSE_MS);
    } else {
        s_nav.op = NAV_HOMING;
        finish();
    }
    return true;
}

bool ui_nav_resume(ui_page_create_t app)
{
    if (!s_nav.screen || s_nav.op != NAV_IDLE || s_nav.susp_n == 0 || s_nav.susp_creates[0] != app) {
        return false;
    }
    if (s_nav.depth + s_nav.susp_n > NAV_MAX_DEPTH) {
        discard_suspended();
        return false;
    }
    s_nav.reveal = top_page();
    for (int i = 0; i < s_nav.susp_n; i++) {
        lv_obj_t *p = s_nav.susp[i];
        lv_obj_move_to_index(p, s_nav.depth);
        s_nav.pages[s_nav.depth] = p;
        s_nav.creates[s_nav.depth] = s_nav.susp_creates[i];
        s_nav.depth++;
        s_nav.susp[i] = NULL;
        s_nav.susp_creates[i] = NULL;
        lv_obj_send_event(p, (lv_event_code_t)s_nav.ev_resume, NULL);
    }
    s_nav.susp_n = 0;
    show(top_page(), true);
    s_nav.dir = 1;
    s_nav.vertical = false;
    animate(NAV_PUSHING, FLEX_SCREEN_W, 0, FLEX_ANIM_APP_OPEN_MS);
    return true;
}

bool ui_nav_open_app(ui_page_create_t app)
{
    if (ui_nav_resume(app)) {
        return true;
    }
    if (s_nav.op != NAV_IDLE) {
        return false;
    }
    discard_suspended(); /* abrir otra app cierra la suspendida */
    return ui_nav_push(app);
}

int ui_nav_suspended_count(void)
{
    return s_nav.susp_n;
}

void ui_nav_discard_suspended(void)
{
    discard_suspended();
}

ui_page_create_t ui_nav_suspended_app(void)
{
    return s_nav.susp_n > 0 ? s_nav.susp_creates[0] : NULL;
}

uint32_t ui_nav_event_suspend(void)
{
    return s_nav.ev_suspend;
}

uint32_t ui_nav_event_resume(void)
{
    return s_nav.ev_resume;
}

void ui_nav_set_dirty(lv_obj_t *page, bool dirty)
{
    if (!page) {
        return;
    }
    if (dirty) {
        lv_obj_add_state(page, LV_STATE_USER_1);
    } else {
        lv_obj_remove_state(page, LV_STATE_USER_1);
        if (s_nav.dirty_warned == page) {
            s_nav.dirty_warned = NULL;
        }
    }
}

bool ui_nav_replace_root(ui_page_create_t root)
{
    if (!s_nav.screen || s_nav.depth < 1) {
        return false;
    }
    if (s_nav.op != NAV_IDLE) {
        /* En plena animación o arrastre: se hace al terminar (finish). */
        s_nav.root_pending = root;
        return true;
    }
    return do_replace_root(root);
}

static bool do_replace_root(ui_page_create_t root)
{
    if (!s_nav.screen || s_nav.depth < 1) {
        return false;
    }
    lv_obj_t *old = s_nav.pages[0];
    lv_obj_t *page = root(s_nav.screen);
    if (!page) {
        return false;
    }
    add_root_edge(page);
    /* Mismo sitio y visibilidad que la raíz anterior (debajo de las páginas
     * abiertas, que no se tocan). */
    lv_obj_move_to_index(page, 0);
    lv_obj_set_pos(page, lv_obj_get_x(old), lv_obj_get_y(old));
    if (lv_obj_has_flag(old, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
    s_nav.pages[0] = page;
    s_nav.creates[0] = root;
    lv_obj_delete(old);
    return true;
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

lv_obj_t *ui_nav_page(int i)
{
    return (i >= 0 && i < s_nav.depth) ? s_nav.pages[i] : NULL;
}

lv_obj_t *ui_overlay_parent(void)
{
    /* LVGL 9.5 no tiene en cuenta lv_layer_top() al decidir qué está tapado:
     * una capa opaca a pantalla completa ahí no evita dibujar la esfera
     * debajo. Como hija de la pantalla sí (lv_refr_get_top_obj). */
    return s_nav.screen ? s_nav.screen : lv_layer_top();
}
