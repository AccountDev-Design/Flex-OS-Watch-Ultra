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
 *    Sensores visible; o el flujo de muestras abierto): ráfaga de 19 bytes
 *    cada 20 ms. Al dejar de pedirlo, vuelve al reposo.
 *  - Flujo de muestras (Bienestar): UN consumidor recibe TODAS las muestras
 *    nuevas en un anillo de tamaño fijo (reservado una vez), con el contador
 *    del sensor desplegado a 32 bits para que sepa si se perdió alguna o si
 *    leyó la misma dos veces. La tarea del IMU sigue siendo la única que lee
 *    el chip. El consumidor elige el periodo de lectura (20 o 40 ms) cuando
 *    nadie más mide; con Sensores o la brújula abiertas manda su 20 ms.
 *  - Muchos errores de I2C seguidos: vuelve a encender los sensores (como
 *    mucho cada 2 s); la primera muestra después lleva `restart`.
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

/* ---- Flujo de muestras (un solo consumidor) ---- */
typedef struct {
    uint32_t index;   /**< contador de muestras del chip, desplegado a 32 bits */
    uint32_t t_ms;    /**< esp_timer en ms (vuelve a 0 cada 49 días: restar en uint32) */
    int16_t a_mg[3];  /**< mili-g, ejes del chip */
    uint8_t restart;  /**< primera muestra tras encender o recuperar los sensores */
    uint8_t pad;
} watch_imu_sample_t;

typedef struct {
    uint32_t pushed;  /**< muestras puestas en el anillo */
    uint32_t dropped; /**< se sobrescribieron sin leer (anillo lleno) */
    uint32_t recoveries; /**< reencendidos tras errores de I2C */
    uint16_t cap;
    uint16_t period_ms;
} watch_imu_stream_stats_t;

/**
 * Abre el flujo: reserva una vez un anillo de cap muestras (PSRAM si la hay)
 * y enciende los sensores. notify recibe xTaskNotify(notify_bit) cuando hay
 * al menos cap/2 muestras sin leer. ESP_ERR_INVALID_STATE si ya está abierto
 * o el IMU no está disponible.
 */
esp_err_t watch_imu_stream_open(uint16_t cap, void *notify_task, uint32_t notify_bit);
/** Copia hasta max muestras (las más antiguas primero). *dropped: perdidas desde la última lectura. */
size_t watch_imu_stream_read(watch_imu_sample_t *out, size_t max, uint32_t *dropped);
/** Periodo de lectura pedido por el consumidor (20..100 ms). */
void watch_imu_stream_set_period(uint16_t ms);
/** Cierra el flujo (el anillo se conserva para no fragmentar si se reabre). */
void watch_imu_stream_close(void);
void watch_imu_stream_stats(watch_imu_stream_stats_t *out);

#ifdef __cplusplus
}
#endif
