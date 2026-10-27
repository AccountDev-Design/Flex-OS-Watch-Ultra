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
/** Aviso informativo del sistema (icono de información, título en ámbar). */
void ui_banner_show_info(const char *title, const char *text);
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
/** Vuelve hasta la página más cercana creada con create (una sola animación;
 * las intermedias se cierran). false si no hay ninguna debajo. */
bool ui_nav_back_to(ui_page_create_t create);
/** Vuelve a la raíz (la esfera) con una sola animación; cierra las páginas. */
void ui_nav_home(void);
/** El cajón de aplicaciones baja desde arriba (gesto hacia abajo en la esfera). */
bool ui_nav_push_from_top(ui_page_create_t create);
/** Sin animación de navegación: la página anima su propia entrada o salida
 * (selector de esferas). */
bool ui_nav_push_instant(ui_page_create_t create);
bool ui_nav_back_instant(void);
void ui_nav_home_instant(void);
/** Abre una página que sube siguiendo al dedo (cajón desde el borde inferior
 * de la esfera): begin al reconocerse el gesto, move con el recorrido, end al
 * soltar (commit: se completa; si no, baja y se borra). */
bool ui_nav_drag_open_begin(ui_page_create_t create);
/** Igual, eligiendo el borde: from_top = la página baja desde arriba. */
bool ui_nav_drag_open_begin_dir(ui_page_create_t create, bool from_top);
/** Igual desde el borde derecho de la esfera (tarjetas de Bienestar). */
bool ui_nav_drag_open_begin_side(ui_page_create_t create);
void ui_nav_drag_open_move(int32_t travel);
void ui_nav_drag_open_end(bool commit, int32_t travel);
/** A la esfera SIN cerrar la app: sus páginas quedan suspendidas (ocultas,
 * con su estado) hasta que se vuelve a abrir esa app (ui_nav_open_app). */
bool ui_nav_suspend(bool animated);
/** Retoma la app suspendida si es app (su página raíz). false si no lo es. */
bool ui_nav_resume(ui_page_create_t app);
/** Abre una app desde el cajón: la retoma si estaba suspendida; si no, cierra
 * la suspendida (si había otra) y la abre nueva. */
bool ui_nav_open_app(ui_page_create_t app);
/** Página raíz de la app suspendida, o NULL. */
ui_page_create_t ui_nav_suspended_app(void);
/** Páginas suspendidas (hijas ocultas de la pantalla de navegación). */
int ui_nav_suspended_count(void);
/** Cierra la app suspendida (libera sus páginas). */
void ui_nav_discard_suspended(void);
/** Códigos de evento que reciben las páginas de una app al suspenderse y al
 * retomarse (p. ej. para soltar o volver a pedir el IMU). */
uint32_t ui_nav_event_suspend(void);
uint32_t ui_nav_event_resume(void);
/** Marca una página con cambios sin guardar: atrás y salir al cajón avisan
 * y hay que repetir el gesto para descartarlos. */
void ui_nav_set_dirty(lv_obj_t *page, bool dirty);
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
/* App Reloj (rediseño 2026-10): pestañas Hora, Cronómetro, Alarmas y
 * Temporizador. Cada vista es un contenedor bajo la barra (y >= UI_RELOJ_TOP)
 * que se crea al elegir su pestaña y se borra al dejarla. */
