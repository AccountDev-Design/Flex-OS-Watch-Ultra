/*
 * ui_wb_home — Bienestar: la app (cajón) y las tarjetas (gesto desde el borde
 * derecho de la esfera, ui_nav).
 *
 * Las dos son la misma página con dos títulos: anillos animados de hoy
 * (mover, ejercicio y pasos) y tarjetas con pasos de la semana, distancia
 * estimada, calorías activas, ejercicio, la semana y, cuando toca, el
 * entrenamiento en curso, la sugerencia "¿Estás entrenando?" y el último
 * entrenamiento. Leen el servicio de actividad (watch_activity_get): no hay
 * otra instancia ni datos de muestra.
 *
 * Refresco: un temporizador de la página (500 ms) mira las generaciones del
 * servicio y solo reescribe lo que cambió; con la pantalla apagada o la
 * página tapada no hace nada.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flex_glass_math.h"
#include "ui_wb.h"
#include "ui_wb_logic.h"
#include "watch_theme.h"

#define HOME_TICK_MS 500
#define RING_D       176
#define RING_W       19
#define CARD_W       (FLEX_SCREEN_W - 2 * FLEX_LIST_X)
#define BARS         7
#define BAR_W        14
#define BAR_GAP      8
#define BAR_H        52

typedef struct {
    lv_obj_t *page;
    bool glance;
    lv_obj_t *rings;
    lv_obj_t *val[3], *unit[3];
    lv_obj_t *wk_card, *wk_icon, *wk_name, *wk_time, *wk_state;
    lv_obj_t *sg_card, *sg_title, *sg_sub;
    lv_obj_t *last_card, *last_name, *last_sub;
    lv_obj_t *steps_val, *bars[BARS], *bar_lbl[BARS];
    lv_obj_t *dist_val, *dist_sub;
    lv_obj_t *kcal_val, *kcal_sub;
    lv_obj_t *ex_val, *ex_sub;
    lv_obj_t *week_val, *week_sub;
    lv_obj_t *notice, *notice_txt;
    uint32_t gen, hgen;
    uint8_t sg_type;
    act_ses_rec_t last;
    watch_activity_history_t *hist; /* copia de la caché (lv_malloc: PSRAM) */
} home_t;

/* ---- Ayudas compartidas ---- */

void ui_wb_set_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

int ui_wb_fit(lv_obj_t *label, const char *text, int32_t w, const lv_font_t *big, const lv_font_t *small)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, big, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *f = sz.x <= w ? big : small;
    if (lv_obj_get_style_text_font(label, 0) != f) {
        lv_obj_set_style_text_font(label, f, 0);
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    if (lv_obj_get_style_width(label, 0) != w) {
        lv_obj_set_width(label, w);
    }
    ui_wb_set_text(label, text);
    if (f == big) {
        return 1;
    }
    lv_text_get_size(&sz, text, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return sz.y > lv_font_get_line_height(f) ? 2 : 1;
}

lv_obj_t *ui_wb_card(lv_obj_t *list, int32_t h, bool clickable)
{
    lv_obj_t *c = clickable ? flex_glass_pressable(list, FLEX_GLASS_CARD, 0, 0, CARD_W, h, FLEX_RADIUS_CARD)
                            : flex_glass_panel(list, FLEX_GLASS_CARD, 0, 0, CARD_W, h, FLEX_RADIUS_CARD);
    lv_obj_set_width(c, LV_PCT(100));
    return c;
}

lv_obj_t *ui_wb_row(lv_obj_t *list, act_icon_t icon, uint32_t color, const char *title, const char *sub)
{
    lv_obj_t *r = flex_glass_pressable(list, FLEX_GLASS_ROW, 0, 0, CARD_W, FLEX_ROW_H, FLEX_RADIUS_ROW);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_t *b = ui_wb_icon_badge(r, icon, 46, color);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *t = watch_theme_label(r, FLEX_TYPE_LABEL, FLEX_C_TXT, "");
    const int lines = ui_wb_fit(t, title, CARD_W - 76 - 36, FLEX_TYPE_LABEL, FLEX_TYPE_FOOTNOTE);
    if (sub) {
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 70, lines > 1 ? 5 : 12);
        lv_obj_t *s = watch_theme_label(r, FLEX_FONT_TINY, FLEX_C_TXT2, sub);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 70, lines > 1 ? -5 : -12);
    } else {
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 70, 0);
    }
    lv_obj_t *ch = flex_icon_create(r, FLEX_ICON_CHEVRON_RIGHT, 18, FLEX_C_MUTE);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -12, 0);
    return r;
}

