/*
 * Prueba "mono" (B3-6): 600 acciones pseudoaleatorias REPRODUCIBLES (semilla
 * fija) sobre la interfaz completa:
 *  - toques en cualquier punto, arrastres desde el borde izquierdo y desde
 *    arriba, arrastres verticales, "volver", a veces sin esperar a que
 *    termine la animación anterior;
 *  - cambios de batería que disparan la animación de carga y los avisos, y
 *    cambios del estado de la hora.
 * Cada 10 acciones se suelta el dedo, se espera y se comprueban las
 * invariantes de reposo:
 *  - navegación quieta, una sola página visible y en su sitio, profundidad
 *    1..6;
 *  - panel rápido cerrado o abierto del todo, y solo sobre la esfera;
 *  - sin animaciones vivas salvo las de los avisos y la animación de carga;
 *  - todos los textos visibles tienen glifo.
 * Al final, de vuelta en la esfera y sin avisos: los mismos bloques de
 * memoria y temporizadores que al principio.
 */
#include <stdio.h>
#include <stdlib.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

#define STEPS 600

static uint32_t s_rng = 0x2026A15u;

static uint32_t rnd(uint32_t n)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) % n;
}

static uint32_t s_charge_events;
static struct {
    int rests, deep, qp_open, overlays, max_depth;
} s_cov;

static void battery_event(void)
{
    switch (rnd(6)) {
    case 0:
        sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
        break;
    case 1: /* inicio de carga confirmado: animación */
        sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
        sim_power.track.prev_state = BATT_STATE_DISCHARGING;
        sim_power.track.charge_events = ++s_charge_events;
        return;
    case 2:
        sim_scenario(12, 3640, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, false);
        break;
    case 3:
        sim_scenario(4, 3450, BATT_STATE_DISCHARGING, BATT_LEVEL_CRITICAL, false);
        break;
    case 4:
        sim_scenario(98, 4180, BATT_STATE_FULL, BATT_LEVEL_NORMAL, true);
        break;
    default:
        sim_scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
        sim_power.driver_ok = false;
        break;
    }
    sim_power.track.charge_events = s_charge_events;
}

static void check_rest(int step)
{
    s_cov.rests++;
    s_cov.deep += ui_nav_depth() > 1;
    s_cov.qp_open += ui_qp_is_open();
    s_cov.overlays += ui_banner_active() || ui_charge_anim_active();
    if (ui_nav_depth() > s_cov.max_depth) {
        s_cov.max_depth = ui_nav_depth();
    }
    char where[48];
    snprintf(where, sizeof(where), "paso %d", step);
    EXPECT(!ui_nav_busy(), "%s: navegacion ocupada en reposo", where);
    const int depth = ui_nav_depth();
    EXPECT(depth >= 1 && depth <= 6, "%s: profundidad %d", where, depth);
    lv_obj_t *scr = ui_nav_screen();
    int visible = 0;
    for (int i = 0; i < depth; i++) {
        visible += !lv_obj_has_flag(ui_nav_page(i), LV_OBJ_FLAG_HIDDEN);
    }
    EXPECT(visible == 1, "%s: %d paginas visibles", where, visible);
    const int overlays = ui_qp_active() + ui_charge_anim_active() + ui_aod_active();
    EXPECT((int)lv_obj_get_child_count(scr) == depth + overlays + ui_nav_suspended_count(),
           "%s: %u hijos para profundidad %d (%d suspendidas)", where, (unsigned)lv_obj_get_child_count(scr), depth,
           ui_nav_suspended_count());
    /* las capas globales siempre encima de las páginas */
    for (int i = 0; i < depth; i++) {
        EXPECT(lv_obj_get_index(ui_nav_page(i)) == i, "%s: la pagina %d no esta en su sitio", where, i);
    }
    EXPECT(lv_obj_get_x(ui_nav_top()) == 0, "%s: pagina desplazada (x = %d)", where, (int)lv_obj_get_x(ui_nav_top()));
    EXPECT(!ui_qp_active() || ui_qp_is_open(), "%s: panel rapido a medias", where);
    EXPECT(!ui_qp_active() || lv_obj_get_index(ui_qp_obj()) >= depth, "%s: panel rapido debajo de una pagina", where);
    if (!ui_banner_active() && !ui_charge_anim_active()) {
        EXPECT(lv_anim_count_running() == 0, "%s: %u animaciones vivas", where, (unsigned)lv_anim_count_running());
    }
    if (sim_missing_glyphs(ui_nav_screen(), where) + sim_missing_glyphs(lv_layer_top(), where) != 0) {
        EXPECT(0, "%s: textos sin glifo", where);
    }
}

