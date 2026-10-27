/*
 * ui_apps — cajón de aplicaciones ("Grid View"): panal infinito con efecto
 * lupa que sigue al dedo, sobre negro AMOLED.
 *
 * Referencia de COMPORTAMIENTO: el panal de Apple Watch (desplazamiento libre
 * en todas las direcciones, iconos grandes cerca del foco y pequeños hacia los
 * bordes). Los iconos y la identidad son los de Flex OS Ultra: los ORIGINALES
 * de cada app (flex_app_icons, el dibujo de drawAppIcon, base circular de
 * 92 px) y el punto de estado en Reloj mientras el cronómetro o el
 * temporizador cuentan (AppDrawer.h:690).
 *
 *  - Se abre desde la esfera deslizando hacia ABAJO desde su zona central (el
 *    cajón baja siguiendo al dedo) o hacia arriba desde el borde inferior; se
 *    vuelve a la esfera deslizando hacia arriba desde el borde inferior (o
 *    desde un borde lateral), como antes (ui_nav).
 *  - Geometría, toro de 16 huecos sin duplicados visibles, lupa, inercia y
 *    muelles: ui_grid_logic.c (lógica pura, probada en el PC). Aquí solo se
 *    traduce a LVGL: un GRUPO FIJO de GRID_MAX_ITEMS imágenes creado una vez
 *    y reutilizado (cada celda visible conserva su imagen mientras se ve), un
 *    único temporizador de 16 ms que solo corre mientras algo se mueve (en
 *    reposo no se redibuja nada) y ninguna reserva de memoria por cuadro.
 *  - Selección fiable: al pulsar se fija la app que hay bajo el dedo con la
 *    geometría que se ve en ese instante; se abre al soltar solo si el dedo no
 *    se movió más de UI_DRAG_SLOP_PX (un arrastre nunca abre nada, y que el
 *    icono crezca después con la lupa no cambia la elección). Pulsar fuera de
 *    un icono no abre nada.
 *  - Fondo: negro AMOLED en reposo. Al abrirse o cerrarse siguiendo al dedo
 *    (ui_nav), la página NO se desplaza: la esfera de debajo se queda quieta y
 *    la página la va cubriendo con un velo negro cuya opacidad sigue al
 *    recorrido, mientras solo la rejilla de iconos se mueve con el dedo
 *    (ui_apps_transition). Antes la página llevaba dentro una copia
 *    desenfocada del fondo de pantalla (402 KB en PSRAM generados por partes)
 *    y la arrastraba entera: dos fondos moviéndose a la vez, un fundido extra
 *    al terminar de generarse y trabajo de CPU durante el gesto.
 *
 * Abrir una app la retoma si estaba suspendida (se salió con el gesto hacia
 * arriba) con su estado; si no, la abre nueva (ui_nav_open_app). Solo aparecen
 * las apps que funcionan de verdad en el reloj (docs/INVENTARIO-APPS.md).
 */
#include "ui_internal.h"

#include <math.h>
#include <string.h>
#include "flex_app_icons.h"
#include "ui_gesture.h"
#include "ui_grid_logic.h"
#include "watch_theme.h"

#define APPS_TICK_MS 500
#define FRAME_MS     16
#define ICON_D       FLEX_APP_ICON_SIZE        /* 92 */
#define BADGE_D      18                        /* punto de estado: 7 px a 72 en la referencia */
#define HIT_MARGIN   6                         /* px alrededor del círculo que cuentan como toque */

typedef struct {
    flex_app_t app;
    ui_page_create_t create;
} app_entry_t;

/* Apps del reloj con su página. Una app del inventario entra aquí cuando
 * hace algo real. */
