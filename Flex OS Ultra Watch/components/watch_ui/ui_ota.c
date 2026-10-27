/*
 * ui_ota — Actualizaciones (F7): las pantallas de FlexOS_OTA (ota.md §4)
 * llevadas a 410x502 y a las piezas del reloj.
 *
 *  - Ajustes > Actualizaciones (OVERLAY_SETTINGS): estado, botones y, si hay
 *    versión nueva, versión, tamaño y novedades. RELOJ: el modal de novedades
 *    (OVERLAY_CHANGELOG) es esta misma página: tiene lo mismo y desplaza.
 *  - Pantalla de instalación (OVERLAY_DOWNLOADING): sin gesto "volver",
 *    "Cancelar" solo al conectar y descargar, la pantalla no se apaga y, si
 *    falla, "No se pudo actualizar" + "El reloj no se ha modificado.".
 *  - Avisos (OVERLAY_NOTIFICATION): con la esfera delante, "Nueva versión
 *    X" una vez por versión (RELOJ: el texto de la referencia no cabe en la
 *    tarjeta) y, RELOJ, tras reiniciar "Flex OS actualizado" o "No se pudo
 *    actualizar / Vuelve la versión anterior".
 * Todo lee instantáneas de watch_ota; nada espera a la red.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flex_icons.h"
#include "watch_ota.h"
#include "watch_theme.h"

#define OTA_TICK_MS   100
#define WATCH_POLL_MS 1000
#define PLAT_NAME     "Flex OS Watch Ultra"

/* ======================= Utilidades ======================= */

