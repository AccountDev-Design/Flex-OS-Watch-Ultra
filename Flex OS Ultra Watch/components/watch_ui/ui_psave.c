/*
 * ui_psave — Flex Ultra Power Saving.
 *
 * Un único modo de ahorro, que controlan el panel rápido y Device Care, y
 * que sigue activo aunque ninguna de las dos esté abierta (vive en los
 * ajustes y en los servicios, no en una página):
 *
 * Al activarlo:
 *  - guarda el estado REAL anterior (brillo, AOD, Wi-Fi y Bluetooth) en los
 *    ajustes persistentes, para restaurarlo aunque el reloj se reinicie;
 *  - baja el brillo físico del panel a UI_PSAVE_BRIGHT como máximo;
 *  - desactiva el AOD (ui_aod_enabled) sin tocar la preferencia guardada;
 *  - esfera mínima en negro (ui_face): sin fondo, sin complicaciones que
 *    sondeen y sin el IMU de la esfera Parallax;
 *  - transiciones a la mitad de duración (ui_nav) y apagado de pantalla a
 *    10 s como máximo (ui_screen);
 *  - radios según la política elegida: conservar, apagar el Wi-Fi (por
 *    defecto: el Clima y las actualizaciones dejan de consultar) o apagar
 *    Wi-Fi y Bluetooth.
 * La hora, la batería, las alarmas y la navegación siguen funcionando, y el
 * mosaico del panel rápido siempre permite salir.
 *
 * Al desactivarlo se restaura lo guardado: el brillo de antes y cada radio
 * que estaba encendida (las que el usuario ya tenía apagadas siguen así).
 * Sin reiniciar ni bloquear la interfaz: las radios se encienden en sus
 * propias tareas (watch_net, watch_phone).
 */
#include "ui_internal.h"

#include "watch_net.h"
#include "watch_phone.h"
#include "watch_settings.h"

static bool s_active;

bool ui_psave_active(void)
{
    return s_active;
}

static void apply_brightness(int percent)
{
    const watch_ui_boot_info_t *b = ui_boot_info();
    if (b->brightness_set) {
        b->brightness_set(percent);
    }
}

void ui_psave_init(void)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    s_active = ws.psave;
    if (s_active && ws.brightness > UI_PSAVE_BRIGHT) {
        apply_brightness(UI_PSAVE_BRIGHT);
    }
}

const char *ui_psave_radio_text(uint8_t policy)
{
    switch (policy) {
    case WS_PS_RADIO_KEEP:
        return "Conservar";
    case WS_PS_RADIO_ALL:
        return "Apagar ambas";
    default:
        return "Apagar Wi-Fi";
    }
}

void ui_psave_set(bool on)
{
    if (on == s_active) {
        return;
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    watch_net_status_t ns;
    watch_net_get(&ns);
    watch_phone_status_t ps;
    watch_phone_get(&ps);

    /* El brillo real del panel (el control lo aplica antes de guardarlo). */
    const watch_ui_boot_info_t *b = ui_boot_info();
    const int cur = b->brightness_get && b->brightness_get() >= 0 ? b->brightness_get() : ws.brightness;
    if (on) {
        ws.ps_bright = (uint8_t)cur;
        ws.ps_flags = (uint8_t)(WS_PS_PREV_VALID | (ws.aod ? WS_PS_PREV_AOD : 0) |
                                (ns.enabled ? WS_PS_PREV_WIFI : 0) | (ps.enabled ? WS_PS_PREV_BT : 0));
        ws.psave = true;
        if (cur > UI_PSAVE_BRIGHT) {
            ws.brightness = UI_PSAVE_BRIGHT;
            apply_brightness(UI_PSAVE_BRIGHT);
        }
        if (ws.psave_radio != WS_PS_RADIO_KEEP && ns.enabled) {
            watch_net_set_enabled(false);
        }
        if (ws.psave_radio == WS_PS_RADIO_ALL && ps.enabled) {
            watch_phone_set_enabled(false);
        }
    } else {
        if (ws.ps_flags & WS_PS_PREV_VALID) {
            if (ws.ps_bright >= WS_BRIGHT_MIN) {
                ws.brightness = ws.ps_bright;
                apply_brightness(ws.ps_bright);
            }
            ws.aod = (ws.ps_flags & WS_PS_PREV_AOD) != 0;
            if ((ws.ps_flags & WS_PS_PREV_WIFI) && !ns.enabled) {
                watch_net_set_enabled(true);
            }
            if ((ws.ps_flags & WS_PS_PREV_BT) && !ps.enabled) {
                watch_phone_set_enabled(true);
            }
        }
        ws.psave = false;
        ws.ps_flags = 0;
        ws.ps_bright = 0;
    }
    s_active = on;
    watch_settings_update(&ws);
    /* La esfera se rehace (mínima en negro, o la elegida al salir). */
    ui_nav_replace_root(ui_face_create);
}
