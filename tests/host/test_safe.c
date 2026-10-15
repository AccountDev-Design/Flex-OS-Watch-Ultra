/*
 * Pruebas de PC del área visible con esquinas redondeadas (flex_safe).
 */
#include <math.h>

#include "flex_safe.h"
#include "flex_tokens.h"
#include "test_util.h"

static void test_inset(void)
{
    int below = 0, above1 = 0, mono = 1;
    int32_t prev = 1 << 30;
    for (int32_t dy = 0; dy <= FLEX_CORNER_R + 5; dy++) {
        const int32_t got = flex_corner_inset(dy);
        double exact = 0;
        if (dy < FLEX_CORNER_R) {
            const double d = FLEX_CORNER_R - (dy + 0.5);
            exact = FLEX_CORNER_R - sqrt((double)FLEX_CORNER_R * FLEX_CORNER_R - d * d);
        }
        if (got < exact - 1e-9) {
            below++; /* nunca prometer un píxel que no se ve */
        }
        if (got > ceil(exact) + 1) {
            above1++; /* ni quitar más de un píxel de más */
        }
        if (got > prev) {
            mono = 0;
        }
        prev = got;
    }
    CHECK_EQ(below, 0);
    CHECK_EQ(above1, 0);
    CHECK(mono);
    CHECK_EQ(flex_corner_inset(FLEX_CORNER_R), 0);
    CHECK_EQ(flex_corner_inset(500), 0);
    CHECK(flex_corner_inset(0) > FLEX_CORNER_R / 2);
    CHECK_EQ(flex_corner_inset(-1), FLEX_PANEL_W);
}

static void test_bands(void)
{
    /* Una banda en el centro vertical no tiene esquina. */
    CHECK_EQ(flex_safe_left(200, 300, 4), 4);
    CHECK_EQ(flex_safe_right(200, 300, 4), FLEX_PANEL_W - 1 - 4);
    /* Arriba y abajo, simétrico. */
    CHECK_EQ(flex_safe_left(20, 40, 0), flex_safe_left(FLEX_PANEL_H - 41, FLEX_PANEL_H - 21, 0));
    CHECK_EQ(flex_safe_left(20, 40, 0) + flex_safe_right(20, 40, 0), FLEX_PANEL_W - 1);
    /* Manda la fila más cercana al borde. */
    CHECK_EQ(flex_safe_left(20, 480, 0), flex_safe_left(20, 20, 0));
    CHECK_EQ(flex_safe_left(400, 480, 0), flex_corner_inset(FLEX_PANEL_H - 1 - 480));

    /* El icono de batería del panel rápido de B3-4 (x 351..388, y 32..49)
     * NO cabía: es lo que se vio cortado en la foto del reloj. */
    CHECK(!flex_area_visible(351, 32, 388, 49, 0));
    /* El centro de la pantalla siempre se ve. */
    CHECK(flex_area_visible(100, 100, 300, 400, FLEX_SAFE_MARGIN));
    /* Fuera del panel, no. */
    CHECK(!flex_area_visible(-1, 200, 10, 210, 0));
    CHECK(!flex_area_visible(400, 200, 410, 210, 0));
    CHECK(!flex_area_visible(200, 495, 210, 502, 0));
    /* Una esquina exacta del rectángulo nunca se ve con radio > 0. */
    CHECK(!flex_area_visible(0, 0, 0, 0, 0));
    CHECK(!flex_area_visible(409, 501, 409, 501, 0));
}

/* Las constantes de diseño que dependen de la esquina siguen cuadrando con
 * ella si cambia FLEX_CORNER_R tras la prueba T44. */
static void test_tokens(void)
{
    CHECK_EQ(FLEX_PANEL_W, FLEX_SCREEN_W);
    CHECK_EQ(FLEX_PANEL_H, FLEX_SCREEN_H);
    const int32_t last = FLEX_SCREEN_H - 1 - FLEX_LIST_BOTTOM; /* última fila visible de la lista */
    /* El lateral de una fila (x = FLEX_LIST_X) en la última fila visible: la
     * esquina no lo muerde (1 px de suavizado, como la vista previa)... */
    CHECK(flex_safe_left(last, last, 0) - 1 <= FLEX_LIST_X);
    CHECK(flex_safe_right(last, last, 0) + 1 >= FLEX_SCREEN_W - 1 - FLEX_LIST_X);
    /* ...y el hueco no sobra: 4 px más abajo ya mordería. */
    CHECK(flex_safe_left(last + 4, last + 4, 0) - 1 > FLEX_LIST_X);
    /* Contenido de las filas: lista + relleno de la fila (16 / 14). */
    const int32_t x1 = FLEX_LIST_X + 16, x2 = FLEX_SCREEN_W - 1 - FLEX_LIST_X - 14;
    CHECK(flex_area_visible(x1, last, x2, last, 2));
    /* El borde superior de la lista (bajo la cabecera) tampoco corta. */
    CHECK(flex_area_visible(FLEX_LIST_X, FLEX_HDR_H, FLEX_SCREEN_W - 1 - FLEX_LIST_X, FLEX_HDR_H, 1));
}

void test_safe_all(void)
{
    test_inset();
    test_bands();
    test_tokens();
}