static lv_obj_t *card_col(lv_obj_t *list, int32_t gap)
{
    lv_obj_t *c = flex_glass_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
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

static void set_text(lv_obj_t *l, const char *t)
{
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* Novedades del servidor preparadas para la fuente, línea a línea
 * (ui_phone_text convierte los saltos en espacios). */
static void log_text(const char *in, char *out, size_t n)
{
    size_t at = 0;
    out[0] = 0;
    while (*in && at + 2 < n) {
        const size_t len = strcspn(in, "\n");
        char line[OTA_LOG_MAX], fit[OTA_LOG_MAX + 16];
        const size_t k = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, in, k);
        line[k] = 0;
        ui_phone_text(line, fit, sizeof(fit), FLEX_FONT_SMALL);
        at += (size_t)snprintf(out + at, n - at, "%s%s", at ? "\n" : "", fit);
        if (at >= n) {
            at = n - 1;
            out[at] = 0;
        }
        in += len;
        if (*in == '\n') {
            in++;
        }
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* ======================= Ajustes > Actualizaciones ======================= */

typedef struct {
    lv_obj_t *list;
    uint32_t shape; /* lo que cambia la forma de la página */
    bool built;
} ota_page_t;

static uint32_t shape_of(const watch_ota_status_t *s)
{
    uint32_t h = (uint32_t)s->state | (uint32_t)s->err << 8 | (uint32_t)s->has_upd << 16 |
                 (uint32_t)s->pending_verify << 17;
    for (const char *p = s->ver; *p; p++) {
        h = h * 31u + (uint8_t)*p;
    }
    return h;
}

static void check_cb(lv_event_t *e)
{
    (void)e;
    watch_ota_check();
}

static void install_cb(lv_event_t *e)
{
    (void)e;
    watch_ota_install();
    ui_nav_push_opts(ui_ota_install_create, false);
}

static void build_page(ota_page_t *p, const watch_ota_status_t *s)
{
    lv_obj_clean(p->list);
    const bool busy = ota_busy_state(s->state);
    const bool upd = s->state == OTA_AVAILABLE || s->has_upd;
    char b[96];

    /* Tarjeta principal: icono y título según el estado (OTA.cpp:1219-1231). */
    lv_obj_t *hero = card_col(p->list, 6);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *title;
    flex_icon_t icon;
    uint32_t rgb;
    if (upd) {
        title = "Actualización disponible", icon = FLEX_ICON_UPDATE, rgb = FLEX_C_ACCENT;
    } else if (s->state == OTA_ERROR) {
        title = "No se pudo comprobar", icon = FLEX_ICON_CROSS, rgb = FLEX_C_ERR;
    } else if (s->state == OTA_CHECKING) {
        title = "Buscando...", icon = FLEX_ICON_UPDATE, rgb = FLEX_C_ACCENT;
    } else if (s->state == OTA_UP_TO_DATE) {
        title = "El sistema está al día", icon = FLEX_ICON_CHECK, rgb = FLEX_C_OK;
    } else {
        /* RELOJ: sin comprobar no se dice "al día" (A8). */
        title = "Sin comprobar todavía", icon = FLEX_ICON_UPDATE, rgb = FLEX_C_TXT2;
    }
    flex_icon_create(hero, icon, 56, rgb);
    lv_obj_t *t = para(hero, FLEX_FONT_TITLE, FLEX_C_TXT, title);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    snprintf(b, sizeof(b), "%s %s", PLAT_NAME, s->local);
    t = para(hero, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

    /* Botones (OTA.cpp:1270-1288) */
    if (busy) {
        char st[64];
        watch_ota_status_text(st, sizeof(st));
        lv_obj_t *l = para(p->list, FLEX_FONT_BODY, FLEX_C_ACCENT, st);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    } else if (upd) {
        lv_obj_t *bt = flex_glass_button(p->list, "Actualizar ahora", FLEX_C_PRIMARY, LV_PCT(100), 60);
        lv_obj_add_event_cb(bt, install_cb, LV_EVENT_CLICKED, NULL);
        bt = flex_glass_button(p->list, "Buscar de nuevo", FLEX_GLASS_TINT_NONE, LV_PCT(100), 60);
        lv_obj_add_event_cb(bt, check_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_t *bt = flex_glass_button(p->list, "Buscar actualización", FLEX_C_PRIMARY, LV_PCT(100), 60);
        lv_obj_add_event_cb(bt, check_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Detalle */
    if (upd) {
        watch_theme_info_row(p->list, "Versión disponible", s->ver);
        if (s->size) {
            ota_fmt_bytes(s->size, b, sizeof(b));
            watch_theme_info_row(p->list, "Tamaño", b);
        }
        if (s->state == OTA_ERROR) { /* RELOJ: el último fallo también se ve aquí (A24) */
            lv_obj_t *c = card_col(p->list, 4);
            para(c, FLEX_FONT_BODY, FLEX_C_ERR, "Último intento");
            para(c, FLEX_FONT_SMALL, FLEX_C_TXT2, ota_err_text(s->err));
        }
        watch_theme_section(p->list, "Novedades");
        lv_obj_t *c = card_col(p->list, 4);
        char log[OTA_LOG_MAX + 16];
        log_text(s->changelog[0] ? s->changelog : "Sin notas de la versión.", log, sizeof(log));
        para(c, FLEX_FONT_SMALL, FLEX_C_TXT, log);
    } else if (s->state == OTA_ERROR) {
        lv_obj_t *c = card_col(p->list, 4);
        para(c, FLEX_FONT_BODY, FLEX_C_ERR, ota_err_text(s->err));
        if (s->err == OTA_ERR_NO_WIFI) {
            para(c, FLEX_FONT_SMALL, FLEX_C_TXT2, "Conecta el Wi-Fi en Ajustes > Wi-Fi.");
        }
    }

    /* Este reloj */
    watch_theme_section(p->list, "Este reloj");
    watch_theme_info_row(p->list, "Versión", s->local);
    if (s->have_key) {
        snprintf(b, sizeof(b), "%.8s", s->key_fp);
        watch_theme_info_row(p->list, "Clave de firma", b);
    } else {
        watch_theme_info_row(p->list, "Clave de firma", "Ninguna");
        lv_obj_t *c = card_col(p->list, 4);
        para(c, FLEX_FONT_SMALL, FLEX_C_TXT2,
             "Sin clave de firma el reloj comprueba y enseña las novedades, pero no instala nada.");
    }
    if (s->pending_verify) {
        watch_theme_info_row(p->list, "Estado", "Confirmando");
    }
    p->built = true;
}

static void page_tick(lv_timer_t *tm)
{
    ota_page_t *p = lv_timer_get_user_data(tm);
    watch_ota_status_t s;
    watch_ota_get(&s);
    const uint32_t sh = shape_of(&s);
    if (!p->built || sh != p->shape) {
        p->shape = sh;
        build_page(p, &s);
    }
}

static void page_delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_ota_create(lv_obj_t *parent)
{
    ota_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Actualizaciones", back_cb, NULL);
    p->list = watch_theme_list(page);
    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, p);
    lv_timer_t *tm = ui_page_timer(page, page_tick, 250, p);
    page_tick(tm);
    return page;
}

/* ======================= Instalación ======================= */

#define I_W        330
#define I_X        ((FLEX_SCREEN_W - I_W) / 2)
#define BAR_W      300
#define BTN_W      220
#define BTN_H      60
#define BTN_Y      392

typedef struct {
    lv_obj_t *icon_upd, *icon_err;
    lv_obj_t *title, *sub;
    lv_obj_t *pct, *bar, *phase, *bytes, *speed;
    lv_obj_t *err_text, *err_foot;
    lv_obj_t *cancel, *ok, *reboot;
    uint32_t gen;
    bool init;
} inst_t;

static lv_obj_t *center_label(lv_obj_t *page, const lv_font_t *font, uint32_t rgb, int32_t y)
{
    lv_obj_t *l = watch_theme_label(page, font, rgb, "");
    lv_obj_set_width(l, I_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, I_X, y);
    return l;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    watch_ota_cancel();
}

static void inst_tick(lv_timer_t *tm)
{
    inst_t *in = lv_timer_get_user_data(tm);
    watch_ota_status_t s;
    watch_ota_get(&s);
    if (in->init && s.gen == in->gen) {
        return;
    }
    in->init = true;
    in->gen = s.gen;
    const bool err = s.state == OTA_ERROR;
    char b[96];
    show(in->icon_upd, !err);
    show(in->icon_err, err);
    set_text(in->title, err ? "No se pudo actualizar" : "Actualizando Flex OS");
    snprintf(b, sizeof(b), "%s %s", PLAT_NAME, s.ver);
    set_text(in->sub, b);
    /* Banda dinámica: %, barra, fase, bytes y velocidad (OTA.cpp:1100-1180) */
    show(in->pct, !err);
    show(in->bar, !err);
    show(in->phase, !err);
    show(in->bytes, !err && s.total > 0);
    show(in->speed, !err && s.state == OTA_DOWNLOADING && s.bps > 0);
    if (!err) {
        snprintf(b, sizeof(b), "%u %%", (unsigned)s.pct);
        set_text(in->pct, b);
        lv_bar_set_value(in->bar, s.pct, LV_ANIM_OFF);
        set_text(in->phase, ota_phase_text(s.state));
        if (s.total > 0) {
            char d[24], t[24];
            ota_fmt_bytes(s.done, d, sizeof(d));
            ota_fmt_bytes(s.total, t, sizeof(t));
            snprintf(b, sizeof(b), "%s / %s", d, t);
            set_text(in->bytes, b);
        }
        if (s.bps > 0) {
            char v[24];
            ota_fmt_bytes(s.bps, v, sizeof(v));
            snprintf(b, sizeof(b), "%s/s", v);
            set_text(in->speed, b);
        }
    }
    /* Pie */
    show(in->cancel, s.state == OTA_CONNECTING || s.state == OTA_DOWNLOADING);
    show(in->reboot, s.state == OTA_REBOOTING);
    show(in->err_text, err);
    show(in->err_foot, err);
    show(in->ok, err);
    if (err) {
        set_text(in->err_text, ota_err_text(s.err));
    }
}

lv_obj_t *ui_ota_install_create(lv_obj_t *parent)
{
    inst_t *in = lv_malloc_zeroed(sizeof(*in));
    LV_ASSERT_MALLOC(in);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);

    in->icon_upd = flex_icon_create(page, FLEX_ICON_UPDATE, 56, FLEX_C_ACCENT);
    lv_obj_set_pos(in->icon_upd, (FLEX_SCREEN_W - 56) / 2, 40);
    in->icon_err = flex_icon_create(page, FLEX_ICON_CROSS, 56, FLEX_C_ERR);
    lv_obj_set_pos(in->icon_err, (FLEX_SCREEN_W - 56) / 2, 40);
    in->title = center_label(page, FLEX_FONT_TITLE, FLEX_C_TXT, 108);
    in->sub = center_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, 148);
    in->pct = center_label(page, FLEX_FONT_LARGE, FLEX_C_TXT, 190);
    in->bar = lv_bar_create(page);
    lv_obj_set_size(in->bar, BAR_W, 12);
    lv_obj_set_pos(in->bar, (FLEX_SCREEN_W - BAR_W) / 2, 248);
    lv_bar_set_range(in->bar, 0, 100);
    lv_obj_set_style_radius(in->bar, 6, 0);
    lv_obj_set_style_radius(in->bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(in->bar, flex_color(FLEX_C_TRACK), 0);
    lv_obj_set_style_bg_opa(in->bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(in->bar, flex_color(FLEX_C_ACCENT), LV_PART_INDICATOR);
    in->phase = center_label(page, FLEX_FONT_BODY, FLEX_C_ACCENT, 274);
    in->bytes = center_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, 310);
    in->speed = center_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, 336);
    /* Error: en el sitio de la banda dinámica */
    in->err_text = center_label(page, FLEX_FONT_BODY, FLEX_C_ERR, 200);
    in->err_foot = center_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, 300);
    lv_label_set_text(in->err_foot, "El reloj no se ha modificado.");

    in->cancel = flex_glass_button(page, "Cancelar", FLEX_GLASS_TINT_NONE, BTN_W, BTN_H);
    lv_obj_set_pos(in->cancel, (FLEX_SCREEN_W - BTN_W) / 2, BTN_Y);
    lv_obj_add_event_cb(in->cancel, cancel_cb, LV_EVENT_CLICKED, NULL);
    in->ok = flex_glass_button(page, "Entendido", FLEX_C_PRIMARY, BTN_W, BTN_H);
    lv_obj_set_pos(in->ok, (FLEX_SCREEN_W - BTN_W) / 2, BTN_Y);
    lv_obj_add_event_cb(in->ok, back_cb, LV_EVENT_CLICKED, NULL);
    in->reboot = center_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, BTN_Y + 16);
    lv_label_set_text(in->reboot, "El reloj se reiniciará solo");

    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, in);
    ui_screen_hold_page(page); /* la pantalla no se apaga mientras se instala */
    lv_timer_t *tm = ui_page_timer(page, inst_tick, OTA_TICK_MS, in);
    inst_tick(tm);
    return page;
}

/* ======================= Avisos ======================= */

static struct {
    uint32_t at;
    bool init;
    char shown_ver[OTA_VER_MAX]; /* una vez por versión en cada arranque */
} s_w;

static bool on_face(void)
{
    return ui_nav_depth() <= 1 && !ui_nav_busy() && !ui_qp_active() && !ui_splash_active() && ui_screen_is_on() &&
           !ui_banner_active();
}

void ui_ota_watch_tick(void)
{
    const uint32_t now = lv_tick_get();
    if (s_w.init && now - s_w.at < WATCH_POLL_MS) {
        return;
    }
    s_w.init = true;
    s_w.at = now;
    watch_ota_status_t s;
    watch_ota_get(&s);
    if (!on_face()) {
        return;
    }
    char b[64];
    if (s.boot == OTA_BOOT_UPDATED) {
        snprintf(b, sizeof(b), "%s %s", PLAT_NAME, s.local);
        ui_banner_show_ota("Flex OS actualizado", b, FLEX_C_OK, false);
        watch_ota_boot_seen();
    } else if (s.boot == OTA_BOOT_REVERTED) {
        ui_banner_show_ota("No se pudo actualizar", "Vuelve la versión anterior", FLEX_C_WARN, true);
        watch_ota_boot_seen();
    } else if (s.state == OTA_AVAILABLE && s.ver[0] && strcmp(s.ver, s_w.shown_ver) != 0) {
        memcpy(s_w.shown_ver, s.ver, sizeof(s_w.shown_ver));
        snprintf(b, sizeof(b), "Nueva versión %s", s.ver);
        ui_banner_show_ota(b, "Toca para ver las novedades", FLEX_C_TXT, true);
    }
}
