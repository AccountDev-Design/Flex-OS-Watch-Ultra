/*
 * Pruebas de las páginas de Ajustes (B3-5) en la vista previa: cada página
 * se abre desde su fila, muestra datos reales del escenario (o "--"), se
 * desplaza sin navegar, vuelve con el botón y no deja memoria ni
 * temporizadores. Además, todas las etiquetas de todas las pantallas se
 * pueden dibujar con su fuente (sin glifos que falten).
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

#define ROW_Y(i) (FLEX_HDR_H + 4 + (i) * (FLEX_ROW_H + FLEX_ROW_GAP) + FLEX_ROW_H / 2)
#define BACK_X   (FLEX_HDR_X + FLEX_HDR_ZONE / 2)
#define BACK_Y   (FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2)

static void open_row(int i, const char *title)
{
    sim_tap(205, ROW_Y(i));
    sim_run(300);
    EXPECT(ui_nav_depth() == 3 && sim_find_label(ui_nav_top(), title), "la fila %d no abrio %s", i, title);
}

static void back(void)
{
    sim_tap(BACK_X, BACK_Y);
    sim_run(300);
}

void sim_suite_settings(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    /* Configuración del cargador del escenario (datos de la vista previa) */
    sim_power.cfg_ok = true;
    sim_power.cfg.charge_ma = 200;
    sim_power.cfg.cv_mv = 4200;
    sim_power.cfg.term_ma = 25;
    sim_power.cfg.term_enabled = true;
    sim_power.cfg.precharge_ma = 50;
    sim_power.cfg.input_limit_ma = 500;
    sim_power.cfg.vindpm_mv = 4360;
    sim_power.cfg.ts_affects_charger = true;
    sim_power.samples = 120;
    sim_power.sample_us_last = 900;
    sim_power.sample_us_max = 1400;
    sim_power.stack_free_min = 1100;
    sim_run(1100);
    EXPECT(ui_nav_depth() == 1, "no se empieza en la esfera");
    sim_check_glyphs(ui_nav_top(), "esfera");
    ui_qp_open();
    sim_run(300);
    sim_check_glyphs(ui_qp_obj(), "panel rapido");
    ui_qp_close();
    sim_run(300);

    const int timers0 = sim_timer_count();
    const uint32_t blocks0 = sim_mem_blocks();

    EXPECT(ui_nav_push(ui_settings_create), "no se abrio Ajustes");
    sim_run(1300);
    lv_obj_t *set = ui_nav_top();
    EXPECT(sim_find_label(set, "Brillo 70 %"), "Ajustes no muestra el brillo");
    EXPECT(sim_find_label(set, "84 %"), "Ajustes no muestra la bateria");
    EXPECT(!sim_find_label(set, "Pendiente"), "quedan filas pendientes en Ajustes");
    sim_check_glyphs(set, "ajustes");
    sim_save(out, "f3_ajustes_raiz");

    /* ---- Pantalla ---- */
    open_row(0, "Pantalla");
    sim_save(out, "f3_pantalla");
    sim_check_glyphs(ui_nav_top(), "pantalla");
    EXPECT(sim_find_label(ui_nav_top(), "Pendiente"), "Apagado automatico no esta marcado pendiente");
    /* Brillo desde la página (control a la altura de la sección "Brillo") */
    lv_obj_t *sl = NULL;
    for (lv_obj_t *o = sim_find_label(ui_nav_top(), "70 %"); o; o = lv_obj_get_parent(o)) {
        if (lv_obj_check_type(o, &lv_slider_class)) {
            sl = o;
            break;
        }
    }
    EXPECT(sl, "la pagina Pantalla no tiene control de brillo");
    sim_ws_reset(NULL);
    if (sl) {
        lv_area_t a;
        lv_obj_get_coords(sl, &a);
        sim_drag(a.x1 + 120, (a.y1 + a.y2) / 2, a.x1 + 40, (a.y1 + a.y2) / 2, 6, 20);
        sim_release();
        sim_run(100);
        EXPECT(sim_brightness < 70 && sim_brightness >= 10, "el brillo no bajo desde Pantalla (%d)", sim_brightness);
    }
    const int bright = sim_brightness;
    /* F4.1: se guarda una sola vez, al soltar, con el valor aplicado. */
    watch_settings_t ws;
    watch_settings_get(&ws);
    EXPECT(sim_ws_updates == 1, "el brillo se guardo %d veces (esperado 1, al soltar)", sim_ws_updates);
    EXPECT(ws.brightness == bright, "brillo guardado %d, aplicado %d", ws.brightness, bright);

    /* Fondo: Negro -> Onyx rehace la esfera de debajo y se guarda; y vuelta. */
    lv_obj_t *wrow = NULL;
    for (lv_obj_t *o = sim_find_label(ui_nav_top(), "Negro"); o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
            wrow = o;
            break;
        }
    }
    EXPECT(wrow, "la pagina Pantalla no tiene la fila Fondo");
    if (wrow) {
        lv_obj_t *face0 = ui_nav_page(0);
        lv_obj_send_event(wrow, LV_EVENT_CLICKED, NULL);
        sim_run(100);
        watch_settings_get(&ws);
        EXPECT(ws.wallpaper == FLEX_WALL_ONYX, "el fondo Onyx no se guardo");
        EXPECT(sim_find_label(ui_nav_top(), "Onyx"), "la fila no muestra Onyx");
        EXPECT(ui_nav_page(0) != face0 && lv_obj_get_index(ui_nav_page(0)) == 0, "la esfera no se rehizo debajo");
        EXPECT(lv_obj_has_flag(ui_nav_page(0), LV_OBJ_FLAG_HIDDEN), "la esfera nueva se ve encima de Ajustes");
        lv_obj_send_event(wrow, LV_EVENT_CLICKED, NULL);
        sim_run(100);
        watch_settings_get(&ws);
        EXPECT(ws.wallpaper == FLEX_WALL_BLACK, "no se volvio al fondo negro");
    }
    back();
    sim_run(1100);
    char want[24];
    snprintf(want, sizeof(want), "Brillo %d %%", bright);
    EXPECT(sim_find_label(ui_nav_top(), want), "Ajustes no refresca el brillo (%s)", want);
    sim_brightness_set(70);

    /* ---- Batería ---- */
    open_row(1, "Batería");
    lv_obj_t *bp = ui_nav_top();
    EXPECT(sim_find_label(bp, "4,08 V"), "Bateria no muestra la tension");
    EXPECT(sim_find_label(bp, "Descargando"), "Bateria no muestra el estado");
    EXPECT(sim_find_label(bp, "15 % (rearme 18 %)"), "Bateria no muestra el umbral bajo");
    EXPECT(sim_find_label(bp, "Avisos (umbrales provisionales)"), "los umbrales no se marcan provisionales");
    EXPECT(sim_find_label(bp, "200 mA"), "Bateria no muestra la corriente de carga");
    EXPECT(sim_find_label(bp, "4,20 V"), "Bateria no muestra la tension de carga");
    sim_check_glyphs(bp, "bateria");
    sim_save(out, "f3_bateria");
    /* Desplazar la lista: no navega ni dispara el gesto volver. */
    sim_drag(205, 440, 205, 120, 10, 20);
    sim_release();
    sim_run(900);
    EXPECT(ui_nav_depth() == 3 && ui_nav_top() == bp, "desplazar la lista cambio de pagina");
    lv_obj_t *blist = lv_obj_get_child(bp, 1); /* cabecera, lista */
    EXPECT(blist && lv_obj_get_scroll_y(blist) > 150, "la lista no se desplazo (scroll %d)",
           blist ? (int)lv_obj_get_scroll_y(blist) : -1);
    sim_save(out, "f3_bateria_cargador");
    /* Sin datos del AXP2101: "--", nunca valores inventados. */
    sim_scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
    sim_power.driver_ok = false;
    sim_run(2100);
    EXPECT(!sim_find_label(bp, "4,08 V") && !sim_find_label(bp, "200 mA"), "sin datos sigue mostrando valores");
    back();

    /* ---- Diagnóstico y prueba de pantalla ---- */
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_power.samples = 120;
    open_row(2, "Diagnóstico");
    sim_run(1100);
    lv_obj_t *dp = ui_nav_top();
    EXPECT(sim_find_label(dp, "Correcta"), "Diagnostico no muestra el estado del RTC");
    EXPECT(sim_find_label(dp, "180 KB"), "Diagnostico no muestra la RAM libre");
    EXPECT(sim_find_label(dp, "sin aliasing"), "Diagnostico no muestra la sonda de flash");
    sim_check_glyphs(dp, "diagnostico");
    sim_save(out, "f3_diagnostico");
    sim_tap(205, ROW_Y(0));
    sim_run(300);
    EXPECT(ui_nav_depth() == 4 && sim_find_label(ui_nav_top(), "Prueba de pantalla"), "no se abrio la prueba");
    /* Sin gesto de borde: el borde izquierdo da coordenadas */
    sim_press(2, 300);
    sim_run(60);
    EXPECT(sim_find_label(ui_nav_top(), "x 2  ·  y 300"), "la prueba no muestra el toque en el borde");
    sim_save(out, "f3_prueba_pantalla");
    sim_release();
    sim_run(60);
    EXPECT(ui_nav_depth() == 4, "tocar el borde de la prueba navego");
    sim_check_glyphs(ui_nav_top(), "prueba de pantalla");
    back();
    EXPECT(ui_nav_depth() == 3, "no se volvio de la prueba");
    back();

    /* ---- Acerca de ---- */
    open_row(3, "Acerca de");
    EXPECT(sim_find_label(ui_nav_top(), "0.1.0-b1"), "Acerca de no muestra la version");
    EXPECT(sim_find_label(ui_nav_top(), "8,0 MB"), "Acerca de no muestra la PSRAM");
    sim_check_glyphs(ui_nav_top(), "acerca de");
    sim_save(out, "f3_acerca_de");
    back();

    back();
    EXPECT(ui_nav_depth() == 1 && !ui_nav_busy(), "no se volvio a la esfera");

    /* ---- Fugas: cada página 5 veces ---- */
    static const ui_page_create_t pages[] = {ui_display_page_create, ui_battery_page_create, ui_diag_create,
                                             ui_about_create, ui_touchtest_create};
    for (int r = 0; r < 5; r++) {
        for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
            ui_nav_push(pages[i]);
            sim_run(1300);
            ui_nav_back();
            sim_run(300);
        }
    }
    EXPECT(ui_nav_depth() == 1, "profundidad %d tras los ciclos", ui_nav_depth());
    EXPECT(sim_mem_blocks() == blocks0, "fuga en Ajustes: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    EXPECT(lv_anim_count_running() == 0, "%u animaciones vivas", (unsigned)lv_anim_count_running());
    printf("ajustes: %u -> %u bloques de LVGL, %d temporizadores\n", (unsigned)blocks0, (unsigned)sim_mem_blocks(),
           sim_timer_count());
    printf("ajustes B3-5: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
