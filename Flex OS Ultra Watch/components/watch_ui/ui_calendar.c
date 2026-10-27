/*
 * ui_calendar — app Calendario (IC_CALEND de Flex OS Ultra).
 *
 * calRender() de la referencia (FlexOS_Ultra_AppsBasic.h:351-402): título
 * "mes año", fila "D L M M J V S" (la semana empieza en domingo), rejilla del
 * mes y el día de hoy en un círculo del acento con el número en blanco; se
 * repinta al cambiar el día.
 * Añadido en el reloj: cambiar de mes con los botones o deslizando arriba y
 * abajo; tocar el título vuelve al mes actual. Sin hora fiable no se marca
 * ningún día (nunca un "hoy" inventado).
 */
#include "ui_internal.h"

#include <stdio.h>
#include "apps_logic.h"
#include "watch_theme.h"
#include "wt_time.h"

#define CAL_TICK_MS 30000
#define MONTH_Y     (FLEX_HDR_H + 2)
#define MONTH_H     44
#define WD_Y        (MONTH_Y + MONTH_H + 6)
#define GRID_Y      (WD_Y + 28)
#define GRID_BOTTOM 452
#define CELL_W      50
#define GRID_X      ((FLEX_SCREEN_W - 7 * CELL_W) / 2)

/* MO_FULL[0] (FlexOS_Ultra_Session.h:383): en minúscula, como en la referencia */
static const char *const kMonths[12] = {"enero", "febrero", "marzo",      "abril",   "mayo",      "junio",
                                        "julio", "agosto",  "septiembre", "octubre", "noviembre", "diciembre"};
static const char *const kWd[7] = {"D", "L", "M", "M", "J", "V", "S"};

typedef struct {
    int year, month;         /* mes a la vista */
    int t_year, t_month, t_day; /* hoy (t_day = 0: sin hora fiable) */
    lv_obj_t *title;
    lv_obj_t *grid;
    lv_obj_t *note;
} cal_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* Hoy en hora local. false sin hora fiable. */
static bool today(int *y, int *m, int *d)
{
    int64_t loc;
    if (!ui_clock_local_now(&loc)) {
        return false;
    }
    wt_datetime_t dt;
    wt_from_seconds(loc, &dt);
    *y = dt.year;
    *m = dt.month;
    *d = dt.day;
    return true;
}

