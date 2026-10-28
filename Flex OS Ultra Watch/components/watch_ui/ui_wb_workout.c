/*
 * ui_wb_workout — entrenamientos de Bienestar: selector (buscar, recientes,
 * favoritos y categorías), lista de una categoría, búsqueda con el teclado
 * del sistema y la pantalla del entrenamiento.
 *
 * La pantalla del entrenamiento tiene tres vistas en la misma página (el
 * contenido se rehace al cambiar de una a otra, no en cada refresco):
 *  - cuenta atrás 3-2-1 con lo que se va a medir (un toque empieza ya);
 *  - en curso: cronómetro del tiempo ACTIVO (décimas), total y pausa, solo
 *    las métricas que el tipo puede medir, pausa/reanudar y terminar (con un
 *    segundo toque de confirmación: un roce no termina nada);
 *  - resumen al terminar (a mano o solo), con lo guardado.
 * El estado lo lleva el servicio: salir de la página (atrás, a la esfera o
 * con la pantalla apagada) no para nada. Ningún entrenamiento se cuenta dos
 * veces: si ya hay uno en marcha, la página enseña ese.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_wb.h"
#include "ui_wb_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

#define CARD_W        (FLEX_SCREEN_W - 2 * FLEX_LIST_X)
#define COUNTDOWN_MS  3000
#define WK_TICK_MS    100
#define CONFIRM_MS    3000
#define SEARCH_MAX    8
#define STAR_ON       0xFFC233
#define STAR_OFF      0x4A4E58

static struct {
    act_cat_t cat;
    uint8_t type;
    bool from_suggestion;
} s_open;

void ui_wb_open_category(act_cat_t c)
{
    s_open.cat = c;
}

void ui_wb_open_workout(uint8_t type, bool from_suggestion)
{
    s_open.type = type;
    s_open.from_suggestion = from_suggestion;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static const char *short_caps(const act_type_t *t)
{
    if (t->flags & ACT_F_WATER) {
        return "Solo tiempo · aviso de agua";
    }
    if (t->flags & ACT_F_DURATION) {
        return "Solo tiempo";
    }
    if (t->flags & ACT_F_DIST) {
        return "Pasos y distancia estimada";
    }
    if (t->flags & ACT_F_STEPS) {
        return "Pasos y movimiento";
    }
    return "Tiempo y movimiento";
}

/* ---- Fila de un tipo: abrir y favorito ---- */

static void type_cb(lv_event_t *e)
{
    const uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    ui_wb_open_workout(id, false);
    ui_nav_push(ui_wb_workout_create);
}

static void star_cb(lv_event_t *e)
{
    lv_obj_t *star = lv_event_get_current_target(e);
    const uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    act_config_t c;
    watch_activity_config(&c);
    if (!act_config_toggle_fav(&c, id)) {
        ui_banner_show_info("Favoritos llenos", "Quita uno para añadir otro");
        return;
    }
    watch_activity_set_config(&c);
    ui_wb_icon_set(lv_obj_get_child(star, 0), ACT_IC_STAR, act_config_is_fav(&c, id) ? STAR_ON : STAR_OFF);
}

