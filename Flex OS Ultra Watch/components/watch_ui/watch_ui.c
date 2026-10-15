/*
 * watch_ui — arranque de la interfaz. Ver include/watch_ui.h
 */
#include "watch_ui.h"

#include "ui_internal.h"
#include "watch_theme.h"

static watch_ui_boot_info_t s_boot;

const watch_ui_boot_info_t *ui_boot_info(void)
{
    return &s_boot;
}

void watch_ui_home_create(const watch_ui_boot_info_t *info)
{
    s_boot = *info;
    watch_theme_init();
    ui_system_start(info->power_ok);
    lv_screen_load(ui_face_create());
}
