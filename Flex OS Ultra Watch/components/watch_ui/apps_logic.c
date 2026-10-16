/*
 * apps_logic — ver include/apps_logic.h
 */
#include "apps_logic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wt_time.h"

/* ================= Calculadora (AppsBasic.h:30-151) ================= */

const char *const calc_labels[5][4] = {
    {"C", "+/-", "%", "/"}, {"7", "8", "9", "x"}, {"4", "5", "6", "-"}, {"1", "2", "3", "+"}, {"0", ".", "=", "DEL"},
};

/* NaN falla (v == v); los infinitos fallan el rango (calcFinite). */
static bool calc_finite(double v)
{
    return (v == v) && (v > -1.0e308) && (v < 1.0e308);
}

static void calc_fmt_to(char *out, size_t n, double v)
{
    if (!calc_finite(v)) {
        snprintf(out, n, "Error");
        return;
    }
    if (v == 0) {
        v = 0; /* evita "-0" */
    }
    snprintf(out, n, "%g", v);
}

static void calc_fmt(calc_t *c, double v)
{
    c->err = !calc_finite(v);
    calc_fmt_to(c->disp, sizeof(c->disp), v);
}

/* 5/0 ya no devuelve 0 en silencio: NaN -> "Error". */
static double calc_compute(double a, double b, char op)
{
    switch (op) {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case 'x':
        return a * b;
    case '/':
        return (b != 0) ? (a / b) : (double)NAN;
    default:
        return b;
    }
}

void calc_reset(calc_t *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->disp, "0");
    c->fresh = true;
}

void calc_key(calc_t *c, char k)
{
    /* Con "Error" en pantalla, el estado numérico no vale: cualquier
     * entrada de número limpia primero; los operadores se ignoran. */
    if (c->err) {
        if (k == 'c' || (k >= '0' && k <= '9') || k == '.') {
            calc_reset(c);
            if (k == 'c') {
                return;
            }
        } else {
            return;
        }
    }
    const int L = (int)strlen(c->disp);
    if (k >= '0' && k <= '9') {
        if (c->fresh || (L == 1 && c->disp[0] == '0')) {
            c->disp[0] = k;
            c->disp[1] = 0;
        } else if (L < 16) {
            c->disp[L] = k;
            c->disp[L + 1] = 0;
        }
        c->fresh = false;
    } else if (k == '.') {
        if (c->fresh) {
            strcpy(c->disp, "0.");
            c->fresh = false;
        } else if (!strchr(c->disp, '.') && L < 15) {
            c->disp[L] = '.';
            c->disp[L + 1] = 0;
        }
    } else if (k == 'c') {
        calc_reset(c);
    } else if (k == '\b') {
        if (!c->fresh && L > 0) {
            c->disp[L - 1] = 0;
            if (c->disp[0] == 0) {
                strcpy(c->disp, "0");
            }
        }
    } else if (k == 'n') {
        if (strcmp(c->disp, "0") != 0) {
            if (c->disp[0] == '-') {
                memmove(c->disp, c->disp + 1, strlen(c->disp));
            } else if (L < (int)sizeof(c->disp) - 1) {
                memmove(c->disp + 1, c->disp, strlen(c->disp) + 1);
                c->disp[0] = '-';
            }
        }
    } else if (k == '%') {
        calc_fmt(c, atof(c->disp) / 100.0);
        c->fresh = true;
    } else if (k == '+' || k == '-' || k == 'x' || k == '/') {
        /* El operador no pisa el display: el acumulador se actualiza y
         * calc_line() compone "acumulador operador entrada". */
        if (c->op && !c->fresh) {
            /* operación a medias: se cierra antes de encadenar ("2 + 3 + 4" = 9) */
            const double r = calc_compute(c->acc, atof(c->disp), c->op);
            if (!calc_finite(r)) {
                calc_fmt(c, r);
                c->op = 0;
                c->fresh = true;
                return;
            }
            c->acc = r;
        } else if (!c->op) {
            c->acc = atof(c->disp);
        }
        /* (op puesto y fresh: cambio de idea de operador; el acumulador se queda) */
        c->op = k;
        c->fresh = true;
    } else if (k == '=') {
        if (c->op) {
            c->acc = calc_compute(c->acc, atof(c->disp), c->op);
            calc_fmt(c, c->acc);
            c->op = 0;
        }
        c->fresh = true;
    }
}