static lv_obj_t *type_row(lv_obj_t *list, const act_type_t *t, const act_config_t *c)
{
    lv_obj_t *r = flex_glass_pressable(list, FLEX_GLASS_ROW, 0, 0, CARD_W, FLEX_ROW_H, FLEX_RADIUS_ROW);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_t *b = ui_wb_icon_badge(r, (act_icon_t)t->icon, 46, ui_wb_cat_color((act_cat_t)t->cat));
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *n = watch_theme_label(r, FLEX_TYPE_LABEL, FLEX_C_TXT, "");
    const int lines = ui_wb_fit(n, t->name, CARD_W - 70 - 64, FLEX_TYPE_LABEL, FLEX_TYPE_FOOTNOTE);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 70, lines > 1 ? 5 : 12);
    lv_obj_t *s = watch_theme_label(r, FLEX_FONT_TINY, FLEX_C_TXT2, short_caps(t));
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 70, lines > 1 ? -5 : -12);
    lv_obj_add_event_cb(r, type_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t->id);
    /* Estrella: su propio objeto pulsable (el toque no llega a la fila) */
    lv_obj_t *st = lv_obj_create(r);
    lv_obj_remove_style_all(st);
    lv_obj_set_size(st, 56, FLEX_ROW_H);
    lv_obj_align(st, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(st, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(st, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *si = ui_wb_icon_create(st, ACT_IC_STAR, 24, act_config_is_fav(c, t->id) ? STAR_ON : STAR_OFF);
    lv_obj_center(si);
    lv_obj_add_event_cb(st, star_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t->id);
    return r;
}

/* ---- Selector ---- */

static void cat_cb(lv_event_t *e)
{
    ui_wb_open_category((act_cat_t)(uintptr_t)lv_event_get_user_data(e));
    ui_nav_push(ui_wb_category_create);
}

static void search_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_wb_search_create);
}

lv_obj_t *ui_wb_workouts_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Entrenar", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    act_config_t c;
    watch_activity_config(&c);

    lv_obj_t *sr = flex_glass_pressable(list, FLEX_GLASS_CARD, 0, 0, CARD_W, 56, 28);
    lv_obj_set_width(sr, LV_PCT(100));
    lv_obj_t *sl = watch_theme_label(sr, FLEX_FONT_BODY, FLEX_C_MUTE, "Buscar entrenamiento");
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_add_event_cb(sr, search_cb, LV_EVENT_CLICKED, NULL);

    if (c.recent[0]) {
        watch_theme_section(list, "Recientes");
        for (int i = 0; i < ACT_RECENT_MAX && c.recent[i]; i++) {
            type_row(list, act_catalog_get(c.recent[i]), &c);
        }
    }
    if (c.fav[0]) {
        watch_theme_section(list, "Favoritos");
        for (int i = 0; i < ACT_FAV_MAX && c.fav[i]; i++) {
            type_row(list, act_catalog_get(c.fav[i]), &c);
        }
    }
    watch_theme_section(list, "Categorías");
    for (int k = 0; k < ACT_CAT_COUNT; k++) {
        uint8_t ids[24];
        char sub[24];
        const size_t n = act_catalog_in_cat((act_cat_t)k, ids, sizeof(ids));
        snprintf(sub, sizeof(sub), "%u tipos", (unsigned)n);
        lv_obj_t *r = ui_wb_row(list, act_cat_icon((act_cat_t)k), ui_wb_cat_color((act_cat_t)k),
                                act_cat_name((act_cat_t)k), sub);
        lv_obj_add_event_cb(r, cat_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)k);
    }
    lv_obj_t *note = watch_theme_label(list, FLEX_FONT_TINY, FLEX_C_MUTE,
                                       "Sin pulsómetro ni GPS: cada tipo mide lo que el sensor de movimiento permite.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, LV_PCT(100));
    return page;
}

lv_obj_t *ui_wb_category_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, act_cat_name(s_open.cat), back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    act_config_t c;
    watch_activity_config(&c);
    uint8_t ids[24];
    const size_t n = act_catalog_in_cat(s_open.cat, ids, sizeof(ids));
    for (size_t i = 0; i < n; i++) {
        type_row(list, act_catalog_get(ids[i]), &c);
    }
    return page;
}

/* ---- Búsqueda ---- */

typedef struct {
    lv_obj_t *ta, *res, *kb;
    char last[40];
} search_t;

static void show_kb(search_t *s, bool on)
{
    watch_theme_set_visible(s->kb, on);
    lv_obj_set_height(s->res, on ? 236 - (FLEX_HDR_H + 66) : FLEX_SCREEN_H - FLEX_HDR_H - 66 - FLEX_LIST_BOTTOM);
}

