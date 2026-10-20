/*
 * ui_face — esferas del reloj: hora y fecha reales del servicio watch_time.
 *
 *  - Sin hora fiable muestran "--:--" (o las agujas ocultas) y el motivo;
 *    nunca una hora inventada.
 *  - Un temporizador ligado a la página solo reescribe lo que cambia (minuto,
 *    día; el segundero de la analógica cada segundo).
 *  - Geometría del panel real de 410x502. Es la página raíz de ui_nav.
 *  - La batería NO va bajo la hora: está en el panel rápido (y como
 *    complicación en la esfera modular).
 *  - Pixel Shift (si está activado): los elementos fijos se desplazan unos
 *    píxeles cada minuto para repartir el desgaste del AMOLED.
 *
 * Esferas (ajuste "face", ui_face_style_t):
 *  - Digital clásica: hora grande y fecha sobre el fondo elegido.
 *  - Analógica: esfera de 12 marcas, agujas y segundero (líneas de LVGL).
 *  - Modular: hora arriba y cuatro complicaciones de vidrio (fecha,
 *    batería, próxima alarma y clima de la caché de watch_weather; "--" si
 *    no hay dato, nunca valores falsos).
 *  - Liquid Glass: la hora dentro de una gran tarjeta de vidrio con el tinte
 *    adaptativo del tema sobre el fondo.
 *  - Parallax: capas (halo, tarjeta, hora) que se desplazan distinto según
 *    la inclinación REAL del acelerómetro (QMI8658). El IMU solo mide
 *    mientras la esfera se ve con la pantalla encendida; sin IMU, queda
 *    quieta.
 * Con Flex Ultra Power Saving activo se usa siempre una esfera mínima: hora y
 * fecha sobre negro, sin fondo, sin IMU y sin complicaciones que sondeen.
 *
 * Mantener pulsada la esfera abre el selector (ui_face_editor.c); deslizar
 * hacia abajo abre el cajón de aplicaciones; arrastrar desde la franja
 * superior, el panel rápido.
 */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_battery_icon.h"
#include "qmi8658.h"
#include "ui_logic.h"
#include "watch_imu.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"
#include "watch_weather.h"

#define FACE_TICK_MS     1000
#define PARALLAX_TICK_MS 50
#define FACE_SHIFT_MAX   3   /* px de Pixel Shift en la esfera */

/* Geometría (px del panel de 410x502) */
#define TIME_Y      150
#define DATE_Y      (TIME_Y + 86 + 10)   /* 86 = alto de línea de la fuente del reloj */
#define ANALOG_D    360
#define MOD_TIME_Y  44
#define MOD_TILE_W  176
#define MOD_TILE_H  112
#define MOD_GAP     14
#define MOD_TOP     176
#define GLASS_W     350
#define GLASS_H     230
#define PAR_MAX     18   /* px de desplazamiento de la capa delantera */
#define PAR_DATE_DY 58   /* fecha bajo la hora, dentro de la tarjeta */

static const char *const kNames[UI_FACE_COUNT_] = {"Digital", "Analógica", "Modular", "Liquid Glass", "Parallax"};

const char *ui_face_name(int style)
{
    return (style >= 0 && style < UI_FACE_COUNT_) ? kNames[style] : "";
}

typedef struct {
    int style;
    bool minimal;     /* ahorro de energía */
    lv_obj_t *page;
    lv_obj_t *shift;  /* contenedor que mueve el Pixel Shift */
    lv_obj_t *time;
    lv_obj_t *ampm;
    lv_obj_t *date;
    lv_obj_t *dial;
    /* modular */
    lv_obj_t *c_day, *c_wday, *c_batt, *c_batt_icon, *c_alarm, *c_wx, *c_wx_sub;
    /* parallax */
    lv_obj_t *p_halo, *p_card;
    int32_t p_x, p_y;  /* inclinación filtrada (décimas de grado) */
    bool imu_on;
    lv_timer_t *timer;
    lv_timer_t *ptimer;
    int64_t last_key;
    int last_sec;
    uint32_t shift_step;
    int last_pct;
    uint32_t wx_gen;
    uint32_t txt, txt2;
} face_t;

