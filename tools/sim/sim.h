/*
 * sim.h — utilidades compartidas de la vista previa de PC (tools/sim).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "flex_tokens.h"
#include "watch_power.h"
#include "watch_time_types.h"
#include "watch_settings.h"
#include "watch_imu.h"
#include "watch_storage.h"

#define SIM_W FLEX_SCREEN_W
#define SIM_H FLEX_SCREEN_H

extern uint16_t sim_fb[SIM_W * SIM_H];
extern int sim_fails;
extern watch_power_snapshot_t sim_power;

#define EXPECT(cond, ...)                                    \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("FALLO (%s:%d): ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                             \
            printf("\n");                                    \
            sim_fails++;                                     \
        }                                                    \
    } while (0)

/* Salida del splash: 200 ms como mucho, más un cuadro de margen. */
#define SIM_SPLASH_OUT_MAX_MS (FLEX_ANIM_BOOT_FADE_MS / 3 + 40)

/* Flex Phone simulado: qué hace su Browser Relay al pedírselo (sim_phone.c) */
enum { SIM_RELAY_OK = 0, SIM_RELAY_FAIL, SIM_RELAY_MUTE };
void sim_phone_reset(void);
void sim_phone_relay_mode(int mode);
bool sim_phone_relay_up(void);
void sim_phone_relay_user_stop(void);
bool sim_phone_relay_token(char *out, size_t n);

/** Avanza el reloj simulado ms milisegundos, ejecutando LVGL cada 5 ms. */
void sim_run(uint32_t ms);
/** Guarda el búfer de pantalla como <dir>/<name>.rgb565 (to_png.py lo convierte). */
void sim_save(const char *dir, const char *name);

/* Dedo simulado (indev de tipo puntero). */
void sim_press(int32_t x, int32_t y);
void sim_release(void);
/** Toque corto: pulsa, espera 60 ms, suelta y deja correr 60 ms. */
void sim_tap(int32_t x, int32_t y);
/** Arrastre en línea recta en n pasos de step_ms; no suelta el dedo. */
void sim_drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int n, uint32_t step_ms);

/** Escenario de batería que devolverá watch_power_get(). */
void sim_scenario(int pct, int mv, batt_state_t st, batt_level_t lv, bool vbus);

/** Hora que devolverá watch_time_now(): avanza con el reloj simulado. */
void sim_set_time(watch_time_state_t st, int y, int mo, int d, int h, int mi, int s);

/** Veces que LVGL ha enviado algo a la pantalla (para comprobar el reposo). */
uint32_t sim_flush_count(void);

/** Busca en el árbol de obj una etiqueta con exactamente ese texto. */
lv_obj_t *sim_find_label(lv_obj_t *obj, const char *text);
/** La etiqueta número nth (0 = la primera) con ese texto. */
lv_obj_t *sim_find_label_n(lv_obj_t *obj, const char *text, int nth);

/** Caracteres de las etiquetas de obj (y sus hijos) que su fuente no puede dibujar. */
int sim_missing_glyphs(lv_obj_t *obj, const char *where);
/** EXPECT de que no falte ningún glifo. */
void sim_check_glyphs(lv_obj_t *obj, const char *where);

/** Número de temporizadores de LVGL vivos. */
int sim_timer_count(void);

/* Brillo simulado: valor aplicado y comandos enviados al "panel". */
/* Puesta en hora simulada (sim_main.c) */
extern int sim_time_sets;
extern int64_t sim_time_set_utc;
extern wt_set_result_t sim_time_set_result;

/* Verificador visual (sim_lint.c) */
int sim_lint(const char *where);
int sim_check_residue(const char *where);

/* Almacenamiento simulado (sim_main.c) */
extern watch_storage_info_t sim_stg;
extern bool sim_stg_fail;

/* IMU simulado (sim_main.c) */
extern watch_imu_snapshot_t sim_imu;
extern int sim_imu_users;

/* Ajustes persistentes en RAM (sim_ws.c) */
extern int sim_ws_updates;
void sim_ws_reset(const watch_settings_t *s); /* NULL = valores por defecto */

extern bool sim_display_on;
extern int sim_display_level;
extern int sim_button;
bool sim_display_power_cb(bool on);
bool sim_display_level_cb(int percent);
int sim_button_level(void);

extern int sim_brightness;
extern int sim_brightness_calls;
int sim_brightness_get(void);
bool sim_brightness_set(int percent);

/** Memoria de LVGL en uso (asignador propio del simulador). Varía con la
 * fragmentación: para buscar fugas se usa sim_mem_blocks(). */
size_t sim_mem_used(void);
/** Bloques de memoria de LVGL vivos: no depende de la fragmentación. */
uint32_t sim_mem_blocks(void);

/* Baterías de pruebas */
void sim_suite_relay(const char *out);
void sim_suite_boot(const char *out);
void sim_suite_power(const char *out);
void sim_suite_nav(const char *out);
void sim_suite_qp(const char *out);
void sim_suite_settings(const char *out);
void sim_suite_monkey(const char *out);
void sim_suite_bench(const char *out);
void sim_suite_visual(const char *out);
void sim_suite_theme(const char *out);
void sim_suite_face(const char *out);
void sim_suite_faces(const char *out);
void sim_suite_datetime(const char *out);
void sim_suite_apps(const char *out);
void sim_suite_wifi(const char *out);
void sim_suite_phone(const char *out);
void sim_suite_notes(const char *out);
void sim_suite_weather(const char *out);
void sim_suite_browser(const char *out);
void sim_suite_ota(const char *out);
/* OTA simulado (sim_ota.c): motor y firma reales, servidor y flash falsos */
void sim_ota_reset(void);
void sim_ota_publish(const char *ver, bool good_sig, const char *changelog);
void sim_ota_online(bool on);
void sim_ota_key(bool on);
void sim_ota_batt(int pct, bool charging);
void sim_ota_boot(int notice);
bool sim_ota_busy(void);
int sim_ota_runs(void);
bool sim_ota_installed(const char *ver);
bool sim_ota_aborted(void);
/* Clima simulado (sim_wx.c): motor y parser reales, red falsa */
void sim_wx_reset(void);
void sim_wx_online(bool on);
void sim_wx_fail(int err);
int sim_wx_fetches(void);
int sim_wx_searches(void);
bool sim_wx_busy(void);
void sim_wx_reboot(void);
void sim_suite_screen(const char *out);
