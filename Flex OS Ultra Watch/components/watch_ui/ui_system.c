/*
 * ui_system — un único temporizador lee la instantánea del servicio de batería
 * (copia protegida por mutex, sin esperar al bus I2C) y decide las capas
 * globales: animación de carga y avisos de batería baja o crítica. Las
 * pantallas leen la copia en caché con ui_system_power() en lugar de
 * consultar cada una el servicio.
 */
#include "ui_internal.h"
#include "ui_logic.h"

#define SYS_TICK_MS 500

static struct {
    bool power_ok;
    bool have;
    watch_power_snapshot_t snap;
    ui_charge_gate_t gate;
    batt_alert_state_t alerts;
    lv_timer_t *timer;
} s_sys;

static void sys_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_sys.power_ok || !watch_power_get(&s_sys.snap)) {
        return;
    }
    s_sys.have = true;
    const batt_tracker_t *tr = &s_sys.snap.track;
    if (ui_charge_gate_update(&s_sys.gate, tr->charge_events, tr->prev_state, tr->state)) {
        ui_charge_anim_play(tr->percent);
    }
    const batt_alert_t alert = batt_alert_update(&s_sys.alerts, tr->state, tr->level, lv_tick_get());
    if (alert != BATT_ALERT_NONE) {
        ui_banner_show_battery(alert, tr->percent);
    }
}

void ui_system_start(bool power_ok)
{
    if (s_sys.timer) {
        return;
    }
    s_sys.power_ok = power_ok;
    ui_charge_gate_init(&s_sys.gate);
    batt_alert_init(&s_sys.alerts);
    s_sys.timer = lv_timer_create(sys_tick, SYS_TICK_MS, NULL);
    sys_tick(s_sys.timer);
}

bool ui_system_power(watch_power_snapshot_t *out)
{
    if (!s_sys.have) {
        return false;
    }
    *out = s_sys.snap;
    return true;
}
