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
 * puede cambiar a mitad (p. ej. 09:59:59 -> 09:00:00).
 *
 * Para escribir (F4.2) este módulo solo CODIFICA los bytes; la escritura con
 * STOP la hace watch_time. Control_1 se escribe conservando CIE y CAP_SEL y
 * nunca con el patrón de reinicio por software (58h).
 *   03h RAM_byte  byte libre del usuario: aquí, la marca de Flex OS
 *                 (PCF85063_MARKER) que dice "hora UTC escrita por Flex OS".
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "wt_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_ADDR        0x51
#define PCF85063_REG_CTRL1   0x00
#define PCF85063_REG_SECONDS 0x04
#define PCF85063_READ_LEN    11 /* 00h..0Ah */

#define PCF85063_REG_RAM     0x03
#define PCF85063_TIME_LEN    7  /* 04h..0Ah */
#define PCF85063_CTRL1_EXT_TEST (1u << 7)
#define PCF85063_CTRL1_STOP  (1u << 5)
#define PCF85063_CTRL1_SR    (1u << 4)
#define PCF85063_CTRL1_CIE   (1u << 2)
#define PCF85063_CTRL1_12H   (1u << 1)
#define PCF85063_CTRL1_CAP_SEL (1u << 0)
#define PCF85063_MARKER      0x5A /* en RAM_byte: hora UTC escrita por Flex OS */
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

typedef enum {
    RTC_ENC_OK = 0,
    RTC_ENC_BAD_YEAR, /**< fuera de 2000..2099 */
    RTC_ENC_BAD_DATE, /**< mes o día imposibles */
    RTC_ENC_BAD_TIME, /**< hora, minuto o segundo imposibles */
} rtc_enc_t;

/**
 * Bytes de los registros 04h..0Ah para la fecha y hora dadas (OS = 0; el día
 * de la semana se calcula, se ignora dt->weekday). mode_12h solo existe para
 * probar la simetría con el decodificador: el firmware escribe en 24 h. No
 * toca out si hay error.
 */
rtc_enc_t pcf85063_encode(const wt_datetime_t *dt, bool mode_12h, uint8_t out[PCF85063_TIME_LEN]);
/** Igual, desde segundos UTC desde 1970 (946684800..4102444799). */
rtc_enc_t pcf85063_encode_utc(int64_t utc_s, bool mode_12h, uint8_t out[PCF85063_TIME_LEN]);
/**
 * Control_1 para escribir la hora: conserva CIE y CAP_SEL del valor leído,
 * pone 24 h, borra EXT_TEST y SR (nunca un reinicio) y pone o quita STOP.
 */
uint8_t pcf85063_ctrl1_for_set(uint8_t ctrl1_read, bool stop);

#ifdef __cplusplus
}
#endif
