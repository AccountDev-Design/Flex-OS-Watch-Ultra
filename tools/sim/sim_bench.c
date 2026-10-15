/*
 * Banco de medida del dibujo (solo con SIM_BENCH=1): tiempo de CPU del PC por
 * cuadro en las situaciones que el reloj mostró lentas en el vídeo de la
 * prueba (panel rápido sobre la esfera, desplazar la lista de Batería) y en
 * las animaciones. Son tiempos del PC, no del ESP32-S3: sirven para comparar
 * antes y después de un cambio, no como medida del reloj.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "sim.h"
#include "ui_internal.h"

static double cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Redibujo completo de lo que haya en pantalla, n veces. */
static double full_frames(int n)
{
    const double t0 = cpu_ms();
    for (int i = 0; i < n; i++) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
        lv_refr_now(NULL);
    }
    return (cpu_ms() - t0) / n;
}

void sim_suite_bench(const char *out)
{
    (void)out;
    if (!getenv("SIM_BENCH")) {
        return;
    }
    const int N = 60;
    lv_screen_load(ui_nav_screen());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    ui_nav_home();
    sim_run(600);

    printf("BANCO esfera, cuadro completo: %.2f ms\n", full_frames(N));

    ui_qp_open();
    sim_run(400);
    printf("BANCO panel rapido abierto, cuadro completo: %.2f ms\n", full_frames(N));
    /* Panel a medio bajar (lo que se ve mientras sigue al dedo) */
    lv_obj_set_y(ui_qp_obj(), -FLEX_SCREEN_H / 2);
    printf("BANCO panel rapido a medias, cuadro completo: %.2f ms\n", full_frames(N));
    lv_obj_set_y(ui_qp_obj(), 0);
    ui_qp_close();
    sim_run(400);

    /* Lista de Batería: desplazamiento de 4 px por cuadro */
    ui_nav_push(ui_battery_page_create);
    sim_run(400);
    lv_obj_t *list = lv_obj_get_child(ui_nav_top(), 1);
    double t0 = cpu_ms();
    for (int i = 0; i < N; i++) {
        lv_obj_scroll_to_y(list, (i * 4) % 400, LV_ANIM_OFF);
        lv_refr_now(NULL);
    }
    printf("BANCO desplazar la lista de Bateria (4 px/cuadro): %.2f ms\n", (cpu_ms() - t0) / N);
    ui_nav_home();
    sim_run(400);

    /* Animación de carga: cuadros de su primer 1 s */
    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_power.track.prev_state = BATT_STATE_DISCHARGING;
    sim_power.track.charge_events = 1000;
    sim_run(110);
    t0 = cpu_ms();
    int frames = 0;
    for (int t = 0; t < 1000 && ui_charge_anim_active(); t += 16, frames++) {
        sim_run(16);
        lv_refr_now(NULL);
    }
    printf("BANCO animacion de carga, cuadro medio del primer segundo: %.2f ms\n",
           frames ? (cpu_ms() - t0) / frames : 0.0);
    sim_run(4000);

    /* Transición abrir/volver */
    t0 = cpu_ms();
    frames = 0;
    ui_nav_push(ui_settings_create);
    for (int t = 0; t < 260; t += 16, frames++) {
        sim_run(16);
        lv_refr_now(NULL);
    }
    printf("BANCO transicion de apertura, cuadro medio: %.2f ms\n", (cpu_ms() - t0) / frames);
    ui_nav_home();
    sim_run(400);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(6000);
}
