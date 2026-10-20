/*
 * imu_heading — rumbo RELATIVO a partir del giroscopio y el acelerómetro
 * (lógica pura: sin ESP-IDF; se prueba en el PC).
 *
 * El QMI8658 no tiene magnetómetro: no hay Norte. Lo que sí se puede medir
 * es cuánto ha girado el reloj alrededor de la vertical desde una referencia
 * que fija el usuario:
 *  - giro alrededor de la vertical = proyección del giroscopio sobre la
 *    gravedad (vale con el reloj inclinado, no solo plano);
 *  - sesgo del giroscopio: se estima mientras el reloj está quieto (giro casi
 *    nulo y aceleración de 1 g) y, quieto, no se integra nada (actualización
 *    de velocidad cero): la deriva solo crece al moverse;
 *  - incertidumbre: crece con el tiempo en movimiento (más sin sesgo
 *    estimado) y con lo girado (error de escala); vuelve a 0 al fijar la
 *    referencia. La interfaz la enseña siempre junto al rumbo.
 * Sentido: positivo en el sentido de las agujas del reloj visto desde arriba
 * (como un rumbo), con el eje Z del chip hacia fuera de la pantalla.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMU_HDG_STILL_DPS    2.0f   /* |giro| por debajo: quieto */
#define IMU_HDG_STILL_MG     60     /* |a| a menos de esto de 1 g: quieto */
#define IMU_HDG_CAL_MS       1000u  /* quieto este tiempo: sesgo estimado */
#define IMU_HDG_GRAV_MIN_MG  700    /* sin gravedad clara (caída, golpe): no se integra */
#define IMU_HDG_GRAV_MAX_MG  1300
#define IMU_HDG_MAX_DT_MS    100u   /* un hueco mayor no se integra (muestras perdidas) */

typedef struct {
    float yaw;        /**< grados, 0..360 desde la referencia */
    float bias;       /**< sesgo estimado del giro vertical, °/s */
    float drift;      /**< incertidumbre estimada, ± grados */
    uint32_t still_ms;
    bool bias_ok;     /**< el sesgo se estimó con el reloj quieto */
    bool valid;       /**< hay gravedad para saber dónde está la vertical */
} imu_heading_t;

void imu_heading_init(imu_heading_t *h);
/** Nueva referencia (0°); conserva el sesgo estimado. */
void imu_heading_reset(imu_heading_t *h);
/** Una muestra (mili-g, mili-°/s) tras dt_ms desde la anterior. */
void imu_heading_update(imu_heading_t *h, const int32_t acc_mg[3], const int32_t gyro_mdps[3], uint32_t dt_ms);

#ifdef __cplusplus
}
#endif
