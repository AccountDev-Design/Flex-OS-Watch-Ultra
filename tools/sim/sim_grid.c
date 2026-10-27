/*
 * Cajón de aplicaciones en panal infinito con lupa (rediseño 2026-10), en la
 * vista previa con el dedo simulado:
 *  - se abre deslizando hacia abajo desde el centro de la esfera (y hacia
 *    arriba desde el borde inferior), sobre el fondo de la esfera desenfocado
 *    (preparado ya en la esfera); con la esfera en negro, negro;
 *  - desplazamiento horizontal, vertical y diagonal, inercia y centrado;
 *    nunca dos copias de la misma app en pantalla ni apps que cambien de
 *    identidad; tras un periodo del toro (4 pasos) se ve lo mismo;
 *  - la lupa sigue al dedo: el icono pulsado crece y vuelve al soltar;
 *  - un toque abre la app pulsada (y no otra), un arrastre no abre nada;
 *  - en reposo no hay temporizador corriendo ni redibujos; sin fugas.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "flex_app_icons.h"
#include "sim.h"
#include "ui_grid_logic.h"
#include "ui_internal.h"
#include "watch_theme.h"

static bool unique_visible(const char *where)
{
    int apps[32];
    lv_obj_t *d = ui_nav_top();
    const int n = ui_apps_visible(d, apps, 32);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (apps[i] == apps[j]) {
                EXPECT(false, "%s: %s aparece dos veces", where, flex_app_name((flex_app_t)apps[i]));
                return false;
            }
        }
    }
    return true;
}

static lv_obj_t *center_icon(void)
{
    /* La imagen visible más cercana al centro de la pantalla. */
    lv_obj_t *best = NULL;
    int32_t bd = INT32_MAX;
    lv_obj_t *d = ui_nav_top();
    lv_obj_t *grid = lv_obj_get_child(d, 0);
    for (uint32_t i = 0; grid && i < lv_obj_get_child_count(grid); i++) {
        lv_obj_t *o = lv_obj_get_child(grid, (int32_t)i);
        if (!lv_obj_check_type(o, &lv_image_class) || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        const int32_t cx = lv_obj_get_x(o) + FLEX_APP_ICON_SIZE / 2 - FLEX_SCREEN_W / 2;
        const int32_t cy = lv_obj_get_y(o) + FLEX_APP_ICON_SIZE / 2 - FLEX_SCREEN_H / 2;
        if (cx * cx + cy * cy < bd) {
            bd = cx * cx + cy * cy;
            best = o;
        }
    }
    return best;
}

static void icon_center(lv_obj_t *o, int32_t *x, int32_t *y)
{
    *x = lv_obj_get_x(o) + FLEX_APP_ICON_SIZE / 2;
    *y = lv_obj_get_y(o) + FLEX_APP_ICON_SIZE / 2;
}

static void set_wall(flex_wall_t w)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.wallpaper = (uint8_t)w;
    ws.face = UI_FACE_DIGITAL;
    watch_settings_update(&ws);
    watch_theme_set_wallpaper(w);
    ui_nav_replace_root(ui_face_create);
}

