/*
 * watch_home — pantalla de inicio de B1. Ver include/watch_ui.h
 *
 * Todo se ejecuta en la tarea de LVGL (creación con el bloqueo tomado y
 * actualización en un lv_timer). Los datos de batería se copian de la
 * instantánea de watch_power, que nunca espera al bus I2C.
 */
#include "watch_ui.h"

#include <stdio.h>
#include <string.h>
#include "watch_power.h"
#include "watch_theme.h"

#define REFRESH_MS      500
#define MEM_EVERY_TICKS 4      /* memoria cada 2 s */

/* Geometría (px del panel de 410x502) */
#define CARD_X      20
#define CARD_W      (FLEX_SCREEN_W - 2 * CARD_X)
#define BATT_CARD_Y 186
#define BATT_CARD_H 136
#define SYS_CARD_Y  334
#define SYS_CARD_H  110
#define CARD_PAD    18
#define TEXT_W      (CARD_W - 2 * CARD_PAD)
#define ICON_W      34
#define ICON_H      17

typedef struct {
    lv_obj_t *status_pct;
    lv_obj_t *icon_fill;
    lv_obj_t *icon_body;
    lv_obj_t *clock;
    lv_obj_t *batt_pct;
    lv_obj_t *batt_state;
    lv_obj_t *batt_detail;
    lv_obj_t *sys_line1;
    lv_obj_t *sys_line2;
    lv_obj_t *sys_line3;
    lv_obj_t *touch_dot;
    lv_point_t touch;
    bool touch_seen;
    bool touch_ok;
    bool power_ok;
    watch_flash_report_t flash;
    uint32_t ticks;
    /* último estado pintado, para no reescribir etiquetas sin cambios */
    int last_pct;
    int last_state;
    int last_level;
    int last_vbat;
    int last_vbus;
} home_t;

static home_t s_home;

static const char *state_text(batt_state_t st)
{
    switch (st) {
    case BATT_STATE_NO_BATTERY:    return "Sin batería";
    case BATT_STATE_DISCHARGING:   return "Descargando";
    case BATT_STATE_CHARGING:      return "Cargando";
    case BATT_STATE_FULL:          return "Carga completa";
    case BATT_STATE_EXTERNAL_IDLE: return "Externa sin carga";
    default:                       return "Sin datos";
    }
}

static uint32_t state_color(batt_state_t st, batt_level_t lv)
{
    if (st == BATT_STATE_CHARGING || st == BATT_STATE_FULL) {
        return FLEX_C_OK;
    }
    if (lv == BATT_LEVEL_CRITICAL) {
        return FLEX_C_ERR;
    }
    if (lv == BATT_LEVEL_LOW) {
        return FLEX_C_WARN;
    }
    return FLEX_C_TXT2;
}

/* Icono de batería con el trazo de drawBattery() de Flex OS Ultra
 * (FlexOS_Ultra_Icons.h:342): contorno redondeado, polo + y relleno
 * proporcional. Sin dato: solo contorno (criterio de la rama IDF). */
static void build_battery_icon(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, ICON_W, ICON_H);
    lv_obj_set_style_radius(body, 4, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, flex_color(FLEX_C_TXT), 0);
    lv_obj_set_style_border_opa(body, LV_OPA_COVER, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_home.icon_body = body;

    lv_obj_t *pin = lv_obj_create(parent);
    lv_obj_remove_style_all(pin);
    lv_obj_set_pos(pin, x + ICON_W, y + ICON_H / 3);
    lv_obj_set_size(pin, 3, ICON_H / 3 + 1);
    lv_obj_set_style_bg_color(pin, flex_color(FLEX_C_TXT), 0);
    lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pin, 1, 0);

    lv_obj_t *fill = lv_obj_create(body);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, 2, 2);
    lv_obj_set_size(fill, 0, ICON_H - 8);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, flex_color(FLEX_C_TXT), 0);
    lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
    s_home.icon_fill = fill;
}