static const app_entry_t kApps[] = {
    {FLEX_APP_RELOJ, ui_reloj_create},
    {FLEX_APP_FLEX_PHONE, ui_flexphone_create},
    {FLEX_APP_NAVEGADOR, ui_browser_create},
    {FLEX_APP_MUSICA, ui_music_create},
    {FLEX_APP_NOTAS, ui_notes_create},
    {FLEX_APP_PAINT, ui_paint_create},
    {FLEX_APP_GALERIA, ui_gallery_create},
    {FLEX_APP_CALENDARIO, ui_calendar_create},
    {FLEX_APP_CALCULADORA, ui_calc_create},
    {FLEX_APP_CLIMA, ui_weather_create},
    {FLEX_APP_FLEX_COMPASS, ui_compass_create},
    {FLEX_APP_DEVICE_CARE, ui_devcare_create},
    {FLEX_APP_ALMACENAMIENTO, ui_storage_create},
    {FLEX_APP_AJUSTES, ui_settings_create},
    {FLEX_APP_BIENESTAR, ui_wb_create},
};
#define APPS_N ((int)(sizeof(kApps) / sizeof(kApps[0])))

/* Hueco del toro -> índice en kApps (-1 = vacío). Con la celda (0,0) en el
 * centro se ven el centro, el anillo de 6 a 1 paso y el de 6 a raíz de 3
 * pasos (13 huecos); los huecos 2, 8 y 10 caen en el anillo a 2 pasos, que en
 * reposo queda fuera de la zona visible: ahí van dos apps y el hueco
 * vacío (15 apps en 16 huecos). Por ángulo, empezando a la derecha:
 *   anillo 1:     1 (1,0)  13 (1,-1)  12 (0,-1)  3 (-1,0)  7 (-1,1)  4 (0,1)
 *   anillo raíz 3: 14 (2,-1)  9 (1,-2)  15 (-1,-1)  6 (-2,1)  11 (-1,2)  5 (1,1) */
static const int8_t kSlotApp[GRID_SLOTS] = {
    [0] = 0,   /* Reloj, en el centro */
    [1] = 1,   /* Flex Phone */
    [13] = 2,  /* Navegador */
    [12] = 3,  /* Música */
    [3] = 4,   /* Notas */
    [7] = 7,   /* Calendario */
    [4] = 9,   /* Clima */
    [14] = 6,  /* Galería */
    [9] = 8,   /* Calculadora */
    [15] = 10, /* Flex Compass */
    [6] = 11,  /* Device Care */
    [11] = 12, /* Almacenamiento */
    [5] = 13,  /* Ajustes */
    [2] = 5,   /* Paint (se ve al desplazarse) */
    [8] = 14,  /* Bienestar (se ve al desplazarse; también con el gesto de la esfera) */
    [10] = -1,
};

typedef struct {
    lv_obj_t *img;
    int q, r;     /* celda que muestra (si used) */
    int8_t app;   /* índice en kApps */
    bool used;
    float x, y, scale;
    uint8_t opa;
} cell_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *grid;
    lv_obj_t *badge;
    cell_t cells[GRID_MAX_ITEMS];
    lv_timer_t *frame;
    grid_vec_t off, vel;     /* desplazamiento de la rejilla y su velocidad */
    grid_vec_t lens, lens_v; /* punto de la lupa y su velocidad */
    grid_vec_t press_pt, last_pt;
    grid_vel_t vs;
    bool pressed, dragging, snapping;
    int8_t hit;              /* celda pulsada (índice en cells) o -1 */
    int8_t hit_app;
    uint32_t last_ms;
} grid_t;

/* Desplazamiento con el que se abrió la última vez (el cajón vuelve donde estaba). */
static grid_vec_t s_last_off;

static const grid_vec_t kCenter = {FLEX_SCREEN_W / 2.0f, FLEX_SCREEN_H / 2.0f};

static lv_obj_t *new_cell(lv_obj_t *grid)
{
    lv_obj_t *img = lv_image_create(grid);
    lv_obj_set_size(img, ICON_D, ICON_D);
    lv_image_set_pivot(img, ICON_D / 2, ICON_D / 2);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return img;
}

