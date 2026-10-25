/*
 * Bienestar en la vista previa (LVGL 9.5 real, dedo simulado y el motor real
 * de watch_activity con muestras SINTÉTICAS, sim_wellness.c):
 *  - la app en el cajón y su página (anillos, tarjetas), glifos y verificador
 *    visual de todas sus páginas;
 *  - el gesto de las tarjetas: desde el borde derecho de la esfera hacia la
 *    izquierda sí; desde el centro, con el dedo esperando o demasiado corto,
 *    no; dentro de una app ese borde sigue siendo "atrás"; con un
 *    entrenamiento en marcha hace falta un gesto más largo; el panel rápido y
 *    el cajón siguen funcionando;
 *  - un entrenamiento completo: cuenta atrás, en curso, pausa automática a
 *    los 20 s, reanudación al volver a caminar, terminar con confirmación,
 *    resumen y vuelta a Bienestar; y el fin automático a los 5 min en pausa;
 *  - ajustes (interruptores y editor), historial, sin peso, sin IMU y sin
 *    almacenamiento;
 *  - abrir y cerrar 20 veces sin fugas de memoria ni temporizadores.
 */
#include <stdio.h>
#include <string.h>

#include "flex_app_icons.h"
#include "sim.h"
#include "ui_internal.h"
#include "ui_wb.h"
#include "watch_activity.h"
#include "watch_theme.h"

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

static bool tap_obj(lv_obj_t *o)
{
    if (!o || lv_obj_has_state(o, LV_STATE_DISABLED)) {
        return false;
    }
    lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
    sim_run(30);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    sim_run(350);
    return true;
}

static bool tap_text(const char *text)
{
    return tap_obj(clickable_of(sim_find_label(ui_nav_top(), text)));
}

static bool has_text(const char *text)
{
    return sim_find_label(ui_nav_top(), text) != NULL;
}

static void home(void)
{
    ui_nav_home();
    sim_run(500);
    ui_nav_discard_suspended();
    sim_run(100);
}

/* Arrastre horizontal de x0 a x1 a media altura en n pasos de 16 ms. */
static void swipe(int32_t x0, int32_t x1, int n, uint32_t hold_ms)
{
    const int32_t y = FLEX_SCREEN_H / 2;
    sim_press(x0, y);
    sim_run(hold_ms ? hold_ms : 16);
    for (int i = 1; i <= n; i++) {
        sim_press(x0 + (x1 - x0) * i / n, y);
        sim_run(16);
    }
    sim_release();
    sim_run(400);
}

static void check_page(const char *out, const char *name)
{
    sim_run(800);
    sim_check_glyphs(ui_nav_top(), name);
    sim_lint(name);
    sim_save(out, name);
}

static void gesture_tests(const char *out)
{
    home();
    const uint32_t b0 = sim_mem_blocks();
    const int t0 = sim_timer_count();
    /* 1) Borde derecho hacia la izquierda: tarjetas */
    swipe(FLEX_SCREEN_W - 6, 120, 14, 0);
    EXPECT(ui_nav_depth() == 2, "gesto: no se abrieron las tarjetas (profundidad %d)", ui_nav_depth());
    EXPECT(has_text("Hoy"), "gesto: no son las tarjetas de Bienestar");
    check_page(out, "wb_tarjetas");
    /* 2) En las tarjetas (una página abierta), el borde derecho es "atrás" */
    swipe(FLEX_SCREEN_W - 6, 120, 14, 0);
    EXPECT(ui_nav_depth() == 1, "gesto: en las tarjetas el borde derecho no volvio (%d)", ui_nav_depth());
    /* 3) Desde el centro hacia la izquierda: nada */
    swipe(250, 40, 14, 0);
    EXPECT(ui_nav_depth() == 1, "gesto: desde el centro abrio algo (%d)", ui_nav_depth());
    /* 4) Dedo apoyado en el borde 600 ms y luego arrastre: no es el gesto */
    swipe(FLEX_SCREEN_W - 6, 120, 14, 600);
    EXPECT(ui_nav_depth() == 1, "gesto: el dedo apoyado abrio las tarjetas (%d)", ui_nav_depth());
    /* 5) Demasiado corto y lento: vuelve y no deja nada */
    swipe(FLEX_SCREEN_W - 6, FLEX_SCREEN_W - 60, 20, 0);
    EXPECT(ui_nav_depth() == 1, "gesto: un arrastre corto se quedo abierto (%d)", ui_nav_depth());
    EXPECT(sim_mem_blocks() <= b0 + 2 && sim_timer_count() == t0, "gesto cancelado: fuga (%u -> %u bloques)",
           (unsigned)b0, (unsigned)sim_mem_blocks());
    /* 6) Dentro de una app el borde derecho sigue siendo "atrás" (no abre Bienestar) */
    ui_nav_push(ui_settings_create);
    sim_run(400);
    swipe(FLEX_SCREEN_W - 6, 120, 14, 0);
    EXPECT(ui_nav_depth() == 1, "gesto: en Ajustes no volvio atras (%d)", ui_nav_depth());
    EXPECT(ui_nav_top() == ui_nav_page(0), "gesto: en Ajustes abrio otra cosa");
    /* 7) El panel rápido (arriba) y el cajón (abajo) siguen como antes */
    sim_drag(FLEX_SCREEN_W / 2, 10, FLEX_SCREEN_W / 2, 360, 12, 16);
    sim_release();
    sim_run(500);
    EXPECT(ui_qp_is_open(), "gesto: el panel rapido ya no se abre");
    ui_qp_close();
    sim_run(500);
    sim_drag(FLEX_SCREEN_W / 2, FLEX_SCREEN_H - 10, FLEX_SCREEN_W / 2, 100, 12, 16);
    sim_release();
    sim_run(600);
    EXPECT(ui_nav_depth() == 2 && ui_nav_top() && sim_find_label(ui_nav_top(), "Hoy") == NULL,
           "gesto: el cajon ya no se abre desde abajo");
    home();
    /* 8) Con un entrenamiento en marcha: un lanzamiento corto ya no basta */
    watch_activity_workout_start(ACT_TYPE_WALK_OUT, false);
    sim_run(300);
    swipe(FLEX_SCREEN_W - 6, FLEX_SCREEN_W - 150, 4, 0); /* rápido, 36 % */
    EXPECT(ui_nav_depth() == 1, "gesto estricto: un lanzamiento corto abrio las tarjetas");
    swipe(FLEX_SCREEN_W - 6, 60, 14, 0); /* largo: sí */
    EXPECT(ui_nav_depth() == 2, "gesto estricto: un gesto largo no abrio las tarjetas");
    EXPECT(has_text("Caminar al aire libre"), "tarjetas: falta el entrenamiento en curso");
    watch_activity_workout_end();
    home();
}

