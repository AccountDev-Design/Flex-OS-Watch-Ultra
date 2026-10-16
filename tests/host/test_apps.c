/* Pruebas de apps_logic: Calculadora y Calendario de Flex OS Ultra. */
#include <string.h>

#include "apps_logic.h"
#include "test_util.h"

static void keys(calc_t *c, const char *seq)
{
    for (const char *p = seq; *p; p++) {
        calc_key(c, *p);
    }
}

static int line_is(const calc_t *c, const char *want)
{
    char l[64];
    calc_line(c, l, sizeof(l));
    if (strcmp(l, want) != 0) {
        fprintf(stderr, "  linea \"%s\", esperada \"%s\"\n", l, want);
        return 0;
    }
    return 1;
}

static void test_calc(void)
{
    calc_t c;
    calc_reset(&c);
    CHECK(line_is(&c, "0"));
    /* La línea muestra la operación tal como se escribe (AppsBasic.h:58-81) */
    keys(&c, "25");
    CHECK(line_is(&c, "25"));
    calc_key(&c, '+');
    CHECK(line_is(&c, "25 +"));
    keys(&c, "10");
    CHECK(line_is(&c, "25 + 10"));
    calc_key(&c, '=');
    CHECK(line_is(&c, "35"));
    /* Encadenar: 2 + 3 + 4 = 9 (no 7) */
    calc_key(&c, 'c');
    keys(&c, "2+3+4=");
    CHECK(line_is(&c, "9"));
    /* Cambio de idea de operador: 8 + x 3 = 24 */
    calc_key(&c, 'c');
    keys(&c, "8+x3=");
    CHECK(line_is(&c, "24"));
    /* División por cero: "Error", no un 0 falso; los operadores se ignoran
     * y un dígito empieza de nuevo */
    calc_key(&c, 'c');
    keys(&c, "5/0=");
    CHECK(line_is(&c, "Error") && c.err);
    calc_key(&c, '+');
    CHECK(line_is(&c, "Error"));
    calc_key(&c, '7');
    CHECK(line_is(&c, "7") && !c.err);
    /* Error a mitad de una cadena: 5 / 0 + -> Error */
    calc_key(&c, 'c');
    keys(&c, "5/0+");
    CHECK(line_is(&c, "Error"));
    /* Decimales, signo, porcentaje y borrar */
    calc_key(&c, 'c');
    keys(&c, ".5");
    CHECK(line_is(&c, "0.5"));
    calc_key(&c, '.');
    CHECK(line_is(&c, "0.5"));
    calc_key(&c, 'n');
    CHECK(line_is(&c, "-0.5"));
    calc_key(&c, 'n');
    CHECK(line_is(&c, "0.5"));
    calc_key(&c, '\b');
    calc_key(&c, '\b');
    calc_key(&c, '\b');
    CHECK(line_is(&c, "0"));
    calc_key(&c, 'n');
    CHECK(line_is(&c, "0")); /* +/- sobre 0 no hace nada */
    keys(&c, "50%");
    CHECK(line_is(&c, "0.5"));
    /* Tope de 16 cifras */
    calc_key(&c, 'c');
    keys(&c, "12345678901234567890");
    CHECK_EQ(strlen(c.disp), 16);
    /* %g: 6 cifras significativas, "-0" nunca */
    calc_key(&c, 'c');
    keys(&c, "1/3=");
    CHECK(line_is(&c, "0.333333"));
    calc_key(&c, 'c');
    keys(&c, "0-0=");
    CHECK(line_is(&c, "0"));
    calc_key(&c, 'c');
    keys(&c, "99999999x99999999=");
    CHECK(line_is(&c, "1e+16"));
    /* Etiquetas de la rejilla */
    CHECK_EQ(calc_key_from_label("C"), 'c');
    CHECK_EQ(calc_key_from_label("+/-"), 'n');
    CHECK_EQ(calc_key_from_label("DEL"), '\b');
    CHECK_EQ(calc_key_from_label("x"), 'x');
    CHECK_EQ(calc_key_from_label(""), 0);
    CHECK(calc_label_is_op(0, 3) && calc_label_is_op(4, 2) && !calc_label_is_op(4, 1));
    int ops = 0;
    for (int r = 0; r < 5; r++) {
        for (int k = 0; k < 4; k++) {
            ops += calc_label_is_op(r, k);
        }
    }
    CHECK_EQ(ops, 6); /* columna 3 (/, x, -, +, DEL) + "=" */
}

