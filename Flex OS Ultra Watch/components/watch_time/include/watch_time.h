/*
 * watch_time — hora del reloj leída del RTC PCF85063 en SOLO LECTURA.
 *
 * Una tarea propia lee el RTC al arrancar y cada WATCH_TIME_RESYNC_MS por el
 * bus I2C compartido del BSP. Entre lecturas, la hora avanza con el
 * temporizador monótono del ESP32 (esp_timer), así que watch_time_now() nunca
 * espera al bus.
 *
 * Límites de esta versión (F3.2), a propósito:
 *  - No escribe el RTC ni toca el reloj del sistema (settimeofday). Poner en
 *    hora, SNTP y la zona horaria son F4.2.
 *  - La hora del RTC se muestra tal cual, como hora LOCAL (decisión D17).
 *  - Si el RTC perdió la hora (bit OS), la fecha es anterior a 2025 o el
 *    reloj está detenido, la hora se da por NO configurada: nunca se inventa.
 *  - Precisión: ±1 s respecto al RTC (no se conoce la fase del segundo).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "watch_time_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_TIME_RESYNC_MS (10u * 60u * 1000u)
#define WATCH_TIME_RETRY_MS  (30u * 1000u)

/** Arranca la tarea de lectura. bus = bsp_i2c_get_handle(). */
esp_err_t watch_time_start(i2c_master_bus_handle_t bus);

/** Hora actual (sin acceso al bus). Rellena *out solo si devuelve WATCH_TIME_OK. */
watch_time_state_t watch_time_now(wt_datetime_t *out);

/** Copia del estado del servicio (diagnóstico). */
void watch_time_info(watch_time_info_t *out);

#ifdef __cplusplus
}
#endif
