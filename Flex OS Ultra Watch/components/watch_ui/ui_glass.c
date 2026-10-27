/*
 * ui_glass — política del Flex Liquid Glass Engine (flex_glass): qué perfil
 * se usa en cada momento.
 *
 *  - El ajuste del usuario (Ajustes > Pantalla > Efectos de vidrio):
 *    Automático, Premium, Estándar o Ligero (watch_settings, NVS).
 *  - Flex Ultra Power Saving activo: Ligero (superficies opacas, sin
 *    transiciones, sin componer lo que hay debajo del panel rápido).
 *  - Automático: empieza en Premium y baja a Estándar si el reloj no llega.
 *    Se mide cada cuadro (de LV_EVENT_REFR_START a LV_EVENT_REFR_READY, en la
 *    tarea de LVGL); solo cuentan los "pesados", que redibujan casi toda la
 *    pantalla (el panel rápido siguiendo al dedo, una transición de página).
 *    La decisión es pura y se prueba en el PC (flex_glass_auto_frame): hace
 *    falta una racha de cuadros lentos y no vuelve a subir sola durante la
 *    sesión (sin vaivenes).
 *
 * Cambiar de perfil no recrea nada: el motor reescribe sus estilos
 * compartidos y LVGL redibuja una vez lo que los usa. El cambio que decide la
 * medida se aplica fuera del evento de dibujo (lv_async_call).
 */
#include "ui_internal.h"

#include "flex_glass.h"
#include "flex_tokens.h"
#include "watch_settings.h"

#define HEAVY_PCT 60 /* un cuadro que redibuja al menos el 60 % de la pantalla es pesado */

static struct {
    flex_glass_auto_t meter;
    int64_t t0;
    uint32_t px;
    bool started;
} s_g;

static void inv_cb(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (a && s_g.px < UINT32_MAX / 2) {
        s_g.px += (uint32_t)lv_area_get_size(a);
    }
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    s_g.t0 = ui_mono_us();
}

static void apply_async(void *ud)
{
    (void)ud;
    ui_glass_apply();
}

static void ready_cb(lv_event_t *e)
{
    (void)e;
    const bool heavy = (uint64_t)s_g.px * 100u >= (uint64_t)FLEX_SCREEN_W * FLEX_SCREEN_H * HEAVY_PCT;
    const int64_t dt = ui_mono_us() - s_g.t0;
    s_g.px = 0;
    if (dt < 0) {
        return;
    }
    if (flex_glass_auto_frame(&s_g.meter, (uint32_t)(dt / 1000), heavy)) {
        LV_LOG_WARN("vidrio: cuadros de pantalla completa lentos; techo automático %s",
                    flex_glass_profile_name(s_g.meter.cap));
        lv_async_call(apply_async, NULL);
    }
}

flex_glass_profile_t ui_glass_effective(void)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    return flex_glass_effective(ws.glass, ui_psave_active(), &s_g.meter);
}

void ui_glass_apply(void)
{
    flex_glass_set_profile(ui_glass_effective());
}

void ui_glass_init(void)
{
    if (s_g.started) {
        return;
    }
    s_g.started = true;
    flex_glass_auto_init(&s_g.meter);
    lv_display_t *d = lv_display_get_default();
    if (d) {
        lv_display_add_event_cb(d, inv_cb, LV_EVENT_INVALIDATE_AREA, NULL);
        lv_display_add_event_cb(d, start_cb, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(d, ready_cb, LV_EVENT_REFR_READY, NULL);
    }
    ui_glass_apply();
}

const char *ui_glass_setting_text(uint8_t setting)
{
    static char buf[32];
    switch (setting) {
    case WS_GLASS_PREMIUM:
        return "Premium";
    case WS_GLASS_STANDARD:
        return "Estándar";
    case WS_GLASS_LITE:
        return "Ligero";
    default:
        lv_snprintf(buf, sizeof(buf), "Auto (%s)", flex_glass_profile_name(s_g.meter.cap));
        return buf;
    }
}

const flex_glass_auto_t *ui_glass_meter(void)
{
    return &s_g.meter;
}