static void search_fill(search_t *s)
{
    const char *q = lv_textarea_get_text(s->ta);
    if (strcmp(q, s->last) == 0) {
        return;
    }
    snprintf(s->last, sizeof(s->last), "%s", q);
    lv_obj_clean(s->res);
    uint8_t ids[SEARCH_MAX];
    const size_t n = act_catalog_search(q, ids, SEARCH_MAX);
    act_config_t c;
    watch_activity_config(&c);
    for (size_t i = 0; i < n; i++) {
        type_row(s->res, act_catalog_get(ids[i]), &c);
    }
    if (!n && q[0]) {
        watch_theme_label(s->res, FLEX_FONT_SMALL, FLEX_C_TXT2, "Sin resultados");
    }
}

static void ta_cb(lv_event_t *e)
{
    search_t *s = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_kb(s, true);
    } else {
        search_fill(s);
    }
}

static void search_ok(void *ud)
{
    search_t *s = ud;
    search_fill(s);
    show_kb(s, false);
}

static void search_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_wb_search_create(lv_obj_t *parent)
{
    search_t *s = lv_malloc_zeroed(sizeof(*s));
    if (!s) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Buscar", back_cb, NULL);
    s->ta = lv_textarea_create(page);
    lv_textarea_set_one_line(s->ta, true);
    lv_textarea_set_max_length(s->ta, (sizeof(s->last) - 1) / 2);
    lv_textarea_set_placeholder_text(s->ta, "Correr, yoga, tenis...");
    lv_obj_set_size(s->ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 56);
    lv_obj_set_pos(s->ta, FLEX_SAFE_X, FLEX_HDR_H);
    ui_textarea_style(s->ta);
    lv_obj_add_event_cb(s->ta, ta_cb, LV_EVENT_CLICKED, s);
    lv_obj_add_event_cb(s->ta, ta_cb, LV_EVENT_VALUE_CHANGED, s);
    s->res = watch_theme_list(page);
    lv_obj_set_pos(s->res, 0, FLEX_HDR_H + 66);
    s->kb = ui_keyboard_create(page, s->ta, UI_KB_TEXT, search_ok, s);
    lv_obj_set_size(s->kb, FLEX_SCREEN_W - 2 * 16, 452 - 236);
    lv_obj_set_pos(s->kb, 16, 236);
    show_kb(s, true);
    lv_obj_add_event_cb(page, search_free_cb, LV_EVENT_DELETE, s);
    return page;
}

/* ---- Pantalla del entrenamiento ---- */

typedef enum { VIEW_NONE = 0, VIEW_COUNTDOWN, VIEW_LIVE, VIEW_SUMMARY } view_t;

#define MAX_TILES 4

typedef struct {
    lv_obj_t *page, *body, *title;
    view_t view;
    uint8_t type;
    uint32_t t0;             /* inicio de la cuenta atrás (lv_tick) */
    bool cd_hold;            /* app suspendida durante la cuenta atrás: no empieza */
    lv_obj_t *cd_arc, *cd_num;
    lv_obj_t *state, *mmss, *tenth, *sub;
    lv_obj_t *tile_val[MAX_TILES];
    uint8_t tile_kind[MAX_TILES];
    int ntiles;
    lv_obj_t *pause_btn, *end_btn;
    uint32_t confirm_at;     /* primer toque de "Terminar" (0 = ninguno) */
    view_t pending;          /* vista que rehacer en el próximo tick: nunca se borra
                              * el botón pulsado dentro de su propio evento */
    uint32_t gen;
} wk_t;

enum { T_KCAL = 0, T_STEPS, T_DIST, T_PACE, T_CAD, T_MOTION };

static void rebuild(wk_t *w, view_t v);

