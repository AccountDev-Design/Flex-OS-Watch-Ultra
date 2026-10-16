/*
 * ui_notifs — notificaciones del teléfono (F5.3): lista, detalle con las
 * acciones reales de Android, respuestas rápidas y el aviso emergente.
 *
 * Reglas de la referencia (FlexOS_FlexPhone_Bridge.h:720-775, Overlay.h,
 * FlexPhone.h):
 *  - "Responder" solo si Android expuso RemoteInput (canReply); si no, el
 *    botón no existe. El resultado que se enseña es el REAL que devuelve el
 *    teléfono (REPLY_RESULT), no un "enviado" optimista.
 *  - Lo sensible (códigos, banca) no se enseña nunca: "Contenido oculto".
 *  - Vacío con el motivo: sin vincular, sin conexión o sin acceso a
 *    notificaciones en el teléfono.
 *  - Descartar en el reloj la quita también del teléfono (NOTIF_REMOVE).
 *
 * El texto viene del teléfono y puede traer caracteres que la fuente del
 * reloj no tiene (emoji, otros alfabetos): se sustituyen al pintar en vez de
 * dejar huecos (ui_phone_text).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_phone.h"
#include "watch_theme.h"
#include "watch_time.h"
#include "wt_time.h"

#define LIST_TICK_MS   500
#define BANNER_POLL_MS 300

static uint32_t s_sel_id; /* notificación abierta en el detalle / respuesta */

void ui_notif_open(uint32_t id)
{
    s_sel_id = id;
    ui_nav_push(ui_notif_detail_create);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* ---------- texto y hora ---------- */

static bool is_emoji(uint32_t cp)
{
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF);
}

static bool is_invisible(uint32_t cp)
{
    return cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0x200B ||
           cp == 0x00AD;
}

static size_t put(char *out, size_t n, size_t at, const char *s)
{
    const size_t k = strlen(s);
    if (at + k < n) {
        memcpy(out + at, s, k);
        at += k;
    }
    return at;
}

void ui_phone_text(const char *in, char *out, size_t n, const lv_font_t *font)
{
    size_t at = 0;
    bool last_emoji = false;
    if (!out || !n) {
        return;
    }
    for (const uint8_t *p = (const uint8_t *)(in ? in : ""); *p && at + 1 < n;) {
        uint32_t cp;
        int len = 1;
        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = (uint32_t)(*p & 0x1F) << 6 | (p[1] & 0x3F);
            len = 2;
        } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = (uint32_t)(*p & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
            len = 3;
        } else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = (uint32_t)(*p & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 |
                 (p[3] & 0x3F);
            len = 4;
        } else {
            break; /* UTF-8 roto: se corta (flexlink ya lo recorta así) */
        }
        p += len;
        if (cp == '\n' || cp == '\r' || cp == '\t') {
            at = put(out, n, at, " ");
            last_emoji = false;
            continue;
        }
        if (is_invisible(cp)) {
            continue;
        }
        lv_font_glyph_dsc_t g;
        if (cp >= 0x20 && lv_font_get_glyph_dsc(font, &g, cp, 0)) {
            if (at + (size_t)len < n) {
                memcpy(out + at, p - len, (size_t)len);
                at += (size_t)len;
            }
            last_emoji = false;
            continue;
        }
        /* Sin glifo: se dice qué había, no se deja un hueco */
        const char *rep = cp == 0x2018 || cp == 0x2019 ? "'"
                          : cp == 0x201C || cp == 0x201D ? "\""
                          : cp == 0x2022 ? "·"
                          : is_emoji(cp) ? (last_emoji ? "" : "(emoji)")
                                         : "?";
        at = put(out, n, at, rep);
        last_emoji = is_emoji(cp);
    }
    out[at] = 0;
}

void ui_phone_when(uint32_t when_s, char *b, size_t n)
{
    int64_t now = 0;
    const bool have = watch_time_now_utc(&now);
    const int64_t local = (int64_t)when_s + (int64_t)watch_time_get_tz() * 60;
    wt_datetime_t dt, today;
    wt_from_seconds(local, &dt);
    if (have) {
        const int64_t d = now - (int64_t)when_s;
        if (d >= 0 && d < 60) {
            snprintf(b, n, "Ahora");
            return;
        }
        if (d >= 0 && d < 3600) {
            snprintf(b, n, "Hace %d min", (int)(d / 60));
            return;
        }
        wt_from_seconds(now + (int64_t)watch_time_get_tz() * 60, &today);
        if (today.year != dt.year || today.month != dt.month || today.day != dt.day) {
            static const char *const kMon[] = {"ene", "feb", "mar", "abr", "may", "jun",
                                               "jul", "ago", "sep", "oct", "nov", "dic"};
            snprintf(b, n, "%d %s", dt.day, kMon[(dt.month - 1) % 12]);
            return;
        }
    }
    snprintf(b, n, "%02d:%02d", dt.hour, dt.minute);
}

