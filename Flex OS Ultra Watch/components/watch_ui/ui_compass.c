/*
 * ui_compass — app Flex Compass (IC_BRUJULA de Flex OS Ultra) en el reloj.
 *
 * La referencia (FlexOS_Ultra_AppCompass.h) "no enseña un rumbo cuando no
 * hay rumbo": sin su IMU de 9 ejes muestra la pantalla de requisito
 * (AppCompass.h:683-709) con el estado REAL del sensor, y en "Datos técnicos"
 * dice "No disponible" en vez de un cero (AppCompass.h:834-870).
 *
 * El QMI8658 del reloj tiene acelerómetro y giroscopio pero NO magnetómetro,
 * así que aquí no hay Norte. Lo que sí se mide es el RUMBO RELATIVO: cuánto
 * ha girado el reloj alrededor de la vertical desde una referencia que fija
 * el usuario ("Fijar referencia"), integrando el giroscopio proyectado sobre
 * la gravedad (watch_imu, imu_heading.c). Se enseña siempre con su
 * incertidumbre (la deriva del giroscopio crece al moverse) y con el aviso de
 * que no es el Norte magnético ni el geográfico. Debajo siguen el requisito
 * con los textos de la referencia y los datos reales (cabeceo, alabeo,
 * estado de cada sensor). El IMU solo mide mientras la página se ve
 * (watch_imu_set_active, con recuento; se suelta si la app se suspende).
 */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qmi8658.h"
#include "watch_imu.h"
#include "watch_theme.h"

#define CMP_TICK_MS 100
#define DIAL_D      200

typedef struct {
    lv_obj_t *acc, *gyr, *fusion;
    lv_obj_t *pitch, *roll, *state;
    lv_obj_t *head, *deg, *unc, *needle;
    lv_point_precise_t npts[2];
    bool imu_on;
} cmp_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* Texto y color solo si cambian (F11: sin redibujos en reposo). */
static void set(lv_obj_t *row, const char *v, uint32_t rgb)
{
    watch_theme_info_set(row, v);
    watch_theme_set_text_color(lv_obj_get_child(row, 1), rgb);
}

