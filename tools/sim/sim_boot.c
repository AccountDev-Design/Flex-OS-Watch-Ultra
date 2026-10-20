/*
 * Pruebas del arranque (B3-3) en la vista previa: el splash aparece animado
 * mientras "arrancan" los servicios, el logotipo se ve al menos 3 s (no un
 * destello) y la esfera lo sustituye sin más espera; no quedan animaciones
 * ni pantallas sueltas.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_ui.h"

void sim_suite_boot(const char *out)
{
    const int f0 = sim_fails;
    sim_set_time(WATCH_TIME_STARTING, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);

    lv_obj_t *before = lv_screen_active();
    watch_ui_boot_splash();
    EXPECT(ui_splash_active(), "el splash no se creo");
    lv_obj_t *splash = lv_screen_active();
    EXPECT(splash != before, "el splash no se cargo");

    /* Mientras app_main arranca servicios, el splash se sigue animando. */
    const uint32_t f1 = sim_flush_count();
    sim_run(300);
    EXPECT(sim_flush_count() > f1 + 3, "el splash no se anima (%u volcados en 300 ms)",
           (unsigned)(sim_flush_count() - f1));
    EXPECT(lv_anim_count_running() == 1, "%u animaciones en el splash", (unsigned)lv_anim_count_running());
    sim_save(out, "f3_splash");

    /* Interfaz lista a los 300 ms: el texto se apaga desde donde esté. */
    watch_ui_boot_info_t boot = {0};
    boot.touch_ok = true;
    boot.power_ok = true;
    boot.flash.configured_size = 32u * 1024 * 1024;
    boot.flash.verdict = FLASH_PROBE_UPPER_DISTINCT;
    boot.brightness_get = sim_brightness_get;
    boot.brightness_set = sim_brightness_set;
    boot.display_power = sim_display_power_cb;
    boot.display_level = sim_display_level_cb;
    boot.button_level = sim_button_level;
    boot.fw_version = "0.1.0-b1"; /* como version.txt */
    boot.idf_version = "v5.5.5";
    boot.disp_buf_lines = 40;     /* ejemplo para la vista previa, no medido */
    boot.disp_buf_bytes = 410 * 40 * 2;
    boot.disp_double_buffer = true;
    watch_ui_home_create(&boot);
    /* Las demás baterías no esperan a que la pantalla se apague: el apagado
     * lo prueba sim_screen.c, que lo activa. */
    ui_screen_enable(false);
    EXPECT(lv_screen_active() == splash, "la esfera corto el splash sin transicion");
    /* Arranque rápido (300 ms): el logotipo sigue hasta los 3 s. */
    sim_run(2400);
    EXPECT(ui_splash_active() && lv_screen_active() == splash, "el logotipo desaparecio antes de 3 s");
    sim_run(3000 - 2700);
    sim_run(SIM_SPLASH_OUT_MAX_MS);
    EXPECT(!ui_splash_active(), "el splash sigue activo a los 3 s + %u ms", (unsigned)SIM_SPLASH_OUT_MAX_MS);
    EXPECT(lv_screen_active() == ui_nav_screen(), "no se cargo la pantalla de navegacion");
    EXPECT(ui_nav_depth() == 1, "profundidad %d al arrancar", ui_nav_depth());
    sim_run(100);
    EXPECT(lv_anim_count_running() == 0, "%u animaciones tras el arranque", (unsigned)lv_anim_count_running());
    EXPECT(sim_find_label(ui_nav_top(), "Leyendo la hora…"), "la esfera no muestra el estado inicial");
    sim_save(out, "f3_arranque_esfera");

    /* El RTC responde después: la esfera se actualiza sola. */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_run(1100);
    EXPECT(sim_find_label(ui_nav_top(), "9:41"), "la esfera no paso a la hora real");

    lv_obj_delete(before); /* pantalla por defecto de LVGL, ya sin uso */
    printf("arranque B3-3: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
