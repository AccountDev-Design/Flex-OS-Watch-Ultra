/*
 * ui_banner — aviso emergente en lv_layer_top(): una lámina (FLEX_GLASS_SHEET)
 * del Flex Liquid Glass Engine.
 *
 * Tiempos de la referencia (FlexOS_Ultra_Overlay.h): entrada 220 ms, visible
 * 4200 ms, salida 180 ms, curva de salida cuadrática. Un toque lo cierra. Se
 * crea al mostrarse y se borra al ocultarse (con él, sus animaciones). Si llega
 * otro aviso mientras se ve uno, se sustituye el contenido y se reinicia la
 * espera.
 *
 * Tres clases de aviso con la misma tarjeta: batería (icono de pila),
 * notificación del teléfono (campana, app y "remitente: texto"; tocarlo abre
 * su detalle, ui_notif_open) y actualización del sistema (F7; tocarlo abre
 * Ajustes > Actualizaciones). El de actualización dura 9 s, como la
 * notificación flotante de FlexOS_OTA.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "flex_battery_icon.h"
#include "flex_icons.h"
#include "watch_theme.h"

/* Posición: lo bastante abajo y adentro para que las esquinas del aviso
 * queden dentro de las esquinas redondeadas del panel (flex_safe.h; lo
 * comprueba la vista previa). */
#define B_X        36
#define B_Y        26
#define B_W        (FLEX_SCREEN_W - 2 * B_X)
#define B_PAD_V    14
#define B_H        (2 * B_PAD_V + 28 + 24)   /* título (28) + texto (24): termina antes de la hora */
#define B_RADIUS   22
#define T_IN_MS    220
#define T_SHOW_MS  4200
#define T_OUT_MS   180
#define T_SHOW_OTA_MS 9000

static struct {
    lv_obj_t *card;
    lv_obj_t *icon;
    lv_obj_t *bell;
    lv_obj_t *upd;
    lv_obj_t *info;
    lv_obj_t *title;
    lv_obj_t *text;
    uint32_t notif_id; /* 0 = aviso de batería */
    bool open_ota;     /* tocarlo abre Ajustes > Actualizaciones */
} s_b;

/* Funciones distintas para entrada y salida: lv_anim_start() sustituye una
 * animación existente con el mismo par (objeto, función). */
static void y_in_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void y_out_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void out_done_cb(lv_anim_t *a)
{
    lv_obj_t *card = a->var;
    if (card == s_b.card) {
        s_b.card = NULL;
    }
    lv_obj_delete_async(card);
}

static void start_out(uint32_t delay_ms)
{
    lv_anim_delete(s_b.card, y_out_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_b.card);
    lv_anim_set_exec_cb(&a, y_out_cb);
    lv_anim_set_values(&a, B_Y, -(B_H + 12));
    lv_anim_set_duration(&a, T_OUT_MS);
    lv_anim_set_delay(&a, delay_ms);
    /* Sin early_apply: LVGL 9 aplicaría ya la posición inicial de la salida
     * (y = B_Y) y el aviso aparecería de golpe antes de entrar deslizando. */
    lv_anim_set_early_apply(&a, false);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, out_done_cb);
    lv_anim_start(&a);
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_b.card) {
        const uint32_t id = s_b.notif_id;
        const bool ota = s_b.open_ota;
        lv_anim_delete(s_b.card, y_in_cb);
        start_out(0);
        if ((id || ota) && ui_qp_active()) {
            ui_qp_close(); /* la página que se abre no puede quedar debajo del panel rápido */
        }
        if (id) {
            ui_notif_open(id);
        } else if (ota) {
            ui_nav_push(ui_ota_create);
        }
    }
}