static void update_battery(void)
{
    watch_power_snapshot_t snap;
    const bool have = s_home.power_ok && watch_power_get(&snap);

    const batt_state_t st = have ? snap.track.state : BATT_STATE_UNKNOWN;
    const batt_level_t lv = have ? snap.track.level : BATT_LEVEL_UNKNOWN;
    const int pct = have ? snap.track.percent : -1;
    const int vbat = have ? snap.track.vbat_mv : -1;
    const int vbus = have ? (int)snap.raw.vbus_present : -1;

    if (pct == s_home.last_pct && (int)st == s_home.last_state && (int)lv == s_home.last_level &&
        vbat == s_home.last_vbat && vbus == s_home.last_vbus) {
        return;
    }
    s_home.last_pct = pct;
    s_home.last_state = (int)st;
    s_home.last_level = (int)lv;
    s_home.last_vbat = vbat;
    s_home.last_vbus = vbus;

    char buf[96];
    const uint32_t col = state_color(st, lv);

    /* barra de estado */
    if (pct >= 0) {
        lv_label_set_text_fmt(s_home.status_pct, "%d %%", pct);
        const int inner = ICON_W - 8;
        int fw = inner * pct / 100;
        if (fw < 2) {
            fw = 2;
        }
        lv_obj_set_width(s_home.icon_fill, fw);
        lv_obj_set_style_bg_color(s_home.icon_fill,
                                  flex_color(col == FLEX_C_TXT2 ? FLEX_C_TXT : col), 0);
        lv_obj_remove_flag(s_home.icon_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_home.status_pct, "--");
        lv_obj_add_flag(s_home.icon_fill, LV_OBJ_FLAG_HIDDEN);
    }

    /* tarjeta de batería */
    if (pct >= 0) {
        lv_label_set_text_fmt(s_home.batt_pct, "%d %%", pct);
    } else {
        lv_label_set_text(s_home.batt_pct, "--");
    }
    lv_label_set_text(s_home.batt_state, state_text(st));
    lv_obj_set_style_text_color(s_home.batt_state, flex_color(col), 0);

    if (!have) {
        snprintf(buf, sizeof(buf), "AXP2101 no disponible");
    } else {
        char v[32];
        if (vbat >= 0) {
            snprintf(v, sizeof(v), "%d.%02d V", vbat / 1000, (vbat % 1000) / 10);
        } else if (snap.raw.batt_present && !snap.raw.vbat_adc_enabled) {
            snprintf(v, sizeof(v), "VBAT: ADC apagado");
        } else {
            snprintf(v, sizeof(v), "VBAT --");
        }
        snprintf(buf, sizeof(buf), "%s  ·  %s%s", v, vbus > 0 ? "USB conectado" : "Sin USB",
                 (snap.raw.batt_present && !snap.raw.gauge_enabled) ? "  ·  gauge apagado" : "");
    }
    lv_label_set_text(s_home.batt_detail, buf);
}

static const char *flash_text(void)
{
    switch (s_home.flash.verdict) {
    case FLASH_PROBE_UPPER_DISTINCT: return ">16 MB sin aliasing";
    case FLASH_PROBE_ALIASED:        return ">16 MB con ALIASING";
    case FLASH_PROBE_INCONCLUSIVE:   return ">16 MB no concluyente";
    case FLASH_PROBE_READ_ERROR:     return ">16 MB error de lectura";
    case FLASH_PROBE_NOT_CONFIGURED: return "16 MB declarados";
    default:                         return "sonda no ejecutada";
    }
}

static void update_system(void)
{
    watch_mem_report_t m;
    watch_diag_mem(&m);
    lv_label_set_text_fmt(s_home.sys_line2, "PSRAM %lu.%lu MB  ·  RAM %lu KB",
                          (unsigned long)(m.psram_free / (1024 * 1024)),
                          (unsigned long)((m.psram_free % (1024 * 1024)) * 10 / (1024 * 1024)),
                          (unsigned long)(m.internal_free / 1024));
    if (s_home.touch_seen) {
        lv_label_set_text_fmt(s_home.sys_line3, "Toque  x %d  ·  y %d", (int)s_home.touch.x, (int)s_home.touch.y);
    } else {
        lv_label_set_text(s_home.sys_line3, s_home.touch_ok ? "Toca la pantalla" : "Táctil no disponible");
    }
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    update_battery();
    if (s_home.ticks++ % MEM_EVERY_TICKS == 0) {
        update_system();
    }
}

static void touch_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_get_point(indev, &s_home.touch);
        s_home.touch_seen = true;
        lv_obj_set_pos(s_home.touch_dot, s_home.touch.x - 14, s_home.touch.y - 14);
        lv_obj_remove_flag(s_home.touch_dot, LV_OBJ_FLAG_HIDDEN);
        if (code == LV_EVENT_PRESSED) {
            update_system();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_add_flag(s_home.touch_dot, LV_OBJ_FLAG_HIDDEN);
        update_system();
    }
}

