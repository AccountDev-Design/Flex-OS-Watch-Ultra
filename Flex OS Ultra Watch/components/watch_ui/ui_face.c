/*
 * ui_face — esfera del reloj: hora y fecha reales del servicio watch_time y
 * batería real de la caché de ui_system.
 *
 *  - Sin hora fiable muestra "--:--" y el motivo; nunca una hora inventada.
 *  - Un temporizador de 1 s, ligado a la pantalla (se borra con ella), solo
 *    reescribe una etiqueta cuando cambia lo que muestra: la hora una vez por
 *    minuto, la fecha una vez al día y la batería cuando cambia su estado.
 *  - Geometría del panel real de 410x502.
 */
#include "ui_internal.h"

#include <string.h>
#include "flex_battery_icon.h"
#include "ui_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

#define FACE_TICK_MS 1000

/* Geometría (px del panel de 410x502) */
#define TIME_Y     112
#define DATE_Y     (TIME_Y + 86 + 10)   /* 86 = alto de línea de la fuente del reloj */
#define COMP_W     236
#define COMP_H     84
#define COMP_Y     372
#define BICON_W    46
#define BICON_H    22

typedef struct {
    lv_obj_t *time;
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
    ui_face_time_t t;
    ui_face_time(st, st == WATCH_TIME_OK ? &dt : NULL, &t);
    if (t.key == f->last_key) {
        return;
    }
    f->last_key = t.key;
    lv_label_set_text(f->time, t.time);
    lv_obj_set_style_text_color(f->time, flex_color(t.valid ? FLEX_C_TXT : FLEX_C_MUTE), 0);
    if (strcmp(lv_label_get_text(f->date), t.date) != 0) {
        lv_label_set_text(f->date, t.date);
    }
}

static void update_battery(face_t *f)
{
    watch_power_snapshot_t snap;
    const bool have = ui_system_power(&snap);
    const batt_state_t st = have ? snap.track.state : BATT_STATE_UNKNOWN;
    const batt_level_t lv = have ? snap.track.level : BATT_LEVEL_UNKNOWN;
    const int pct = have ? snap.track.percent : -1;
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
    face_t *f = lv_timer_get_user_data(t);
    update_time(f);
    update_battery(f);
}

static void delete_cb(lv_event_t *e)
{
    face_t *f = lv_event_get_user_data(e);
    lv_timer_delete(f->timer);
    lv_free(f);
}

lv_obj_t *ui_face_create(void)
{
    watch_theme_init();
    face_t *f = lv_malloc_zeroed(sizeof(*f));
    LV_ASSERT_MALLOC(f);
    f->last_key = INT64_MIN;
    f->last_pct = -2;
    f->last_state = -1;
    f->last_level = -1;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    watch_theme_apply_background(scr);

    f->time = watch_theme_label(scr, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, TIME_Y);
    f->date = watch_theme_label(scr, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_set_width(f->date, FLEX_SCREEN_W - 2 * FLEX_SAFE_X);
    lv_obj_set_style_text_align(f->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(f->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, DATE_Y);

    /* Complicación de batería: vidrio con el indicador, el % y el estado. */
    lv_obj_t *card = watch_theme_glass_card(scr, (FLEX_SCREEN_W - COMP_W) / 2, COMP_Y, COMP_W, COMP_H);
    f->batt_icon = flex_battery_icon_create(card, BICON_W, BICON_H);
    lv_obj_align(f->batt_icon, LV_ALIGN_LEFT_MID, 0, 0);
    f->batt_pct = watch_theme_label(card, FLEX_FONT_TITLE, FLEX_C_TXT, "--");
    lv_obj_align(f->batt_pct, LV_ALIGN_TOP_LEFT, BICON_W + 16, -6);
    f->batt_state = watch_theme_label(card, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_width(f->batt_state, COMP_W - 36 - BICON_W - 16);
    lv_label_set_long_mode(f->batt_state, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->batt_state, LV_ALIGN_BOTTOM_LEFT, BICON_W + 16, 4);

    f->timer = lv_timer_create(tick_cb, FACE_TICK_MS, f);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, f);
    update_time(f);
    update_battery(f);
    return scr;
}