/* Superposiciones durante la navegación, de forma determinista: el aviso de
 * batería baja aparece encima del panel rápido abierto y la animación de
 * carga encima de una página; las dos se cierran con un toque o solas y lo
 * de debajo sigue funcionando. */
static void overlays_during_nav(const char *out)
{
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_power.track.charge_events = s_charge_events;
    /* El aviso bajo ya sonó en sim_suite_power(): se deja pasar su
     * enfriamiento de 10 min (BATT_ALERT_COOLDOWN_MS) para que vuelva a sonar. */
    sim_run(BATT_ALERT_COOLDOWN_MS + 1000);
    ui_qp_open();
    sim_run(300);
    EXPECT(ui_qp_is_open(), "superposiciones: el panel no se abrio");
    sim_scenario(12, 3640, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, false);
    sim_power.track.charge_events = s_charge_events;
    sim_run(800);
    EXPECT(ui_banner_active(), "superposiciones: el aviso no aparecio con el panel abierto");
    /* El aviso vive en lv_layer_top(), que LVGL dibuja siempre encima de la
     * pantalla (y del panel, que es hija de ella). */
    lv_obj_t *top = lv_layer_top();
    EXPECT(lv_obj_get_child_count(top) == 1 && ui_qp_obj() && lv_obj_get_screen(ui_qp_obj()) == ui_nav_screen(),
           "superposiciones: el aviso no esta encima del panel");
    sim_check_glyphs(top, "aviso sobre el panel");
    sim_save(out, "f3_aviso_sobre_panel");
    sim_tap(205, 58); /* toque en el aviso: se cierra */
    sim_run(400);
    EXPECT(!ui_banner_active(), "superposiciones: el aviso no se cerro con un toque");
    EXPECT(ui_qp_is_open(), "superposiciones: cerrar el aviso cerro el panel");
    sim_tap(205, FLEX_SCREEN_H - 20);
    sim_run(400);
    EXPECT(!ui_qp_active(), "superposiciones: el panel no se cerro tras el aviso");

    /* Un aviso que ABRE algo (OTA) tocado con el panel abierto: el panel se
     * cierra y la página queda arriba, no debajo del panel (lo encontró el
     * mono en la ejecución completa, paso 412). */
    ui_qp_open();
    sim_run(300);
    ui_banner_show_ota("Actualización disponible", "Toca para ver las novedades", FLEX_C_TXT, true);
    sim_run(800);
    EXPECT(ui_qp_is_open() && ui_banner_active(), "superposiciones: falta el panel o el aviso OTA");
    sim_tap(205, 58);
    sim_run(900);
    EXPECT(!ui_qp_active() && ui_nav_depth() == 2, "superposiciones: aviso OTA con el panel: panel %d, profundidad %d",
           ui_qp_active(), ui_nav_depth());
    ui_nav_home();
    sim_run(400);

    /* Animación de carga encima de Ajustes > Batería */
    ui_nav_push(ui_settings_create);
    sim_run(300);
    ui_nav_push(ui_battery_page_create);
    sim_run(300);
    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_power.track.prev_state = BATT_STATE_DISCHARGING;
    sim_power.track.charge_events = ++s_charge_events;
    sim_run(700);
    EXPECT(ui_charge_anim_active(), "superposiciones: no hubo animacion de carga sobre la pagina");
    sim_check_glyphs(ui_charge_anim_obj(), "animacion de carga");
    sim_save(out, "f3_carga_sobre_pagina");
    sim_tap(205, 251); /* un toque la cierra */
    sim_run(400);
    EXPECT(!ui_charge_anim_active(), "superposiciones: la animacion no se cerro con un toque");
    EXPECT(ui_nav_depth() == 3 && sim_find_label(ui_nav_top(), "Cargando"),
           "superposiciones: la pagina de bateria no sigue debajo con el estado nuevo");
    ui_nav_home();
    sim_run(400);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_power.track.charge_events = s_charge_events;
    sim_run(6000);
    EXPECT(ui_nav_depth() == 1 && lv_obj_get_child_count(lv_layer_top()) == 0 && !ui_charge_anim_active() &&
               !ui_qp_active(),
           "superposiciones: no queda limpio");
}