static void build(void)
{
    watch_theme_init();
    /* Lámina del Flex Liquid Glass Engine: vidrio oscuro legible sobre
     * cualquier esfera o página (no es vidrio sobre vidrio: va sola encima). */
    lv_obj_t *card = flex_glass_panel(lv_layer_top(), FLEX_GLASS_SHEET, B_X, -(B_H + 12), B_W, B_H, B_RADIUS);
    lv_obj_set_style_pad_hor(card, 18, 0);
    lv_obj_set_style_pad_ver(card, B_PAD_V, 0);
    flex_glass_interactive(card);
    lv_obj_add_event_cb(card, tap_cb, LV_EVENT_CLICKED, NULL);

    s_b.icon = flex_battery_icon_create(card, 44, 22);
    lv_obj_align(s_b.icon, LV_ALIGN_LEFT_MID, 0, 0);
    s_b.bell = flex_icon_create(card, FLEX_ICON_BELL, 36, FLEX_C_ACCENT);
    lv_obj_align(s_b.bell, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_flag(s_b.bell, LV_OBJ_FLAG_HIDDEN);
    s_b.upd = flex_icon_create(card, FLEX_ICON_UPDATE, 36, FLEX_C_ACCENT);
    lv_obj_align(s_b.upd, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_flag(s_b.upd, LV_OBJ_FLAG_HIDDEN);
    s_b.info = flex_icon_create(card, FLEX_ICON_INFO, 36, FLEX_C_WARN);
    lv_obj_align(s_b.info, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_flag(s_b.info, LV_OBJ_FLAG_HIDDEN);
    /* Título y texto en columna: no se pisan. */
    lv_obj_t *col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, B_W - 36 - 60, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 60, 0);
    s_b.title = watch_theme_label(col, FLEX_TYPE_LABEL, FLEX_GLASS_TXT, "");
    lv_obj_set_size(s_b.title, LV_PCT(100), lv_font_get_line_height(FLEX_TYPE_LABEL) + 4);
    lv_label_set_long_mode(s_b.title, LV_LABEL_LONG_MODE_DOTS);
    s_b.text = watch_theme_label(col, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "Conecta el cargador");
    lv_obj_set_size(s_b.text, LV_PCT(100), lv_font_get_line_height(FLEX_TYPE_CAPTION));
    lv_label_set_long_mode(s_b.text, LV_LABEL_LONG_MODE_DOTS);
    s_b.card = card;
}

/* Crea la tarjeta (entrando) si no existe */
static void ensure_shown(void)
{
    if (!s_b.card) {
        build();
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_b.card);
        lv_anim_set_exec_cb(&a, y_in_cb);
        lv_anim_set_values(&a, -(B_H + 12), B_Y);
        lv_anim_set_duration(&a, T_IN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

void ui_banner_show_notif(const char *app, const char *text, uint32_t notif_id)
{
    ensure_shown();
    s_b.notif_id = notif_id;
    s_b.open_ota = false;
    lv_obj_add_flag(s_b.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.upd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_b.bell, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_b.title, app && app[0] ? app : "Notificación");
    lv_obj_set_style_text_color(s_b.title, flex_color(FLEX_GLASS_TXT), 0);
    lv_label_set_text(s_b.text, text ? text : "");
    start_out(T_IN_MS + T_SHOW_MS);
}

void ui_banner_show_battery(batt_alert_t alert, int percent)
{
    if (alert == BATT_ALERT_NONE) {
        return;
    }
    const bool critical = (alert == BATT_ALERT_CRITICAL);
    ensure_shown();
    s_b.notif_id = 0;
    s_b.open_ota = false;
    lv_obj_remove_flag(s_b.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.bell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.upd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.info, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_b.text, "Conecta el cargador");
    flex_battery_icon_set(s_b.icon, critical ? FLEX_BATT_CRITICAL : FLEX_BATT_LOW, percent);
    const char *title = critical ? "Batería crítica" : "Batería baja";
    if (percent >= 0) {
        lv_label_set_text_fmt(s_b.title, "%s · %d %%", title, percent);
    } else {
        lv_label_set_text(s_b.title, title);
    }
    lv_obj_set_style_text_color(s_b.title, flex_color(critical ? FLEX_C_ERR : FLEX_C_WARN), 0);
    start_out(T_IN_MS + T_SHOW_MS);
}

void ui_banner_show_ota(const char *title, const char *text, uint32_t title_rgb, bool open)
{
    ensure_shown();
    s_b.notif_id = 0;
    s_b.open_ota = open;
    lv_obj_add_flag(s_b.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.bell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_b.upd, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_b.title, title);
    lv_obj_set_style_text_color(s_b.title, flex_color(title_rgb), 0);
    lv_label_set_text(s_b.text, text ? text : "");
    start_out(T_IN_MS + T_SHOW_OTA_MS);
}

void ui_banner_show_info(const char *title, const char *text)
{
    ensure_shown();
    s_b.notif_id = 0;
    s_b.open_ota = false;
    lv_obj_add_flag(s_b.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.bell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_b.upd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_b.info, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_b.title, title ? title : "");
    lv_obj_set_style_text_color(s_b.title, flex_color(FLEX_C_WARN), 0);
    lv_label_set_text(s_b.text, text ? text : "");
    start_out(T_IN_MS + T_SHOW_MS);
}

bool ui_banner_active(void)
{
    return s_b.card != NULL;
}
