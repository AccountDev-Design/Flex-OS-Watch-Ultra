/*
 * ui_recorder — app Grabadora (docs/MULTIMEDIA.md §6 y §9).
 *
 * La pantalla solo enseña el estado del servicio (wa_rec) y le pide cosas:
 * la captura y la escritura van en sus tareas, así que salir de la app no
 * para una grabación (al volver se ve en curso).
 *
 * Composición para 410 x 502 (la jerarquía de la grabadora del Apple Watch,
 * con el vidrio de Flex OS):
 *  - nivel: 28 barras que avanzan de derecha a izquierda, dibujadas en UN
 *    objeto (un evento de dibujo, no 28 objetos) e invalidado 15 veces por
 *    segundo solo mientras se graba; rojas grabando, grises en pausa;
 *  - tiempo grabado en cifras grandes;
 *  - chip de destino con el tiempo que cabe (abre "Guardar en");
 *  - botón central grande (grabar / parar), a la izquierda las grabaciones o
 *    "descartar" (con un segundo toque) y a la derecha pausa / seguir.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_logic.h"
#include "wa_logic.h"
#include "wa_player.h"
#include "wa_rec.h"
#include "watch_media.h"
#include "watch_theme.h"
#include "wm_store.h"

#define FAST_MS     66      /* 15 Hz con la grabación en marcha */
#define SLOW_MS     500
#define BARS        28
#define GRAPH_X     30
#define GRAPH_Y     96
#define GRAPH_W     350
#define GRAPH_H     118
#define TIME_Y      222
#define CHIP_Y      284
#define CHIP_W      300
#define CHIP_H      44
#define NOTE_Y      334
#define BTN_Y       384
#define REC_D       104
#define SIDE_D      76
#define C_REC       0xFF453A
#define C_BAR_IDLE  0x3A3D46
#define CONFIRM_MS  4000
#define REC_LIST_MAX 96

typedef struct {
    lv_obj_t *page, *graph, *time, *chip, *note;
    lv_obj_t *left, *left_icon, *rec, *rec_icon, *right, *right_icon;
    lv_timer_t *timer;
    uint8_t hist[BARS];      /* niveles 0..100, el más nuevo al final */
    uint32_t last_push;
    uint32_t gen;
    uint32_t discard_ms;     /* primer toque de "descartar" */
    wr_state_t shown;
} rec_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

/* ---------------- nivel ---------------- */

static void graph_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    rec_page_t *p = lv_obj_get_user_data(obj);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    const int32_t slot = w / BARS, bw = slot * 5 / 9;
    const bool live = p->shown == WR_RECORDING;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(live ? C_REC : C_BAR_IDLE);
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < BARS; i++) {
        int32_t bh = (int32_t)p->hist[i] * h / 100;
        if (bh < bw) {
            bh = bw; /* un punto en silencio */
        }
        const int32_t x = a.x1 + i * slot + (slot - bw) / 2;
        const int32_t y = a.y1 + (h - bh) / 2;
        const lv_area_t r = {x, y, x + bw - 1, y + bh - 1};
        lv_draw_rect(layer, &d, &r);
    }
}

static void push_level(rec_page_t *p, float level)
{
    memmove(p->hist, p->hist + 1, BARS - 1);
    p->hist[BARS - 1] = (uint8_t)(level * 100.0f + 0.5f);
    lv_obj_invalidate(p->graph);
}

/* ---------------- estado ---------------- */

static void set_round(lv_obj_t *b, lv_obj_t *icon, flex_icon_t ic, uint32_t tint)
{
    flex_icon_set(icon, ic);
    flex_glass_set_tint(b, tint, LV_STATE_DEFAULT);
}

