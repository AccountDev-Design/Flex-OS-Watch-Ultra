/*
 * ui_reloj — app Reloj (IC_RELOJ de Flex OS Ultra) con el diseño del usuario
 * (design/referencias/reloj_*.png, docs/REDISENO-2026-10.md):
 *
 *  - barra de pestañas de vidrio arriba con cuatro iconos (hora, cronómetro,
 *    alarmas, temporizador) y la pestaña activa resaltada en azul, que se
 *    desliza al cambiar;
 *  - debajo, la vista de la pestaña: una sola a la vez (se crea al elegirla y
 *    se borra al dejarla, con sus temporizadores); el estado vive en
 *    ui_clock_core, así que el cronómetro y el temporizador siguen contando
 *    y nada se pierde al cambiar de pestaña o salir de la app.
 *
 * Vista Hora: la hora real en grande (League Spartan Bold), la fecha larga y
 * dos tarjetas de vidrio: "Fecha" (abre Fecha y hora) y "Formato" (cambia
 * entre 12 y 24 h, el ajuste real). Sin hora fiable, "--:--" y el motivo.
 *
 * Se vuelve con los gestos de borde (no hay cabecera en el diseño).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define RELOJ_TICK_MS 1000
#define TAB_X         21
#define TAB_Y         37
#define TAB_W         368
#define TAB_H         52
#define TAB_ICON      44
#define TAB_ANIM_MS   180

/* Centros de los iconos y ancho del resaltado medidos en las referencias (no
 * son equidistantes; la primera y la última pestaña llegan al extremo). */
static const int16_t kTabCx[UI_RELOJ_TABS] = {82, 164, 242, 326};
static const int16_t kTabHiW[UI_RELOJ_TABS] = {118, 84, 96, 112};
static const flex_icon_t kTabIcon[UI_RELOJ_TABS] = {FLEX_ICON_CLOCK_SOLID, FLEX_ICON_STOPWATCH, FLEX_ICON_ALARM,
                                                    FLEX_ICON_HOURGLASS};

typedef struct {
    lv_obj_t *page;
    lv_obj_t *hi;   /* resaltado de la pestaña activa */
    lv_obj_t *icons[UI_RELOJ_TABS];
    lv_obj_t *view;
    int tab;
} reloj_t;

static int s_last_tab; /* la app vuelve a la última pestaña usada */

/* x del resaltado en coordenadas de la barra (dentro de su borde de 2 px) */
static int32_t hi_x(int tab)
{
    return LV_CLAMP(TAB_X + 2, kTabCx[tab] - kTabHiW[tab] / 2, TAB_X + TAB_W - 2 - kTabHiW[tab]) - TAB_X - 2;
}

static void hi_w_cb(void *var, int32_t v)
{
    lv_obj_set_width(var, v);
}

static lv_obj_t *build_view(lv_obj_t *page, int tab)
{
    switch (tab) {
    case UI_RELOJ_CRONO:
        return ui_stopwatch_view(page);
    case UI_RELOJ_ALARMAS:
        return ui_alarms_view(page);
    case UI_RELOJ_TEMP:
        return ui_timer_view(page);
    default:
        return ui_reloj_time_view(page);
    }
}

static void hi_x_cb(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void select_tab(reloj_t *r, int tab, bool animate)
{
    if (tab == r->tab && r->view) {
        return;
    }
    const int old = r->tab;
    r->tab = tab;
    s_last_tab = tab;
    if (r->view) {
        lv_obj_delete(r->view); /* con sus temporizadores (ui_page_timer) */
    }
    r->view = build_view(r->page, tab);
    lv_obj_move_to_index(r->view, 0); /* debajo de la barra y de los bordes */
    for (int i = 0; i < UI_RELOJ_TABS; i++) {
        flex_icon_set_color2(r->icons[i], i == tab ? FLEX_RD_TAB_ACTIVE : FLEX_RD_TAB_BOT);
    }
    lv_anim_delete(r->hi, hi_x_cb);
    lv_anim_delete(r->hi, hi_w_cb);
    if (animate && old >= 0) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, r->hi);
        lv_anim_set_exec_cb(&a, hi_x_cb);
        lv_anim_set_values(&a, lv_obj_get_x(r->hi), hi_x(tab));
        lv_anim_set_duration(&a, TAB_ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a); /* ligadas al resaltado: se borran con la página */
        lv_anim_set_exec_cb(&a, hi_w_cb);
        lv_anim_set_values(&a, lv_obj_get_width(r->hi), kTabHiW[tab]);
        lv_anim_start(&a);
    } else {
        lv_obj_set_x(r->hi, hi_x(tab));
        lv_obj_set_width(r->hi, kTabHiW[tab]);
    }
}

