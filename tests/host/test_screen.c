/*
 * Pruebas de PC del apagado de pantalla y del botón lateral (F4.5).
 */
#include "test_util.h"
#include "ui_screen_logic.h"

static void test_policy(void)
{
    /* 15 s: atenuada desde 12 s, apagada a los 15 s */
    CHECK_EQ(scr_dim_ms(15000), 3000);
    CHECK_EQ(scr_dim_ms(5000), 1666);
    CHECK_EQ(scr_next(SCR_ON, 0, 15000, false), SCR_ON);
    CHECK_EQ(scr_next(SCR_ON, 11999, 15000, false), SCR_ON);
    CHECK_EQ(scr_next(SCR_ON, 12000, 15000, false), SCR_DIM);
    CHECK_EQ(scr_next(SCR_DIM, 14999, 15000, false), SCR_DIM);
    CHECK_EQ(scr_next(SCR_DIM, 15000, 15000, false), SCR_OFF);
    /* actividad durante la atenuación: vuelve a encendida */
    CHECK_EQ(scr_next(SCR_DIM, 100, 15000, false), SCR_ON);
    /* apagada no se enciende sola, aunque la inactividad sea 0 */
    CHECK_EQ(scr_next(SCR_OFF, 0, 15000, false), SCR_OFF);
    /* una retención la mantiene (o la vuelve) encendida */
    CHECK_EQ(scr_next(SCR_ON, 60000, 15000, true), SCR_ON);
    CHECK_EQ(scr_next(SCR_OFF, 60000, 15000, true), SCR_ON);
    /* brillo atenuado */
    CHECK_EQ(scr_dim_level(70), 23);
    CHECK_EQ(scr_dim_level(10), 5);
    CHECK_EQ(scr_dim_level(3), 3);
}

/* Lee niveles a 10 ms y cuenta pulsaciones cortas. */
static int run(btn_t *b, uint32_t *t, int level, uint32_t ms)
{
    int n = 0;
    for (uint32_t k = 0; k < ms; k += 10) {
        n += btn_update(b, level, *t);
        *t += 10;
    }
    return n;
}

static void test_button(void)
{
    btn_t b;
    uint32_t t = 0;
    /* reposo en 0 (polaridad desconocida a priori) */
    btn_init(&b);
    CHECK_EQ(run(&b, &t, 1, 800), 0);  /* pulsado al encender: no cuenta ni se aprende */
    CHECK_EQ(b.idle, -1);
    CHECK_EQ(run(&b, &t, 0, 2000), 0); /* reposo estable tras 1,5 s */
    CHECK_EQ(b.idle, 0);
    CHECK_EQ(run(&b, &t, 1, 200), 0);  /* pulsación corta */
    CHECK_EQ(run(&b, &t, 0, 100), 1);  /* cuenta al soltar */
    CHECK_EQ(run(&b, &t, 1, 3000), 0); /* larga: no cuenta */
    CHECK_EQ(run(&b, &t, 0, 100), 0);
    /* rebote de 10 ms: no cuenta */
    CHECK_EQ(run(&b, &t, 1, 10), 0);
    CHECK_EQ(run(&b, &t, 0, 100), 0);

    /* reposo en 1 (polaridad contraria): igual de bien */
    btn_init(&b);
    t = 0;
    CHECK_EQ(run(&b, &t, 1, 2500), 0);
    CHECK_EQ(b.idle, 1);
    CHECK_EQ(run(&b, &t, 0, 150), 0);
    CHECK_EQ(run(&b, &t, 1, 50), 1);
}

/* AOD, atenuación progresiva y Pixel Shift */
static void test_aod_shift(void)
{
    /* Con AOD, al agotarse el tiempo pasa a SCR_AOD (no se apaga) */
    CHECK_EQ(scr_next_ex(SCR_DIM, 15000, 15000, false, true, true), SCR_AOD);
    CHECK_EQ(scr_next_ex(SCR_DIM, 15000, 15000, false, false, true), SCR_OFF);
    /* en AOD solo un evento la enciende (como apagada) */
    CHECK_EQ(scr_next_ex(SCR_AOD, 0, 15000, false, true, true), SCR_AOD);
    CHECK_EQ(scr_next_ex(SCR_AOD, 0, 15000, true, true, true), SCR_ON);
    /* sin atenuación: de encendida al final directamente */
    CHECK_EQ(scr_next_ex(SCR_ON, 13000, 15000, false, false, false), SCR_ON);
    CHECK_EQ(scr_next_ex(SCR_ON, 15000, 15000, false, false, false), SCR_OFF);
    /* scr_next conserva su comportamiento */
    CHECK_EQ(scr_next(SCR_ON, 12000, 15000, false), SCR_DIM);

    /* Rampa: de 70 a 23 en 3 s, lineal, nunca por debajo del objetivo */
    CHECK_EQ(scr_dim_ramp(70, 23, 0, 3000), 70);
    CHECK_EQ(scr_dim_ramp(70, 23, 1500, 3000), 47);
    CHECK_EQ(scr_dim_ramp(70, 23, 3000, 3000), 23);
    CHECK_EQ(scr_dim_ramp(70, 23, 9000, 3000), 23);
    CHECK_EQ(scr_dim_ramp(20, 30, 100, 3000), 20); /* el objetivo nunca sube el brillo */
    for (uint32_t e = 0; e <= 3000; e += 100) {
        CHECK(scr_dim_ramp(70, 23, e, 3000) >= scr_dim_ramp(70, 23, e + 100, 3000));
    }

    /* Pixel Shift: paso 0 en su sitio, dentro de ±max, ciclo de 16 pasos y
     * dos pasos seguidos nunca en la misma posición. */
    int dx, dy, px = 99, py = 99;
    scr_pixel_shift(0, 8, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    int sum_x = 0, sum_y = 0;
    for (uint32_t k = 0; k < 64; k++) {
        scr_pixel_shift(k, 8, &dx, &dy);
        CHECK(dx >= -8 && dx <= 8 && dy >= -8 && dy <= 8);
        CHECK(dx != px || dy != py);
        int qx, qy;
        scr_pixel_shift(k + SCR_SHIFT_STEPS, 8, &qx, &qy);
        CHECK(qx == dx && qy == dy);
        if (k < SCR_SHIFT_STEPS) {
            sum_x += dx;
            sum_y += dy;
        }
        px = dx;
        py = dy;
    }
    /* repartido: en un ciclo, el centro de masas queda cerca del origen */
    CHECK(sum_x >= -16 && sum_x <= 16 && sum_y >= -16 && sum_y <= 16);
    scr_pixel_shift(5, 0, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
}

void test_screen_all(void)
{
    test_policy();
    test_button();
    test_aod_shift();
}
