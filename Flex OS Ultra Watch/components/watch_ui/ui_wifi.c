/*
 * ui_wifi — Ajustes > Wi-Fi (F5.1) y la página de contraseña.
 *
 * Categoría "Red e Internet" de Ajustes de Flex OS Ultra (AppSettings.h:78,
 * "WiFi, Bluetooth"), con las filas de tarjeta del tema:
 *  - interruptor del Wi-Fi (la radio está apagada hasta que se enciende);
 *  - estado real del servicio watch_net ("Conectado a Casa", "Contraseña
 *    incorrecta", "Red no encontrada · reintento en 4 s"...), IP y si la hora
 *    se sincronizó por Internet;
 *  - redes del último escaneo (guardadas primero, después por señal) y
 *    redes guardadas, que se olvidan con dos toques.
 * La página no toca la radio: manda órdenes a watch_net y lee su instantánea
 * cada 500 ms. Nada se bloquea mientras conecta.
 *
 * Teclado: el compartido del sistema (ui_keyboard.c) en modo ASCII: todo el
 * ASCII imprimible de una contraseña Wi-Fi en 4 páginas.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_net.h"
#include "watch_theme.h"

#define WIFI_TICK_MS 500
#define FORGET_MS    3000

typedef struct {
    lv_obj_t *list;
    lv_obj_t *sw_row;
    lv_obj_t *sw;
    lv_obj_t *state;
    lv_obj_t *ip;
    lv_obj_t *time;
    lv_obj_t *nets;       /* contenedor de las redes (se rehace) */
    uint32_t nets_key;    /* scan_gen + redes guardadas + estado: si cambia, se rehace */
    char forget[NET_SSID_MAX + 1];
    uint32_t forget_at;
} wifi_page_t;

static char s_pw_ssid[NET_SSID_MAX + 1]; /* red para la página de contraseña */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* "Conectado a Casa", "Contraseña incorrecta", "Red no encontrada · 4 s"... */
static void state_text(const watch_net_status_t *s, char *b, size_t n)
{
    const char *t = watch_net_state_text(s);
    if ((s->state == NET_CONNECTED || s->state == NET_CONNECTING) && s->ssid[0]) {
        snprintf(b, n, "%s a %s", t, s->ssid);
    } else if (s->state == NET_FAILED && s->retry_in_s > 0) {
        snprintf(b, n, "%s · %d s", t, s->retry_in_s);
    } else {
        snprintf(b, n, "%s", t);
    }
}

static uint32_t hash_str(uint32_t h, const char *s)
{
    while (*s) {
        h = h * 31u + (uint8_t)*s++;
    }
    return h * 31u;
}

static void switch_style(lv_obj_t *sw)
{
    lv_obj_set_size(sw, 56, 32);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_C_WHITE), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE); /* toca la fila entera */
}

/* ---- Contraseña ---- */

typedef struct {
    lv_obj_t *ta;
} pw_page_t;

static void pw_connect(void *ud)
{
    pw_page_t *p = ud;
    watch_net_connect(s_pw_ssid, lv_textarea_get_text(p->ta));
    ui_nav_back();
}

static void show_cb(lv_event_t *e)
{
    pw_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    const bool hide = !lv_textarea_get_password_mode(p->ta);
    lv_textarea_set_password_mode(p->ta, hide);
    watch_theme_button_set(b, hide ? "Ver" : "Ocultar", 0);
}

