/*
 * ui_aod — pantalla siempre activa (AOD) y Pixel Shift.
 *
 * La enciende y la apaga ui_screen (estado SCR_AOD) en lugar de apagar el
 * panel, si está activada en Ajustes > Pantalla o en Device Care y no está
 * activo Flex Ultra Power Saving.
 *
 *  - Capa opaca negra sobre las páginas (ui_overlay_parent: LVGL no dibuja
 *    nada de debajo) con lo esencial atenuado: la hora (digital, o agujas sin
 *    segundero si la esfera elegida es la analógica), la fecha y el % de
 *    batería. El resto de píxeles quedan apagados.
 *  - Una sola actualización por minuto: el temporizador mira cada segundo,
 *    pero solo reescribe etiquetas al cambiar el minuto, y sin animaciones.
 *  - El brillo físico baja a un nivel fijo bajo (ui_screen, DCS 0x51).
 *  - Pixel Shift: el contenido se desplaza cada minuto unos píxeles
 *    (scr_pixel_shift, ±8 px en AOD, ±3 px en la esfera) para repartir el
 *    desgaste. Reduce el riesgo de marcas en el AMOLED; no lo elimina ni
 *    detecta el quemado.
 */
#include "ui_internal.h"

#include "ui_logic.h"
#include "ui_screen_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define AOD_TICK_MS    1000
#define AOD_SHIFT_MAX  8
#define AOD_C_TIME     0x6A6F7C
#define AOD_C_DATE     0x4A4E58
#define AOD_ANALOG_D   300

static struct {
    lv_obj_t *root;
    lv_obj_t *box;
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *batt;
    lv_obj_t *dial;
    lv_timer_t *timer;
    int64_t last_key;
    int last_pct;
    uint32_t step; /* paso de Pixel Shift (sigue entre una entrada y otra) */
} s_aod;

bool ui_aod_enabled(void)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    return ws.aod && !ui_psave_active();
}

bool ui_aod_active(void)
{
    return s_aod.root != NULL;
}

const char *ui_dim_text(uint8_t dim)
{
    switch (dim) {
    case WS_DIM_OFF:
        return "Desactivada";
    case WS_DIM_SOFT:
        return "Suave";
    case WS_DIM_STRONG:
        return "Fuerte";
    default:
        return "Media";
    }
}

void ui_pixel_shift(int max, uint32_t step, int32_t *dx, int32_t *dy)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (!ws.pshift) {
        *dx = 0;
        *dy = 0;
        return;
    }
    int x, y;
    scr_pixel_shift(step, max, &x, &y);
    *dx = x;
    *dy = y;
}

static void update(bool force)
{
    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);
    if (force || t.key != s_aod.last_key) {
        s_aod.last_key = t.key;
        if (s_aod.time) {
            lv_label_set_text(s_aod.time, t.time);
        }
        if (s_aod.dial) {
            ui_analog_set(s_aod.dial, t.valid ? dt.hour : -1, t.valid ? dt.minute : 0, -1);
        }
        lv_label_set_text(s_aod.date, t.date);
        /* Pixel Shift: un paso por minuto, en el mismo redibujo. */
        int32_t dx, dy;
        ui_pixel_shift(AOD_SHIFT_MAX, s_aod.step++, &dx, &dy);
        lv_obj_set_pos(s_aod.box, dx, dy);
    }
    watch_power_snapshot_t snap;
    const int pct = ui_system_power(&snap) ? snap.track.percent : -1;
    if (force || pct != s_aod.last_pct) {
        s_aod.last_pct = pct;
        if (pct >= 0) {
            lv_label_set_text_fmt(s_aod.batt, "%d %%", pct);
        } else {
            lv_label_set_text(s_aod.batt, "");
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    update(false);
}

void ui_aod_show(void)
{
    if (s_aod.root) {
        return;
    }
    watch_theme_init();
    watch_settings_t ws;
    watch_settings_get(&ws);
    lv_obj_t *root = lv_obj_create(ui_overlay_parent());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_aod.root = root;

    lv_obj_t *box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_aod.box = box;

    s_aod.time = NULL;
    s_aod.dial = NULL;
    if (ws.face == UI_FACE_ANALOG && !ui_psave_active()) {
        s_aod.dial = ui_analog_create(box, AOD_ANALOG_D, true);
        lv_obj_align(s_aod.dial, LV_ALIGN_CENTER, 0, -24);
        s_aod.date = watch_theme_label(box, FLEX_FONT_SMALL, AOD_C_DATE, "");
        lv_obj_align(s_aod.date, LV_ALIGN_BOTTOM_MID, 0, -56);
    } else {
        s_aod.time = watch_theme_label(box, FLEX_FONT_CLOCK, AOD_C_TIME, "--:--");
        lv_obj_align(s_aod.time, LV_ALIGN_TOP_MID, 0, 140);
        s_aod.date = watch_theme_label(box, FLEX_FONT_BODY, AOD_C_DATE, "");
        lv_obj_align(s_aod.date, LV_ALIGN_TOP_MID, 0, 140 + 96);
    }
    s_aod.batt = watch_theme_label(box, FLEX_FONT_SMALL, AOD_C_DATE, "");
    lv_obj_align(s_aod.batt, LV_ALIGN_BOTTOM_MID, 0, -28);

    s_aod.last_key = INT64_MIN;
    s_aod.last_pct = -2;
    update(true);
    s_aod.timer = lv_timer_create(tick_cb, AOD_TICK_MS, NULL);
}

void ui_aod_hide(void)
{
    if (!s_aod.root) {
        return;
    }
    lv_timer_delete(s_aod.timer);
    s_aod.timer = NULL;
    lv_obj_delete(s_aod.root);
    s_aod.root = NULL;
    s_aod.box = NULL;
    s_aod.time = NULL;
    s_aod.dial = NULL;
}
