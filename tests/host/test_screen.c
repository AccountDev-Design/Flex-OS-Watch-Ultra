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

void test_screen_all(void)
{
    test_policy();
    test_button();
}
