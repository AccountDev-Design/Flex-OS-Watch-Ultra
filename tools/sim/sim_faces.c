/*
 * Esferas y selector en la vista previa:
 *  - los cinco estilos se construyen, se dibujan sin glifos que falten ni
 *    textos cortados (verificador visual) y no dejan memoria ni temporizadores;
 *  - mantener pulsada la esfera abre el selector; las vistas previas son
 *    imágenes renderizadas de las esferas reales; tocar una la aplica, se
 *    guarda (ajuste "face") y la esfera se rehace;
 *  - Ajustes > Pantalla > Esfera abre el mismo selector y al aplicar vuelve a
 *    la esfera;
 *  - modular: complicaciones con datos reales ("--" / "Sin datos" sin ellos);
 *  - Parallax: la capa delantera sigue a la inclinación REAL del IMU y el IMU
 *    solo mide con la esfera a la vista;
 *  - con Flex Ultra Power Saving, esfera mínima sobre negro sin IMU.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

int sim_lint(const char *where);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

static int face_setting(void)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    return ws.face;
}

static void set_face(int style)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.face = (uint8_t)style;
    watch_settings_update(&ws);
    ui_nav_replace_root(ui_face_create);
    sim_run(1100);
}

/* Imagen de la vista previa i del selector abierto (o NULL). */
static const lv_image_dsc_t *preview(int i)
{
    lv_obj_t *page = ui_nav_top();
    lv_obj_t *strip = NULL;
    for (uint32_t k = 0; k < lv_obj_get_child_count(page); k++) {
        lv_obj_t *c = lv_obj_get_child(page, (int32_t)k);
        if (lv_obj_get_child_count(c) == UI_FACE_COUNT_) {
            strip = c;
        }
    }
    if (!strip) {
        return NULL;
    }
    lv_obj_t *card = lv_obj_get_child(strip, i);
    lv_obj_t *img = lv_obj_get_child(card, 0);
    const lv_image_dsc_t *d = lv_image_get_src(img);
    return d && d->data ? d : NULL;
}

/* Miniatura del selector rediseñado: 276x358 (design/referencias/editor_esferas.png) */
static bool thumb_ok(int i)
{
    const lv_image_dsc_t *d = preview(i);
    return d && d->header.w == 276 && d->header.h == 358;
}

/* Desliza el carrusel una tarjeta (dir > 0: a la siguiente). */
static void swipe(int dir)
{
    sim_drag(dir > 0 ? 300 : 110, 250, dir > 0 ? 110 : 300, 252, 8, 16);
    sim_release();
    sim_run(700);
}

/* Centra la esfera i deslizando, como lo haría el usuario. */
static void center(int i)
{
    for (int k = 0; k < UI_FACE_COUNT_ && ui_face_editor_centered() >= 0 && ui_face_editor_centered() != i; k++) {
        swipe(i > ui_face_editor_centered() ? 1 : -1);
    }
}