static void refresh(rec_page_t *p)
{
    wr_status_t s;
    wr_status(&s);
    const bool active = s.state == WR_RECORDING || s.state == WR_PAUSED || s.state == WR_STARTING;
    const uint32_t now = lv_tick_get();
    if (p->discard_ms && lv_tick_elaps(p->discard_ms) > CONFIRM_MS) {
        p->discard_ms = 0; /* la confirmación de descartar caduca */
    }
    /* Nivel: una barra nueva cada ~66 ms grabando; en pausa se congela. */
    if (s.state == WR_RECORDING && now - p->last_push >= FAST_MS - 5) {
        p->last_push = now;
        push_level(p, s.level);
    }
    if (p->shown != s.state) {
        p->shown = s.state;
        lv_obj_invalidate(p->graph); /* color de las barras */
        if (!active) {
            memset(p->hist, 0, sizeof(p->hist));
        }
        lv_timer_set_period(p->timer, active ? FAST_MS : SLOW_MS);
    }
    char t[48];
    if (active || s.state == WR_SAVING) {
        wa_fmt_time(s.elapsed_ms, t, sizeof(t));
    } else {
        snprintf(t, sizeof(t), "0:00");
    }
    set_text(p->time, t);
    /* Chip: dónde y cuánto cabe (grabando, el que mide el servicio). */
    char extra[48] = "";
    if (active) {
        char left[24];
        ui_fmt_left_s(s.seconds_left, left, sizeof(left));
        snprintf(extra, sizeof(extra), "quedan %s", left);
    }
    ui_dest_chip_refresh(p->chip, extra[0] ? extra : NULL);
    enable(p->chip, !active && s.state != WR_SAVING);
    /* Mensaje: error, aviso al terminar o la limitación del destino. */
    if (s.message[0] && (s.state == WR_ERROR || s.state == WR_SAVED || s.state == WR_IDLE)) {
        set_text(p->note, s.message);
        watch_theme_set_text_color(p->note, s.state == WR_ERROR ? FLEX_C_WARN : FLEX_C_TXT2);
    } else if (active && p->discard_ms) {
        set_text(p->note, "Toca otra vez la papelera para descartar la grabación");
        watch_theme_set_text_color(p->note, FLEX_C_WARN);
    } else if (active && s.dropped_ms > 0) {
        snprintf(t, sizeof(t), "Se han perdido %u ms: el disco va lento", (unsigned)s.dropped_ms);
        set_text(p->note, t);
        watch_theme_set_text_color(p->note, FLEX_C_WARN);
    } else if (active && s.clipping) {
        set_text(p->note, "Demasiado cerca: el sonido satura");
        watch_theme_set_text_color(p->note, FLEX_C_WARN);
    } else if (!active && s.state != WR_SAVING) {
        wr_plan_t plan;
        wr_plan(&plan);
        set_text(p->note, plan.note);
        watch_theme_set_text_color(p->note, plan.ok ? FLEX_C_MUTE : FLEX_C_WARN);
    } else {
        set_text(p->note, s.state == WR_SAVING ? "Guardando…" : "");
        watch_theme_set_text_color(p->note, FLEX_C_TXT2);
    }
    /* Botones. */
    if (active) {
        set_round(p->rec, p->rec_icon, FLEX_ICON_STOP, C_REC);
        set_round(p->right, p->right_icon, s.state == WR_PAUSED ? FLEX_ICON_MIC : FLEX_ICON_PAUSE, FLEX_GLASS_TINT_NONE);
        set_round(p->left, p->left_icon, FLEX_ICON_TRASH, p->discard_ms ? FLEX_C_DANGER : FLEX_GLASS_TINT_NONE);
        watch_theme_set_visible(p->right, true);
        enable(p->rec, s.state != WR_STARTING);
        enable(p->right, s.state != WR_STARTING);
    } else {
        set_round(p->rec, p->rec_icon, FLEX_ICON_MIC, C_REC);
        set_round(p->left, p->left_icon, FLEX_ICON_STORAGE, FLEX_GLASS_TINT_NONE);
        watch_theme_set_visible(p->right, false);
        enable(p->rec, s.state != WR_SAVING);
        p->discard_ms = 0;
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void rec_cb(lv_event_t *e)
{
    rec_page_t *p = lv_event_get_user_data(e);
    wr_status_t s;
    wr_status(&s);
    if (s.state == WR_RECORDING || s.state == WR_PAUSED) {
        wr_stop();
    } else {
        wr_ack();
        wr_start(); /* si no se puede, su motivo sale en el mensaje */
    }
    refresh(p);
}

static void right_cb(lv_event_t *e)
{
    rec_page_t *p = lv_event_get_user_data(e);
    wr_status_t s;
    wr_status(&s);
    if (s.state == WR_RECORDING) {
        wr_pause();
    } else if (s.state == WR_PAUSED) {
        wr_resume();
    }
    refresh(p);
}

static void left_cb(lv_event_t *e)
{
    rec_page_t *p = lv_event_get_user_data(e);
    wr_status_t s;
    wr_status(&s);
    if (s.state == WR_RECORDING || s.state == WR_PAUSED) {
        if (!p->discard_ms) {
            p->discard_ms = (lv_tick_get() - 1u) | 1u; /* != 0 y nunca en el futuro (lv_tick_elaps) */
        } else {
            p->discard_ms = 0;
            wr_cancel();
        }
    } else {
        ui_nav_push(ui_recordings_create);
    }
    refresh(p);
}

static lv_obj_t *round_btn(lv_obj_t *parent, int32_t d, flex_icon_t ic, int32_t isz, lv_obj_t **icon,
                           lv_event_cb_t cb, rec_page_t *p)
{
    lv_obj_t *b = flex_glass_icon_button(parent, ic, isz, d, FLEX_GLASS_TINT_NONE);
    *icon = lv_obj_get_child(b, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, p);
    return b;
}

static void page_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_recorder_create(lv_obj_t *parent)
{
    rec_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Grabadora", back_cb, NULL);

    p->graph = lv_obj_create(page);
    lv_obj_remove_style_all(p->graph);
    lv_obj_set_pos(p->graph, GRAPH_X, GRAPH_Y);
    lv_obj_set_size(p->graph, GRAPH_W, GRAPH_H);
    lv_obj_remove_flag(p->graph, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(p->graph, p);
    lv_obj_add_event_cb(p->graph, graph_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    p->time = watch_theme_label(page, FLEX_FONT_CAPS_L, FLEX_C_TXT, "0:00");
    lv_obj_set_width(p->time, FLEX_SCREEN_W);
    lv_obj_set_style_text_align(p->time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->time, 0, TIME_Y);

    p->chip = ui_dest_chip(page, CHIP_W, CHIP_H);
    lv_obj_set_pos(p->chip, (FLEX_SCREEN_W - CHIP_W) / 2, CHIP_Y);

    p->note = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    lv_label_set_long_mode(p->note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->note, 330);
    lv_obj_set_style_text_align(p->note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->note, (FLEX_SCREEN_W - 330) / 2, NOTE_Y);

    const int32_t cx = FLEX_SCREEN_W / 2;
    p->rec = round_btn(page, REC_D, FLEX_ICON_MIC, 40, &p->rec_icon, rec_cb, p);
    lv_obj_set_pos(p->rec, cx - REC_D / 2, BTN_Y);
    p->left = round_btn(page, SIDE_D, FLEX_ICON_STORAGE, 28, &p->left_icon, left_cb, p);
    lv_obj_set_pos(p->left, cx - REC_D / 2 - 24 - SIDE_D, BTN_Y + (REC_D - SIDE_D) / 2);
    p->right = round_btn(page, SIDE_D, FLEX_ICON_PAUSE, 26, &p->right_icon, right_cb, p);
    lv_obj_set_pos(p->right, cx + REC_D / 2 + 24, BTN_Y + (REC_D - SIDE_D) / 2);

    p->shown = (wr_state_t)-1;
    p->timer = ui_page_timer(page, tick_cb, SLOW_MS, p);
    refresh(p);
    lv_obj_add_event_cb(page, page_free_cb, LV_EVENT_DELETE, p);
    return page;
}

/* ======================= Grabaciones ======================= */

typedef struct {
    uint8_t vol;
    uint32_t id;
    uint32_t created;
} rec_ref_t;

typedef struct {
    lv_obj_t *list, *empty;
    rec_ref_t refs[REC_LIST_MAX];
    int n;
    uint32_t rev[WM_VOL_N];
} recs_page_t;

static bool is_recording(const FlexMlRec *r)
{
    return r->kind == FML_K_AUDIO && !strncmp(r->path, FML_DIR_REC "/", sizeof(FML_DIR_REC));
}

static void play_cb(lv_event_t *e)
{
    recs_page_t *p = lv_event_get_user_data(e);
    const int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (idx < 0 || idx >= p->n) {
        return;
    }
    /* La lista entera, empezando por la elegida (la siguiente es la anterior grabada). */
    wap_track_t *list = wm_alloc(sizeof(wap_track_t) * (size_t)p->n);
    if (!list) {
        ui_banner_show_info("Grabadora", "No hay memoria para reproducir");
        return;
    }
    int k = 0, start = 0;
    for (int i = 0; i < p->n; i++) {
        FlexMlRec r;
        if (!wm_lib_get((wm_vol_t)p->refs[i].vol, p->refs[i].id, &r)) {
            continue; /* se borró o se sacó la tarjeta entretanto */
        }
        if (i == idx) {
            start = k;
        }
        wap_track_t *t = &list[k++];
        memset(t, 0, sizeof(*t));
        t->vol = p->refs[i].vol;
        t->id = r.id;
        snprintf(t->path, sizeof(t->path), "%s", r.path);
        ui_media_display_name(&r, t->name, sizeof(t->name));
        ui_copy_utf8(t->title, sizeof(t->title), t->name);
        t->dur_ms = r.durMs;
    }
    if (k > 0) {
        wap_play_list(list, k, start);
        ui_nav_push(ui_nowplaying_create);
    }
    wm_free(list);
}

static void recs_build(recs_page_t *p)
{
    lv_obj_clean(p->list);
    p->n = 0;
    FlexMlRec *tmp = wm_alloc(sizeof(FlexMlRec) * REC_LIST_MAX);
    if (!tmp) {
        watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_WARN, "No hay memoria para listar las grabaciones.");
        return;
    }
    for (int v = 0; v < WM_VOL_N; v++) {
        const int n = wm_lib_snapshot((wm_vol_t)v, FML_MASK_AUDIO, tmp, REC_LIST_MAX, &p->rev[v]);
        for (int i = 0; i < n && p->n < REC_LIST_MAX; i++) {
            if (is_recording(&tmp[i]) && !(tmp[i].flags & FML_R_LOCKED)) {
                p->refs[p->n++] = (rec_ref_t){(uint8_t)v, tmp[i].id, tmp[i].created ? tmp[i].created : tmp[i].added};
            }
        }
    }
    /* De la más nueva a la más antigua. */
    for (int i = 1; i < p->n; i++) {
        const rec_ref_t x = p->refs[i];
        int k = i - 1;
        while (k >= 0 && p->refs[k].created < x.created) {
            p->refs[k + 1] = p->refs[k];
            k--;
        }
        p->refs[k + 1] = x;
    }
    for (int i = 0; i < p->n; i++) {
        FlexMlRec r;
        if (!wm_lib_get((wm_vol_t)p->refs[i].vol, p->refs[i].id, &r)) {
            continue;
        }
        char name[FML_NAME_MAX], val[48], d[16];
        ui_media_display_name(&r, name, sizeof(name));
        wa_fmt_time(r.durMs, d, sizeof(d));
        snprintf(val, sizeof(val), "%s%s", r.durMs ? d : "", p->refs[i].vol == WM_VOL_SD ? " · SD" : "");
        lv_obj_t *row = watch_theme_list_row(p->list, FLEX_ICON_MIC, C_REC, name, val, false);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, play_cb, LV_EVENT_CLICKED, p);
    }
    wm_free(tmp);
    if (p->n == 0) {
        lv_obj_t *l = watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2,
                                        "Aún no hay grabaciones. Las que hagas aparecerán aquí, en el reloj y en la microSD.");
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(l, LV_PCT(92));
    }
}

static void recs_tick_cb(lv_timer_t *t)
{
    recs_page_t *p = lv_timer_get_user_data(t);
    for (int v = 0; v < WM_VOL_N; v++) {
        if (wm_lib_rev((wm_vol_t)v) != p->rev[v]) {
            recs_build(p); /* algo cambió: nueva, borrada, tarjeta que entra o sale */
            return;
        }
    }
}

lv_obj_t *ui_recordings_create(lv_obj_t *parent)
{
    recs_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Grabaciones", back_cb, NULL);
    p->list = watch_theme_list(page);
    recs_build(p);
    lv_obj_add_event_cb(page, page_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, recs_tick_cb, 1000, p);
    return page;
}