static void place_badge(grid_t *g)
{
    if (!g->badge) {
        return;
    }
    const bool busy = ui_sw()->st == SW_RUN || ui_tmr()->st == TMR_RUN;
    const cell_t *reloj = NULL;
    for (int i = 0; i < GRID_MAX_ITEMS && busy; i++) {
        if (g->cells[i].used && g->cells[i].app == 0 && g->cells[i].opa > 40) {
            reloj = &g->cells[i];
            break;
        }
    }
    watch_theme_set_visible(g->badge, reloj != NULL);
    if (reloj) {
        /* arriba a la derecha, sobre el borde del círculo (45°) */
        const float r = ICON_D / 2.0f * reloj->scale;
        const int32_t x = (int32_t)lroundf(reloj->x + r * 0.70f) - BADGE_D / 2;
        const int32_t y = (int32_t)lroundf(reloj->y - r * 0.70f) - BADGE_D / 2;
        if (x != lv_obj_get_x(g->badge) || y != lv_obj_get_y(g->badge)) {
            lv_obj_set_pos(g->badge, x, y);
        }
    }
}

/* Aplica la geometría actual a las imágenes: cada celda visible conserva su
 * imagen; solo se toca lo que cambió. */
static void layout(grid_t *g)
{
    grid_item_t items[GRID_MAX_ITEMS];
    const int n = grid_layout(g->off, g->lens, kCenter, items, GRID_MAX_ITEMS);
    bool keep[GRID_MAX_ITEMS] = {false};
    int map[GRID_MAX_ITEMS];
    /* 1) celdas que ya tenían imagen */
    for (int k = 0; k < n; k++) {
        map[k] = -1;
        if (kSlotApp[items[k].slot] < 0) {
            continue; /* hueco vacío: no se dibuja nada */
        }
        for (int i = 0; i < GRID_MAX_ITEMS; i++) {
            if (g->cells[i].used && g->cells[i].q == items[k].q && g->cells[i].r == items[k].r) {
                map[k] = i;
                keep[i] = true;
                break;
            }
        }
    }
    /* 2) las que salen de la vista sueltan su imagen */
    for (int i = 0; i < GRID_MAX_ITEMS; i++) {
        if (g->cells[i].used && !keep[i]) {
            g->cells[i].used = false;
            watch_theme_set_visible(g->cells[i].img, false);
            if (g->hit == i) {
                g->hit = -1; /* el icono pulsado salió de la vista: ya no se abre */
            }
        }
    }
    /* 3) las nuevas toman una libre */
    for (int k = 0; k < n; k++) {
        if (map[k] >= 0 || kSlotApp[items[k].slot] < 0) {
            continue;
        }
        for (int i = 0; i < GRID_MAX_ITEMS; i++) {
            if (!g->cells[i].used && !keep[i]) {
                cell_t *c = &g->cells[i];
                c->used = true;
                keep[i] = true;
                c->q = items[k].q;
                c->r = items[k].r;
                const int8_t app = kSlotApp[items[k].slot];
                if (app != c->app || !lv_image_get_src(c->img)) {
                    c->app = app;
                    lv_image_set_src(c->img, flex_app_icon(kApps[app].app));
                }
                c->x = c->y = -1000.0f;
                c->scale = -1.0f;
                c->opa = 0;
                map[k] = i;
                break;
            }
        }
    }
    /* 4) posición, tamaño y opacidad (solo si cambian a nivel de píxel) */
    for (int k = 0; k < n; k++) {
        if (map[k] < 0) {
            continue;
        }
        cell_t *c = &g->cells[map[k]];
        const grid_item_t *it = &items[k];
        const int32_t px = (int32_t)lroundf(it->x) - ICON_D / 2, py = (int32_t)lroundf(it->y) - ICON_D / 2;
        if (px != (int32_t)lroundf(c->x) - ICON_D / 2 || py != (int32_t)lroundf(c->y) - ICON_D / 2) {
            lv_obj_set_pos(c->img, px, py);
        }
        const uint32_t sc = (uint32_t)lroundf(it->scale * 256.0f);
        if (sc != (uint32_t)lroundf(c->scale * 256.0f)) {
            lv_image_set_scale(c->img, sc);
        }
        if (it->opa != c->opa) {
            lv_obj_set_style_image_opa(c->img, it->opa, 0);
        }
        c->x = it->x;
        c->y = it->y;
        c->scale = it->scale;
        c->opa = it->opa;
        watch_theme_set_visible(c->img, it->opa > 0);
    }
    place_badge(g);
}