static void start_now(wk_t *w)
{
    if (watch_activity_workout_start(w->type, false) != ESP_OK) {
        /* ya había uno en marcha (otra página, la sugerencia): se enseña ese */
        ui_banner_show_info("Ya hay un entrenamiento", "Se muestra el que está en marcha");
    }
    rebuild(w, VIEW_LIVE);
}

static void countdown_tap_cb(lv_event_t *e)
{
    wk_t *w = lv_event_get_user_data(e);
    w->t0 = lv_tick_get() - COUNTDOWN_MS; /* el próximo tick empieza */
}

static void build_countdown(wk_t *w)
{
    const act_type_t *t = act_catalog_get(w->type);
    lv_obj_t *b = w->body;
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, countdown_tap_cb, LV_EVENT_CLICKED, w);
    w->cd_arc = lv_arc_create(b);
    lv_obj_remove_style_all(w->cd_arc);
    lv_obj_set_size(w->cd_arc, 190, 190);
    lv_obj_align(w->cd_arc, LV_ALIGN_TOP_MID, 0, 6);
    lv_arc_set_rotation(w->cd_arc, 270);
    lv_arc_set_bg_angles(w->cd_arc, 0, 360);
    lv_arc_set_range(w->cd_arc, 0, COUNTDOWN_MS);
    lv_arc_set_value(w->cd_arc, COUNTDOWN_MS);
    lv_obj_set_style_arc_width(w->cd_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(w->cd_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w->cd_arc, flex_color(0x2A3312), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(w->cd_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(w->cd_arc, flex_color(WB_C_GO), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w->cd_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->cd_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(w->cd_arc, LV_OBJ_FLAG_CLICKABLE);
    w->cd_num = watch_theme_label(w->cd_arc, FLEX_FONT_CLOCK, WB_C_GO, "3");
    lv_obj_center(w->cd_num);
    char m[96];
    ui_wb_metrics_text(t, m, sizeof(m));
    lv_obj_t *ml = watch_theme_label(b, FLEX_FONT_TINY, FLEX_C_TXT2, m);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(ml, FLEX_SCREEN_W - 60);
    lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ml, LV_ALIGN_TOP_MID, 0, 206);
    if (t->flags & ACT_F_WATER) {
        lv_obj_t *wl = watch_theme_label(b, FLEX_FONT_TINY, FLEX_C_WARN,
                                         "Comprueba la resistencia al agua del reloj antes de mojarlo.");
        lv_label_set_long_mode(wl, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(wl, FLEX_SCREEN_W - 60);
        lv_obj_set_style_text_align(wl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(wl, LV_ALIGN_TOP_MID, 0, 262);
    }
    lv_obj_t *hint = watch_theme_label(b, FLEX_FONT_SMALL, FLEX_C_MUTE, "Toca para empezar ya");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -46);
    w->t0 = lv_tick_get();
}

static void pause_cb(lv_event_t *e)
{
    wk_t *w = lv_event_get_user_data(e);
    watch_activity_status_t st;
    watch_activity_get(&st);
    if (st.v.ses == ACT_SES_ACTIVE) {
        watch_activity_workout_pause();
    } else if (st.v.ses == ACT_SES_PAUSED) {
        watch_activity_workout_resume();
    }
    w->gen = 0; /* refrescar ya */
}

static void end_cb(lv_event_t *e)
{
    wk_t *w = lv_event_get_user_data(e);
    if (!w->confirm_at || lv_tick_elaps(w->confirm_at) > CONFIRM_MS) {
        w->confirm_at = (lv_tick_get() - 1u) | 1u; /* nunca en el futuro: lv_tick_elaps lo leería como una vuelta */
        flex_glass_button_set(w->end_btn, "¿Terminar?", 0);
        return;
    }
    w->confirm_at = 0;
    watch_activity_workout_end();
    w->pending = VIEW_SUMMARY;
}

static void add_tile(wk_t *w, lv_obj_t *grid, uint8_t kind, const char *label, uint32_t rgb)
{
    if (w->ntiles >= MAX_TILES) {
        return;
    }
    const int i = w->ntiles++;
    lv_obj_t *t = lv_obj_create(grid);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, (FLEX_SCREEN_W - 2 * 28 - 10) / 2, 66);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    w->tile_val[i] = watch_theme_label(t, FLEX_TYPE_TITLE, rgb, "--");
    lv_obj_align(w->tile_val[i], LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *l = watch_theme_label(t, FLEX_FONT_TINY, FLEX_C_TXT2, label);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    w->tile_kind[i] = kind;
}

static void build_live(wk_t *w)
{
    const act_type_t *t = act_catalog_get(w->type);
    lv_obj_t *b = w->body;
    w->state = watch_theme_label(b, FLEX_FONT_TINY, WB_C_GO, "");
    lv_obj_align(w->state, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *row = lv_obj_create(b);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 16);
    w->mmss = watch_theme_label(row, FLEX_FONT_CLOCK, WB_C_TIMER, "00:00");
    w->tenth = watch_theme_label(row, FLEX_FONT_CAPS_L, WB_C_TIMER, ",0");
    lv_obj_set_style_pad_bottom(w->tenth, 14, 0);
    w->sub = watch_theme_label(b, FLEX_FONT_TINY, FLEX_C_TXT2, "");
    lv_obj_align(w->sub, LV_ALIGN_TOP_MID, 0, 146);

    lv_obj_t *grid = lv_obj_create(b);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, FLEX_SCREEN_W - 2 * 28, 66 * 2 + 10);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 172);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    w->ntiles = 0;
    add_tile(w, grid, T_KCAL, "kcal activas (est.)", WB_C_MOVE);
    if (t->flags & ACT_F_STEPS) {
        add_tile(w, grid, T_STEPS, "pasos", WB_C_STEPS_TXT);
    }
    if (t->flags & ACT_F_DIST) {
        add_tile(w, grid, T_DIST, "distancia (est.)", WB_C_DIST);
        add_tile(w, grid, T_PACE, "ritmo medio / km", WB_C_PACE);
    } else if (t->flags & ACT_F_STEPS) {
        add_tile(w, grid, T_CAD, "pasos por minuto", FLEX_C_TXT);
    }
    if ((t->flags & ACT_F_MOTION) && w->ntiles < MAX_TILES) {
        add_tile(w, grid, T_MOTION, "movimiento (muñeca)", WB_C_EXER);
    }
    if (t->flags & ACT_F_DURATION) {
        lv_obj_t *n = watch_theme_label(grid, FLEX_FONT_TINY, FLEX_C_MUTE,
                                        "Este sensor no mide más de esta actividad: se registra el tiempo.");
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(n, (FLEX_SCREEN_W - 2 * 28 - 10) / 2);
    }

    /* Controles por encima de las franjas de los gestos de borde */
    w->pause_btn = flex_glass_button(b, "Pausa", WB_C_TIMER, 168, 62);
    lv_obj_set_style_text_color(w->pause_btn, flex_color(FLEX_C_BLACK), 0);
    lv_obj_align(w->pause_btn, LV_ALIGN_TOP_LEFT, 28, 326);
    lv_obj_add_event_cb(w->pause_btn, pause_cb, LV_EVENT_CLICKED, w);
    w->end_btn = flex_glass_button(b, "Terminar", WB_C_STOP, 168, 62);
    lv_obj_align(w->end_btn, LV_ALIGN_TOP_RIGHT, -28, 326);
    lv_obj_add_event_cb(w->end_btn, end_cb, LV_EVENT_CLICKED, w);
    w->confirm_at = 0;
    w->gen = 0;
}

static const char *motion_word(uint16_t mg)
{
    return mg >= 250 ? "Alto" : (mg >= 70 ? "Medio" : (mg >= 15 ? "Bajo" : "Quieto"));
}

static void refresh_live(wk_t *w, const watch_activity_status_t *st)
{
    const act_view_t *v = &st->v;
    char a[16], b[16];
    wb_fmt_stopwatch(a, sizeof(a), b, sizeof(b), v->active_ms);
    ui_wb_set_text(w->mmss, a);
    ui_wb_set_text(w->tenth, b);
    const uint32_t g = watch_activity_generation();
    if (g == w->gen) {
        return;
    }
    w->gen = g;
    char s[64], t1[16], t2[16];
    snprintf(s, sizeof(s), "%s", ui_wb_state_text(v));
    for (unsigned char *c = (unsigned char *)s; *c; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (unsigned char)(*c - 'a' + 'A'); /* rótulo en mayúsculas */
        } else if (*c == 0xC3 && c[1] >= 0xA0 && c[1] <= 0xBF) {
            c[1] = (unsigned char)(c[1] - 0x20); /* á -> Á, ñ -> Ñ... (UTF-8 de dos bytes) */
            c++;
        }
    }
    ui_wb_set_text(w->state, s);
    watch_theme_set_text_color(w->state, v->ses == ACT_SES_ACTIVE ? WB_C_GO : WB_C_TIMER);
    wb_fmt_dur(t1, sizeof(t1), v->total_ms);
    wb_fmt_dur(t2, sizeof(t2), v->paused_ms);
    snprintf(s, sizeof(s), "Total %s · Pausa %s", t1, t2);
    ui_wb_set_text(w->sub, s);
    for (int i = 0; i < w->ntiles; i++) {
        switch (w->tile_kind[i]) {
        case T_KCAL:
            if (v->ses_kcal_valid) {
                snprintf(s, sizeof(s), "%lu", (unsigned long)v->ses_kcal);
            } else {
                snprintf(s, sizeof(s), "--");
            }
            break;
        case T_STEPS:
            wb_fmt_int(s, sizeof(s), v->ses_steps);
            break;
        case T_DIST:
            wb_fmt_dist(s, sizeof(s), v->ses_dist_m);
            break;
        case T_PACE:
            wb_fmt_pace(s, sizeof(s), v->pace_s_km);
            break;
        case T_CAD:
            if (v->ses_cadence) {
                snprintf(s, sizeof(s), "%u", v->ses_cadence);
            } else {
                snprintf(s, sizeof(s), "--");
            }
            break;
        default:
            snprintf(s, sizeof(s), "%s", motion_word(v->ses == ACT_SES_ACTIVE ? v->motion_mg : 0));
            break;
        }
        ui_wb_set_text(w->tile_val[i], s);
    }
    const bool paused = v->ses == ACT_SES_PAUSED;
    flex_glass_button_set(w->pause_btn, paused ? "Reanudar" : "Pausa", paused ? WB_C_GO : WB_C_TIMER);
    if (w->confirm_at && lv_tick_elaps(w->confirm_at) > CONFIRM_MS) {
        w->confirm_at = 0;
        flex_glass_button_set(w->end_btn, "Terminar", 0);
    }
}

