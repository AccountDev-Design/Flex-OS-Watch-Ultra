/*
 * Rastros de las superficies de vidrio en movimiento (Flex Liquid Glass Engine).
 *
 * Reproduce en el PC las condiciones de dibujo del reloj (watch_display.c):
 * búferes PARCIALES dobles de pocas líneas y el redondeo de áreas del CO5300
 * (inicio par, final impar). Mueve superficies translúcidas sobre fondos
 * complejos (fondo de pantalla, esfera fotográfica) y, después de CADA cuadro,
 * compara lo que hay en la pantalla con un redibujado completo: cualquier
 * píxel que quede de una posición anterior es un rastro.
 *
 * Se prueba con varias alturas de franja porque los límites entre franjas
 * cambian dónde empieza cada trozo de dibujo.
 *
 * Qué es un rastro: un píxel que el dibujo por partes NO actualizó (sigue
 * igual que en el cuadro anterior) cuando un redibujado completo dice que
 * debería haber cambiado. Una imagen escalada (el zoom del selector) se
 * remuestrea desde el borde de cada franja, y con franjas distintas algún
 * píxel sale con otro valor: eso no es un rastro (no es la imagen vieja), se
 * cuenta aparte y solo se informa.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flex_glass.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

#define TOL 6 /* el mismo margen que sim_check_residue (escalado de iconos por franjas) */

static uint16_t s_before[SIM_W * SIM_H];
static uint16_t s_last[SIM_W * SIM_H]; /* la pantalla tras la comprobación anterior (redibujado completo) */
static uint16_t *s_buf1, *s_buf2;
static int s_frames, s_bad_frames, s_bad_px, s_resample_px;
static char s_first[160];

