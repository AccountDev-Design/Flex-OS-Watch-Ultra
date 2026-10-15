/*
 * ui_qp — panel rápido: cortina que baja desde el borde superior de la
 * esfera siguiendo al dedo.
 *
 * Solo lleva controles con función real:
 *  - Brillo: control real del panel (DCS 0x51), mínimo 10 %, con el envío
 *    limitado durante el arrastre. No se guarda hasta F4.1 (NVS).
 *  - Ajustes: cierra el panel y abre la página de Ajustes.
 *  - Batería: el % y el estado reales (de la caché de ui_system); abre
 *    Ajustes > Batería.
 *  - Wi-Fi y Bluetooth: marcados "Pendiente" (F5); no reciben toques.
 *
 * Gestos (ui_gesture.c): arrastrar hacia abajo desde la franja superior de
 * la esfera abre; arrastrar hacia arriba en el panel o tocar el asa inferior
 * cierra. Al soltar, la cortina termina de abrirse o vuelve, con la
 * duración que corresponde a lo que falta (base 210/190 ms).
 *
 * El panel vive en lv_layer_top() mientras se ve y se borra al cerrarse
 * (con él, su temporizador y sus animaciones).
 */
#include "ui_internal.h"

#include "flex_battery_icon.h"
#include "ui_gesture.h"
#include "ui_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

#define QP_W        FLEX_SCREEN_W
#define QP_H        FLEX_SCREEN_H
#define QP_PULL_H   64          /* franja de la esfera que abre el panel */
#define QP_TICK_MS  1000

/* Geometría del contenido (px del panel de 410x502) */
#define TILE_X0     16
#define TILE_GAP    12
#define TILE_W      ((QP_W - 2 * TILE_X0 - TILE_GAP) / 2)
#define TILE_H      104
#define TILE_Y0     72
#define TILE_Y1     (TILE_Y0 + TILE_H + TILE_GAP)
#define SLIDER_Y    (TILE_Y1 + TILE_H + 16)
#define HANDLE_H    56          /* zona táctil del asa inferior */

typedef enum {
    QP_CLOSED = 0,
    QP_DRAG_OPEN,
    QP_OPENING,
    QP_OPEN,
    QP_DRAG_CLOSE,
    QP_CLOSING,
} qp_state_t;

static struct {
    qp_state_t st;
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *batt_icon;
    lv_obj_t *batt_pct;
    lv_obj_t *batt_tile_icon;
    lv_obj_t *batt_tile_pct;
    lv_obj_t *batt_tile_state;
    lv_timer_t *timer;
    ui_drag_t drag;
    ui_page_create_t after_close; /* página que se abre al terminar de cerrar */
    int64_t last_time_key;
    int last_pct;
    int last_state;
    int last_level;
} s_qp;

/* ---- Contenido ---- */

static void update(void)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    ui_face_time_t t;
    ui_face_time(st, st == WATCH_TIME_OK ? &dt : NULL, &t);
    if (t.key != s_qp.last_time_key) {
        s_qp.last_time_key = t.key;
        lv_label_set_text(s_qp.time, t.time);
        char d[24];
        if (t.valid) {
            wt_fmt_date_short(d, sizeof(d), &dt);
        } else {
            lv_snprintf(d, sizeof(d), "%s", "");
        }
        lv_label_set_text(s_qp.date, d);
    }

}

static void update_battery(const watch_power_snapshot_t *snap)
{
    const bool have = snap != NULL;
    const batt_state_t bst = have ? snap->track.state : BATT_STATE_UNKNOWN;
    const batt_level_t blv = have ? snap->track.level : BATT_LEVEL_UNKNOWN;
    const int pct = have ? snap->track.percent : -1;
    if (pct == s_qp.last_pct && (int)bst == s_qp.last_state && (int)blv == s_qp.last_level) {
        return;
    }
    s_qp.last_pct = pct;
    s_qp.last_state = (int)bst;
    s_qp.last_level = (int)blv;
    const flex_batt_kind_t kind = ui_batt_kind(bst, blv);
    flex_battery_icon_set(s_qp.batt_icon, kind, pct);
    flex_battery_icon_set(s_qp.batt_tile_icon, kind, pct);
    if (pct >= 0) {
        lv_label_set_text_fmt(s_qp.batt_pct, "%d %%", pct);
        lv_label_set_text_fmt(s_qp.batt_tile_pct, "%d %%", pct);
    } else {
        lv_label_set_text(s_qp.batt_pct, "--");
        lv_label_set_text(s_qp.batt_tile_pct, "--");
    }
    lv_label_set_text(s_qp.batt_tile_state, ui_batt_state_text(bst));
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    update();
}

