/*
 * Apagado de pantalla (F4.5) en la vista previa:
 *  - atenuación PROGRESIVA del brillo físico en los últimos 3 s y apagada al
 *    tiempo elegido; vuelve a la esfera con la app suspendida (no cerrada);
 *  - AOD: en vez de apagarse, negro con la hora atenuada, brillo bajo y
 *    Pixel Shift por minuto; un toque la enciende; con el ahorro de energía
 *    no hay AOD;
 *  - el toque que la enciende (o que la saca de la atenuación) no llega a la
 *    interfaz;
 *  - el botón lateral la apaga y la enciende (polaridad aprendida);
 *  - el aviso del temporizador y enchufar el USB la encienden;
 *  - la prueba de pantalla la mantiene encendida;
 *  - el tiempo se cambia en Ajustes > Pantalla y se guarda.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"
#include "ui_screen_logic.h"
#include "watch_theme.h"

int sim_lint(const char *where);
int sim_check_residue(const char *where);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

static bool tap_text(const char *text)
{
    lv_obj_t *o = clickable_of(sim_find_label(ui_nav_top(), text));
    if (!o) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    sim_run(350);
    return true;
}

/* El botón se lee cada 100 ms (tick de ui_system): pulsaciones y pausas de
 * al menos dos lecturas, como las de un dedo. */
static void press_button(uint32_t ms)
{
    sim_button = 1;
    sim_run(ms);
    sim_button = 0;
    sim_run(400);
}

