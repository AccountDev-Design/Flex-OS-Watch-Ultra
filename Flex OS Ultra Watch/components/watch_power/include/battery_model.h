/*
 * battery_model — lógica PURA de batería (sin ESP-IDF; se prueba en el PC).
 *
 * Registros del AXP2101 según su datasheet (X-Powers, cap. 6.13.2), contrastado
 * con el código oficial (ejemplo 01_AXP2101 de Waveshare + XPowersLib, MIT):
 *
 *   0x00 PMU status1  bit5 VBUS good · bit4 BATFET · bit3 batería presente ·
 *                     bit1 regulación térmica · bit0 límite de corriente de entrada
 *   0x01 PMU status2  [6:5] sentido de la corriente: 00 reposo, 01 carga, 10 descarga
 *                     (bit7 reservado) · bit3 VINDPM (la entrada cae y el PMU
 *                     reduce su consumo; NO indica USB desconectado) ·
 *                     [2:0] etapa del cargador: 0 goteo, 1 precarga, 2 corriente
 *                     constante, 3 tensión constante, 4 terminada, 5 sin carga
 *   0x03 identificador del chip = 0x4A
 *   0x18 bit3 fuel gauge activo · bit2 carga de la pila de botón · bit1 cargador
 *        de la batería habilitado
 *   0x30 canales del ADC: bit0 VBAT · bit1 pin TS
 *   0x34/0x35 VBAT: 14 bits, 1 mV/LSB = ((0x34 & 0x3F) << 8) | 0x35
 *             (leer primero el byte alto)
 *   0xA4 porcentaje del fuel gauge
 *
 * Diferencia con XPowersLib: su isVbusIn() exige además REG01 bit3 == 0, lo
 * que marcaría "sin USB" un USB conectado pero en VINDPM. Aquí la presencia de
 * USB es VBUS good (REG00 bit5) y VINDPM se informa aparte.
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
#define AXP2101_REG_VINDPM     0x15
#define AXP2101_REG_IIN_LIM    0x16
#define AXP2101_REG_GAUGE_CTRL 0x18
#define AXP2101_REG_ADC_CTRL   0x30
#define AXP2101_REG_VBAT_H     0x34
#define AXP2101_REG_VBAT_L     0x35
#define AXP2101_REG_TS_CTRL    0x50
#define AXP2101_REG_IPRECHG    0x61
#define AXP2101_REG_ICC        0x62
#define AXP2101_REG_ITERM      0x63
#define AXP2101_REG_CV         0x64
#define AXP2101_REG_BAT_DET    0x68
#define AXP2101_REG_PERCENT    0xA4

/** Copia cruda de los registros leídos en una muestra periódica. */
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
    bool vbus_present;     /**< VBUS good (REG00 bit5): hay alimentación externa válida */
    bool vindpm;           /**< REG01 bit3: entrada en VINDPM (fuente débil o muy cargada) */
    bool input_limited;    /**< REG00 bit0: en el límite de corriente de entrada */
    bool thermal_reg;      /**< REG00 bit1: regulación térmica */
    axp_dir_t dir;
    axp_chg_stage_t stage;
    bool charger_enabled;  /**< REG18 bit1 */
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

/* ---- Configuración del cargador (solo lectura, diagnóstico) ------------ */

/** Registros de configuración leídos (nunca se escriben). */
typedef struct {
    uint8_t vindpm;     /* 0x15 */
    uint8_t iin_lim;    /* 0x16 */
    uint8_t gauge_ctrl; /* 0x18 */
    uint8_t adc_ctrl;   /* 0x30 */
    uint8_t ts_ctrl;    /* 0x50 */
    uint8_t iprechg;    /* 0x61 */
    uint8_t icc;        /* 0x62 */
    uint8_t iterm;      /* 0x63 */
    uint8_t cv;         /* 0x64 */
    uint8_t bat_det;    /* 0x68 */
} axp2101_cfg_regs_t;

/** Configuración decodificada según las tablas del datasheet. -1 = reservado. */
typedef struct {
    int vindpm_mv;            /**< 3880 + 80*N */
    int input_limit_ma;       /**< 100/500/900/1000/1500/2000 */
    int precharge_ma;         /**< 25*N, N <= 8 */
    int charge_ma;            /**< 25*N si N <= 8; 200 + 100*(N-8) si 8 < N <= 16 */
    int term_ma;              /**< 25*N, N <= 8 */
    bool term_enabled;
    int cv_mv;                /**< 4000/4100/4200/4350/4400 */
    bool charger_enabled;     /**< REG18 bit1 */
    bool button_cell_charge;  /**< REG18 bit2 (pila de respaldo del RTC) */
    bool gauge_enabled;       /**< REG18 bit3 */
    bool vbat_adc_enabled;    /**< REG30 bit0 */
    bool ts_adc_enabled;      /**< REG30 bit1 */
    bool ts_affects_charger;  /**< REG50 bit4 == 0: el pin TS actúa como sensor de temperatura del cargador */
    uint8_t ts_current_mode;  /**< REG50 [3:2]: 0 apagada, 1 con el canal TS, 2 solo midiendo TS, 3 siempre */
    bool batt_detect_enabled; /**< REG68 bit0 */
} axp2101_cfg_t;

void axp2101_decode_cfg(const axp2101_cfg_regs_t *r, axp2101_cfg_t *out);

/* ---- Estado de alimentación --------------------------------------------- */

typedef enum {
    BATT_STATE_UNKNOWN = 0,   /**< sin lectura válida del chip */
    BATT_STATE_NO_BATTERY,    /**< el AXP2101 no detecta batería */
    BATT_STATE_DISCHARGING,   /**< funcionando con batería */
    BATT_STATE_CHARGING,      /**< carga activa: corriente hacia la batería y etapa 0-3 */
    BATT_STATE_FULL,          /**< el cargador indica carga terminada con VBUS presente */
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

/* Umbrales con histéresis: battery_thresholds.h (provisionales hasta calibrar). */
#include "battery_thresholds.h"

/** Siguiente nivel con histéresis: empeorar es inmediato, mejorar exige el umbral de salida. */
batt_level_t batt_level_next(batt_level_t cur, int pct);

/** Muestras iguales seguidas para confirmar un cambio de estado. */
#define BATT_CONFIRM_SAMPLES 2
/** Muestras seguidas para SALIR de "cargando": al final de la tensión
 *  constante la corriente cae hacia cero y el sentido puede oscilar entre
 *  carga y reposo; sin esta histéresis el estado parpadearía. */
#define BATT_CHARGING_EXIT_SAMPLES 3

/** Seguimiento filtrado entre muestras. */
typedef struct {
    /* salidas */
    batt_state_t state;       /**< estado confirmado */
    batt_state_t prev_state;  /**< estado confirmado anterior (antes de la última transición) */
    int percent;              /**< % mostrado (-1 = no disponible) */
    int vbat_mv;              /**< VBAT filtrada (-1 = no disponible) */
    batt_level_t level;
    bool charging_started;    /**< true solo en la muestra que confirma CHARGING */
    uint32_t charge_events;   /**< inicios de carga confirmados (para detectarlos aunque se sondee tarde) */
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
