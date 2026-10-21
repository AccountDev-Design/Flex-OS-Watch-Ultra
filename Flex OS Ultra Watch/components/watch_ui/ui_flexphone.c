/*
 * ui_flexphone — app Flex Phone (IC_FLEXPHONE de Flex OS Ultra) en el reloj.
 *
 * Misma estructura que la portada de la referencia
 * (FlexOS_FlexPhone_Bridge.h:550-700): tarjeta del teléfono con su estado y
 * tres datos (Batería, Red, Latencia), tarjeta del código de emparejamiento
 * ("Teclea este código en el teléfono", "Confirmar aquí", "Cancelar"),
 * acción principal ("Activar el enlace" / "Emparejar teléfono"), aviso con
 * el motivo REAL de un fallo y las secciones. El icono del teléfono sale del
 * mismo SVG que pinta Flex Phone en Android (design/devices/phone.svg,
 * prerasterizado a RGB565A8 en flex_device_icons.c). "Navegador del
 * teléfono" enseña y gobierna su Browser Relay: el reloj se lo pide por BLE y
 * el navegador navega por él (watch_browser, br_backend).
 *
 * Todo sale de la instantánea de watch_phone: la página no inventa estados
 * ("Conectado" solo con sesión autenticada, "--" si no hay dato).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flex_device_icons.h"
#include "watch_browser.h"
#include "watch_phone.h"
#include "watch_theme.h"

#define FP_TICK_MS 250
#define FORGET_MS  3000

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* ---------- textos comunes (también para el panel rápido) ---------- */

const char *ui_phone_chip(const watch_phone_status_t *s, uint32_t *rgb)
{
    uint32_t c = FLEX_C_MUTE;
    const char *t;
    switch (s->link) {
    case FLP_LS_READY:
        c = FLEX_C_OK;
        t = "Conectado";
        break;
    case FLP_LS_SEARCHING:
        t = s->chan == FLP_TC_OPENING ? "Conectando" : "Buscando";
        c = FLEX_C_WARN;
        break;
    case FLP_LS_CONNECTING:
    case FLP_LS_AUTH:
        t = "Conectando";
        c = FLEX_C_WARN;
        break;
    case FLP_LS_PAIRING:
        t = "Emparejando";
        c = FLEX_C_ACCENT;
        break;
    case FLP_LS_ERROR:
        t = "Error";
        c = FLEX_C_ERR;
        break;
    case FLP_LS_OFF:
        t = "Apagado";
        break;
    default:
        t = "No disponible";
        break;
    }
    if (rgb) {
        *rgb = c;
    }
    return t;
}

static void fmt_battery(const watch_phone_status_t *s, char *b, size_t n)
{
    if (!s->phone.valid || s->phone.battery > 100) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%u %%", s->phone.battery);
    }
}

static const char *net_name(const watch_phone_status_t *s)
{
    if (!s->phone.valid) {
        return "--";
    }
    switch (s->phone.net) {
    case FLP_NET_WIFI:
        return "Wi-Fi";
    case FLP_NET_MOBILE:
        return "Móvil";
    case FLP_NET_NONE:
        return "Sin red";
    default:
        return "--";
    }
}

static void fmt_rtt(const watch_phone_status_t *s, char *b, size_t n)
{
    if (s->rtt_ms == 0xFFFF) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%u ms", s->rtt_ms);
    }
}

/* Tarjeta de vidrio con columna (alto según el contenido) */
static lv_obj_t *card_col(lv_obj_t *list, int32_t gap)
{
    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, gap, 0);
    return c;
}

static lv_obj_t *para(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text)
{
    lv_obj_t *l = watch_theme_label(parent, font, rgb, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

/* Aviso: título + texto, en una tarjeta (fgNotice de la referencia) */
static lv_obj_t *notice(lv_obj_t *list, const char *title, uint32_t rgb, const char *text, lv_obj_t **text_out)
{
    lv_obj_t *c = card_col(list, 4);
    para(c, FLEX_FONT_BODY, rgb, title);
    lv_obj_t *t = para(c, FLEX_FONT_SMALL, FLEX_C_TXT2, text);
    if (text_out) {
        *text_out = t;
    }
    return c;
}

static lv_obj_t *full_button(lv_obj_t *list, const char *text, uint32_t rgb, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = watch_theme_button(list, text, rgb, LV_PCT(100), 60);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    }
    return b;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* ======================= Portada ======================= */

typedef struct {
    lv_obj_t *dyn;   /* parte que se rehace cuando cambia la forma de la página */
    uint32_t key;
    lv_obj_t *name, *chip, *sub, *bat, *net, *lat;
    lv_obj_t *code, *left, *pstat, *confirm;
    lv_obj_t *err;
    lv_obj_t *notif_row;
} fp_page_t;

static void enable_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_set_enabled(true);
}

static void pair_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_pair_begin();
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_pair_confirm();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_pair_cancel();
}