void sim_suite_faces(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_ws_reset(NULL);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 10, 9, 30);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    memset(&sim_imu, 0, sizeof(sim_imu));
    sim_run(1100);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* ---- Todos los estilos (los cinco de siempre y los seis del rediseño) ---- */
    static const char *const png[UI_FACE_COUNT_] = {
        "f10_esfera_digital", "f10_esfera_analogica", "f10_esfera_modular", "f10_esfera_glass",
        "f10_esfera_parallax", "f10_esfera_montana", "f10_esfera_ultra", "f10_esfera_luna",
        "f10_esfera_tierra", "f10_esfera_flores", "f10_esfera_taj"};
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        set_face(i);
        EXPECT(face_setting() == i, "estilo %d no se guardo", i);
        lv_obj_t *face = ui_nav_page(0);
        sim_check_glyphs(face, ui_face_name(i));
        sim_lint(ui_face_name(i));
        sim_save(out, png[i]);
        if (i == UI_FACE_FLORES) { /* horas y minutos apilados */
            EXPECT(sim_find_label_n(face, "10", 0) && sim_find_label(face, "09"), "%s: no muestra la hora",
                   ui_face_name(i));
        } else if (i != UI_FACE_ANALOG && i != UI_FACE_ULTRA) {
            EXPECT(sim_find_label(face, "10:09"), "%s: no muestra la hora", ui_face_name(i));
        }
    }
    /* Modular: complicaciones reales */
    set_face(UI_FACE_MODULAR);
    lv_obj_t *face = ui_nav_page(0);
    EXPECT(sim_find_label(face, "84 %") && sim_find_label(face, "15") && sim_find_label(face, "JUE"),
           "modular: faltan bateria o fecha");
    EXPECT(sim_find_label(face, "Sin alarmas"), "modular: la alarma no es real");
    EXPECT(sim_find_label(face, "Sin datos") || sim_find_label(face, "--"), "modular: clima inventado sin datos");

    /* Parallax: el IMU solo mide con la esfera a la vista; la hora sigue a la muñeca */
    set_face(UI_FACE_PARALLAX);
    face = ui_nav_page(0);
    sim_imu.state = IMU_RUNNING;
    sim_imu.samples = 10;
    sim_imu.acc_mg[2] = 1000;
    sim_run(300);
    EXPECT(sim_imu_users == 1, "parallax: no pidio el IMU con la esfera a la vista (%d)", sim_imu_users);
    lv_obj_t *t = sim_find_label(face, "10:09");
    const int32_t x0 = t ? lv_obj_get_x(t) : 0;
    sim_imu.acc_mg[0] = 0;
    sim_imu.acc_mg[1] = 342; /* ~20° de alabeo */
    sim_imu.acc_mg[2] = 940;
    sim_run(800);
    EXPECT(t && lv_obj_get_x(t) != x0, "parallax: la hora no se mueve con la inclinacion");
    sim_save(out, "f10_esfera_parallax_inclinada");
    ui_nav_push(ui_settings_create);
    sim_run(500);
    EXPECT(sim_imu_users == 0, "parallax: el IMU sigue midiendo con la esfera tapada (%d)", sim_imu_users);
    ui_nav_home();
    sim_run(500);
    memset(&sim_imu, 0, sizeof(sim_imu));

    /* ---- Mantener pulsada la esfera: selector (zoom desde la esfera) ---- */
    set_face(UI_FACE_DIGITAL);
    const uint32_t made0 = ui_face_editor_previews_made();
    sim_press(205, 300);
    sim_run(560); /* pulsación larga (400 ms) y la mitad del zoom */
    EXPECT(ui_nav_depth() == 2 && ui_face_editor_centered() < 0, "mantener pulsado: no empezo el zoom");
    /* durante el zoom solo existe la captura de la esfera (nada se renderiza mientras anima) */
    EXPECT(ui_face_editor_previews_made() - made0 == 1 && thumb_ok(UI_FACE_DIGITAL),
           "selector: la tarjeta centrada no es la captura de la esfera");
    sim_save(out, "f10_selector_zoom");
    sim_release();
    sim_run(400);
    EXPECT(ui_nav_depth() == 2 && ui_face_editor_centered() == UI_FACE_DIGITAL, "mantener pulsado no abrio el selector");
    sim_run(6 * 60 + 300); /* las vecinas, de una en una */
    /* Ventana deslizante: la centrada y dos a cada lado, ni una más */
    EXPECT(thumb_ok(0) && thumb_ok(1) && thumb_ok(2) && !preview(3) && ui_face_editor_thumbs() == 3,
           "selector: ventana de vistas previas incorrecta (%d)", ui_face_editor_thumbs());
    EXPECT(sim_find_label(ui_nav_top(), "Digital"), "selector: falta el nombre de la centrada");
    sim_check_glyphs(ui_nav_top(), "selector");
    sim_lint("selector de esferas");
    sim_save(out, "f10_selector");
    /* Recorrer hasta el final: nunca más de 5 miniaturas vivas */
    int most = 0;
    for (int k = 0; k < UI_FACE_COUNT_; k++) {
        swipe(1);
        sim_run(6 * 60);
        most = ui_face_editor_thumbs() > most ? ui_face_editor_thumbs() : most;
    }
    EXPECT(ui_face_editor_centered() == UI_FACE_COUNT_ - 1 && most <= 5 && thumb_ok(UI_FACE_COUNT_ - 1) &&
               !preview(0),
           "selector: al recorrer (centrada %d, %d miniaturas como mucho)", ui_face_editor_centered(), most);
    sim_save(out, "f10_selector_final");
    /* Aplicar la analógica: crece hasta llenar la pantalla, se guarda y la esfera se rehace */
    center(UI_FACE_ANALOG);
    sim_run(6 * 60);
    EXPECT(ui_face_editor_centered() == UI_FACE_ANALOG, "selector: no se centro la analogica");
    sim_tap(205, 250);
    sim_run(150);
    sim_save(out, "f10_selector_aplicar");
    sim_run(600);
    EXPECT(face_setting() == UI_FACE_ANALOG, "selector: no aplico la analogica (%d)", face_setting());
    EXPECT(ui_nav_depth() == 1 && !ui_nav_busy(), "selector: no volvio a la esfera");
    sim_save(out, "f10_esfera_analogica_aplicada");
    /* Una vecina tocada se centra, no se aplica */
    sim_press(205, 300);
    sim_run(700);
    sim_release();
    sim_run(600);
    EXPECT(ui_face_editor_centered() == UI_FACE_ANALOG, "selector: no se abrio en la esfera actual");
    sim_tap(380, 250);
    sim_run(700);
    EXPECT(ui_nav_depth() == 2 && ui_face_editor_centered() == UI_FACE_MODULAR && face_setting() == UI_FACE_ANALOG,
           "selector: tocar la vecina no la centro (o la aplico)");
    sim_drag(FLEX_SCREEN_W - 4, 300, 100, 300, 12, 20); /* borde derecho: sale sin cambiar */
    sim_release();
    sim_run(700);
    EXPECT(ui_nav_depth() == 1 && face_setting() == UI_FACE_ANALOG, "selector: salir cambio la esfera");

    /* ---- Desde Ajustes > Pantalla > Esfera: mismo selector, vuelve a la esfera ---- */
    ui_nav_push(ui_settings_create);
    sim_run(400);
    ui_nav_push(ui_display_page_create);
    sim_run(400);
    lv_obj_t *row = clickable_of(sim_find_label(ui_nav_top(), "Analógica"));
    EXPECT(row, "Pantalla no muestra la esfera elegida");
    if (row) {
        lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
        sim_run(600);
    }
    EXPECT(ui_face_editor_centered() == UI_FACE_ANALOG, "Pantalla > Esfera no abrio el selector");
    sim_run(6 * 60 + 300);
    center(UI_FACE_GLASS);
    sim_run(6 * 60);
    sim_tap(205, 250);
    sim_run(900);
    EXPECT(face_setting() == UI_FACE_GLASS && ui_nav_depth() == 1,
           "desde Ajustes: no aplico o no volvio a la esfera (estilo %d, profundidad %d)", face_setting(),
           ui_nav_depth());

    /* ---- El fondo de la esfera activa no cambia por las vistas previas ----
     * (antes una vista previa con otro fondo reescribía el de la activa) */
    set_face(UI_FACE_TAJ);
    sim_save(out, "f10_taj_antes");
    const uint16_t px0 = sim_fb[300 * SIM_W + 200];
    sim_press(205, 300);
    sim_run(700);
    sim_release();
    sim_run(6 * 60 + 600); /* genera Tierra, Flores (otros fondos) */
    EXPECT(ui_face_editor_thumbs() == 3, "selector: vistas previas de las vecinas del Taj (%d)", ui_face_editor_thumbs());
    sim_drag(FLEX_SCREEN_W - 4, 300, 100, 300, 12, 20);
    sim_release();
    sim_run(800);
    lv_obj_invalidate(ui_nav_page(0));
    sim_run(100);
    EXPECT(ui_nav_depth() == 1 && sim_fb[300 * SIM_W + 200] == px0,
           "selector: el fondo de la esfera activa cambio tras las vistas previas");

    /* ---- Ahorro de energía: esfera mínima en negro ---- */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 10, 9, 30); /* el recorrido del selector ha llevado su tiempo */
    set_face(UI_FACE_PARALLAX);
    ui_psave_set(true);
    sim_run(1100);
    face = ui_nav_page(0);
    EXPECT(sim_find_label(face, "10:09") && sim_imu_users == 0, "ahorro: la esfera no es la minima o usa el IMU");
    sim_save(out, "f10_esfera_ahorro");
    ui_psave_set(false);
    sim_run(1100);

    /* ---- Regresión (vídeo del 2026-10-20): con fondo de color y la esfera
     * Parallax, salir del selector con el borde izquierdo reiniciaba el reloj. */
    {
        watch_settings_t ws;
        watch_settings_get(&ws);
        ws.wallpaper = FLEX_WALL_OCEANO;
        watch_settings_update(&ws);
        watch_theme_set_wallpaper(FLEX_WALL_OCEANO);
    }
    for (int round = 0; round < 6; round++) {
        set_face(UI_FACE_PARALLAX);
        sim_press(205, 300);
        sim_run(700);
        sim_release();
        sim_run(round % 2 ? 300 : 6 * 60 + 300); /* a veces con las vistas previas a medias */
        EXPECT(ui_face_editor_centered() == UI_FACE_PARALLAX, "regresion %d: el selector no se abrio", round);
        if (round % 3 == 0) {
            sim_drag(4, 300, 300, 300, 12, 20); /* borde izquierdo: al cajón */
        } else if (round % 3 == 1) {
            sim_drag(FLEX_SCREEN_W - 4, 300, 100, 300, 12, 20); /* borde derecho: atrás */
        } else {
            sim_drag(200, FLEX_SCREEN_H - 6, 200, 120, 12, 20); /* borde inferior: a la esfera */
        }
        sim_release();
        sim_run(600);
        EXPECT(!ui_nav_busy() && ui_face_editor_centered() < 0, "regresion %d: no se salio del selector", round);
        ui_nav_discard_suspended();
        ui_nav_home();
        sim_run(400);
    }
    {
        watch_settings_t ws;
        watch_settings_get(&ws);
        ws.wallpaper = FLEX_WALL_BLACK;
        watch_settings_update(&ws);
        watch_theme_set_wallpaper(FLEX_WALL_BLACK);
        ui_nav_replace_root(ui_face_create);
        sim_run(600);
    }

    set_face(UI_FACE_DIGITAL);
    sim_ws_reset(NULL);
    ui_nav_replace_root(ui_face_create);
    sim_run(1100);
    EXPECT(sim_timer_count() == timers0, "esferas: temporizadores %d -> %d", timers0, sim_timer_count());
    EXPECT(sim_mem_blocks() == blocks0, "esferas: fuga %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    printf("esferas y selector: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
