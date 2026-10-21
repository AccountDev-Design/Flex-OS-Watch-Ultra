/*
 * ui_sensors — app Sensores (F4.4): datos reales del IMU QMI8658 y de la
 * batería.
 *  - Nivel de burbuja: el punto se mueve con la inclinación (alabeo en
 *    horizontal, cabeceo en vertical) y se pone verde casi plano. La
 *    correspondencia entre los ejes del chip y la esfera se comprueba en el
 *    reloj (prueba T61).
 *  - Acelerómetro (g), giroscopio (°/s) y temperatura del chip.
 *  - Batería (de la caché de ui_system) y estado del IMU (muestras, perdidas,
 *    errores de I2C).
 * El IMU solo mide mientras esta página existe (watch_imu_set_active). Sin
 * IMU se dice "No disponible": nunca se inventan valores.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "qmi8658.h"
#include "ui_logic.h"
#include "watch_imu.h"
#include "watch_theme.h"

#define SEN_TICK_MS 100
#define LEVEL_D     200      /* diámetro del nivel */
#define DOT_R       14
#define TILT_MAX    300      /* décimas de grado al borde del nivel (30°) */
#define FLAT_D10    20       /* "plano": menos de 2° */

typedef struct {
    lv_obj_t *level;
    lv_obj_t *tilt;
    lv_obj_t *acc[3];
    lv_obj_t *gyro[3];
    lv_obj_t *temp;
    lv_obj_t *bv;
    lv_obj_t *bp;
    lv_obj_t *bs;
    lv_obj_t *st_samples;
    lv_obj_t *st_missed;
    lv_obj_t *st_errors;
    lv_obj_t *note;
    int32_t pitch, roll;  /* décimas de grado */
    bool have_tilt;
    int32_t dot_dx, dot_dy; /* posición dibujada del punto (px desde el centro) */
    bool dot_flat, dot_shown;
    uint32_t gen;
    bool imu_on;
} sen_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* "-1,02" con dos decimales a partir de milésimas. */
static void fmt_milli(char *buf, size_t n, int32_t v, const char *unit, int decimals)
{
    const char *sign = v < 0 ? "-" : "";
    const int32_t a = v < 0 ? -v : v;
    if (decimals == 2) {
        snprintf(buf, n, "%s%ld,%02ld %s", sign, (long)(a / 1000), (long)(a % 1000 / 10), unit);
    } else {
        snprintf(buf, n, "%s%ld,%01ld %s", sign, (long)(a / 1000), (long)(a % 1000 / 100), unit);
    }
}

/* Posición del punto en px (dentro del círculo) y si está "plano". */
static void dot_pos(const sen_t *s, int32_t *odx, int32_t *ody, bool *oflat)
{
    const int32_t r = LEVEL_D / 2 - 2;
    const int32_t span = r - DOT_R - 2;
    int32_t dx = s->roll * span / TILT_MAX, dy = s->pitch * span / TILT_MAX;
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 > span * span) {
        int32_t d = 1;
        while (d * d < d2) {
            d++;
        }
        dx = dx * span / d;
        dy = dy * span / d;
    }
    *odx = dx;
    *ody = dy;
    *oflat = s->roll < FLAT_D10 && s->roll > -FLAT_D10 && s->pitch < FLAT_D10 && s->pitch > -FLAT_D10;
}

static void level_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const sen_t *s = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2, r = LEVEL_D / 2 - 2;

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = cx;
    arc.center.y = cy;
    arc.color = flex_color(FLEX_C_BORDER);
    arc.width = 2;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.radius = (uint16_t)r;
    lv_draw_arc(layer, &arc);
    arc.radius = (uint16_t)(r / 2);
    lv_draw_arc(layer, &arc);

    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = flex_color(FLEX_C_DIVIDER);
    ln.width = 2;
    ln.p1.x = cx - r;
    ln.p1.y = cy;
    ln.p2.x = cx + r;
    ln.p2.y = cy;
    lv_draw_line(layer, &ln);
    ln.p1.x = cx;
    ln.p1.y = cy - r;
    ln.p2.x = cx;
    ln.p2.y = cy + r;
    lv_draw_line(layer, &ln);

    if (!s->dot_shown) {
        return;
    }
    const int32_t dx = s->dot_dx, dy = s->dot_dy;
    const bool flat = s->dot_flat;
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = flex_color(flat ? FLEX_C_OK : FLEX_C_ACCENT);
    dot.bg_opa = LV_OPA_COVER;
    dot.radius = LV_RADIUS_CIRCLE;
    const lv_area_t da = {cx + dx - DOT_R, cy + dy - DOT_R, cx + dx + DOT_R, cy + dy + DOT_R};
    lv_draw_rect(layer, &dot, &da);
}