static void set_pressed_look(grid_t *g, bool on)
{
    if (g->hit < 0) {
        return;
    }
    lv_obj_t *img = g->cells[g->hit].img;
    lv_obj_set_style_image_recolor(img, lv_color_black(), 0);
    lv_obj_set_style_image_recolor_opa(img, on ? LV_OPA_40 : LV_OPA_TRANSP, 0);
}

/* Paso de física de un cuadro; false cuando todo está quieto. */
static bool physics(grid_t *g, float dt)
{
    const grid_vec_t lens_target = g->pressed ? g->last_pt : kCenter;
    grid_spring_step(&g->lens, &g->lens_v, lens_target, GRID_LENS_OMEGA, dt);
    if (!g->pressed) {
        const float sp = sqrtf(g->vel.x * g->vel.x + g->vel.y * g->vel.y);
        if (!g->snapping && sp > GRID_SNAP_SPEED) {
            grid_inertia_step(&g->off, &g->vel, dt);
        } else {
            /* frenado: el muelle centra la celda más cercana (con la velocidad que llevaba) */
            g->snapping = true;
            grid_spring_step(&g->off, &g->vel, grid_snap_target(g->off), GRID_SNAP_OMEGA, dt);
        }
    }
    const grid_vec_t t = grid_snap_target(g->off);
    const bool off_still = g->pressed || (g->snapping && fabsf(g->off.x - t.x) < 0.3f &&
                                          fabsf(g->off.y - t.y) < 0.3f && fabsf(g->vel.x) < 3.0f &&
                                          fabsf(g->vel.y) < 3.0f);
    const bool lens_still = fabsf(g->lens.x - lens_target.x) < 0.3f && fabsf(g->lens.y - lens_target.y) < 0.3f &&
                            fabsf(g->lens_v.x) < 3.0f && fabsf(g->lens_v.y) < 3.0f;
    if (off_still && lens_still && !g->pressed) {
        g->off = t;
        g->vel.x = g->vel.y = 0.0f;
        g->lens = kCenter;
        g->lens_v.x = g->lens_v.y = 0.0f;
        return false;
    }
    return !(off_still && lens_still);
}

static void frame_cb(lv_timer_t *t)
{
    grid_t *g = lv_timer_get_user_data(t);
    const uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, g->last_ms) / 1000.0f;
    g->last_ms = now;
    if (dt > 0.05f) {
        dt = 0.05f; /* tras una pausa larga (otra página encima), sin saltos */
    }
    const bool moving = physics(g, dt);
    layout(g);
    s_last_off = g->off;
    if (!moving && !g->pressed) {
        lv_timer_pause(t); /* quieto: sin cuadros ni redibujos hasta el próximo toque */
    }
}

static void kick(grid_t *g)
{
    if (lv_timer_get_paused(g->frame)) {
        g->last_ms = lv_tick_get();
        lv_timer_resume(g->frame);
    }
}

