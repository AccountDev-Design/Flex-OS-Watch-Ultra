/*
 * battery_model — lógica PURA de batería (sin ESP-IDF; se prueba en el PC).
 *
 * Interpreta los registros del AXP2101 tal y como lo hace el código oficial
 * (ejemplo 01_AXP2101 de Waveshare + XPowersLib, MIT, Lewis He):
 *
 *   0x00 STATUS1 bit5  VBUS correcto              (isVbusGood)
 *                bit3  batería presente           (isBatteryConnect)
 *   0x01 STATUS2 [7:5] sentido de la corriente: 0 reposo, 1 carga, 2 descarga
 *                bit3  0 = VBUS conectado (junto con VBUS correcto, isVbusIn)
 *                [2:0] etapa del cargador: 0 goteo, 1 precarga, 2 corriente
 *                      constante, 3 tensión constante, 4 terminada, 5 parada
 *   0x03 identificador del chip = 0x4A
 *   0x18 bit3  medidor de carga (fuel gauge) activo (enableGauge)
 *   0x30 bit0  medida de VBAT en el ADC activa       (enableBattVoltageMeasure)
 *        bit1  medida del pin TS activa              (el oficial la desactiva)
 *   0x34/0x35  VBAT en mV: ((0x34 & 0x1F) << 8) | 0x35  (getBattVoltage)
 *   0xA4       porcentaje del fuel gauge              (getBatteryPercent)
 *
 * Nada aquí escribe en el chip.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_I2C_ADDR       0x34
#define AXP2101_CHIP_ID_VALUE  0x4A

#define AXP2101_REG_STATUS1    0x00
#define AXP2101_REG_STATUS2    0x01
#define AXP2101_REG_CHIP_ID    0x03
#define AXP2101_REG_GAUGE_CTRL 0x18
#define AXP2101_REG_ADC_CTRL   0x30
#define AXP2101_REG_VBAT_H     0x34
#define AXP2101_REG_VBAT_L     0x35
#define AXP2101_REG_PERCENT    0xA4

/** Copia cruda de los registros leídos en una muestra. */
typedef struct {
    uint8_t status1;
    uint8_t status2;
    uint8_t chip_id;
    uint8_t gauge_ctrl;
    uint8_t adc_ctrl;
    uint8_t vbat_h;
    uint8_t vbat_l;
    uint8_t percent;
} axp2101_regs_t;

typedef enum {
    AXP_DIR_STANDBY = 0,
    AXP_DIR_CHARGE = 1,
    AXP_DIR_DISCHARGE = 2,
    AXP_DIR_OTHER = 3,
} axp_dir_t;

typedef enum {
    AXP_CHG_TRICKLE = 0,
    AXP_CHG_PRECHARGE = 1,
    AXP_CHG_CC = 2,
    AXP_CHG_CV = 3,
    AXP_CHG_DONE = 4,
    AXP_CHG_STOPPED = 5,
    AXP_CHG_UNKNOWN = 6,
} axp_chg_stage_t;

/** Registros decodificados. -1 = dato no disponible. */
typedef struct {
    bool chip_ok;
    bool batt_present;
    bool vbus_good;
    bool vbus_in;
    axp_dir_t dir;
    axp_chg_stage_t stage;
    bool vbat_adc_enabled;
    bool ts_adc_enabled;
    bool gauge_enabled;
    int vbat_mv;  /**< -1 si no hay batería, ADC apagado o valor fuera de rango */
    int percent;  /**< -1 si no hay batería, gauge apagado o valor > 100 */
} axp2101_status_t;

/** Límites de plausibilidad de VBAT para una LiPo de una celda. */
#define BATT_VBAT_MIN_MV 2500
#define BATT_VBAT_MAX_MV 4500

void axp2101_decode(const axp2101_regs_t *r, axp2101_status_t *out);

/** Estado de alimentación. */
typedef enum {
    BATT_STATE_UNKNOWN = 0,   /**< sin lectura válida del chip */
    BATT_STATE_NO_BATTERY,    /**< el AXP2101 no detecta batería */
    BATT_STATE_DISCHARGING,   /**< funcionando con batería */
    BATT_STATE_CHARGING,      /**< carga activa confirmada por el cargador */
    BATT_STATE_FULL,          /**< el cargador indica carga terminada (VBUS presente) */
    BATT_STATE_EXTERNAL_IDLE, /**< VBUS presente sin carga activa ni terminada */
} batt_state_t;

/** Clasificación instantánea (sin filtrar) de una muestra decodificada. */
batt_state_t batt_classify(const axp2101_status_t *s);

typedef enum {
    BATT_LEVEL_UNKNOWN = 0,
    BATT_LEVEL_NORMAL,
    BATT_LEVEL_LOW,
    BATT_LEVEL_CRITICAL,
} batt_level_t;

/* Umbrales PROVISIONALES con histéresis (entrar / salir), en %. Se ajustarán
 * tras medir la batería real del reloj. */
#define BATT_LOW_ENTER_PCT      15
#define BATT_LOW_EXIT_PCT       18
#define BATT_CRITICAL_ENTER_PCT 5
#define BATT_CRITICAL_EXIT_PCT  8

/** Siguiente nivel con histéresis: empeorar es inmediato, mejorar exige el umbral de salida. */
batt_level_t batt_level_next(batt_level_t cur, int pct);

/** Muestras iguales seguidas necesarias para confirmar un cambio de estado. */
#define BATT_CONFIRM_SAMPLES 2

/** Seguimiento filtrado entre muestras. */
typedef struct {
    /* salidas */
    batt_state_t state;       /**< estado confirmado */
    int percent;              /**< % mostrado (-1 = no disponible) */
    int vbat_mv;              /**< VBAT filtrada (-1 = no disponible) */
    batt_level_t level;
    bool charging_started;    /**< true solo en la muestra que confirma CHARGING */
    uint32_t invalid_samples; /**< lecturas descartadas por implausibles */

    /* estado interno */
    int pct_hist[3];
    uint8_t pct_n;
    int vbat_ema_x4;          /**< EMA de VBAT en cuartos de mV */
    batt_state_t pending;
    uint8_t pending_count;
} batt_tracker_t;

void batt_tracker_init(batt_tracker_t *t);

/**
 * Incorpora una muestra.
 * @param read_ok false si la lectura I2C falló (no se modifica el % ni VBAT).
 */
void batt_tracker_update(batt_tracker_t *t, const axp2101_status_t *s, bool read_ok);

const char *batt_state_name(batt_state_t st);

#ifdef __cplusplus
}
#endif