static void power_cb(lv_obj_t *root, const watch_power_snapshot_t *snap)
{
    (void)root;
    update_battery(snap);
}

static lv_obj_t *tile(lv_obj_t *parent, int32_t x, int32_t y, flex_icon_t icon, uint32_t accent,
                      const char *title, const char *sub, lv_obj_t **sub_out)
{
    lv_obj_t *t = watch_theme_tile(parent, x, y, TILE_W, TILE_H);
    lv_obj_t *ic = flex_icon_create(t, icon, 28, accent);
    lv_obj_set_pos(ic, 0, 0);
    lv_obj_t *l = watch_theme_label(t, FLEX_FONT_BODY, FLEX_C_TXT, title);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, TILE_W - 32 - 40);
    lv_obj_set_pos(l, 40, -1);
    lv_obj_t *s = watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, sub);
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s, TILE_W - 32);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    if (sub_out) {
        *sub_out = s;
    }
    return t;
}

static void tile_pending(lv_obj_t *t)
{
    lv_obj_add_state(t, LV_STATE_DISABLED);
    flex_icon_set_color(lv_obj_get_child(t, 0), FLEX_C_MUTE);
    lv_obj_set_style_text_color(lv_obj_get_child(t, 1), flex_color(FLEX_C_MUTE), 0);
    lv_label_set_text(lv_obj_get_child(t, 2), "Pendiente");
    lv_obj_set_style_text_color(lv_obj_get_child(t, 2), flex_color(FLEX_C_MUTE), 0);
}

/* ---- Animación de la cortina ---- */

static void destroy(void)
{
    if (s_qp.timer) {
        lv_timer_delete(s_qp.timer);
        s_qp.timer = NULL;
    }
    if (s_qp.root) {
        lv_obj_delete_async(s_qp.root);
        s_qp.root = NULL;
    }
    s_qp.st = QP_CLOSED;
}

static void y_cb(void *var, int32_t v)
{
    (void)var;
    lv_obj_set_y(s_qp.root, v);
}

static void anim_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_qp.st == QP_OPENING) {
        s_qp.st = QP_OPEN;
        return;
    }
    if (s_qp.st == QP_CLOSING) {
        ui_page_create_t next = s_qp.after_close;
        s_qp.after_close = NULL;
        destroy();
        if (next) {
            ui_nav_push(next);
        }
    }
}

static void animate_to(bool open, uint32_t ms)
{
    s_qp.st = open ? QP_OPENING : QP_CLOSING;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_qp); /* no a la cortina: se borra de forma asíncrona */
    lv_anim_set_exec_cb(&a, y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_qp.root), open ? 0 : -QP_H);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, anim_done_cb);
    lv_anim_start(&a);
}

/* ---- Gestos ---- */

static void close_drag_cb(lv_event_t *e);
static void handle_cb(lv_event_t *e);
static void settings_cb(lv_event_t *e);
static void battery_cb(lv_event_t *e);

