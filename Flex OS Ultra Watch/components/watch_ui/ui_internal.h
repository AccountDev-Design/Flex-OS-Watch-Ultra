/*
 * ui_internal — funciones compartidas entre los archivos de watch_ui.
 * Todas se llaman desde la tarea de LVGL (o con su bloqueo tomado).
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "battery_alerts.h"
#include "watch_power.h"
#include "ui_clock_logic.h"
#include "watch_ui.h"

/* ---- ui_system: sondeo único del servicio de batería y capas globales ---- */

/** Arranca el temporizador del sistema. power_ok = el servicio de batería arrancó. */
void ui_system_start(bool power_ok);

/** Copia de la última instantánea de batería. false si todavía no hay ninguna. */
bool ui_system_power(watch_power_snapshot_t *out);

/**
 * Suscripción a los cambios de batería: cb se llama en la tarea de LVGL en
 * cuanto ui_system detecta una instantánea nueva (y una vez al suscribirse si
 * ya hay datos). Se anula sola al borrarse obj. false si no hay hueco.
 */
typedef void (*ui_power_cb_t)(lv_obj_t *obj, const watch_power_snapshot_t *snap);
bool ui_power_listen(lv_obj_t *obj, ui_power_cb_t cb);
int ui_power_listener_count(void);

/* ---- ui_charge_anim: animación de inicio de carga (lv_layer_top) ---- */

/** Reproduce la animación una vez (si ya hay una en curso, no hace nada). */
void ui_charge_anim_play(int percent);
bool ui_charge_anim_active(void);
lv_obj_t *ui_charge_anim_obj(void);

/* ---- ui_banner: aviso emergente de vidrio (lv_layer_top) ---- */

/** Muestra el aviso de batería baja o crítica (o lo actualiza si ya se ve). */
void ui_banner_show_battery(batt_alert_t alert, int percent);
bool ui_banner_active(void);

/* ---- Navegación (ui_nav) ----
 * Las páginas son hijas de una sola pantalla. Una función de creación recibe
 * el contenedor padre y devuelve la página (ui_page_new) o NULL si no pudo. */
typedef lv_obj_t *(*ui_page_create_t)(lv_obj_t *parent);

/** Contenedor de página a pantalla completa, sin estilo ni desplazamiento. */
lv_obj_t *ui_page_new(lv_obj_t *parent);
/** Crea la pantalla de navegación con su página raíz (sin cargarla). */
lv_obj_t *ui_nav_start(ui_page_create_t root);
/** Abre una página encima (210 ms). false si hay una transición en curso o no cabe. */
bool ui_nav_push(ui_page_create_t create);
/** Igual, pero sin el gesto "volver" del borde (p. ej. la prueba táctil, que
 * necesita el borde izquierdo); se vuelve con el botón de la cabecera. */
bool ui_nav_push_opts(ui_page_create_t create, bool edge_gesture);
/** Vuelve a la página anterior (190 ms). false en la raíz. */
bool ui_nav_back(void);
/** Vuelve a la raíz (la esfera) con una sola animación. */
void ui_nav_home(void);
/** Rehace la página raíz (la esfera) debajo de las páginas abiertas: para
 * cambios de ajustes que afectan a su construcción (fondo, estilo). */
bool ui_nav_replace_root(ui_page_create_t root);
int ui_nav_depth(void);
bool ui_nav_busy(void);
lv_obj_t *ui_nav_top(void);
lv_obj_t *ui_nav_screen(void);
/** Página i de la pila (0 = raíz) o NULL. */
lv_obj_t *ui_nav_page(int i);
/** Padre de las capas globales opacas a pantalla completa (panel rápido,
 * animación de carga): la pantalla de navegación, encima de las páginas. */
lv_obj_t *ui_overlay_parent(void);

/* ---- Páginas ---- */

/** Datos del arranque (táctil, batería, flash) guardados por watch_ui_home_create(). */
const watch_ui_boot_info_t *ui_boot_info(void);

/** Esfera: hora, fecha y batería (página raíz). */
lv_obj_t *ui_face_create(lv_obj_t *parent);

/** Ajustes > Diagnóstico (con los datos de la pantalla de inicio de B1). */
lv_obj_t *ui_diag_create(lv_obj_t *parent);