static void info(lv_obj_t *list, const char *k, const char *v)
{
    watch_theme_info_row(list, k, v);
}

static void done_cb(lv_event_t *e)
{
    (void)e;
    ui_wb_back_home();
}

static void build_summary(wk_t *w, const watch_activity_status_t *st)
{
    const act_view_t *v = &st->v;
    const act_type_t *t = act_catalog_get(v->type);
    lv_obj_t *list = watch_theme_list(w->body);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_height(list, FLEX_SCREEN_H - FLEX_HDR_H - FLEX_LIST_BOTTOM);
    char b[48];
    lv_obj_t *hdr = lv_obj_create(list);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), 64);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bd = ui_wb_icon_badge(hdr, (act_icon_t)t->icon, 56, ui_wb_cat_color((act_cat_t)t->cat));
    lv_obj_align(bd, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nm = watch_theme_label(hdr, FLEX_TYPE_BODY, FLEX_C_TXT, "");
    const int nl = ui_wb_fit(nm, t->name, CARD_W - 70, FLEX_TYPE_BODY, FLEX_TYPE_FOOTNOTE);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 68, nl > 1 ? 0 : 6);
    lv_obj_t *how = watch_theme_label(hdr, FLEX_FONT_TINY, v->end_reason == ACT_END_AUTO ? FLEX_C_WARN : WB_C_EXER,
                                      v->end_reason == ACT_END_AUTO ? "Terminado solo tras la pausa" : "Terminado");
    lv_obj_align(how, LV_ALIGN_BOTTOM_LEFT, 68, -8);
    if (v->start_utc > 0) {
        wt_datetime_t dt;
        wt_utc_to_local(v->start_utc, watch_time_get_tz(), &dt);
        wt_fmt_hhmm(b, sizeof(b), &dt);
        info(list, "Inicio", b);
    }
    wb_fmt_dur(b, sizeof(b), v->total_ms);
    info(list, "Tiempo total", b);
    wb_fmt_dur(b, sizeof(b), v->active_ms);
    info(list, "Activo", b);
    wb_fmt_dur(b, sizeof(b), v->paused_ms);
    info(list, "En pausa", b);
    if (v->ses_kcal_valid) {
        snprintf(b, sizeof(b), "%lu kcal", (unsigned long)v->ses_kcal);
        info(list, "Calorías activas (est.)", b);
    } else {
        info(list, "Calorías activas", "Sin peso");
    }
    if (t->flags & ACT_F_STEPS) {
        wb_fmt_int(b, sizeof(b), v->ses_steps);
        info(list, "Pasos", b);
    }
    if (t->flags & ACT_F_DIST) {
        wb_fmt_dist(b, sizeof(b), v->ses_dist_m);
        info(list, "Distancia (estimada)", b);
        wb_fmt_pace(b, sizeof(b), v->pace_s_km);
        info(list, "Ritmo medio", b);
    }
    if (v->auto_pauses) {
        snprintf(b, sizeof(b), "%u", v->auto_pauses);
        info(list, "Pausas automáticas", b);
    }
    info(list, "Guardado", st->storage_ok ? "En el historial" : (st->persistent ? "Hasta mañana (sin LittleFS)" : "No"));
    lv_obj_t *ok = flex_glass_button(list, "Listo", WB_C_GO, CARD_W, 60);
    lv_obj_set_width(ok, LV_PCT(100));
    lv_obj_set_style_text_color(ok, flex_color(FLEX_C_BLACK), 0);
    lv_obj_add_event_cb(ok, done_cb, LV_EVENT_CLICKED, w);
}