static void build(void)
{
    watch_theme_init();
    s_qp.last_time_key = INT64_MIN;
    s_qp.last_pct = -2;
    s_qp.last_state = -1;
    s_qp.last_level = -1;

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, QP_W, QP_H);
    lv_obj_set_pos(root, 0, -QP_H);
    /* Velo opaco. La referencia deja pasar algo de fondo porque detrás hay
     * un fondo desenfocado; aquí no se desenfoca (coste) y con 245/255 la
     * esfera se leía como un fantasma a través de los controles (vista
     * previa). Opaco, además, LVGL deja de dibujar la esfera bajo el panel. */
    lv_obj_set_style_bg_color(root, flex_color(FLEX_C_PAGE), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, FLEX_RADIUS_CARD, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESS_LOST, NULL);
    s_qp.root = root;

    /* Cabecera: hora, fecha corta y batería */
    s_qp.time = watch_theme_label(root, FLEX_FONT_TITLE, FLEX_C_TXT, "--:--");
    lv_obj_set_pos(s_qp.time, FLEX_SAFE_X, FLEX_SAFE_TOP + 8);
    s_qp.date = watch_theme_label(root, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_align_to(s_qp.date, s_qp.time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -3);
    s_qp.batt_icon = flex_battery_icon_create(root, 37, 17);
    lv_obj_align(s_qp.batt_icon, LV_ALIGN_TOP_RIGHT, -FLEX_SAFE_X, FLEX_SAFE_TOP + 20);
    s_qp.batt_pct = watch_theme_label(root, FLEX_FONT_SMALL, FLEX_C_TXT, "--");
    lv_obj_align(s_qp.batt_pct, LV_ALIGN_TOP_RIGHT, -(FLEX_SAFE_X + 37 + 8), FLEX_SAFE_TOP + 16);

    /* Mosaicos */
    lv_obj_t *wifi = tile(root, TILE_X0, TILE_Y0, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Wi-Fi", "", NULL);
    tile_pending(wifi);
    lv_obj_t *bt = tile(root, TILE_X0 + TILE_W + TILE_GAP, TILE_Y0, FLEX_ICON_BLUETOOTH, FLEX_C_CAT_NET,
                        "Bluetooth", "", NULL);
    tile_pending(bt);
    lv_obj_t *batt = tile(root, TILE_X0, TILE_Y1, FLEX_ICON_BOLT, FLEX_C_CAT_GREEN, "--", "",
                          &s_qp.batt_tile_state);
    s_qp.batt_tile_pct = lv_obj_get_child(batt, 1);
    lv_obj_add_event_cb(batt, battery_cb, LV_EVENT_CLICKED, NULL); /* abre Ajustes > Batería */
    /* El rayo diría "cargando" aunque descargue: el mosaico usa el indicador
     * real, el mismo de la esfera. */
    lv_obj_delete(lv_obj_get_child(batt, 0));
    s_qp.batt_tile_icon = flex_battery_icon_create(batt, 32, 16);
    lv_obj_move_to_index(s_qp.batt_tile_icon, 0);
    lv_obj_set_pos(s_qp.batt_tile_icon, 0, 6);
    lv_obj_t *set = tile(root, TILE_X0 + TILE_W + TILE_GAP, TILE_Y1, FLEX_ICON_GEAR, FLEX_C_CAT_BLUE,
                         "Ajustes", "Pantalla, batería…", NULL);
    lv_obj_add_event_cb(set, settings_cb, LV_EVENT_CLICKED, NULL);

    ui_bright_create(root, TILE_X0, SLIDER_Y, QP_W - 2 * TILE_X0);

    /* Asa inferior: tocarla cierra el panel */
    lv_obj_t *handle = lv_obj_create(root);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, QP_W, HANDLE_H);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle, handle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pill = lv_obj_create(handle);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 44, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, flex_color(FLEX_C_TXT2), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);

    update();
    update_battery(NULL);
    ui_power_listen(root, power_cb);
    s_qp.timer = lv_timer_create(tick_cb, QP_TICK_MS, NULL);
}

