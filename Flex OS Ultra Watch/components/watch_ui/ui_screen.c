/*
 * ui_screen — apagado de pantalla por inactividad (F4.5).
 *
 *  - Inactividad = la que mide LVGL desde el último toque
 *    (lv_display_get_inactive_time). Con el tiempo de Ajustes > Pantalla >
 *    Apagado (5-60 s, 15 por defecto): los últimos segundos se atenúa (un
 *    tercio del brillo del usuario, sin cambiarlo) y después se apaga
 *    (brillo 0 + DISPOFF; watch_display_set_power).
 *  - Atenuación PROGRESIVA y configurable (Ajustes > Pantalla y Device Care:
 *    desactivada, suave, media o fuerte): el brillo físico del panel (DCS
 *    0x51, no la opacidad de LVGL) baja poco a poco durante la fase atenuada.
 *  - Al apagarse vuelve a la esfera SIN cerrar la app abierta (queda
 *    suspendida, ui_nav_suspend; sin código, D16) y cierra el panel rápido.
 *  - Con AOD activado (y sin Flex Ultra Power Saving), en vez de apagarse
 *    pasa a la pantalla siempre activa (ui_aod.c): negro, hora atenuada,
 *    brillo físico bajo y Pixel Shift cada minuto. Un toque o el botón la
 *    encienden. La elevación de muñeca NO la enciende: el controlador del
 *    QMI8658 no usa sus interrupciones de movimiento (sin GPIO verificado) y
 *    medir sin parar gastaría más que el propio AOD.
 *  - Se enciende con un toque, el botón lateral, una alarma o el
 *    temporizador, al enchufar el USB o con un aviso de batería. El toque que
 *    la enciende (o que la saca de la atenuación) no llega a la interfaz: lo
 *    recoge una capa transparente sobre todo.
 *  - No se apaga mientras suena un aviso, se ve la animación de carga o está
 *    abierta la prueba de pantalla.
 *  - Sin control de la pantalla (watch_ui_boot_info_t.display_power = NULL)
 *    no se apaga nunca.
 * La CPU no entra en reposo ligero automático: eso exige medir en el reloj
 * (consumo y despertar del táctil) y queda para F8.4.
 */
#include "ui_internal.h"

#include "ui_screen_logic.h"
#include "watch_settings.h"
#include "watch_theme.h"

#define AOD_LEVEL 8 /* % de brillo físico en AOD */

static struct {
    scr_state_t st;
    uint32_t dim_at;  /* inicio de la fase atenuada */
    int dim_last;     /* último brillo enviado durante la rampa */
    lv_obj_t *shield;
    btn_t btn;
    bool enabled;
    int holds;
    uint32_t offs;  /* veces que se apagó (diagnóstico y pruebas) */
    uint32_t wakes;
} s_scr;

static const watch_ui_boot_info_t *bi(void)
{
    return ui_boot_info();
}

static void shield_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ui_screen_wake();
    } else if (s_scr.st == SCR_ON && s_scr.shield) {
        /* al soltar (ya encendida): la capa se va; el siguiente toque es normal */
        lv_obj_delete_async(s_scr.shield);
        s_scr.shield = NULL;
    }
}

static void shield_on(void)
{
    if (s_scr.shield) {
        return;
    }
    lv_obj_t *s = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, shield_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s, shield_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s, shield_cb, LV_EVENT_PRESS_LOST, NULL);
    s_scr.shield = s;
}

static void shield_off_if_idle(void)
{
    /* Sin dedo encima se quita ya; con dedo, al soltar (shield_cb). */
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (s_scr.shield && (!indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED)) {
        lv_obj_delete_async(s_scr.shield);
        s_scr.shield = NULL;
    }
}

