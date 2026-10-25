/*
 * ui_wb_logic — textos de Bienestar (lógica PURA, sin LVGL; se prueba en el
 * PC): duraciones, ritmo, distancia, cifras con separador de miles y fechas
 * cortas de los entrenamientos.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** "5.855" (punto de miles, como en español). */
int wb_fmt_int(char *b, size_t n, uint32_t v);
/** "12:03" o "1:02:03" a partir de ms. */
int wb_fmt_dur(char *b, size_t n, uint32_t ms);
/** "27:04" (minutos:segundos, horas incluidas en los minutos hasta 99:59) y
 * las décimas aparte (",3"): el cronómetro grande del entrenamiento. */
void wb_fmt_stopwatch(char *mmss, size_t n1, char *tenth, size_t n2, uint32_t ms);
/** "1 h 05 min" / "42 min" / "35 s" (resúmenes). */
int wb_fmt_dur_words(char *b, size_t n, uint32_t s);
/** "4,54 km" (desde 1 km) o "850 m". */
int wb_fmt_dist(char *b, size_t n, uint32_t m);
/** Ritmo "8'37\"" por km; "--" sin datos o fuera de rango (> 59'59"). */
int wb_fmt_pace(char *b, size_t n, uint32_t s_per_km);
/** "72,5 kg" a partir de décimas de kg. */
int wb_fmt_weight(char *b, size_t n, uint16_t dkg);
/** Iniciales de los días ("L", "M", "X"...) para un día desde 1970 (0 = jueves). */
const char *wb_day_letter(int32_t day);
/** "hoy", "ayer" o "lun 14 oct" para un día (today: hoy). */
int wb_fmt_day(char *b, size_t n, int32_t day, int32_t today);
/** Altura de una barra de 0..h px para v sobre max (mínimo 2 px si v > 0). */
int32_t wb_bar_h(uint32_t v, uint32_t max, int32_t h);

#ifdef __cplusplus
}
#endif