/* Cuerpo que se puede enseñar, ya preparado para la fuente */
static void body_text(const FlexPhoneNotif *nt, bool lock, char *out, size_t n)
{
    char raw[FLP_TEXT_MAX];
    if (watch_phone_notif_body(nt, lock, raw, sizeof(raw))) {
        ui_phone_text(raw, out, n, FLEX_FONT_SMALL);
    } else {
        snprintf(out, n, "%s", nt->sensitive ? "Contenido sensible: míralo en el teléfono" : "Contenido oculto");
    }
}

/* ======================= Lista ======================= */

typedef struct {
    lv_obj_t *list;
    uint32_t gen, key;
    FlexPhoneNotif *buf; /* copia de la lista (PSRAM vía malloc grande) */
} nl_page_t;

static void open_detail_cb(lv_event_t *e)
{
    s_sel_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ui_nav_push(ui_notif_detail_create);
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    watch_phone_clear_all();
}

static lv_obj_t *notif_card(lv_obj_t *list, const FlexPhoneNotif *nt)
{
    char t[FLP_TEXT_MAX + 16];
    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 2, 0);
    lv_obj_add_event_cb(c, open_detail_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)nt->id);
    lv_obj_t *top = lv_obj_create(c);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    ui_phone_text(nt->app[0] ? nt->app : nt->pkg, t, sizeof(t), FLEX_FONT_SMALL);
    lv_obj_t *app = watch_theme_label(top, FLEX_FONT_SMALL, FLEX_C_ACCENT, t);
    lv_obj_set_width(app, LV_PCT(65));
    lv_label_set_long_mode(app, LV_LABEL_LONG_MODE_DOTS);
    ui_phone_when(nt->whenMs, t, sizeof(t));
    lv_obj_t *when = watch_theme_label(top, FLEX_FONT_SMALL, FLEX_C_MUTE, t);
    lv_obj_align(when, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_phone_text(nt->title[0] ? nt->title : "(sin título)", t, sizeof(t), FLEX_FONT_BODY);
    lv_obj_t *title = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, t);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    body_text(nt, false, t, sizeof(t));
    lv_obj_t *body = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT2, t);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 2 * lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_DOTS);
    return c;
}

static void empty_card(lv_obj_t *list, const watch_phone_status_t *s)
{
    const char *why;
    if (s->caps.valid && !(s->caps.granted & FLP_CAP_NOTIF)) {
        why = flexPhoneCapWhyNot(s->caps.supported, s->caps.granted, FLP_CAP_NOTIF);
    } else if (s->link == FLP_LS_READY) {
        why = "El teléfono está conectado. Lo que llegue aparecerá aquí.";
    } else if (!s->bonded) {
        why = "Empareja el teléfono en Flex Phone para recibir sus notificaciones.";
    } else {
        why = "Conecta el teléfono para recibir sus notificaciones.";
    }
    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *t = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, "Sin notificaciones");
    (void)t;
    lv_obj_t *w = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT2, why);
    lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(w, LV_PCT(100));
}

static void nl_refresh(nl_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    const uint32_t key = (uint32_t)s.link | (uint32_t)s.bonded << 4 | (uint32_t)s.caps.valid << 5 |
                         (uint32_t)s.caps.granted << 6;
    if (s.notif_gen == p->gen && key == p->key && lv_obj_get_child_count(p->list)) {
        return;
    }
    p->gen = s.notif_gen;
    p->key = key;
    lv_obj_clean(p->list);
    const int n = p->buf ? watch_phone_notifs(p->buf, WATCH_PHONE_NOTIF_MAX) : 0;
    if (n == 0) {
        empty_card(p->list, &s);
        return;
    }
    for (int i = 0; i < n; i++) {
        notif_card(p->list, &p->buf[i]);
    }
    lv_obj_t *b = watch_theme_button(p->list, "Borrar el historial", FLEX_C_SURF2, LV_PCT(100), 60);
    lv_obj_add_event_cb(b, clear_cb, LV_EVENT_CLICKED, NULL);
}

static void nl_tick_cb(lv_timer_t *t)
{
    nl_refresh(lv_timer_get_user_data(t));
}

static void nl_free_cb(lv_event_t *e)
{
    nl_page_t *p = lv_event_get_user_data(e);
    lv_free(p->buf);
    lv_free(p);
}

