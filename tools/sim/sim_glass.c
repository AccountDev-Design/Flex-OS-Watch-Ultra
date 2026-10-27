/*
 * Flex Liquid Glass Engine en la vista previa: perfiles en caliente,
 * política (ajuste, ahorro), memoria y el desenfoque nativo de LVGL.
 *
 *  - Cambiar de perfil no crea ni borra objetos, no deja restos en pantalla
 *    y cambia de verdad la composición (el velo del panel rápido es
 *    translúcido en Premium y Estándar y opaco en Ligero).
 *  - Flex Ultra Power Saving pone Ligero y al salir vuelve el anterior.
 *  - Ajustes > Pantalla > Vidrio recorre Automático, Premium, Estándar y
 *    Ligero y se guarda.
 *  - Diez cambios de perfil no dejan memoria de LVGL.
 *  - blur_backdrop de LVGL 9.5 con búferes parciales: se mide cuánto difiere
 *    redibujar solo una zona de la tarjeta desenfocada frente a redibujarla
 *    entera (por qué el motor no lo usa: docs/LIQUID-GLASS.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flex_glass.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

static void set_face(int style, flex_wall_t wall)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.face = (uint8_t)style;
    ws.wallpaper = (uint8_t)wall;
    watch_settings_update(&ws);
    watch_theme_set_wallpaper(wall);
    ui_nav_replace_root(ui_face_create);
    sim_run(600);
}

static void set_setting(uint8_t g)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.glass = g;
    watch_settings_update(&ws);
    ui_glass_apply();
    sim_run(300);
}

static uint32_t count_objs(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += count_objs(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

/* ¿Se ve la esfera a través del velo? (opacidad efectiva del velo < 255) */
static bool qp_translucent(void)
{
    lv_obj_t *r = ui_qp_obj();
    if (!r) {
        return false;
    }
    const lv_opa_t opa = lv_obj_get_style_bg_opa(r, 0);
    const bool grad = lv_obj_get_style_bg_grad_dir(r, 0) != LV_GRAD_DIR_NONE;
    if (!grad) {
        return opa < LV_OPA_COVER;
    }
    return opa < LV_OPA_COVER || lv_obj_get_style_bg_main_opa(r, 0) < LV_OPA_COVER ||
           lv_obj_get_style_bg_grad_opa(r, 0) < LV_OPA_COVER;
}

static void suite_profiles(const char *out)
{
    set_face(UI_FACE_DIGITAL, FLEX_WALL_AURORA);
    set_setting(WS_GLASS_AUTO);
    EXPECT(flex_glass_profile() == FLEX_GLASS_PREMIUM, "vidrio: automatico no empieza en Premium");
    ui_qp_open();
    sim_run(400);
    EXPECT(ui_qp_is_open(), "vidrio: el panel no se abre");
    const uint32_t objs = count_objs(ui_nav_screen());
    EXPECT(qp_translucent(), "vidrio: en Premium el velo del panel no deja ver la esfera");
    sim_save(out, "lg_panel_premium");
    sim_check_residue("panel en Premium");

    static const struct {
        uint8_t setting;
        flex_glass_profile_t want;
        bool translucent;
        const char *shot;
    } kSteps[] = {
        {WS_GLASS_STANDARD, FLEX_GLASS_STANDARD, true, "lg_panel_estandar"},
        {WS_GLASS_LITE, FLEX_GLASS_LITE, false, "lg_panel_ligero"},
        {WS_GLASS_PREMIUM, FLEX_GLASS_PREMIUM, true, NULL},
    };
    for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); i++) {
        set_setting(kSteps[i].setting);
        char w[64];
        snprintf(w, sizeof(w), "panel en %s", flex_glass_profile_name(kSteps[i].want));
        EXPECT(flex_glass_profile() == kSteps[i].want, "vidrio: %s no aplicado", w);
        EXPECT(ui_qp_is_open() && count_objs(ui_nav_screen()) == objs, "vidrio: %s recreo objetos", w);
        EXPECT(qp_translucent() == kSteps[i].translucent, "vidrio: %s con el velo equivocado", w);
        sim_check_residue(w);
        sim_lint(w);
        if (kSteps[i].shot) {
            sim_save(out, kSteps[i].shot);
        }
    }
    ui_qp_close();
    sim_run(400);

    /* Ahorro de energía: Ligero mientras dura, y vuelve el del ajuste */
    ui_psave_set(true);
    sim_run(800);
    EXPECT(flex_glass_profile() == FLEX_GLASS_LITE, "vidrio: el ahorro no pone Ligero");
    ui_psave_set(false);
    sim_run(800);
    EXPECT(flex_glass_profile() == FLEX_GLASS_PREMIUM, "vidrio: al salir del ahorro no vuelve Premium");

    /* Diez cambios de perfil sin memoria de más */
    const uint32_t blocks0 = sim_mem_blocks();
    for (int k = 0; k < 10; k++) {
        flex_glass_set_profile((flex_glass_profile_t)(k % FLEX_GLASS_PROFILE_COUNT));
        sim_run(50);
    }
    ui_glass_apply();
    sim_run(200);
    EXPECT(sim_mem_blocks() == blocks0, "vidrio: los cambios de perfil dejan memoria (%u -> %u bloques)",
           (unsigned)blocks0, (unsigned)sim_mem_blocks());
    printf("  estilos del motor: %u (fijos: funciones y paleta de tintes)\n", (unsigned)flex_glass_style_count());
}

