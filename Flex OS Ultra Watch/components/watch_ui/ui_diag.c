/*
 * ui_diag — Ajustes > Diagnóstico: estado medido de cada servicio, como
 * lista que se refresca cada segundo (solo se reescribe lo que cambia).
 *
 *  - Prueba de pantalla y táctil (T2, T3): abre ui_touchtest.
 *  - Memoria libre (watch_diag), búferes de LVGL (watch_display, al arrancar).
 *  - Flash: tamaño declarado, identificador y la sonda de >16 MB de B1.
 *  - Reloj: estado del servicio de hora y registros crudos del PCF85063.
 *  - Batería: lecturas, errores y tiempos del servicio del AXP2101.
 * Sustituye a la pantalla de inicio de B1 (con sus mismos datos).
 */
#include "ui_internal.h"

#include <stdio.h>
#include "ui_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

#define DIAG_TICK_MS 1000

typedef struct {
    lv_obj_t *ram;
    lv_obj_t *ram_big;
    lv_obj_t *psram;
    lv_obj_t *rtc_state;
    lv_obj_t *rtc_detail;
    lv_obj_t *rtc_reads;
    lv_obj_t *rtc_regs;
    lv_obj_t *pwr_reads;
    lv_obj_t *pwr_i2c;
    lv_obj_t *pwr_stack;
} diag_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void touchtest_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push_opts(ui_touchtest_create, false);
}

static const char *flash_text(flash_probe_verdict_t v)
{
    switch (v) {
    case FLASH_PROBE_UPPER_DISTINCT: return "sin aliasing";
    case FLASH_PROBE_ALIASED:        return "CON ALIASING";
    case FLASH_PROBE_INCONCLUSIVE:   return "no concluyente";
    case FLASH_PROBE_READ_ERROR:     return "error de lectura";
    case FLASH_PROBE_NOT_CONFIGURED: return "no aplica (16 MB)";
    default:                         return "no ejecutada";
    }
}

static void refresh(diag_t *d)
{
    char buf[48];
    watch_mem_report_t m;
    watch_diag_mem(&m);
    ui_fmt_bytes(buf, sizeof(buf), m.internal_free);
    watch_theme_info_set(d->ram, buf);
    ui_fmt_bytes(buf, sizeof(buf), m.internal_largest);
    watch_theme_info_set(d->ram_big, buf);
    ui_fmt_bytes(buf, sizeof(buf), m.psram_free);
    watch_theme_info_set(d->psram, buf);

    watch_time_info_t ti;
    watch_time_info(&ti);
    watch_theme_info_set(d->rtc_state, ui_time_state_text(ti.state));
    watch_theme_info_set(d->rtc_detail, ti.reads ? pcf85063_read_text(ti.last_read) : "--");
    snprintf(buf, sizeof(buf), "%lu · errores %lu", (unsigned long)ti.reads, (unsigned long)ti.errors);
    watch_theme_info_set(d->rtc_reads, buf);
    if (ti.reads) {
        snprintf(buf, sizeof(buf), "%02x %02x | %02x %02x %02x %02x %02x %02x %02x", ti.regs[0], ti.regs[1],
                 ti.regs[4], ti.regs[5], ti.regs[6], ti.regs[7], ti.regs[8], ti.regs[9], ti.regs[10]);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_set(d->rtc_regs, buf);

    watch_power_snapshot_t s;
    if (ui_system_power(&s) && s.driver_ok) {
        snprintf(buf, sizeof(buf), "%lu · errores %lu", (unsigned long)s.samples, (unsigned long)s.errors);
        watch_theme_info_set(d->pwr_reads, buf);
        snprintf(buf, sizeof(buf), "%lu µs (máx. %lu)", (unsigned long)s.sample_us_last,
                 (unsigned long)s.sample_us_max);
        watch_theme_info_set(d->pwr_i2c, buf);
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)s.stack_free_min);
        watch_theme_info_set(d->pwr_stack, buf);
    } else {
        watch_theme_info_set(d->pwr_reads, "AXP2101 no disponible");
        watch_theme_info_set(d->pwr_i2c, "--");
        watch_theme_info_set(d->pwr_stack, "--");
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_diag_create(lv_obj_t *parent)
{
    const watch_ui_boot_info_t *bi = ui_boot_info();
    diag_t *d = lv_malloc_zeroed(sizeof(*d));
    LV_ASSERT_MALLOC(d);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Diagnóstico", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    char buf[48];

    lv_obj_t *tt = watch_theme_list_row(list, FLEX_ICON_CHECK, FLEX_C_CAT_BLUE, "Prueba de pantalla", NULL, true);
    lv_obj_add_event_cb(tt, touchtest_cb, LV_EVENT_CLICKED, NULL);
    if (!bi->touch_ok) {
        watch_theme_row_set_value(tt, "Táctil no disponible");
    }

    watch_theme_section(list, "Memoria libre");
    d->ram = watch_theme_info_row(list, "RAM interna", "--");
    d->ram_big = watch_theme_info_row(list, "Bloque mayor interno", "--");
    d->psram = watch_theme_info_row(list, "PSRAM", "--");

    watch_theme_section(list, "Pantalla");
    if (bi->disp_buf_lines) {
        snprintf(buf, sizeof(buf), "%s%u líneas · %s", bi->disp_double_buffer ? "2 × " : "",
                 (unsigned)bi->disp_buf_lines, bi->disp_buf_psram ? "PSRAM" : "RAM DMA");
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_row(list, "Búferes de LVGL", buf);

    watch_theme_section(list, "Flash");
    ui_fmt_bytes(buf, sizeof(buf), bi->flash.configured_size);
    watch_theme_info_row(list, "Declarada", bi->flash.configured_size ? buf : "--");
    if (bi->flash.jedec_id) {
        snprintf(buf, sizeof(buf), "%06lx", (unsigned long)bi->flash.jedec_id);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_row(list, "JEDEC", buf);
    watch_theme_info_row(list, "Sonda > 16 MB", flash_text(bi->flash.verdict));

    watch_theme_section(list, "Reloj (PCF85063, solo lectura)");
    d->rtc_state = watch_theme_info_row(list, "Hora", "--");
    d->rtc_detail = watch_theme_info_row(list, "Última lectura", "--");
    d->rtc_reads = watch_theme_info_row(list, "Lecturas", "--");
    d->rtc_regs = watch_theme_info_row(list, "Registros", "--");

    watch_theme_section(list, "Batería (AXP2101, solo lectura)");
    d->pwr_reads = watch_theme_info_row(list, "Lecturas", "--");
    d->pwr_i2c = watch_theme_info_row(list, "Lectura I2C", "--");
    d->pwr_stack = watch_theme_info_row(list, "Pila libre mínima", "--");

    refresh(d);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, d);
    ui_page_timer(page, tick_cb, DIAG_TICK_MS, d);
    return page;
}