lv_obj_t *ui_notifs_create(lv_obj_t *parent)
{
    nl_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->buf = lv_malloc(sizeof(FlexPhoneNotif) * WATCH_PHONE_NOTIF_MAX);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Notificaciones", back_cb, NULL);
    p->list = watch_theme_list(page);
    p->gen = 0xFFFFFFFFu;
    nl_refresh(p);
    lv_obj_add_event_cb(page, nl_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, nl_tick_cb, LIST_TICK_MS, p);
    return page;
}

/* ======================= Detalle ======================= */

typedef struct {
    uint32_t id;
    lv_obj_t *gone, *status;
    lv_obj_t *btns[FLP_ACTION_MAX];
    int nbtn;
    bool was_ready;
} nd_page_t;

static void action_cb(lv_event_t *e)
{
    nd_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    FlexPhoneNotif nt;
    if (!watch_phone_notif(p->id, &nt)) {
        return;
    }
    for (int i = 0; i < p->nbtn; i++) {
        if (p->btns[i] != btn) {
            continue;
        }
        if (nt.canReply && i == nt.replyAction) {
            s_sel_id = p->id;
            ui_nav_push(ui_notif_reply_create);
        } else if (watch_phone_action(p->id, (uint8_t)i)) {
            set_text(p->status, "Enviado al teléfono");
        } else {
            set_text(p->status, "Requiere el teléfono conectado");
        }
    }
}

static void dismiss_cb(lv_event_t *e)
{
    nd_page_t *p = lv_event_get_user_data(e);
    watch_phone_dismiss(p->id);
    ui_nav_back();
}

static void nd_refresh(nd_page_t *p)
{
    FlexPhoneNotif nt;
    watch_phone_status_t s;
    watch_phone_get(&s);
    const bool alive = watch_phone_notif(p->id, &nt);
    if (!alive) {
        lv_obj_remove_flag(p->gone, LV_OBJ_FLAG_HIDDEN);
    }
    const bool ready = s.link == FLP_LS_READY;
    for (int i = 0; i < p->nbtn; i++) {
        if (alive && ready) {
            lv_obj_remove_state(p->btns[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(p->btns[i], LV_STATE_DISABLED);
        }
    }
    if (!ready && p->nbtn && alive) {
        set_text(p->status, "Las acciones necesitan el teléfono conectado");
    } else if (ready && !p->was_ready) {
        set_text(p->status, "");
    }
    p->was_ready = ready;
}

static void nd_tick_cb(lv_timer_t *t)
{
    nd_refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_notif_detail_create(lv_obj_t *parent)
{
    nd_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->id = s_sel_id;
    FlexPhoneNotif nt;
    const bool have = watch_phone_notif(p->id, &nt);
    char t[FLP_TEXT_MAX + 48];
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    ui_phone_text(have ? (nt.app[0] ? nt.app : nt.pkg) : "Notificación", t, sizeof(t), FLEX_FONT_TITLE);
    watch_theme_header(page, t, back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    if (have) {
        ui_phone_text(nt.title[0] ? nt.title : "(sin título)", t, sizeof(t), FLEX_FONT_TITLE);
        lv_obj_t *title = watch_theme_label(c, FLEX_FONT_TITLE, FLEX_C_TXT, t);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(title, LV_PCT(100));
        ui_phone_when(nt.whenMs, t, sizeof(t));
        watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_MUTE, t);
        body_text(&nt, false, t, sizeof(t));
        lv_obj_t *body = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT2, t);
        lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(body, LV_PCT(100));
    }
    p->gone = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_WARN, "Ya no está en el teléfono");
    if (have) {
        lv_obj_add_flag(p->gone, LV_OBJ_FLAG_HIDDEN);
    }

    /* Acciones de Android: las mismas etiquetas, en el mismo orden */
    for (int i = 0; have && i < nt.actionCount && i < FLP_ACTION_MAX; i++) {
        ui_phone_text(nt.actions[i], t, sizeof(t), FLEX_FONT_BODY);
        const bool reply = nt.canReply && i == nt.replyAction;
        p->btns[i] = watch_theme_button(list, t, reply ? FLEX_C_PRIMARY : FLEX_C_SURF2, LV_PCT(100), 60);
        lv_obj_add_event_cb(p->btns[i], action_cb, LV_EVENT_CLICKED, p);
        p->nbtn++;
    }
    p->status = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->status, LV_PCT(100));
    lv_obj_t *d = watch_theme_button(list, "Descartar", FLEX_C_SURF2, LV_PCT(100), 60);
    lv_obj_add_event_cb(d, dismiss_cb, LV_EVENT_CLICKED, p);
    p->was_ready = true;
    nd_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, nd_tick_cb, LIST_TICK_MS, p);
    return page;
}

