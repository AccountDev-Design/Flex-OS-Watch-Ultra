/*
 * ui_compass — app Flex Compass (IC_BRUJULA de Flex OS Ultra) en el reloj.
 *
 * La referencia (FlexOS_Ultra_AppCompass.h) "no enseña un rumbo cuando no
 * hay rumbo": sin su IMU de 9 ejes muestra la pantalla de requisito
 * (AppCompass.h:683-709) con el estado REAL del sensor, y en "Datos técnicos"
 * dice "No disponible" en vez de un cero (AppCompass.h:834-870).
 *
 * El QMI8658 del reloj tiene acelerómetro y giroscopio pero NO magnetómetro,
 * así que aquí no hay Norte: se muestra el requisito con los textos de la
 * referencia adaptados al reloj y, como en la referencia, los datos que sí
 * son reales (cabeceo y alabeo del acelerómetro, estado de cada sensor). El
 * IMU solo mide mientras la página existe (watch_imu_set_active, con
 * recuento: no apaga el sensor a otra página que lo use).
 */
#include "ui_internal.h"

#include <stdio.h>
#include "qmi8658.h"
#include "watch_imu.h"
#include "watch_theme.h"

#define CMP_TICK_MS 200

typedef struct {
    lv_obj_t *acc, *gyr, *fusion;
    lv_obj_t *pitch, *roll, *state;
} cmp_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void set(lv_obj_t *row, const char *v, uint32_t rgb)
{
    watch_theme_info_set(row, v);
    lv_obj_set_style_text_color(lv_obj_get_child(row, 1), flex_color(rgb), 0);
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
    watch_imu_set_active(false);
    lv_free(lv_event_get_user_data(e));
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

    /* Pantalla de requisito (AppCompass.h:683-695) */
    para(list, FLEX_FONT_TITLE, FLEX_C_TXT, "Requiere magnetómetro");
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
    lv_obj_t *head = watch_theme_info_row(list, "Heading", "");
    set(head, "No disponible", FLEX_C_MUTE);
    p->pitch = watch_theme_info_row(list, "Pitch", "");
    p->roll = watch_theme_info_row(list, "Roll", "");

    watch_imu_set_active(true);
    refresh(p);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, CMP_TICK_MS, p);
    return page;
}
