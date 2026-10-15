/*
 * watch_power — telemetría de batería del AXP2101 en SOLO LECTURA.
 *
 * Una tarea propia lee el PMIC cada WATCH_POWER_PERIOD_MS por el bus I2C
 * compartido del BSP y publica una instantánea. La interfaz solo copia esa
 * instantánea (nunca espera al bus I2C).
 *
 * Garantía: este módulo NO escribe ningún registro del AXP2101. Si el ADC de
 * VBAT o el fuel gauge están desactivados en el chip, el dato se informa como
 * "no disponible" en lugar de activarlos. La configuración del cargador
 * (corrientes, tensión, pin TS) solo se lee y se registra para diagnóstico.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "battery_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_POWER_PERIOD_MS 2000

typedef struct {
    bool driver_ok;          /**< el AXP2101 respondió con su identificador */
    uint32_t samples;        /**< muestras completas leídas */
    uint32_t errors;         /**< lecturas I2C fallidas */
    axp2101_regs_t regs;     /**< última lectura cruda válida */
    axp2101_status_t raw;    /**< última lectura decodificada */
    batt_tracker_t track;    /**< estado confirmado y valores filtrados */
    int64_t last_ok_us;      /**< esp_timer_get_time() de la última lectura válida */
    bool cfg_ok;             /**< configuración del cargador leída al menos una vez */
    axp2101_cfg_regs_t cfg_regs; /**< configuración cruda (solo lectura, cada 60 s) */
    axp2101_cfg_t cfg;       /**< configuración decodificada */
    uint32_t sample_us_last; /**< duración de la última lectura I2C periódica */
    uint32_t sample_us_max;
    uint32_t stack_free_min; /**< margen mínimo de pila de la tarea, en bytes */
} watch_power_snapshot_t;

/** Arranca la tarea de lectura. bus = bsp_i2c_get_handle(). */
esp_err_t watch_power_start(i2c_master_bus_handle_t bus);

/** Copia la última instantánea. Devuelve false si el servicio no arrancó. */
bool watch_power_get(watch_power_snapshot_t *out);

#ifdef __cplusplus
}
#endif
