/*
 * ui_wb_history — historial de Bienestar: últimos 7 días en barras, racha,
 * récords, logros, entrenamientos y días. Todo sale de la caché del servicio
 * (watch_activity_history, calculada de los registros guardados); los
 * entrenamientos antiguos se piden por páginas y los lee la tarea del
 * servicio: la tarea de LVGL nunca toca la flash.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_wb.h"
#include "ui_wb_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

#define CARD_W   (FLEX_SCREEN_W - 2 * FLEX_LIST_X)
#define CH_BARS  7
#define CH_W     30
#define CH_GAP   18
#define CH_H     96
#define SES_TICK_MS 200

static act_ses_rec_t s_ses; /* el de ui_wb_open_session */

void ui_wb_open_session(const act_ses_rec_t *r)
{
    s_ses = *r;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void ses_cb(lv_event_t *e)
{
    const act_ses_rec_t *r = lv_event_get_user_data(e);
    ui_wb_open_session(r);
    ui_nav_push(ui_wb_session_create);
}

/* Fila de un entrenamiento guardado; r tiene que vivir lo que la fila. */
static lv_obj_t *ses_row(lv_obj_t *list, const act_ses_rec_t *r, int32_t today)
{
    const act_type_t *t = act_catalog_get(r->type);
    char sub[64], d[24], when[24];
    wb_fmt_dur_words(d, sizeof(d), r->active_s);
    if (r->day != ACT_NO_DAY) {
        wb_fmt_day(when, sizeof(when), r->day, today);
    } else {
        snprintf(when, sizeof(when), "Sin fecha");
    }
    snprintf(sub, sizeof(sub), "%s · %s", when, d);
    lv_obj_t *row = ui_wb_row(list, (act_icon_t)t->icon, ui_wb_cat_color((act_cat_t)t->cat), t->name, sub);
    lv_obj_add_event_cb(row, ses_cb, LV_EVENT_CLICKED, (void *)r);
    return row;
}

typedef struct {
    watch_activity_history_t h;
    act_config_t cfg;
} hist_page_t;

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void push_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static void week_chart(lv_obj_t *list, const hist_page_t *p, uint32_t today_steps)
{
    lv_obj_t *c = ui_wb_card(list, CH_H + 108, false);
    lv_obj_t *t = watch_theme_label(c, FLEX_FONT_S20, FLEX_C_TXT, "Últimos 7 días");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 18, 14);
    uint32_t v[CH_BARS], mx = p->cfg.goal_steps, sum = 0;
    int days = 0;
    for (int i = 0; i < CH_BARS; i++) {
        const act_day_t *d = &p->h.days[WB_HIST_DAYS - (CH_BARS - 1) + i];
        v[i] = i == CH_BARS - 1 ? today_steps : (d->day == ACT_NO_DAY ? 0 : d->steps);
        mx = v[i] > mx ? v[i] : mx;
        sum += v[i];
        days += (i == CH_BARS - 1 || d->day != ACT_NO_DAY);
    }
    const int32_t x0 = (CARD_W - (CH_BARS * CH_W + (CH_BARS - 1) * CH_GAP)) / 2, base = 48 + CH_H;
    /* línea del objetivo */
    const int32_t gy = base - wb_bar_h(p->cfg.goal_steps, mx, CH_H);
    lv_obj_t *gl = lv_obj_create(c);
    lv_obj_remove_style_all(gl);
    lv_obj_set_size(gl, CARD_W - 2 * x0 + 12, 2);
    lv_obj_set_pos(gl, x0 - 6, gy);
    lv_obj_set_style_bg_color(gl, flex_color(WB_C_STEPS), 0);
    lv_obj_set_style_bg_opa(gl, LV_OPA_40, 0);
    for (int i = 0; i < CH_BARS; i++) {
        const int32_t bh = wb_bar_h(v[i], mx, CH_H);
        lv_obj_t *b = lv_obj_create(c);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, CH_W, bh < 3 ? 3 : bh);
        lv_obj_set_pos(b, x0 + i * (CH_W + CH_GAP), base - (bh < 3 ? 3 : bh));
        lv_obj_set_style_radius(b, 6, 0);
        const bool met = v[i] >= p->cfg.goal_steps;
        lv_obj_set_style_bg_color(b, flex_color(met ? WB_C_STEPS : 0x4A4F86), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if (p->h.today != ACT_NO_DAY) {
            lv_obj_t *l = watch_theme_label(c, FLEX_FONT_TINY, i == CH_BARS - 1 ? FLEX_C_TXT : FLEX_C_MUTE,
                                            wb_day_letter(p->h.today - (CH_BARS - 1) + i));
            lv_obj_set_width(l, CH_W);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(l, x0 + i * (CH_W + CH_GAP), base + 6);
        }
    }
    char a[16], m[16], b[64];
    wb_fmt_int(a, sizeof(a), sum);
    wb_fmt_int(m, sizeof(m), days ? sum / (uint32_t)days : 0);
    snprintf(b, sizeof(b), "%s pasos · media %s al día", a, m);
    lv_obj_t *tl = watch_theme_label(c, FLEX_FONT_TINY, FLEX_C_TXT2, b);
    lv_obj_align(tl, LV_ALIGN_BOTTOM_LEFT, 18, -12);
}