static void pw_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_wifi_password_create(lv_obj_t *parent)
{
    pw_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Contraseña", back_cb, NULL);

    lv_obj_t *net = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, s_pw_ssid);
    lv_label_set_long_mode(net, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(net, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 16);
    lv_obj_set_pos(net, FLEX_SAFE_X + 8, FLEX_HDR_H - 4);

    p->ta = lv_textarea_create(page);
    lv_textarea_set_one_line(p->ta, true);
    lv_textarea_set_password_mode(p->ta, true);
    lv_textarea_set_password_bullet(p->ta, "*");
    lv_textarea_set_password_show_time(p->ta, 0); /* para verla está "Ver"; sin animación pendiente */
    lv_textarea_set_max_length(p->ta, NET_PASS_MAX);
    lv_textarea_set_placeholder_text(p->ta, "Contraseña de la red");
    lv_obj_set_size(p->ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 100, 56);
    lv_obj_set_pos(p->ta, FLEX_SAFE_X, FLEX_HDR_H + 22);
    ui_textarea_style(p->ta);

    lv_obj_t *show = watch_theme_button(page, "Ver", FLEX_C_SURF2, 88, 56);
    lv_obj_set_pos(show, FLEX_SCREEN_W - FLEX_SAFE_X - 88, FLEX_HDR_H + 22);
    lv_obj_add_event_cb(show, show_cb, LV_EVENT_CLICKED, p);

    lv_obj_t *hint = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       "La red se guarda solo si la conexión funciona.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(hint, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 16);
    lv_obj_set_pos(hint, FLEX_SAFE_X + 8, FLEX_HDR_H + 92);

    lv_obj_t *kb = ui_keyboard_create(page, p->ta, UI_KB_ASCII, pw_connect, p);
    lv_obj_set_size(kb, FLEX_SCREEN_W - 2 * 16, 452 - 236);
    lv_obj_set_pos(kb, 16, 236);

    lv_obj_add_event_cb(page, pw_free_cb, LV_EVENT_DELETE, p);
    return page;
}

/* ---- Página Wi-Fi ---- */

static void switch_cb(lv_event_t *e)
{
    (void)e;
    watch_net_status_t s;
    watch_net_get(&s);
    watch_net_set_enabled(!s.enabled);
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    watch_net_scan();
}

static void net_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    const net_ap_t *ap = lv_obj_get_user_data(row);
    if (!ap) {
        return;
    }
    if (ap->saved || !ap->secure) {
        watch_net_connect(ap->ssid, ap->saved ? NULL : "");
    } else {
        snprintf(s_pw_ssid, sizeof(s_pw_ssid), "%s", ap->ssid);
        ui_nav_push(ui_wifi_password_create);
    }
}

static void forget_cb(lv_event_t *e)
{
    wifi_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_current_target(e);
    const char *ssid = lv_obj_get_user_data(row);
    if (!ssid) {
        return;
    }
    /* Dos toques (como Formatear en Almacenamiento): el primero avisa. */
    if (strcmp(p->forget, ssid) == 0 && lv_tick_elaps(p->forget_at) < FORGET_MS) {
        watch_net_forget(ssid);
        p->forget[0] = 0;
    } else {
        snprintf(p->forget, sizeof(p->forget), "%s", ssid);
        p->forget_at = lv_tick_get();
        watch_theme_row_set_value(row, "Toca para olvidar");
    }
}

static void free_ud_cb(lv_event_t *e)
{
    lv_free(lv_obj_get_user_data(lv_event_get_current_target(e)));
}

static void build_nets(wifi_page_t *p, const watch_net_status_t *s)
{
    lv_obj_clean(p->nets);
    if (!s->enabled) {
        lv_obj_t *n = watch_theme_label(p->nets, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                        "Enciende el Wi-Fi para buscar redes. Apagado no gasta batería.");
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(n, LV_PCT(100));
        lv_obj_set_style_pad_hor(n, 16, 0);
        return;
    }
    watch_theme_section(p->nets, "Redes");
    lv_obj_t *scan = watch_theme_list_row(p->nets, FLEX_ICON_GLOBE, FLEX_C_CAT_NET, "Buscar redes", NULL, false);
    lv_obj_add_event_cb(scan, scan_cb, LV_EVENT_CLICKED, NULL);
    net_ap_t aps[NET_SCAN_MAX];
    const int n = watch_net_scan_results(aps, NET_SCAN_MAX);
    for (int i = 0; i < n; i++) {
        const bool cur = s->state == NET_CONNECTED && strcmp(s->ssid, aps[i].ssid) == 0;
        const char *v = cur ? "Conectada" : aps[i].saved ? "Guardada" : aps[i].secure ? "Segura" : "Abierta";
        const int bars = net_rssi_bars(aps[i].rssi);
        const uint32_t col = bars >= 3 ? FLEX_C_CAT_NET : bars >= 2 ? FLEX_C_TXT2 : FLEX_C_MUTE;
        lv_obj_t *r = watch_theme_list_row(p->nets, FLEX_ICON_WIFI, col, aps[i].ssid, v,
                                           aps[i].secure && !aps[i].saved);
        net_ap_t *ap = lv_malloc(sizeof(*ap));
        LV_ASSERT_MALLOC(ap);
        *ap = aps[i];
        lv_obj_set_user_data(r, ap);
        lv_obj_add_event_cb(r, free_ud_cb, LV_EVENT_DELETE, NULL);
        lv_obj_add_event_cb(r, net_cb, LV_EVENT_CLICKED, NULL);
        if (cur) {
            lv_obj_add_state(r, LV_STATE_CHECKED);
        }
    }
    char saved[NET_MAX_SAVED][NET_SSID_MAX + 1];
    const int ns = watch_net_saved(saved, NET_MAX_SAVED);
    if (ns > 0) {
        watch_theme_section(p->nets, "Redes guardadas");
        for (int i = 0; i < ns; i++) {
            lv_obj_t *r = watch_theme_list_row(p->nets, FLEX_ICON_WIFI, FLEX_C_MUTE, saved[i], "Olvidar", false);
            char *ud = lv_malloc(NET_SSID_MAX + 1);
            LV_ASSERT_MALLOC(ud);
            snprintf(ud, NET_SSID_MAX + 1, "%.*s", NET_SSID_MAX, saved[i]);
            lv_obj_set_user_data(r, ud);
            lv_obj_add_event_cb(r, free_ud_cb, LV_EVENT_DELETE, NULL);
            lv_obj_add_event_cb(r, forget_cb, LV_EVENT_CLICKED, p);
        }
    }
}

