/*
 * ui_battery_page — Ajustes > Batería: estado real, umbrales de aviso
 * (provisionales) y configuración del cargador en SOLO LECTURA.
 *
 * Todo sale de la instantánea de watch_power (caché de ui_system); lo que no
 * se conoce se muestra "--". Los umbrales vienen de battery_thresholds.h y se
 * marcan como provisionales mientras BATT_THRESHOLDS_CALIBRATED sea 0.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "battery_thresholds.h"
#include "ui_logic.h"
#include "watch_theme.h"

typedef struct {
    lv_obj_t *level;
    lv_obj_t *state;
    lv_obj_t *volts;
    lv_obj_t *usb;
    lv_obj_t *vindpm;
    lv_obj_t *charge;
    lv_obj_t *cv;
    lv_obj_t *term;
    lv_obj_t *prechg;
    lv_obj_t *in_lim;
    lv_obj_t *vindpm_th;
    lv_obj_t *ts;
} battpage_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static const char *yes_no(bool known, bool v)
{
    return known ? (v ? "Sí" : "No") : "--";
}

static void refresh(battpage_t *b)
{
    char buf[32];
    watch_power_snapshot_t s;
    const bool have = ui_system_power(&s);
    const bool chip = have && s.driver_ok;

    if (have && s.track.percent >= 0) {
        snprintf(buf, sizeof(buf), "%d %%", s.track.percent);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_set(b->level, buf);
    watch_theme_info_set(b->state, ui_batt_state_text(have ? s.track.state : BATT_STATE_UNKNOWN));
    ui_fmt_volts(buf, sizeof(buf), have ? s.track.vbat_mv : -1);
    watch_theme_info_set(b->volts, buf);
    watch_theme_info_set(b->usb, chip ? (s.raw.vbus_present ? "Conectado" : "No") : "--");
    watch_theme_info_set(b->vindpm, yes_no(chip, s.raw.vindpm));

    const bool cfg = chip && s.cfg_ok;
    ui_fmt_ma(buf, sizeof(buf), cfg ? s.cfg.charge_ma : -1);
    watch_theme_info_set(b->charge, buf);
    ui_fmt_volts(buf, sizeof(buf), cfg ? s.cfg.cv_mv : -1);
    watch_theme_info_set(b->cv, buf);
    if (cfg && s.cfg.term_ma >= 0) {
        snprintf(buf, sizeof(buf), "%d mA%s", s.cfg.term_ma, s.cfg.term_enabled ? "" : " (desactivada)");
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_set(b->term, buf);
    ui_fmt_ma(buf, sizeof(buf), cfg ? s.cfg.precharge_ma : -1);
    watch_theme_info_set(b->prechg, buf);
    ui_fmt_ma(buf, sizeof(buf), cfg ? s.cfg.input_limit_ma : -1);
    watch_theme_info_set(b->in_lim, buf);
    ui_fmt_volts(buf, sizeof(buf), cfg ? s.cfg.vindpm_mv : -1);
    watch_theme_info_set(b->vindpm_th, buf);
    watch_theme_info_set(b->ts, cfg ? (s.cfg.ts_affects_charger ? "Afecta a la carga" : "No afecta") : "--");
}

/* Cambio de batería: llega de ui_system en cuanto se confirma. */
static void power_cb(lv_obj_t *page, const watch_power_snapshot_t *snap)
{
    (void)snap;
    refresh(lv_obj_get_user_data(page));
}

static void delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static lv_obj_t *note(lv_obj_t *list, const char *text)
{
    lv_obj_t *n = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE, text);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(n, LV_PCT(100));
    lv_obj_set_style_pad_left(n, 6, 0);
    return n;
}

lv_obj_t *ui_battery_page_create(lv_obj_t *parent)
{
    battpage_t *b = lv_malloc_zeroed(sizeof(*b));
    LV_ASSERT_MALLOC(b);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Batería", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    char buf[40];

    watch_theme_section(list, "Estado");
    b->level = watch_theme_info_row(list, "Nivel", "--");
    b->state = watch_theme_info_row(list, "Estado", "--");
    b->volts = watch_theme_info_row(list, "Tensión", "--");
    b->usb = watch_theme_info_row(list, "USB", "--");
    b->vindpm = watch_theme_info_row(list, "Entrada limitada (VINDPM)", "--");

    watch_theme_section(list, BATT_THRESHOLDS_CALIBRATED ? "Avisos" : "Avisos (umbrales provisionales)");
    /* Sin "<=" ni ">=": las fuentes Outfit generadas solo traen Latin-1. */
    snprintf(buf, sizeof(buf), "%d %% (rearme %d %%)", BATT_LOW_ENTER_PCT, BATT_LOW_EXIT_PCT);
    watch_theme_info_row(list, "Batería baja", buf);
    snprintf(buf, sizeof(buf), "%d %% (rearme %d %%)", BATT_CRITICAL_ENTER_PCT, BATT_CRITICAL_EXIT_PCT);
    watch_theme_info_row(list, "Crítica", buf);
    if (!BATT_THRESHOLDS_CALIBRATED) {
        note(list, "Sin calibrar con la batería real (decisión D4).");
    }

    watch_theme_section(list, "Cargador (solo lectura)");
    b->charge = watch_theme_info_row(list, "Corriente de carga", "--");
    b->cv = watch_theme_info_row(list, "Tensión de carga", "--");
    b->term = watch_theme_info_row(list, "Fin de carga", "--");
    b->prechg = watch_theme_info_row(list, "Precarga", "--");
    b->in_lim = watch_theme_info_row(list, "Límite de entrada", "--");
    b->vindpm_th = watch_theme_info_row(list, "Umbral VINDPM", "--");
    b->ts = watch_theme_info_row(list, "Pin TS", "--");
    note(list, "Este firmware no escribe ningún registro del AXP2101.");

    refresh(b);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, b);
    lv_obj_set_user_data(page, b);
    ui_power_listen(page, power_cb);
    return page;
}