static void set_txt(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* "-1,5°" desde décimas de grado */
static void fmt_d10(char *b, size_t n, int32_t d10)
{
    const int32_t a = d10 < 0 ? -d10 : d10;
    snprintf(b, n, "%s%ld,%ld°", d10 < 0 ? "-" : "", (long)(a / 10), (long)(a % 10));
}

static void refresh(cmp_page_t *p)
{
    watch_imu_snapshot_t im;
    const bool have = watch_imu_get(&im);
    const imu_state_t st = have ? im.state : IMU_ABSENT;
    const bool live = st == IMU_RUNNING && im.samples > 0;
    /* Rumbo relativo y su incertidumbre */
    char b[64];
    if (live && im.heading.valid) {
        const int deg = (int)lroundf(im.heading.yaw) % 360;
        snprintf(b, sizeof(b), "%d°", deg);
        set_txt(p->deg, b);
        snprintf(b, sizeof(b), "± %d° · %s", (int)lroundf(im.heading.drift),
                 im.heading.bias_ok ? "giroscopio calibrado" : "quieto 1 s para calibrar");
        set_txt(p->unc, b);
        watch_theme_set_text_color(p->unc, im.heading.drift > 20.0f ? FLEX_C_WARN : FLEX_C_TXT2);
        /* La aguja apunta a la referencia (0°): gira al revés que el reloj.
         * Solo se mueve si su punta cambia de píxel. */
        const float a = -im.heading.yaw * 3.14159265f / 180.0f;
        const float r = DIAL_D / 2.0f;
        const int32_t tx = (int32_t)lroundf(r + sinf(a) * r * 0.82f), ty = (int32_t)lroundf(r - cosf(a) * r * 0.82f);
        if (tx != (int32_t)p->npts[1].x || ty != (int32_t)p->npts[1].y || p->npts[0].x == 0) {
            p->npts[0].x = (int32_t)r;
            p->npts[0].y = (int32_t)r;
            p->npts[1].x = tx;
            p->npts[1].y = ty;
            lv_line_set_points(p->needle, p->npts, 2);
        }
        if (lv_obj_has_flag(p->needle, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(p->needle, LV_OBJ_FLAG_HIDDEN);
        }
        set(p->head, "Relativo (sin Norte)", FLEX_C_TXT2);
    } else {
        set_txt(p->deg, "--");
        set_txt(p->unc, live ? "Sin vertical clara: sujeta el reloj" : "Esperando al IMU");
        if (!lv_obj_has_flag(p->needle, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(p->needle, LV_OBJ_FLAG_HIDDEN);
        }
        set(p->head, "No disponible", FLEX_C_MUTE);
    }
    const char *stxt = st == IMU_RUNNING ? "Conectado"
                       : st == IMU_STARTING || st == IMU_IDLE ? "Detectando"
                       : st == IMU_ERROR ? "Error del bus" : "IMU no detectada";
    set(p->state, stxt, live ? FLEX_C_OK : FLEX_C_MUTE);
    set(p->acc, live ? "Activo" : "No disponible", live ? FLEX_C_OK : FLEX_C_MUTE);
    set(p->gyr, live ? "Activo" : "No disponible", live ? FLEX_C_OK : FLEX_C_MUTE);
    int32_t pitch, roll;
    if (live && qmi_tilt(im.acc_mg, &pitch, &roll)) {
        char b[16];
        fmt_d10(b, sizeof(b), pitch);
        set(p->pitch, b, FLEX_C_TXT);
        fmt_d10(b, sizeof(b), roll);
        set(p->roll, b, FLEX_C_TXT);
    } else {
        set(p->pitch, "No disponible", FLEX_C_MUTE);
        set(p->roll, "No disponible", FLEX_C_MUTE);
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void delete_cb(lv_event_t *e)
{
    cmp_page_t *p = lv_event_get_user_data(e);
    if (p->imu_on) {
        watch_imu_set_active(false);
    }
    lv_free(p);
}

/* App suspendida (gesto hacia arriba): el IMU se suelta; al volver, se pide. */
static void suspend_cb(lv_event_t *e)
{
    cmp_page_t *p = lv_event_get_user_data(e);
    const bool on = lv_event_get_code(e) == (lv_event_code_t)ui_nav_event_resume();
    if (on != p->imu_on) {
        p->imu_on = on;
        watch_imu_set_active(on);
    }
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    watch_imu_heading_reset();
}

static lv_obj_t *para(lv_obj_t *list, const lv_font_t *font, uint32_t rgb, const char *text)
{
    lv_obj_t *l = watch_theme_label(list, font, rgb, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(l, 8, 0);
    return l;
}

lv_obj_t *ui_compass_create(lv_obj_t *parent)
{
    cmp_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Flex Compass", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    /* Rumbo relativo: esfera con marcas, aguja hacia la referencia y grados */
    lv_obj_t *card = watch_theme_tile(list, 0, 0, LV_PCT(100), DIAL_D + 150);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *dial = ui_analog_create(card, DIAL_D, true);
    lv_obj_align(dial, LV_ALIGN_TOP_MID, 0, 0);
    ui_analog_set(dial, -1, 0, -1);
    p->needle = lv_line_create(dial);
    lv_obj_set_style_line_width(p->needle, 6, 0);
    lv_obj_set_style_line_color(p->needle, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_line_rounded(p->needle, true, 0);
    lv_obj_remove_flag(p->needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p->needle, LV_OBJ_FLAG_HIDDEN);
    p->deg = watch_theme_label(dial, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    lv_obj_align(p->deg, LV_ALIGN_CENTER, 0, 30);
    p->unc = watch_theme_label(card, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_width(p->unc, LV_PCT(100));
    lv_obj_set_style_text_align(p->unc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->unc, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(p->unc, LV_ALIGN_TOP_MID, 0, DIAL_D + 6);
    lv_obj_t *rb = watch_theme_button(card, "Fijar referencia", FLEX_C_PRIMARY, 240, 56);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(rb, reset_cb, LV_EVENT_CLICKED, NULL);
    para(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
         "La aguja señala la referencia. Rumbo relativo medido con el giroscopio: no es el Norte magnético ni el "
         "geográfico. "
         "La deriva aumenta al moverse; fija la referencia de nuevo cuando la incertidumbre crezca.");

    /* Pantalla de requisito (AppCompass.h:683-695) */
    para(list, FLEX_FONT_TITLE, FLEX_C_TXT, "Norte: requiere magnetómetro");
    para(list, FLEX_FONT_SMALL, FLEX_C_TXT2,
         "Flex Compass necesita un módulo IMU con magnetómetro para determinar la orientación y el Norte.");
    para(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
         "El QMI8658 de este reloj mide aceleración y giro (6 ejes), pero no el campo magnético: no se muestra "
         "ningún rumbo.");

    watch_theme_section(list, "Sensor IMU");
    p->state = watch_theme_info_row(list, "Estado", "");
    p->acc = watch_theme_info_row(list, "Acelerómetro", "");
    p->gyr = watch_theme_info_row(list, "Giroscopio", "");
    lv_obj_t *mag = watch_theme_info_row(list, "Magnetómetro", "");
    set(mag, "No presente", FLEX_C_MUTE);
    p->fusion = watch_theme_info_row(list, "Sensor Fusion", "");
    set(p->fusion, "Inactivo", FLEX_C_MUTE);

    watch_theme_section(list, "Datos técnicos");
    p->head = watch_theme_info_row(list, "Heading", "");
    set(p->head, "No disponible", FLEX_C_MUTE);
    p->pitch = watch_theme_info_row(list, "Pitch", "");
    p->roll = watch_theme_info_row(list, "Roll", "");

    watch_imu_set_active(true);
    p->imu_on = true;
    watch_imu_heading_reset(); /* referencia: hacia donde apunta al abrir */
    refresh(p);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, p);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), p);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_resume(), p);
    ui_page_timer(page, tick_cb, CMP_TICK_MS, p);
    return page;
}