/* Celda visible bajo el punto (la más cercana a su centro dentro de su círculo). */
static int8_t hit_test(const grid_t *g, float x, float y)
{
    int8_t best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < GRID_MAX_ITEMS; i++) {
        const cell_t *c = &g->cells[i];
        if (!c->used || c->opa < 128) {
            continue; /* los que se están desvaneciendo en el borde no se eligen */
        }
        const float r = ICON_D / 2.0f * c->scale + HIT_MARGIN;
        const float d = (c->x - x) * (c->x - x) + (c->y - y) * (c->y - y);
        if (d <= r * r && d < best_d) {
            best_d = d;
            best = (int8_t)i;
        }
    }
    return best;
}

static void touch_cb(lv_event_t *e)
{
    grid_t *g = lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    const grid_vec_t pt = {(float)p.x, (float)p.y};
    if (code == LV_EVENT_PRESSED) {
        g->pressed = true;
        g->dragging = false;
        g->snapping = false;
        g->vel.x = g->vel.y = 0.0f; /* tocar detiene la inercia */
        g->press_pt = pt;
        g->last_pt = pt;
        grid_vel_reset(&g->vs);
        grid_vel_add(&g->vs, pt.x, pt.y, now);
        g->hit = hit_test(g, pt.x, pt.y);
        g->hit_app = g->hit >= 0 ? g->cells[g->hit].app : -1;
        set_pressed_look(g, true);
        kick(g);
        return;
    }
    if (!g->pressed) {
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        const float dx = pt.x - g->press_pt.x, dy = pt.y - g->press_pt.y;
        if (!g->dragging && dx * dx + dy * dy > (float)(UI_DRAG_SLOP_PX * UI_DRAG_SLOP_PX)) {
            g->dragging = true; /* ya es un desplazamiento: no se abrirá nada */
            set_pressed_look(g, false);
            g->hit = -1;
        }
        if (g->dragging) {
            g->off.x += pt.x - g->last_pt.x; /* 1:1 con el dedo */
            g->off.y += pt.y - g->last_pt.y;
        }
        g->last_pt = pt;
        grid_vel_add(&g->vs, pt.x, pt.y, now);
        kick(g);
        return;
    }
    /* RELEASED o PRESS_LOST */
    g->pressed = false;
    set_pressed_look(g, false);
    if (g->dragging) {
        g->vel = grid_vel_get(&g->vs, now);
    }
    const bool open = code == LV_EVENT_RELEASED && !g->dragging && g->hit >= 0 && g->hit_app >= 0 &&
                      g->cells[g->hit].used && g->cells[g->hit].app == g->hit_app;
    const int8_t app = g->hit_app;
    g->hit = -1;
    g->dragging = false;
    kick(g);
    if (open && !ui_nav_busy()) {
        ui_nav_open_app(kApps[app].create);
    }
}

static void tick_cb(lv_timer_t *t)
{
    place_badge(lv_timer_get_user_data(t));
}

static void delete_cb(lv_event_t *e)
{
    grid_t *g = lv_event_get_user_data(e);
    lv_timer_delete(g->frame);
    lv_free(g);
}

