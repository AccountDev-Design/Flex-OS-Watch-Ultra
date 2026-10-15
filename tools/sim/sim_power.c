/*
 * Pruebas de B2 en la vista previa: esfera con cada estado de batería, avisos
 * de batería baja/crítica y animación de inicio de carga.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"

void sim_suite_power(const char *out)
{
    /* La interfaz ya arrancó en sim_suite_boot(): esfera con hora válida. */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1200);
    sim_save(out, "b1_descargando");

    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_run(1200);
    sim_save(out, "b1_cargando");

    sim_scenario(12, 3640, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, false);
    sim_run(1200);
    sim_save(out, "b1_baja");
    /* B2-5: aviso de batería baja visible y que se va solo (~4,6 s) */
    EXPECT(ui_banner_active(), "el aviso de bateria baja no se mostro");
    sim_run(4000);
    EXPECT(!ui_banner_active(), "el aviso de bateria baja no se oculto");
    sim_run(2000);
    EXPECT(!ui_banner_active(), "el aviso de bateria baja se repitio");

    sim_scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
    sim_power.driver_ok = false;
    sim_run(1200);
    sim_save(out, "b1_sin_datos");

    sim_scenario(98, 4180, BATT_STATE_FULL, BATT_LEVEL_NORMAL, true);
    sim_run(1200);
    sim_save(out, "b2_completa");

    sim_scenario(97, 4170, BATT_STATE_EXTERNAL_IDLE, BATT_LEVEL_NORMAL, true);
    sim_run(1200);
    sim_save(out, "b2_externa");

    sim_scenario(4, 3450, BATT_STATE_DISCHARGING, BATT_LEVEL_CRITICAL, false);
    sim_run(1200);
    sim_save(out, "b2_critica");
    EXPECT(ui_banner_active(), "el aviso critico no se mostro");
    sim_run(4000);
    EXPECT(!ui_banner_active(), "el aviso critico no se oculto");

    /* B2-4: enchufar desde descarga -> una animación, que se borra sola. */
    sim_scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    sim_power.track.prev_state = BATT_STATE_DISCHARGING;
    sim_power.track.charge_events = 1;
    sim_run(1200);
    sim_save(out, "b2_animacion_carga");
    EXPECT(ui_charge_anim_active(), "la animacion de carga no se mostro");
    sim_run(2600);
    EXPECT(!ui_charge_anim_active(), "la animacion sigue activa a los 3,8 s");
    EXPECT(lv_anim_count_running() == 0, "%u animaciones vivas", (unsigned)lv_anim_count_running());
    EXPECT(lv_obj_get_child_count(lv_layer_top()) == 0, "quedan objetos en layer_top");
    /* Sin evento nuevo no se repite */
    sim_run(1200);
    EXPECT(!ui_charge_anim_active(), "la animacion se repitio sin evento");
}
