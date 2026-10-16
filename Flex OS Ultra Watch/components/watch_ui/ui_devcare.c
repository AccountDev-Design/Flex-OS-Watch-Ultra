/*
 * ui_devcare — app Device Care (IC_DEVCARE de Flex OS Ultra), pantalla
 * "Salud de Flex OS" adaptada al reloj (FlexOS_Ultra_DeviceCare.h:760-930).
 *
 * De la referencia:
 *  - la puntuación 0..100 sale SOLO de métricas medidas (dcScoreFrom,
 *    apps_logic); sin nada medido no se enseña número;
 *  - anillo con la puntuación en el color de su nivel y la palabra
 *    ("Todo correcto", "Necesita atención", "Estado crítico");
 *  - filas con semáforo (dcRowLed): punto del color del nivel, nombre y valor;
 *  - acceso a las pruebas (pantalla y táctil, sensores, diagnóstico).
 * Métricas del reloj: RAM interna, PSRAM, estabilidad (motivo del arranque y
 * tiempo encendido), servicios, errores de bus I2C, almacenamiento y
 * batería. Las del P4 sin equivalente aquí (ritmo del bucle de Arduino,
 * contador persistente de reinicios, BNO085) no se inventan.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "apps_logic.h"
#include "esp_timer.h"
#include "watch_diag.h"
#include "watch_imu.h"
#include "watch_settings.h"
#include "watch_storage.h"
#include "watch_theme.h"
#include "watch_time.h"

#define DC_TICK_MS 2000
#define RING_D     120
#define CARD_H     152

enum { M_MEM, M_PSRAM, M_STAB, M_SVC, M_ERR, M_STO, M_BATT, M_N };
static const char *const kNames[M_N] = {"Memoria", "PSRAM", "Estabilidad", "Servicios",
                                        "Errores",  "Almacenamiento", "Batería"};

typedef struct {
    lv_obj_t *arc;
    lv_obj_t *score;
    lv_obj_t *word;
    lv_obj_t *dot[M_N];
    lv_obj_t *val[M_N];
} dc_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    const ui_page_create_t page = (ui_page_create_t)lv_event_get_user_data(e);
    /* la prueba táctil necesita el borde izquierdo: sin el gesto "volver" */
    ui_nav_push_opts(page, page != ui_touchtest_create);
}

static uint32_t level_rgb(dc_level_t lv)
{
    switch (lv) {
    case DC_OK:
        return FLEX_C_OK;
    case DC_WARN:
        return FLEX_C_WARN;
    case DC_BAD:
        return FLEX_C_ERR;
    default:
        return FLEX_C_MUTE;
    }
}

static void fmt_bytes(char *b, size_t n, unsigned long v)
{
    if (v >= 1024ul * 1024) {
        snprintf(b, n, "%lu,%lu MB", v / (1024ul * 1024), (v % (1024ul * 1024)) * 10 / (1024ul * 1024));
    } else {
        snprintf(b, n, "%lu KB", v / 1024);
    }
}

/* Mide las métricas y deja su valor en texto (dcCollect). */
static void collect(dc_metric_t m[M_N], char val[M_N][32])
{
    char b[16];
    watch_mem_report_t mem;
    watch_diag_mem(&mem);
    fmt_bytes(b, sizeof(b), mem.internal_free);
    m[M_MEM].level = dc_sram_level(mem.internal_free);
    m[M_MEM].pct = -1;
    snprintf(val[M_MEM], 32, "%s libres", b);

    if (mem.psram_total) {
        m[M_PSRAM].level = dc_psram_level(mem.psram_free, mem.psram_largest);
        m[M_PSRAM].pct = (int)((unsigned long long)mem.psram_free * 100 / mem.psram_total);
        fmt_bytes(b, sizeof(b), mem.psram_free);
        snprintf(val[M_PSRAM], 32, "%s libres", b);
    } else {
        m[M_PSRAM].level = DC_NA;
        m[M_PSRAM].pct = -1;
        snprintf(val[M_PSRAM], 32, "No disponible");
    }

    const watch_ui_boot_info_t *bi = ui_boot_info();
    const unsigned long up = (unsigned long)(esp_timer_get_time() / 1000000);
    m[M_STAB].pct = -1;
    m[M_STAB].level = (bi && bi->reset_text) ? (bi->reset_ok ? DC_OK : DC_WARN) : DC_OK;
    if (up >= 3600) {
        snprintf(val[M_STAB], 32, "%luh %lum", up / 3600, (up % 3600) / 60);
    } else {
        snprintf(val[M_STAB], 32, "%lum %lus", up / 60, up % 60);
    }

    /* Servicios que el reloj sabe comprobar de verdad */
    watch_time_info_t ti;
    watch_time_info(&ti);
    watch_storage_info_t si;
    watch_storage_info(&si);
    watch_imu_snapshot_t im;
    const bool imu = watch_imu_get(&im) && im.state != IMU_ABSENT && im.state != IMU_ERROR;
    watch_power_snapshot_t ps;
    const bool pwr = ui_system_power(&ps);
    int ok = 0;
    const int tot = 5;
    ok += watch_settings_persistent();
    ok += ti.state == WATCH_TIME_OK;
    ok += si.state == STG_MOUNTED;
    ok += imu;
    ok += pwr;
    m[M_SVC].level = dc_count_level(ok, tot);
    m[M_SVC].pct = ok * 100 / tot;
    snprintf(val[M_SVC], 32, "%d/%d", ok, tot);

    /* Errores: subsistemas con fallos de bus I2C desde el arranque */
    int errs = (ti.errors > 0) + (imu && im.errors > 0) + (si.state == STG_ERROR);
    m[M_ERR].level = errs == 0 ? DC_OK : (errs == 1 ? DC_WARN : DC_BAD);
    m[M_ERR].pct = -1;
    snprintf(val[M_ERR], 32, "%d", errs);

    if (si.state == STG_MOUNTED && si.total) {
        const int used = (int)((unsigned long long)si.used * 100 / si.total);
        m[M_STO].level = dc_storage_level(used);
        m[M_STO].pct = 100 - used;
        fmt_bytes(b, sizeof(b), (unsigned long)(si.total - si.used));
        snprintf(val[M_STO], 32, "%s libres", b);
    } else {
        m[M_STO].level = DC_NA;
        m[M_STO].pct = -1;
        snprintf(val[M_STO], 32, "No disponible");
    }

    if (pwr && ps.track.percent >= 0) {
        m[M_BATT].level = ps.track.level == BATT_LEVEL_CRITICAL ? DC_BAD
                          : (ps.track.level == BATT_LEVEL_LOW ? DC_WARN : DC_OK);
        m[M_BATT].pct = ps.track.percent;
        snprintf(val[M_BATT], 32, "%d %%", ps.track.percent);
    } else {
        m[M_BATT].level = DC_NA;
        m[M_BATT].pct = -1;
        snprintf(val[M_BATT], 32, "No disponible");
    }
}