lv_obj_t *ui_apps_create(lv_obj_t *parent)
{
    grid_t *g = lv_malloc_zeroed(sizeof(*g));
    if (!g) {
        return NULL; /* ui_nav no abre nada: sin memoria no se cuelga */
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    g->page = page;
    g->hit = -1;
    g->hit_app = -1;
    g->off = s_last_off;
    g->lens = kCenter;

    /* Rejilla a pantalla completa (sin cabecera): se sale con los gestos de borde. */
    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(grid, touch_cb, LV_EVENT_PRESSED, g);
    lv_obj_add_event_cb(grid, touch_cb, LV_EVENT_PRESSING, g);
    lv_obj_add_event_cb(grid, touch_cb, LV_EVENT_RELEASED, g);
    lv_obj_add_event_cb(grid, touch_cb, LV_EVENT_PRESS_LOST, g);
    g->grid = grid;
    for (int i = 0; i < GRID_MAX_ITEMS; i++) {
        g->cells[i].img = new_cell(grid);
        g->cells[i].app = -1;
    }
    g->badge = lv_obj_create(grid);
    lv_obj_remove_style_all(g->badge);
    lv_obj_set_size(g->badge, BADGE_D, BADGE_D);
    lv_obj_set_style_radius(g->badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g->badge, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(g->badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g->badge, lv_color_black(), 0); /* aro: separa el punto del icono */
    lv_obj_set_style_border_width(g->badge, 2, 0);
    lv_obj_remove_flag(g->badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g->badge, LV_OBJ_FLAG_HIDDEN);

    g->frame = lv_timer_create(frame_cb, FRAME_MS, g);
    lv_timer_pause(g->frame);
    layout(g);
    lv_obj_set_user_data(page, g);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, g);
    ui_page_timer(page, tick_cb, APPS_TICK_MS, g);
    return page;
}

static grid_t *grid_of(lv_obj_t *drawer)
{
    grid_t *g = drawer ? lv_obj_get_user_data(drawer) : NULL;
    return (g && g->page == drawer) ? g : NULL;
}

void ui_apps_transition(lv_obj_t *drawer, int32_t offset)
{
    grid_t *g = grid_of(drawer);
    if (!g) {
        return;
    }
    const int32_t d = LV_CLAMP(0, LV_ABS(offset), FLEX_SCREEN_H);
    /* Velo: transparente con el cajón fuera y opaco (el negro de reposo) con
     * el cajón en su sitio. Solo se toca lo que cambia. */
    const lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER * (FLEX_SCREEN_H - d) / FLEX_SCREEN_H);
    if (lv_obj_get_style_bg_opa(drawer, LV_PART_MAIN) != opa) {
        lv_obj_set_style_bg_opa(drawer, opa, 0);
    }
    if (lv_obj_get_y(g->grid) != offset) {
        lv_obj_set_y(g->grid, offset);
    }
}

lv_obj_t *ui_apps_find(lv_obj_t *drawer, const char *name)
{
    grid_t *g = grid_of(drawer);
    if (!g || !name) {
        return NULL;
    }
    int app = -1;
    for (int i = 0; i < APPS_N; i++) {
        if (strcmp(flex_app_name(kApps[i].app), name) == 0) {
            app = i;
        }
    }
    if (app < 0) {
        return NULL;
    }
    for (int i = 0; i < GRID_MAX_ITEMS; i++) {
        if (g->cells[i].used && g->cells[i].app == app && g->cells[i].opa == LV_OPA_COVER) {
            return g->cells[i].img;
        }
    }
    /* No se ve del todo: se centra su copia más cercana (accesibilidad). */
    int slot = -1;
    for (int s = 0; s < GRID_SLOTS; s++) {
        if (kSlotApp[s] == app) {
            slot = s;
        }
    }
    int q, r;
    grid_nearest_copy(slot, -g->off.x, -g->off.y, &q, &r);
    const grid_vec_t p = grid_cell_pos(q, r);
    g->off.x = -p.x;
    g->off.y = -p.y;
    g->vel.x = g->vel.y = 0.0f;
    g->lens = kCenter;
    g->lens_v.x = g->lens_v.y = 0.0f;
    layout(g);
    s_last_off = g->off;
    for (int i = 0; i < GRID_MAX_ITEMS; i++) {
        if (g->cells[i].used && g->cells[i].app == app) {
            return g->cells[i].img;
        }
    }
    return NULL;
}

lv_obj_t *ui_apps_badge(lv_obj_t *drawer)
{
    grid_t *g = grid_of(drawer);
    return g ? g->badge : NULL;
}

int ui_apps_visible(lv_obj_t *drawer, int *apps, int max)
{
    grid_t *g = grid_of(drawer);
    int n = 0;
    for (int i = 0; g && i < GRID_MAX_ITEMS && n < max; i++) {
        if (g->cells[i].used && g->cells[i].opa > 0) {
            apps[n++] = kApps[g->cells[i].app].app;
        }
    }
    return n;
}