lv_obj_t *ui_wb_history_create(lv_obj_t *parent)
{
    hist_page_t *p = lv_malloc_zeroed(sizeof(*p));
    if (!p) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Historial", back_cb, NULL);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    lv_obj_t *list = watch_theme_list(page);
    watch_activity_status_t st;
    watch_activity_get(&st);
    watch_activity_config(&p->cfg);
    if (!watch_activity_history(&p->h)) {
        lv_obj_t *l = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "Cargando el historial...");
        (void)l;
        return page;
    }
    week_chart(list, p, st.v.steps);
    if (!p->h.storage) {
        lv_obj_t *n = watch_theme_label(list, FLEX_FONT_TINY, FLEX_C_WARN,
                                        "Sin almacenamiento: solo se ve lo de este arranque.");
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(n, LV_PCT(100));
    }
    char b[48], n[16];
    const act_stats_t *s = &p->h.stats;
    watch_theme_section(list, "Rachas y récords");
    snprintf(b, sizeof(b), "%u días", (unsigned)s->streak);
    watch_theme_info_row(list, "Racha actual", b);
    snprintf(b, sizeof(b), "%u días", (unsigned)s->best_streak);
    watch_theme_info_row(list, "Mejor racha", b);
    if (s->best_steps) {
        wb_fmt_int(n, sizeof(n), s->best_steps);
        watch_theme_info_row(list, "Récord de pasos", n);
    }
    snprintf(b, sizeof(b), "%lu", (unsigned long)s->all_rings_days);
    watch_theme_info_row(list, "Días con los tres anillos", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)s->sessions);
    watch_theme_info_row(list, "Entrenamientos", b);
    wb_fmt_dur_words(b, sizeof(b), s->workout_s);
    watch_theme_info_row(list, "Tiempo entrenando", b);
    wb_fmt_dist(b, sizeof(b), s->total_dist_m);
    watch_theme_info_row(list, "Distancia total (est.)", b);

    watch_theme_section(list, "Logros");
    for (int i = 0; i < ACT_ACH_COUNT; i++) {
        const bool got = (s->ach >> i) & 1u;
        lv_obj_t *r = ui_wb_card(list, 64, false);
        lv_obj_t *ic = ui_wb_icon_create(r, ACT_IC_STAR, 30, got ? 0xFFC233 : 0x3A3E48);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_t *t = watch_theme_label(r, FLEX_FONT_S16, got ? FLEX_C_TXT : FLEX_C_MUTE, act_ach_name(i));
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 60, 10);
        lv_obj_t *d = watch_theme_label(r, FLEX_FONT_TINY, FLEX_C_TXT2, act_ach_desc(i));
        lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 60, -10);
    }

    watch_theme_section(list, "Entrenamientos");
    if (!p->h.nrecent) {
        watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "Aún no hay entrenamientos");
    }
    for (int i = 0; i < p->h.nrecent; i++) {
        ses_row(list, &p->h.recent[i], p->h.today);
    }
    if (p->h.storage && p->h.nrecent >= WB_HIST_RECENT) {
        lv_obj_t *r = watch_theme_list_row(list, FLEX_ICON_GRID, FLEX_C_CAT_BLUE, "Ver todos", NULL, true);
        lv_obj_add_event_cb(r, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_sessions_create);
    }

    watch_theme_section(list, "Días");
    for (int i = WB_HIST_DAYS - 1; i >= WB_HIST_DAYS - 14; i--) {
        const act_day_t *d = &p->h.days[i];
        if (d->day == ACT_NO_DAY) {
            continue;
        }
        char k[24], when[24], val[48];
        uint32_t kcal;
        wb_fmt_day(when, sizeof(when), d->day, p->h.today);
        wb_fmt_int(n, sizeof(n), d->steps);
        if (act_day_kcal(&p->cfg, d, &kcal)) {
            snprintf(k, sizeof(k), " · %lu kcal", (unsigned long)kcal);
        } else {
            k[0] = 0;
        }
        snprintf(val, sizeof(val), "%s pasos%s", n, k);
        watch_theme_info_row(list, when, val);
    }
    return page;
}

/* ---- Todos los entrenamientos, por páginas ---- */

typedef struct {
    lv_obj_t *list;
    act_ses_rec_t recs[WB_PAGE];
    int n;
    uint32_t skip, want_gen;
    bool waiting;
    bool request;  /* pedir la página en el próximo tick (el botón no se borra en su evento) */
    int32_t today;
} sessions_t;

static void ses_build(sessions_t *p);

static void more_cb(lv_event_t *e)
{
    sessions_t *p = lv_event_get_user_data(e);
    const bool older = (uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e)) == 1;
    if (older) {
        p->skip += WB_PAGE;
    } else {
        p->skip = p->skip >= WB_PAGE ? p->skip - WB_PAGE : 0;
    }
    p->request = true;
}

