/*
 * Pruebas de la esfera (B3-2) en la vista previa.
 *
 *  - Con hora válida muestra "H:MM" y la fecha en español; el minuto avanza.
 *  - En reposo (mismo minuto, misma batería) no se redibuja nada.
 *  - Sin hora fiable muestra "--:--" y el motivo, en los tres casos.
 *  - Crear y borrar la esfera no deja temporizadores ni memoria.
 *
 * Se ejecuta después de sim_suite_power(), con ui_system ya arrancado.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"

/* La esfera se prueba en una pantalla propia, fuera de la navegación. */
static lv_obj_t *s_tmp;

static lv_obj_t *show_face(void)
{
    lv_obj_t *old = s_tmp;
    s_tmp = lv_obj_create(NULL);
    lv_obj_t *page = ui_face_create(s_tmp);
    lv_screen_load(s_tmp);
    if (old) {
        lv_obj_delete(old);
    }
    return page;
}

void sim_suite_face(const char *out)
{
    const int f0 = sim_fails;
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);

    /* Hora válida: 15-10-2026 09:41:00 (jueves) */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    lv_obj_t *scr = show_face();
    sim_run(1500);
    sim_save(out, "f3_esfera");
    EXPECT(sim_find_label(scr, "9:41"), "la esfera no muestra 9:41");
    EXPECT(sim_find_label(scr, "Jueves, 15 de octubre"), "la esfera no muestra la fecha");
    EXPECT(sim_find_label(scr, "84 %"), "la esfera no muestra el 84 %%");
    EXPECT(sim_find_label(scr, "Descargando"), "la esfera no muestra el estado de la bateria");

    /* Reposo: durante el resto del minuto no se envía nada a la pantalla. */
    const uint32_t fl = sim_flush_count();
    sim_run(50000);
    EXPECT(sim_flush_count() == fl, "la esfera se redibujo %u veces en reposo", (unsigned)(sim_flush_count() - fl));
    sim_run(10000); /* 9:42:01 */
    EXPECT(sim_find_label(scr, "9:42"), "el minuto no avanzo a 9:42");
    EXPECT(sim_flush_count() > fl, "el cambio de minuto no se dibujo");

    /* Cambio de día a medianoche */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 23, 59, 58);
    sim_run(3000);
    EXPECT(sim_find_label(scr, "0:00"), "no paso a 0:00");
    EXPECT(sim_find_label(scr, "Viernes, 16 de octubre"), "la fecha no cambio a medianoche");

    /* Sin hora fiable: nunca una hora inventada. */
    static const struct {
        watch_time_state_t st;
        const char *text;
        const char *png;
    } cases[] = {
        {WATCH_TIME_STARTING, "Leyendo la hora…", "f3_esfera_leyendo"},
        {WATCH_TIME_NOT_SET, "Hora sin configurar", "f3_esfera_sin_hora"},
        {WATCH_TIME_NO_RTC, "Reloj RTC no disponible", "f3_esfera_sin_rtc"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sim_set_time(cases[i].st, 2026, 10, 15, 9, 41, 0);
        sim_run(1100);
        EXPECT(sim_find_label(scr, "--:--"), "estado %d: falta --:--", (int)cases[i].st);
        EXPECT(sim_find_label(scr, cases[i].text), "estado %d: falta \"%s\"", (int)cases[i].st, cases[i].text);
        EXPECT(!sim_find_label(scr, "9:41"), "estado %d: muestra una hora", (int)cases[i].st);
        if (cases[i].st == WATCH_TIME_NOT_SET) {
            sim_scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
            sim_power.driver_ok = false;
            sim_run(2100); /* ui_system sondea cada 0,5 s y la esfera cada 1 s */
            EXPECT(sim_find_label(scr, "--"), "sin datos de bateria no muestra --");
        }
        sim_save(out, cases[i].png);
    }

    /* Ciclo de vida: crear y borrar la esfera no deja temporizadores ni memoria. */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    show_face();
    sim_run(1100);
    const int timers = sim_timer_count();
    const size_t mem = sim_mem_used();
    const uint32_t blocks = sim_mem_blocks();
    for (int i = 0; i < 20; i++) {
        show_face();
        sim_run(1100);
    }
    EXPECT(sim_timer_count() == timers, "temporizadores: %d antes, %d despues", timers, sim_timer_count());
    EXPECT(sim_mem_blocks() == blocks, "fuga en la esfera: %u bloques -> %u", (unsigned)blocks,
           (unsigned)sim_mem_blocks());
    printf("esfera: %u -> %u bloques de LVGL (%zu B -> %zu B), %d temporizadores\n", (unsigned)blocks,
           (unsigned)sim_mem_blocks(), mem, sim_mem_used(), sim_timer_count());

    lv_screen_load(ui_nav_screen());
    lv_obj_delete(s_tmp);
    s_tmp = NULL;
    printf("esfera B3-2: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
