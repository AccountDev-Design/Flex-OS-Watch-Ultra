/*
 * Apps de tiempo (F4.3) en la vista previa:
 *  - deslizar la esfera a la izquierda abre el lanzador;
 *  - cronómetro: iniciar, vueltas, pausa, seguir, reiniciar; sigue contando
 *    fuera de su página;
 *  - temporizador: atajo, iniciar, pausa, aviso "Tiempo agotado" en
 *    cualquier pantalla, +1 min y detener;
 *  - alarmas: crear, editar, una sola vez (se desactiva al sonar), posponer
 *    5 min, eliminar; se guardan (bloque de watch_settings);
 *  - integridad visual de todas las páginas y del aviso, y sin fugas.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"
#include "watch_time.h"

int sim_lint(const char *where);
int sim_check_residue(const char *where);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

/* Toca lo que contiene ese texto (en root), desplazándolo a la vista. */
static bool tap_text(lv_obj_t *root, const char *text)
{
    lv_obj_t *o = clickable_of(sim_find_label(root, text));
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

static lv_obj_t *roller_n(lv_obj_t *o, int idx, int *n)
{
    if (lv_obj_check_type(o, &lv_roller_class) && (*n)++ == idx) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = roller_n(lv_obj_get_child(o, (int32_t)i), idx, n);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *roller(int idx)
{
    int n = 0;
    return roller_n(ui_nav_top(), idx, &n);
}

static void lint_page(const char *name, const char *out, const char *png)
{
    char w[64];
    snprintf(w, sizeof(w), "%s", name);
    sim_lint(w);
    sim_check_glyphs(ui_nav_top(), name);
    if (png) {
        sim_save(out, png);
    }
}

void sim_suite_apps(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_ws_reset(NULL);
    watch_time_set_tz(-300);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 6, 58, 0); /* jueves 6:58 */
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1100);
    const alarm_set_t none = {0};
    ui_alarms_set(&none);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* ---- Lanzador: deslizar la esfera hacia la izquierda ---- */
    sim_drag(330, 260, 120, 262, 6, 16);
    sim_release();
    sim_run(400);
    EXPECT(ui_nav_depth() == 2 && sim_find_label(ui_nav_top(), "Aplicaciones"), "deslizar no abrio las apps");
    EXPECT(sim_find_label(ui_nav_top(), "Parado") && sim_find_label(ui_nav_top(), "Listo") &&
               sim_find_label(ui_nav_top(), "Ninguna"),
           "el lanzador no muestra el estado de las apps");
    lint_page("aplicaciones", out, "f4_apps");
    sim_check_residue("aplicaciones");

    /* ---- Cronómetro ---- */
    EXPECT(tap_text(ui_nav_top(), "Cronómetro"), "no se abrio el cronometro");
    EXPECT(sim_find_label(ui_nav_top(), "00:00") && sim_find_label(ui_nav_top(), "Sin vueltas"),
           "cronometro: estado inicial");
    lint_page("cronometro parado", out, NULL);
    EXPECT(!tap_text(ui_nav_top(), "Vuelta"), "cronometro: Vuelta no esta desactivado parado");
    EXPECT(tap_text(ui_nav_top(), "Iniciar"), "cronometro: no hay Iniciar");
    sim_run(1600);
    EXPECT(tap_text(ui_nav_top(), "Vuelta"), "cronometro: no hay Vuelta en marcha");
    sim_run(2500);
    tap_text(ui_nav_top(), "Vuelta");
    sim_run(700);
    tap_text(ui_nav_top(), "Vuelta");
    sim_run(1200);
    EXPECT(ui_sw()->n == 3 && ui_sw()->st == SW_RUN, "cronometro: %d vueltas", ui_sw()->n);
    EXPECT(sim_find_label(ui_nav_top(), "Vuelta 3") && sim_find_label(ui_nav_top(), "Vuelta 1"),
           "cronometro: la tabla no muestra las vueltas");
    lint_page("cronometro con vueltas", out, "f4_cronometro");
    EXPECT(tap_text(ui_nav_top(), "Pausa"), "cronometro: no hay Pausa");
    const int64_t paused = sw_elapsed(ui_sw(), ui_mono_us());
    sim_run(2000);
    EXPECT(sw_elapsed(ui_sw(), ui_mono_us()) == paused, "cronometro: avanza en pausa");
    EXPECT(tap_text(ui_nav_top(), "Seguir"), "cronometro: no hay Seguir");
    ui_nav_back();
    sim_run(2000);
    EXPECT(ui_sw()->st == SW_RUN && sw_elapsed(ui_sw(), ui_mono_us()) > paused + 1900000,
           "cronometro: no sigue contando fuera de su pagina");
    sim_run(600);
    lv_obj_t *swt = clickable_of(sim_find_label(ui_nav_top(), "Cronómetro"));
    EXPECT(swt && !sim_find_label(swt, "Parado"), "el lanzador no muestra el cronometro en marcha");
    tap_text(ui_nav_top(), "Cronómetro");
    tap_text(ui_nav_top(), "Pausa");
    EXPECT(tap_text(ui_nav_top(), "Reiniciar"), "cronometro: no hay Reiniciar en pausa");
    EXPECT(ui_sw()->st == SW_IDLE && ui_sw()->n == 0, "cronometro: no se reinicio");
    ui_nav_back();
    sim_run(400);

    /* ---- Temporizador: atajo de 1 min, pausa, aviso en otra pantalla ---- */
    EXPECT(tap_text(ui_nav_top(), "Temporizador"), "no se abrio el temporizador");
    lint_page("temporizador parado", out, "f4_temporizador");
    EXPECT(tap_text(ui_nav_top(), "1"), "temporizador: no hay atajo de 1 min");
    sim_run(400);
    EXPECT(lv_roller_get_selected(roller(0)) == 1 && lv_roller_get_selected(roller(1)) == 0,
           "temporizador: el atajo no pone 01:00");
    EXPECT(tap_text(ui_nav_top(), "Iniciar"), "temporizador: no hay Iniciar");
    sim_run(1100);
    EXPECT(ui_tmr()->st == TMR_RUN && sim_find_label(ui_nav_top(), "00:59"), "temporizador: no cuenta");
    lint_page("temporizador en marcha", out, "f4_temporizador_marcha");
    EXPECT(tap_text(ui_nav_top(), "Pausa"), "temporizador: no hay Pausa");
    sim_run(3000);
    EXPECT(sim_find_label(ui_nav_top(), "00:59"), "temporizador: avanza en pausa");
    EXPECT(tap_text(ui_nav_top(), "Seguir"), "temporizador: no hay Seguir");
    ui_nav_home();
    sim_run(400);
    ui_nav_push(ui_settings_create); /* el aviso debe salir en cualquier pantalla */
    sim_run(60000);
    EXPECT(ui_ring_active() && ui_tmr()->st == TMR_RING, "temporizador: no aviso al llegar a 0");
    EXPECT(ui_ring_obj() && sim_find_label(ui_ring_obj(), "Tiempo agotado"), "temporizador: aviso sin titulo");
    sim_run(700);
    sim_lint("aviso del temporizador");
    if (ui_ring_obj()) {
        sim_check_glyphs(ui_ring_obj(), "aviso del temporizador");
    }
    sim_save(out, "f4_aviso_temporizador");
    EXPECT(ui_ring_obj() && tap_text(ui_ring_obj(), "+1 min"), "aviso: no hay +1 min");
    EXPECT(!ui_ring_active() && ui_tmr()->st == TMR_RUN, "+1 min no reinicia el temporizador");
    sim_run(60200);
    EXPECT(ui_ring_active(), "temporizador: +1 min no volvio a avisar");
    EXPECT(ui_ring_obj() && tap_text(ui_ring_obj(), "Detener"), "aviso: no hay Detener");
    EXPECT(!ui_ring_active() && ui_tmr()->st == TMR_IDLE, "Detener no para el temporizador");
    sim_check_residue("tras el aviso del temporizador");
    ui_nav_home();
    sim_run(400);

    /* ---- Alarmas ---- */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 6, 58, 0);
    ui_nav_push(ui_alarms_create);
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "Sin alarmas"), "alarmas: estado vacio");
    EXPECT(tap_text(ui_nav_top(), "Añadir alarma"), "alarmas: no hay Añadir");
    EXPECT(sim_find_label(ui_nav_top(), "Nueva alarma"), "alarmas: no se abrio el editor");
    lint_page("editor de alarma", out, "f4_alarma_editor");
    /* 7:00 una sola vez: se quitan los días L-V */
    static const char *const days[] = {"L", "M", "X", "J", "V"};
    for (int i = 0; i < 5; i++) {
        EXPECT(tap_text(ui_nav_top(), days[i]), "alarmas: no hay chip %s", days[i]);
    }
    EXPECT(tap_text(ui_nav_top(), "Guardar"), "alarmas: no hay Guardar");
    EXPECT(ui_alarms()->n == 1 && ui_alarms()->a[0].hour == 7 && ui_alarms()->a[0].days == 0,
           "alarmas: no se guardo 7:00 una vez");
    uint8_t blob[64], ver = 0;
    size_t bn = sizeof(blob);
    EXPECT(watch_settings_blob_get(WS_BLOB_ALARMS, &ver, blob, &bn) == ESP_OK && ver == AL_BLOB_VER && bn == 4,
           "alarmas: no se guardaron en los ajustes");
    sim_run(600);
    EXPECT(sim_find_label(ui_nav_top(), "7:00") && sim_find_label(ui_nav_top(), "Una vez"),
           "alarmas: la lista no muestra la alarma");
    EXPECT(sim_find_label(ui_nav_top(), "hoy 7:00"), "alarmas: no muestra la proxima");
    lint_page("alarmas", out, "f4_alarmas");

    /* Suena a las 7:00 (en otra pantalla) y se desactiva; posponer 5 min */
    ui_nav_home();
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 6, 59, 30);
    sim_run(20000);
    EXPECT(!ui_ring_active(), "alarma: sono antes de las 7:00");
    sim_run(20000);
    EXPECT(ui_ring_active() && sim_find_label(ui_ring_obj(), "Alarma"), "alarma: no sono a las 7:00");
    EXPECT(ui_alarms()->a[0].on == 0, "alarma: una vez no se desactivo");
    sim_lint("aviso de alarma");
    sim_save(out, "f4_aviso_alarma");
    EXPECT(ui_ring_obj() && tap_text(ui_ring_obj(), "Posponer"), "aviso: no hay Posponer");
    EXPECT(!ui_ring_active(), "Posponer no cierra");
    sim_run(4 * 60000);
    EXPECT(!ui_ring_active(), "alarma pospuesta sono antes de 5 min");
    sim_run(61000);
    EXPECT(ui_ring_active(), "alarma pospuesta no volvio a sonar");
    if (ui_ring_obj()) {
        tap_text(ui_ring_obj(), "Detener");
    }
    EXPECT(!ui_ring_active(), "Detener no cierra la alarma");

    /* Editar: reactivar con L a V y eliminar */
    ui_nav_push(ui_alarms_create);
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "Desactivada"), "alarmas: no se ve desactivada");
    EXPECT(tap_text(ui_nav_top(), "7:00"), "alarmas: no se abre para editar");
    EXPECT(sim_find_label(ui_nav_top(), "Editar alarma"), "alarmas: no es el editor de una existente");
    tap_text(ui_nav_top(), "Desactivada");
    for (int i = 0; i < 5; i++) {
        tap_text(ui_nav_top(), days[i]);
    }
    tap_text(ui_nav_top(), "Guardar");
    sim_run(600);
    EXPECT(ui_alarms()->a[0].on == 1 && ui_alarms()->a[0].days == 0x3E, "alarmas: la edicion no se guardo");
    EXPECT(sim_find_label(ui_nav_top(), "L a V"), "alarmas: la lista no se rehizo tras editar");
    tap_text(ui_nav_top(), "7:00");
    EXPECT(tap_text(ui_nav_top(), "Eliminar"), "alarmas: no hay Eliminar");
    sim_run(600);
    EXPECT(ui_alarms()->n == 0 && sim_find_label(ui_nav_top(), "Sin alarmas"), "alarmas: no se elimino");

    ui_nav_home();
    sim_run(1500);
    sim_check_residue("tras las apps");
    EXPECT(sim_mem_blocks() == blocks0, "fuga en las apps: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_run(1100);
    printf("apps de tiempo F4.3: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