/* ======================= Respuesta rápida ======================= */

static const char *const kQuick[] = {"Vale", "Ahora no puedo", "Voy de camino", "Te llamo luego",
                                     "Gracias"};

typedef struct {
    uint32_t id;
    lv_obj_t *status;
    uint32_t ok0, fail0; /* contadores REALES al enviar */
    bool waiting;
} nr_page_t;

static const char *reply_err(uint8_t e)
{
    switch (e) {
    case FLNK_E_NOTPAIRED:
        return "Requiere el teléfono conectado";
    case FLNK_E_DENIED:
        return "El teléfono no permite responder (acceso a notificaciones)";
    case FLNK_E_GONE:
        return "La notificación ya no existe en el teléfono";
    case FLNK_E_NOREPLY:
        return "Esta notificación no admite respuesta";
    default:
        return flexLinkErrName(e);
    }
}

static void quick_cb(lv_event_t *e)
{
    nr_page_t *p = lv_event_get_user_data(e);
    const char *text = kQuick[(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))];
    watch_phone_status_t s;
    watch_phone_get(&s);
    const uint8_t r = watch_phone_reply(p->id, text);
    if (r != FLNK_E_NONE) {
        set_text(p->status, reply_err(r));
        return;
    }
    p->ok0 = s.replies_ok;
    p->fail0 = s.replies_fail;
    p->waiting = true;
    set_text(p->status, "Enviando...");
}

static void nr_tick_cb(lv_timer_t *t)
{
    nr_page_t *p = lv_timer_get_user_data(t);
    if (!p->waiting) {
        return;
    }
    watch_phone_status_t s;
    watch_phone_get(&s);
    if (s.replies_ok != p->ok0) {
        p->waiting = false;
        set_text(p->status, "Entregada a la app del teléfono");
    } else if (s.replies_fail != p->fail0) {
        p->waiting = false;
        set_text(p->status, "El teléfono no pudo enviarla");
    } else if (s.link != FLP_LS_READY) {
        p->waiting = false;
        set_text(p->status, "Se perdió la conexión: no se sabe si llegó");
    }
}

lv_obj_t *ui_notif_reply_create(lv_obj_t *parent)
{
    nr_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->id = s_sel_id;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Responder", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    for (size_t i = 0; i < sizeof(kQuick) / sizeof(kQuick[0]); i++) {
        lv_obj_t *r = watch_theme_list_row(list, FLEX_ICON_CHEVRON_RIGHT, FLEX_C_PRIMARY, kQuick[i], NULL, false);
        lv_obj_set_user_data(r, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r, quick_cb, LV_EVENT_CLICKED, p);
    }
    p->status = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->status, LV_PCT(100));
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, nr_tick_cb, LIST_TICK_MS, p);
    return page;
}

/* ======================= Aviso emergente ======================= */

static struct {
    bool init;
    uint32_t seq;
    uint32_t at;
} s_watch;

void ui_notif_watch_tick(bool screen_on)
{
    const uint32_t now = lv_tick_get();
    if (s_watch.init && now - s_watch.at < BANNER_POLL_MS) {
        return;
    }
    s_watch.at = now;
    watch_phone_status_t s;
    watch_phone_get(&s);
    if (!s_watch.init) {
        s_watch.init = true;
        s_watch.seq = s.put_seq; /* lo que ya había no avisa */
        return;
    }
    if (s.put_seq == s_watch.seq) {
        return;
    }
    s_watch.seq = s.put_seq;
    FlexPhoneNotif nt;
    if (s.link != FLP_LS_READY || !watch_phone_notif(s.put_id, &nt)) {
        return;
    }
    /* Con la pantalla apagada cuenta como bloqueo: solo app y remitente */
    char app[FLP_APPNAME_MAX + 8], title[FLP_TITLE_MAX + 8], body[FLP_TITLE_MAX + FLP_TEXT_MAX + 48];
    ui_phone_text(nt.app[0] ? nt.app : nt.pkg, app, sizeof(app), FLEX_FONT_BODY);
    ui_phone_text(nt.title, title, sizeof(title), FLEX_FONT_SMALL);
    char raw[FLP_TEXT_MAX];
    if (watch_phone_notif_body(&nt, !screen_on, raw, sizeof(raw))) {
        char t[FLP_TEXT_MAX + 16];
        ui_phone_text(raw, t, sizeof(t), FLEX_FONT_SMALL);
        snprintf(body, sizeof(body), "%s: %s", title, t);
    } else {
        snprintf(body, sizeof(body), "%s", title);
    }
    ui_screen_wake();
    ui_banner_show_notif(app, body, nt.id);
}
