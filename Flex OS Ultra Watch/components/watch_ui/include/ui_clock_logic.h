/*
 * ui_clock_logic — lógica PURA de las apps de tiempo (F4.3; sin LVGL ni
 * ESP-IDF, se prueba en el PC). Los instantes son microsegundos de un reloj
 * monótono (esp_timer) o segundos de hora LOCAL (alarmas).
 *
 * - Cronómetro (referencia AppChrono.h:107-209): parado / en marcha / en
 *   pausa; hasta 20 vueltas {parcial, total}; al llenarse se descarta la más
 *   antigua sin renumerar; mejor y peor vuelta solo con 3 o más y si no son
 *   todas iguales; "MM:SS.cc" y desde 1 h "H:MM:SS.cc".
 * - Temporizador (no existe en la referencia): por instante de fin absoluto,
 *   así no se desvía aunque la interfaz se retrase; al llegar a 0 suena una
 *   sola vez.
 * - Alarmas (no existen en la referencia): hasta 5, hora local, días de la
 *   semana (bit 0 = domingo) o una sola vez; se comparan con la hora LOCAL
 *   (el día de la semana del RTC es el de UTC y no sirve).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wt_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Cronómetro ---- */
#define SW_MAX_LAPS 20

typedef enum { SW_IDLE = 0, SW_RUN, SW_PAUSE } sw_state_t;

typedef struct {
    int64_t split_us; /**< tiempo de esta vuelta */
    int64_t total_us; /**< tiempo total al marcarla */
} sw_lap_t;

typedef struct {
    sw_state_t st;
    int64_t accum_us;           /**< tiempo acumulado antes de la última reanudación */
    int64_t t0_us;              /**< instante de la última reanudación */
    sw_lap_t laps[SW_MAX_LAPS]; /**< la más antigua primero */
    int n;                      /**< vueltas guardadas */
    int first_no;               /**< número de la vuelta laps[0] (1 = la primera) */
} sw_t;

void sw_reset(sw_t *s);
void sw_start(sw_t *s, int64_t now_us); /**< desde parado o en pausa */
void sw_pause(sw_t *s, int64_t now_us);
void sw_lap(sw_t *s, int64_t now_us);   /**< solo en marcha */
int64_t sw_elapsed(const sw_t *s, int64_t now_us);
/** Índices (en laps[]) de la mejor y la peor vuelta; false si no aplica. */
bool sw_best_worst(const sw_t *s, int *best, int *worst);
/** "01:02.34" o "1:02:03.45"; sin centésimas: "01:02" / "1:02:03". */
size_t sw_format(int64_t us, bool hundredths, char *buf, size_t n);

/* ---- Temporizador ---- */
#define TMR_MAX_US (99LL * 60 * 1000000 + 59LL * 1000000) /* 99:59 */

typedef enum { TMR_IDLE = 0, TMR_RUN, TMR_PAUSE, TMR_RING } tmr_state_t;

typedef struct {
    tmr_state_t st;
    int64_t dur_us;  /**< duración elegida (para el anillo de progreso) */
    int64_t end_us;  /**< fin, en marcha */
    int64_t left_us; /**< lo que queda, en pausa */
} tmr_t;

void tmr_start(tmr_t *t, int64_t dur_us, int64_t now_us);
void tmr_pause(tmr_t *t, int64_t now_us);
void tmr_resume(tmr_t *t, int64_t now_us);
void tmr_cancel(tmr_t *t);
/** Suma tiempo (también tras sonar: vuelve a contar desde ahora). */
void tmr_add(tmr_t *t, int64_t us, int64_t now_us);
int64_t tmr_left(const tmr_t *t, int64_t now_us);
/** true UNA vez, cuando llega a 0 en marcha (pasa a TMR_RING). */
bool tmr_check(tmr_t *t, int64_t now_us);
/** Progreso 0..1000 (lo que queda respecto a la duración). */
int tmr_permille(const tmr_t *t, int64_t now_us);
/** "04:59" (redondea hacia arriba: muestra 00:01 hasta llegar a 0) o "1:00:00". */
size_t tmr_format(int64_t left_us, char *buf, size_t n);

/* ---- Alarmas ---- */
#define AL_MAX       5
#define AL_BLOB_VER  1
#define AL_SNOOZE_S  (5 * 60)

typedef struct {
    uint8_t on;
    uint8_t hour;   /**< 0..23, hora local */
    uint8_t minute; /**< 0..59 */
    uint8_t days;   /**< bit 0 = domingo .. bit 6 = sábado; 0 = una sola vez */
} alarm_t;

typedef struct {
    uint8_t n;
    alarm_t a[AL_MAX];
} alarm_set_t;

/** ¿Suena en este minuto local? */
bool alarm_matches(const alarm_t *a, const wt_datetime_t *local);
/** Próximo instante local (segundos, minuto exacto) estrictamente posterior a
 * now; -1 si está desactivada. */
int64_t alarm_next_local(const alarm_t *a, int64_t now_local_s);
/** La próxima de todas (índice en *idx); -1 si ninguna. */
int64_t alarms_next(const alarm_set_t *s, int64_t now_local_s, int *idx);
/** "Una vez", "Todos los días", "L a V", "Fin de semana" o "L X V". */
size_t alarm_days_text(uint8_t days, char *buf, size_t n);
/** "hoy 7:00", "mañana 7:00" o "lun 7:00" (12 h: "7:00 AM"). */
size_t alarm_when_text(int64_t when_local_s, int64_t now_local_s, bool h24, char *buf, size_t n);
/** Empaquetado para watch_settings (4 bytes por alarma, sin cabecera). */
size_t alarm_pack(const alarm_set_t *s, uint8_t *out, size_t cap);
/** false si algún campo es imposible (no se usan datos dañados). */
bool alarm_unpack(const uint8_t *in, size_t n, alarm_set_t *out);

#ifdef __cplusplus
}
#endif