static uint32_t layout_key(const watch_phone_status_t *s)
{
    return (uint32_t)s->link | (uint32_t)s->bonded << 4 | (uint32_t)s->enabled << 5 | (uint32_t)(s->code[0] != 0)
           << 6 | (uint32_t)(s->err[0] != 0) << 7 | (uint32_t)s->pair_user_ok << 8 | 1u << 9;
}

static lv_obj_t *stat(lv_obj_t *row, const char *caption)
{
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    watch_theme_label(col, FLEX_FONT_SMALL, FLEX_C_MUTE, caption);
    return watch_theme_label(col, FLEX_FONT_BODY, FLEX_C_TXT, "--");
}

static void build(fp_page_t *p, const watch_phone_status_t *s)
{
    lv_obj_clean(p->dyn);
    p->code = p->left = p->pstat = p->confirm = p->err = NULL;

    /* Tarjeta del teléfono */
    lv_obj_t *hero = card_col(p->dyn, 6);
    lv_obj_t *top = lv_obj_create(hero);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ic = lv_image_create(top);
    lv_image_set_src(ic, &flex_devicon_phone);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, -4, 0);
    p->name = watch_theme_label(top, FLEX_FONT_TITLE, FLEX_C_TXT, "");
    lv_obj_set_width(p->name, LV_PCT(48));
    lv_obj_align(p->name, LV_ALIGN_LEFT_MID, 60, 0);
    lv_label_set_long_mode(p->name, LV_LABEL_LONG_MODE_DOTS);
    p->chip = watch_theme_label(top, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    lv_obj_align(p->chip, LV_ALIGN_RIGHT_MID, 0, 0);
    p->sub = para(hero, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_t *stats = lv_obj_create(hero);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(stats, 6, 0);
    lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    p->bat = stat(stats, "Batería");
    p->net = stat(stats, "Red");
    p->lat = stat(stats, "Latencia");

    /* Código de emparejamiento */
    if (s->code[0]) {
        lv_obj_t *pc = card_col(p->dyn, 6);
        lv_obj_set_flex_align(pc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        watch_theme_label(pc, FLEX_FONT_SMALL, FLEX_C_TXT2, "Teclea este código en el teléfono");
        p->code = watch_theme_label(pc, FLEX_FONT_LARGE, FLEX_C_TXT, "");
        lv_obj_set_style_text_letter_space(p->code, 4, 0);
        p->left = watch_theme_label(pc, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
        p->pstat = para(pc, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
        lv_obj_set_style_text_align(p->pstat, LV_TEXT_ALIGN_CENTER, 0);
        p->confirm = full_button(p->dyn, s->pair_user_ok ? "Confirmado en el reloj" : "Confirmar aquí",
                                 FLEX_C_PRIMARY, confirm_cb, NULL);
        if (s->pair_user_ok) {
            lv_obj_add_state(p->confirm, LV_STATE_DISABLED);
        }
        full_button(p->dyn, "Cancelar", FLEX_C_SURF2, cancel_cb, NULL);
    } else if (s->err[0]) {
        notice(p->dyn, s->link == FLP_LS_ERROR ? "El enlace falló" : "Aviso", s->link == FLP_LS_ERROR ? FLEX_C_ERR
                                                                                                    : FLEX_C_WARN,
               s->err, &p->err);
    }

    /* Acción principal */
    if (!s->enabled || s->link == FLP_LS_OFF) {
        full_button(p->dyn, "Activar el enlace", FLEX_C_PRIMARY, enable_cb, NULL);
    } else if (!s->bonded && !s->code[0] && s->link != FLP_LS_READY) {
        full_button(p->dyn, "Emparejar teléfono", FLEX_C_PRIMARY, pair_cb, NULL);
    }

    watch_theme_section(p->dyn, "Secciones");
    p->notif_row = watch_theme_list_row(p->dyn, FLEX_ICON_BELL, FLEX_C_CAT_BLUE, "Notificaciones", "", true);
    lv_obj_add_event_cb(p->notif_row, open_cb, LV_EVENT_CLICKED, (void *)ui_notifs_create);
    lv_obj_t *r = watch_theme_list_row(p->dyn, FLEX_ICON_PLAY, FLEX_C_CAT_SUN, "Música", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_music_create);
    r = watch_theme_list_row(p->dyn, FLEX_ICON_INFO, FLEX_C_CAT_GREEN, "Estado del teléfono", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_phone_state_create);
    r = watch_theme_list_row(p->dyn, FLEX_ICON_GLOBE, FLEX_C_CAT_BLUE, "Navegador del teléfono", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_phone_relay_create);
    r = watch_theme_list_row(p->dyn, FLEX_ICON_BLUETOOTH, FLEX_C_CAT_NET, "Conexión", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_phone_conn_create);
}

static void pair_status(const watch_phone_status_t *s, char *b, size_t n)
{
    if (s->pair_peer_ok && s->pair_user_ok) {
        snprintf(b, n, "Emparejando...");
    } else if (s->pair_peer_ok) {
        snprintf(b, n, "Código verificado en el teléfono: confirma aquí");
    } else if (s->err[0]) {
        snprintf(b, n, "%s", s->err);
    } else if (s->chan != FLP_TC_OPEN) {
        snprintf(b, n, "Buscando el teléfono...");
    } else {
        snprintf(b, n, "Esperando al teléfono");
    }
}

static void refresh(fp_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    const uint32_t key = layout_key(&s);
    if (key != p->key) {
        p->key = key;
        build(p, &s);
    }
    char b[96];
    set_text(p->name, s.peer_name[0] ? s.peer_name : (s.bonded ? "Teléfono vinculado" : "Sin teléfono vinculado"));
    uint32_t rgb;
    set_text(p->chip, ui_phone_chip(&s, &rgb));
    watch_theme_set_text_color(p->chip, rgb);
    if (s.link == FLP_LS_READY) {
        snprintf(b, sizeof(b), "%s", s.caps.valid && s.caps.osver[0] ? s.caps.osver : "Sesión abierta y autenticada");
    } else if (s.link == FLP_LS_OFF) {
        snprintf(b, sizeof(b), "El Bluetooth del reloj está libre: el enlace no busca al teléfono");
    } else {
        snprintf(b, sizeof(b), "%s", s.chan_text[0] ? s.chan_text : watch_phone_state_text(&s));
    }
    set_text(p->sub, b);
    fmt_battery(&s, b, sizeof(b));
    set_text(p->bat, b);
    set_text(p->net, net_name(&s));
    fmt_rtt(&s, b, sizeof(b));
    set_text(p->lat, b);
    if (p->code) {
        snprintf(b, sizeof(b), "%.3s %.3s", s.code, s.code + 3);
        set_text(p->code, b);
        const uint32_t sec = (s.pair_left_ms + 999) / 1000;
        snprintf(b, sizeof(b), "Caduca en %lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        set_text(p->left, b);
        pair_status(&s, b, sizeof(b));
        set_text(p->pstat, b);
    }
    if (p->err) {
        set_text(p->err, s.err);
    }
    if (s.notif_count) {
        snprintf(b, sizeof(b), "%u", s.notif_count);
    } else {
        b[0] = 0;
    }
    watch_theme_row_set_value(p->notif_row, b);
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_flexphone_create(lv_obj_t *parent)
{
    fp_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Flex Phone", back_cb, NULL);
    p->dyn = watch_theme_list(page);
    refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, FP_TICK_MS, p);
    return page;
}

/* ======================= Conexión ======================= */

typedef struct {
    lv_obj_t *state, *chan, *peer, *lat, *err_card, *err;
    lv_obj_t *toggle, *find, *forget;
    uint32_t forget_at;
    bool finding;
} conn_page_t;

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_status_t s;
    watch_phone_get(&s);
    watch_phone_set_enabled(!s.enabled);
}

static void find_cb(lv_event_t *e)
{
    conn_page_t *p = lv_event_get_user_data(e);
    if (watch_phone_find(!p->finding)) {
        p->finding = !p->finding;
        watch_theme_button_set(p->find, p->finding ? "Parar el sonido" : "Hacer sonar el teléfono",
                               p->finding ? FLEX_C_DANGER : FLEX_C_SURF2);
    }
}

static void forget_cb(lv_event_t *e)
{
    conn_page_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (p->forget_at && now - p->forget_at < FORGET_MS) {
        p->forget_at = 0;
        watch_phone_forget();
        watch_theme_button_set(p->forget, "Olvidar el teléfono", FLEX_C_SURF2);
        return;
    }
    p->forget_at = now;
    watch_theme_button_set(p->forget, "Toca otra vez para olvidar", FLEX_C_DANGER);
}

static void conn_refresh(conn_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    char b[96];
    watch_theme_info_set(p->state, watch_phone_state_text(&s));
    set_text(p->chan, s.chan_text[0] ? s.chan_text : "Bluetooth apagado");
    watch_theme_info_set(p->peer, s.peer_name[0] ? s.peer_name : "--");
    fmt_rtt(&s, b, sizeof(b));
    watch_theme_info_set(p->lat, b);
    if (s.err[0]) {
        watch_theme_set_visible(p->err_card, true);
        set_text(p->err, s.err);
    } else {
        watch_theme_set_visible(p->err_card, false);
    }
    watch_theme_button_set(p->toggle, s.enabled ? "Apagar el enlace" : "Activar el enlace",
                           s.enabled ? FLEX_C_SURF2 : FLEX_C_PRIMARY);
    const bool can_find = s.link == FLP_LS_READY && s.caps.valid && (s.caps.granted & FLP_CAP_FIND);
    if (can_find) {
        lv_obj_remove_state(p->find, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(p->find, LV_STATE_DISABLED);
        if (p->finding) {
            p->finding = false;
            watch_theme_button_set(p->find, "Hacer sonar el teléfono", FLEX_C_SURF2);
        }
    }
    if (p->forget_at && lv_tick_get() - p->forget_at >= FORGET_MS) {
        p->forget_at = 0;
        watch_theme_button_set(p->forget, "Olvidar el teléfono", FLEX_C_SURF2);
    }
    if (s.bonded) {
        lv_obj_remove_state(p->forget, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(p->forget, LV_STATE_DISABLED);
    }
}

static void conn_tick_cb(lv_timer_t *t)
{
    conn_refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_phone_conn_create(lv_obj_t *parent)
{
    conn_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Conexión", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Enlace");
    p->state = watch_theme_info_row(list, "Estado", "");
    watch_theme_info_row(list, "Transporte", "Bluetooth LE");
    p->peer = watch_theme_info_row(list, "Teléfono", "");
    p->lat = watch_theme_info_row(list, "Latencia", "");
    lv_obj_t *cc = card_col(list, 4);
    watch_theme_label(cc, FLEX_FONT_SMALL, FLEX_C_MUTE, "Canal Bluetooth");
    p->chan = para(cc, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    p->err_card = notice(list, "Último error", FLEX_C_WARN, "", &p->err);

    p->toggle = full_button(list, "", FLEX_C_PRIMARY, toggle_cb, NULL);
    p->find = full_button(list, "Hacer sonar el teléfono", FLEX_C_SURF2, find_cb, p);
    p->forget = full_button(list, "Olvidar el teléfono", FLEX_C_SURF2, forget_cb, p);

    notice(list, "Cómo se conecta", FLEX_C_TXT,
           "El reloj busca por Bluetooth LE el servicio de la app Flex Phone y se conecta solo. La primera vez el "
           "teléfono pide aceptar el emparejamiento Bluetooth; después, el código de 6 cifras del reloj autentica "
           "a los dos. El Wi-Fi no interviene: sin Internet el enlace sigue.",
           NULL);
    conn_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, conn_tick_cb, FP_TICK_MS, p);
    return page;
}

/* ======================= Estado del teléfono ======================= */

typedef struct {
    lv_obj_t *dyn;
    uint32_t key;
    lv_obj_t *bat, *net, *sto, *mem;
} st_page_t;

static void fmt_mb_pair(uint32_t free_mb, uint32_t total_mb, char *b, size_t n)
{
    if (!total_mb) {
        snprintf(b, n, "--");
        return;
    }
    /* "41,3 / 122 GB libres" con coma decimal */
    const uint32_t f10 = (free_mb * 10u + 512u) / 1024u, t = (total_mb + 512u) / 1024u;
    snprintf(b, n, "%lu,%lu de %lu GB libres", (unsigned long)(f10 / 10), (unsigned long)(f10 % 10),
             (unsigned long)t);
}

static void st_build(st_page_t *p, const watch_phone_status_t *s)
{
    lv_obj_clean(p->dyn);
    p->bat = p->net = p->sto = p->mem = NULL;
    if (!s->phone.valid) {
        notice(p->dyn, "Sin datos", FLEX_C_TXT,
               "El teléfono tiene que estar conectado. No se enseña nada de una sesión anterior: una batería de "
               "hace diez minutos no es la batería de ahora.",
               NULL);
        return;
    }
    p->bat = watch_theme_info_row(p->dyn, "Batería", "");
    p->net = watch_theme_info_row(p->dyn, "Red", "");
    p->sto = watch_theme_info_row(p->dyn, "Almacenamiento", "");
    p->mem = watch_theme_info_row(p->dyn, "Memoria", "");
    if (s->caps.valid) {
        watch_theme_info_row(p->dyn, "Modelo", s->caps.model[0] ? s->caps.model : "--");
        watch_theme_info_row(p->dyn, "Fabricante", s->caps.vendor[0] ? s->caps.vendor : "--");
        watch_theme_info_row(p->dyn, "Sistema", s->caps.osver[0] ? s->caps.osver : "--");
    }
    watch_theme_section(p->dyn, "Capacidades");
    if (!s->caps.valid) {
        para(p->dyn, FLEX_FONT_SMALL, FLEX_C_MUTE, "El teléfono aún no las ha informado");
        return;
    }
    static const uint16_t kCaps[] = {FLP_CAP_NOTIF, FLP_CAP_REPLY, FLP_CAP_MEDIA, FLP_CAP_FIND,
                                     FLP_CAP_STATE, FLP_CAP_TIME,  FLP_CAP_RELAY};
    for (size_t i = 0; i < sizeof(kCaps) / sizeof(kCaps[0]); i++) {
        const char *why = flexPhoneCapWhyNot(s->caps.supported, s->caps.granted, kCaps[i]);
        lv_obj_t *c = card_col(p->dyn, 2);
        lv_obj_t *t = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, flexPhoneCapName(kCaps[i]));
        (void)t;
        para(c, FLEX_FONT_SMALL, why ? FLEX_C_MUTE : FLEX_C_OK, why ? why : "Disponible");
    }
}

static void st_refresh(st_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    const uint32_t key = (uint32_t)s.phone.valid | (uint32_t)s.caps.valid << 1 | (uint32_t)s.caps.granted << 2 |
                         1u << 20;
    if (key != p->key) {
        p->key = key;
        st_build(p, &s);
    }
    if (!p->bat) {
        return;
    }
    char b[64], v[24];
    fmt_battery(&s, v, sizeof(v));
    snprintf(b, sizeof(b), "%s%s", v, s.phone.charging ? " · cargando" : (s.phone.powerSave ? " · ahorro" : ""));
    watch_theme_info_set(p->bat, b);
    watch_theme_info_set(p->net, net_name(&s));
    fmt_mb_pair(s.phone.storageFreeMb, s.phone.storageTotalMb, b, sizeof(b));
    watch_theme_info_set(p->sto, b);
    fmt_mb_pair(s.phone.ramFreeMb, s.phone.ramTotalMb, b, sizeof(b));
    watch_theme_info_set(p->mem, b);
}

static void st_tick_cb(lv_timer_t *t)
{
    st_refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_phone_state_create(lv_obj_t *parent)
{
    st_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Estado del teléfono", back_cb, NULL);
    p->dyn = watch_theme_list(page);
    st_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, st_tick_cb, 1000, p);
    return page;
}

/* ======================= Navegador del teléfono ======================= */

typedef struct {
    lv_obj_t *state, *addr, *src, *why_card, *why, *toggle, *open;
    uint8_t shown; /* FLP_RELAY_* pintado en el botón (0xFF = nada) */
} relay_page_t;

static const char *relay_state_text(const watch_phone_status_t *s, uint32_t *rgb)
{
    uint32_t c = FLEX_C_MUTE;
    const char *t = "Parado";
    if (s->link != FLP_LS_READY) {
        t = "Sin teléfono conectado";
    } else {
        switch (s->relay.state) {
        case FLP_RELAY_STARTING:
            t = "Arrancando en el teléfono…";
            c = FLEX_C_WARN;
            break;
        case FLP_RELAY_UP:
            t = "En marcha";
            c = FLEX_C_OK;
            break;
        case FLP_RELAY_ERROR:
            t = "Error";
            c = FLEX_C_ERR;
            break;
        case FLP_RELAY_SUSPENDED:
            t = "Suspendido por Android";
            c = FLEX_C_WARN;
            break;
        default:
            t = s->relay.byUser ? "Detenido por el usuario" : "Parado";
            break;
        }
    }
    if (rgb) {
        *rgb = c;
    }
    return t;
}

static void relay_toggle_cb(lv_event_t *e)
{
    relay_page_t *p = lv_event_get_user_data(e);
    watch_phone_status_t s;
    watch_phone_get(&s);
    const bool up = s.relay.state == FLP_RELAY_UP || s.relay.state == FLP_RELAY_STARTING;
    if (!watch_phone_relay(!up)) {
        watch_theme_set_visible(p->why_card, true);
        set_text(p->why, s.link == FLP_LS_READY ? "La app Flex Phone no ofrece el navegador" : "Conecta antes el teléfono");
    }
}

static void relay_refresh(relay_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    char b[128];
    uint32_t rgb;
    watch_theme_info_set(p->state, relay_state_text(&s, &rgb));
    if (s.relay.state == FLP_RELAY_UP) {
        snprintf(b, sizeof(b), "%u.%u.%u.%u:%u%s", s.relay.ip[0], s.relay.ip[1], s.relay.ip[2], s.relay.ip[3],
                 (unsigned)s.relay.port, s.relay.tls ? " (TLS)" : "");
    } else {
        snprintf(b, sizeof(b), "--");
    }
    watch_theme_info_set(p->addr, b);
    watch_browser_source_t src;
    watch_browser_source(&src);
    watch_theme_info_set(p->src, src.src != BRSRC_NONE ? flexBrSourceName(src.src) : "Ninguno");
    const char *why = s.relay.err[0] && s.relay.state != FLP_RELAY_UP ? s.relay.err : src.src == BRSRC_NONE ? src.why : "";
    if (why[0]) {
        watch_theme_set_visible(p->why_card, true);
        set_text(p->why, why);
    } else {
        watch_theme_set_visible(p->why_card, false);
    }
    const bool up = s.relay.state == FLP_RELAY_UP || s.relay.state == FLP_RELAY_STARTING;
    const uint8_t key = s.link != FLP_LS_READY ? 0xFE : up ? 1 : 0;
    if (key != p->shown) {
        p->shown = key;
        watch_theme_button_set(p->toggle, up ? "Parar el servidor del teléfono" : "Encender el servidor del teléfono",
                               up ? FLEX_C_SURF2 : FLEX_C_PRIMARY);
        if (s.link == FLP_LS_READY) {
            lv_obj_remove_state(p->toggle, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(p->toggle, LV_STATE_DISABLED);
        }
    }
}

static void relay_tick_cb(lv_timer_t *t)
{
    relay_refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_phone_relay_create(lv_obj_t *parent)
{
    relay_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->shown = 0xFF;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Web del teléfono", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    lv_obj_t *hero = card_col(list, 4);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ic = lv_image_create(hero);
    lv_image_set_src(ic, &flex_devicon_phone);
    lv_obj_t *cap = para(hero, FLEX_FONT_SMALL, FLEX_C_TXT2,
                         "El teléfono abre la web y el reloj recibe las imágenes por el Wi-Fi");
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);

    p->state = watch_theme_info_row(list, "Servidor", "");
    p->addr = watch_theme_info_row(list, "Dirección", "");
    p->src = watch_theme_info_row(list, "Origen del navegador", "");
    p->why_card = notice(list, "Motivo", FLEX_C_WARN, "", &p->why);
    p->toggle = full_button(list, "", FLEX_C_PRIMARY, relay_toggle_cb, p);
    p->open = full_button(list, "Abrir el navegador", FLEX_C_SURF2, open_cb, (void *)ui_browser_create);

    notice(list, "Cómo funciona", FLEX_C_TXT,
           "Al abrir el navegador con el origen en Automático o Flex Phone, el reloj pide al teléfono por "
           "Bluetooth que encienda su servidor y se conecta a él por el Wi-Fi: los dos tienen que estar en la "
           "misma red. La credencial sale del emparejamiento y no viaja nunca. La página se actualiza sola, sin "
           "tocar la pantalla.",
           NULL);
    relay_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, relay_tick_cb, FP_TICK_MS, p);
    return page;
}
