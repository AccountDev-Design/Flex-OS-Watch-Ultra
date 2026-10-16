/*
 * watch_time — hora del reloj con el RTC PCF85063 (F3.2 lectura, F4.2 puesta
 * en hora y zona horaria).
 *
 * Una tarea propia es la ÚNICA que habla con el RTC: lo lee al arrancar y
 * cada WATCH_TIME_RESYNC_MS y, cuando se le pide, lo pone en hora. Entre
 * lecturas la hora avanza con el temporizador monótono del ESP32
 * (esp_timer), así que watch_time_now() nunca espera al bus.
 *
 * - El RTC guarda la hora en UTC. La zona horaria es un desfase fijo en
 *   minutos (D17; por defecto Lima, UTC-5, sin horario de verano, como la
 *   referencia). Cambiar la zona nunca escribe el RTC.
 * - La hora solo se da por buena si la escribió Flex OS: marca PCF85063_MARKER
 *   en RAM_byte (03h). Una hora del demo de fábrica (en zona desconocida), un
 *   RTC sin alimentación (OS), detenido (STOP) o anterior a 2025 cuentan como
 *   "sin configurar": nunca se inventa.
 * - Puesta en hora (en la tarea): STOP con Control_1 conservando CIE y
 *   CAP_SEL, una sola escritura de 03h-0Ah (marca y hora, OS = 0), quitar
 *   STOP, releer y comprobar, y 1,1 s después comprobar que el reloj avanza.
 *   Si algo falla, el RTC queda detenido (se verá "sin configurar" tras un
 *   reinicio, nunca una hora mal escrita), la hora pedida sigue en RAM y se
 *   reintenta cada WATCH_TIME_RETRY_MS.
 * - Reloj del sistema (time(), gettimeofday): se ajusta en UTC con cada
 *   lectura válida si difiere 2 s o más, y tras cada puesta en hora.
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

/** Hora LOCAL actual (sin acceso al bus). Rellena *out solo si devuelve WATCH_TIME_OK. */
watch_time_state_t watch_time_now(wt_datetime_t *out);

/** Segundos UTC desde 1970. false si no hay hora válida. */
bool watch_time_now_utc(int64_t *utc_s);

/**
 * Pide poner el RTC en hora (segundos UTC; año 2025-2099). Vuelve enseguida:
 * la escritura la hace la tarea y su resultado queda en
 * watch_time_info().set_result. Desde ese momento watch_time_now() ya da la
 * hora nueva.
 */
esp_err_t watch_time_set_utc(int64_t utc_s);

/** Zona horaria (min respecto a UTC, -720..840). Solo cambia lo que se muestra. */
void watch_time_set_tz(int off_min);
int watch_time_get_tz(void);

/** Copia del estado del servicio (diagnóstico). */
void watch_time_info(watch_time_info_t *out);

#ifdef __cplusplus
}
#endif