const char *ui_wb_state_text(const act_view_t *v)
{
    switch (v->ses) {
    case ACT_SES_ACTIVE:
        return "En curso";
    case ACT_SES_PAUSED:
        return v->pause_kind == ACT_PAUSE_AUTO       ? "Pausa automática"
               : v->pause_kind == ACT_PAUSE_RESTORED ? "En pausa (reinicio)"
                                                     : "En pausa";
    case ACT_SES_COMPLETED:
        return v->end_reason == ACT_END_AUTO ? "Finalizado solo" : "Terminado";
    default:
        return "";
    }
}

void ui_wb_metrics_text(const act_type_t *t, char *b, size_t n)
{
    if (t->flags & ACT_F_DURATION) {
        snprintf(b, n, "Mide: tiempo. Estima: calorías (MET). Sin pasos ni distancia.");
    } else if (t->flags & ACT_F_DIST) {
        snprintf(b, n, "Mide: tiempo, pasos y cadencia. Estima: distancia, ritmo y calorías.");
    } else if (t->flags & ACT_F_STEPS) {
        snprintf(b, n, "Mide: tiempo, pasos y movimiento. Estima: calorías (MET).");
    } else {
        snprintf(b, n, "Mide: tiempo y movimiento de la muñeca. Estima: calorías (MET).");
    }
}

void ui_wb_back_home(void)
{
    if (!ui_nav_back_to(ui_wb_create) && !ui_nav_back_to(ui_wb_cards_create)) {
        ui_nav_back();
    }
}

bool ui_wb_gesture_strict(void)
{
    watch_activity_status_t st;
    watch_activity_get(&st);
    return st.v.ses == ACT_SES_ACTIVE || st.v.ses == ACT_SES_PAUSED;
}

/* ---- Página ---- */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void push_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static void workout_cb(lv_event_t *e)
{
    (void)e;
    ui_wb_open_workout(0, false); /* el que está en marcha */
    ui_nav_push(ui_wb_workout_create);
}

static void suggest_cb(lv_event_t *e)
{
    home_t *h = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    /* "Al aire libre" (1) o "En interior" (2) del tipo sugerido */
    const bool indoor = (uintptr_t)lv_obj_get_user_data(btn) == 2;
    uint8_t type = h->sg_type;
    if (indoor) {
        type = type == ACT_TYPE_RUN_OUT ? ACT_TYPE_RUN_IN : ACT_TYPE_WALK_IN;
    }
    ui_wb_open_workout(type, true);
    ui_nav_push(ui_wb_workout_create);
}

static void dismiss_cb(lv_event_t *e)
{
    (void)e;
    watch_activity_suggest_dismiss();
}

static void last_cb(lv_event_t *e)
{
    home_t *h = lv_event_get_user_data(e);
    ui_wb_open_session(&h->last);
    ui_nav_push(ui_wb_session_create);
}

static lv_obj_t *card_title(lv_obj_t *card, const char *title, bool chevron)
{
    lv_obj_t *t = watch_theme_label(card, FLEX_TYPE_LABEL, FLEX_C_TXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 18, 14);
    if (chevron) {
        lv_obj_t *ch = flex_icon_create(card, FLEX_ICON_CHEVRON_RIGHT, 18, WB_C_EXER);
        lv_obj_align(ch, LV_ALIGN_TOP_RIGHT, -16, 16);
    }
    return t;
}

/* Valor grande con su unidad pequeña al lado (una fila flexible: la unidad
 * sigue al número aunque cambie de ancho). */
