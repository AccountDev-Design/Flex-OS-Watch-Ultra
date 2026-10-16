/*
 * ui_datetime — Ajustes > Fecha y hora (F4.2):
 *  - "Poner en hora": rodillos de hora, minuto, día, mes y año (hora LOCAL).
 *    Al guardar se convierte a UTC con la zona y se pide al servicio de hora,
 *    que escribe el RTC, lo relee y comprueba que avanza. La página muestra
 *    el resultado real (watch_time_info().set_result): nunca da por guardada
 *    una hora que el RTC no confirmó.
 *  - Formato de 24 horas (ajuste persistente; la esfera grande no lleva
 *    AM/PM, la barra del panel rápido sí).
 *  - Zona horaria: desfase fijo de UTC-12 a UTC+14 (D17). Cambiarla no
 *    escribe el RTC.
 *  - Sincronización por Internet: F5 (Wi-Fi), marcada "Pendiente".
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define DT_TICK_MS   1000
#define SET_POLL_MS  100
#define SET_BACK_MS  900     /* tras confirmar, vuelve sola */
#define YEAR_FIRST   2025
#define YEAR_LAST    2099

static const char *const MONTHS = "ene\nfeb\nmar\nabr\nmay\njun\njul\nago\nsept\noct\nnov\ndic";

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static const char *set_result_text(wt_set_result_t r)
{
    switch (r) {
    case WT_SET_PENDING:    return "Guardando en el reloj…";
    case WT_SET_OK:         return "Guardada y comprobada";
    case WT_SET_ERR_I2C:    return "El RTC no responde (reintenta)";
    case WT_SET_ERR_VERIFY: return "No se confirmó (reintenta)";
    case WT_SET_ERR_OSC:    return "El RTC no avanza";
    default:                return "--";
    }
}

/* ======================= Fecha y hora (lista) ======================= */

typedef struct {
    lv_obj_t *set;
    lv_obj_t *date;
    lv_obj_t *h24;
    lv_obj_t *tz;
    lv_obj_t *rtc;
    lv_obj_t *last;
} dtp_t;

static void dt_refresh(dtp_t *d)
{
    char buf[40];
    watch_settings_t ws;
    watch_settings_get(&ws);
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    if (st == WATCH_TIME_OK) {
        if (ws.h24) {
            wt_fmt_hhmm(buf, sizeof(buf), &dt);
        } else {
            char h[8];
            wt_fmt_hhmm12(h, sizeof(h), &dt);
            snprintf(buf, sizeof(buf), "%s %s", h, wt_ampm(&dt));
        }
        watch_theme_row_set_value(d->set, buf);
        wt_fmt_date_short(buf, sizeof(buf), &dt);
        watch_theme_info_set(d->date, buf);
    } else {
        watch_theme_row_set_value(d->set, "Sin configurar");
        watch_theme_info_set(d->date, "--");
    }
    watch_theme_row_set_value(d->h24, ws.h24 ? "Sí" : "No");
    wt_fmt_utc_offset(buf, sizeof(buf), ws.tz_min);
    watch_theme_row_set_value(d->tz, buf);

    watch_time_info_t ti;
    watch_time_info(&ti);
    watch_theme_info_set(d->rtc, ui_time_state_text(ti.state));
    watch_theme_info_set(d->last, set_result_text(ti.set_result));
}

static void dt_tick_cb(lv_timer_t *t)
{
    dt_refresh(lv_timer_get_user_data(t));
}

static void open_set_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_timeset_create);
}

static void open_tz_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_tz_create);
}