static void ses_build(sessions_t *p)
{
    lv_obj_clean(p->list);
    if (!p->n) {
        watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2, "No hay más entrenamientos");
    }
    for (int i = 0; i < p->n; i++) {
        ses_row(p->list, &p->recs[i], p->today);
    }
    if (p->skip) {
        lv_obj_t *b = watch_theme_button(p->list, "Más recientes", FLEX_C_SURF2, CARD_W, 52);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_user_data(b, (void *)(uintptr_t)0);
        lv_obj_add_event_cb(b, more_cb, LV_EVENT_CLICKED, p);
    }
    if (p->n == WB_PAGE) {
        lv_obj_t *b = watch_theme_button(p->list, "Más antiguos", FLEX_C_SURF2, CARD_W, 52);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_user_data(b, (void *)(uintptr_t)1);
        lv_obj_add_event_cb(b, more_cb, LV_EVENT_CLICKED, p);
    }
}

static void ses_tick_cb(lv_timer_t *t)
{
    sessions_t *p = lv_timer_get_user_data(t);
    if (p->request) {
        p->request = false;
        uint32_t gen;
        watch_activity_sessions_page(NULL, p->recs, 0, &gen);
        p->want_gen = gen + 1;
        p->waiting = true;
        lv_obj_clean(p->list);
        watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2, "Cargando...");
        watch_activity_sessions_request(p->skip);
        return;
    }
    if (!p->waiting) {
        return;
    }
    uint32_t skip, gen;
    const int n = watch_activity_sessions_page(&skip, p->recs, WB_PAGE, &gen);
    if ((int32_t)(gen - p->want_gen) >= 0 && skip == p->skip) {
        p->waiting = false;
        p->n = n;
        ses_build(p);
    }
}

lv_obj_t *ui_wb_sessions_create(lv_obj_t *parent)
{
    sessions_t *p = lv_malloc_zeroed(sizeof(*p));
    if (!p) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Entrenamientos", back_cb, NULL);
    p->list = watch_theme_list(page);
    watch_activity_status_t st;
    watch_activity_get(&st);
    p->today = st.v.day;
    uint32_t gen;
    watch_activity_sessions_page(NULL, p->recs, 0, &gen);
    p->want_gen = gen + 1;
    p->waiting = true;
    watch_activity_sessions_request(0);
    watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2, "Cargando...");
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, ses_tick_cb, SES_TICK_MS, p);
    return page;
}

/* ---- Detalle de un entrenamiento ---- */

lv_obj_t *ui_wb_session_create(lv_obj_t *parent)
{
    const act_ses_rec_t *r = &s_ses;
    const act_type_t *t = act_catalog_get(r->type);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, t->name, back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    char b[48];
    watch_activity_status_t st;
    watch_activity_get(&st);
    if (r->day != ACT_NO_DAY) {
        wb_fmt_day(b, sizeof(b), r->day, st.v.day);
        watch_theme_info_row(list, "Día", b);
    }
    if (r->start_utc > 0) {
        wt_datetime_t dt;
        wt_utc_to_local(r->start_utc, watch_time_get_tz(), &dt);
        wt_fmt_hhmm(b, sizeof(b), &dt);
        watch_theme_info_row(list, "Inicio", b);
    }
    wb_fmt_dur(b, sizeof(b), r->total_s * 1000u);
    watch_theme_info_row(list, "Tiempo total", b);
    wb_fmt_dur(b, sizeof(b), r->active_s * 1000u);
    watch_theme_info_row(list, "Activo", b);
    wb_fmt_dur(b, sizeof(b), r->paused_s * 1000u);
    watch_theme_info_row(list, "En pausa", b);
    act_config_t cfg;
    watch_activity_config(&cfg);
    uint32_t kcal = r->kcal;
    bool kv = r->kcal_valid;
    if (!kv) {
        kv = act_kcal(&cfg, r->ee, &kcal); /* se guardó sin peso: con el de ahora */
    }
    if (kv) {
        snprintf(b, sizeof(b), "%lu kcal", (unsigned long)kcal);
        watch_theme_info_row(list, "Calorías activas (est.)", b);
    } else {
        watch_theme_info_row(list, "Calorías activas", "Sin peso");
    }
    if (t->flags & ACT_F_STEPS) {
        wb_fmt_int(b, sizeof(b), r->steps);
        watch_theme_info_row(list, "Pasos", b);
        if (r->avg_cadence) {
            snprintf(b, sizeof(b), "%u por minuto", r->avg_cadence);
            watch_theme_info_row(list, "Cadencia media", b);
        }
    }
    if (t->flags & ACT_F_DIST) {
        wb_fmt_dist(b, sizeof(b), r->dist_m);
        watch_theme_info_row(list, "Distancia (estimada)", b);
        wb_fmt_pace(b, sizeof(b), r->dist_m >= 50 ? r->active_s * 1000u / r->dist_m : 0);
        watch_theme_info_row(list, "Ritmo medio", b);
    }
    snprintf(b, sizeof(b), "%u (%u automáticas)", r->pauses, r->auto_pauses);
    watch_theme_info_row(list, "Pausas", b);
    watch_theme_info_row(list, "Fin", r->end_reason == ACT_END_AUTO ? "Automático tras la pausa" : "Manual");
    return page;
}