/** Raíz de Ajustes y sus páginas. */
lv_obj_t *ui_settings_create(lv_obj_t *parent);
lv_obj_t *ui_display_page_create(lv_obj_t *parent);
lv_obj_t *ui_battery_page_create(lv_obj_t *parent);
lv_obj_t *ui_about_create(lv_obj_t *parent);
/* Apps de tiempo (F4.3): estado y motor (ui_clock_core), páginas */
int64_t ui_mono_us(void);                 /**< reloj monótono (esp_timer) */
sw_t *ui_sw(void);                        /**< el cronómetro (sigue fuera de su página) */
tmr_t *ui_tmr(void);                      /**< el temporizador */
const alarm_set_t *ui_alarms(void);
void ui_alarms_set(const alarm_set_t *set); /**< aplica y guarda (watch_settings) */
uint32_t ui_alarms_gen(void);             /**< cambia con cada cambio de alarmas */
bool ui_clock_local_now(int64_t *local_s); /**< segundos locales; false sin hora */
void ui_clock_init(void);                 /**< carga las alarmas guardadas */
void ui_clock_tick(void);                 /**< cada 100 ms (ui_system): fin del temporizador y alarmas */
bool ui_ring_active(void);                /**< aviso de alarma o temporizador en pantalla */
lv_obj_t *ui_ring_obj(void);
void ui_ring_dismiss(void);
lv_obj_t *ui_apps_create(lv_obj_t *parent);
void ui_apps_attach(lv_obj_t *face_page); /**< deslizar la esfera a la izquierda abre las apps */
lv_obj_t *ui_stopwatch_create(lv_obj_t *parent);
lv_obj_t *ui_timer_create(lv_obj_t *parent);
lv_obj_t *ui_alarms_create(lv_obj_t *parent);
lv_obj_t *ui_alarm_edit_create(lv_obj_t *parent);
/** Rodillo con el estilo del sistema (3 filas visibles, la elegida resaltada). */
lv_obj_t *ui_roller(lv_obj_t *parent, const char *opts, lv_roller_mode_t mode, const lv_font_t *font, int32_t w);
/* Apagado de pantalla (F4.5, ui_screen.c) */
void ui_screen_init(void);
void ui_screen_tick(void);              /**< cada 100 ms (ui_system) */
void ui_screen_wake(void);              /**< enciende (o mantiene encendida) la pantalla */
void ui_screen_enable(bool on);         /**< false: no se apaga (pruebas) */
bool ui_screen_is_on(void);
int ui_screen_state(void);              /**< scr_state_t */
uint32_t ui_screen_offs(void);          /**< veces que se apagó */
uint32_t ui_screen_wakes(void);         /**< veces que se encendió */
int ui_screen_button_idle(void);        /**< nivel en reposo aprendido del botón (-1 = aún no) */
void ui_screen_hold_page(lv_obj_t *page); /**< no apagar mientras exista esta página */

/* Fecha y hora (F4.2): lista, zona horaria y poner en hora */
lv_obj_t *ui_datetime_create(lv_obj_t *parent);
lv_obj_t *ui_tz_create(lv_obj_t *parent);
lv_obj_t *ui_timeset_create(lv_obj_t *parent);
/** Prueba de pantalla (marco de 1 px) y táctil (coordenadas). */
lv_obj_t *ui_touchtest_create(lv_obj_t *parent);

/** Control de brillo compartido (panel rápido y Pantalla), de w x 64 px. */
lv_obj_t *ui_bright_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w);

/** Temporizador ligado a una página: se borra con ella. */
lv_timer_t *ui_page_timer(lv_obj_t *page, lv_timer_cb_t cb, uint32_t period_ms, void *user_data);

/* ---- Panel rápido (ui_qp) ---- */

/** Añade a la esfera la franja superior que abre el panel al arrastrar. */
void ui_qp_attach(lv_obj_t *face_page);
void ui_qp_open(void);
void ui_qp_close(void);
bool ui_qp_is_open(void);
/** El panel existe (abierto, abriéndose, arrastrándose o cerrándose). */
bool ui_qp_active(void);
lv_obj_t *ui_qp_obj(void);

/** Splash de arranque activo (watch_ui_boot_splash). */
bool ui_splash_active(void);