enum { UI_RELOJ_HORA = 0, UI_RELOJ_CRONO, UI_RELOJ_ALARMAS, UI_RELOJ_TEMP, UI_RELOJ_TABS };
#define UI_RELOJ_TOP 96
lv_obj_t *ui_reloj_create(lv_obj_t *parent);           /**< en la última pestaña usada */
lv_obj_t *ui_reloj_page(lv_obj_t *parent, int tab);
int ui_reloj_tab(lv_obj_t *page);                      /**< pestaña activa (-1 si no es Reloj) */
void ui_reloj_select(lv_obj_t *page, int tab);
lv_obj_t *ui_reloj_view(lv_obj_t *page);               /**< contenedor vacío de una vista */
lv_obj_t *ui_reloj_time_view(lv_obj_t *page);
lv_obj_t *ui_stopwatch_view(lv_obj_t *page);
lv_obj_t *ui_alarms_view(lv_obj_t *page);
lv_obj_t *ui_timer_view(lv_obj_t *page);
lv_obj_t *ui_calc_create(lv_obj_t *parent);     /**< app Calculadora (F9.2) */
lv_obj_t *ui_calendar_create(lv_obj_t *parent); /**< app Calendario (F9.3) */
lv_obj_t *ui_compass_create(lv_obj_t *parent);  /**< app Flex Compass (F9.4) */
/** Textos del anillo naranja de la brújula abierta (0..3: inclinación, alabeo,
 * deriva, "SIN NORTE"; se dibujan, no son etiquetas). Para las pruebas. */
const char *ui_compass_ring_text(int i);
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
/** Pinta el dibujo .fxp encajado en un lienzo cw x ch (miniaturas de la Galería). false si no es válido. */
bool ui_paint_thumb(lv_obj_t *canvas, int cw, int ch, const char *path);
/* Galería (ui_gallery.c, motor en watch_gallery) */
lv_obj_t *ui_gallery_create(lv_obj_t *parent);     /**< app Galería: reloj, Flex OS Ultra, Flex Cloud y Flex Phone */
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
lv_obj_t *ui_phone_relay_create(lv_obj_t *parent);  /**< Flex Phone > Navegador del teléfono (Browser Relay) */
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
/* Navegador (F6; ui_browser.c; JPEG con flex_jpeg de watch_theme) */
lv_obj_t *ui_browser_create(lv_obj_t *parent);      /**< app Navegador: web renderizada por el servicio FBP/1 */
void ui_notif_open(uint32_t id);                    /**< abre el detalle de una notificación */
void ui_notif_watch_tick(bool screen_on);           /**< cada 100 ms (ui_system): aviso al llegar una */
/** Texto del teléfono preparado para la fuente (sin huecos por glifos que no existen). */
void ui_phone_text(const char *in, char *out, size_t n, const lv_font_t *font);
/** "Ahora", "Hace 5 min", "12:41" o "14 oct" a partir de segundos UTC del teléfono. */
void ui_phone_when(uint32_t when_s, char *b, size_t n);
/** Estado corto del enlace ("Conectado", "Buscando"...) y su color. */
const char *ui_phone_chip(const watch_phone_status_t *s, uint32_t *rgb);
/** Gestos de la esfera que abren el cajón: hacia arriba desde el borde
 * inferior y hacia abajo desde la zona central (los dos siguen al dedo). */
void ui_apps_attach(lv_obj_t *face_page);
/** Icono de la app con ese nombre en el cajón (o NULL): accesibilidad y
 * pruebas. Si no se ve entero, centra la rejilla en su copia más cercana. */
lv_obj_t *ui_apps_find(lv_obj_t *drawer, const char *name);
/** Punto de estado de Reloj (cronómetro o temporizador en marcha). */
lv_obj_t *ui_apps_badge(lv_obj_t *drawer);
/** Apps (flex_app_t) con icono visible ahora mismo; devuelve cuántas. */
int ui_apps_visible(lv_obj_t *drawer, int *apps, int max);
/** La rejilla se está moviendo (arrastre, inercia o muelles). */
bool ui_apps_moving(lv_obj_t *drawer);
/** Transición vertical del cajón (ui_nav): offset es donde estaría la página
 * (0 = en su sitio, +-FLEX_SCREEN_H = fuera). La página no se mueve: la
 * rejilla sigue al dedo y el fondo de la página es un velo negro con la
 * opacidad del recorrido sobre la esfera quieta. */