static void rebuild(wk_t *w, view_t view)
{
    if (w->body) {
        lv_obj_delete(w->body);
    }
    w->body = lv_obj_create(w->page);
    lv_obj_remove_style_all(w->body);
    lv_obj_set_pos(w->body, 0, FLEX_HDR_H);
    lv_obj_set_size(w->body, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_remove_flag(w->body, LV_OBJ_FLAG_SCROLLABLE);
    /* debajo de las franjas de los gestos de borde (que son hijas posteriores) */
    lv_obj_move_to_index(w->body, 1);
    w->view = view;
    watch_activity_status_t st;
    watch_activity_get(&st);
    if (view != VIEW_COUNTDOWN && (st.v.ses != ACT_SES_IDLE)) {
        w->type = st.v.type;
    }
    ui_wb_set_text(w->title, act_catalog_get(w->type)->name);
    switch (view) {
    case VIEW_COUNTDOWN:
        build_countdown(w);
        break;
    case VIEW_LIVE:
        build_live(w);
        refresh_live(w, &st);
        break;
    case VIEW_SUMMARY:
        build_summary(w, &st);
        break;
    default:
        break;
    }
}

static void wk_tick_cb(lv_timer_t *t)
{
    wk_t *w = lv_timer_get_user_data(t);
    if (w->pending != VIEW_NONE) {
        const view_t v = w->pending;
        w->pending = VIEW_NONE;
        rebuild(w, v);
        return;
    }
    if (w->view == VIEW_COUNTDOWN) {
        if (w->cd_hold) {
            return;
        }
        const uint32_t el = lv_tick_elaps(w->t0);
        if (el >= COUNTDOWN_MS) {
            start_now(w);
            return;
        }
        if (ui_nav_top() == w->page && ui_screen_is_on()) {
            lv_arc_set_value(w->cd_arc, (int16_t)(COUNTDOWN_MS - el));
            char n[4];
            snprintf(n, sizeof(n), "%lu", (unsigned long)((COUNTDOWN_MS - el + 999) / 1000));
            ui_wb_set_text(w->cd_num, n);
        }
        return;
    }
    if (ui_nav_top() != w->page || !ui_screen_is_on()) {
        return;
    }
    if (w->view == VIEW_LIVE) {
        watch_activity_status_t st;
        watch_activity_get(&st);
        if (st.v.ses == ACT_SES_COMPLETED) {
            rebuild(w, VIEW_SUMMARY); /* terminó solo (5 min en pausa) */
            return;
        }
        refresh_live(w, &st);
    }
}

static void wk_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* A la esfera (o pantalla apagada) durante la cuenta atrás: no se empieza
 * nada a escondidas; al volver, la cuenta atrás empieza otra vez. */
static void wk_suspend_cb(lv_event_t *e)
{
    wk_t *w = lv_event_get_user_data(e);
    if (w->view == VIEW_COUNTDOWN) {
        w->cd_hold = lv_event_get_code(e) == (lv_event_code_t)ui_nav_event_suspend();
        w->t0 = lv_tick_get();
    }
}

lv_obj_t *ui_wb_workout_create(lv_obj_t *parent)
{
    wk_t *w = lv_malloc_zeroed(sizeof(*w));
    if (!w) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    w->page = page;
    lv_obj_t *hdr = watch_theme_header(page, "", back_cb, NULL);
    w->title = lv_obj_get_child(hdr, -1);
    if (!w->title || !lv_obj_check_type(w->title, &lv_label_class)) {
        w->title = watch_theme_label(page, FLEX_TYPE_LABEL, FLEX_C_TXT, "");
        lv_obj_align(w->title, LV_ALIGN_TOP_MID, 0, FLEX_SAFE_TOP + 20);
    }
    watch_activity_status_t st;
    watch_activity_get(&st);
    const bool running = st.v.ses == ACT_SES_ACTIVE || st.v.ses == ACT_SES_PAUSED;
    w->type = s_open.type ? s_open.type : st.v.type;
    view_t v;
    if (running) {
        v = VIEW_LIVE;
    } else if (s_open.type && s_open.from_suggestion) {
        /* Sugerencia aceptada: el tiempo ya contaba, sin cuenta atrás. */
        watch_activity_workout_start(s_open.type, true);
        v = VIEW_LIVE;
    } else if (s_open.type) {
        v = VIEW_COUNTDOWN;
    } else {
        v = st.v.ses == ACT_SES_COMPLETED ? VIEW_SUMMARY : VIEW_LIVE;
    }
    s_open.type = 0;
    s_open.from_suggestion = false;
    rebuild(w, v);
    lv_obj_add_event_cb(page, wk_free_cb, LV_EVENT_DELETE, w);
    lv_obj_add_event_cb(page, wk_suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), w);
    lv_obj_add_event_cb(page, wk_suspend_cb, (lv_event_code_t)ui_nav_event_resume(), w);
    ui_page_timer(page, wk_tick_cb, WK_TICK_MS, w);
    return page;
}