char calc_key_from_label(const char *t)
{
    if (!t || !t[0]) {
        return 0;
    }
    if (!strcmp(t, "C")) {
        return 'c';
    }
    if (!strcmp(t, "+/-")) {
        return 'n';
    }
    if (!strcmp(t, "DEL")) {
        return '\b';
    }
    return t[0]; /* dígitos, '.', '%', '/', 'x', '-', '+', '=' */
}

void calc_line(const calc_t *c, char *out, size_t n)
{
    if (c->err) {
        snprintf(out, n, "Error");
        return;
    }
    if (!c->op) {
        snprintf(out, n, "%s", c->disp);
        return;
    }
    char acc[24];
    calc_fmt_to(acc, sizeof(acc), c->acc);
    if (c->fresh) {
        snprintf(out, n, "%s %c", acc, c->op); /* operador recién pulsado */
    } else {
        snprintf(out, n, "%s %c %s", acc, c->op, c->disp);
    }
}

bool calc_label_is_op(int row, int col)
{
    return col == 3 || (row == 4 && col == 2);
}

/* ================= Calendario (AppsBasic.h:351-402) ================= */

bool cal_month(int year, int month, cal_month_t *m)
{
    const int dim = wt_days_in_month(year, month);
    if (dim <= 0) {
        return false;
    }
    m->year = year;
    m->month = month;
    m->dim = dim;
    m->first_wd = wt_weekday_from_days(wt_days_from_civil(year, month, 1));
    m->rows = (m->first_wd + dim + 6) / 7;
    return true;
}

void cal_shift(int *year, int *month, int delta)
{
    int idx = *year * 12 + (*month - 1) + delta;
    *year = idx / 12;
    *month = idx % 12 + 1;
}

void cal_cell(const cal_month_t *m, int day, int *row, int *col)
{
    const int cell = m->first_wd + day - 1;
    *row = cell / 7;
    *col = cell % 7;
}

/* ================= Device Care (DeviceCare.h:760-890) ================= */

bool dc_score(const dc_metric_t *m, int n, int *out)
{
    int sum = 0, k = 0;
    for (int i = 0; i < n; i++) {
        if (m[i].level == DC_NA) {
            continue; /* no medida: no puntúa */
        }
        int v;
        if (m[i].pct >= 0) {
            v = m[i].pct;
            if (m[i].level == DC_WARN) {
                v = v * 70 / 100;
            }
            if (m[i].level == DC_BAD) {
                v = v * 35 / 100;
            }
        } else {
            v = (m[i].level == DC_OK) ? 100 : (m[i].level == DC_WARN ? 62 : 25);
        }
        sum += v;
        k++;
    }
    if (k == 0) {
        return false;
    }
    int sc = sum / k;
    sc = sc < 0 ? 0 : (sc > 100 ? 100 : sc);
    if (out) {
        *out = sc;
    }
    return true;
}

const char *dc_score_word(int sc)
{
    return sc >= 80 ? "Todo correcto" : (sc >= 55 ? "Necesita atención" : "Estado crítico");
}

dc_level_t dc_score_level(int sc)
{
    return sc >= 80 ? DC_OK : (sc >= 55 ? DC_WARN : DC_BAD);
}

dc_level_t dc_sram_level(unsigned long free_b)
{
    return free_b < 40ul * 1024 ? DC_BAD : (free_b < 64ul * 1024 ? DC_WARN : DC_OK);
}

dc_level_t dc_psram_level(unsigned long free_b, unsigned long largest_b)
{
    if (free_b < 512ul * 1024 || largest_b < 410ul * 502 * 2) {
        return DC_BAD;
    }
    if (free_b < 1024ul * 1024 || largest_b < 1024ul * 1024) {
        return DC_WARN;
    }
    return DC_OK;
}

dc_level_t dc_storage_level(int used_pct)
{
    return used_pct >= 95 ? DC_BAD : (used_pct >= 85 ? DC_WARN : DC_OK);
}

dc_level_t dc_count_level(int ok, int tot)
{
    return ok >= tot ? DC_OK : (ok >= tot - 1 ? DC_WARN : DC_BAD);
}
