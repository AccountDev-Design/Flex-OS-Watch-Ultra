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
    lv_obj_t *item = lv_obj_get_child(strip, i);
    lv_obj_t *card = lv_obj_get_child(item, 0);
    lv_obj_t *img = lv_obj_get_child(card, 0);
    const lv_image_dsc_t *d = lv_image_get_src(img);
    return d && d->data ? d : NULL;
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

    /* ---- Los cinco estilos ---- */
    static const char *const png[UI_FACE_COUNT_] = {"f10_esfera_digital", "f10_esfera_analogica",
                                                    "f10_esfera_modular", "f10_esfera_glass",
                                                    "f10_esfera_parallax"};
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        set_face(i);
        EXPECT(face_setting() == i, "estilo %d no se guardo", i);
        lv_obj_t *face = ui_nav_page(0);
        sim_check_glyphs(face, ui_face_name(i));
        sim_lint(ui_face_name(i));
        sim_save(out, png[i]);
        if (i != UI_FACE_ANALOG) {
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

    /* ---- Mantener pulsada la esfera: selector ---- */
    set_face(UI_FACE_DIGITAL);
    sim_press(205, 300);
    sim_run(700);
    sim_release();
    sim_run(400);
    EXPECT(ui_nav_depth() == 2 && sim_find_label(ui_nav_top(), "Esferas"), "mantener pulsado no abrio el selector");
    sim_run(UI_FACE_COUNT_ * 40 + 200); /* vistas previas, de una en una */
    int previews = 0;
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        const lv_image_dsc_t *d = preview(i);
        previews += d && d->header.w == FLEX_SCREEN_W / 2 && d->header.h == FLEX_SCREEN_H / 2;
    }
    EXPECT(previews == UI_FACE_COUNT_, "selector: %d de %d vistas previas renderizadas", previews, UI_FACE_COUNT_);
    sim_check_glyphs(ui_nav_top(), "selector");
    sim_lint("selector de esferas");
    sim_save(out, "f10_selector");
    /* Aplicar la analógica: se guarda y la esfera se rehace */
    lv_obj_t *name = sim_find_label(ui_nav_top(), "Analógica");
    lv_obj_t *item = clickable_of(name);
    EXPECT(item, "selector: no hay Analogica");
    if (item) {
        lv_obj_scroll_to_view(item, LV_ANIM_OFF);
        sim_run(100);
        lv_area_t a;
        lv_obj_get_coords(item, &a);
        sim_tap((a.x1 + a.x2) / 2, a.y1 + 100);
        sim_run(600);
    }
    EXPECT(face_setting() == UI_FACE_ANALOG, "selector: no aplico la analogica (%d)", face_setting());
    EXPECT(ui_nav_depth() == 1, "selector: no volvio a la esfera");
    sim_save(out, "f10_esfera_analogica_aplicada");

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
    EXPECT(sim_find_label(ui_nav_top(), "Esferas"), "Pantalla > Esfera no abrio el selector");
    sim_run(UI_FACE_COUNT_ * 40 + 200);
    item = clickable_of(sim_find_label(ui_nav_top(), "Liquid Glass"));
    if (item) {
        lv_obj_send_event(item, LV_EVENT_CLICKED, NULL);
        sim_run(600);
    }
    EXPECT(face_setting() == UI_FACE_GLASS && ui_nav_depth() == 1,
           "desde Ajustes: no aplico o no volvio a la esfera (estilo %d, profundidad %d)", face_setting(),
           ui_nav_depth());

    /* ---- Ahorro de energía: esfera mínima en negro ---- */
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
        sim_run(round % 2 ? 60 : UI_FACE_COUNT_ * 40 + 200); /* a veces con las vistas previas a medias */
        EXPECT(sim_find_label(ui_nav_top(), "Esferas"), "regresion %d: el selector no se abrio", round);
        if (round % 3 == 0) {
            sim_drag(4, 300, 300, 300, 12, 20); /* borde izquierdo: al cajón */
        } else if (round % 3 == 1) {
            sim_drag(FLEX_SCREEN_W - 4, 300, 100, 300, 12, 20); /* borde derecho: atrás */
        } else {
            sim_drag(200, FLEX_SCREEN_H - 6, 200, 120, 12, 20); /* borde inferior: a la esfera */
        }
        sim_release();
        sim_run(600);
        EXPECT(!ui_nav_busy() && !sim_find_label(ui_nav_top(), "Esferas"), "regresion %d: no se salio del selector",
               round);
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
