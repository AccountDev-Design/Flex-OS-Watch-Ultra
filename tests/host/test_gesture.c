/*
 * Pruebas de PC del reconocedor de arrastres (B3-3): toque, eje, sentido,
 * decisión al soltar, velocidad, duración restante y curva de la animación.
 */
#include "test_util.h"
#include "ui_gesture.h"

/* Arrastre en línea recta de (x0,y0) a (x1,y1) en n pasos de dt ms. */
static ui_drag_result_t swipe(ui_drag_t *d, int x0, int y0, int x1, int y1, int n, int dt)
{
    uint32_t t = 1000;
    ui_drag_press(d, x0, y0, t);
    for (int i = 1; i <= n; i++) {
        t += (uint32_t)dt;
        ui_drag_move(d, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, t);
    }
    return ui_drag_release(d, x1, y1, t + (uint32_t)dt);
}

static void test_drag_basics(void)
{
    ui_drag_t d;
    ui_drag_init(&d, UI_AXIS_X, +1, 410);

    /* Toque: sin movimiento (o dentro del margen) no es un arrastre. */
    CHECK_EQ(swipe(&d, 5, 200, 5, 200, 3, 30), UI_DRAG_NONE);
    CHECK_EQ(swipe(&d, 5, 200, 5 + UI_DRAG_SLOP_PX - 1, 205, 3, 30), UI_DRAG_NONE);

    /* Vertical: se rechaza (la lista se desplaza en su lugar). */
    ui_drag_press(&d, 5, 200, 0);
    CHECK_EQ(ui_drag_move(&d, 8, 240, 30), UI_DRAG_REJECTED);
    CHECK_EQ(ui_drag_move(&d, 200, 240, 60), UI_DRAG_REJECTED); /* rechazado se queda rechazado */
    CHECK_EQ(ui_drag_release(&d, 300, 240, 90), UI_DRAG_NONE);

    /* Sentido contrario: se rechaza. */
    ui_drag_press(&d, 100, 200, 0);
    CHECK_EQ(ui_drag_move(&d, 80, 200, 30), UI_DRAG_REJECTED);

    /* Diagonal dominada por el eje: se acepta. */
    ui_drag_press(&d, 5, 200, 0);
    CHECK_EQ(ui_drag_move(&d, 25, 212, 30), UI_DRAG_ACTIVE);
    CHECK_EQ(d.travel, 20);

    /* El recorrido se limita a [0, span]. */
    ui_drag_move(&d, 900, 212, 60);
    CHECK_EQ(d.travel, 410);
    ui_drag_move(&d, -50, 212, 90);
    CHECK_EQ(d.travel, 0);

    /* Arrastre largo y lento: se completa por recorrido. */
    CHECK_EQ(swipe(&d, 5, 200, 5 + 410 * 40 / 100, 200, 30, 30), UI_DRAG_COMMIT);
    /* Corto y lento: se cancela. */
    CHECK_EQ(swipe(&d, 5, 200, 5 + 410 * 20 / 100, 200, 30, 30), UI_DRAG_CANCEL);
    /* Corto pero rápido (lanzamiento): se completa. */
    CHECK_EQ(swipe(&d, 5, 200, 5 + 410 * 15 / 100, 200, 4, 10), UI_DRAG_COMMIT);
    /* Rapidísimo pero diminuto (< 8 %): no se completa. */
    CHECK_EQ(swipe(&d, 5, 200, 5 + 410 * 5 / 100, 200, 1, 5), UI_DRAG_CANCEL);

    /* Arrastra lejos y vuelve deprisa: cancela aunque haya recorrido > 35 %. */
    uint32_t t = 0;
    ui_drag_press(&d, 5, 200, t);
    for (int x = 5; x <= 305; x += 30) {
        ui_drag_move(&d, x, 200, t += 30);
    }
    for (int x = 305; x >= 185; x -= 40) {
        ui_drag_move(&d, x, 200, t += 15);
    }
    CHECK(d.travel * 100 >= 410 * 35);
    CHECK(ui_drag_velocity(&d) < -UI_DRAG_BACKFLING_PX_S);
    CHECK_EQ(ui_drag_release(&d, 185, 200, t + 15), UI_DRAG_CANCEL);

    /* Eje Y hacia abajo (cortina del panel rápido) */
    ui_drag_t c;
    ui_drag_init(&c, UI_AXIS_Y, +1, 502);
    CHECK_EQ(swipe(&c, 205, 4, 205, 300, 20, 20), UI_DRAG_COMMIT);
    CHECK_EQ(swipe(&c, 205, 4, 320, 30, 5, 20), UI_DRAG_NONE); /* horizontal: rechazado */
    /* Eje Y hacia arriba (cerrar la cortina) */
    ui_drag_init(&c, UI_AXIS_Y, -1, 502);
    CHECK_EQ(swipe(&c, 205, 480, 205, 100, 20, 20), UI_DRAG_COMMIT);
    CHECK_EQ(swipe(&c, 205, 100, 205, 400, 20, 20), UI_DRAG_NONE); /* hacia abajo: rechazado */
}

