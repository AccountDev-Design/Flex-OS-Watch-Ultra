/*
 * Pruebas de la navegación (B3-3) en la vista previa, con el dedo simulado:
 * abrir y volver con sus tiempos, botón "volver", gesto desde el borde
 * (completo, cancelado, vertical y toque), doble toque, volver durante una
 * apertura, volver a la raíz, límite de la pila y fugas.
 */
#include <stdio.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"

static int s_created;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* Página de prueba: fondo de página y cabecera con "volver". */
static lv_obj_t *test_page(lv_obj_t *parent)
{
    lv_obj_t *p = ui_page_new(parent);
    watch_theme_page(p);
    char t[24];
    snprintf(t, sizeof(t), "Prueba %d", ++s_created);
    watch_theme_header(p, t, back_cb, NULL);
    return p;
}

static bool hidden(lv_obj_t *o)
{
    return lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Estado de reposo coherente: nada animándose, la de arriba visible en x = 0
 * y todas las de debajo ocultas. */
static void expect_rest(int depth, const char *when)
{
    EXPECT(!ui_nav_busy(), "%s: sigue ocupada", when);
    EXPECT(ui_nav_depth() == depth, "%s: profundidad %d (esperada %d)", when, ui_nav_depth(), depth);
    EXPECT(lv_anim_count_running() == 0, "%s: %u animaciones vivas", when, (unsigned)lv_anim_count_running());
    lv_obj_t *top = ui_nav_top();
    EXPECT(top && lv_obj_get_x(top) == 0 && !hidden(top), "%s: la pagina de arriba no esta en su sitio", when);
    lv_obj_t *scr = ui_nav_screen();
    int visible = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++) {
        visible += !hidden(lv_obj_get_child(scr, (int32_t)i));
    }
    EXPECT(visible == 1, "%s: %d paginas visibles", when, visible);
    EXPECT((int)lv_obj_get_child_count(scr) == depth, "%s: %u paginas en la pantalla", when,
           (unsigned)lv_obj_get_child_count(scr));
}

