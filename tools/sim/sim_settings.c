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

/* Abre la fila de Ajustes con ese título (la desplaza a la vista si hace
 * falta) y comprueba que la página abierta lleva el mismo título. */
static void open_row(int i, const char *title)
{
    lv_obj_t *row = sim_find_label(ui_nav_top(), title);
    while (row && !lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE)) {
        row = lv_obj_get_parent(row);
    }
    EXPECT(row, "no hay fila %s", title);
    if (!row) {
        return;
    }
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    sim_run(50);
    lv_area_t a;
    lv_obj_get_coords(row, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
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
    EXPECT(sim_find_label(ui_nav_top(), "15 s"), "Pantalla no muestra el apagado de 15 s (F4.5)");
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

    /* Fondo (F3.6): la fila abre el selector con los 9 fondos; elegir uno
     * rehace la esfera de debajo y se guarda; y vuelta al negro. */
    lv_obj_t *wrow = NULL;
    for (lv_obj_t *o = sim_find_label(ui_nav_top(), "Negro"); o; o = lv_obj_get_parent(o)) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
            wrow = o;
            break;
        }
    }
    EXPECT(wrow, "la pagina Pantalla no tiene la fila Fondo");
    if (wrow) {
        lv_obj_send_event(wrow, LV_EVENT_CLICKED, NULL);
        sim_run(400);
        EXPECT(sim_find_label(ui_nav_top(), "Fondo de pantalla"), "la fila Fondo no abre el selector");
        for (int i = 0; i < FLEX_WALL_COUNT_; i++) {
            EXPECT(sim_find_label(ui_nav_top(), watch_theme_wallpaper_name((flex_wall_t)i)),
                   "el selector no ofrece %s", watch_theme_wallpaper_name((flex_wall_t)i));
        }
        sim_lint("selector de fondos");
        sim_check_glyphs(ui_nav_top(), "selector de fondos");
        sim_save(out, "f3_fondos");
        lv_obj_t *face0 = ui_nav_page(0);
        lv_obj_t *aur = sim_find_label(ui_nav_top(), "Aurora");
        while (aur && !lv_obj_has_flag(aur, LV_OBJ_FLAG_CLICKABLE)) {
            aur = lv_obj_get_parent(aur);
        }
        EXPECT(aur, "el selector no tiene la celda Aurora");
        if (aur) {
            lv_obj_send_event(aur, LV_EVENT_CLICKED, NULL);
        }
        sim_run(100);
        watch_settings_get(&ws);
        EXPECT(ws.wallpaper == FLEX_WALL_AURORA, "el fondo Aurora no se guardo (%d)", ws.wallpaper);
        EXPECT(ui_nav_page(0) != face0 && lv_obj_get_index(ui_nav_page(0)) == 0, "la esfera no se rehizo debajo");
        EXPECT(lv_obj_has_flag(ui_nav_page(0), LV_OBJ_FLAG_HIDDEN), "la esfera nueva se ve encima de Ajustes");
        sim_save(out, "f3_fondos_aurora");
        lv_obj_t *blk = sim_find_label(ui_nav_top(), "Negro");
        while (blk && !lv_obj_has_flag(blk, LV_OBJ_FLAG_CLICKABLE)) {
            blk = lv_obj_get_parent(blk);
        }
        if (blk) {
            lv_obj_send_event(blk, LV_EVENT_CLICKED, NULL);
        }
        sim_run(100);
        watch_settings_get(&ws);
        EXPECT(ws.wallpaper == FLEX_WALL_BLACK, "no se volvio al fondo negro");
        back();
        sim_run(600);
        EXPECT(sim_find_label(ui_nav_top(), "Negro"), "Pantalla no muestra el fondo actual al volver");
    }
    back();
    sim_run(1100);
    char want[24];
    snprintf(want, sizeof(want), "Brillo %d %%", bright);
    EXPECT(sim_find_label(ui_nav_top(), want), "Ajustes no refresca el brillo (%s)", want);
    sim_brightness_set(70);

    /* ---- Almacenamiento (F4.6): bloqueado, y con la puerta abierta el
     * orden prueba -> formatear (dos toques) -> archivo ---- */
    open_row(4, "Almacenamiento");
    EXPECT(sim_find_label(ui_nav_top(), "Sonda de 32 MB no superada"), "almacenamiento: no dice por que esta bloqueado");
    lv_obj_t *bsec = sim_find_label(ui_nav_top(), "Probar escritura");
    while (bsec && !lv_obj_has_flag(bsec, LV_OBJ_FLAG_CLICKABLE)) {
        bsec = lv_obj_get_parent(bsec);
    }
    EXPECT(bsec && lv_obj_has_state(bsec, LV_STATE_DISABLED), "almacenamiento: se ofrece escribir con la puerta cerrada");
    sim_stg.gate = STG_GATE_OK;
    sim_stg.state = STG_UNFORMATTED;
    sim_stg.detail = "Sin formato de LittleFS";
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "Sin formato"), "almacenamiento: no muestra sin formato");
    lv_obj_t *bfmt = sim_find_label(ui_nav_top(), "Formatear");
    while (bfmt && !lv_obj_has_flag(bfmt, LV_OBJ_FLAG_CLICKABLE)) {
        bfmt = lv_obj_get_parent(bfmt);
    }
    EXPECT(bfmt && lv_obj_has_state(bfmt, LV_STATE_DISABLED), "almacenamiento: formatear antes de la prueba");
    lv_obj_send_event(bsec, LV_EVENT_CLICKED, NULL);
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "Superada") && !lv_obj_has_state(bfmt, LV_STATE_DISABLED),
           "almacenamiento: la prueba del sector no habilita formatear");
    lv_obj_send_event(bfmt, LV_EVENT_CLICKED, NULL); /* primer toque: solo pide confirmar */
    sim_run(300);
    EXPECT(sim_stg.state == STG_UNFORMATTED && sim_find_label(ui_nav_top(), "Toca otra vez"),
           "almacenamiento: formateo con un solo toque");
    sim_save(out, "f4_almacenamiento_confirmar");
    sim_lint("almacenamiento (confirmar)");
    lv_obj_send_event(bfmt, LV_EVENT_CLICKED, NULL);
    sim_run(400);
    EXPECT(sim_stg.state == STG_MOUNTED && sim_find_label(ui_nav_top(), "Montado (LittleFS)"),
           "almacenamiento: no se monto tras formatear");
    lv_obj_t *bfile = sim_find_label(ui_nav_top(), "Probar archivo");
    while (bfile && !lv_obj_has_flag(bfile, LV_OBJ_FLAG_CLICKABLE)) {
        bfile = lv_obj_get_parent(bfile);
    }
    lv_obj_send_event(bfile, LV_EVENT_CLICKED, NULL);
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "Bien (n.º 1)"), "almacenamiento: archivo de prueba");
    sim_check_glyphs(ui_nav_top(), "almacenamiento");
    sim_lint("almacenamiento");
    sim_save(out, "f4_almacenamiento");
    back();
    sim_run(1100);
    sim_stg = (watch_storage_info_t){.state = STG_LOCKED, .gate = STG_GATE_PROBE, .detail = "Sonda de 32 MB no superada"};

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

    /* ---- La esfera con cada fondo (F3.6): legible y sin recortes; el
     * texto pasa a oscuro sobre los fondos claros ---- */
    for (int i = 1; i < FLEX_WALL_COUNT_; i++) {
        watch_theme_set_wallpaper((flex_wall_t)i);
        ui_nav_replace_root(ui_face_create);
        sim_run(1100);
        char where[48];
        snprintf(where, sizeof(where), "esfera con %s", watch_theme_wallpaper_name((flex_wall_t)i));
        sim_lint(where);
        char png[32];
        snprintf(png, sizeof(png), "f3_esfera_fondo_%d", i);
        sim_save(out, png);
    }
    watch_theme_set_wallpaper(FLEX_WALL_AURORA);
    ui_nav_replace_root(ui_face_create);
    sim_run(1100);
    lv_obj_t *tl = sim_find_label(ui_nav_page(0), "9:41");
    EXPECT(!tl || lv_color_to_u32(lv_obj_get_style_text_color(tl, 0)) != lv_color_to_u32(lv_color_hex(FLEX_C_TXT)) ||
               !watch_theme_wall_is_light(FLEX_SAFE_X, 112, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 86),
           "Aurora: la hora sigue en blanco sobre un fondo claro");
    watch_theme_set_wallpaper(FLEX_WALL_BLACK);
    ui_nav_replace_root(ui_face_create);
    sim_run(400);

    /* ---- Fugas: cada página 5 veces ---- */
    static const ui_page_create_t pages[] = {ui_display_page_create, ui_battery_page_create, ui_diag_create,
                                             ui_about_create, ui_touchtest_create, ui_wallpaper_create};
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