static void refresh(sen_t *s)
{
    char buf[40];
    watch_imu_snapshot_t im;
    const bool have = watch_imu_get(&im);
    const bool live = have && im.state == IMU_RUNNING && im.samples > 0;
    if (!have || im.state == IMU_ABSENT || im.state == IMU_ERROR) {
        watch_theme_info_set(s->tilt, "No disponible");
        const char *nt = im.state == IMU_ERROR ? "El IMU responde pero no se pudo configurar."
                                               : "El IMU (QMI8658, 0x6B) no responde.";
        if (strcmp(lv_label_get_text(s->note), nt) != 0) {
            lv_label_set_text(s->note, nt);
        }
        watch_theme_set_visible(s->note, true);
    } else {
        watch_theme_set_visible(s->note, false);
    }
    if (live && watch_imu_generation() != s->gen) {
        s->gen = watch_imu_generation();
        s->have_tilt = qmi_tilt(im.acc_mg, &s->pitch, &s->roll);
        if (s->have_tilt) {
            const int32_t r = s->roll < 0 ? -s->roll : s->roll, q = s->pitch < 0 ? -s->pitch : s->pitch;
            snprintf(buf, sizeof(buf), "%s%ld,%ld° · %s%ld,%ld°", s->roll < 0 ? "-" : "", (long)(r / 10), (long)(r % 10),
                     s->pitch < 0 ? "-" : "", (long)(q / 10), (long)(q % 10));
            watch_theme_info_set(s->tilt, buf);
        }
        for (int i = 0; i < 3; i++) {
            fmt_milli(buf, sizeof(buf), im.acc_mg[i], "g", 2);
            watch_theme_info_set(s->acc[i], buf);
            fmt_milli(buf, sizeof(buf), im.gyro_mdps[i], "°/s", 1);
            watch_theme_info_set(s->gyro[i], buf);
        }
        fmt_milli(buf, sizeof(buf), im.temp_c100 * 10, "°C", 1);
        watch_theme_info_set(s->temp, buf);
        /* El nivel se redibuja solo si el punto se mueve de verdad (antes,
         * con cada muestra: ~2 pantallas por segundo con el reloj quieto). */
        int32_t dx = 0, dy = 0;
        bool flat = false;
        if (s->have_tilt) {
            dot_pos(s, &dx, &dy, &flat);
        }
        if (s->have_tilt != s->dot_shown || dx != s->dot_dx || dy != s->dot_dy || flat != s->dot_flat) {
            s->dot_shown = s->have_tilt;
            s->dot_dx = dx;
            s->dot_dy = dy;
            s->dot_flat = flat;
            lv_obj_invalidate(s->level);
        }
    } else if (have && im.state == IMU_STARTING) {
        watch_theme_info_set(s->tilt, "Arrancando…");
    }
    if (have) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)im.samples);
        watch_theme_info_set(s->st_samples, buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)im.missed);
        watch_theme_info_set(s->st_missed, buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)im.errors);
        watch_theme_info_set(s->st_errors, buf);
    }

    watch_power_snapshot_t p;
    if (ui_system_power(&p) && p.driver_ok) {
        ui_fmt_volts(buf, sizeof(buf), p.track.vbat_mv);
        watch_theme_info_set(s->bv, p.track.vbat_mv > 0 ? buf : "--");
        if (p.track.percent >= 0) {
            snprintf(buf, sizeof(buf), "%d %%", p.track.percent);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        watch_theme_info_set(s->bp, buf);
        watch_theme_info_set(s->bs, ui_batt_state_text(p.track.state));
    } else {
        watch_theme_info_set(s->bv, "No disponible");
        watch_theme_info_set(s->bp, "--");
        watch_theme_info_set(s->bs, "--");
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void delete_cb(lv_event_t *e)
{
    sen_t *sp = lv_event_get_user_data(e);
    if (sp->imu_on) {
        watch_imu_set_active(false);
    }
    lv_free(lv_event_get_user_data(e));
}

/* App suspendida: el IMU se suelta; al retomarla, se vuelve a pedir. */
static void suspend_cb(lv_event_t *e)
{
    sen_t *sp = lv_event_get_user_data(e);
    const bool on = lv_event_get_code(e) == (lv_event_code_t)ui_nav_event_resume();
    if (on != sp->imu_on) {
        sp->imu_on = on;
        watch_imu_set_active(on);
    }
}

lv_obj_t *ui_sensors_create(lv_obj_t *parent)
{
    sen_t *s = lv_malloc_zeroed(sizeof(*s));
    LV_ASSERT_MALLOC(s);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Sensores", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    /* Nivel de burbuja, centrado */
    lv_obj_t *box = lv_obj_create(list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LEVEL_D);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s->level = lv_obj_create(box);
    lv_obj_remove_style_all(s->level);
    lv_obj_set_size(s->level, LEVEL_D, LEVEL_D);
    lv_obj_center(s->level);
    lv_obj_remove_flag(s->level, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->level, level_draw_cb, LV_EVENT_DRAW_MAIN, s);
    s->note = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_WARN, "");
    lv_label_set_long_mode(s->note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s->note, LV_PCT(100));
    lv_obj_set_style_text_align(s->note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s->note, LV_OBJ_FLAG_HIDDEN);

    watch_theme_section(list, "Movimiento (QMI8658)");
    s->tilt = watch_theme_info_row(list, "Inclinación", "--");
    static const char *const AX[3] = {"Aceleración X", "Aceleración Y", "Aceleración Z"};
    static const char *const GX[3] = {"Giro X", "Giro Y", "Giro Z"};
    for (int i = 0; i < 3; i++) {
        s->acc[i] = watch_theme_info_row(list, AX[i], "--");
    }
    for (int i = 0; i < 3; i++) {
        s->gyro[i] = watch_theme_info_row(list, GX[i], "--");
    }
    s->temp = watch_theme_info_row(list, "Temperatura del chip", "--");

    watch_theme_section(list, "Batería");
    s->bv = watch_theme_info_row(list, "Tensión", "--");
    s->bp = watch_theme_info_row(list, "Nivel", "--");
    s->bs = watch_theme_info_row(list, "Estado", "--");

    watch_theme_section(list, "Estado del IMU");
    s->st_samples = watch_theme_info_row(list, "Muestras", "--");
    s->st_missed = watch_theme_info_row(list, "Perdidas", "--");
    s->st_errors = watch_theme_info_row(list, "Errores I2C", "--");

    watch_imu_set_active(true);
    s->imu_on = true;
    s->gen = (uint32_t)-1;
    refresh(s);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, s);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), s);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_resume(), s);
    ui_page_timer(page, tick_cb, SEN_TICK_MS, s);
    return page;
}