static void h24_cb(lv_event_t *e)
{
    dtp_t *d = lv_event_get_user_data(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.h24 = !ws.h24;
    watch_settings_update(&ws);
    dt_refresh(d);
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static lv_obj_t *note(lv_obj_t *list, const char *text)
{
    lv_obj_t *n = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE, text);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(n, LV_PCT(100));
    lv_obj_set_style_pad_left(n, 16, 0);
    return n;
}

lv_obj_t *ui_datetime_create(lv_obj_t *parent)
{
    dtp_t *d = lv_malloc_zeroed(sizeof(*d));
    LV_ASSERT_MALLOC(d);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Fecha y hora", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Hora del reloj");
    d->set = watch_theme_list_row(list, FLEX_ICON_CLOCK, FLEX_C_CAT_BLUE, "Poner en hora", "", true);
    lv_obj_add_event_cb(d->set, open_set_cb, LV_EVENT_CLICKED, NULL);
    d->h24 = watch_theme_list_row(list, FLEX_ICON_WATCH, FLEX_C_CAT_BLUE, "Formato 24 h", "", false);
    lv_obj_add_event_cb(d->h24, h24_cb, LV_EVENT_CLICKED, d);
    d->tz = watch_theme_list_row(list, FLEX_ICON_GLOBE, FLEX_C_CAT_NET, "Zona horaria", "", true);
    lv_obj_add_event_cb(d->tz, open_tz_cb, LV_EVENT_CLICKED, NULL);
    note(list, "Sin horario de verano: elige el desfase que use tu ciudad.");

    watch_theme_section(list, "Sincronización");
    lv_obj_t *sntp = watch_theme_list_row(list, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Por Internet", NULL, false);
    watch_theme_row_set_pending(sntp);

    watch_theme_section(list, "Reloj (PCF85063)");
    d->date = watch_theme_info_row(list, "Fecha", "--");
    d->rtc = watch_theme_info_row(list, "Hora", "--");
    d->last = watch_theme_info_row(list, "Puesta en hora", "--");

    dt_refresh(d);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, d);
    ui_page_timer(page, dt_tick_cb, DT_TICK_MS, d);
    return page;
}

/* ======================= Zona horaria ======================= */

static void tz_pick_cb(lv_event_t *e)
{
    const int off = (int)(intptr_t)lv_event_get_user_data(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.tz_min = (int16_t)off;
    watch_settings_update(&ws);
    watch_time_set_tz(ws.tz_min);
    ui_nav_back();
}

lv_obj_t *ui_tz_create(lv_obj_t *parent)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Zona horaria", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    lv_obj_t *cur = NULL;
    for (size_t i = 0; i < ui_tz_count; i++) {
        char t[16];
        wt_fmt_utc_offset(t, sizeof(t), ui_tz_table[i].off_min);
        const bool sel = ui_tz_table[i].off_min == ws.tz_min;
        lv_obj_t *r = watch_theme_list_row(list, sel ? FLEX_ICON_CHECK : FLEX_ICON_GLOBE,
                                           sel ? FLEX_C_ACCENT : FLEX_C_MUTE, t, ui_tz_table[i].where, false);
        lv_obj_add_event_cb(r, tz_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ui_tz_table[i].off_min);
        if (sel) {
            cur = r;
        }
    }
    if (cur) {
        lv_obj_update_layout(page);
        lv_obj_scroll_to_view(cur, LV_ANIM_OFF);
    }
    return page;
}

/* ======================= Poner en hora ======================= */

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *min;
    lv_obj_t *day;
    lv_obj_t *month;
    lv_obj_t *year;
    lv_obj_t *status;
    lv_obj_t *btn;
    lv_obj_t *btn_label;
    lv_timer_t *poll;
    uint32_t ok_ms; /* cuándo se confirmó (0 = aún no) */
    char days_opts[31 * 3 + 1];
    int days_n;
} ts_t;

static void style_roller(lv_obj_t *r, const lv_font_t *font)
{
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_text_font(r, font, 0);
    lv_obj_set_style_text_color(r, flex_color(FLEX_C_MUTE), 0);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(r, 12, 0);
    lv_obj_set_style_anim_duration(r, 160, 0);
    lv_obj_set_style_bg_color(r, flex_color(FLEX_C_SURF), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_radius(r, 12, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, flex_color(FLEX_C_TXT), LV_PART_SELECTED);
    lv_obj_set_style_text_font(r, font, LV_PART_SELECTED);
}

lv_obj_t *ui_roller(lv_obj_t *page, const char *opts, lv_roller_mode_t mode, const lv_font_t *font, int32_t w)
{
    lv_obj_t *r = lv_roller_create(page);
    style_roller(r, font);
    lv_roller_set_options(r, opts, mode);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, w);
    return r;
}

