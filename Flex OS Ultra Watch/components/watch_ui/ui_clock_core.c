/*
 * ui_clock_core — estado y motor de las apps de tiempo (F4.3) y el aviso a
 * pantalla completa cuando suenan.
 *
 *  - El cronómetro y el temporizador viven aquí (no en sus páginas): siguen
 *    contando aunque se salga de la app. Solo en RAM, como la referencia.
 *  - Las alarmas se guardan con watch_settings (bloque con versión y CRC).
 *  - ui_clock_tick() corre en el tick de 100 ms de ui_system (hilo de LVGL):
 *    comprueba el fin del temporizador y, una vez por minuto LOCAL, las
 *    alarmas. Una alarma "una vez" se desactiva al sonar. Posponer = 5 min.
 *    Si el reloj estaba apagado a la hora de una alarma, no suena después.
 *  - Aviso SOLO visual (D13: altavoz y motor sin decidir): pantalla negra
 *    con icono que late, la hora y dos botones. Se cierra solo a los 3 min.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define RING_MAX_MS  (3u * 60u * 1000u)
#define RING_ICON    88
#define RING_BTN_W   160
#define RING_BTN_H   60

static sw_t s_sw;
static tmr_t s_tmr;
static alarm_set_t s_al;
static bool s_inited;
static int64_t s_last_min = INT64_MIN;
static int s_snooze_idx = -1;
static int64_t s_snooze_at = -1; /* segundos locales */
static uint32_t s_al_gen;

typedef enum { RING_NONE = 0, RING_ALARM, RING_TIMER } ring_kind_t;

static struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *big;
    lv_obj_t *left;
    lv_obj_t *right;
    lv_timer_t *timeout;
    ring_kind_t kind;
    int alarm_idx;
} s_ring;

int64_t ui_mono_us(void)
{
    return esp_timer_get_time();
}

/* ---- Estado ---- */

sw_t *ui_sw(void)
{
    return &s_sw;
}

tmr_t *ui_tmr(void)
{
    return &s_tmr;
}

const alarm_set_t *ui_alarms(void)
{
    return &s_al;
}

uint32_t ui_alarms_gen(void)
{
    return s_al_gen;
}

void ui_alarms_set(const alarm_set_t *set)
{
    s_al_gen++;
    s_al = *set;
    if (s_al.n > AL_MAX) {
        s_al.n = AL_MAX;
    }
    uint8_t buf[AL_MAX * 4];
    const size_t n = alarm_pack(&s_al, buf, sizeof(buf));
    watch_settings_blob_set(WS_BLOB_ALARMS, AL_BLOB_VER, buf, n);
    /* Si se toca la alarma pospuesta, se olvida el aplazamiento. */
    s_snooze_idx = -1;
    s_snooze_at = -1;
}

bool ui_clock_local_now(int64_t *local_s)
{
    wt_datetime_t dt;
    if (watch_time_now(&dt) != WATCH_TIME_OK) {
        return false;
    }
    *local_s = wt_to_seconds(&dt);
    return true;
}

void ui_clock_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    sw_reset(&s_sw);
    memset(&s_tmr, 0, sizeof(s_tmr));
    uint8_t buf[AL_MAX * 4];
    uint8_t ver = 0;
    size_t n = sizeof(buf);
    if (watch_settings_blob_get(WS_BLOB_ALARMS, &ver, buf, &n) != ESP_OK || ver != AL_BLOB_VER ||
        !alarm_unpack(buf, n, &s_al)) {
        memset(&s_al, 0, sizeof(s_al)); /* sin alarmas guardadas (o dañadas: no se usan) */
    }
}

/* ---- Aviso ---- */

static void ring_close(void)
{
    if (!s_ring.root) {
        return;
    }
    if (s_ring.timeout) {
        lv_timer_delete(s_ring.timeout);
        s_ring.timeout = NULL;
    }
    lv_anim_delete(s_ring.icon, NULL);
    lv_obj_delete_async(s_ring.root);
    memset(&s_ring, 0, sizeof(s_ring));
}

static void left_cb(lv_event_t *e)
{
    (void)e;
    if (s_ring.kind == RING_ALARM) {
        int64_t now;
        if (ui_clock_local_now(&now)) {
            s_snooze_idx = s_ring.alarm_idx;
            s_snooze_at = (now / 60) * 60 + AL_SNOOZE_S;
        }
    } else if (s_ring.kind == RING_TIMER) {
        tmr_add(&s_tmr, 60LL * 1000000, ui_mono_us());
    }
    ring_close();
}

static void right_cb(lv_event_t *e)
{
    (void)e;
    if (s_ring.kind == RING_TIMER) {
        tmr_cancel(&s_tmr);
    }
    ring_close();
}