void sim_suite_screen(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_ws_reset(NULL); /* apagado a los 15 s */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(500);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();
    ui_screen_enable(true);
    sim_tap(205, 300); /* actividad: empieza a contar desde aquí */
    sim_run(100);

    /* ---- En Ajustes: atenuada a los 12 s; el toque no abre la fila ---- */
    ui_nav_push(ui_settings_create);
    sim_run(1300);
    sim_run(10000);
    EXPECT(ui_screen_state() == 0 && sim_display_level == -1, "se atenuo antes de tiempo");
    sim_run(1500);
    EXPECT(ui_screen_state() == 1, "no se atenuo a los 12 s (estado %d)", ui_screen_state());
    EXPECT(sim_display_level < 70 && sim_display_level > 30, "la atenuacion no es progresiva (%d a 0,8 s)",
           sim_display_level);
    sim_run(1900); /* 14,7 s: casi al final de la rampa (hacia 70/3 = 23) */
    EXPECT(sim_display_level >= 23 && sim_display_level <= 28, "brillo atenuado %d (esperado ~23 = 70/3)",
           sim_display_level);
    EXPECT(sim_brightness == 70, "la atenuacion cambio el brillo del usuario");
    sim_save(out, "f4_atenuada");
    EXPECT(tap_text("Pantalla"), "no hay fila Pantalla");
    EXPECT(ui_screen_state() == 0 && sim_display_level == -1, "un toque no la saco de la atenuacion");
    EXPECT(ui_nav_depth() == 2, "el toque que la encendio abrio la fila (profundidad %d)", ui_nav_depth());
    EXPECT(lv_obj_get_child_count(lv_layer_top()) == 0, "la capa del toque no se quito");

    /* ---- Apagada a los 15 s: vuelve a la esfera ---- */
    const uint32_t offs = ui_screen_offs();
    sim_run(15500);
    EXPECT(!sim_display_on && ui_screen_offs() == offs + 1, "no se apago a los 15 s");
    EXPECT(ui_nav_depth() == 1, "al apagarse no volvio a la esfera");
    EXPECT(ui_nav_suspended_app() == ui_settings_create, "al apagarse se cerro la app en vez de suspenderla");
    sim_run(30000);
    EXPECT(!sim_display_on, "se encendio sola");
    /* un toque la enciende y no hace nada más */
    sim_drag(330, 260, 120, 262, 6, 16); /* sería "abrir apps" con la pantalla encendida */
    sim_release();
    sim_run(400);
    EXPECT(sim_display_on && ui_screen_is_on(), "un toque no encendio la pantalla");
    EXPECT(ui_nav_depth() == 1, "el gesto que la encendio abrio las apps");
    EXPECT(lv_obj_get_child_count(lv_layer_top()) == 0, "la capa del toque no se quito tras encender");

    /* ---- Botón lateral: apaga y enciende; una pulsación larga no cuenta ---- */
    press_button(250);
    EXPECT(!sim_display_on, "el boton no apago la pantalla");
    press_button(250);
    EXPECT(sim_display_on, "el boton no encendio la pantalla");
    press_button(2600);
    EXPECT(sim_display_on, "una pulsacion larga apago la pantalla");

    /* ---- El aviso del temporizador la enciende ---- */
    tmr_start(ui_tmr(), 30LL * 1000000, ui_mono_us());
    sim_run(16000);
    EXPECT(!sim_display_on, "no se apago con el temporizador en marcha");
    sim_run(15000);
    EXPECT(sim_display_on && ui_ring_active(), "el aviso del temporizador no encendio la pantalla");
    sim_run(20000);
    EXPECT(sim_display_on, "se apago con el aviso sonando");
    ui_ring_dismiss();
    tmr_cancel(ui_tmr());
    sim_run(500);

    /* ---- Enchufar el USB la enciende ---- */
    sim_run(16000);
    EXPECT(!sim_display_on, "no se apago antes de enchufar");
    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_power.track.prev_state = BATT_STATE_DISCHARGING;
    sim_power.track.charge_events = 7000;
    sim_run(400);
    EXPECT(sim_display_on && ui_charge_anim_active(), "enchufar no encendio la pantalla");
    sim_run(4000);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(500);

    /* ---- La prueba de pantalla la mantiene encendida ---- */
    sim_tap(205, 300);
    ui_nav_push_opts(ui_touchtest_create, false);
    sim_run(40000);
    EXPECT(sim_display_on && ui_screen_state() == 0, "se apago durante la prueba de pantalla");
    ui_nav_back();
    sim_run(400);

    /* ---- Ajustes > Pantalla > Apagado: 15 -> 30 -> 60 -> 5 s ---- */
    ui_nav_push(ui_display_page_create);
    sim_run(400);
    lv_obj_t *row = clickable_of(sim_find_label(ui_nav_top(), "15 s"));
    EXPECT(row, "Pantalla no muestra el apagado de 15 s");
    for (int i = 0; row && i < 3; i++) {
        lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    EXPECT(ws.screen_to_s == 5 && sim_find_label(ui_nav_top(), "5 s"), "el apagado no paso a 5 s (%u)",
           (unsigned)ws.screen_to_s);
    sim_lint("pantalla con apagado");
    sim_tap(205, 470); /* zona sin controles: solo actividad */
    sim_run(5300);
    EXPECT(!sim_display_on, "no se apago a los 5 s");
    sim_tap(205, 300);
    sim_run(400);

    /* ---- AOD: negro, hora atenuada, brillo bajo y Pixel Shift ---- */
    ws.screen_to_s = 15;
    ws.aod = true;
    watch_settings_update(&ws);
    sim_tap(205, 300);
    sim_run(16000);
    EXPECT(ui_screen_state() == SCR_AOD && sim_display_on && ui_aod_active(), "no paso al AOD (estado %d)",
           ui_screen_state());
    EXPECT(sim_display_level == 8, "brillo fisico del AOD %d (esperado 8)", sim_display_level);
    EXPECT(!ui_screen_is_on(), "el AOD cuenta como pantalla encendida");
    sim_save(out, "f4_aod");
    lv_obj_t *aod_root = lv_obj_get_child(ui_nav_screen(), -1); /* capa del AOD, encima de todo */
    lv_obj_t *box = aod_root ? lv_obj_get_child(aod_root, 0) : NULL;
    const int32_t bx = box ? lv_obj_get_x(box) : 0, by = box ? lv_obj_get_y(box) : 0;
    sim_run(61000);
    EXPECT(box && (lv_obj_get_x(box) != bx || lv_obj_get_y(box) != by), "el AOD no hizo Pixel Shift al cambiar el minuto");
    EXPECT(box && lv_obj_get_x(box) >= -8 && lv_obj_get_x(box) <= 8 && lv_obj_get_y(box) >= -8 &&
               lv_obj_get_y(box) <= 8,
           "Pixel Shift fuera de los limites seguros");
    sim_tap(205, 300);
    sim_run(400);
    EXPECT(ui_screen_state() == 0 && !ui_aod_active() && sim_display_level == -1, "un toque no salio del AOD");
    /* Con el ahorro de energía no hay AOD: se apaga. */
    ui_psave_set(true);
    sim_run(400);
    sim_tap(205, 300);
    sim_run(11000); /* en ahorro el apagado es de 10 s como mucho */
    EXPECT(ui_screen_state() == SCR_OFF && !sim_display_on, "con ahorro de energia no se apago (estado %d)",
           ui_screen_state());
    sim_tap(205, 300);
    sim_run(400);
    ui_psave_set(false);
    sim_run(400);
    EXPECT(sim_brightness == 70, "al salir del ahorro el brillo no volvio a 70 (%d)", sim_brightness);

    ui_screen_enable(false);
    sim_ws_reset(NULL);
    /* Cierra la app que quedó suspendida al apagarse. */
    ui_nav_open_app(ui_about_create);
    sim_run(400);
    ui_nav_home();
    sim_run(1500);
    EXPECT(sim_display_on, "no quedo encendida al terminar");
    sim_check_residue("tras el apagado de pantalla");
    EXPECT(sim_mem_blocks() == blocks0, "fuga en el apagado: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("apagado de pantalla F4.5: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