static void workout_tests(const char *out)
{
    act_config_t c;
    act_config_default(&c);
    c.weight_dkg = 700;
    c.height_cm = 175;
    sim_wb_reset(&c, true);
    home();
    ui_nav_push(ui_wb_create);
    sim_run(500);
    EXPECT(tap_text("Entrenar"), "no se encontro Entrenar");
    EXPECT(has_text("Categorías"), "selector: faltan las categorías");
    check_page(out, "wb_selector");
    EXPECT(tap_text("Caminar y correr"), "selector: no se abrio la categoria");
    check_page(out, "wb_categoria");
    EXPECT(tap_text("Caminar al aire libre"), "categoria: no se abrio el tipo");
    sim_run(400);
    check_page(out, "wb_cuenta_atras");
    sim_run(3300);
    watch_activity_status_t st;
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_ACTIVE && st.v.type == ACT_TYPE_WALK_OUT, "cuenta atras: no empezo (%d)", st.v.ses);
    /* caminar 60 s: los pasos llegan a la página */
    sim_wb_motion(110, 420);
    sim_run(60000);
    watch_activity_get(&st);
    EXPECT(st.v.ses_steps > 90 && st.v.ses_steps < 125, "en curso: %lu pasos en 60 s a 110/min",
           (unsigned long)st.v.ses_steps);
    EXPECT(has_text("EN CURSO"), "en curso: falta el estado");
    check_page(out, "wb_en_curso");
    /* quieto 25 s: pausa automática */
    sim_wb_motion(0, 0);
    sim_run(25000);
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_PAUSED && st.v.pause_kind == ACT_PAUSE_AUTO, "pausa automatica: estado %d", st.v.ses);
    EXPECT(has_text("PAUSA AUTOMÁTICA"), "pausa automatica: falta el rotulo");
    EXPECT(has_text("Reanudar"), "pausa automatica: el boton no dice Reanudar");
    check_page(out, "wb_pausa");
    /* a caminar: reanuda solo */
    sim_wb_motion(110, 420);
    sim_run(12000);
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_ACTIVE, "reanudar: estado %d", st.v.ses);
    /* terminar: el primer toque pide confirmación */
    EXPECT(tap_text("Terminar"), "no se encontro Terminar");
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_ACTIVE, "terminar: un solo toque termino");
    EXPECT(tap_text("¿Terminar?"), "terminar: falta la confirmacion");
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_COMPLETED, "terminar: estado %d", st.v.ses);
    EXPECT(has_text("Tiempo total") && has_text("Activo"), "resumen: faltan filas");
    check_page(out, "wb_resumen");
    EXPECT(tap_text("Listo"), "resumen: no se encontro Listo");
    sim_run(400);
    EXPECT(ui_nav_depth() == 2 && has_text("Bienestar"), "Listo: no volvio a Bienestar (%d)", ui_nav_depth());
    EXPECT(has_text("Último entrenamiento"), "Bienestar: falta el último entrenamiento");
    check_page(out, "wb_app");
    /* el historial lo tiene */
    EXPECT(tap_text("Historial y logros"), "no se abrio el historial");
    EXPECT(has_text("Últimos 7 días") && has_text("Caminar al aire libre"), "historial: falta el entrenamiento");
    check_page(out, "wb_historial");
    EXPECT(tap_text("Caminar al aire libre"), "historial: no se abrio el detalle");
    EXPECT(has_text("Pasos") && has_text("Distancia (estimada)"), "detalle: faltan datos");
    check_page(out, "wb_detalle");

    /* Fin automático con la página abierta: 5 min en pausa -> resumen */
    home();
    ui_wb_open_workout(ACT_TYPE_RUN_OUT, false);
    ui_nav_push(ui_wb_workout_create);
    sim_run(3600);
    sim_wb_motion(165, 1300);
    sim_run(30000);
    sim_wb_motion(0, 0);
    sim_run(25000 + 300000 + 2000);
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_COMPLETED && st.v.end_reason == ACT_END_AUTO, "fin automatico: estado %d", st.v.ses);
    EXPECT(has_text("Terminado solo tras la pausa"), "fin automatico: el resumen no lo dice");
    /* Salir de la página no para el entrenamiento: sigue y la tarjeta lo enseña */
    home();
    ui_wb_open_workout(ACT_TYPE_WALK_IN, false);
    ui_nav_push(ui_wb_workout_create);
    sim_run(3600);
    ui_nav_back();
    sim_run(400);
    sim_wb_motion(110, 420);
    sim_run(20000);
    watch_activity_get(&st);
    EXPECT(st.v.ses == ACT_SES_ACTIVE && st.v.ses_steps > 20, "fuera de la pagina: no siguio (%d, %lu)", st.v.ses,
           (unsigned long)st.v.ses_steps);
    ui_nav_push(ui_wb_create);
    sim_run(800);
    EXPECT(has_text("Caminar en interior"), "Bienestar: falta la tarjeta del entrenamiento en curso");
    watch_activity_workout_end();
    sim_wb_motion(0, 0);
    /* A la esfera durante la cuenta atrás: no empieza a escondidas */
    home();
    ui_wb_open_workout(30, false);
    ui_nav_push(ui_wb_workout_create);
    sim_run(1000);
    ui_nav_suspend(false);
    sim_run(6000);
    watch_activity_get(&st);
    EXPECT(st.v.ses != ACT_SES_ACTIVE, "cuenta atras: empezo con la app suspendida");
    home();
}

