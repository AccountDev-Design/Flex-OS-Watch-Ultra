/*
 * Redibujado en reposo (F11): con los datos quietos, ¿cuántos píxeles envía
 * cada pantalla al panel en 5 s? Un refresco periódico que reescribe estilos o
 * reconstruye objetos aunque nada cambie se ve aquí como píxeles enviados; un
 * redibujado completo son 205 820 píxeles (410 x 502).
 *
 * Solo cuenta lo que cambia de verdad (el segundero de la analógica, un
 * cronómetro en marcha...). La lista de páginas es la de las apps y los
 * Ajustes; se permite el área de lo que cambia por diseño.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"

#define FULL_PX (FLEX_SCREEN_W * FLEX_SCREEN_H)

static uint64_t idle_px(uint32_t ms)
{
    sim_run(2000); /* que termine la apertura y las primeras lecturas */
    const uint64_t p0 = sim_flush_pixels();
    sim_run(ms);
    return sim_flush_pixels() - p0;
}

void sim_suite_redraw(const char *out)
{
    (void)out;
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_ws_reset(NULL);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_imu.state = IMU_RUNNING;
    sim_imu.samples = 10;
    sim_imu.acc_mg[2] = 1000;
    sim_run(1500);

    static const struct {
        const char *name;
        ui_page_create_t create;
        uint32_t allowed_px; /* lo que cambia por diseño en 5 s */
    } pages[] = {
        {"esfera", NULL, 0},
        {"cajon", ui_apps_create, 0},
        {"ajustes", ui_settings_create, 0},
        {"pantalla", ui_display_page_create, 0},
        {"bateria", ui_battery_page_create, 0},
        {"device care", ui_devcare_create, 0},
        {"wifi", ui_wifi_create, 0},
        {"flex compass", ui_compass_create, 0},
        {"sensores", ui_sensors_create, 0},
        {"reloj", ui_reloj_create, 0},
        {"calendario", ui_calendar_create, 0},
        {"calculadora", ui_calc_create, 0},
        {"clima", ui_weather_create, 0},
        {"flex phone", ui_flexphone_create, 0},
        {"musica", ui_music_create, 0},
        {"grabadora", ui_recorder_create, 0},
        {"guardar en", ui_dest_create, 0},
        {"microsd", ui_sd_create, 0},
        {"almacenamiento", ui_storage_create, 0},
        {"galeria", ui_gallery_create, 0},
        {"diagnostico", ui_diag_create, FULL_PX / 2}, /* memoria y tiempo encendido en vivo */
        {"acerca de", ui_about_create, 0},
    };
    uint64_t total = 0;
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        /* Cada página empieza en el segundo 0: en sus 7 s no cambia el minuto
         * (si no, la hora de las que la muestran se redibujaría por diseño) */
        sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
        sim_run(100);
        if (pages[i].create) {
            ui_nav_push(pages[i].create);
        }
        const uint64_t px = idle_px(5000);
        total += px;
        printf("  reposo %-15s %8llu px en 5 s (%.2f pantallas)\n", pages[i].name, (unsigned long long)px,
               (double)px / FULL_PX);
        EXPECT(px <= pages[i].allowed_px, "%s: redibuja %llu px en reposo (permitido %u)", pages[i].name,
               (unsigned long long)px, (unsigned)pages[i].allowed_px);
        ui_nav_home();
        sim_run(400);
    }
    /* El panel rápido abierto, quieto */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_run(100);
    ui_qp_open();
    const uint64_t qp = idle_px(5000);
    printf("  reposo %-15s %8llu px en 5 s\n", "panel rapido", (unsigned long long)qp);
    EXPECT(qp == 0, "panel rapido: redibuja %llu px en reposo", (unsigned long long)qp);
    ui_qp_close();
    sim_run(400);
    memset(&sim_imu, 0, sizeof(sim_imu));
    printf("redibujado en reposo: %s (%llu px en total)\n", sim_fails == f0 ? "OK" : "FALLOS",
           (unsigned long long)total);
}