/* ---- Esfera analógica compartida (también la usa el AOD) ---- */

typedef struct {
    lv_point_precise_t ticks[12][2];
    lv_point_precise_t hand[3][2];
    lv_obj_t *l_hand[3]; /* hora, minuto, segundo */
    int32_t r;
} analog_t;

static void analog_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static lv_obj_t *line(lv_obj_t *parent, lv_point_precise_t *pts, int32_t w, uint32_t rgb, lv_opa_t opa)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, flex_color(rgb), 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(l, 0, 0);
    return l;
}

lv_obj_t *ui_analog_create(lv_obj_t *parent, int32_t d, bool aod)
{
    analog_t *a = lv_malloc_zeroed(sizeof(*a));
    LV_ASSERT_MALLOC(a);
    a->r = d / 2;
    lv_obj_t *dial = lv_obj_create(parent);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, d, d);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(dial, a);
    lv_obj_add_event_cb(dial, analog_free_cb, LV_EVENT_DELETE, a);
    const uint32_t tick_c = aod ? 0x40444E : FLEX_C_TXT2;
    for (int i = 0; i < 12; i++) {
        const float ang = (float)i * 3.14159265f / 6.0f;
        const float in = (i % 3 == 0) ? 0.80f : 0.87f;
        a->ticks[i][0].x = a->r + sinf(ang) * a->r * in;
        a->ticks[i][0].y = a->r - cosf(ang) * a->r * in;
        a->ticks[i][1].x = a->r + sinf(ang) * a->r * 0.96f;
        a->ticks[i][1].y = a->r - cosf(ang) * a->r * 0.96f;
        line(dial, a->ticks[i], i % 3 == 0 ? 6 : 3, tick_c, LV_OPA_COVER);
    }
    a->l_hand[0] = line(dial, a->hand[0], aod ? 6 : 10, aod ? 0x6A6F7C : FLEX_C_TXT, LV_OPA_COVER);
    a->l_hand[1] = line(dial, a->hand[1], aod ? 4 : 7, aod ? 0x6A6F7C : FLEX_C_TXT, LV_OPA_COVER);
    if (!aod) {
        a->l_hand[2] = line(dial, a->hand[2], 3, FLEX_C_ACCENT, LV_OPA_COVER);
        lv_obj_t *dot = lv_obj_create(dial);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, flex_color(FLEX_C_ACCENT), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_center(dot);
    }
    ui_analog_set(dial, -1, 0, -1);
    return dial;
}

static void set_hand(analog_t *a, int i, float frac, float len)
{
    const float ang = frac * 2.0f * 3.14159265f;
    a->hand[i][0].x = a->r - sinf(ang) * a->r * 0.08f;
    a->hand[i][0].y = a->r + cosf(ang) * a->r * 0.08f;
    a->hand[i][1].x = a->r + sinf(ang) * a->r * len;
    a->hand[i][1].y = a->r - cosf(ang) * a->r * len;
    lv_line_set_points(a->l_hand[i], a->hand[i], 2);
}

