/*
 * qmi8658 — lógica PURA del IMU QMI8658C de la placa (sin ESP-IDF; se prueba
 * en el PC). Hoja de datos QMI8658A Rev A (la placa lleva la variante C; los
 * registros básicos coinciden según los dos controladores de Waveshare).
 *
 * Configuración: acelerómetro ±4 g y giroscopio ±512 °/s, ambos con el código
 * de frecuencia 7 (56,05 Hz con los dos activos), autoincremento de
 * dirección, interrupciones sin usar (INT2, el de "dato listo", solo llega a
 * un punto de prueba). Lectura: una ráfaga de 19 bytes desde 0x2E.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QMI_ADDR          0x6B  /* SA0 a GND */
#define QMI_REG_WHO_AM_I  0x00
#define QMI_REG_REVISION  0x01
#define QMI_REG_CTRL1     0x02
#define QMI_REG_CTRL2     0x03
#define QMI_REG_CTRL3     0x04
#define QMI_REG_CTRL5     0x06
#define QMI_REG_CTRL7     0x08
#define QMI_REG_CTRL8     0x09
#define QMI_REG_STATUS0   0x2E
#define QMI_REG_RESET_OK  0x4D  /* 0x80 tras el reinicio (se sobrescribe al activar) */
#define QMI_REG_RESET     0x60

#define QMI_WHO_AM_I      0x05
#define QMI_RESET_CMD     0xB0
#define QMI_RESET_DONE    0x80
#define QMI_CTRL1_RUN     0x60  /* autoincremento + BE por defecto, oscilador encendido */
#define QMI_CTRL1_SLEEP   0x61  /* ...y bit 0: oscilador apagado (~6 µA, conserva la configuración) */
#define QMI_CTRL2_VAL     0x17  /* ±4 g, código 7 */
#define QMI_CTRL3_VAL     0x57  /* ±512 °/s, código 7 */
#define QMI_CTRL7_ON      0x03  /* aEN | gEN */
#define QMI_BURST_LEN     19    /* STATUS0, STATUS1, TS(3), TEMP(2), A(6), G(6) */
#define QMI_ODR_HZ_X100   5605  /* 56,05 Hz con los dos sensores */
#define QMI_SETTLE_MS     250   /* giroscopio: 150 ms + 3/ODR */

/** Escrituras de configuración, en orden (un registro por escritura: la
 * hoja de datos no admite ráfagas en los registros de control). */
typedef struct {
    uint8_t reg;
    uint8_t val;
} qmi_write_t;

extern const qmi_write_t qmi_config[];
extern const size_t qmi_config_len;

/** Comprueba la relectura de 0x02..0x0A (9 bytes) contra la configuración
 * con los sensores aún apagados. */
bool qmi_config_verify(const uint8_t regs02[9]);

typedef struct {
    bool fresh;      /**< STATUS0: datos nuevos de los dos sensores */
    uint32_t ts;     /**< contador de muestras (24 bits) */
    int16_t temp;    /**< 1/256 °C */
    int16_t a[3];    /**< crudo */
    int16_t g[3];
} qmi_sample_t;

/** Decodifica la ráfaga de 19 bytes (enteros con el byte bajo primero). */
void qmi_decode(const uint8_t raw[QMI_BURST_LEN], qmi_sample_t *out);
/** Muestras transcurridas entre dos contadores (con vuelta a 24 bits). */
uint32_t qmi_ts_delta(uint32_t prev, uint32_t now);
/** Crudo -> mili-g (±4 g: 8192 LSB/g). */
int32_t qmi_mg(int16_t raw);
/** Crudo -> mili-grados por segundo (±512 °/s: 64 LSB/(°/s)). */
int32_t qmi_mdps(int16_t raw);
/** Temperatura -> centésimas de °C (con signo también bajo 0 °C). */
int32_t qmi_temp_c100(int16_t raw);
/** Inclinación (décimas de grado) a partir de la aceleración en mili-g:
 * cabeceo (eje X) y alabeo (eje Y). false si no hay gravedad suficiente. */
bool qmi_tilt(const int32_t mg[3], int32_t *pitch_d10, int32_t *roll_d10);

#ifdef __cplusplus
}
#endif
