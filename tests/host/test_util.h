/* Macros comunes de las pruebas de PC. */
#pragma once
#include <stdio.h>

extern int g_checks, g_fail;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); g_checks++; if (_a != _b) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s = %ld, esperado %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* Suites de los demás archivos */
void test_ui_all(void);
void test_alerts_all(void);