void watch_ui_home_create(const watch_ui_boot_info_t *info)
{
    memset(&s_home, 0, sizeof(s_home));
    s_home.touch_ok = info->touch_ok;
    s_home.power_ok = info->power_ok;
    s_home.flash = info->flash;
    s_home.last_pct = -2;
    s_home.last_state = -1;

    watch_theme_init();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    watch_theme_apply_background(scr);

    /* Marco de 1 px en el borde exacto del panel: si el desfase de columna
     * fuera incorrecto, uno de los lados se vería cortado o desplazado. */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_70, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    /* Barra de estado */
    lv_obj_t *brand = watch_theme_label(scr, FLEX_FONT_SMALL, FLEX_C_TXT2, "Flex OS");
    lv_obj_set_pos(brand, FLEX_SAFE_X, FLEX_SAFE_TOP + 2);
    const int32_t icon_x = FLEX_SCREEN_W - FLEX_SAFE_X - ICON_W - 3;
    build_battery_icon(scr, icon_x, FLEX_SAFE_TOP + 6);
    s_home.status_pct = watch_theme_label(scr, FLEX_FONT_SMALL, FLEX_C_TXT, "--");
    lv_obj_align(s_home.status_pct, LV_ALIGN_TOP_RIGHT, -(FLEX_SAFE_X + ICON_W + 3 + 8), FLEX_SAFE_TOP + 2);

    /* Esfera: sin RTC en B1 no hay hora real que mostrar. */
    s_home.clock = watch_theme_label(scr, FLEX_FONT_CLOCK, FLEX_C_TXT, "--:--");
    lv_obj_align(s_home.clock, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_t *sub = watch_theme_label(scr, FLEX_FONT_SMALL, FLEX_C_MUTE, "Hora sin sincronizar");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 150);

    /* Tarjeta de batería */
    lv_obj_t *bc = watch_theme_glass_card(scr, CARD_X, BATT_CARD_Y, CARD_W, BATT_CARD_H);
    lv_obj_t *bt = watch_theme_label(bc, FLEX_FONT_BODY, FLEX_C_TXT2, "Batería");
    lv_obj_align(bt, LV_ALIGN_TOP_LEFT, 0, 0);
    s_home.batt_state = watch_theme_label(bc, FLEX_FONT_BODY, FLEX_C_TXT2, "Sin datos");
    lv_obj_align(s_home.batt_state, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_home.batt_pct = watch_theme_label(bc, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    lv_obj_align(s_home.batt_pct, LV_ALIGN_TOP_LEFT, 0, 24);
    s_home.batt_detail = watch_theme_label(bc, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    lv_obj_align(s_home.batt_detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Tarjeta de sistema: tres líneas que nunca se salen de la tarjeta. */
    lv_obj_t *sc = watch_theme_glass_card(scr, CARD_X, SYS_CARD_Y, CARD_W, SYS_CARD_H);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    s_home.sys_line1 = watch_theme_label(sc, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_text_fmt(s_home.sys_line1, "Flash %lu MB  ·  %s",
                          (unsigned long)(s_home.flash.configured_size / (1024 * 1024)), flash_text());
    s_home.sys_line2 = watch_theme_label(sc, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    s_home.sys_line3 = watch_theme_label(sc, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    lv_obj_t *lines[] = {s_home.sys_line1, s_home.sys_line2, s_home.sys_line3, s_home.batt_detail};
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        lv_label_set_long_mode(lines[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(lines[i], TEXT_W);
    }

    /* Punto de toque: anillo del acento que sigue al dedo. */
    s_home.touch_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_home.touch_dot);
    lv_obj_set_size(s_home.touch_dot, 28, 28);
    lv_obj_set_style_radius(s_home.touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_home.touch_dot, 3, 0);
    lv_obj_set_style_border_color(s_home.touch_dot, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_border_opa(s_home.touch_dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_home.touch_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_home.touch_dot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESS_LOST, NULL);

    update_battery();
    update_system();
    lv_timer_create(refresh_cb, REFRESH_MS, NULL);

    lv_screen_load(scr);
}
