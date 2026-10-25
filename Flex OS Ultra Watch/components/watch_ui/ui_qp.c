/*
 * ui_qp — panel rápido: cortina que baja desde el borde superior de la
 * esfera siguiendo al dedo.
 *
 * Rediseño 2026-10 (diseño del usuario: design/referencias/panel_rapido.png):
 * degradado turquesa-verde, cabecera con hora, fecha y batería, seis mosaicos
 * de vidrio claro solo con icono y la barra de brillo con relleno cian. Cada
 * mosaico enseña el estado REAL de su servicio: activo = relleno cian
 * (LV_STATE_CHECKED), no disponible = más transparente (LV_STATE_DISABLED).
 * Solo lleva controles con función real:
 *  - Wi-Fi: tocarlo lo enciende o lo apaga (watch_net, F5.1).
 *  - Bluetooth: enciende o apaga el enlace con Flex Phone (watch_phone, F5.5).
 *  - Ajustes: cierra el panel y abre la página de Ajustes.
 *  - AOD (luna): pantalla siempre activa (ui_aod.c); no disponible con el ahorro.
 *  - Ahorro (batería con hoja): Flex Ultra Power Saving (ui_psave.c).
 *  - Reloj: cierra el panel y abre la app Reloj.
 *  - Brillo: control real del panel (DCS 0x51), mínimo 10 %, con el envío
 *    limitado durante el arrastre (ui_bright.c).
 *  - Batería de la cabecera: % y estado reales (de la caché de ui_system; el
 *    icono lleva el rayo o el enchufe); tocarla abre Ajustes > Batería.
 *
 * Gestos (ui_gesture.c): arrastrar hacia abajo desde la franja superior de
 * la esfera, o desde el borde superior de CUALQUIER página (ui_nav), abre;
 * arrastrar hacia arriba en el panel o tocar el asa inferior cierra. La
 * página de debajo no se toca: al cerrar sigue igual. Al soltar, la cortina
 * termina de abrirse o vuelve, con la duración que corresponde a lo que
 * falta (base 210/190 ms).
 *
 * El panel es hijo de la pantalla de navegación, encima de las páginas
 * (ui_overlay_parent: así LVGL no dibuja la parte de la esfera que tapa), y
 * se borra al cerrarse (con él, su temporizador y sus animaciones).
 */
#include "ui_internal.h"

#include <string.h>

#include "flex_battery_icon.h"
#include "flex_safe.h"
#include "ui_gesture.h"
#include "ui_logic.h"
#include "watch_net.h"
#include "watch_phone.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define QP_W        FLEX_SCREEN_W
#define QP_H        FLEX_SCREEN_H
#define QP_PULL_H   64          /* franja de la esfera que abre el panel */
#define QP_TICK_MS  1000

/* Geometría del contenido (px del panel de 410x502, medida en la referencia;
 * la cabecera baja hasta donde las esquinas redondeadas dejan verla) */
#define HDR_Y       22          /* fila de hora y batería */
#define TILE_X0     30
#define TILE_GAP    10
#define TILE_W      170
#define TILE_H      98
#define TILE_R      38
#define TILE_Y0     66
#define TILE_DY     (TILE_H + 10)
#define SLIDER_Y    (TILE_Y0 + 3 * TILE_DY + 4)
#define SLIDER_H    60
#define HANDLE_H    44          /* zona táctil del asa inferior */
#define C_CLOCK_HANDS 0x63C5B4  /* agujas del reloj macizo: el tono del mosaico */

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
    lv_obj_t *hdr;
    lv_obj_t *hdr_left;
    lv_obj_t *hdr_right;
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *batt_icon;
    lv_obj_t *batt_pct;
    lv_obj_t *tile[UI_QP_TILES];
    lv_obj_t *aod_icon;
    lv_timer_t *timer;
    ui_drag_t drag;
    ui_page_create_t after_close; /* página que se abre al terminar de cerrar */
    int64_t last_time_key;
    int last_pct;
    int last_state;
    int last_level;
} s_qp;