void ui_analog_set(lv_obj_t *dial, int hour, int minute, int second)
{
    analog_t *a = lv_obj_get_user_data(dial);
    if (!a) {
        return;
    }
    const bool valid = hour >= 0;
    for (int i = 0; i < 3; i++) {
        if (a->l_hand[i]) {
            if (valid && (i < 2 || second >= 0)) {
                lv_obj_remove_flag(a->l_hand[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(a->l_hand[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (!valid) {
        return;
    }
    set_hand(a, 0, ((float)(hour % 12) + (float)minute / 60.0f) / 12.0f, 0.50f);
    set_hand(a, 1, ((float)minute + (second > 0 ? (float)second / 60.0f : 0.0f)) / 60.0f, 0.78f);
    if (a->l_hand[2] && second >= 0) {
        set_hand(a, 2, (float)second / 60.0f, 0.86f);
    }
}

/* ---- Contenido ---- */

static void update_modular(face_t *f)
{
    watch_power_snapshot_t snap;
    const bool have = ui_system_power(&snap);
    const int pct = have ? snap.track.percent : -1;
    if (pct != f->last_pct) {
        f->last_pct = pct;
        flex_battery_icon_set(f->c_batt_icon,
                              ui_batt_kind(have ? snap.track.state : BATT_STATE_UNKNOWN,
                                           have ? snap.track.level : BATT_LEVEL_UNKNOWN),
                              pct);
        if (pct >= 0) {
            lv_label_set_text_fmt(f->c_batt, "%d %%", pct);
        } else {
            lv_label_set_text(f->c_batt, "--");
        }
    }
    /* Próxima alarma */
    char b[24];
    int64_t now_local;
    int idx = -1;
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ui_clock_local_now(&now_local)) {
        const int64_t next = alarms_next(ui_alarms(), now_local, &idx);
        if (next >= 0) {
            alarm_when_text(next, now_local, ws.h24, b, sizeof(b));
        } else {
            snprintf(b, sizeof(b), "Sin alarmas");
        }
    } else {
        snprintf(b, sizeof(b), "--");
    }
    if (strcmp(lv_label_get_text(f->c_alarm), b) != 0) {
        lv_label_set_text(f->c_alarm, b);
    }
    /* Clima: solo el dato real de la caché, y solo cuando cambia. */
    watch_weather_status_t *wst = lv_malloc(sizeof(*wst));
    if (!wst) {
        return;
    }
    watch_weather_get(wst);
    const uint32_t gen = wst->gen + (wst->have ? 1u : 0u);
    lv_free(wst);
    if (gen == f->wx_gen) {
        return;
    }
    f->wx_gen = gen;
    wx_data_t *wx = lv_malloc(sizeof(*wx));
    if (wx && watch_weather_data(wx)) {
        snprintf(b, sizeof(b), "%d°", (int)lroundf(wx->temp));
        lv_label_set_text(f->c_wx, b);
        lv_label_set_text(f->c_wx_sub, wx->loc.name);
    } else {
        lv_label_set_text(f->c_wx, "--");
        lv_label_set_text(f->c_wx_sub, "Sin datos");
    }
    lv_free(wx);
}

static void update_time(face_t *f)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);

    if (f->dial && t.valid && dt.second != f->last_sec) {
        f->last_sec = dt.second;
        ui_analog_set(f->dial, dt.hour, dt.minute, dt.second);
    }
    if (f->c_alarm) {
        update_modular(f);
    }
    if (t.key == f->last_key) {
        return;
    }
    f->last_key = t.key;
    if (f->shift) {
        /* Pixel Shift: un paso por cambio de minuto (ya se redibuja). */
        int32_t dx, dy;
        ui_pixel_shift(FACE_SHIFT_MAX, f->shift_step++, &dx, &dy);
        lv_obj_set_pos(f->shift, dx, dy);
    }
    if (f->dial && !t.valid) {
        ui_analog_set(f->dial, -1, 0, -1);
    }
    if (f->time) {
        lv_label_set_text(f->time, t.time);
        lv_obj_set_style_text_color(f->time, flex_color(t.valid ? f->txt : FLEX_C_MUTE), 0);
        if (f->ampm) {
            if (t.ampm[0]) {
                lv_label_set_text(f->ampm, t.ampm);
                lv_obj_update_layout(f->time);
                lv_obj_align_to(f->ampm, f->time, LV_ALIGN_OUT_RIGHT_TOP, 6, 16);
                lv_obj_remove_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (f->date && strcmp(lv_label_get_text(f->date), t.date) != 0) {
        lv_label_set_text(f->date, t.date);
    }
    if (f->c_day) {
        char b[8];
        if (t.valid) {
            static const char *const kWd[7] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
            snprintf(b, sizeof(b), "%d", dt.day);
            lv_label_set_text(f->c_day, b);
            lv_label_set_text(f->c_wday, kWd[dt.weekday % 7]);
        } else {
            lv_label_set_text(f->c_day, "--");
            lv_label_set_text(f->c_wday, "");
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    update_time(lv_timer_get_user_data(t));
}

/* Parallax: el IMU solo mide con la esfera a la vista y la pantalla encendida. */
static void parallax_cb(lv_timer_t *t)
{
    face_t *f = lv_timer_get_user_data(t);
    const bool visible = ui_nav_depth() == 1 && !ui_nav_busy() && ui_screen_is_on() && !ui_qp_active() &&
                         !lv_obj_has_flag(f->page, LV_OBJ_FLAG_HIDDEN);
    if (visible != f->imu_on) {
        f->imu_on = visible;
        watch_imu_set_active(visible);
    }
    if (!visible) {
        return;
    }
    watch_imu_snapshot_t im;
    int32_t pitch, roll;
    if (!watch_imu_get(&im) || im.state != IMU_RUNNING || !qmi_tilt(im.acc_mg, &pitch, &roll)) {
        return;
    }
    /* Filtro de primer orden (1/4) y saturación a ±30°. */
    pitch = LV_CLAMP(-300, pitch, 300);
    roll = LV_CLAMP(-300, roll, 300);
    f->p_x += (roll - f->p_x) / 4;
    f->p_y += (pitch - f->p_y) / 4;
    const int32_t dx = f->p_x * PAR_MAX / 300, dy = f->p_y * PAR_MAX / 300;
    lv_obj_set_pos(f->p_halo, -dx / 3, -dy / 3);
    lv_obj_align(f->p_card, LV_ALIGN_CENTER, dx / 2, dy / 2);
    lv_obj_align(f->time, LV_ALIGN_CENTER, dx, dy - 18);
    lv_obj_align(f->date, LV_ALIGN_CENTER, dx, dy + PAR_DATE_DY);
}

static void delete_cb(lv_event_t *e)
{
    face_t *f = lv_event_get_user_data(e);
    lv_timer_delete(f->timer);
    if (f->ptimer) {
        lv_timer_delete(f->ptimer);
    }
    if (f->imu_on) {
        watch_imu_set_active(false);
    }
    lv_free(f);
}

static void long_press_cb(lv_event_t *e)
{
    (void)e;
    if (ui_nav_depth() == 1 && !ui_nav_busy() && !ui_qp_active()) {
        ui_face_editor_open(false);
    }
}

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void build_digital(face_t *f, lv_obj_t *c, int32_t time_y)
{
    f->time = watch_theme_label(c, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, time_y);
    f->ampm = watch_theme_label(c, FLEX_FONT_BODY, f->txt2, "");
    lv_obj_add_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);
    f->date = watch_theme_label(c, FLEX_FONT_BODY, f->txt2, "");
    lv_obj_set_width(f->date, FLEX_SCREEN_W - 2 * FLEX_SAFE_X);
    lv_obj_set_style_text_align(f->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(f->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, time_y + 96);
}

static lv_obj_t *mod_tile(face_t *f, lv_obj_t *c, int col, int row, const char *title)
{
    (void)f;
    const int32_t x = (FLEX_SCREEN_W - 2 * MOD_TILE_W - MOD_GAP) / 2 + col * (MOD_TILE_W + MOD_GAP);
    const int32_t y = MOD_TOP + row * (MOD_TILE_H + MOD_GAP);
    lv_obj_t *t = watch_theme_glass_card(c, x, y, MOD_TILE_W, MOD_TILE_H);
    lv_obj_t *l = watch_theme_label(t, FLEX_FONT_SMALL, FLEX_C_TXT2, title);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, -4);
    return t;
}

static lv_obj_t *mod_value(lv_obj_t *tile, const lv_font_t *font, const char *txt)
{
    lv_obj_t *v = watch_theme_label(tile, font, FLEX_C_TXT, txt);
    lv_obj_set_width(v, MOD_TILE_W - 32);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 4);
    return v;
}

static void build_modular(face_t *f, lv_obj_t *c)
{
    f->time = watch_theme_label(c, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, MOD_TIME_Y);
    f->ampm = watch_theme_label(c, FLEX_FONT_BODY, f->txt2, "");
    lv_obj_add_flag(f->ampm, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *d = mod_tile(f, c, 0, 0, "Fecha");
    f->c_wday = watch_theme_label(d, FLEX_FONT_BODY, FLEX_C_ERR, "");
    lv_obj_align(f->c_wday, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    f->c_day = watch_theme_label(d, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    lv_obj_align(f->c_day, LV_ALIGN_BOTTOM_RIGHT, 0, 6);

    lv_obj_t *bt = mod_tile(f, c, 1, 0, "Batería");
    f->c_batt_icon = flex_battery_icon_create(bt, 40, 20);
    lv_obj_align(f->c_batt_icon, LV_ALIGN_TOP_RIGHT, 0, -2);
    f->c_batt = mod_value(bt, FLEX_FONT_LARGE, "--");

    lv_obj_t *al = mod_tile(f, c, 0, 1, "Alarma");
    f->c_alarm = mod_value(al, FLEX_FONT_BODY, "--");

    lv_obj_t *wx = mod_tile(f, c, 1, 1, "Clima");
    f->c_wx = watch_theme_label(wx, FLEX_FONT_TITLE, FLEX_C_TXT, "--");
    lv_obj_align(f->c_wx, LV_ALIGN_LEFT_MID, 0, 6);
    f->c_wx_sub = mod_value(wx, FLEX_FONT_SMALL, "Sin datos");
    lv_obj_set_style_text_color(f->c_wx_sub, flex_color(FLEX_C_TXT2), 0);
}

static void build_glass(face_t *f, lv_obj_t *c)
{
    lv_obj_t *card = watch_theme_glass_card(c, (FLEX_SCREEN_W - GLASS_W) / 2, (FLEX_SCREEN_H - GLASS_H) / 2,
                                            GLASS_W, GLASS_H);
    lv_obj_set_style_radius(card, 48, 0);
    if (watch_theme_wall_is_light((FLEX_SCREEN_W - GLASS_W) / 2, (FLEX_SCREEN_H - GLASS_H) / 2, GLASS_W,
                                  GLASS_H)) {
        watch_theme_glass_set_min_tint(card, FLEX_GLASS_MIN_MIX_LIGHT);
    }
    f->txt = FLEX_C_TXT;
    f->txt2 = FLEX_C_TXT2;
    f->time = watch_theme_label(card, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_CENTER, 0, -20);
    f->date = watch_theme_label(card, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_set_width(f->date, GLASS_W - 40);
    lv_obj_set_style_text_align(f->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(f->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 58);
}

static void build_parallax(face_t *f, lv_obj_t *c, bool interactive)
{
    /* Halo de fondo (capa trasera, se mueve poco y al revés) */
    lv_obj_t *halo = lv_obj_create(c);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_remove_flag(halo, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    static const int32_t kR[3] = {190, 140, 90};
    static const lv_opa_t kO[3] = {40, 60, 90};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = lv_obj_create(halo);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 2 * kR[i], 2 * kR[i]);
        lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(r, 2, 0);
        lv_obj_set_style_border_color(r, flex_color(FLEX_C_ACCENT), 0);
        lv_obj_set_style_border_opa(r, kO[i], 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(r);
    }
    f->p_halo = halo;
    /* Tarjeta de vidrio intermedia */
    f->p_card = watch_theme_glass_card(c, 0, 0, 300, 190);
    lv_obj_set_style_radius(f->p_card, 40, 0);
    lv_obj_align(f->p_card, LV_ALIGN_CENTER, 0, 0);
    f->txt = FLEX_C_TXT;
    f->txt2 = FLEX_C_TXT2;
    /* Hora (capa delantera) como hija de la página: se mueve más */
    f->time = watch_theme_label(c, FLEX_FONT_CLOCK, FLEX_C_MUTE, "--:--");
    lv_obj_align(f->time, LV_ALIGN_CENTER, 0, -18);
    f->date = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_set_width(f->date, 260);
    lv_obj_set_style_text_align(f->date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(f->date, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(f->date, LV_ALIGN_CENTER, 0, PAR_DATE_DY);
    if (interactive) {
        f->ptimer = lv_timer_create(parallax_cb, PARALLAX_TICK_MS, f);
    }
}

lv_obj_t *ui_face_create_style(lv_obj_t *parent, int style, bool interactive)
{
    watch_theme_init();
    face_t *f = lv_malloc_zeroed(sizeof(*f));
    LV_ASSERT_MALLOC(f);
    f->style = (style >= 0 && style < UI_FACE_COUNT_) ? style : UI_FACE_DIGITAL;
    f->minimal = interactive && ui_psave_active();
    f->last_key = INT64_MIN;
    f->last_sec = -1;
    f->last_pct = -2;
    f->wx_gen = UINT32_MAX;

    lv_obj_t *scr = ui_page_new(parent);
    f->page = scr;
    if (f->minimal) {
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    } else {
        watch_theme_apply_background(scr);
    }
    /* Sobre un fondo claro el texto pasa a oscuro (onColor() de la referencia). */
    const bool light_time =
        !f->minimal && watch_theme_wall_is_light(FLEX_SAFE_X, TIME_Y, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 86);
    const bool light_date =
        !f->minimal && watch_theme_wall_is_light(FLEX_SAFE_X, DATE_Y, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 28);
    f->txt = light_time ? FLEX_C_ON_LIGHT : FLEX_C_TXT;
    f->txt2 = light_date ? FLEX_C_ON_LIGHT2 : FLEX_C_TXT2;

    /* Todo lo fijo va en un contenedor que mueve el Pixel Shift. */
    lv_obj_t *c = box(scr);
    f->shift = c;
    if (f->minimal) {
        build_digital(f, c, TIME_Y);
    } else {
        switch (f->style) {
        case UI_FACE_ANALOG:
            f->dial = ui_analog_create(c, ANALOG_D, false);
            lv_obj_align(f->dial, LV_ALIGN_CENTER, 0, -12);
            f->date = watch_theme_label(c, FLEX_FONT_SMALL, f->txt2, "");
            lv_obj_align(f->date, LV_ALIGN_BOTTOM_MID, 0, -40);
            break;
        case UI_FACE_MODULAR:
            build_modular(f, c);
            break;
        case UI_FACE_GLASS:
            build_glass(f, c);
            break;
        case UI_FACE_PARALLAX:
            build_parallax(f, c, interactive);
            f->shift = NULL; /* ya se mueve con la muñeca */
            break;
        default:
            build_digital(f, c, TIME_Y);
            break;
        }
    }

    if (interactive) {
        /* Arrastrar hacia abajo desde arriba abre el panel rápido; deslizar
         * hacia abajo en el resto, el cajón; mantener pulsado, el selector. */
        ui_qp_attach(scr);
        ui_apps_attach(scr);
        lv_obj_add_event_cb(scr, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
    f->timer = lv_timer_create(tick_cb, FACE_TICK_MS, f);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, f);
    update_time(f);
    lv_obj_set_user_data(scr, f);
    return scr;
}

lv_obj_t *ui_face_create(lv_obj_t *parent)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    return ui_face_create_style(parent, ws.face, true);
}