static void refresh(wifi_page_t *p)
{
    watch_net_status_t s;
    watch_net_get(&s);
    if (s.enabled != lv_obj_has_state(p->sw, LV_STATE_CHECKED)) {
        if (s.enabled) {
            lv_obj_add_state(p->sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(p->sw, LV_STATE_CHECKED);
        }
    }
    char b[64];
    state_text(&s, b, sizeof(b));
    watch_theme_info_set(p->state, b);
    lv_obj_set_style_text_color(lv_obj_get_child(p->state, 1),
                                flex_color(s.state == NET_CONNECTED ? FLEX_C_OK
                                           : s.state == NET_FAILED  ? FLEX_C_WARN
                                                                    : FLEX_C_TXT),
                                0);
    if (s.state == NET_CONNECTED) {
        watch_theme_info_set(p->ip, s.ip);
        lv_obj_remove_flag(p->ip, LV_OBJ_FLAG_HIDDEN);
        watch_theme_info_set(p->time, s.time_synced ? "Sincronizada" : "Pidiendo la hora");
        lv_obj_remove_flag(p->time, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(p->ip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->time, LV_OBJ_FLAG_HIDDEN);
    }
    /* Forget: el aviso caduca */
    if (p->forget[0] && lv_tick_elaps(p->forget_at) >= FORGET_MS) {
        p->forget[0] = 0;
        p->nets_key = 0;
    }
    char saved[NET_MAX_SAVED][NET_SSID_MAX + 1];
    const int ns = watch_net_saved(saved, NET_MAX_SAVED);
    uint32_t key = s.scan_gen * 2654435761u + (uint32_t)s.enabled + (uint32_t)s.state * 7u + (uint32_t)ns * 131u;
    key = hash_str(key, s.ssid);
    for (int i = 0; i < ns; i++) {
        key = hash_str(key, saved[i]);
    }
    if (key == 0) {
        key = 1;
    }
    if (key != p->nets_key && !p->forget[0]) {
        p->nets_key = key;
        build_nets(p, &s);
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_wifi_create(lv_obj_t *parent)
{
    wifi_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Wi-Fi", back_cb, NULL);
    p->list = watch_theme_list(page);

    p->sw_row = watch_theme_list_row(p->list, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Wi-Fi", NULL, false);
    p->sw = lv_switch_create(p->sw_row);
    switch_style(p->sw);
    lv_obj_add_event_cb(p->sw_row, switch_cb, LV_EVENT_CLICKED, NULL);

    p->state = watch_theme_info_row(p->list, "Estado", "");
    p->ip = watch_theme_info_row(p->list, "Dirección IP", "");
    p->time = watch_theme_info_row(p->list, "Hora", "");
    p->nets = lv_obj_create(p->list);
    lv_obj_remove_style_all(p->nets);
    lv_obj_set_size(p->nets, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p->nets, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p->nets, FLEX_ROW_GAP, 0);
    lv_obj_remove_flag(p->nets, LV_OBJ_FLAG_SCROLLABLE);

    refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, WIFI_TICK_MS, p);
    return page;
}