/* ---- Contenido ---- */

/* La fecha corta se muestra solo si cabe entre la hora y la batería (en
 * 12 h, "12:59 PM" con "mié, 30 sep" y "100 %" no caben con aire). */
#define HDR_MIN_GAP 16

static void fit_header(void)
{
    if (!s_qp.hdr) {
        return;
    }
    lv_obj_remove_flag(s_qp.date, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_qp.hdr);
    const int32_t need = lv_obj_get_width(s_qp.hdr_left) + lv_obj_get_width(s_qp.hdr_right) + HDR_MIN_GAP;
    if (need > lv_obj_get_content_width(s_qp.hdr)) {
        lv_obj_add_flag(s_qp.date, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_checked(lv_obj_t *o, bool on)
{
    if (o && on != lv_obj_has_state(o, LV_STATE_CHECKED)) {
        if (on) {
            lv_obj_add_state(o, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(o, LV_STATE_CHECKED);
        }
    }
}

static void update(void)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);
    if (t.key != s_qp.last_time_key) {
        s_qp.last_time_key = t.key;
        if (t.ampm[0]) {
            lv_label_set_text_fmt(s_qp.time, "%s %s", t.time, t.ampm); /* barra: "9:41 PM" */
        } else {
            lv_label_set_text(s_qp.time, t.time);
        }
        char d[24];
        if (t.valid) {
            wt_fmt_date_short(d, sizeof(d), &dt);
        } else {
            lv_snprintf(d, sizeof(d), "%s", "");
        }
        lv_label_set_text(s_qp.date, d);
        fit_header();
    }
    if (!s_qp.root) {
        return;
    }
    watch_net_status_t ns;
    watch_net_get(&ns);
    set_checked(s_qp.tile[UI_QP_WIFI], ns.enabled);
    watch_phone_status_t ps;
    watch_phone_get(&ps);
    set_checked(s_qp.tile[UI_QP_BT], ps.enabled);
    const bool on = ui_psave_active();
    set_checked(s_qp.tile[UI_QP_PSAVE], on);
    set_checked(s_qp.tile[UI_QP_AOD], ui_aod_enabled());
    if (on != lv_obj_has_state(s_qp.tile[UI_QP_AOD], LV_STATE_DISABLED)) {
        if (on) {
            lv_obj_add_state(s_qp.tile[UI_QP_AOD], LV_STATE_DISABLED); /* no con el ahorro */
        } else {
            lv_obj_remove_state(s_qp.tile[UI_QP_AOD], LV_STATE_DISABLED);
        }
        flex_icon_set_color(s_qp.aod_icon, on ? 0xB8E6DC : FLEX_C_WHITE);
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
    if (pct >= 0) {
        lv_label_set_text_fmt(s_qp.batt_pct, "%d %%", pct);
    } else {
        lv_label_set_text(s_qp.batt_pct, "--");
    }
    fit_header();
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

/* Mosaico de vidrio claro con un icono blanco centrado. */
static lv_obj_t *tile(lv_obj_t *parent, int i, flex_icon_t icon, int32_t size, lv_event_cb_t cb)
{
    const int32_t x = TILE_X0 + (i % 2) * (TILE_W + TILE_GAP), y = TILE_Y0 + (i / 2) * TILE_DY;
    lv_obj_t *t = watch_theme_lg_button(parent, FLEX_LG_LIGHT, x, y, TILE_W, TILE_H, TILE_R, FLEX_GLOW_NONE);
    lv_obj_t *ic = flex_icon_create(t, icon, size, FLEX_C_WHITE);
    lv_obj_center(ic);
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, NULL);
    return t;
}

/* Contenedor transparente en fila (flex), sin desplazamiento ni toques. */
static lv_obj_t *row_box(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_layout(b, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}

/* Wi-Fi: tocar el mosaico lo enciende o lo apaga (QSID_WIFI es QT_TOGGLE).
 * Respuesta en el mismo toque; el refresco (1 s) pone después el estado real. */
static void wifi_cb(lv_event_t *e)
{
    (void)e;
    watch_net_status_t ns;
    watch_net_get(&ns);
    watch_net_set_enabled(!ns.enabled);
    set_checked(s_qp.tile[UI_QP_WIFI], !ns.enabled);
}

/* Bluetooth: tocar el mosaico enciende o apaga el enlace con Flex Phone. */
static void bt_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_status_t ps;
    watch_phone_get(&ps);
    watch_phone_set_enabled(!ps.enabled);
    set_checked(s_qp.tile[UI_QP_BT], !ps.enabled);
}

/* Flex Ultra Power Saving: el estado real lo pone update() en el mismo toque. */
static void psave_cb(lv_event_t *e)
{
    (void)e;
    ui_psave_set(!ui_psave_active());
    update();
}

/* AOD: se guarda en los ajustes; ui_screen lo usa al apagarse la pantalla. */
static void aod_cb(lv_event_t *e)
{
    (void)e;
    if (ui_psave_active()) {
        return;
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.aod = !ws.aod;
    watch_settings_update(&ws);
    update();
}

/* ---- Animación de la cortina ---- */

static void destroy(void)
{
    if (s_qp.timer) {
        lv_timer_delete(s_qp.timer);
        s_qp.timer = NULL;
    }
    memset(s_qp.tile, 0, sizeof(s_qp.tile));
    s_qp.aod_icon = NULL;
    if (s_qp.root) {
        lv_obj_delete_async(s_qp.root);
        s_qp.root = NULL;
        s_qp.hdr = NULL;
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
static void clock_cb(lv_event_t *e);

static void build(void)
{
    watch_theme_init();
    s_qp.last_time_key = INT64_MIN;
    s_qp.last_pct = -2;
    s_qp.last_state = -1;
    s_qp.last_level = -1;

    lv_obj_t *root = lv_obj_create(ui_overlay_parent());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, QP_W, QP_H);
    lv_obj_set_pos(root, 0, -QP_H);
    /* Degradado horizontal opaco: LVGL no dibuja la esfera bajo el panel.
     * Sin radio: un rectángulo opaco que cubre la pantalla entera (con radio,
     * las esquinas obligaban a redibujar la esfera en cada cuadro). Una línea
     * abajo marca el borde mientras baja siguiendo al dedo. */
    lv_obj_set_style_bg_color(root, flex_color(FLEX_QP_GRAD_L), 0);
    lv_obj_set_style_bg_grad_color(root, flex_color(FLEX_QP_GRAD_R), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_border_side(root, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(root, flex_color(FLEX_C_WHITE), 0);
    lv_obj_set_style_border_opa(root, FLEX_LG_LIGHT_EDGE_OPA, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, close_drag_cb, LV_EVENT_PRESS_LOST, NULL);
    s_qp.root = root;

    /* Cabecera: hora y fecha corta a la izquierda, % y batería a la
     * derecha, dentro de lo que dejan ver las esquinas redondeadas a esa
     * altura (en la foto del reloj el icono de batería quedaba cortado). */
    const int32_t hh = lv_font_get_line_height(FLEX_FONT_S32);
    const int32_t hx = flex_safe_left(HDR_Y, HDR_Y + hh - 1, FLEX_SAFE_MARGIN);
    lv_obj_t *hdr = row_box(root);
    s_qp.hdr = hdr;
    lv_obj_set_pos(hdr, hx, HDR_Y);
    lv_obj_set_size(hdr, flex_safe_right(HDR_Y, HDR_Y + hh - 1, FLEX_SAFE_MARGIN) + 1 - hx, hh);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *left = row_box(hdr);
    s_qp.hdr_left = left;
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(left, 10, 0);
    s_qp.time = watch_theme_label(left, FLEX_FONT_S32, FLEX_C_WHITE, "--:--");
    s_qp.date = watch_theme_label(left, FLEX_FONT_S20, FLEX_C_WHITE, "");
    lv_obj_set_style_pad_bottom(s_qp.date, 4, 0);
    lv_obj_t *right = row_box(hdr);
    s_qp.hdr_right = right;
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(right, 6, 0);
    lv_obj_add_flag(right, LV_OBJ_FLAG_CLICKABLE); /* abre Ajustes > Batería */
    lv_obj_set_ext_click_area(right, 10);
    lv_obj_add_event_cb(right, battery_cb, LV_EVENT_CLICKED, NULL);
    s_qp.batt_pct = watch_theme_label(right, FLEX_FONT_S32, FLEX_C_WHITE, "--");
    s_qp.batt_icon = flex_battery_icon_create(right, 46, 22);
    flex_battery_icon_set_mono(s_qp.batt_icon, true);

    /* Mosaicos (orden de la referencia) */
    s_qp.tile[UI_QP_WIFI] = tile(root, 0, FLEX_ICON_WIFI, 66, wifi_cb);
    s_qp.tile[UI_QP_BT] = tile(root, 1, FLEX_ICON_BLUETOOTH, 70, bt_cb);
    s_qp.tile[UI_QP_SETTINGS] = tile(root, 2, FLEX_ICON_GEAR_SOLID, 64, settings_cb);
    s_qp.tile[UI_QP_AOD] = tile(root, 3, FLEX_ICON_MOON_SOLID, 60, aod_cb);
    s_qp.aod_icon = lv_obj_get_child(s_qp.tile[UI_QP_AOD], 0);
    s_qp.tile[UI_QP_PSAVE] = tile(root, 4, FLEX_ICON_BATT_LEAF, 84, psave_cb);
    flex_icon_set_color2(lv_obj_get_child(s_qp.tile[UI_QP_PSAVE], 0), C_CLOCK_HANDS); /* nervios de la hoja */
    s_qp.tile[UI_QP_CLOCK] = tile(root, 5, FLEX_ICON_CLOCK_SOLID, 64, clock_cb);
    flex_icon_set_color2(lv_obj_get_child(s_qp.tile[UI_QP_CLOCK], 0), C_CLOCK_HANDS);

    ui_bright_create_light(root, TILE_X0, SLIDER_Y, 2 * TILE_W + TILE_GAP, SLIDER_H);

    /* Asa inferior: tocarla cierra el panel; arrastrar desde ella hacia
     * arriba también (los eventos suben a la cortina). */
    lv_obj_t *handle = lv_obj_create(root);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, QP_W, HANDLE_H);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(handle, handle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pill = lv_obj_create(handle);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 44, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, flex_color(FLEX_C_WHITE), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
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

void ui_qp_pull_cb(lv_event_t *e)
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
        if (s_qp.st == QP_CLOSED && !ui_nav_busy()) {
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

static void clock_cb(lv_event_t *e)
{
    (void)e;
    if (s_qp.st == QP_OPEN) {
        s_qp.after_close = ui_reloj_create;
        animate_to(false, FLEX_ANIM_APP_CLOSE_MS);
    }
}

/* ---- API ---- */

lv_obj_t *ui_qp_tile(ui_qp_tile_t t)
{
    return t >= 0 && t < UI_QP_TILES ? s_qp.tile[t] : NULL;
}

lv_obj_t *ui_qp_battery(void)
{
    return s_qp.hdr_right && s_qp.root ? s_qp.hdr_right : NULL;
}


void ui_qp_attach(lv_obj_t *face_page)
{
    /* Franja superior de la esfera (la esfera no tiene controles ahí). */
    lv_obj_t *strip = lv_obj_create(face_page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, QP_W, QP_PULL_H);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(strip, ui_qp_pull_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(strip, ui_qp_pull_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(strip, ui_qp_pull_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(strip, ui_qp_pull_cb, LV_EVENT_PRESS_LOST, NULL);

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
    if (s_qp.st != QP_CLOSED || ui_nav_busy()) {
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
