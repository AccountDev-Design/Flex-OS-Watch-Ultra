/*
 * ui_alarms — Alarmas (F4.3): la pestaña Alarmas de Reloj con la próxima que
 * va a sonar, hasta 5 alarmas (hora local, días de la semana o una sola vez)
 * y su editor.
 *
 * Se guardan con watch_settings (ui_alarms_set). Suenan con el aviso a
 * pantalla completa de ui_clock_core, SOLO visual: altavoz y motor dependen
 * de la decisión D13. La página lo dice para no prometer un sonido que no
 * hay.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "watch_settings.h"
#include "watch_theme.h"

#define AL_TICK_MS 500
#define CHIP       42
#define BTN_W      150
#define BTN_H      60

static const char *const DAY_L[7] = {"D", "L", "M", "X", "J", "V", "S"};
static const int DAY_ORDER[7] = {1, 2, 3, 4, 5, 6, 0}; /* de lunes a domingo */

static int s_edit = -1;   /* alarma que se edita (-1 = nueva) */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void save(const alarm_set_t *set)
{
    ui_alarms_set(set); /* cambia ui_alarms_gen(): la lista se rehace */
}

/* ======================= Lista (vista de la pestaña Alarmas) =======================
 * Diseño del usuario (design/referencias/reloj_alarmas.png): una tarjeta de
 * vidrio en forma de píldora por alarma con la hora grande, los días debajo
 * y un interruptor a la derecha (verde: activa). Las desactivadas, en gris.
 * Tocar la tarjeta la edita; el interruptor la activa o desactiva y se guarda
 * en el acto. Arriba, la próxima que va a sonar y "+" para añadir (hasta 5). */

#define CARD_X   26
#define CARD_W   360
#define CARD_H   91
#define CARD_GAP 8
#define LIST_Y   106 /* en pantalla */
#define LIST_BOTTOM FLEX_LIST_BOTTOM /* la lista termina antes de la esquina redondeada */
#define HEAD_H   40
#define SW_W     75
#define SW_H     35
#define ADD_W    104
#define ADD_H    36

typedef struct {
    lv_obj_t *list;
    lv_obj_t *next;
    uint32_t gen;
    int64_t next_key;
} alp_t;

static void open_edit_cb(lv_event_t *e)
{
    s_edit = (int)(intptr_t)lv_event_get_user_data(e);
    ui_nav_push(ui_alarm_edit_create);
}

static void switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_current_target(e);
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    alarm_set_t set = *ui_alarms();
    if (i >= 0 && i < set.n) {
        set.a[i].on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
        save(&set); /* la lista se rehace con los colores nuevos */
    }
}

/* "7:00" + "a. m." (12 h) o "07:00" (24 h). */
static void alarm_time_text(const alarm_t *a, char *t, size_t tn, char *ampm, size_t an)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    const wt_datetime_t dt = {2026, 1, 1, a->hour, a->minute, 0, 4};
    if (ws.h24) {
        wt_fmt_hhmm(t, tn, &dt);
        ampm[0] = 0;
    } else {
        wt_fmt_hhmm12(t, tn, &dt);
        snprintf(ampm, an, "%s", a->hour < 12 ? "a. m." : "p. m.");
    }
}

