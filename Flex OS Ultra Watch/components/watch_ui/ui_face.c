/*
 * ui_face — esfera del reloj: hora y fecha reales del servicio watch_time y
 * batería real de la caché de ui_system.
 *
 *  - Sin hora fiable muestra "--:--" y el motivo; nunca una hora inventada.
 *  - Un temporizador de 1 s, ligado a la pantalla (se borra con ella), solo
 *    reescribe la hora al cambiar el minuto y la fecha al cambiar el día.
 *  - La batería se actualiza por suscripción (ui_power_listen): en cuanto
 *    ui_system recibe un cambio, sin sondeo propio ni esperar a un toque.
 *  - Geometría del panel real de 410x502.
 *  - Es la página raíz de ui_nav.
 */
#include "ui_internal.h"

#include <string.h>
#include "flex_battery_icon.h"
#include "ui_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define FACE_TICK_MS 1000

/* Geometría (px del panel de 410x502) */
#define TIME_Y     112
#define DATE_Y     (TIME_Y + 86 + 10)   /* 86 = alto de línea de la fuente del reloj */
#define COMP_W     264
#define COMP_H     96   /* relleno 18 + % (34) + estado (24) + relleno 18 */
#define COMP_Y     364
#define BICON_W    46
#define BICON_H    22

typedef struct {
    lv_obj_t *time;
    lv_obj_t *ampm; /* "AM"/"PM" junto a la hora en 12 h */
    lv_obj_t *date;
    lv_obj_t *batt_icon;
    lv_obj_t *batt_pct;
    lv_obj_t *batt_state;
    lv_timer_t *timer;
    int64_t last_key;
    int last_pct;
    int last_state;
    int last_level;
} face_t;

static void update_time(face_t *f)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);
    if (t.key == f->last_key) {
        return;
    }
    f->last_key = t.key;
    lv_label_set_text(f->time, t.time);
    lv_obj_set_style_text_color(f->time, flex_color(t.valid ? FLEX_C_TXT : FLEX_C_MUTE), 0);
    if (t.ampm[0]) {
        lv_label_set_text(f->ampm, t.ampm);
        lv_obj_update_layout(f->time);
        lv_obj_align_to(f->ampm, f->time, LV_ALIGN_OUT_RIGHT_TOP, 6, 16);
        lv_obj_remove_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
    }
    if (strcmp(lv_label_get_text(f->date), t.date) != 0) {
        lv_label_set_text(f->date, t.date);
    }
}

static void update_battery(face_t *f, const watch_power_snapshot_t *snap)
{
    const bool have = snap != NULL;
    const batt_state_t st = have ? snap->track.state : BATT_STATE_UNKNOWN;
    const batt_level_t lv = have ? snap->track.level : BATT_LEVEL_UNKNOWN;
    const int pct = have ? snap->track.percent : -1;
    if (pct == f->last_pct && (int)st == f->last_state && (int)lv == f->last_level) {
        return;
    }
    f->last_pct = pct;
    f->last_state = (int)st;
    f->last_level = (int)lv;

    const flex_batt_kind_t kind = ui_batt_kind(st, lv);
    flex_battery_icon_set(f->batt_icon, kind, pct);
    if (pct >= 0) {
        lv_label_set_text_fmt(f->batt_pct, "%d %%", pct);
    } else {
        lv_label_set_text(f->batt_pct, "--");
    }
    lv_label_set_text(f->batt_state, ui_batt_state_text(st));
    const uint32_t col =
        (kind == FLEX_BATT_NORMAL || kind == FLEX_BATT_UNKNOWN) ? FLEX_C_TXT2 : flex_batt_fill_rgb(kind);
    lv_obj_set_style_text_color(f->batt_state, flex_color(col), 0);
}

static void tick_cb(lv_timer_t *t)
{
    update_time(lv_timer_get_user_data(t));
}

/* Cambio de batería: llega de ui_system en cuanto se confirma (sin sondeo). */
static void power_cb(lv_obj_t *page, const watch_power_snapshot_t *snap)
{
    update_battery(lv_obj_get_user_data(page), snap);
}

static void delete_cb(lv_event_t *e)
{
    face_t *f = lv_event_get_user_data(e);
    lv_timer_delete(f->timer);
    lv_free(f);
}

lv_obj_t *ui_face_create(lv_obj_t *parent)
{
    watch_theme_init();
    face_t *f = lv_malloc_zeroed(sizeof(*f));
    LV_ASSERT_MALLOC(f);
    f->last_key = INT64_MIN;
    f->last_pct = -2;
    f->last_state = -1;
    f->last_level = -1;

    lv_obj_t *scr = ui_page_new(parent);
    watch_theme_apply_background(scr);

    f->time = watch_theme_label(scr, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, TIME_Y);
    f->ampm = watch_theme_label(scr, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_add_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
    f->date = watch_theme_label(scr, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_set_width(f->date, FLEX_SCREEN_W - 2 * FLEX_SAFE_X);
    lv_obj_set_style_text_align(f->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(f->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, DATE_Y);

    /* Complicación de batería: vidrio con el indicador, el % y el estado. */
    lv_obj_t *card = watch_theme_glass_card(scr, (FLEX_SCREEN_W - COMP_W) / 2, COMP_Y, COMP_W, COMP_H);
    f->batt_icon = flex_battery_icon_create(card, BICON_W, BICON_H);
    lv_obj_align(f->batt_icon, LV_ALIGN_LEFT_MID, 0, 0);
    /* % y estado en columna: nunca se pisan, sea cual sea el texto. */
    lv_obj_t *col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, COMP_W - 36 - BICON_W - 16, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, BICON_W + 16, 0);
    f->batt_pct = watch_theme_label(col, FLEX_FONT_TITLE, FLEX_C_TXT, "--");
    f->batt_state = watch_theme_label(col, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_size(f->batt_state, LV_PCT(100), lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_label_set_long_mode(f->batt_state, LV_LABEL_LONG_MODE_DOTS);

    /* Arrastrar hacia abajo desde arriba abre el panel rápido; deslizar
     * hacia la izquierda, las aplicaciones. */
    ui_qp_attach(scr);
    ui_apps_attach(scr);

    f->timer = lv_timer_create(tick_cb, FACE_TICK_MS, f);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, f);
    update_time(f);
    update_battery(f, NULL);
    lv_obj_set_user_data(scr, f);
    ui_power_listen(scr, power_cb);
    return scr;
}
