/*
 * watch_imu — IMU QMI8658C (acelerómetro y giroscopio) en el bus I2C
 * compartido (F4.4).
 *
 * Una tarea propia es la única que habla con el chip:
 *  - Al arrancar: sondeo de 0x6B e identidad (WHO_AM_I = 0x05). Sin
 *    respuesta o con otra identidad NO escribe nada y el servicio queda
 *    "no disponible".
 *  - Si es el chip: reinicio por software (0x60 = 0xB0), espera a 0x4D = 0x80,
 *    configuración registro a registro y relectura. Después queda en reposo
 *    (oscilador apagado, ~6 µA).
 *  - Solo mide mientras alguien lo pide (watch_imu_set_active: la app
 *    Sensores visible): ráfaga de 19 bytes cada 20 ms. Al dejar de pedirlo,
 *    vuelve al reposo.
 * La interfaz solo copia instantáneas (watch_imu_get); nunca hace I2C.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "imu_heading.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMU_ABSENT = 0, /**< no responde o no es un QMI8658: no se le escribe nada */
    IMU_ERROR,      /**< respondía pero falló el reinicio o la configuración */
    IMU_IDLE,       /**< configurado y en reposo */
    IMU_STARTING,   /**< encendiendo los sensores (~250 ms) */
    IMU_RUNNING,    /**< midiendo */
} imu_state_t;

typedef struct {
    imu_state_t state;
    uint8_t who_am_i;
    uint8_t revision;
    int32_t acc_mg[3];   /**< mili-g, ejes del chip */
    int32_t gyro_mdps[3];/**< mili-grados por segundo */
    int32_t temp_c100;   /**< centésimas de °C (se actualiza a 8 Hz) */
    uint32_t samples;    /**< muestras leídas */
    uint32_t missed;     /**< muestras que se perdieron entre lecturas */
    uint32_t reads;      /**< ráfagas I2C */
    uint32_t errors;     /**< transacciones I2C fallidas */
    int64_t last_us;     /**< esp_timer de la última muestra */
    imu_heading_t heading; /**< rumbo RELATIVO integrado en cada muestra (sin Norte) */
} watch_imu_snapshot_t;

/** Arranca la tarea del IMU (bus = bsp_i2c_get_handle()). */
esp_err_t watch_imu_start(i2c_master_bus_handle_t bus);
/** Copia de la última instantánea. false si el servicio no arrancó. */
bool watch_imu_get(watch_imu_snapshot_t *out);
/** Cambia con cada muestra nueva. */
uint32_t watch_imu_generation(void);
/** Pide (true) o suelta (false) medidas; cuenta las peticiones. */
void watch_imu_set_active(bool on);
/** Fija la referencia del rumbo relativo (0° = hacia donde apunta ahora). */
void watch_imu_heading_reset(void);

#ifdef __cplusplus
}
#endif
