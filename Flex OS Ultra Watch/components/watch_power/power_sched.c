/*
 * power_sched — ver include/power_sched.h
 */
#include "power_sched.h"

#include <string.h>

void power_sched_init(power_sched_t *ps)
{
    memset(ps, 0, sizeof(*ps));
}

static bool in_fast(const power_sched_t *ps, uint32_t now)
{
    /* now < fast_until con aritmética sin signo (tolera la vuelta del reloj) */
    return (int32_t)(ps->fast_until_ms - now) > 0;
}

bool power_sched_need_full(power_sched_t *ps, uint32_t now, bool probe_ok, uint8_t reg00, bool pending)
{
    if (!ps->started || !probe_ok) {
        return true; /* arranque, o el bus falló: la muestra completa lo registra */
    }
    const uint8_t bits = reg00 & PWR_PROBE_MASK;
    bool changed = false;
    if (ps->have_probe && bits != ps->last_probe) {
        changed = true;
        ps->fast_until_ms = now + PWR_FAST_WINDOW_MS;
    }
    ps->last_probe = bits;
    ps->have_probe = true;
    const uint32_t since = now - ps->last_full_ms;
    if (changed) {
        return true;
    }
    if (pending || in_fast(ps, now)) {
        return since >= PWR_FAST_MS;
    }
    return since >= PWR_FULL_MS;
}

void power_sched_full_done(power_sched_t *ps, uint32_t now, bool changed, bool pending)
{
    ps->started = true;
    ps->last_full_ms = now;
    if (changed || pending) {
        /* Tras un cambio el cargador puede seguir moviéndose (arranque de la
         * carga tras enchufar): se sigue mirando deprisa un rato. */
        ps->fast_until_ms = now + PWR_FAST_WINDOW_MS;
    }
}

uint32_t power_sched_delay_ms(const power_sched_t *ps, uint32_t now, bool pending)
{
    if (pending || in_fast(ps, now)) {
        return PWR_FAST_MS;
    }
    return PWR_PROBE_MS;
}
