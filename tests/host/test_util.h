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
void test_time_all(void);
void test_gesture_all(void);
void test_power_sched_all(void);
void test_safe_all(void);
void test_settings_all(void);
void test_clock_all(void);
void test_screen_all(void);
void test_imu_all(void);
void test_storage_all(void);
void test_apps_all(void);
void test_net_all(void);
void test_flexlink_all(void);
void test_files_all(void);
void test_weather_all(void);
void test_browser_all(void);
void test_br_net_all(void);
void test_br_backend_all(void);
void test_ota_all(void);
void test_grid_all(void);
void test_glass_all(void);
void test_activity_all(void);
void test_media_all(void);
void test_audio_all(void);
