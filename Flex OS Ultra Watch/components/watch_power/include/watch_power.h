/*
 * watch_power — telemetría de batería del AXP2101 en SOLO LECTURA.
 *
 * Una tarea propia lee el PMIC cada WATCH_POWER_PERIOD_MS por el bus I2C
 * compartido del BSP y publica una instantánea. La interfaz solo copia esa
 * instantánea (nunca espera al bus I2C).
 *
 * Garantía de B1: este módulo NO escribe ningún registro del AXP2101. Si el
 * ADC de VBAT o el fuel gauge están desactivados en el chip, el dato se
 * informa como "no disponible" en lugar de activarlos.
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
} watch_power_snapshot_t;

/** Arranca la tarea de lectura. bus = bsp_i2c_get_handle(). */
esp_err_t watch_power_start(i2c_master_bus_handle_t bus);

/** Copia la última instantánea. Devuelve false si el servicio no arrancó. */
bool watch_power_get(watch_power_snapshot_t *out);

#ifdef __cplusplus
}
#endif
