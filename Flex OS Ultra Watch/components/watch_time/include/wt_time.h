/*
 * wt_time — fechas y horas PURAS (sin ESP-IDF; se prueba en el PC).
 *
 * Calendario gregoriano proléptico con los algoritmos de días civiles de
 * Howard Hinnant (days_from_civil / civil_from_days), en enteros: sin tablas
 * de zona horaria ni coma flotante. Los textos siguen a la referencia
 * (FlexOS_Ultra_Session.h:369-388 y Home.h:441-454): "Jueves, 15 de octubre",
 * "jue, 15 oct" y la hora de 24 h como "9:05" (Clock.h:181).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;    /**< 4 cifras */
    int month;   /**< 1..12 */
    int day;     /**< 1..31 */
    int hour;    /**< 0..23 */
    int minute;  /**< 0..59 */
    int second;  /**< 0..59 */
    int weekday; /**< 0 = domingo .. 6 = sábado (calculado de la fecha) */
} wt_datetime_t;

int wt_is_leap(int year);
/** Días del mes, o 0 si el mes no es válido. */
int wt_days_in_month(int year, int month);
/** Días desde el 1-1-1970 (negativo antes). */
int64_t wt_days_from_civil(int year, int month, int day);
void wt_civil_from_days(int64_t days, int *year, int *month, int *day);
/** Día de la semana, 0 = domingo. */
int wt_weekday_from_days(int64_t days);

/** Segundos desde el 1-1-1970 00:00 de la fecha y hora dadas (sin zona). */
int64_t wt_to_seconds(const wt_datetime_t *dt);
/** Inverso de wt_to_seconds(); rellena también weekday. */
void wt_from_seconds(int64_t seconds, wt_datetime_t *dt);

/** "9:05" (24 h). Devuelve lo mismo que snprintf. */
int wt_fmt_hhmm(char *buf, size_t n, const wt_datetime_t *dt);
/** "9:05" en 12 h (0 h -> "12:05"), sin AM/PM: la esfera grande no lo lleva
 * (referencia, Clock.h:179-185). */
int wt_fmt_hhmm12(char *buf, size_t n, const wt_datetime_t *dt);
/** "AM" o "PM" (para barras: "9:05 PM", Clock.h:186-196). */
const char *wt_ampm(const wt_datetime_t *dt);

/* ---- Zona horaria: desfase fijo en minutos respecto a UTC (D17) ---- */
/** Hora local de un instante UTC con el desfase dado. */
void wt_utc_to_local(int64_t utc_s, int off_min, wt_datetime_t *out);
/** Instante UTC de una hora local con el desfase dado. */
int64_t wt_local_to_utc(const wt_datetime_t *local, int off_min);
/** "UTC-05:00", "UTC+05:30", "UTC". */
int wt_fmt_utc_offset(char *buf, size_t n, int off_min);
/** "Jueves, 15 de octubre" */
int wt_fmt_date_long(char *buf, size_t n, const wt_datetime_t *dt);
/** "jue, 15 oct" */
int wt_fmt_date_short(char *buf, size_t n, const wt_datetime_t *dt);

#ifdef __cplusplus
}
#endif
