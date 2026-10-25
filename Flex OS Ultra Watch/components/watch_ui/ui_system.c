/*
 * ui_system — un único temporizador lleva el estado del servicio de batería a
 * la interfaz y decide las capas globales (animación de carga y avisos de
 * batería baja o crítica).
 *
 * Respuesta: cada SYS_TICK_MS se mira watch_power_generation(), una lectura
 * atómica sin bloqueo; solo cuando cambia se copia la instantánea (con el
 * mutex del servicio, que nunca se retiene durante el I2C) y se avisa en el
 * mismo ciclo a las pantallas suscritas (ui_power_listen). Así un cambio de
 * carga llega a la pantalla en <= 100 ms desde que el servicio lo confirma,
 * sin que cada pantalla sondee por su cuenta y sin tocar el panel.
 */
#include "ui_internal.h"
#include "ui_logic.h"

#define SYS_TICK_MS   100
#define MAX_LISTENERS 16

typedef struct {
    lv_obj_t *obj;
    ui_power_cb_t cb;
} listener_t;

static struct {
    bool power_ok;
    bool have;
    uint32_t gen;
    watch_power_snapshot_t snap;
    ui_charge_gate_t gate;
    batt_alert_state_t alerts;
    lv_timer_t *timer;
    listener_t listeners[MAX_LISTENERS];
} s_sys;

static void notify(void)
{
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_sys.listeners[i].obj) {
            s_sys.listeners[i].cb(s_sys.listeners[i].obj, &s_sys.snap);
        }
    }
}

static void sys_tick(lv_timer_t *t)
{
    (void)t;
    ui_clock_tick();  /* temporizador y alarmas: también sin datos de batería */
    ui_screen_tick(); /* apagado por inactividad y botón lateral */
    ui_notif_watch_tick(ui_screen_is_on()); /* aviso al llegar una notificación del teléfono */
    ui_ota_watch_tick(); /* versión nueva o aviso tras actualizar (F7) */
    ui_wb_watch_tick(ui_screen_is_on()); /* Bienestar: pausa, reanudación y fin automáticos */
    if (!s_sys.power_ok) {
        return;
    }
    const uint32_t gen = watch_power_generation();
    if (s_sys.have && gen == s_sys.gen) {
        /* Los avisos dependen también del tiempo (enfriamiento): se
         * evalúan aunque no cambie la instantánea. */
        const batt_tracker_t *tr = &s_sys.snap.track;
        const batt_alert_t alert = batt_alert_update(&s_sys.alerts, tr->state, tr->level, lv_tick_get());
        if (alert != BATT_ALERT_NONE) {
            ui_screen_wake();
            ui_banner_show_battery(alert, tr->percent);
        }
        return;
    }
    if (!watch_power_get(&s_sys.snap)) {
        return;
    }
    s_sys.gen = gen;
    s_sys.have = true;
    const batt_tracker_t *tr = &s_sys.snap.track;
    if (ui_charge_gate_update(&s_sys.gate, tr->charge_events, tr->prev_state, tr->state)) {
        ui_screen_wake(); /* enchufar enciende la pantalla */
        ui_charge_anim_play(tr->percent);
    }
    const batt_alert_t alert = batt_alert_update(&s_sys.alerts, tr->state, tr->level, lv_tick_get());
    if (alert != BATT_ALERT_NONE) {
        ui_screen_wake();
        ui_banner_show_battery(alert, tr->percent);
    }
    notify();
}

void ui_system_start(bool power_ok)
{
    if (s_sys.timer) {
        return;
    }
    s_sys.power_ok = power_ok;
    s_sys.gen = 0;
    ui_charge_gate_init(&s_sys.gate);
    batt_alert_init(&s_sys.alerts);
    ui_clock_init();
    ui_psave_init(); /* Flex Ultra Power Saving guardado: antes de crear la esfera */
    ui_screen_init();
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

static void listener_delete_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_sys.listeners[i].obj == obj) {
            s_sys.listeners[i].obj = NULL;
            s_sys.listeners[i].cb = NULL;
        }
    }
}

bool ui_power_listen(lv_obj_t *obj, ui_power_cb_t cb)
{
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!s_sys.listeners[i].obj) {
            s_sys.listeners[i].obj = obj;
            s_sys.listeners[i].cb = cb;
            lv_obj_add_event_cb(obj, listener_delete_cb, LV_EVENT_DELETE, NULL);
            if (s_sys.have) {
                cb(obj, &s_sys.snap); /* estado actual nada más suscribirse */
            }
            return true;
        }
    }
    LV_LOG_WARN("ui_power_listen: sin hueco para mas suscriptores");
    return false;
}

int ui_power_listener_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_LISTENERS; i++) {
        n += s_sys.listeners[i].obj != NULL;
    }
    return n;
}
