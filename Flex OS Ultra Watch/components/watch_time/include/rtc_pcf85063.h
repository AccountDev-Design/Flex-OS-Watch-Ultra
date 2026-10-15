/*
 * rtc_pcf85063 — decodificación PURA de los registros del RTC PCF85063A
 * (sin ESP-IDF; se prueba en el PC). Fuente: hoja de datos de NXP PCF85063A,
 * registro Control_1 y registros de hora y fecha.
 *
 *   00h Control_1  bit5 STOP (1 = reloj parado), bit1 12_24 (1 = modo 12 h)
 *   04h Seconds    bit7 OS (1 = el oscilador se paró: hora no fiable), BCD 0-59
 *   05h Minutes    BCD 0-59
 *   06h Hours      24 h: BCD 0-23 · 12 h: bit5 AM/PM (1 = PM), BCD 1-12
 *   07h Days       BCD 1-31
 *   08h Weekdays   0-6, asignable por el usuario: NO se usa (se calcula)
 *   09h Months     BCD 1-12
 *   0Ah Years      BCD 0-99 (2000-2099)
 *
 * El firmware lee 00h-0Ah en UNA transacción: el chip congela los contadores
 * durante la lectura y el puntero de registro avanza solo, así que la hora no
 * puede cambiar a mitad (p. ej. 09:59:59 -> 09:00:00). Este módulo NO escribe
 * el RTC: ponerlo en hora es F4.2.
 */
#pragma once

#include <stdint.h>
#include "wt_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_ADDR        0x51
#define PCF85063_REG_CTRL1   0x00
#define PCF85063_REG_SECONDS 0x04
#define PCF85063_READ_LEN    11 /* 00h..0Ah */

#define PCF85063_CTRL1_STOP  (1u << 5)
#define PCF85063_CTRL1_12H   (1u << 1)
#define PCF85063_SEC_OS      (1u << 7)
#define PCF85063_HOUR_PM     (1u << 5)

/* Una fecha anterior a este año no puede ser la hora real: el firmware no
 * existía. Cubre el RTC que arranca de cero (1-1-2000) con OS ya borrado. */
#define WT_MIN_PLAUSIBLE_YEAR 2025

typedef enum {
    RTC_READ_OK = 0,
    RTC_READ_OSC_STOPPED, /**< bit OS: se perdió la alimentación; hora no fiable */
    RTC_READ_STOPPED,     /**< bit STOP de Control_1: el reloj no avanza */
    RTC_READ_BAD_FIELD,   /**< BCD o rango imposibles */
    RTC_READ_IMPLAUSIBLE, /**< campos válidos pero año < WT_MIN_PLAUSIBLE_YEAR */
} rtc_read_t;

/**
 * Decodifica regs[0..10] (registros 00h..0Ah). Rellena *out (con weekday
 * calculado) solo si el resultado es RTC_READ_OK o RTC_READ_IMPLAUSIBLE.
 */
rtc_read_t pcf85063_decode(const uint8_t regs[PCF85063_READ_LEN], wt_datetime_t *out);

/** Texto corto para registros y diagnóstico. */
const char *pcf85063_read_text(rtc_read_t r);

#ifdef __cplusplus
}
#endif