static void suite_setting(const char *out)
{
    ui_nav_home_instant();
    set_setting(WS_GLASS_AUTO);
    ui_nav_push(ui_display_page_create);
    sim_run(400);
    lv_obj_t *row = sim_find_label(ui_nav_top(), "Vidrio");
    while (row && !lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE)) {
        row = lv_obj_get_parent(row);
    }
    EXPECT(row, "vidrio: no hay fila en Ajustes > Pantalla");
    static const char *const kVals[] = {"Premium", "Estándar", "Ligero", "Auto (Premium)"};
    static const flex_glass_profile_t kProf[] = {FLEX_GLASS_PREMIUM, FLEX_GLASS_STANDARD, FLEX_GLASS_LITE,
                                                 FLEX_GLASS_PREMIUM};
    for (int i = 0; row && i < 4; i++) {
        lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
        sim_run(300);
        EXPECT(sim_find_label(row, kVals[i]), "vidrio: la fila no dice %s", kVals[i]);
        EXPECT(flex_glass_profile() == kProf[i], "vidrio: la fila no aplica %s", kVals[i]);
        if (i == 1) {
            sim_lint("ajustes en Estandar");
            sim_save(out, "lg_ajustes_estandar");
        }
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    EXPECT(ws.glass == WS_GLASS_AUTO, "vidrio: el ajuste no se guarda (%u)", (unsigned)ws.glass);
    ui_nav_home_instant();
    sim_run(300);
}

/* Componentes del motor sobre un fondo complejo: tarjeta, botón, botón
 * tintado, lámina, interruptor y deslizador (captura para la documentación). */
static void suite_components(const char *out)
{
    set_face(UI_FACE_MONTANA, FLEX_WALL_BLACK);
    lv_obj_t *box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_t *card = flex_glass_panel(box, FLEX_GLASS_CARD, 40, 40, 330, 96, 26);
    lv_obj_t *t = watch_theme_label(card, FLEX_TYPE_HEADLINE, FLEX_GLASS_TXT, "Tarjeta");
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 18, -12);
    lv_obj_t *t2 = watch_theme_label(card, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "Material de contenido");
    lv_obj_align(t2, LV_ALIGN_LEFT_MID, 18, 16);
    lv_obj_t *b1 = flex_glass_button(box, "Vidrio", FLEX_GLASS_TINT_NONE, 150, 56);
    lv_obj_set_pos(b1, 40, 156);
    lv_obj_t *b2 = flex_glass_button(box, "Principal", FLEX_C_PRIMARY, 150, 56);
    lv_obj_set_pos(b2, 220, 156);
    lv_obj_t *sh = flex_glass_panel(box, FLEX_GLASS_SHEET, 40, 232, 330, 80, 24);
    lv_obj_t *t3 = watch_theme_label(sh, FLEX_TYPE_LABEL, FLEX_GLASS_TXT, "Lámina (avisos)");
    lv_obj_align(t3, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_t *sw = flex_glass_switch(box, 75, 35);
    lv_obj_set_pos(sw, 40, 336);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_t *ic = flex_glass_icon_button(box, FLEX_ICON_MOON_SOLID, 34, 64, FLEX_TINT_INDIGO);
    lv_obj_set_pos(ic, 150, 322);
    lv_obj_t *sl = flex_glass_slider(box, 236, 330, 134, 46, FLEX_C_WHITE);
    lv_slider_set_value(sl, 60, LV_ANIM_OFF);
    sim_run(200);
    sim_lint("componentes del motor");
    sim_save(out, "lg_componentes");
    lv_obj_add_state(b1, LV_STATE_PRESSED);
    sim_run(300);
    sim_save(out, "lg_componentes_pulsado");
    lv_obj_delete(box);
    sim_run(200);
    sim_check_residue("componentes borrados");
}

/* blur_backdrop de LVGL 9.5 (desenfoque real del fondo) con búferes
 * parciales: se compara redibujar una zona pequeña con redibujarlo todo. */
static void suite_native_blur(void)
{
    set_face(UI_FACE_MONTANA, FLEX_WALL_BLACK);
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, 30, 60);
    lv_obj_set_size(o, 350, 300);
    lv_obj_set_style_radius(o, 30, 0);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_20, 0);
    lv_obj_set_style_blur_radius(o, 24, 0);
    lv_obj_set_style_blur_backdrop(o, true, 0);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    static uint16_t full[SIM_W * SIM_H];
    memcpy(full, sim_fb, sizeof(full));
    /* Dos dibujos completos seguidos salen iguales (es determinista) */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    int same = 0;
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        same += full[i] != sim_fb[i];
    }
    /* Solo una franja pequeña dentro de la tarjeta (como un texto que cambia) */
    lv_area_t a = {150, 190, 260, 214};
    lv_obj_invalidate_area(lv_screen_active(), &a);
    lv_refr_now(NULL);
    int diff = 0, maxd = 0;
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        if (full[i] != sim_fb[i]) {
            const int d = abs((full[i] >> 11) - (sim_fb[i] >> 11)) + abs(((full[i] >> 5) & 63) - ((sim_fb[i] >> 5) & 63)) +
                          abs((full[i] & 31) - (sim_fb[i] & 31));
            diff++;
            maxd = d > maxd ? d : maxd;
        }
    }
    printf("  blur_backdrop de LVGL con franjas parciales: dos dibujos completos difieren en %d px; redibujar una "
           "zona de 110x25 px deja %d px distintos del dibujo completo (diferencia maxima %d)\n",
           same, diff, maxd);
    lv_obj_delete(o);
    sim_run(100);
}

void sim_suite_glass(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home_instant();
    sim_ws_reset(NULL);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 26, 10, 9, 30);
    sim_scenario(76, 4020, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    suite_profiles(out);
    suite_setting(out);
    suite_components(out);
    suite_native_blur();
    set_face(UI_FACE_DIGITAL, FLEX_WALL_BLACK);
    sim_ws_reset(NULL);
    ui_glass_apply();
    printf("vidrio: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