static void value_unit(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, lv_obj_t **val, lv_obj_t **unit,
                       const char *u)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    *val = watch_theme_label(row, f, rgb, "--");
    *unit = watch_theme_label(row, FLEX_FONT_TINY, rgb, u);
    lv_obj_set_style_pad_bottom(*unit, 4, 0);
}

static lv_obj_t *simple_card(lv_obj_t *list, const char *title, bool click, uint32_t rgb, lv_obj_t **val,
                             lv_obj_t **sub)
{
    lv_obj_t *c = ui_wb_card(list, 116, click);
    card_title(c, title, click);
    *val = watch_theme_label(c, FLEX_TYPE_TITLE, rgb, "--");
    lv_obj_align(*val, LV_ALIGN_TOP_LEFT, 18, 44);
    *sub = watch_theme_label(c, FLEX_FONT_TINY, FLEX_C_TXT2, "");
    lv_obj_align(*sub, LV_ALIGN_BOTTOM_LEFT, 18, -12);
    return c;
}

static void build(home_t *h, lv_obj_t *page)
{
    lv_obj_t *list = watch_theme_list(page);

    /* Anillos y cifras */
    lv_obj_t *rc = ui_wb_card(list, RING_D + 28, false);
    h->rings = ui_wb_rings_create(rc, RING_D, RING_W);
    lv_obj_align(h->rings, LV_ALIGN_LEFT_MID, 8, 0);
    static const char *const kName[3] = {"Moverse", "Ejercicio", "Pasos"};
    static const char *const kUnit[3] = {"KCAL", "MIN", ""};
    static const uint32_t kCol[3] = {WB_C_MOVE, WB_C_EXER, WB_C_STEPS};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *n = watch_theme_label(rc, FLEX_TYPE_FOOTNOTE, FLEX_C_TXT, kName[i]);
        lv_obj_set_pos(n, RING_D + 22, 14 + i * 64);
        lv_obj_t *box = lv_obj_create(rc);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, CARD_W - RING_D - 26, 34);
        lv_obj_set_pos(box, RING_D + 22, 34 + i * 64);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        value_unit(box, FLEX_TYPE_BODY, kCol[i], &h->val[i], &h->unit[i], kUnit[i]);
    }

    /* Avisos del sensor y del almacenamiento (ocultos si todo va bien) */
    h->notice = ui_wb_card(list, 76, false);
    h->notice_txt = watch_theme_label(h->notice, FLEX_FONT_TINY, FLEX_C_WARN, "");
    lv_label_set_long_mode(h->notice_txt, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(h->notice_txt, CARD_W - 32);
    lv_obj_align(h->notice_txt, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_add_flag(h->notice, LV_OBJ_FLAG_HIDDEN);

    /* Entrenamiento en curso */
    h->wk_card = ui_wb_card(list, 92, true);
    lv_obj_set_style_border_color(h->wk_card, flex_color(WB_C_GO), 0);
    lv_obj_set_style_border_width(h->wk_card, 2, 0);
    lv_obj_set_style_border_opa(h->wk_card, LV_OPA_COVER, 0);
    h->wk_icon = ui_wb_icon_badge(h->wk_card, ACT_IC_RUN, 52, WB_C_GO);
    lv_obj_align(h->wk_icon, LV_ALIGN_LEFT_MID, 14, 0);
    h->wk_name = watch_theme_label(h->wk_card, FLEX_TYPE_LABEL, FLEX_C_TXT, "");
    lv_obj_align(h->wk_name, LV_ALIGN_TOP_LEFT, 80, 10);
    h->wk_state = watch_theme_label(h->wk_card, FLEX_FONT_TINY, WB_C_GO, "");
    lv_obj_align(h->wk_state, LV_ALIGN_BOTTOM_LEFT, 80, -10);
    h->wk_time = watch_theme_label(h->wk_card, FLEX_TYPE_HEADLINE, WB_C_TIMER, "");
    lv_obj_align(h->wk_time, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_event_cb(h->wk_card, workout_cb, LV_EVENT_CLICKED, h);
    lv_obj_add_flag(h->wk_card, LV_OBJ_FLAG_HIDDEN);

    /* ¿Estás entrenando? */
    h->sg_card = ui_wb_card(list, 150, false);
    h->sg_title = watch_theme_label(h->sg_card, FLEX_TYPE_LABEL, WB_C_GO, "¿Estás entrenando?");
    lv_obj_align(h->sg_title, LV_ALIGN_TOP_LEFT, 18, 12);
    h->sg_sub = watch_theme_label(h->sg_card, FLEX_FONT_TINY, FLEX_C_TXT2, "");
    lv_obj_align(h->sg_sub, LV_ALIGN_TOP_LEFT, 18, 40);
    static const char *const kWhere[2] = {"Al aire libre", "En interior"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = flex_glass_button(h->sg_card, kWhere[i], i == 0 ? WB_C_GO : FLEX_GLASS_TINT_NONE, 160, 48);
        if (i == 0) {
            lv_obj_set_style_text_color(b, flex_color(FLEX_C_BLACK), 0);
        }
        lv_obj_set_user_data(b, (void *)(uintptr_t)(i + 1));
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 14 + i * 172, 64);
        lv_obj_add_event_cb(b, suggest_cb, LV_EVENT_CLICKED, h);
    }
    lv_obj_t *no = watch_theme_label(h->sg_card, FLEX_FONT_SMALL, FLEX_C_TXT2, "Ahora no");
    lv_obj_add_flag(no, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(no, 14);
    lv_obj_align(no, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(no, dismiss_cb, LV_EVENT_CLICKED, h);
    lv_obj_add_flag(h->sg_card, LV_OBJ_FLAG_HIDDEN);

    /* Último entrenamiento */
    h->last_card = ui_wb_card(list, 88, true);
    lv_obj_t *lt = watch_theme_label(h->last_card, FLEX_FONT_TINY, FLEX_C_TXT2, "Último entrenamiento");
    lv_obj_align(lt, LV_ALIGN_TOP_LEFT, 18, 12);
    h->last_name = watch_theme_label(h->last_card, FLEX_TYPE_LABEL, FLEX_C_TXT, "");
    lv_obj_align(h->last_name, LV_ALIGN_TOP_LEFT, 18, 32);
    h->last_sub = watch_theme_label(h->last_card, FLEX_FONT_TINY, WB_C_EXER, "");
    lv_obj_align(h->last_sub, LV_ALIGN_BOTTOM_LEFT, 18, -10);
    lv_obj_t *lch = flex_icon_create(h->last_card, FLEX_ICON_CHEVRON_RIGHT, 18, FLEX_C_MUTE);
    lv_obj_align(lch, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_event_cb(h->last_card, last_cb, LV_EVENT_CLICKED, h);
    lv_obj_add_flag(h->last_card, LV_OBJ_FLAG_HIDDEN);

    /* Pasos y la semana en barras */
    lv_obj_t *sc = ui_wb_card(list, 156, true);
    card_title(sc, "Pasos", true);
    lv_obj_t *hoy = watch_theme_label(sc, FLEX_FONT_TINY, FLEX_C_TXT2, "Hoy");
    lv_obj_align(hoy, LV_ALIGN_TOP_LEFT, 18, 46);
    h->steps_val = watch_theme_label(sc, FLEX_TYPE_NUMBER, WB_C_STEPS_TXT, "--");
    lv_obj_align(h->steps_val, LV_ALIGN_TOP_LEFT, 18, 66);
    const int32_t bx = CARD_W - 18 - (BARS * BAR_W + (BARS - 1) * BAR_GAP);
    for (int i = 0; i < BARS; i++) {
        lv_obj_t *b = lv_obj_create(sc);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_bg_color(b, flex_color(i == BARS - 1 ? WB_C_STEPS : 0x5B5F8F), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_size(b, BAR_W, 2);
        lv_obj_set_pos(b, bx + i * (BAR_W + BAR_GAP), 126 - 2);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        h->bars[i] = b;
        h->bar_lbl[i] = watch_theme_label(sc, FLEX_FONT_TINY, FLEX_C_MUTE, "");
        lv_obj_set_width(h->bar_lbl[i], BAR_W + 8);
        lv_obj_set_style_text_align(h->bar_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(h->bar_lbl[i], bx + i * (BAR_W + BAR_GAP) - 4, 130);
    }
    lv_obj_add_event_cb(sc, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_history_create);

    lv_obj_t *c;
    c = simple_card(list, "Distancia", false, WB_C_DIST, &h->dist_val, &h->dist_sub);
    c = simple_card(list, "Calorías activas", false, WB_C_MOVE, &h->kcal_val, &h->kcal_sub);
    c = simple_card(list, "Ejercicio", false, WB_C_EXER, &h->ex_val, &h->ex_sub);
    c = simple_card(list, "Esta semana", true, WB_C_STEPS_TXT, &h->week_val, &h->week_sub);
    lv_obj_add_event_cb(c, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_history_create);

    /* Acciones */
    lv_obj_t *go = flex_glass_button(list, "Entrenar", WB_C_GO, CARD_W, 64);
    lv_obj_set_width(go, LV_PCT(100));
    lv_obj_set_style_text_color(go, flex_color(FLEX_C_BLACK), 0);
    lv_obj_add_event_cb(go, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_workouts_create);
    lv_obj_t *r = ui_wb_row(list, ACT_IC_STAR, WB_C_STEPS_TXT, "Historial y logros", NULL);
    lv_obj_add_event_cb(r, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_history_create);
    r = watch_theme_list_row(list, FLEX_ICON_GEAR, FLEX_C_CAT_BLUE, "Ajustes de Bienestar", NULL, true);
    lv_obj_add_event_cb(r, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_settings_create);
}

/* Semana, racha y último entrenamiento: la caché se copia solo cuando cambia;
 * la barra de hoy sigue a los pasos en cada refresco. */
static void refresh_hist(home_t *h, const watch_activity_status_t *st)
{
    const uint32_t hg = watch_activity_history_gen();
    if (hg != h->hgen) {
        if (!watch_activity_history(h->hist)) {
            return;
        }
        h->hgen = hg;
    }
    if (!h->hgen) {
        return;
    }
    const watch_activity_history_t *hs = h->hist;
    uint32_t v[BARS], mx = 1;
    act_config_t cfg;
    watch_activity_config(&cfg);
    for (int i = 0; i < BARS; i++) {
        const act_day_t *d = &hs->days[WB_HIST_DAYS - (BARS - 1) + i];
        v[i] = i == BARS - 1 ? st->v.steps : (d->day == ACT_NO_DAY ? 0 : d->steps);
        mx = v[i] > mx ? v[i] : mx;
    }
    mx = cfg.goal_steps > mx ? cfg.goal_steps : mx;
    uint32_t week = 0;
    for (int i = 0; i < BARS; i++) {
        week += v[i];
        const int32_t bh = wb_bar_h(v[i], mx, BAR_H);
        const int32_t hh = bh < 2 ? 2 : bh;
        if (lv_obj_get_height(h->bars[i]) != hh) {
            lv_obj_set_height(h->bars[i], hh);
            lv_obj_set_y(h->bars[i], 126 - hh);
        }
        if (st->v.day != ACT_NO_DAY) {
            ui_wb_set_text(h->bar_lbl[i], wb_day_letter(st->v.day - (BARS - 1) + i));
        }
    }
    char b[64], n[24];
    wb_fmt_int(n, sizeof(n), week);
    snprintf(b, sizeof(b), "%s pasos", n);
    ui_wb_set_text(h->week_val, b);
    if (hs->stats.best_steps) {
        char best[16];
        wb_fmt_int(best, sizeof(best), hs->stats.best_steps);
        snprintf(b, sizeof(b), "Racha: %u días · Récord: %s", (unsigned)hs->stats.streak, best);
    } else {
        snprintf(b, sizeof(b), "Racha: %u días", (unsigned)hs->stats.streak);
    }
    ui_wb_set_text(h->week_sub, b);
    /* Último entrenamiento (de hoy) */
    const bool show_last = hs->last_valid && hs->last.day == st->v.day && st->v.ses != ACT_SES_ACTIVE &&
                           st->v.ses != ACT_SES_PAUSED;
    watch_theme_set_visible(h->last_card, show_last);
    if (show_last) {
        h->last = hs->last;
        ui_wb_fit(h->last_name, act_catalog_get(hs->last.type)->name, CARD_W - 60, FLEX_TYPE_LABEL, FLEX_TYPE_FOOTNOTE);
        char d[20], k[20];
        wb_fmt_dur_words(d, sizeof(d), hs->last.active_s);
        if (hs->last.kcal_valid) {
            snprintf(k, sizeof(k), " · %u kcal", hs->last.kcal);
        } else {
            k[0] = 0;
        }
        char line[80];
        snprintf(line, sizeof(line), "%s%s%s", d, k, hs->last.end_reason == ACT_END_AUTO ? " · fin automático" : "");
        snprintf(b, sizeof(b), "%.63s", line);
        ui_wb_set_text(h->last_sub, b);
    }
}

static void refresh(home_t *h)
{
    watch_activity_status_t st;
    watch_activity_get(&st);
    const act_view_t *v = &st.v;
    char b[64], n[24];
    /* anillos */
    act_config_t cfg;
    watch_activity_config(&cfg);
    if (v->kcal_valid) {
        snprintf(b, sizeof(b), "%lu/%u", (unsigned long)v->kcal, cfg.goal_kcal);
    } else {
        snprintf(b, sizeof(b), "--/%u", cfg.goal_kcal);
    }
    ui_wb_set_text(h->val[0], b);
    snprintf(b, sizeof(b), "%lu/%u", (unsigned long)(v->ex_s / 60), cfg.goal_ex_min);
    ui_wb_set_text(h->val[1], b);
    wb_fmt_int(n, sizeof(n), v->steps);
    char g[16];
    wb_fmt_int(g, sizeof(g), cfg.goal_steps);
    snprintf(b, sizeof(b), "%s/%s", n, g);
    ui_wb_set_text(h->val[2], b);
    ui_wb_rings_set(h->rings, &v->rings, true);

    /* tarjetas */
    ui_wb_set_text(h->steps_val, n);
    wb_fmt_dist(b, sizeof(b), v->dist_m);
    ui_wb_set_text(h->dist_val, b);
    ui_wb_set_text(h->dist_sub, v->dist_generic ? "Estimada con zancada genérica: añade tu altura"
                                                : "Estimada: pasos x zancada (sin GPS)");
    if (v->kcal_valid) {
        snprintf(b, sizeof(b), "%lu kcal", (unsigned long)v->kcal);
        ui_wb_set_text(h->kcal_val, b);
        ui_wb_set_text(h->kcal_sub, "Estimadas sin frecuencia cardíaca");
    } else {
        ui_wb_set_text(h->kcal_val, "--");
        ui_wb_set_text(h->kcal_sub, "Añade tu peso en Ajustes de Bienestar");
    }
    wb_fmt_dur_words(b, sizeof(b), v->ex_s);
    ui_wb_set_text(h->ex_val, v->ex_s < 60 ? "0 min" : b);
    snprintf(b, sizeof(b), "Horas en movimiento: %u · Entrenamientos: %u", v->move_hours, v->workouts);
    ui_wb_set_text(h->ex_sub, b);

    /* entrenamiento en curso */
    const bool run = v->ses == ACT_SES_ACTIVE || v->ses == ACT_SES_PAUSED;
    watch_theme_set_visible(h->wk_card, run);
    if (run) {
        const act_type_t *t = act_catalog_get(v->type);
        ui_wb_fit(h->wk_name, t->name, CARD_W - 90 - 110, FLEX_TYPE_LABEL, FLEX_TYPE_FOOTNOTE);
        ui_wb_set_text(h->wk_state, ui_wb_state_text(v));
        wb_fmt_dur(b, sizeof(b), v->active_ms);
        ui_wb_set_text(h->wk_time, b);
        lv_obj_t *ic = lv_obj_get_child(h->wk_icon, 0);
        ui_wb_icon_set(ic, (act_icon_t)t->icon, 0x0B0D10);
    }
    /* sugerencia */
    watch_theme_set_visible(h->sg_card, v->suggest && !run);
    if (v->suggest && !run) {
        h->sg_type = v->suggest_type;
        ui_wb_set_text(h->sg_sub, v->suggest_type == ACT_TYPE_RUN_OUT ? "Carrera detectada: el tiempo ya cuenta"
                                                                      : "Caminata detectada: el tiempo ya cuenta");
    }
    /* avisos */
    const char *why = NULL;
    if (!st.imu_present) {
        why = "Sensor de movimiento no disponible: no se cuentan pasos.";
    } else if (!cfg.tracking && !run) {
        why = "El conteo de pasos está desactivado en Ajustes de Bienestar.";
    } else if (st.started && !st.imu_ok && st.imu_present && (cfg.tracking || run)) {
        why = "El sensor no envía datos ahora mismo.";
    } else if (!st.storage_ok) {
        why = "Sin almacenamiento: el historial no se guarda (Almacenamiento).";
    }
    watch_theme_set_visible(h->notice, why != NULL);
    if (why) {
        ui_wb_set_text(h->notice_txt, why);
    }
    refresh_hist(h, &st);
}

static void tick_cb(lv_timer_t *t)
{
    home_t *h = lv_timer_get_user_data(t);
    if (ui_nav_top() != h->page || !ui_screen_is_on()) {
        return; /* tapada o con la pantalla apagada: nada que dibujar */
    }
    const uint32_t g = watch_activity_generation();
    if (g != h->gen) {
        h->gen = g;
        refresh(h);
    }
}

static void delete_cb(lv_event_t *e)
{
    home_t *h = lv_event_get_user_data(e);
    lv_free(h->hist);
    lv_free(h);
}

static lv_obj_t *create(lv_obj_t *parent, bool glance)
{
    home_t *h = lv_malloc_zeroed(sizeof(*h));
    if (!h) {
        return NULL;
    }
    h->hist = lv_malloc_zeroed(sizeof(*h->hist));
    if (!h->hist) {
        lv_free(h);
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    h->page = page;
    h->glance = glance;
    watch_theme_header(page, glance ? "Hoy" : "Bienestar", back_cb, NULL);
    build(h, page);
    h->gen = watch_activity_generation();
    refresh(h);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, h);
    ui_page_timer(page, tick_cb, HOME_TICK_MS, h);
    return page;
}

lv_obj_t *ui_wb_create(lv_obj_t *parent)
{
    return create(parent, false);
}

lv_obj_t *ui_wb_cards_create(lv_obj_t *parent)
{
    return create(parent, true);
}

/* ---- Avisos (cada 100 ms desde ui_system) ---- */

void ui_wb_watch_tick(bool screen_on)
{
    static uint32_t s_gen, s_seq;
    static bool s_init;
    const uint32_t g = watch_activity_generation();
    if (g == s_gen) {
        return;
    }
    s_gen = g;
    watch_activity_status_t st;
    watch_activity_get(&st);
    if (!s_init) {
        s_init = true;
        s_seq = st.ui_seq; /* lo de antes de arrancar la interfaz no se anuncia */
        return;
    }
    if (st.ui_seq == s_seq) {
        return;
    }
    s_seq = st.ui_seq;
    if (!screen_on) {
        return; /* no se enciende la pantalla por esto (sin motor de vibración) */
    }
    const char *name = act_catalog_get(st.v.type)->name;
    if (st.ui_ev & WB_UI_AUTO_END) {
        ui_banner_show_info("Entrenamiento guardado", "Terminado tras la pausa");
    } else if (st.ui_ev & WB_UI_AUTO_PAUSE) {
        ui_banner_show_info("Pausa automática", name);
    } else if (st.ui_ev & WB_UI_AUTO_RESUME) {
        ui_banner_show_info("Entrenamiento reanudado", name);
    } else if (st.ui_ev & WB_UI_SUGGEST) {
        ui_banner_show_info("¿Estás entrenando?", "Ábrelo en Bienestar");
    }
}