static void timeout_cb(lv_timer_t *t)
{
    (void)t;
    if (s_ring.kind == RING_TIMER) {
        tmr_cancel(&s_tmr);
    }
    s_ring.timeout = NULL; /* se borra solo (repeat 1) */
    ring_close();
}

static void pulse_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void ring_show(ring_kind_t kind, int alarm_idx, const char *big)
{
    if (s_ring.root) {
        ring_close(); /* un aviso nuevo sustituye al anterior */
    }
    watch_theme_init();
    lv_obj_t *root = lv_obj_create(ui_overlay_parent());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_bg_color(root, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE); /* no deja pasar toques a lo de debajo */
    s_ring.root = root;
    s_ring.kind = kind;
    s_ring.alarm_idx = alarm_idx;

    const bool alarm = kind == RING_ALARM;
    s_ring.icon = flex_icon_create(root, alarm ? FLEX_ICON_BELL : FLEX_ICON_HOURGLASS, RING_ICON,
                                   alarm ? FLEX_C_WARN : FLEX_C_ACCENT);
    lv_obj_align(s_ring.icon, LV_ALIGN_TOP_MID, 0, 48);
    s_ring.title = watch_theme_label(root, FLEX_FONT_TITLE, FLEX_C_TXT2, alarm ? "Alarma" : "Tiempo agotado");
    lv_obj_align(s_ring.title, LV_ALIGN_TOP_MID, 0, 48 + RING_ICON + 12);
    s_ring.big = watch_theme_label(root, FLEX_FONT_CLOCK, FLEX_C_TXT, big);
    lv_obj_align(s_ring.big, LV_ALIGN_TOP_MID, 0, 48 + RING_ICON + 12 + 40);

    s_ring.left = flex_glass_button(root, alarm ? "Posponer" : "+1 min", FLEX_GLASS_TINT_NONE, RING_BTN_W, RING_BTN_H);
    lv_obj_align(s_ring.left, LV_ALIGN_BOTTOM_MID, -(RING_BTN_W / 2 + 8), -48);
    lv_obj_add_event_cb(s_ring.left, left_cb, LV_EVENT_CLICKED, NULL);
    s_ring.right = flex_glass_button(root, "Detener", FLEX_C_DANGER, RING_BTN_W, RING_BTN_H);
    lv_obj_align(s_ring.right, LV_ALIGN_BOTTOM_MID, RING_BTN_W / 2 + 8, -48);
    lv_obj_add_event_cb(s_ring.right, right_cb, LV_EVENT_CLICKED, NULL);

    /* El icono late (solo él: zona pequeña, sin capas). */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ring.icon);
    lv_anim_set_exec_cb(&a, pulse_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_reverse_duration(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    s_ring.timeout = lv_timer_create(timeout_cb, RING_MAX_MS, NULL);
    lv_timer_set_repeat_count(s_ring.timeout, 1);
    ui_screen_wake(); /* F4.5: enciende la pantalla si estaba apagada */
}

bool ui_ring_active(void)
{
    return s_ring.root != NULL;
}

void ui_ring_dismiss(void)
{
    ring_close();
}

lv_obj_t *ui_ring_obj(void)
{
    return s_ring.root;
}

/* ---- Motor (cada 100 ms) ---- */

static void fmt_alarm_time(const alarm_t *al, char *buf, size_t n)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    const wt_datetime_t dt = {2026, 1, 1, al->hour, al->minute, 0, 4};
    if (ws.h24) {
        wt_fmt_hhmm(buf, n, &dt);
    } else {
        wt_fmt_hhmm12(buf, n, &dt);
    }
}

void ui_clock_tick(void)
{
    if (!s_inited) {
        return;
    }
    if (tmr_check(&s_tmr, ui_mono_us())) {
        ring_show(RING_TIMER, -1, "00:00");
    }
    int64_t now;
    if (!ui_clock_local_now(&now)) {
        return;
    }
    const int64_t min = now / 60;
    if (min == s_last_min) {
        return;
    }
    s_last_min = min;
    if (s_snooze_at >= 0 && now >= s_snooze_at) {
        const int idx = s_snooze_idx;
        s_snooze_idx = -1;
        s_snooze_at = -1;
        if (idx >= 0 && idx < s_al.n) {
            char t[8];
            fmt_alarm_time(&s_al.a[idx], t, sizeof(t));
            ring_show(RING_ALARM, idx, t);
            return;
        }
    }
    wt_datetime_t dt;
    wt_from_seconds(now, &dt);
    for (int i = 0; i < s_al.n; i++) {
        if (alarm_matches(&s_al.a[i], &dt)) {
            char t[8];
            fmt_alarm_time(&s_al.a[i], t, sizeof(t));
            if (s_al.a[i].days == 0) {
                alarm_set_t set = s_al;
                set.a[i].on = 0; /* una sola vez: se desactiva al sonar */
                ui_alarms_set(&set);
            }
            ring_show(RING_ALARM, i, t);
            return;
        }
    }
}
