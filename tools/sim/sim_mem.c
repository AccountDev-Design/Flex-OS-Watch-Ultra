/*
 * Memoria y ciclo de vida (F11) en la vista previa:
 *  - selector de esferas: abrir, recorrer, elegir, cerrar y reabrir 8 veces
 *    sin fugas ni temporizadores; ninguna vista previa se renderiza durante
 *    una transición o un gesto; con la PSRAM sin un bloque para la captura, no
 *    hay vistas previas pero el selector funciona (nombres) y se puede elegir;
 *  - navegador: con poca RAM interna no se abre la sesión (nada reservado) y
 *    lo dice con las cifras; en cuanto vuelve la memoria se abre sola; abrir y
 *    cerrar el navegador 8 veces no deja memoria ni temporizadores; cerrarlo
 *    con el dedo apoyado en la página no toca el estado ya liberado.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_browser.h"

uint32_t ui_face_editor_previews_made(void);
void sim_br_reset(void);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

static bool has_prefix(lv_obj_t *root, const char *prefix)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        strncmp(lv_label_get_text(root), prefix, strlen(prefix)) == 0) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        if (has_prefix(lv_obj_get_child(root, (int32_t)i), prefix)) {
            return true;
        }
    }
    return false;
}

static void open_other_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_open_app(ui_about_create);
}

static void open_editor(void)
{
    sim_press(205, 300);
    sim_run(700);
    sim_release();
}

void sim_suite_mem(const char *out)
{
    (void)out;
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_ws_reset(NULL);
    ui_nav_replace_root(ui_face_create);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1200);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* Lo que LVGL tiene reservado con cada pantalla (objetos, estilos, textos).
     * En el reloj, con LV_STDLIB_CLIB y SPIRAM_MALLOC_ALWAYSINTERNAL=16384, todo
     * esto iba a la RAM interna; ahora va a la PSRAM (lv_mem_watch.c). */
    {
        (void)sim_mem_blocks(); /* vacía las cachés */
        const size_t face_b = sim_mem_used();
        ui_nav_push(ui_apps_create);
        sim_run(700);
        (void)sim_mem_blocks();
        const size_t drawer_b = sim_mem_used();
        ui_nav_home();
        sim_run(600);
        ui_nav_push(ui_settings_create);
        sim_run(700);
        (void)sim_mem_blocks();
        const size_t set_b = sim_mem_used();
        ui_nav_home();
        sim_run(600);
        ui_nav_push(ui_devcare_create);
        sim_run(700);
        (void)sim_mem_blocks();
        const size_t dc_b = sim_mem_used();
        ui_nav_home();
        sim_run(600);
        printf("  LVGL en uso: esfera %u KB, +cajon %u KB, +Ajustes %u KB, +Device Care %u KB\n",
               (unsigned)(face_b / 1024), (unsigned)(drawer_b / 1024), (unsigned)(set_b / 1024),
               (unsigned)(dc_b / 1024));
    }

    /* ---- Selector: ciclos completos ---- */
    for (int round = 0; round < 8; round++) {
        const uint32_t made0 = ui_face_editor_previews_made();
        open_editor();
        /* durante la apertura (ui_nav ocupada) no se renderiza ninguna */
        EXPECT(ui_face_editor_previews_made() == made0 || !ui_nav_busy(),
               "selector %d: vista previa durante la transicion", round);
        sim_run(UI_FACE_COUNT_ * 50 + 300);
        EXPECT(ui_face_editor_previews_made() - made0 == UI_FACE_COUNT_, "selector %d: %u vistas previas", round,
               (unsigned)(ui_face_editor_previews_made() - made0));
        /* recorrer */
        for (int k = 0; k < 3; k++) {
            sim_drag(300, 250, 80, 252, 8, 16);
            sim_release();
            sim_run(500);
        }
        /* elegir (una ronda de cada tres) o salir con un gesto */
        if (round % 3 == 0) {
            lv_obj_t *it = clickable_of(sim_find_label(ui_nav_top(), ui_face_name(round % UI_FACE_COUNT_)));
            if (it) {
                lv_obj_send_event(it, LV_EVENT_CLICKED, NULL);
            }
        } else if (round % 3 == 1) {
            sim_drag(4, 300, 300, 300, 12, 20);
            sim_release();
        } else {
            sim_drag(FLEX_SCREEN_W - 4, 300, 100, 300, 12, 20);
            sim_release();
        }
        sim_run(700);
        ui_nav_home();
        sim_run(400);
    }
    /* Gesto de borde a mitad de la generación: nada se renderiza mientras arrastra */
    open_editor();
    sim_run(400); /* termina la apertura; empieza a generar */
    const uint32_t m1 = ui_face_editor_previews_made();
    sim_drag(FLEX_SCREEN_W - 4, 300, 330, 300, 6, 60); /* lento: el arrastre dura 360 ms */
    EXPECT(ui_face_editor_previews_made() == m1, "selector: se renderizo una vista previa durante el gesto");
    sim_release();
    sim_run(800);
    ui_nav_home();
    sim_run(400);

    /* Sin un bloque de PSRAM para la captura: sin vistas previas, sin fallo */
    sim_mem_psram_block = 200 * 1024;
    const uint32_t m2 = ui_face_editor_previews_made();
    open_editor();
    sim_run(UI_FACE_COUNT_ * 50 + 300);
    EXPECT(sim_find_label(ui_nav_top(), "Esferas") && ui_face_editor_previews_made() == m2,
           "selector sin PSRAM: genero vistas previas o no se abrio");
    lv_obj_t *it = clickable_of(sim_find_label(ui_nav_top(), "Digital"));
    if (it) {
        lv_obj_send_event(it, LV_EVENT_CLICKED, NULL);
    }
    sim_run(600);
    EXPECT(ui_nav_depth() == 1, "selector sin PSRAM: elegir no volvio a la esfera");
    sim_mem_psram_block = 6900u * 1024;
    ui_nav_home();
    sim_run(400);

    /* ---- Navegador con poca RAM interna ---- */
    sim_br_reset();
    BrSettings st;
    watch_browser_get_settings(&st);
    snprintf(st.server, sizeof(st.server), "wss://nav.example.com/v1/session");
    snprintf(st.token, sizeof(st.token), "credencial-de-prueba-1234567890");
    watch_browser_set_settings(&st);
    sim_mem_internal_free = 20 * 1024;
    ui_nav_push(ui_browser_create);
    sim_run(800);
    EXPECT(watch_browser_net() == NULL, "navegador: abrio la sesion sin memoria");
    EXPECT(has_prefix(ui_nav_top(), "Memoria insuficiente. Falta RAM interna"),
           "navegador: no explica que falta RAM interna");
    sim_mem_internal_free = 120 * 1024; /* p. ej. se cerró otra app */
    sim_run(3500);
    EXPECT(watch_browser_net() != NULL, "navegador: no reintento al volver la memoria");
    ui_nav_home();
    sim_run(600);
    EXPECT(watch_browser_net() == NULL, "navegador: la sesion sigue abierta al cerrar");
    for (int i = 0; i < 8; i++) {
        ui_nav_push(ui_browser_create);
        sim_run(900);
        ui_nav_back();
        sim_run(500);
    }
    /* Regresión (semilla 13 con ASan): el navegador suspendido se descarta al
     * tocar otra app en el cajón; el lienzo (LV_EVENT_ALL) recibía
     * LV_EVENT_DELETE dentro del procesado del toque, con el estado ya
     * liberado por delete_cb -> puntero nulo (en el reloj, LoadProhibited y
     * reinicio). Un botón en la capa superior hace de icono del cajón. */
    for (int i = 0; i < 3; i++) {
        watch_browser_set_last("https://ejemplo.com"); /* abre directamente la página remota */
        ui_nav_open_app(ui_browser_create);
        sim_run(1500);
        ui_nav_suspend(true); /* botón lateral: la app queda suspendida */
        sim_run(600);
        EXPECT(ui_nav_suspended_count() == 1, "navegador: no quedo suspendido (%d)", i);
        lv_obj_t *icon = lv_obj_create(lv_layer_top());
        lv_obj_set_size(icon, 120, 120);
        lv_obj_center(icon);
        lv_obj_add_event_cb(icon, open_other_cb, LV_EVENT_CLICKED, NULL);
        sim_tap(FLEX_SCREEN_W / 2, FLEX_SCREEN_H / 2);
        lv_obj_delete(icon);
        sim_run(600);
        EXPECT(ui_nav_suspended_count() == 0 && ui_nav_depth() == 2, "navegador: no se descarto al abrir otra app (%d)",
               i);
        ui_nav_home();
        sim_run(400);
        ui_nav_discard_suspended();
    }
    sim_br_reset();
    sim_run(300);

    EXPECT(sim_timer_count() == timers0, "memoria: temporizadores %d -> %d", timers0, sim_timer_count());
    EXPECT(sim_mem_blocks() <= blocks0 + 2, "memoria: bloques de LVGL %u -> %u", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    printf("memoria y ciclo de vida F11: %s (%u -> %u bloques, %d temporizadores)\n", sim_fails == f0 ? "OK" : "FALLOS",
           (unsigned)blocks0, (unsigned)sim_mem_blocks(), sim_timer_count());
}