static void test_velocity(void)
{
    ui_drag_t d;
    ui_drag_init(&d, UI_AXIS_X, +1, 410);
    /* 10 px cada 10 ms = 1000 px/s, con muestras muy seguidas */
    ui_drag_press(&d, 0, 0, 0);
    for (int i = 1; i <= 12; i++) {
        ui_drag_move(&d, 10 * i + 12, 0, (uint32_t)(10 * i));
    }
    const int32_t v = ui_drag_velocity(&d);
    CHECK(v >= 950 && v <= 1050);
    /* Mismo movimiento con muestras cada 33 ms (ritmo de lectura de LVGL) */
    ui_drag_press(&d, 0, 0, 0);
    for (int i = 1; i <= 6; i++) {
        ui_drag_move(&d, 33 * i + 12, 0, (uint32_t)(33 * i));
    }
    const int32_t v2 = ui_drag_velocity(&d);
    CHECK(v2 >= 950 && v2 <= 1050);
    /* El reloj de milisegundos da la vuelta a mitad del gesto */
    ui_drag_press(&d, 0, 0, 0xFFFFFFF0u);
    ui_drag_move(&d, 20, 0, 0xFFFFFFF0u + 20);
    ui_drag_move(&d, 40, 0, 0xFFFFFFF0u + 40);
    ui_drag_move(&d, 60, 0, 0xFFFFFFF0u + 60);
    const int32_t v3 = ui_drag_velocity(&d);
    CHECK(v3 >= 950 && v3 <= 1050);
    /* Sin muestras suficientes: 0 */
    ui_drag_press(&d, 0, 0, 0);
    CHECK_EQ(ui_drag_velocity(&d), 0);
}

static void test_commit_and_timing(void)
{
    CHECK(!ui_drag_should_commit(0, 410, 0));
    CHECK(!ui_drag_should_commit(143, 410, 0)); /* 34,9 % */
    CHECK(ui_drag_should_commit(144, 410, 0));  /* 35,1 % */
    CHECK(ui_drag_should_commit(40, 410, UI_DRAG_FLING_PX_S));
    CHECK(!ui_drag_should_commit(30, 410, UI_DRAG_FLING_PX_S)); /* 7,3 % */
    CHECK(!ui_drag_should_commit(400, 410, -UI_DRAG_BACKFLING_PX_S));
    CHECK(!ui_drag_should_commit(100, 0, 5000));

    CHECK_EQ(ui_drag_remaining_ms(0, 410, 190, true), 190);
    CHECK_EQ(ui_drag_remaining_ms(205, 410, 190, true), 95);
    CHECK_EQ(ui_drag_remaining_ms(205, 410, 210, false), 105);
    CHECK_EQ(ui_drag_remaining_ms(400, 410, 190, true), UI_ANIM_MIN_MS); /* nunca un salto */
    CHECK_EQ(ui_drag_remaining_ms(3, 410, 190, false), UI_ANIM_MIN_MS);
    CHECK_EQ(ui_drag_remaining_ms(999, 410, 190, false), 190); /* se limita al recorrido */
    CHECK_EQ(ui_drag_remaining_ms(10, 0, 190, true), UI_ANIM_MIN_MS);

    CHECK_EQ(ui_ease_out_cubic_permille(0), 0);
    CHECK_EQ(ui_ease_out_cubic_permille(1000), 1000);
    CHECK_EQ(ui_ease_out_cubic_permille(500), 875);
    CHECK_EQ(ui_ease_out_cubic_permille(-5), 0);
    CHECK_EQ(ui_ease_out_cubic_permille(2000), 1000);
    int prev = -1, mono = 1;
    for (int u = 0; u <= 1000; u += 10) {
        const int e = ui_ease_out_cubic_permille(u);
        if (e < prev) {
            mono = 0;
        }
        prev = e;
    }
    CHECK(mono);
}

void test_gesture_all(void)
{
    test_drag_basics();
    test_velocity();
    test_commit_and_timing();
}