bool ui_apps_moving(lv_obj_t *drawer)
{
    grid_t *g = grid_of(drawer);
    return g && !lv_timer_get_paused(g->frame);
}

void ui_apps_reset_view(void)
{
    s_last_off.x = s_last_off.y = 0.0f;
}

/* ---- Gestos en la esfera ----
 *
 * 1) Desde el borde inferior hacia arriba (franja de FACE_PULL_H): el cajón
 *    sube siguiendo al dedo (F11).
 * 2) Hacia ABAJO desde la zona central de la esfera: el cajón baja siguiendo
 *    al dedo. La franja superior (panel rápido) y la inferior tienen sus
 *    propios objetos con PRESS_LOCK, así que este gesto solo nace en el
 *    centro; la esfera marca que el dedo se movió y deja de ofrecer la
 *    pulsación larga del selector (ui_face_moved).
 * El reconocedor (ui_gesture) exige superar el umbral de movimiento con
 * dirección dominante en el sentido del gesto (si el primer movimiento claro
 * es lateral o al revés, se descarta) y al soltar decide con el recorrido
 * (35 %) o la velocidad; si no, el cajón vuelve. */
#define FACE_PULL_H 56

static struct {
    ui_drag_t drag;
    bool opening;
    bool from_top;
} s_pull;

static void pull(lv_event_t *e, bool from_top)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        ui_drag_init(&s_pull.drag, UI_AXIS_Y, from_top ? 1 : -1, FLEX_SCREEN_H);
        ui_drag_press(&s_pull.drag, p.x, p.y, now);
        s_pull.opening = false;
        s_pull.from_top = from_top;
    } else if (code == LV_EVENT_PRESSING) {
        if (s_pull.from_top != from_top || ui_drag_move(&s_pull.drag, p.x, p.y, now) != UI_DRAG_ACTIVE) {
            return;
        }
        if (!s_pull.opening) {
            if (ui_nav_depth() != 1 || ui_nav_busy() || ui_qp_active()) {
                s_pull.drag.state = UI_DRAG_REJECTED;
                return;
            }
            ui_face_moved();
            s_pull.opening = ui_nav_drag_open_begin_dir(ui_apps_create, from_top);
            if (!s_pull.opening) {
                s_pull.drag.state = UI_DRAG_REJECTED;
                return;
            }
        }
        ui_nav_drag_open_move(s_pull.drag.travel);
    } else if (s_pull.from_top == from_top) {
        const ui_drag_result_t r = ui_drag_release(&s_pull.drag, p.x, p.y, now);
        if (s_pull.opening) {
            ui_nav_drag_open_end(r == UI_DRAG_COMMIT, s_pull.drag.travel);
            s_pull.opening = false;
        }
    }
}

static void face_pull_up_cb(lv_event_t *e)
{
    pull(e, false);
}

static void face_pull_down_cb(lv_event_t *e)
{
    pull(e, true);
}

void ui_apps_attach(lv_obj_t *face_page)
{
    lv_obj_t *strip = lv_obj_create(face_page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, FLEX_SCREEN_W, FACE_PULL_H);
    lv_obj_set_pos(strip, 0, FLEX_SCREEN_H - FACE_PULL_H);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(strip, face_pull_up_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(strip, face_pull_up_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(strip, face_pull_up_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(strip, face_pull_up_cb, LV_EVENT_PRESS_LOST, NULL);
    /* Indicio visual: una pequeña asa abajo */
    lv_obj_t *pill = lv_obj_create(strip);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 40, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, flex_color(FLEX_C_TXT2), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_50, 0);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    /* Hacia abajo desde el centro: la propia esfera (que ya es pulsable para
     * la pulsación larga) recibe el arrastre. */
    lv_obj_add_event_cb(face_page, face_pull_down_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face_page, face_pull_down_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face_page, face_pull_down_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face_page, face_pull_down_cb, LV_EVENT_PRESS_LOST, NULL);
}
