/*
 * ui_internal — funciones compartidas entre los archivos de watch_ui.
 * Todas se llaman desde la tarea de LVGL (o con su bloqueo tomado).
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "battery_alerts.h"
#include "watch_power.h"
#include "watch_ui.h"

/* ---- ui_system: sondeo único del servicio de batería y capas globales ---- */

/** Arranca el temporizador del sistema. power_ok = el servicio de batería arrancó. */
void ui_system_start(bool power_ok);

/** Copia de la última instantánea de batería. false si todavía no hay ninguna. */
bool ui_system_power(watch_power_snapshot_t *out);

/* ---- ui_charge_anim: animación de inicio de carga (lv_layer_top) ---- */

/** Reproduce la animación una vez (si ya hay una en curso, no hace nada). */
void ui_charge_anim_play(int percent);
bool ui_charge_anim_active(void);

/* ---- ui_banner: aviso emergente de vidrio (lv_layer_top) ---- */

/** Muestra el aviso de batería baja o crítica (o lo actualiza si ya se ve). */
void ui_banner_show_battery(batt_alert_t alert, int percent);
bool ui_banner_active(void);

/* ---- Pantallas ---- */

/** Datos del arranque (táctil, batería, flash) guardados por watch_ui_home_create(). */
const watch_ui_boot_info_t *ui_boot_info(void);

/** Esfera: hora, fecha y batería. Devuelve la pantalla sin cargarla. */
lv_obj_t *ui_face_create(void);

/** Diagnóstico (antigua pantalla de inicio de B1). NULL si ya existe una. */
lv_obj_t *ui_diag_create(void);