static void close_drag_cb(lv_event_t *e)
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
        if (s_qp.st == QP_OPEN) {
            ui_drag_init(&s_qp.drag, UI_AXIS_Y, -1, QP_H);
            ui_drag_press(&s_qp.drag, p.x, p.y, now);
        }
    } else if (code == LV_EVENT_PRESSING) {
        if ((s_qp.st == QP_OPEN || s_qp.st == QP_DRAG_CLOSE) &&
            ui_drag_move(&s_qp.drag, p.x, p.y, now) == UI_DRAG_ACTIVE) {
            s_qp.st = QP_DRAG_CLOSE;
            lv_obj_set_y(s_qp.root, -s_qp.drag.travel);
        }
    } else {
        const ui_drag_result_t r = ui_drag_release(&s_qp.drag, p.x, p.y, now);
        if (s_qp.st != QP_DRAG_CLOSE) {
            return;
        }
        const bool close = (r == UI_DRAG_COMMIT);
        animate_to(!close, close ? ui_drag_remaining_ms(s_qp.drag.travel, QP_H, FLEX_ANIM_APP_CLOSE_MS, true)
                                 : ui_drag_remaining_ms(s_qp.drag.travel, QP_H, FLEX_ANIM_APP_OPEN_MS, false));
    }
}

static void open_drag_cb(lv_event_t *e)
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
        if (s_qp.st == QP_CLOSED && !ui_nav_busy() && ui_nav_depth() == 1) {
            ui_drag_init(&s_qp.drag, UI_AXIS_Y, +1, QP_H);
            ui_drag_press(&s_qp.drag, p.x, p.y, now);
        }
    } else if (code == LV_EVENT_PRESSING) {
        if ((s_qp.st == QP_CLOSED || s_qp.st == QP_DRAG_OPEN) &&
            ui_drag_move(&s_qp.drag, p.x, p.y, now) == UI_DRAG_ACTIVE) {
            if (s_qp.st == QP_CLOSED) {
                build();
                s_qp.st = QP_DRAG_OPEN;
            }
            lv_obj_set_y(s_qp.root, -QP_H + s_qp.drag.travel);
        }
    } else {
        const ui_drag_result_t r = ui_drag_release(&s_qp.drag, p.x, p.y, now);
        if (s_qp.st != QP_DRAG_OPEN) {
            return;
        }
        const bool open = (r == UI_DRAG_COMMIT);
        animate_to(open, open ? ui_drag_remaining_ms(s_qp.drag.travel, QP_H, FLEX_ANIM_APP_OPEN_MS, true)
                              : ui_drag_remaining_ms(s_qp.drag.travel, QP_H, FLEX_ANIM_APP_CLOSE_MS, false));
    }
}

static void handle_cb(lv_event_t *e)
{
    (void)e;
    ui_qp_close();
}

static void settings_cb(lv_event_t *e)
{
    (void)e;
    if (s_qp.st == QP_OPEN) {
        s_qp.after_close = ui_settings_create;
        animate_to(false, FLEX_ANIM_APP_CLOSE_MS);
    }
}

static void battery_cb(lv_event_t *e)
{
    (void)e;
    if (s_qp.st == QP_OPEN) {
        s_qp.after_close = ui_battery_page_create;
        animate_to(false, FLEX_ANIM_APP_CLOSE_MS);
    }
}

/* ---- API ---- */

void ui_qp_attach(lv_obj_t *face_page)
{
    /* Franja superior de la esfera (la esfera no tiene controles ahí). */
    lv_obj_t *strip = lv_obj_create(face_page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, QP_W, QP_PULL_H);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(strip, open_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(strip, open_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(strip, open_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(strip, open_drag_cb, LV_EVENT_PRESS_LOST, NULL);

    /* Indicio visual: una pequeña asa arriba */
    lv_obj_t *pill = lv_obj_create(strip);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 40, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, flex_color(FLEX_C_TXT2), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_50, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
}

void ui_qp_open(void)
{
    if (s_qp.st != QP_CLOSED || ui_nav_busy() || ui_nav_depth() != 1) {
        return;
    }
    build();
    animate_to(true, FLEX_ANIM_APP_OPEN_MS);
}

void ui_qp_close(void)
{
    if (s_qp.st == QP_OPEN || s_qp.st == QP_OPENING) {
        lv_anim_delete(&s_qp, y_cb);
        animate_to(false, FLEX_ANIM_APP_CLOSE_MS);
    }
}

bool ui_qp_is_open(void)
{
    return s_qp.st == QP_OPEN;
}

bool ui_qp_active(void)
{
    return s_qp.st != QP_CLOSED;
}

lv_obj_t *ui_qp_obj(void)
{
    return s_qp.root;
}
