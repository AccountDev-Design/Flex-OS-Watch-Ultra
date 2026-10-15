/*
 * Integridad visual de TODAS las pantallas (B4-C4): pasa el verificador de
 * sim_lint.c por cada pantalla y estado, con las listas arriba y al final,
 * y busca residuos tras cada animación (panel rápido, transiciones, aviso y
 * animación de carga).
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"

int sim_lint(const char *where);
int sim_check_residue(const char *where);

static void settle(void)
{
    sim_release();
    sim_run(700);
}

/* Lista de la página de arriba (hijo 1: cabecera, lista). */
static lv_obj_t *top_list(void)
{
    lv_obj_t *page = ui_nav_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(page); i++) {
        lv_obj_t *c = lv_obj_get_child(page, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(c) + lv_obj_get_scroll_top(c) > 0) {
            return c;
        }
    }
    return NULL;
}

static void lint_page(ui_page_create_t create, const char *name, const char *out)
{
    char where[64];
    ui_nav_push(create);
    sim_run(1300);
    snprintf(where, sizeof(where), "%s (arriba)", name);
    sim_lint(where);
    sim_check_residue(where);
    snprintf(where, sizeof(where), "f4_%s", name);
    sim_save(out, where);
    lv_obj_t *list = top_list();
    if (list) {
        lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
        sim_run(100);
        snprintf(where, sizeof(where), "%s (final de la lista)", name);
        sim_lint(where);
        snprintf(where, sizeof(where), "f4_%s_final", name);
        sim_save(out, where);
    }
    ui_nav_back();
    settle();
    snprintf(where, sizeof(where), "tras volver de %s", name);
    sim_check_residue(where);
}

void sim_suite_visual(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 23, 59, 0);
    sim_scenario(100, 4190, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    settle();

    /* Esfera en sus estados (textos largos: 23:59, 100 %, "Externa sin carga") */
    sim_lint("esfera 23:59 100 %");
    sim_scenario(97, 4170, BATT_STATE_EXTERNAL_IDLE, BATT_LEVEL_NORMAL, true);
    sim_set_time(WATCH_TIME_OK, 2026, 9, 30, 20, 8, 0); /* Miércoles, 30 de septiembre */
    settle();
    sim_lint("esfera externa, miercoles");
    sim_set_time(WATCH_TIME_NO_RTC, 2026, 9, 30, 20, 8, 0);
    sim_scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
    sim_power.driver_ok = false;
    settle();
    sim_lint("esfera sin hora ni bateria");
    sim_check_residue("esfera sin hora ni bateria");

    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(100, 4190, BATT_STATE_FULL, BATT_LEVEL_NORMAL, true);
    settle();

    /* Panel rápido */
    ui_qp_open();
    sim_run(400);
    sim_lint("panel rapido");
    sim_save(out, "f4_panel");
    ui_qp_close();
    settle();
    sim_check_residue("tras cerrar el panel rapido");

    /* Ajustes y sus páginas */
    ui_nav_push(ui_settings_create);
    sim_run(1300);
    sim_lint("ajustes");
    sim_save(out, "f4_ajustes");
    lint_page(ui_display_page_create, "pantalla", out);
    lint_page(ui_battery_page_create, "bateria", out);
    lint_page(ui_diag_create, "diagnostico", out);
    lint_page(ui_about_create, "acerca_de", out);
    ui_nav_home();
    settle();
    sim_check_residue("tras volver a la esfera");
    lint_page(ui_touchtest_create, "prueba_pantalla", out);

    /* Aviso de batería baja (quieto) y su salida */
    sim_run(BATT_ALERT_COOLDOWN_MS + 1000);
    sim_scenario(12, 3640, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, false);
    sim_run(1000);
    EXPECT(ui_banner_active(), "visual: el aviso de bateria baja no aparecio");
    sim_lint("aviso de bateria baja");
    sim_save(out, "f4_aviso");
    sim_run(5000);
    sim_check_residue("tras el aviso");

    /* Animación de carga (quieta, al segundo) y su salida */
    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_power.track.prev_state = BATT_STATE_DISCHARGING;
    sim_power.track.charge_events = 5000;
    sim_run(1100);
    EXPECT(ui_charge_anim_active(), "visual: no hubo animacion de carga");
    sim_lint("animacion de carga");
    sim_save(out, "f4_animacion_carga");
    sim_run(3000);
    sim_check_residue("tras la animacion de carga");

    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    settle();
    printf("integridad visual B4-C4: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
