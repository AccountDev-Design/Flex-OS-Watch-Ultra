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
#include "watch_phone.h"

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
/** Aviso de una notificación del teléfono; tocarlo abre su detalle. */
void ui_banner_show_notif(const char *app, const char *text, uint32_t notif_id);
/** Aviso del sistema de actualizaciones (F7); open: tocarlo abre Ajustes > Actualizaciones. */
void ui_banner_show_ota(const char *title, const char *text, uint32_t title_rgb, bool open);
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
lv_obj_t *ui_wallpaper_create(lv_obj_t *parent); /**< Ajustes > Pantalla > Fondo (F3.6) */
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
lv_obj_t *ui_reloj_create(lv_obj_t *parent); /**< app Reloj: alarmas, cronómetro y temporizador */
lv_obj_t *ui_calc_create(lv_obj_t *parent);     /**< app Calculadora (F9.2) */
lv_obj_t *ui_calendar_create(lv_obj_t *parent); /**< app Calendario (F9.3) */
lv_obj_t *ui_compass_create(lv_obj_t *parent);  /**< app Flex Compass (F9.4) */
lv_obj_t *ui_devcare_create(lv_obj_t *parent);  /**< app Device Care (F9.5) */
lv_obj_t *ui_wifi_create(lv_obj_t *parent);     /**< Ajustes > Wi-Fi (F5.1) */
lv_obj_t *ui_wifi_password_create(lv_obj_t *parent);
/* Teclado de pantalla compartido (ui_keyboard.c) */
enum { UI_KB_ASCII = 0, UI_KB_TEXT = 1, UI_KB_NEWLINE = 2 };
typedef void (*ui_kb_cb_t)(void *ud);
/** Teclado que escribe en ta. flags: UI_KB_TEXT (ñ, tildes), UI_KB_NEWLINE ("Intro" en vez de "OK"). */
lv_obj_t *ui_keyboard_create(lv_obj_t *parent, lv_obj_t *ta, int flags, ui_kb_cb_t ok, void *ud);
/** Estilo de campo de texto del sistema (cursor fijo, sin parpadeo). */
void ui_textarea_style(lv_obj_t *ta);
/* Notas y archivos (F9.6, F9.7; ui_notes.c, ui_files.c) */
lv_obj_t *ui_notes_create(lv_obj_t *parent);       /**< app Notas: lista de /Notas */
lv_obj_t *ui_note_edit_create(lv_obj_t *parent);   /**< editor de la nota de ui_note_open() */
void ui_note_open(const char *path);
lv_obj_t *ui_paint_create(lv_obj_t *parent);        /**< app Paint: galería de /Paint (F9.11) */
lv_obj_t *ui_paint_canvas_create(lv_obj_t *parent); /**< lienzo del dibujo de ui_paint_open() */
void ui_paint_open(const char *path);
lv_obj_t *ui_files_create(lv_obj_t *parent);       /**< explorador (carpeta de ui_files_open_dir, o "/") */
lv_obj_t *ui_file_opts_create(lv_obj_t *parent);   /**< opciones del archivo de ui_file_options() */
lv_obj_t *ui_file_rename_create(lv_obj_t *parent);
void ui_files_open_dir(const char *dir);
void ui_file_options(const char *path);
void ui_files_changed(void);                        /**< algo cambió en el disco: las listas se rehacen */
uint32_t ui_files_gen(void);
/** ¿Hay almacenamiento montado? Si no, *why dice por qué. Crea las carpetas fijas la primera vez. */
bool ui_fs_ready(const char **why);
/** Tarjeta "<app> sin almacenamiento" con el motivo (fkNoFsScreen de la referencia). */
lv_obj_t *ui_fs_notice(lv_obj_t *list, const char *app, const char *why);
/* Flex Phone (F5.2/F5.3/F9.9/F9.10) */
lv_obj_t *ui_flexphone_create(lv_obj_t *parent);    /**< app Flex Phone: estado, emparejamiento, secciones */
lv_obj_t *ui_phone_conn_create(lv_obj_t *parent);   /**< Flex Phone > Conexión */
lv_obj_t *ui_phone_state_create(lv_obj_t *parent);  /**< Flex Phone > Estado del teléfono */
lv_obj_t *ui_notifs_create(lv_obj_t *parent);       /**< notificaciones del teléfono */
lv_obj_t *ui_notif_detail_create(lv_obj_t *parent); /**< la elegida con ui_notif_open() */
lv_obj_t *ui_notif_reply_create(lv_obj_t *parent);
lv_obj_t *ui_music_create(lv_obj_t *parent);        /**< app Música: lo que suena en el teléfono */
/* Clima (F9.8; ui_weather.c) */
lv_obj_t *ui_weather_create(lv_obj_t *parent);      /**< app Clima: Open-Meteo por Wi-Fi */
lv_obj_t *ui_wx_locs_create(lv_obj_t *parent);      /**< Clima > Mis ubicaciones */
lv_obj_t *ui_wx_search_create(lv_obj_t *parent);    /**< Clima > Buscar ciudad */
/* Actualizaciones (F7; ui_ota.c) */
lv_obj_t *ui_ota_create(lv_obj_t *parent);          /**< Ajustes > Actualizaciones */
lv_obj_t *ui_ota_install_create(lv_obj_t *parent);  /**< pantalla de instalación (sin gesto "volver") */
/** Cada 100 ms (ui_system): aviso de versión nueva en la esfera y aviso tras reiniciar. */
void ui_ota_watch_tick(void);
/* Navegador (F6; ui_browser.c, ui_jpeg.c) */
lv_obj_t *ui_browser_create(lv_obj_t *parent);      /**< app Navegador: web renderizada por el servicio FBP/1 */
/** JPEG baseline a un lienzo RGB565 cw x ch en (dx,dy) con tamaño dw x dh (vecino más próximo). 0 = bien. */
int ui_jpeg_draw(const uint8_t *jpg, uint32_t n, uint16_t *canvas, int cw, int ch, int dx, int dy, int dw, int dh);
void ui_notif_open(uint32_t id);                    /**< abre el detalle de una notificación */
void ui_notif_watch_tick(bool screen_on);           /**< cada 100 ms (ui_system): aviso al llegar una */
/** Texto del teléfono preparado para la fuente (sin huecos por glifos que no existen). */
void ui_phone_text(const char *in, char *out, size_t n, const lv_font_t *font);
/** "Ahora", "Hace 5 min", "12:41" o "14 oct" a partir de segundos UTC del teléfono. */
void ui_phone_when(uint32_t when_s, char *b, size_t n);
/** Estado corto del enlace ("Conectado", "Buscando"...) y su color. */
const char *ui_phone_chip(const watch_phone_status_t *s, uint32_t *rgb);
void ui_apps_attach(lv_obj_t *face_page); /**< deslizar la esfera a la izquierda abre las apps */
lv_obj_t *ui_stopwatch_create(lv_obj_t *parent);
lv_obj_t *ui_timer_create(lv_obj_t *parent);
lv_obj_t *ui_alarms_create(lv_obj_t *parent);
lv_obj_t *ui_alarm_edit_create(lv_obj_t *parent);
lv_obj_t *ui_sensors_create(lv_obj_t *parent); /* F4.4 */
lv_obj_t *ui_storage_create(lv_obj_t *parent); /* F4.6 */
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