void ui_apps_transition(lv_obj_t *drawer, int32_t offset);
/** La próxima apertura empieza con Reloj en el centro (pruebas). */
void ui_apps_reset_view(void);
/** La esfera supo que el dedo se movió (un gesto): no ofrece la pulsación larga. */
void ui_face_moved(void);
/** Reloj abierto en la pestaña de cronómetro, temporizador o alarmas. */
lv_obj_t *ui_stopwatch_create(lv_obj_t *parent);
lv_obj_t *ui_timer_create(lv_obj_t *parent);
lv_obj_t *ui_alarms_create(lv_obj_t *parent);
lv_obj_t *ui_alarm_edit_create(lv_obj_t *parent);
lv_obj_t *ui_sensors_create(lv_obj_t *parent); /* F4.4 */
/* Bienestar (ui_wb_*.c; servicio en watch_activity) */
lv_obj_t *ui_wb_create(lv_obj_t *parent);       /**< app Bienestar (cajón) */
lv_obj_t *ui_wb_cards_create(lv_obj_t *parent); /**< tarjetas: gesto desde el borde derecho de la esfera */
/** Cada 100 ms (ui_system): avisos de pausa, reanudación y fin automáticos. */
void ui_wb_watch_tick(bool screen_on);
/** Hay un entrenamiento en marcha: el gesto de las tarjetas es más estricto. */
bool ui_wb_gesture_strict(void);
lv_obj_t *ui_storage_create(lv_obj_t *parent);         /**< Almacenamiento (diseño del usuario) */
lv_obj_t *ui_storage_details_create(lv_obj_t *parent); /**< sus detalles: LittleFS, pruebas y formateo (F4.6) */
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
/** El mismo control con el estilo claro del panel rápido rediseñado. */
lv_obj_t *ui_bright_create_light(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/** Temporizador ligado a una página: se borra con ella. */
lv_timer_t *ui_page_timer(lv_obj_t *page, lv_timer_cb_t cb, uint32_t period_ms, void *user_data);

/* ---- Flex Ultra Power Saving (ui_psave.c) ---- */
#define UI_PSAVE_BRIGHT       30      /* % de brillo máximo en ahorro */
#define UI_PSAVE_SCREEN_TO_MS 10000u  /* apagado de pantalla máximo en ahorro */
/** Aplica al arrancar el modo guardado (si quedó activo). */
void ui_psave_init(void);
bool ui_psave_active(void);
/** Activa o desactiva el ahorro: guarda y restaura el estado real anterior. */
void ui_psave_set(bool on);
/** Texto de la política de radios ("Conservar", "Apagar Wi-Fi"...). */
const char *ui_psave_radio_text(uint8_t policy);

/* ---- Flex Liquid Glass Engine: política de perfiles (ui_glass.c) ---- */
#include "flex_glass.h"
/** Mide los cuadros y aplica el perfil del ajuste guardado (una vez, al arrancar). */
void ui_glass_init(void);
/** Vuelve a calcular y aplica el perfil (al cambiar el ajuste o el ahorro). */
void ui_glass_apply(void);
/** Perfil que toca ahora: el del ajuste, Ligero con el ahorro, o el techo medido en automático. */
flex_glass_profile_t ui_glass_effective(void);
/** "Auto (Premium)", "Estándar"... */
const char *ui_glass_setting_text(uint8_t setting);
/** Medidor del perfil automático (diagnóstico y pruebas). */
const flex_glass_auto_t *ui_glass_meter(void);

/* ---- AOD y protección AMOLED (ui_aod.c) ---- */
/** AOD efectivo: activado en Ajustes y sin ahorro de energía. */
bool ui_aod_enabled(void);
void ui_aod_show(void);
void ui_aod_hide(void);
bool ui_aod_active(void);
/** Desplazamiento de Pixel Shift de ±max px para el paso step (quien lo usa
 * avanza el paso con cada cambio de minuto, que ya redibuja); (0,0) si Pixel
 * Shift está desactivado. */
void ui_pixel_shift(int max, uint32_t step, int32_t *dx, int32_t *dy);
/** Texto de un modo de atenuación (WS_DIM_*). */
const char *ui_dim_text(uint8_t dim);

/* ---- Esferas (ui_face.c, ui_face_editor.c) ---- */
typedef enum {
    UI_FACE_DIGITAL = 0,
    UI_FACE_ANALOG,
    UI_FACE_MODULAR,
    UI_FACE_GLASS,
    UI_FACE_PARALLAX,
    /* Diseños del usuario (fondos propios: watch_theme_apply_image) */
    UI_FACE_MONTANA,
    UI_FACE_ULTRA,      /**< analógica azul con complicación de batería */
    UI_FACE_LUNA,       /**< gato y luna */
    UI_FACE_TIERRA,     /**< con el clima de la caché */
    UI_FACE_FLORES,     /**< horas y minutos apilados y batería */
    UI_FACE_TAJ,
    UI_FACE_COUNT_
} ui_face_style_t;
/** ¿La esfera usa su propio fondo (no el fondo de pantalla elegido)? */
bool ui_face_has_own_background(int style);
/** Nombre de una esfera. */
const char *ui_face_name(int style);
/** Esfera de un estilo concreto (sin gestos: para las vistas previas). */
lv_obj_t *ui_face_create_style(lv_obj_t *parent, int style, bool interactive);
/** Esfera analógica (marcas y agujas con líneas de LVGL) de d px; aod: gris
 * atenuado y sin segundero. */
lv_obj_t *ui_analog_create(lv_obj_t *parent, int32_t d, bool aod);
/** hour < 0: sin hora (agujas ocultas); second < 0: sin segundero. */
void ui_analog_set(lv_obj_t *dial, int hour, int minute, int second);
/** Selector de esferas (mantener pulsada la esfera o Ajustes > Pantalla). */
lv_obj_t *ui_face_editor_create(lv_obj_t *parent);
/** Abre el selector deslizando; from_settings: al aplicar vuelve a la esfera. */
void ui_face_editor_open(bool from_settings);
/** Desde la esfera (pulsación larga): en el siguiente tick, fuera del evento
 * táctil, captura la esfera y abre el selector con un zoom hasta su tarjeta. */
void ui_face_editor_open_zoom(void);
/** Vistas previas renderizadas desde el arranque (diagnóstico y pruebas). */
uint32_t ui_face_editor_previews_made(void);
/** Esfera centrada en el selector a la vista y quieto (-1 si no lo está). */
int ui_face_editor_centered(void);
/** Miniaturas vivas en el selector abierto (ventana deslizante). */
int ui_face_editor_thumbs(void);

/* ---- Panel rápido (ui_qp) ---- */
/** Arrastre hacia abajo que abre el panel (franja superior de cualquier página). */
void ui_qp_pull_cb(lv_event_t *e);

/** Añade a la esfera la franja superior que abre el panel al arrastrar. */
void ui_qp_attach(lv_obj_t *face_page);
void ui_qp_open(void);
void ui_qp_close(void);
bool ui_qp_is_open(void);
/** El panel existe (abierto, abriéndose, arrastrándose o cerrándose). */
bool ui_qp_active(void);
lv_obj_t *ui_qp_obj(void);
/** Mosaicos del panel rápido abierto (o NULL): para las pruebas y el diagnóstico. */
typedef enum { UI_QP_WIFI = 0, UI_QP_BT, UI_QP_SETTINGS, UI_QP_AOD, UI_QP_PSAVE, UI_QP_CLOCK, UI_QP_TILES } ui_qp_tile_t;
lv_obj_t *ui_qp_tile(ui_qp_tile_t t);
/** Batería de la cabecera del panel (% e icono; tocarla abre Ajustes > Batería). */
lv_obj_t *ui_qp_battery(void);

/** Splash de arranque activo (watch_ui_boot_splash). */
bool ui_splash_active(void);
