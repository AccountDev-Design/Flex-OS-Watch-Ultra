/*
 * ui_about — Ajustes > Acerca de: versión del firmware y datos del equipo.
 * Las cifras de memoria y flash son las medidas en este arranque; los datos
 * fijos de la placa son los verificados en la auditoría de la Fase 0.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "ui_logic.h"
#include "watch_theme.h"

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

lv_obj_t *ui_about_create(lv_obj_t *parent)
{
    const watch_ui_boot_info_t *bi = ui_boot_info();
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Acerca de", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    char buf[40];

    watch_theme_section(list, "Software");
    watch_theme_info_row(list, "Sistema", "Flex OS Watch Ultra");
    watch_theme_info_row(list, "Versión", bi->fw_version ? bi->fw_version : "--");
    watch_theme_info_row(list, "ESP-IDF", bi->idf_version ? bi->idf_version : "--");
    snprintf(buf, sizeof(buf), "%d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());
    watch_theme_info_row(list, "LVGL", buf);

    watch_theme_section(list, "Equipo");
    watch_theme_info_row(list, "Placa", "Waveshare ESP32-S3 Touch AMOLED 2.06");
    watch_theme_info_row(list, "Pantalla", "AMOLED 410 × 502");
    watch_mem_report_t m;
    watch_diag_mem(&m);
    if (m.psram_total > 0) {
        ui_fmt_bytes(buf, sizeof(buf), m.psram_total);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_row(list, "PSRAM (heap)", buf);
    if (bi->flash.configured_size > 0) {
        ui_fmt_bytes(buf, sizeof(buf), bi->flash.configured_size);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    watch_theme_info_row(list, "Flash", buf);
    return page;
}