static void tab_cb(lv_event_t *e)
{
    reloj_t *r = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    /* La pestaña más cercana al dedo (la barra entera es la zona táctil). */
    int best = 0;
    for (int i = 1; i < UI_RELOJ_TABS; i++) {
        if (LV_ABS(p.x - kTabCx[i]) < LV_ABS(p.x - kTabCx[best])) {
            best = i;
        }
    }
    select_tab(r, best, true);
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

int ui_reloj_tab(lv_obj_t *page)
{
    reloj_t *r = page ? lv_obj_get_user_data(page) : NULL;
    return (r && r->page == page) ? r->tab : -1;
}

void ui_reloj_select(lv_obj_t *page, int tab)
{
    reloj_t *r = page ? lv_obj_get_user_data(page) : NULL;
    if (r && r->page == page && tab >= 0 && tab < UI_RELOJ_TABS) {
        select_tab(r, tab, false);
    }
}

lv_obj_t *ui_reloj_page(lv_obj_t *parent, int tab)
{
    reloj_t *r = lv_malloc_zeroed(sizeof(*r));
    if (!r) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    r->page = page;
    r->tab = -1;

    /* Barra de pestañas: vidrio oscuro con degradado gris y reflejo inferior */
    lv_obj_t *bar = watch_theme_lg(page, FLEX_LG_DARK, TAB_X, TAB_Y, TAB_W, TAB_H, TAB_H / 2, FLEX_GLOW_BOTTOM);
    lv_obj_set_style_bg_color(bar, flex_color(FLEX_RD_TAB_TOP), 0);
    lv_obj_set_style_bg_grad_color(bar, flex_color(FLEX_RD_TAB_BOT), 0);
    lv_obj_set_style_border_color(bar, flex_color(0x9A9A9A), 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, tab_cb, LV_EVENT_CLICKED, r);
    r->hi = lv_obj_create(bar);
    lv_obj_remove_style_all(r->hi);
    lv_obj_set_size(r->hi, kTabHiW[0], TAB_H - 8);
    lv_obj_set_style_radius(r->hi, (TAB_H - 8) / 2, 0);
    lv_obj_set_style_bg_color(r->hi, flex_color(0x6B85FF), 0);
    lv_obj_set_style_bg_grad_color(r->hi, flex_color(FLEX_RD_TAB_ACTIVE), 0);
    lv_obj_set_style_bg_grad_dir(r->hi, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(r->hi, LV_OPA_COVER, 0);
    lv_obj_remove_flag(r->hi, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(r->hi, hi_x(tab < 0 || tab >= UI_RELOJ_TABS ? 0 : tab), 2);
    for (int i = 0; i < UI_RELOJ_TABS; i++) {
        r->icons[i] = flex_icon_create(bar, kTabIcon[i], TAB_ICON, FLEX_C_WHITE);
        lv_obj_set_pos(r->icons[i], kTabCx[i] - TAB_X - 2 - TAB_ICON / 2, (TAB_H - 4 - TAB_ICON) / 2);
    }
    lv_obj_set_user_data(page, r);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, r);
    select_tab(r, tab < 0 || tab >= UI_RELOJ_TABS ? 0 : tab, false);
    return page;
}

lv_obj_t *ui_reloj_create(lv_obj_t *parent)
{
    return ui_reloj_page(parent, s_last_tab);
}

/* ======================= Vista Hora ======================= */

typedef struct {
    lv_obj_t *time, *date, *card_date, *card_fmt;
    int64_t key;
} hora_t;

#define HORA_TIME_Y  97   /* parte alta de la línea: el dígito empieza en y ~ 113, como en el diseño */
#define HORA_DATE_Y  262
#define CARD_Y       312
#define CARD_H       105
#define CARD_W       171
#define CARD_R       40

static void hora_refresh(hora_t *h)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);
    const int64_t key = t.key * 2 + (ws.h24 ? 1 : 0);
    if (key == h->key) {
        return;
    }
    h->key = key;
    lv_label_set_text(h->time, t.time);
    watch_theme_set_text_color(h->time, t.valid ? FLEX_C_WHITE : FLEX_C_MUTE);
    lv_label_set_text(h->date, t.date);
    char b[24];
    if (t.valid) {
        wt_fmt_date_short(b, sizeof(b), &dt);
    } else {
        snprintf(b, sizeof(b), "--");
    }
    lv_label_set_text(lv_obj_get_child(h->card_date, 2), b);
    lv_label_set_text(lv_obj_get_child(h->card_fmt, 2), ws.h24 ? "24 h" : "12 h");
}