static void settings_tests(const char *out)
{
    act_config_t c;
    act_config_default(&c);
    sim_wb_reset(&c, true);
    home();
    ui_nav_push(ui_wb_create);
    sim_run(500);
    /* sin peso: no hay calorías, se pide el peso */
    EXPECT(has_text("Añade tu peso en Ajustes de Bienestar"), "sin peso: falta el aviso");
    EXPECT(tap_text("Ajustes de Bienestar"), "no se abrieron los ajustes");
    EXPECT(has_text("Pausa automática") && has_text("Calibrar distancia"), "ajustes: faltan filas");
    check_page(out, "wb_ajustes");
    /* el editor del peso */
    EXPECT(tap_text("Peso"), "ajustes: no se abrio el peso");
    lv_obj_t *plus = clickable_of(sim_find_label(ui_nav_top(), "+"));
    for (int i = 0; i < 4; i++) {
        tap_obj(plus);
    }
    check_page(out, "wb_editor");
    EXPECT(tap_text("Guardar"), "editor: no se guardo");
    act_config_t now;
    watch_activity_config(&now);
    EXPECT(now.weight_dkg == 720, "editor: peso %u (esperado 720)", now.weight_dkg);
    /* interruptor de la pausa automática */
    lv_obj_t *row = clickable_of(sim_find_label(ui_nav_top(), "Pausa automática"));
    (void)row;
    lv_obj_t *lbl = sim_find_label(ui_nav_top(), "Pausa automática");
    lv_obj_t *r = lbl ? lv_obj_get_parent(lbl) : NULL;
    lv_obj_t *sw = NULL;
    for (uint32_t i = 0; r && i < lv_obj_get_child_count(r); i++) {
        if (lv_obj_check_type(lv_obj_get_child(r, (int32_t)i), &lv_switch_class)) {
            sw = lv_obj_get_child(r, (int32_t)i);
        }
    }
    EXPECT(sw && lv_obj_has_state(sw, LV_STATE_CHECKED), "ajustes: el interruptor de pausa no esta activo");
    tap_obj(sw);
    watch_activity_config(&now);
    EXPECT(!now.autos.auto_pause, "ajustes: el interruptor no desactivo la pausa automatica");
    /* calibración */
    EXPECT(tap_text("Calibrar distancia"), "no se abrio la calibracion");
    EXPECT(tap_text("Caminar"), "calibracion: no empezo");
    sim_wb_motion(110, 420);
    sim_run(300000); /* 5 min: unos 550 pasos */
    sim_wb_motion(0, 0);
    EXPECT(tap_text("He llegado"), "calibracion: falta He llegado");
    EXPECT(tap_text("Calcular"), "calibracion: falta Calcular");
    check_page(out, "wb_calibracion");
    EXPECT(tap_text("Aplicar"), "calibracion: falta Aplicar");
    watch_activity_config(&now);
    EXPECT(now.stride_walk_cm >= 60 && now.stride_walk_cm <= 85, "calibracion: zancada %u cm (400 m / ~550 pasos)",
           now.stride_walk_cm);
    home();
    /* sin IMU y sin almacenamiento: avisos honestos */
    sim_wb_imu(false);
    ui_nav_push(ui_wb_create);
    sim_run(1500);
    EXPECT(has_text("Sensor de movimiento no disponible: no se cuentan pasos."), "sin IMU: falta el aviso");
    home();
    sim_wb_imu(true);
    sim_wb_reset(&c, false);
    ui_nav_push(ui_wb_create);
    sim_run(1500);
    EXPECT(has_text("Sin almacenamiento: el historial no se guarda (Almacenamiento)."),
           "sin almacenamiento: falta el aviso");
    home();
    /* búsqueda */
    ui_nav_push(ui_wb_search_create);
    sim_run(400);
    lv_obj_t *ta = NULL;
    lv_obj_t *top = ui_nav_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (lv_obj_check_type(lv_obj_get_child(top, (int32_t)i), &lv_textarea_class)) {
            ta = lv_obj_get_child(top, (int32_t)i);
        }
    }
    EXPECT(ta != NULL, "busqueda: no hay campo");
    if (ta) {
        lv_textarea_set_text(ta, "tenis");
        sim_run(300);
        EXPECT(has_text("Tenis") && has_text("Tenis de mesa"), "busqueda: faltan resultados");
        check_page(out, "wb_busqueda");
    }
    home();
}

