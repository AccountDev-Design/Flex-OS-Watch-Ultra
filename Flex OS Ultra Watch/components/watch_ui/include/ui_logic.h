/*
 * ui_logic — decisiones PURAS de la interfaz (sin LVGL ni ESP-IDF; se prueban
 * en el PC). La interfaz solo pinta lo que estas funciones deciden.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "battery_model.h"
#include "flex_batt_style.h"
#include "watch_time_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Tipo de vista del indicador para un estado confirmado del servicio de batería. */
flex_batt_kind_t ui_batt_kind(batt_state_t state, batt_level_t level);

/** Texto de estado en español. */
const char *ui_batt_state_text(batt_state_t state);

/**
 * Compuerta de la animación de carga. Se dispara una sola vez por cada inicio
 * de carga CONFIRMADO por el servicio (charge_events), y solo si se venía de
 * un estado conocido: al arrancar (UNKNOWN -> CHARGING) o al recuperarse de un
 * error de lectura no se anima. Los eventos anteriores a la creación de la
 * interfaz tampoco se animan.
 */
typedef struct {
    uint32_t seen_events;
    bool primed;
} ui_charge_gate_t;

void ui_charge_gate_init(ui_charge_gate_t *g);
bool ui_charge_gate_update(ui_charge_gate_t *g, uint32_t charge_events, batt_state_t prev_state,
                           batt_state_t state);

/**
 * Textos de la esfera. Con hora válida: "9:05" y "Jueves, 15 de octubre".
 * Sin ella, "--:--" y el motivo (nunca una hora inventada). key cambia solo
 * cuando cambia lo que hay que pintar (minuto, día o estado), para no
 * reescribir las etiquetas cada segundo.
 */
typedef struct {
    bool valid;
    int64_t key;
    char time[8];
    char date[40];
    char ampm[3];  /**< "AM"/"PM" en 12 h; "" en 24 h o sin hora */
} ui_face_time_t;

/** Textos de la esfera en 24 h. */
void ui_face_time(watch_time_state_t state, const wt_datetime_t *dt, ui_face_time_t *out);
/** Igual, en 24 h o en 12 h (la clave cambia también con el formato). */
void ui_face_time_fmt(watch_time_state_t state, const wt_datetime_t *dt, bool h24, ui_face_time_t *out);

/**
 * Brillo del panel rápido. Mínimo 10 %: por debajo, a plena luz la pantalla
 * parecería apagada y no habría forma visible de recuperarla. Mientras se
 * arrastra el control, el comando al panel (DCS 0x51) se envía como mucho
 * cada UI_BRIGHT_APPLY_MS; al soltar se envía siempre el valor final.
 */
#define UI_BRIGHT_MIN      10
#define UI_BRIGHT_MAX      100
#define UI_BRIGHT_APPLY_MS 60

/* ---- Formatos de las páginas de información (español: coma decimal) ----
 * Un valor negativo significa "no disponible" y se escribe "--": nunca se
 * inventa un valor. Todas devuelven lo mismo que snprintf. */

/** 4085 -> "4,08 V" (se trunca a centésimas). */
int ui_fmt_volts(char *buf, size_t n, int mv);
/** 200 -> "200 mA" */
int ui_fmt_ma(char *buf, size_t n, int ma);
/** 184320 -> "180 KB"; 7340032 -> "7,0 MB" (a partir de 1 MB, con un decimal). */
int ui_fmt_bytes(char *buf, size_t n, uint32_t bytes);
/** Estado del servicio de hora para Diagnóstico. */
const char *ui_time_state_text(watch_time_state_t st);

int ui_brightness_clamp(int percent);
bool ui_brightness_should_apply(int value, int applied, uint32_t last_ms, uint32_t now_ms, bool final);

/* ---- Almacenamiento (rediseño 2026-10) ---- */
/** "16/32 MB  (50%)" con coma decimal por debajo de 10 MB ("4,5/8 MB  (56%)").
 * total 0: "--" (no se divide por cero). */
int ui_fmt_usage(char *buf, size_t n, uint64_t used, uint64_t total);
/** Uso en milésimas (0..1000), sin dividir por cero ni pasarse de 1000. */
int32_t ui_permille(uint64_t used, uint64_t total);

/** Estado de la memoria de la tarjeta "Detalles de memoria y sistema". */
typedef enum { UI_MEM_OK = 0, UI_MEM_TIGHT, UI_MEM_LOW } ui_mem_health_t;
#define UI_MEM_INTERNAL_OK    (48u * 1024u)  /* lo que pide el Wi-Fi para encenderse (watch_net) */
#define UI_MEM_INTERNAL_LOW   (24u * 1024u)
#define UI_MEM_PSRAM_BLOCK_OK (1024u * 1024u) /* cabe un fondo (402 KB) y una captura (411 KB) */
#define UI_MEM_PSRAM_BLOCK_LOW (256u * 1024u)
/** A partir de la RAM interna libre, su bloque mayor, el bloque mayor de PSRAM
 * y las reservas fallidas desde el arranque. */
ui_mem_health_t ui_mem_health(uint32_t internal_free, uint32_t internal_largest, uint32_t psram_largest,
                              uint32_t alloc_fails);
const char *ui_mem_health_text(ui_mem_health_t h);

/** Mayúsculas de un texto UTF-8 en español (a-z, áéíóúñü). */
void ui_upper_es(const char *in, char *out, size_t n);

/* ---- Zonas horarias (F4.2): desfases fijos con ciudades de ejemplo ---- */
typedef struct {
    int16_t off_min;   /**< minutos respecto a UTC */
    const char *where; /**< ciudades de ejemplo (corto: cabe en una fila) */
} ui_tz_entry_t;

extern const ui_tz_entry_t ui_tz_table[];
extern const size_t ui_tz_count;
/** Índice de la zona con ese desfase, o -1. */
int ui_tz_index(int off_min);

#ifdef __cplusplus
}
#endif