static void build(cal_page_t *p)
{
    char t[32];
    snprintf(t, sizeof(t), "%s %d", kMonths[p->month - 1], p->year);
    lv_label_set_text(p->title, t);
    lv_obj_clean(p->grid);
    cal_month_t m;
    if (!cal_month(p->year, p->month, &m)) {
        return;
    }
    int32_t ch = (GRID_BOTTOM - GRID_Y) / m.rows;
    if (ch > 52) {
        ch = 52;
    }
    const int32_t d = (CELL_W < ch ? CELL_W : ch) - 6; /* círculo de hoy */
    for (int day = 1; day <= m.dim; day++) {
        int r, c;
        cal_cell(&m, day, &r, &c);
        const bool is_today = p->t_day && p->t_year == p->year && p->t_month == p->month && p->t_day == day;
        lv_obj_t *cell = lv_obj_create(p->grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, d, d);
        lv_obj_set_pos(cell, c * CELL_W + (CELL_W - d) / 2, r * ch + (ch - d) / 2);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        if (is_today) {
            lv_obj_set_style_radius(cell, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cell, flex_color(FLEX_C_ACCENT), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        }
        char n[12];
        snprintf(n, sizeof(n), "%d", day);
        lv_obj_t *l = watch_theme_label(cell, FLEX_FONT_BODY, is_today ? FLEX_C_WHITE : FLEX_C_TXT, n);
        lv_obj_center(l);
    }
}

static void go(cal_page_t *p, int delta)
{
    if (delta == 0) {
        if (p->t_day) {
            p->year = p->t_year;
            p->month = p->t_month;
        }
    } else {
        cal_shift(&p->year, &p->month, delta);
    }
    build(p);
}

static void prev_cb(lv_event_t *e)
{
    go(lv_event_get_user_data(e), -1);
}

static void next_cb(lv_event_t *e)
{
    go(lv_event_get_user_data(e), 1);
}

static void title_cb(lv_event_t *e)
{
    go(lv_event_get_user_data(e), 0);
}

static void gesture_cb(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(in);
    if (dir == LV_DIR_TOP) {
        go(lv_event_get_user_data(e), 1);
    } else if (dir == LV_DIR_BOTTOM) {
        go(lv_event_get_user_data(e), -1);
    }
}

/* Cambio de día (o la hora pasa a ser fiable): se repinta (calTick). */
static void tick_cb(lv_timer_t *t)
{
    cal_page_t *p = lv_timer_get_user_data(t);
    int y = 0, m = 0, d = 0;
    if (!today(&y, &m, &d)) {
        y = m = d = 0;
    }
    if (y != p->t_year || m != p->t_month || d != p->t_day) {
        const bool first = !p->t_day && d;
        p->t_year = y;
        p->t_month = m;
        p->t_day = d;
        if (first) {
            p->year = y;
            p->month = m;
        }
        if (d) {
            lv_obj_add_flag(p->note, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(p->note, LV_OBJ_FLAG_HIDDEN);
        }
        build(p);
    }
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static lv_obj_t *nav_btn(lv_obj_t *page, cal_page_t *p, flex_icon_t icon, int32_t x, lv_event_cb_t cb)
{
    lv_obj_t *b = flex_glass_control(page, x, MONTH_Y, 64, MONTH_H);
    lv_obj_set_style_radius(b, MONTH_H / 2, 0);
    lv_obj_t *ic = flex_icon_create(b, icon, 20, FLEX_C_TXT);
    lv_obj_center(ic);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, p);
    return b;
}

lv_obj_t *ui_calendar_create(lv_obj_t *parent)
{
    cal_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    if (today(&p->t_year, &p->t_month, &p->t_day)) {
        p->year = p->t_year;
        p->month = p->t_month;
    } else {
        p->year = 2026;
        p->month = 1;
    }

    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Calendario", back_cb, NULL);

    nav_btn(page, p, FLEX_ICON_CHEVRON_LEFT, FLEX_SAFE_X, prev_cb);
    nav_btn(page, p, FLEX_ICON_CHEVRON_RIGHT, FLEX_SCREEN_W - FLEX_SAFE_X - 64, next_cb);
    p->title = watch_theme_label(page, FLEX_FONT_BODY, FLEX_C_TXT, "");
    lv_obj_set_size(p->title, FLEX_SCREEN_W - 2 * (FLEX_SAFE_X + 64 + 6), MONTH_H);
    lv_obj_set_style_text_align(p->title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(p->title, (MONTH_H - lv_font_get_line_height(FLEX_FONT_BODY)) / 2, 0);
    lv_obj_set_pos(p->title, FLEX_SAFE_X + 64 + 6, MONTH_Y);
    lv_obj_add_flag(p->title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p->title, title_cb, LV_EVENT_CLICKED, p);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *l = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, kWd[i]);
        lv_obj_set_width(l, CELL_W);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, GRID_X + i * CELL_W, WD_Y);
    }

    p->grid = lv_obj_create(page);
    lv_obj_remove_style_all(p->grid);
    lv_obj_set_pos(p->grid, GRID_X, GRID_Y);
    lv_obj_set_size(p->grid, 7 * CELL_W, GRID_BOTTOM - GRID_Y);
    lv_obj_remove_flag(p->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p->grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p->grid, gesture_cb, LV_EVENT_GESTURE, p);

    p->note = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, "Sin hora: no se marca el día de hoy.");
    lv_obj_set_width(p->note, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 40);
    lv_obj_set_style_text_align(p->note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->note, FLEX_SAFE_X + 20, GRID_BOTTOM + 2);
    if (p->t_day) {
        lv_obj_add_flag(p->note, LV_OBJ_FLAG_HIDDEN);
    }

    build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, CAL_TICK_MS, p);
    return page;
}