static void leak_tests(void)
{
    home();
    sim_run(1200);
    const uint32_t b0 = sim_mem_blocks();
    const int t0 = sim_timer_count();
    static const ui_page_create_t kPages[] = {ui_wb_create,  ui_wb_cards_create, ui_wb_workouts_create,
                                              ui_wb_history_create, ui_wb_settings_create};
    for (int round = 0; round < 20; round++) {
        ui_nav_push(kPages[round % 5]);
        sim_run(800);
        ui_nav_back();
        sim_run(500);
    }
    /* y por el gesto */
    for (int round = 0; round < 10; round++) {
        swipe(FLEX_SCREEN_W - 6, 120, 14, 0);
        sim_run(300);
        ui_nav_back();
        sim_run(500);
    }
    EXPECT(ui_nav_depth() == 1, "fugas: no se volvio a la esfera (%d)", ui_nav_depth());
    EXPECT(sim_timer_count() == t0, "fugas: temporizadores %d -> %d", t0, sim_timer_count());
    EXPECT(sim_mem_blocks() <= b0 + 2, "fugas: bloques de LVGL %u -> %u", (unsigned)b0, (unsigned)sim_mem_blocks());
    printf("  Bienestar abierto y cerrado 30 veces: %u -> %u bloques de LVGL, %d -> %d temporizadores\n",
           (unsigned)b0, (unsigned)sim_mem_blocks(), t0, sim_timer_count());
}

void sim_suite_wellness(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    sim_ws_reset(NULL);
    ui_nav_replace_root(ui_face_create);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    act_config_t c;
    act_config_default(&c);
    sim_wb_reset(&c, true);
    sim_run(1200);

    /* En el cajón */
    ui_nav_push(ui_apps_create);
    sim_run(700);
    lv_obj_t *icon = ui_apps_find(ui_nav_top(), "Bienestar");
    EXPECT(icon != NULL, "cajon: no esta Bienestar");
    if (icon) {
        lv_area_t a;
        lv_obj_get_coords(icon, &a);
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(600);
        EXPECT(has_text("Bienestar") && has_text("Moverse"), "cajon: no se abrio Bienestar");
    }
    home();

    gesture_tests(out);
    workout_tests(out);
    settings_tests(out);
    leak_tests();
    sim_wb_motion(0, 0);
    sim_wb_reset(&c, true);
    home();
    printf("Bienestar: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
