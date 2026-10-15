/*
 * sim.h — utilidades compartidas de la vista previa de PC (tools/sim).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "flex_tokens.h"
#include "watch_power.h"
#include "watch_time_types.h"

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

/** Avanza el reloj simulado ms milisegundos, ejecutando LVGL cada 5 ms. */
void sim_run(uint32_t ms);
/** Guarda el búfer de pantalla como <dir>/<name>.rgb565 (to_png.py lo convierte). */
void sim_save(const char *dir, const char *name);

/* Dedo simulado (indev de tipo puntero). */
void sim_press(int32_t x, int32_t y);
void sim_release(void);
/** Toque corto: pulsa, espera 60 ms, suelta y deja correr 60 ms. */
void sim_tap(int32_t x, int32_t y);

/** Escenario de batería que devolverá watch_power_get(). */
void sim_scenario(int pct, int mv, batt_state_t st, batt_level_t lv, bool vbus);

/** Hora que devolverá watch_time_now(): avanza con el reloj simulado. */
void sim_set_time(watch_time_state_t st, int y, int mo, int d, int h, int mi, int s);

/** Veces que LVGL ha enviado algo a la pantalla (para comprobar el reposo). */
uint32_t sim_flush_count(void);

/** Busca en el árbol de obj una etiqueta con exactamente ese texto. */
lv_obj_t *sim_find_label(lv_obj_t *obj, const char *text);

/** Número de temporizadores de LVGL vivos. */
int sim_timer_count(void);

/** Memoria de LVGL en uso (asignador propio del simulador). Varía con la
 * fragmentación: para buscar fugas se usa sim_mem_blocks(). */
size_t sim_mem_used(void);
/** Bloques de memoria de LVGL vivos: no depende de la fragmentación. */
uint32_t sim_mem_blocks(void);

/* Baterías de pruebas */
void sim_suite_power(const char *out);
void sim_suite_theme(const char *out);
void sim_suite_face(const char *out);
