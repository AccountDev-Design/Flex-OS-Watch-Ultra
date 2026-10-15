/*
 * power_sched — cuándo leer el AXP2101 (lógica PURA, se prueba en el PC).
 *
 * Objetivo: reaccionar en menos de un segundo al enchufar o desenchufar sin
 * aumentar el tráfico I2C medio:
 *  - sondeo ligero cada PWR_PROBE_MS: un solo registro (00h: VBUS presente y
 *    batería presente);
 *  - muestra completa (estado, VBAT, % del gauge) cada PWR_FULL_MS;
 *  - modo rápido: si el sondeo ve un cambio o hay un estado pendiente de
 *    confirmar, muestra completa cada PWR_FAST_MS durante PWR_FAST_WINDOW_MS
 *    (la confirmación con tiempo mínimo está en battery_model.c).
 * Antes: 1 muestra completa cada 2 s (~9 lecturas). Ahora: 1 lectura cada
 * 0,5 s + 1 muestra completa cada 3 s, salvo durante un cambio.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWR_PROBE_MS        500u
#define PWR_FULL_MS         3000u
#define PWR_FAST_MS         200u
#define PWR_FAST_WINDOW_MS  3000u
/* REG00: bit5 VBUS good, bit3 batería presente */
#define PWR_PROBE_MASK      0x28u

typedef struct {
    bool started;          /**< ya hubo una muestra completa */
    bool have_probe;
    uint8_t last_probe;    /**< bits de PWR_PROBE_MASK de la última lectura */
    uint32_t last_full_ms;
    uint32_t fast_until_ms;
} power_sched_t;

void power_sched_init(power_sched_t *ps);

/**
 * Tras el sondeo ligero: ¿hay que tomar ya una muestra completa?
 * @param probe_ok  la lectura de 00h salió bien
 * @param reg00     su valor
 * @param pending   el seguimiento tiene un cambio sin confirmar
 */
bool power_sched_need_full(power_sched_t *ps, uint32_t now_ms, bool probe_ok, uint8_t reg00, bool pending);

/** Tras una muestra completa. changed = cambió el estado confirmado. */
void power_sched_full_done(power_sched_t *ps, uint32_t now_ms, bool changed, bool pending);

/** Espera hasta el próximo despertar de la tarea. */
uint32_t power_sched_delay_ms(const power_sched_t *ps, uint32_t now_ms, bool pending);

#ifdef __cplusplus
}
#endif