void sim_suite_grid(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    sim_ws_reset(NULL);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 24, 13, 35, 0);
    sim_scenario(90, 4100, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    ui_apps_reset_view();
    set_wall(FLEX_WALL_AURORA);
    sim_run(600);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* ---- Abrir: hacia abajo desde el centro, siguiendo al dedo ---- */
    sim_drag(205, 150, 206, 330, 8, 16);
    EXPECT(ui_nav_busy() && ui_nav_depth() == 2, "el cajon no baja siguiendo al dedo");
    lv_obj_t *drawer = ui_nav_top();
    /* Transición continua: la página (y con ella cualquier fondo) no se
     * mueve; solo la rejilla sigue al dedo, sobre un velo a medias encima de
     * la esfera, que sigue quieta y visible. */
    lv_obj_t *grid = drawer ? lv_obj_get_child(drawer, 0) : NULL;
    EXPECT(grid && lv_obj_get_y(grid) > -FLEX_SCREEN_H + 120 && lv_obj_get_y(grid) < -FLEX_SCREEN_H + 230,
           "rejilla en y = %d durante el arrastre", grid ? (int)lv_obj_get_y(grid) : 0);
    EXPECT(drawer && lv_obj_get_y(drawer) == 0 && lv_obj_get_x(drawer) == 0, "la pagina del cajon se desplaza entera");
    const lv_opa_t veil = drawer ? lv_obj_get_style_bg_opa(drawer, 0) : 0;
    EXPECT(veil > LV_OPA_10 && veil < LV_OPA_80, "velo del cajon a %d durante el arrastre", (int)veil);
    EXPECT(!lv_obj_has_flag(ui_nav_page(0), LV_OBJ_FLAG_HIDDEN) && lv_obj_get_y(ui_nav_page(0)) == 0,
           "la esfera no se queda quieta y visible bajo el cajon");
    sim_save(out, "rd_cajon_abriendo");
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 2 && !ui_nav_busy() && ui_apps_find(ui_nav_top(), "Reloj"),
           "deslizar hacia abajo desde el centro no abrio el cajon");
    drawer = ui_nav_top();
    EXPECT(lv_obj_get_style_bg_opa(drawer, 0) == LV_OPA_COVER && lv_obj_get_y(lv_obj_get_child(drawer, 0)) == 0,
           "el cajon abierto no queda opaco y en su sitio");
    EXPECT(lv_obj_has_flag(ui_nav_page(0), LV_OBJ_FLAG_HIDDEN), "la esfera se sigue dibujando bajo el cajon");
    for (uint32_t k = 0; k < lv_obj_get_child_count(drawer); k++) {
        EXPECT(!lv_obj_check_type(lv_obj_get_child(drawer, (int32_t)k), &lv_image_class),
               "el cajon lleva una imagen de fondo");
    }
    EXPECT(!ui_apps_moving(drawer), "la rejilla se mueve en reposo");
    lv_obj_t *c = center_icon();
    EXPECT(c && lv_image_get_scale(c) == 256, "el icono central no esta a su tamano nativo (%d)",
           c ? (int)lv_image_get_scale(c) : -1);
    EXPECT(ui_apps_find(drawer, "Reloj") == c, "Reloj no esta en el centro al abrir");
    int apps[32];
    const int nvis = ui_apps_visible(drawer, apps, 32);
    EXPECT(nvis >= 12 && nvis <= GRID_MAX_ITEMS, "%d apps visibles en reposo", nvis);
    unique_visible("reposo");
    sim_lint("cajon en reposo");
    sim_check_residue("cajon en reposo");
    for (int k = 0; k < 6; k++) { /* centrar otras apps (accesibilidad) no deja restos */
        static const char *const names[] = {"Ajustes", "Paint", "Clima", "Notas", "Galería", "Reloj"};
        ui_apps_find(drawer, names[k]);
        sim_run(100);
        char w[48];
        snprintf(w, sizeof(w), "cajon centrado en %s", names[k]);
        sim_check_residue(w);
    }
    sim_save(out, "rd_cajon");

    /* En reposo no se redibuja nada */
    sim_run(300);
    const uint64_t px0 = sim_flush_pixels();
    sim_run(2000);
    EXPECT(sim_flush_pixels() == px0, "el cajon redibuja en reposo (%llu px)",
           (unsigned long long)(sim_flush_pixels() - px0));

    /* ---- Lupa que sigue al dedo: pulsar un icono del borde lo agranda ---- */
    lv_obj_t *cal = ui_apps_find(drawer, "Flex Phone"); /* en el primer anillo: se ve entera */
    int32_t x, y;
    icon_center(cal, &x, &y);
    const int32_t sc0 = lv_image_get_scale(cal);
    sim_press(x, y);
    sim_run(300);
    EXPECT(lv_image_get_scale(cal) >= sc0 + 25 && lv_image_get_scale(cal) >= 250, "la lupa no sigue al dedo (%d -> %d)", (int)sc0,
           (int)lv_image_get_scale(cal));
    EXPECT(lv_image_get_scale(c) < 256, "el icono central no encoge al alejarse la lupa");
    sim_save(out, "rd_cajon_lupa");
    /* soltar sin moverse abre ESA app */
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 3 && sim_find_label(ui_nav_top(), "Flex Phone"), "el toque no abrio Flex Phone");
    ui_nav_back();
    sim_run(500);
    drawer = ui_nav_top();

    /* ---- Arrastres: diagonal, vertical y horizontal; nunca abren nada ---- */
    const int depth0 = ui_nav_depth();
    sim_press(150, 330);
    sim_run(30);
    for (int i = 1; i <= 12; i++) {
        sim_press(150 + i * 12, 330 - i * 12);
        sim_run(16);
        char w[48];
        snprintf(w, sizeof(w), "arrastre diagonal %d", i);
        unique_visible(w);
    }
    sim_save(out, "rd_cajon_arrastre");
    sim_release();
    sim_run(60);
    EXPECT(ui_apps_moving(drawer), "sin inercia al soltar un arrastre rapido");
    for (int i = 0; i < 40; i++) {
        sim_run(25);
        unique_visible("inercia");
    }
    sim_run(1500);
    EXPECT(ui_nav_depth() == depth0, "un arrastre abrio una app");
    EXPECT(!ui_apps_moving(drawer), "la rejilla no se detuvo tras la inercia");
    c = center_icon();
    EXPECT(c && lv_image_get_scale(c) == 256 && lv_obj_get_x(c) == FLEX_SCREEN_W / 2 - FLEX_APP_ICON_SIZE / 2,
           "al detenerse no queda una celda centrada");
    sim_check_residue("cajon tras la inercia");

    /* ---- Infinito: un periodo del toro (4 pasos en horizontal) vuelve a lo mismo ---- */
    const void *src_before = c ? lv_image_get_src(c) : NULL;
    const int32_t step = (int32_t)(GRID_PITCH * GRID_TORUS); /* 432 px */
    for (int k = 0; k < 2; k++) { /* dos arrastres lentos de medio periodo */
        sim_press(100, 251);
        sim_run(30);
        for (int i = 1; i <= 18; i++) {
            sim_press(100 + i * step / 2 / 18, 251);
            sim_run(40); /* lento: sin inercia */
            unique_visible("periodo");
        }
        sim_run(120);
        sim_release();
        sim_run(900);
    }
    c = center_icon();
    EXPECT(c && lv_image_get_src(c) == src_before, "tras un periodo completo no se ve la misma app en el centro");
    /* Vertical y diagonal largos: siempre sin duplicados */
    for (int k = 0; k < 6; k++) {
        sim_press(205, 120);
        sim_run(20);
        for (int i = 1; i <= 10; i++) {
            sim_press(205 + (k & 1 ? i * 9 : 0), 120 + i * 26);
            sim_run(12);
            unique_visible("vertical");
        }
        sim_release();
        for (int i = 0; i < 20; i++) {
            sim_run(40);
            unique_visible("vertical, inercia");
        }
    }
    sim_run(1500);
    EXPECT(!ui_apps_moving(drawer), "la rejilla no se detiene");
    EXPECT(ui_nav_depth() == depth0, "los arrastres abrieron una app");

    /* ---- Un toque en una app que se ve en el borde, aunque crezca, abre esa ---- */
    lv_obj_t *aj = ui_apps_find(drawer, "Ajustes");
    icon_center(aj, &x, &y);
    sim_tap(x + 6, y - 4);
    sim_run(500);
    EXPECT(ui_nav_depth() == depth0 + 1 && sim_find_label(ui_nav_top(), "Ajustes"),
           "el toque no abrio Ajustes");
    ui_nav_back();
    sim_run(500);
    /* Pulsar entre iconos no abre nada */
    sim_tap(205 + (int32_t)(GRID_PITCH / 2), 251);
    sim_run(400);
    EXPECT(ui_nav_depth() == depth0, "un toque entre iconos abrio algo");

    /* ---- Volver a la esfera: hacia arriba desde el borde inferior ---- */
    sim_drag(205, FLEX_SCREEN_H - 12, 205, 120, 10, 16);
    sim_release();
    sim_run(600);
    EXPECT(ui_nav_depth() == 1, "hacia arriba desde el borde inferior no volvio a la esfera");
    /* y otra vez desde el borde inferior de la esfera (gesto de F11) */
    sim_drag(205, FLEX_SCREEN_H - 20, 207, 120, 8, 16);
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 2 && ui_apps_find(ui_nav_top(), "Reloj"), "desde el borde inferior no abre el cajon");
    ui_nav_home();
    sim_run(500);
    /* Un arrastre corto hacia abajo se cancela (vuelve arriba y se borra) */
    sim_drag(205, 200, 205, 240, 4, 40);
    sim_release();
    sim_run(600);
    EXPECT(ui_nav_depth() == 1 && !ui_nav_busy(), "un arrastre corto hacia abajo no se cancelo");
    /* Hacia arriba desde el centro y lateral: nada */
    sim_drag(205, 330, 205, 120, 8, 16);
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 1, "hacia arriba desde el centro abrio algo");
    sim_drag(60, 250, 360, 260, 8, 16);
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 1, "un arrastre lateral en la esfera abrio algo");
    /* Mover el dedo cancela la pulsación larga del selector */
    sim_press(120, 250);
    sim_run(100);
    sim_press(160, 250);
    sim_run(900);
    sim_release();
    sim_run(500);
    EXPECT(ui_nav_depth() == 1, "la pulsacion larga con el dedo en movimiento abrio el selector");

    /* ---- Fondo negro AMOLED: el cajón va sobre negro ---- */
    set_wall(FLEX_WALL_BLACK);
    sim_run(400);
    EXPECT(!watch_theme_has_background(), "con el negro queda un fondo");
    ui_nav_drag_open_begin_dir(ui_apps_create, true);
    ui_nav_drag_open_end(true, FLEX_SCREEN_H);
    sim_run(500);
    EXPECT(ui_nav_depth() == 2 && lv_obj_get_style_bg_opa(ui_nav_top(), 0) == LV_OPA_COVER,
           "con fondo negro el cajon no se abre");
    sim_save(out, "rd_cajon_negro");
    ui_nav_home();
    sim_run(500);

    /* ---- Sin fugas ni temporizadores ---- */
    set_wall(FLEX_WALL_AURORA);
    sim_run(1500);
    for (int k = 0; k < 4; k++) {
        sim_drag(205, 150, 206, 400, 8, 16);
        sim_release();
        sim_run(400);
        sim_drag(150, 300, 300, 160, 8, 12);
        sim_release();
        sim_run(1200);
        ui_nav_home();
        sim_run(500);
    }
    EXPECT(sim_timer_count() == timers0, "temporizadores: %d -> %d", timers0, sim_timer_count());
    EXPECT(sim_mem_blocks() == blocks0, "bloques de memoria: %u -> %u", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    set_wall(FLEX_WALL_BLACK);
    sim_run(300);
    ui_apps_reset_view();
    printf("rejilla: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