/* Días del mes elegido; el día elegido se recorta si ya no existe. */
static void update_days(ts_t *t)
{
    const int y = YEAR_FIRST + (int)lv_roller_get_selected(t->year);
    const int m = 1 + (int)lv_roller_get_selected(t->month);
    const int n = wt_days_in_month(y, m);
    if (n == t->days_n) {
        return;
    }
    const int sel = (int)lv_roller_get_selected(t->day);
    char *p = t->days_opts;
    for (int d = 1; d <= n; d++) {
        p += snprintf(p, sizeof(t->days_opts) - (size_t)(p - t->days_opts), d < n ? "%d\n" : "%d", d);
    }
    t->days_n = n;
    lv_roller_set_options(t->day, t->days_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(t->day, (uint32_t)(sel < n ? sel : n - 1), LV_ANIM_OFF);
}

static void date_changed_cb(lv_event_t *e)
{
    update_days(lv_event_get_user_data(e));
}

static void set_status(ts_t *t, const char *text, uint32_t color)
{
    lv_label_set_text(t->status, text);
    lv_obj_set_style_text_color(t->status, flex_color(color), 0);
}

static void poll_cb(lv_timer_t *tm)
{
    ts_t *t = lv_timer_get_user_data(tm);
    if (t->ok_ms) {
        if (lv_tick_elaps(t->ok_ms) >= SET_BACK_MS) {
            lv_timer_pause(tm);
            ui_nav_back();
        }
        return;
    }
    watch_time_info_t ti;
    watch_time_info(&ti);
    switch (ti.set_result) {
    case WT_SET_OK:
        set_status(t, "Hora guardada y comprobada", FLEX_C_OK);
        lv_label_set_text(t->btn_label, "Guardada");
        t->ok_ms = lv_tick_get();
        break;
    case WT_SET_PENDING:
        break;
    default:
        set_status(t, set_result_text(ti.set_result), FLEX_C_ERR);
        lv_label_set_text(t->btn_label, "Reintentar");
        lv_obj_remove_state(t->btn, LV_STATE_DISABLED);
        lv_timer_pause(tm);
        break;
    }
}

static void save_cb(lv_event_t *e)
{
    ts_t *t = lv_event_get_user_data(e);
    wt_datetime_t dt = {0};
    dt.year = YEAR_FIRST + (int)lv_roller_get_selected(t->year);
    dt.month = 1 + (int)lv_roller_get_selected(t->month);
    dt.day = 1 + (int)lv_roller_get_selected(t->day);
    dt.hour = (int)lv_roller_get_selected(t->hour);
    dt.minute = (int)lv_roller_get_selected(t->min);
    const esp_err_t err = watch_time_set_utc(wt_local_to_utc(&dt, watch_time_get_tz()));
    if (err == ESP_ERR_INVALID_ARG) {
        set_status(t, "Fecha fuera de rango (2025-2099)", FLEX_C_ERR);
        return;
    }
    if (err != ESP_OK) {
        set_status(t, "Servicio de hora no disponible", FLEX_C_ERR);
        return;
    }
    set_status(t, "Guardando en el reloj…", FLEX_C_TXT2);
    lv_label_set_text(t->btn_label, "Guardando…");
    lv_obj_add_state(t->btn, LV_STATE_DISABLED);
    t->ok_ms = 0;
    lv_timer_resume(t->poll);
    lv_timer_ready(t->poll);
}

/* Fecha de compilación del firmware: punto de partida razonable cuando el
 * reloj no tiene hora (no se muestra como hora real: solo coloca los rodillos). */
static void build_date(wt_datetime_t *dt)
{
    static const char *const mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__; /* "Oct 16 2026" */
    char m[4] = {d[0], d[1], d[2], 0};
    const char *f = strstr(mon, m);
    dt->month = f ? (int)(f - mon) / 3 + 1 : 1;
    dt->day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
    dt->year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
    dt->hour = 12;
    dt->minute = 0;
}

static void ts_delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_timeset_create(lv_obj_t *parent)
{
    ts_t *t = lv_malloc_zeroed(sizeof(*t));
    LV_ASSERT_MALLOC(t);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Poner en hora", back_cb, NULL);

    static char hours[24 * 3], mins[60 * 3], years[(YEAR_LAST - YEAR_FIRST + 1) * 5];
    if (!hours[0]) {
        char *p = hours;
        for (int i = 0; i < 24; i++) {
            p += sprintf(p, i < 23 ? "%02d\n" : "%02d", i);
        }
        p = mins;
        for (int i = 0; i < 60; i++) {
            p += sprintf(p, i < 59 ? "%02d\n" : "%02d", i);
        }
        p = years;
        for (int y = YEAR_FIRST; y <= YEAR_LAST; y++) {
            p += sprintf(p, y < YEAR_LAST ? "%d\n" : "%d", y);
        }
    }

    /* Hora: [HH] : [MM]; debajo, fecha: [día] [mes] [año] */
    t->hour = ui_roller(page, hours, LV_ROLLER_MODE_INFINITE, FLEX_FONT_TITLE, 96);
    t->min = ui_roller(page, mins, LV_ROLLER_MODE_INFINITE, FLEX_FONT_TITLE, 96);
    t->day = ui_roller(page, "1", LV_ROLLER_MODE_NORMAL, FLEX_FONT_BODY, 76);
    t->month = ui_roller(page, MONTHS, LV_ROLLER_MODE_NORMAL, FLEX_FONT_BODY, 100);
    t->year = ui_roller(page, years, LV_ROLLER_MODE_NORMAL, FLEX_FONT_BODY, 100);
    lv_obj_update_layout(page);
    const int32_t y_time = FLEX_HDR_H + 6;
    const int32_t h_time = lv_obj_get_height(t->hour);
    lv_obj_align(t->hour, LV_ALIGN_TOP_MID, -62, y_time);
    lv_obj_align(t->min, LV_ALIGN_TOP_MID, 62, y_time);
    lv_obj_t *colon = watch_theme_label(page, FLEX_FONT_TITLE, FLEX_C_TXT, ":");
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, y_time + (h_time - lv_font_get_line_height(FLEX_FONT_TITLE)) / 2);
    const int32_t y_date = y_time + h_time + 14;
    lv_obj_align(t->day, LV_ALIGN_TOP_MID, -100, y_date);
    lv_obj_align(t->month, LV_ALIGN_TOP_MID, 0, y_date);
    lv_obj_align(t->year, LV_ALIGN_TOP_MID, 112, y_date);

    /* Valores iniciales: la hora actual o, sin hora, la fecha del firmware. */
    wt_datetime_t now;
    if (watch_time_now(&now) != WATCH_TIME_OK) {
        build_date(&now);
    }
    if (now.year < YEAR_FIRST) {
        now.year = YEAR_FIRST;
    } else if (now.year > YEAR_LAST) {
        now.year = YEAR_LAST;
    }
    lv_roller_set_selected(t->hour, (uint32_t)now.hour, LV_ANIM_OFF);
    lv_roller_set_selected(t->min, (uint32_t)now.minute, LV_ANIM_OFF);
    lv_roller_set_selected(t->month, (uint32_t)(now.month - 1), LV_ANIM_OFF);
    lv_roller_set_selected(t->year, (uint32_t)(now.year - YEAR_FIRST), LV_ANIM_OFF);
    update_days(t);
    lv_roller_set_selected(t->day, (uint32_t)(now.day - 1), LV_ANIM_OFF);
    lv_obj_add_event_cb(t->month, date_changed_cb, LV_EVENT_VALUE_CHANGED, t);
    lv_obj_add_event_cb(t->year, date_changed_cb, LV_EVENT_VALUE_CHANGED, t);

    /* Estado y botón */
    lv_obj_update_layout(page);
    const int32_t y_status = lv_obj_get_y2(t->day) + 8;
    t->status = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, "Hora local de la zona elegida");
    lv_obj_set_width(t->status, 300);
    lv_obj_set_style_text_align(t->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t->status, LV_ALIGN_TOP_MID, 0, y_status);

    t->btn = watch_theme_tile(page, (FLEX_SCREEN_W - 200) / 2, y_status + 32, 200, 56);
    lv_obj_set_style_radius(t->btn, 28, 0);
    lv_obj_set_style_pad_all(t->btn, 0, 0);
    lv_obj_set_style_bg_color(t->btn, flex_color(FLEX_C_PRIMARY), 0);
    t->btn_label = watch_theme_label(t->btn, FLEX_FONT_BODY, FLEX_C_WHITE, "Guardar");
    lv_obj_center(t->btn_label);
    lv_obj_add_event_cb(t->btn, save_cb, LV_EVENT_CLICKED, t);

    /* El temporizador se borra con la página. */
    t->poll = ui_page_timer(page, poll_cb, SET_POLL_MS, t);
    lv_timer_pause(t->poll);
    lv_obj_add_event_cb(page, ts_delete_cb, LV_EVENT_DELETE, t);
    return page;
}