void sim_suite_monkey(const char *out)
{
    (void)out;
    const int f0 = sim_fails;
    const char *seed = getenv("SIM_SEED");
    if (seed) {
        s_rng = (uint32_t)strtoul(seed, NULL, 0);
    }
    lv_screen_load(ui_nav_screen());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    s_charge_events = 0;
    sim_power.track.charge_events = 0;
    sim_run(6000); /* que terminen avisos de baterías anteriores */
    overlays_during_nav(out);
    EXPECT(ui_nav_depth() == 1 && !ui_qp_active(), "la prueba no empieza en la esfera");
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();
    int counts[9] = {0};

    for (int step = 1; step <= STEPS; step++) {
        const uint32_t a = rnd(100);
        if (a < 25) { /* toque en cualquier punto */
            counts[0]++;
            sim_tap((int32_t)rnd(SIM_W), (int32_t)rnd(SIM_H));
        } else if (a < 30) { /* deslizar hacia abajo en el centro (en la esfera abre el cajón) */
            counts[8]++;
            const int32_t x = 90 + (int32_t)rnd(230);
            const int32_t y0 = 90 + (int32_t)rnd(200);
            sim_drag(x, y0, x + (int32_t)rnd(30) - 15, y0 + 60 + (int32_t)rnd(150), 2 + (int)rnd(8), 10 + rnd(30));
            sim_release();
        } else if (a < 44) { /* bordes laterales: derecho = atrás, izquierdo = al cajón */
            counts[1]++;
            const int32_t y = FLEX_HDR_H + 10 + (int32_t)rnd(SIM_H - FLEX_HDR_H - 50);
            if (rnd(2)) {
                sim_drag((int32_t)rnd(18), y, 30 + (int32_t)rnd(370), y + (int32_t)rnd(40) - 20, 2 + (int)rnd(10),
                         10 + rnd(30));
            } else {
                const int32_t x0 = SIM_W - 1 - (int32_t)rnd(18);
                sim_drag(x0, y, x0 - 30 - (int32_t)rnd(370), y + (int32_t)rnd(40) - 20, 2 + (int)rnd(10),
                         10 + rnd(30));
            }
            sim_release();
        } else if (a < 58) { /* desde arriba hacia abajo (panel rápido) */
            counts[2]++;
            const int32_t x = 20 + (int32_t)rnd(370);
            sim_drag(x, (int32_t)rnd(50), x + (int32_t)rnd(60) - 30, 40 + (int32_t)rnd(440), 2 + (int)rnd(10),
                     10 + rnd(30));
            sim_release();
        } else if (a < 70) { /* hacia arriba desde cualquier altura (desde abajo: a la esfera) */
            counts[3]++;
            const int32_t x = 20 + (int32_t)rnd(370);
            const int32_t y0 = rnd(2) ? SIM_H - 1 - (int32_t)rnd(25) : 100 + (int32_t)rnd(400);
            sim_drag(x, y0, x, (int32_t)rnd((uint32_t)y0), 2 + (int)rnd(10), 10 + rnd(30));
            sim_release();
        } else if (a < 80) { /* botón volver de la cabecera */
            counts[4]++;
            sim_tap(FLEX_HDR_X + FLEX_HDR_ZONE / 2, FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2);
        } else if (a < 85) { /* abrir una página sin esperar (si el panel no está) */
            counts[5]++;
            if (!ui_qp_active()) {
                static const ui_page_create_t pages[] = {
                    ui_settings_create, ui_display_page_create, ui_battery_page_create, ui_diag_create,
                    ui_about_create,    ui_apps_create,         ui_stopwatch_create,    ui_timer_create,
                    ui_alarms_create,   ui_datetime_create,     ui_tz_create,           ui_timeset_create,
                    ui_reloj_create,    ui_calc_create,         ui_calendar_create,
                    ui_compass_create,  ui_devcare_create,      ui_wifi_create,         ui_wifi_password_create,
                    ui_flexphone_create, ui_notifs_create,      ui_notif_detail_create, ui_notif_reply_create,
                    ui_music_create,    ui_phone_conn_create,   ui_phone_state_create,
                    ui_notes_create,    ui_note_edit_create,    ui_files_create,        ui_file_opts_create,
                    ui_file_rename_create, ui_paint_create,     ui_paint_canvas_create,
                    ui_weather_create,  ui_wx_locs_create,      ui_wx_search_create,    ui_browser_create,
                    ui_ota_create,      ui_ota_install_create,  ui_phone_relay_create,
                    ui_face_editor_create, ui_sensors_create,
                };
                ui_nav_push(pages[rnd(sizeof(pages) / sizeof(pages[0]))]);
            }
        } else if (a < 92) { /* batería: carga, avisos, sin datos */
            counts[6]++;
            battery_event();
        } else if (a < 95) { /* estado de la hora */
            counts[7]++;
            static const watch_time_state_t st[] = {WATCH_TIME_OK, WATCH_TIME_NOT_SET, WATCH_TIME_NO_RTC,
                                                    WATCH_TIME_STARTING};
            sim_set_time(st[rnd(4)], 2026, 10, 15, 23, 59, (int)rnd(60));
        }
        /* A veces la siguiente acción llega en mitad de la animación. */
        sim_run(rnd(4) == 0 ? rnd(120) : 150 + rnd(400));
        if (getenv("SIM_TRACE")) { /* depurar un fallo: acción y estado tras cada paso */
            printf("  paso %d: accion %u profundidad %d panel %d/%d aviso %d\n", step, (unsigned)a, ui_nav_depth(),
                   ui_qp_active(), ui_qp_is_open(), ui_banner_active());
        }

        if (step % 10 == 0) {
            sim_release();
            sim_run(700);
            /* Algunas páginas vuelven solas (p. ej. tras guardar la hora):
             * en reposo es cuando ya no queda nada en marcha. */
            for (int k = 0; k < 20 && ui_nav_busy(); k++) {
                sim_run(100);
            }
            check_rest(step);
        }
    }

    /* Vuelta a la esfera, sin avisos: no queda nada de más. */
    sim_release();
    sim_run(500);
    if (ui_qp_active()) {
        ui_qp_close();
        sim_run(400);
    }
    /* Lo que hayan dejado las apps de tiempo: aviso, temporizador,
     * cronómetro y alarmas creadas al azar. */
    ui_ring_dismiss();
    tmr_cancel(ui_tmr());
    sw_reset(ui_sw());
    const alarm_set_t none = {0};
    ui_alarms_set(&none);
    ui_nav_home();
    sim_run(400);
    ui_nav_discard_suspended(); /* la app que quedara suspendida con el gesto hacia arriba */
    if (ui_psave_active()) {
        ui_psave_set(false); /* un toque en el panel pudo activar el ahorro */
        sim_run(400);
    }
    /* Los toques pueden haber cambiado el fondo (Ajustes > Pantalla): se
     * vuelve al inicial para comparar la memoria con la misma esfera. */
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.wallpaper != FLEX_WALL_BLACK) {
        ws.wallpaper = FLEX_WALL_BLACK;
        watch_settings_update(&ws);
        watch_theme_set_wallpaper(FLEX_WALL_BLACK);
        ui_nav_replace_root(ui_face_create);
    }
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_power.track.charge_events = s_charge_events;
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_run(6000);
    check_rest(STEPS + 1);
    EXPECT(ui_nav_depth() == 1 && !ui_qp_active(), "no se volvio a la esfera");
    EXPECT(!ui_banner_active() && !ui_charge_anim_active(), "quedan avisos activos");
    EXPECT(lv_obj_get_child_count(lv_layer_top()) == 0, "quedan objetos en layer_top");
    EXPECT(sim_mem_blocks() == blocks0, "fuga tras %d acciones: %u -> %u bloques", STEPS, (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("mono: %d acciones (toques %d, izquierda %d, borde %d, desde arriba %d, hacia arriba %d, volver %d, "
           "abrir %d, bateria %d, hora %d); %u -> %u bloques\n",
           STEPS, counts[0], counts[8], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
           (unsigned)blocks0, (unsigned)sim_mem_blocks());
    printf("mono: en reposo %d veces: en una pagina %d (profundidad maxima %d), panel abierto %d, "
           "con aviso o animacion de carga %d\n",
           s_cov.rests, s_cov.deep, s_cov.max_depth, s_cov.qp_open, s_cov.overlays);
    printf("mono B3-6: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