void sim_suite_nav(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1100);
    expect_rest(1, "inicio");
    lv_obj_t *face = ui_nav_top();
    const int timers0 = sim_timer_count();

    /* Volver en la raíz no hace nada. */
    EXPECT(!ui_nav_back(), "volver en la raiz devolvio true");

    /* ---- Abrir: 210 ms con paralaje ---- */
    EXPECT(ui_nav_push(ui_diag_create), "no se abrio diagnostico");
    lv_obj_t *diag = ui_nav_top();
    EXPECT(ui_nav_busy(), "abrir no anima");
    EXPECT(!ui_nav_push(test_page), "un segundo abrir durante la animacion no se ignoro");
    sim_run(100);
    const int32_t dx = lv_obj_get_x(diag), fx = lv_obj_get_x(face);
    EXPECT(dx > 0 && dx < FLEX_SCREEN_W, "a mitad de apertura diag x = %d", (int)dx);
    EXPECT(fx < 0 && fx > -FLEX_SCREEN_W * 30 / 100 - 1, "a mitad de apertura esfera x = %d (paralaje)", (int)fx);
    EXPECT(!hidden(face), "la esfera no se ve durante la apertura");
    sim_save(out, "f3_nav_apertura");
    sim_run(80); /* 180 ms */
    EXPECT(ui_nav_busy(), "la apertura termino antes de 210 ms");
    sim_run(80); /* 260 ms: 210 más hasta dos ciclos de 16 ms del temporizador de animaciones */
    expect_rest(2, "tras abrir");
    EXPECT(hidden(face), "la esfera no se oculto bajo la pagina");
    sim_run(600);
    sim_save(out, "f3_diagnostico");

    /* ---- Volver con el botón de la cabecera: 190 ms ---- */
    sim_tap(FLEX_HDR_X + FLEX_HDR_ZONE / 2, FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2);
    sim_run(100); /* 160 ms tras soltar */
    EXPECT(ui_nav_busy() && ui_nav_depth() == 2, "la vuelta termino antes de 190 ms");
    sim_run(100); /* 260 ms tras soltar */
    expect_rest(1, "tras volver con el boton");
    EXPECT(ui_nav_top() == face, "no se volvio a la esfera");
    EXPECT(sim_timer_count() == timers0, "el temporizador del diagnostico quedo vivo (%d -> %d)", timers0,
           sim_timer_count());

    /* ---- Gesto volver desde el borde: completo ---- */
    ui_nav_push(test_page);
    sim_run(300);
    lv_obj_t *page = ui_nav_top();
    sim_drag(4, 300, 200, 300, 10, 20); /* lento: 196 px en 220 ms */
    EXPECT(ui_nav_busy(), "el arrastre no mueve la pagina");
    const int32_t px = lv_obj_get_x(page);
    EXPECT(px > 170 && px <= 200, "la pagina no sigue al dedo (x = %d)", (int)px);
    EXPECT(!hidden(face), "la esfera no asoma durante el gesto");
    sim_save(out, "f3_nav_gesto");
    sim_release();
    sim_run(250);
    expect_rest(1, "tras el gesto completo");

    /* ---- Gesto corto y lento: se cancela ---- */
    ui_nav_push(test_page);
    sim_run(300);
    page = ui_nav_top();
    sim_drag(4, 300, 70, 300, 10, 30);
    sim_release();
    sim_run(250);
    expect_rest(2, "tras el gesto cancelado");
    EXPECT(ui_nav_top() == page, "el gesto cancelado cambio de pagina");

    /* ---- Gesto vertical desde el borde: no navega ---- */
    sim_drag(6, 200, 10, 420, 10, 20);
    sim_release();
    sim_run(250);
    expect_rest(2, "tras arrastre vertical en el borde");

    /* ---- Toque en el borde: nada ---- */
    sim_tap(6, 300);
    sim_run(250);
    expect_rest(2, "tras tocar el borde");

    /* ---- Volver durante una apertura: termina la apertura y vuelve ---- */
    ui_nav_push(test_page);
    sim_run(60);
    EXPECT(ui_nav_back(), "volver durante la apertura no se acepto");
    sim_run(250);
    expect_rest(2, "tras volver durante la apertura");

    /* ---- Volver a la raíz desde 3 niveles ---- */
    ui_nav_push(test_page);
    sim_run(300);
    ui_nav_push(test_page);
    sim_run(300);
    EXPECT(ui_nav_depth() == 4, "profundidad %d antes de ir a la raiz", ui_nav_depth());
    ui_nav_home();
    sim_run(250);
    expect_rest(1, "tras volver a la raiz");
    EXPECT(ui_nav_top() == face, "volver a la raiz no dejo la esfera");

    /* ---- Límite de la pila ---- */
    int pushed = 0;
    while (ui_nav_push(test_page)) {
        pushed++;
        sim_run(300);
        if (pushed > 10) {
            break;
        }
    }
    EXPECT(pushed == 5, "la pila admitio %d paginas sobre la raiz (esperadas 5)", pushed);
    ui_nav_home();
    sim_run(250);
    expect_rest(1, "tras vaciar la pila");

    /* ---- Fugas: abrir y volver 20 veces (botón y gesto) ---- */
    const uint32_t blocks = sim_mem_blocks();
    for (int i = 0; i < 20; i++) {
        ui_nav_push(i % 2 ? test_page : ui_diag_create);
        sim_run(300);
        if (i % 2) {
            sim_drag(4, 300, 260, 300, 8, 20);
            sim_release();
        } else {
            sim_tap(FLEX_HDR_X + FLEX_HDR_ZONE / 2, FLEX_SAFE_TOP + FLEX_HDR_ZONE / 2);
        }
        sim_run(300);
    }
    expect_rest(1, "tras 20 ciclos");
    EXPECT(sim_mem_blocks() == blocks, "fuga en la navegacion: %u -> %u bloques", (unsigned)blocks,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("navegacion: %u -> %u bloques de LVGL, %d temporizadores\n", (unsigned)blocks,
           (unsigned)sim_mem_blocks(), sim_timer_count());

    printf("navegacion B3-3: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