static void refresh(dc_page_t *p)
{
    dc_metric_t m[M_N];
    char val[M_N][32];
    collect(m, val);
    for (int i = 0; i < M_N; i++) {
        lv_obj_set_style_bg_color(p->dot[i], flex_color(level_rgb(m[i].level)), 0);
        if (strcmp(lv_label_get_text(p->val[i]), val[i]) != 0) {
            lv_label_set_text(p->val[i], val[i]);
        }
    }
    int sc;
    if (dc_score(m, M_N, &sc)) {
        const uint32_t c = level_rgb(dc_score_level(sc));
        lv_arc_set_value(p->arc, sc);
        lv_obj_set_style_arc_color(p->arc, flex_color(c), LV_PART_INDICATOR);
        lv_label_set_text_fmt(p->score, "%d", sc);
        lv_label_set_text(p->word, dc_score_word(sc));
        lv_obj_set_style_text_color(p->word, flex_color(c), 0);
    } else {
        lv_arc_set_value(p->arc, 0);
        lv_label_set_text(p->score, "--");
        lv_label_set_text(p->word, "Sin datos");
        lv_obj_set_style_text_color(p->word, flex_color(FLEX_C_MUTE), 0);
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* Fila con semáforo (dcRowLed): punto, nombre y valor. El punto va delante
 * de la fila de información del tema; el valor se guarda aparte porque el
 * punto desplaza el índice de los hijos. */
static void led_row(lv_obj_t *list, const char *name, lv_obj_t **dot_out, lv_obj_t **val_out)
{
    lv_obj_t *r = watch_theme_info_row(list, name, "");
    *val_out = lv_obj_get_child(r, 1);
    lv_obj_t *dot = lv_obj_create(r);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, flex_color(FLEX_C_MUTE), 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(dot, 0);
    *dot_out = dot;
}

lv_obj_t *ui_devcare_create(lv_obj_t *parent)
{
    dc_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Device Care", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    /* Tarjeta de la puntuación (dcScoreRing): anillo sobre la pista del tema. */
    lv_obj_t *card = watch_theme_tile(list, 0, 0, LV_PCT(100), CARD_H);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    p->arc = lv_arc_create(card);
    lv_obj_set_size(p->arc, RING_D, RING_D);
    lv_obj_align(p->arc, LV_ALIGN_LEFT_MID, 0, 0);
    lv_arc_set_rotation(p->arc, 270);
    lv_arc_set_bg_angles(p->arc, 0, 360);
    lv_arc_set_range(p->arc, 0, 100);
    lv_obj_remove_style(p->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(p->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(p->arc, RING_D / 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(p->arc, RING_D / 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p->arc, flex_color(FLEX_C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(p->arc, true, LV_PART_INDICATOR);
    p->score = watch_theme_label(p->arc, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    lv_obj_align(p->score, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t *of = watch_theme_label(p->arc, FLEX_FONT_SMALL, FLEX_C_MUTE, "/100");
    lv_obj_align(of, LV_ALIGN_CENTER, 0, 28);
    /* Rótulo y palabra en columna a la derecha del anillo: nunca se pisan. */
    lv_obj_t *col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(col, RING_D + 16, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
    watch_theme_label(col, FLEX_FONT_SMALL, FLEX_C_TXT2, "Salud de Flex OS");
    p->word = watch_theme_label(col, FLEX_FONT_BODY, FLEX_C_MUTE, "");
    lv_label_set_long_mode(p->word, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->word, LV_PCT(100));

    watch_theme_section(list, "Estado del dispositivo");
    for (int i = 0; i < M_N; i++) {
        led_row(list, kNames[i], &p->dot[i], &p->val[i]);
    }

    watch_theme_section(list, "Pruebas");
    static const struct {
        flex_icon_t icon;
        const char *name;
        ui_page_create_t page;
    } kTests[] = {
        {FLEX_ICON_WATCH, "Pantalla y táctil", ui_touchtest_create},
        {FLEX_ICON_MOTION, "Sensores", ui_sensors_create},
        {FLEX_ICON_PULSE, "Diagnóstico completo", ui_diag_create},
    };
    for (size_t i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++) {
        lv_obj_t *r = watch_theme_list_row(list, kTests[i].icon, FLEX_C_CAT_BLUE, kTests[i].name, NULL, true);
        lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)kTests[i].page);
    }

    refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, DC_TICK_MS, p);
    return page;
}