static lv_obj_t *alarm_card(alp_t *p, int i, const alarm_t *a, int32_t y)
{
    lv_obj_t *c = watch_theme_lg_button(p->list, FLEX_LG_DARK, CARD_X, y, CARD_W, CARD_H, CARD_H / 2,
                                        FLEX_GLOW_TOP);
    lv_obj_add_event_cb(c, open_edit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    const uint32_t col = a->on ? FLEX_C_WHITE : FLEX_RD_ALARM_OFF;
    char t[16], ampm[8], d[32];
    alarm_time_text(a, t, sizeof(t), ampm, sizeof(ampm));
    lv_obj_t *tl = watch_theme_label(c, FLEX_FONT_S40, col, t);
    lv_obj_set_pos(tl, 36, 14);
    if (ampm[0]) {
        lv_obj_t *al = watch_theme_label(c, FLEX_FONT_S40, col, ampm);
        lv_obj_update_layout(tl);
        lv_obj_align_to(al, tl, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, 0);
    }
    alarm_days_text(a->days, d, sizeof(d));
    lv_obj_t *dl = watch_theme_label(c, FLEX_FONT_S16, a->on ? FLEX_RD_ALARM_SUB : FLEX_RD_ALARM_OFF,
                                     strcmp(d, "Todos los días") == 0 ? "Diariamente" : d);
    lv_obj_set_pos(dl, 40, 58);
    lv_obj_t *sw = watch_theme_switch(c, SW_W, SW_H);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -24, 0);
    if (a->on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    return c;
}

static void build_list(alp_t *p)
{
    lv_obj_clean(p->list);
    const alarm_set_t *set = ui_alarms();
    /* Cabecera: la próxima alarma y "+" */
    p->next = watch_theme_label(p->list, FLEX_FONT_S16, FLEX_C_TXT2, "");
    lv_obj_set_width(p->next, CARD_W - ADD_W - 24);
    lv_label_set_long_mode(p->next, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(p->next, CARD_X + 14, (HEAD_H - lv_font_get_line_height(FLEX_FONT_S16)) / 2);
    p->next_key = INT64_MIN;
    if (set->n < AL_MAX) {
        lv_obj_t *add = watch_theme_lg_button(p->list, FLEX_LG_DARK, CARD_X + CARD_W - ADD_W, (HEAD_H - ADD_H) / 2,
                                              ADD_W, ADD_H, ADD_H / 2, FLEX_GLOW_NONE);
        lv_obj_set_ext_click_area(add, 12);
        lv_obj_t *plus = flex_icon_create(add, FLEX_ICON_PLUS, 16, FLEX_C_WHITE);
        lv_obj_align(plus, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *t = watch_theme_label(add, FLEX_FONT_S16, FLEX_C_WHITE, "Añadir");
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 34, 1);
        lv_obj_add_event_cb(add, open_edit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    }
    int32_t y = HEAD_H + 8;
    for (int i = 0; i < set->n; i++) {
        alarm_card(p, i, &set->a[i], y);
        y += CARD_H + CARD_GAP;
    }
    if (set->n == 0) {
        lv_obj_t *e = watch_theme_label(p->list, FLEX_FONT_S22, FLEX_C_TXT2, "Sin alarmas");
        lv_obj_align(e, LV_ALIGN_TOP_MID, 0, y + 40);
        y += 80;
    }
    lv_obj_t *n = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                    "Aviso solo en pantalla: el sonido y la vibración están pendientes.");
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(n, CARD_W - 28);
    lv_obj_set_pos(n, CARD_X + 14, y + 6);
    lv_obj_t *pad = lv_obj_create(p->list); /* aire bajo la nota, por encima del borde redondeado */
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, FLEX_LIST_BOTTOM);
    lv_obj_set_pos(pad, 0, y + 6 + 2 * lv_font_get_line_height(FLEX_FONT_SMALL));
    p->gen = ui_alarms_gen();
}

static void refresh(alp_t *p)
{
    if (p->gen != ui_alarms_gen()) {
        build_list(p);
    }
    int64_t now;
    int64_t next = -1;
    if (ui_clock_local_now(&now)) {
        next = alarms_next(ui_alarms(), now, NULL);
    }
    const int64_t key = next >= 0 ? next * 2 + (now / 86400 != next / 86400) : -1;
    if (key == p->next_key) {
        return;
    }
    p->next_key = key;
    if (next >= 0) {
        char b[32], t[48];
        watch_settings_t ws;
        watch_settings_get(&ws);
        alarm_when_text(next, now, ws.h24, b, sizeof(b));
        snprintf(t, sizeof(t), "Próxima: %s", b);
        lv_label_set_text(p->next, t);
    } else {
        lv_label_set_text(p->next, ui_alarms()->n ? "Ninguna activa" : "Sin alarmas");
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

lv_obj_t *ui_alarms_view(lv_obj_t *page)
{
    alp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *v = ui_reloj_view(page);
    p->list = lv_obj_create(v);
    lv_obj_remove_style_all(p->list);
    lv_obj_set_pos(p->list, 0, LIST_Y - UI_RELOJ_TOP);
    lv_obj_set_size(p->list, FLEX_SCREEN_W, FLEX_SCREEN_H - LIST_Y - LIST_BOTTOM);
    lv_obj_set_scroll_dir(p->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_CLICKABLE); /* LVGL solo desplaza desde un objeto pulsable */
    build_list(p);
    refresh(p);
    lv_obj_add_event_cb(v, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(v, tick_cb, AL_TICK_MS, p);
    return v;
}

lv_obj_t *ui_alarms_create(lv_obj_t *parent)
{
    return ui_reloj_page(parent, UI_RELOJ_ALARMAS);
}

/* ======================= Editor ======================= */

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *min;
    lv_obj_t *chips[7]; /* en orden de DAY_ORDER */
    lv_obj_t *active;
    alarm_t a;
} ale_t;

static void paint_chips(ale_t *p)
{
    for (int k = 0; k < 7; k++) {
        const bool on = p->a.days & (1u << DAY_ORDER[k]);
        watch_theme_button_set(p->chips[k], NULL, on ? FLEX_C_PRIMARY : FLEX_C_SURF);
    }
    watch_theme_button_set(p->active, p->a.on ? "Activa" : "Desactivada", p->a.on ? FLEX_C_OK : FLEX_C_SURF2);
}

static void chip_cb(lv_event_t *e)
{
    ale_t *p = lv_obj_get_user_data(lv_event_get_current_target(e));
    const int wd = (int)(intptr_t)lv_event_get_user_data(e);
    p->a.days ^= (uint8_t)(1u << wd);
    paint_chips(p);
}

static void active_cb(lv_event_t *e)
{
    ale_t *p = lv_event_get_user_data(e);
    p->a.on = !p->a.on;
    paint_chips(p);
}

static void save_cb(lv_event_t *e)
{
    ale_t *p = lv_event_get_user_data(e);
    p->a.hour = (uint8_t)lv_roller_get_selected(p->hour);
    p->a.minute = (uint8_t)lv_roller_get_selected(p->min);
    alarm_set_t set = *ui_alarms();
    if (s_edit >= 0 && s_edit < set.n) {
        set.a[s_edit] = p->a;
    } else if (set.n < AL_MAX) {
        set.a[set.n++] = p->a;
    }
    save(&set);
    ui_nav_back();
}

static void delete_alarm_cb(lv_event_t *e)
{
    (void)e;
    alarm_set_t set = *ui_alarms();
    if (s_edit >= 0 && s_edit < set.n) {
        memmove(&set.a[s_edit], &set.a[s_edit + 1], sizeof(set.a[0]) * (size_t)(set.n - s_edit - 1));
        set.n--;
        save(&set);
    }
    ui_nav_back();
}

lv_obj_t *ui_alarm_edit_create(lv_obj_t *parent)
{
    ale_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    const alarm_set_t *set = ui_alarms();
    const bool existing = s_edit >= 0 && s_edit < set->n;
    p->a = existing ? set->a[s_edit] : (alarm_t){1, 7, 0, 0x3E}; /* nueva: 7:00, de lunes a viernes */

    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, existing ? "Editar alarma" : "Nueva alarma", back_cb, NULL);

    static char hours[24 * 3], mins[60 * 3];
    if (!hours[0]) {
        char *q = hours;
        for (int i = 0; i < 24; i++) {
            q += sprintf(q, i < 23 ? "%02d\n" : "%02d", i);
        }
        q = mins;
        for (int i = 0; i < 60; i++) {
            q += sprintf(q, i < 59 ? "%02d\n" : "%02d", i);
        }
    }
    p->hour = ui_roller(page, hours, LV_ROLLER_MODE_INFINITE, FLEX_FONT_TITLE, 96);
    p->min = ui_roller(page, mins, LV_ROLLER_MODE_INFINITE, FLEX_FONT_TITLE, 96);
    const int32_t y = FLEX_HDR_H + 2;
    lv_obj_align(p->hour, LV_ALIGN_TOP_MID, -62, y);
    lv_obj_align(p->min, LV_ALIGN_TOP_MID, 62, y);
    lv_roller_set_selected(p->hour, p->a.hour, LV_ANIM_OFF);
    lv_roller_set_selected(p->min, p->a.minute, LV_ANIM_OFF);
    lv_obj_update_layout(page);
    const int32_t hr = lv_obj_get_height(p->hour);
    lv_obj_t *colon = watch_theme_label(page, FLEX_FONT_TITLE, FLEX_C_TXT, ":");
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, y + (hr - lv_font_get_line_height(FLEX_FONT_TITLE)) / 2);

    /* Días: L M X J V S D (ninguno = una sola vez) */
    const int32_t gap = 6, cw = 7 * CHIP + 6 * gap, cy = y + hr + 12;
    for (int k = 0; k < 7; k++) {
        lv_obj_t *c = watch_theme_button(page, DAY_L[DAY_ORDER[k]], FLEX_C_SURF, CHIP, CHIP);
        lv_obj_set_pos(c, (FLEX_SCREEN_W - cw) / 2 + k * (CHIP + gap), cy);
        lv_obj_set_user_data(c, p);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)DAY_ORDER[k]);
        p->chips[k] = c;
    }
    lv_obj_t *hint = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, "Sin días: suena una sola vez");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, cy + CHIP + 6);
    p->active = watch_theme_button(page, "Activa", FLEX_C_OK, 180, 44);
    lv_obj_align(p->active, LV_ALIGN_TOP_MID, 0, cy + CHIP + 36);
    lv_obj_add_event_cb(p->active, active_cb, LV_EVENT_CLICKED, p);

    lv_obj_t *left = watch_theme_button(page, existing ? "Eliminar" : "Cancelar",
                                        existing ? FLEX_C_DANGER : FLEX_C_SURF2, BTN_W, BTN_H);
    lv_obj_align(left, LV_ALIGN_BOTTOM_MID, -(BTN_W / 2 + 8), -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(left, existing ? delete_alarm_cb : back_cb, LV_EVENT_CLICKED, p);
    lv_obj_t *ok = watch_theme_button(page, "Guardar", FLEX_C_PRIMARY, BTN_W, BTN_H);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, BTN_W / 2 + 8, -FLEX_LIST_BOTTOM);
    lv_obj_add_event_cb(ok, save_cb, LV_EVENT_CLICKED, p);

    paint_chips(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    return page;
}