/* Mismo redondeo que watch_display.c (rounder_cb). */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static void hw_begin(int lines)
{
    lv_display_t *d = lv_display_get_default();
    const size_t bytes = (size_t)SIM_W * (size_t)lines * sizeof(uint16_t);
    s_buf1 = malloc(bytes);
    s_buf2 = malloc(bytes);
    lv_refr_now(d);
    lv_display_set_buffers(d, s_buf1, s_buf2, (uint32_t)bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(d, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(d);
    memcpy(s_last, sim_fb, sizeof(s_last));
}

static void hw_end(void)
{
    static uint16_t buf[SIM_W * 64];
    lv_display_t *d = lv_display_get_default();
    lv_refr_now(d);
    lv_display_remove_event_cb_with_user_data(d, rounder_cb, NULL);
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    free(s_buf1);
    free(s_buf2);
    s_buf1 = s_buf2 = NULL;
}

/* Lo que se ve ahora frente a un redibujado completo; el redibujado deja la
 * pantalla limpia para el cuadro siguiente. */
static void frame_check(const char *where)
{
    lv_refr_now(NULL);
    memcpy(s_before, sim_fb, sizeof(s_before));
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
    int bad = 0, x1 = SIM_W, y1 = SIM_H, x2 = -1, y2 = -1;
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        const uint16_t a = s_before[i], b = sim_fb[i];
        if (a == b) {
            continue;
        }
        const int dd = abs((a >> 11) - (b >> 11)) + abs(((a >> 5) & 63) - ((b >> 5) & 63)) + abs((a & 31) - (b & 31));
        if (dd <= TOL) {
            continue;
        }
        if (a != s_last[i]) {
            s_resample_px++; /* el píxel sí se redibujó, con otro muestreo */
            continue;
        }
        bad++;
        const int x = i % SIM_W, y = i / SIM_W;
        x1 = LV_MIN(x1, x);
        y1 = LV_MIN(y1, y);
        x2 = LV_MAX(x2, x);
        y2 = LV_MAX(y2, y);
    }
    memcpy(s_last, sim_fb, sizeof(s_last));
    s_frames++;
    if (bad) {
        if (!s_bad_frames) {
            snprintf(s_first, sizeof(s_first), "%s: %d px en (%d,%d)-(%d,%d)", where, bad, x1, y1, x2, y2);
        }
        s_bad_frames++;
        s_bad_px += bad;
    }
}

/* Avanza ms comprobando cada cuadro (LVGL redibuja cada 16 ms). */
static void run_checked(uint32_t ms, const char *where)
{
    for (uint32_t t = 0; t < ms; t += 16) {
        sim_run(16);
        frame_check(where);
    }
}

static void drag_checked(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int n, const char *where)
{
    sim_press(x0, y0);
    sim_run(16);
    for (int i = 1; i <= n; i++) {
        sim_press(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
        sim_run(16);
        frame_check(where);
    }
}

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

/* Componentes del motor moviéndose solos sobre la esfera: tarjeta, botón,
 * superficie y una lámina translúcida, por pasos de distinto tamaño, y
 * después ocultos y borrados. */
static void engine_components(const char *where)
{
    lv_obj_t *root = ui_nav_page(0);
    lv_obj_t *card = flex_glass_create(root, FLEX_GLASS_CARD);
    lv_obj_set_size(card, 180, 110);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_t *btn = flex_glass_create(root, FLEX_GLASS_CONTROL);
    flex_glass_interactive(btn);
    lv_obj_set_size(btn, 92, 92);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *sheet = flex_glass_create(root, FLEX_GLASS_SHEET);
    lv_obj_set_size(sheet, 300, 140);
    lv_obj_set_style_radius(sheet, 34, 0);
    lv_obj_t *t = watch_theme_label(sheet, FLEX_FONT_BODY, FLEX_C_TXT, "Vidrio");
    lv_obj_center(t);
    for (int i = 0; i <= 40; i++) {
        lv_obj_set_pos(card, 10 + i * 5, 40 + i * 7);
        lv_obj_set_pos(btn, 300 - i * 7, 20 + i * 9);
        lv_obj_set_pos(sheet, 50 + (i % 7) * 3 - i, 300 - i * 6);
        if (i == 20) {
            lv_obj_add_state(btn, LV_STATE_PRESSED);
        }
        if (i == 26) {
            lv_obj_remove_state(btn, LV_STATE_PRESSED);
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        sim_run(16);
        frame_check(where);
    }
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    run_checked(48, where);
    lv_obj_delete(btn);
    run_checked(48, where);
    lv_obj_delete(card);
    lv_obj_delete(sheet);
    run_checked(48, where);
}

static void scenarios(const char *tag)
{
    char w[96];
    /* Panel rápido: abrir siguiendo al dedo, soltar, cerrar arrastrando */
    snprintf(w, sizeof(w), "%s: panel rapido", tag);
    drag_checked(205, 20, 205, 300, 20, w);
    sim_release();
    run_checked(400, w);
    drag_checked(205, 470, 205, 120, 18, w);
    sim_release();
    run_checked(400, w);
    EXPECT(!ui_qp_active(), "%s: el panel no se cerro", w);
    /* Panel abierto y cerrado con su animación */
    ui_qp_open();
    run_checked(320, w);
    ui_qp_close();
    run_checked(320, w);

    /* Aviso emergente sobre la esfera (lv_layer_top) */
    snprintf(w, sizeof(w), "%s: aviso", tag);
    ui_banner_show_info("Vidrio", "Sobre un fondo complejo");
    run_checked(400, w);
    sim_tap(205, 60);
    run_checked(400, w);

    /* Cajón: sube desde el borde inferior siguiendo al dedo y vuelve */
    snprintf(w, sizeof(w), "%s: cajon", tag);
    drag_checked(205, 495, 205, 200, 16, w);
    sim_release();
    run_checked(400, w);
    drag_checked(205, 498, 205, 150, 16, w);
    sim_release();
    run_checked(400, w);
    ui_nav_home_instant();
    run_checked(64, w);

    /* Componentes del motor */
    snprintf(w, sizeof(w), "%s: componentes", tag);
    engine_components(w);

    /* App Reloj: el resaltado de la pestaña se desliza */
    snprintf(w, sizeof(w), "%s: pestanas del Reloj", tag);
    ui_nav_push(ui_reloj_create);
    run_checked(300, w);
    lv_obj_t *page = ui_nav_top();
    for (int tab = 1; tab < UI_RELOJ_TABS; tab++) {
        sim_tap(82 + tab * 81, 63);
        run_checked(260, w);
    }
    (void)page;
    ui_nav_home_instant();
    run_checked(64, w);

    /* Ajustes: entra deslizando, la lista se desplaza con el dedo y vuelve */
    snprintf(w, sizeof(w), "%s: ajustes", tag);
    ui_nav_push(ui_settings_create);
    run_checked(300, w);
    drag_checked(205, 420, 205, 160, 14, w);
    sim_release();
    run_checked(600, w);
    drag_checked(405, 300, 120, 300, 14, w); /* borde derecho: atrás siguiendo al dedo */
    sim_release();
    run_checked(400, w);

    /* Tarjetas de Bienestar desde el borde derecho de la esfera */
    snprintf(w, sizeof(w), "%s: bienestar", tag);
    drag_checked(404, 250, 150, 250, 12, w);
    sim_release();
    run_checked(400, w);
    ui_nav_home_instant();
    run_checked(64, w);

    /* Selector de esferas: zoom al mantener pulsada la esfera, carrusel y salida */
    snprintf(w, sizeof(w), "%s: selector de esferas", tag);
    sim_press(205, 250);
    run_checked(900, w);
    sim_release();
    run_checked(500, w);
    drag_checked(300, 250, 100, 250, 10, w);
    sim_release();
    run_checked(500, w);
    ui_nav_home_instant();
    run_checked(200, w);
}

void sim_suite_ghost(const char *out)
{
    (void)out;
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home_instant();
    sim_ws_reset(NULL);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 26, 10, 9, 30);
    sim_scenario(76, 4020, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    static const int kLines[] = {54, 40, 16};
    static const struct {
        int face;
        flex_wall_t wall;
        const char *name;
    } kBg[] = {
        {UI_FACE_DIGITAL, FLEX_WALL_AURORA, "fondo Aurora"},
        {UI_FACE_MONTANA, FLEX_WALL_BLACK, "esfera Montana"},
    };
    for (size_t b = 0; b < sizeof(kBg) / sizeof(kBg[0]); b++) {
        set_face(kBg[b].face, kBg[b].wall);
        for (size_t l = 0; l < sizeof(kLines) / sizeof(kLines[0]); l++) {
            char tag[64];
            snprintf(tag, sizeof(tag), "%s, franjas de %d lineas", kBg[b].name, kLines[l]);
            s_frames = s_bad_frames = s_bad_px = s_resample_px = 0;
            s_first[0] = 0;
            hw_begin(kLines[l]);
            scenarios(tag);
            hw_end();
            printf("  %s: %d cuadros comparados, %d con rastros (%d px de remuestreo de imagenes escaladas)\n", tag,
                   s_frames, s_bad_frames, s_resample_px);
            EXPECT(s_bad_frames == 0, "rastros: %d cuadros (%d px); el primero en %s", s_bad_frames, s_bad_px,
                   s_first);
        }
    }
    set_face(UI_FACE_DIGITAL, FLEX_WALL_BLACK);
    sim_ws_reset(NULL);
    printf("rastros: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