static void test_calendar(void)
{
    cal_month_t m;
    /* Octubre de 2026: empieza en jueves (4), 31 días, 5 filas */
    CHECK(cal_month(2026, 10, &m));
    CHECK_EQ(m.first_wd, 4);
    CHECK_EQ(m.dim, 31);
    CHECK_EQ(m.rows, 5);
    int r, c;
    cal_cell(&m, 15, &r, &c);
    CHECK_EQ(r, 2);
    CHECK_EQ(c, 4); /* jueves 15 */
    /* Febrero bisiesto y no bisiesto */
    CHECK(cal_month(2028, 2, &m));
    CHECK_EQ(m.dim, 29);
    CHECK(cal_month(2026, 2, &m));
    CHECK_EQ(m.dim, 28);
    CHECK_EQ(m.first_wd, 0); /* 1-2-2026 es domingo: 4 filas */
    CHECK_EQ(m.rows, 4);
    /* Mayo de 2026 empieza en viernes y necesita 6 filas */
    CHECK(cal_month(2026, 5, &m));
    CHECK_EQ(m.first_wd, 5);
    CHECK_EQ(m.rows, 6);
    CHECK(!cal_month(2026, 13, &m));
    CHECK(!cal_month(2026, 0, &m));
    /* Cambio de mes con paso de año */
    int y = 2026, mo = 12;
    cal_shift(&y, &mo, 1);
    CHECK(y == 2027 && mo == 1);
    cal_shift(&y, &mo, -1);
    CHECK(y == 2026 && mo == 12);
    cal_shift(&y, &mo, -12);
    CHECK(y == 2025 && mo == 12);
    y = 2026;
    mo = 1;
    cal_shift(&y, &mo, -1);
    CHECK(y == 2025 && mo == 12);
}

static void test_devcare(void)
{
    int sc = -1;
    /* Nada medido: sin puntuación */
    const dc_metric_t none[2] = {{DC_NA, -1}, {DC_NA, 50}};
    CHECK(!dc_score(none, 2, &sc));
    /* Todo bien sin porcentaje = 100 */
    const dc_metric_t ok[3] = {{DC_OK, -1}, {DC_OK, -1}, {DC_NA, -1}};
    CHECK(dc_score(ok, 3, &sc) && sc == 100);
    /* (90 + 80*70/100 + 62 + 25 + 40*35/100) / 5 = (90 + 56 + 62 + 25 + 14) / 5 = 49 */
    const dc_metric_t mix[5] = {{DC_OK, 90}, {DC_WARN, 80}, {DC_WARN, -1}, {DC_BAD, -1}, {DC_BAD, 40}};
    CHECK(dc_score(mix, 5, &sc));
    CHECK_EQ(sc, 49);
    CHECK(strcmp(dc_score_word(80), "Todo correcto") == 0);
    CHECK(strcmp(dc_score_word(79), "Necesita atención") == 0);
    CHECK(strcmp(dc_score_word(54), "Estado crítico") == 0);
    CHECK(dc_score_level(55) == DC_WARN && dc_score_level(54) == DC_BAD && dc_score_level(100) == DC_OK);
    CHECK(dc_sram_level(39 * 1024) == DC_BAD && dc_sram_level(40 * 1024) == DC_WARN && dc_sram_level(64 * 1024) == DC_OK);
    /* PSRAM: el fondo (402 KB) tiene que caber en el bloque mayor */
    CHECK(dc_psram_level(6ul << 20, 400ul * 1024) == DC_BAD);
    CHECK(dc_psram_level(6ul << 20, 900ul * 1024) == DC_WARN);
    CHECK(dc_psram_level(6ul << 20, 4ul << 20) == DC_OK);
    CHECK(dc_psram_level(600ul * 1024, 600ul * 1024) == DC_WARN);
    CHECK(dc_storage_level(95) == DC_BAD && dc_storage_level(85) == DC_WARN && dc_storage_level(84) == DC_OK);
    CHECK(dc_count_level(5, 5) == DC_OK && dc_count_level(4, 5) == DC_WARN && dc_count_level(3, 5) == DC_BAD);
}

void test_apps_all(void)
{
    test_calc();
    test_calendar();
    test_devcare();
}