static void hora_tick_cb(lv_timer_t *t)
{
    hora_refresh(lv_timer_get_user_data(t));
}

static void date_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_datetime_create);
}

static void fmt_cb(lv_event_t *e)
{
    hora_t *h = lv_event_get_user_data(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.h24 = !ws.h24;
    watch_settings_update(&ws);
    hora_refresh(h);
}

static lv_obj_t *card(lv_obj_t *parent, int32_t x, const char *title, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *c = watch_theme_lg_button(parent, FLEX_LG_DARK, x, CARD_Y - UI_RELOJ_TOP, CARD_W, CARD_H, CARD_R,
                                        FLEX_GLOW_BOTTOM);
    lv_obj_t *t = watch_theme_label(c, FLEX_FONT_S22, FLEX_C_WHITE, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *v = watch_theme_label(c, FLEX_FONT_S22, FLEX_C_WHITE, "");
    lv_obj_set_width(v, CARD_W - 24);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, ud);
    return c; /* hijos: 0 reflejo, 1 título, 2 valor */
}

lv_obj_t *ui_reloj_view(lv_obj_t *page)
{
    lv_obj_t *v = lv_obj_create(page);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, 0, UI_RELOJ_TOP);
    lv_obj_set_size(v, FLEX_SCREEN_W, FLEX_SCREEN_H - UI_RELOJ_TOP);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return v;
}

static void hora_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_reloj_time_view(lv_obj_t *page)
{
    hora_t *h = lv_malloc_zeroed(sizeof(*h));
    LV_ASSERT_MALLOC(h);
    h->key = INT64_MIN;
    lv_obj_t *v = ui_reloj_view(page);
    h->time = watch_theme_label(v, FLEX_FONT_S_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(h->time, LV_ALIGN_TOP_MID, 0, HORA_TIME_Y - UI_RELOJ_TOP);
    h->date = watch_theme_label(v, FLEX_FONT_S22, FLEX_C_WHITE, "");
    lv_obj_set_width(h->date, FLEX_SCREEN_W - 2 * FLEX_SAFE_X);
    lv_obj_set_style_text_align(h->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(h->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(h->date, LV_ALIGN_TOP_MID, 0, HORA_DATE_Y - UI_RELOJ_TOP);
    h->card_date = card(v, 24, "Fecha", date_cb, h);
    h->card_fmt = card(v, FLEX_SCREEN_W - 24 - CARD_W, "Formato", fmt_cb, h);
    hora_refresh(h);
    lv_obj_add_event_cb(v, hora_free_cb, LV_EVENT_DELETE, h);
    ui_page_timer(v, hora_tick_cb, RELOJ_TICK_MS, h);
    return v;
}
