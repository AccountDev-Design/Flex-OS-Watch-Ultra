/*
 * Pruebas del panel rápido (B3-4) y de la raíz de Ajustes en la vista previa,
 * con el dedo simulado: abrir siguiendo al dedo, controles pendientes sin
 * función, brillo real limitado, cerrar (arrastre y asa), Ajustes, y que no
 * quede nada vivo al cerrarlo.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

/* Geometría del panel (ui_qp.c) usada para tocar sus controles */
#define T_W    ((FLEX_SCREEN_W - 32 - 12) / 2)
#define T_H    88
#define T_Y0   70
#define T_Y1   (T_Y0 + T_H + 12)
#define T_Y2   (T_Y1 + T_H + 12)
#define SL_Y   (T_Y2 + T_H + 14)

static void expect_closed(const char *when)
{
    EXPECT(!ui_qp_active(), "%s: el panel sigue activo", when);
    EXPECT(lv_obj_get_child_count(lv_layer_top()) == 0, "%s: quedan objetos en layer_top", when);
    EXPECT(lv_anim_count_running() == 0, "%s: %u animaciones vivas", when, (unsigned)lv_anim_count_running());
}

void sim_suite_qp(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1100);
    EXPECT(ui_nav_depth() == 1, "no se empieza en la esfera");
    const int timers0 = sim_timer_count();
    sim_save(out, "f3_esfera_asa");

    /* ---- Toque y arrastre horizontal en la franja: no abren ---- */
    sim_tap(205, 20);
    sim_run(300);
    expect_closed("tras tocar la franja");
    sim_drag(100, 20, 330, 40, 8, 20);
    sim_release();
    sim_run(300);
    expect_closed("tras arrastre horizontal en la franja");

    /* ---- Abrir siguiendo al dedo ---- */
    sim_drag(205, 20, 205, 260, 12, 25); /* lento: 240 px en 325 ms */
    EXPECT(ui_qp_active() && !ui_qp_is_open(), "el panel no sigue al dedo");
    lv_obj_t *qp = ui_qp_obj();
    const int32_t y = qp ? lv_obj_get_y(qp) : 0;
    EXPECT(qp && y > -FLEX_SCREEN_H + 200 && y < -FLEX_SCREEN_H + 260, "cortina en y = %d", (int)y);
    sim_save(out, "f3_panel_arrastre");
    sim_release();
    sim_run(300);
    EXPECT(ui_qp_is_open(), "el panel no termino de abrirse");
    EXPECT(lv_obj_get_y(ui_qp_obj()) == 0, "el panel abierto no esta en y = 0");
    EXPECT(lv_anim_count_running() == 0, "animaciones vivas con el panel abierto");
    sim_save(out, "f3_panel");
    qp = ui_qp_obj();
    EXPECT(sim_find_label(qp, "9:41"), "el panel no muestra la hora");
    EXPECT(sim_find_label(qp, "jue, 15 oct"), "el panel no muestra la fecha");
    EXPECT(sim_find_label(qp, "84 %"), "el panel no muestra la bateria");
    EXPECT(sim_find_label(qp, "Descargando"), "el panel no muestra el estado de la bateria");
    EXPECT(sim_find_label(qp, "70 %"), "el panel no muestra el brillo actual");

    /* ---- Wi-Fi y Bluetooth: pendientes, sin función ---- */
    sim_tap(16 + T_W / 2, T_Y0 + 44);
    sim_tap(16 + T_W + 12 + T_W / 2, T_Y0 + 44);
    sim_run(300);
    EXPECT(ui_qp_is_open() && ui_nav_depth() == 1, "un mosaico pendiente hizo algo");

    /* ---- Brillo: real, limitado al arrastrar, mínimo 10 % ---- */
    int calls = sim_brightness_calls;
    sim_drag(120, SL_Y + 32, 330, SL_Y + 32, 12, 15); /* 195 ms arrastrando */
    const int during = sim_brightness_calls - calls;
    sim_release();
    sim_run(100);
    EXPECT(during >= 1 && during <= 195 / 60 + 2, "%d comandos de brillo durante el arrastre", during);
    EXPECT(sim_brightness > 70, "el brillo no subio (%d)", sim_brightness);
    char val[16];
    snprintf(val, sizeof(val), "%d %%", sim_brightness);
    EXPECT(sim_find_label(ui_qp_obj(), val), "el control no muestra el brillo aplicado (%s)", val);
    sim_tap(18, SL_Y + 32); /* extremo izquierdo */
    sim_run(100);
    EXPECT(sim_brightness == 10, "el brillo bajo de 10 %% o no se aplico (%d)", sim_brightness);
    EXPECT(ui_qp_is_open(), "el control de brillo cerro el panel");
    calls = sim_brightness_calls;
    sim_tap(18, SL_Y + 32);
    sim_run(100);
    EXPECT(sim_brightness_calls == calls, "se reenvio el mismo brillo");
    sim_brightness_set(70);

    /* ---- Ahorro de energía y AOD: estado real de sus servicios ---- */
    sim_tap(16 + T_W / 2, T_Y2 + 44);
    sim_run(300);
    EXPECT(ui_psave_active(), "el mosaico Ahorro no activo Flex Ultra Power Saving");
    EXPECT(sim_brightness <= UI_PSAVE_BRIGHT, "en ahorro el brillo fisico no bajo (%d)", sim_brightness);
    EXPECT(sim_find_label(ui_qp_obj(), "No con ahorro"), "el AOD no se marca no disponible con el ahorro");
    sim_save(out, "f3_panel_ahorro");
    sim_tap(16 + T_W / 2, T_Y2 + 44);
    sim_run(300);
    EXPECT(!ui_psave_active() && sim_brightness == 70, "al salir del ahorro no se restauro el brillo (%d)",
           sim_brightness);
    sim_tap(16 + T_W + 12 + T_W / 2, T_Y2 + 44);
    sim_run(300);
    EXPECT(ui_aod_enabled(), "el mosaico AOD no lo activo");
    sim_tap(16 + T_W + 12 + T_W / 2, T_Y2 + 44);
    sim_run(300);
    EXPECT(!ui_aod_enabled(), "el mosaico AOD no lo desactivo");
    sim_brightness_set(70);

    /* ---- Cerrar arrastrando hacia arriba desde el asa ---- */
    sim_drag(205, FLEX_SCREEN_H - 20, 205, 150, 12, 20);
    sim_release();
    sim_run(300);
    expect_closed("tras cerrar arrastrando");

    /* ---- Cerrar con el asa ---- */
    ui_qp_open();
    sim_run(300);
    EXPECT(ui_qp_is_open(), "ui_qp_open no abrio el panel");
    sim_tap(205, FLEX_SCREEN_H - 20);
    sim_run(300);
    expect_closed("tras tocar el asa");

    /* ---- Arrastre corto hacia abajo: se cancela ---- */
    sim_drag(205, 20, 205, 90, 8, 30);
    sim_release();
    sim_run(300);
    expect_closed("tras un arrastre corto");

    /* ---- Ajustes: cierra el panel y abre la página ---- */
    ui_qp_open();
    sim_run(300);
    sim_tap(16 + T_W + 12 + T_W / 2, T_Y1 + 44);
    sim_run(600);
    expect_closed("tras abrir Ajustes");
    EXPECT(ui_nav_depth() == 2 && !ui_nav_busy(), "Ajustes no se abrio (profundidad %d)", ui_nav_depth());
    EXPECT(sim_find_label(ui_nav_top(), "Ajustes"), "la pagina abierta no es Ajustes");
    sim_save(out, "f3_ajustes");

    /* Volver a la esfera */
    sim_tap(FLEX_HDR_X + FLEX_HDR_ZONE / 2, FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2);
    sim_run(300);
    EXPECT(ui_nav_depth() == 1 && !ui_nav_busy(), "no se volvio a la esfera (profundidad %d)", ui_nav_depth());

    /* ---- Mosaico de batería: abre Ajustes > Batería ---- */
    ui_qp_open();
    sim_run(300);
    sim_tap(16 + T_W / 2, T_Y1 + 44);
    sim_run(600);
    expect_closed("tras abrir Bateria");
    EXPECT(ui_nav_depth() == 2 && sim_find_label(ui_nav_top(), "Batería"), "el mosaico no abrio Bateria");
    sim_tap(FLEX_HDR_X + FLEX_HDR_ZONE / 2, FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2);
    sim_run(300);
    EXPECT(ui_nav_depth() == 1, "no se volvio desde Bateria");

    /* ---- Fugas: abrir y cerrar 20 veces ---- */
    const uint32_t blocks = sim_mem_blocks();
    for (int i = 0; i < 20; i++) {
        sim_drag(205, 20, 205, 300, 8, 20);
        sim_release();
        sim_run(300);
        if (i % 2) {
            sim_tap(205, FLEX_SCREEN_H - 20);
        } else {
            sim_drag(205, FLEX_SCREEN_H - 20, 205, 100, 8, 20);
            sim_release();
        }
        sim_run(300);
    }
    expect_closed("tras 20 ciclos");
    EXPECT(sim_mem_blocks() == blocks, "fuga en el panel: %u -> %u bloques", (unsigned)blocks,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("panel rapido: %u -> %u bloques de LVGL, %d temporizadores\n", (unsigned)blocks,
           (unsigned)sim_mem_blocks(), sim_timer_count());

    printf("panel rapido B3-4: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