static void apply(scr_state_t to)
{
    const scr_state_t from = s_scr.st;
    if (to == from) {
        return;
    }
    s_scr.st = to;
    const watch_ui_boot_info_t *b = bi();
    switch (to) {
    case SCR_ON:
        ui_aod_hide();
        if (from == SCR_OFF && b->display_power) {
            b->display_power(true);
        } else if (b->display_level) {
            b->display_level(-1); /* vuelve el brillo del usuario */
        }
        s_scr.wakes++;
        shield_off_if_idle();
        break;
    case SCR_DIM:
        s_scr.dim_at = lv_tick_get();
        s_scr.dim_last = -1;
        shield_on();
        break;
    case SCR_AOD:
    case SCR_OFF:
        shield_on();
        if (ui_qp_active()) {
            ui_qp_close();
        }
        ui_nav_suspend(false); /* al volver, la esfera; la app sigue suspendida */
        if (to == SCR_AOD) {
            if (from == SCR_OFF && b->display_power) {
                b->display_power(true);
            }
            ui_aod_show();
            if (b->display_level) {
                const int user = b->brightness_get ? b->brightness_get() : AOD_LEVEL;
                b->display_level(user >= 0 && user < AOD_LEVEL ? user : AOD_LEVEL);
            }
        } else {
            ui_aod_hide();
            if (b->display_power) {
                b->display_power(false);
            }
        }
        s_scr.offs++;
        break;
    }
}

static bool held(void)
{
    return s_scr.holds > 0 || ui_ring_active() || ui_charge_anim_active();
}

void ui_screen_init(void)
{
    s_scr.st = SCR_ON;
    btn_init(&s_scr.btn);
    s_scr.enabled = bi()->display_power != NULL;
}

void ui_screen_enable(bool on)
{
    s_scr.enabled = on;
    if (!on) {
        ui_screen_wake();
    }
}

void ui_screen_tick(void)
{
    const watch_ui_boot_info_t *b = bi();
    const bool aod = ui_aod_enabled();
    /* Botón lateral: una pulsación corta apaga (o pasa al AOD) o enciende. */
    if (b->button_level) {
        const int lv = b->button_level();
        if (lv >= 0 && btn_update(&s_scr.btn, lv, lv_tick_get())) {
            if ((s_scr.st == SCR_ON || s_scr.st == SCR_DIM) && s_scr.enabled && !held()) {
                apply(aod ? SCR_AOD : SCR_OFF);
            } else {
                ui_screen_wake();
            }
            return;
        }
    }
    if (!s_scr.enabled) {
        return;
    }
    /* El AOD se desactivó (o entró el ahorro) con la pantalla en AOD: apagar. */
    if (s_scr.st == SCR_AOD && !aod) {
        apply(SCR_OFF);
        return;
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    uint32_t timeout = (uint32_t)ws.screen_to_s * 1000u;
    if (ui_psave_active() && timeout > UI_PSAVE_SCREEN_TO_MS) {
        timeout = UI_PSAVE_SCREEN_TO_MS;
    }
    apply(scr_next_ex(s_scr.st, lv_display_get_inactive_time(NULL), timeout, held(), aod, ws.dim != WS_DIM_OFF));
    if (s_scr.st == SCR_DIM && b->display_level && b->brightness_get) {
        /* Rampa del brillo físico hasta el nivel atenuado. */
        const int user = b->brightness_get();
        const int lvl = scr_dim_ramp(user, ws_dim_level(ws.dim, user), lv_tick_elaps(s_scr.dim_at),
                                     scr_dim_ms(timeout));
        if (lvl != s_scr.dim_last) {
            s_scr.dim_last = lvl;
            b->display_level(lvl);
        }
    }
}

void ui_screen_wake(void)
{
    lv_display_trigger_activity(NULL);
    apply(SCR_ON);
}

bool ui_screen_is_on(void)
{
    return s_scr.st != SCR_OFF && s_scr.st != SCR_AOD;
}

int ui_screen_state(void)
{
    return (int)s_scr.st;
}

uint32_t ui_screen_offs(void)
{
    return s_scr.offs;
}

static void hold_del_cb(lv_event_t *e)
{
    (void)e;
    if (s_scr.holds > 0) {
        s_scr.holds--;
    }
}

void ui_screen_hold_page(lv_obj_t *page)
{
    s_scr.holds++;
    lv_obj_add_event_cb(page, hold_del_cb, LV_EVENT_DELETE, NULL);
}

int ui_screen_button_idle(void)
{
    return s_scr.btn.idle;
}

uint32_t ui_screen_wakes(void)
{
    return s_scr.wakes;
}
