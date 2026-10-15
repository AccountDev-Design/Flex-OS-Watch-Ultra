/*
 * Pruebas de PC de la lógica de interfaz (watch_ui/ui_logic, watch_theme/flex_batt_style).
 */
#include <string.h>

#include "flex_glass_math.h"
#include "test_util.h"

#include "flex_batt_style.h"
#include "flex_tokens.h"
#include "ui_logic.h"

static void test_batt_kind(void)
{
    CHECK_EQ(ui_batt_kind(BATT_STATE_UNKNOWN, BATT_LEVEL_NORMAL), FLEX_BATT_UNKNOWN);
    CHECK_EQ(ui_batt_kind(BATT_STATE_NO_BATTERY, BATT_LEVEL_UNKNOWN), FLEX_BATT_ABSENT);
    CHECK_EQ(ui_batt_kind(BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL), FLEX_BATT_NORMAL);
    CHECK_EQ(ui_batt_kind(BATT_STATE_DISCHARGING, BATT_LEVEL_UNKNOWN), FLEX_BATT_NORMAL);
    CHECK_EQ(ui_batt_kind(BATT_STATE_DISCHARGING, BATT_LEVEL_LOW), FLEX_BATT_LOW);
    CHECK_EQ(ui_batt_kind(BATT_STATE_DISCHARGING, BATT_LEVEL_CRITICAL), FLEX_BATT_CRITICAL);
    /* Con USB, el nivel bajo no se muestra como alarma */
    CHECK_EQ(ui_batt_kind(BATT_STATE_CHARGING, BATT_LEVEL_CRITICAL), FLEX_BATT_CHARGING);
    CHECK_EQ(ui_batt_kind(BATT_STATE_FULL, BATT_LEVEL_NORMAL), FLEX_BATT_FULL);
    CHECK_EQ(ui_batt_kind(BATT_STATE_EXTERNAL_IDLE, BATT_LEVEL_LOW), FLEX_BATT_EXTERNAL);
    CHECK(ui_batt_state_text(BATT_STATE_CHARGING)[0] == 'C');
}

static void test_batt_style(void)
{
    /* Colores de la paleta de Flex OS Ultra */
    CHECK_EQ(flex_batt_fill_rgb(FLEX_BATT_NORMAL), FLEX_C_TXT);
    CHECK_EQ(flex_batt_fill_rgb(FLEX_BATT_LOW), FLEX_C_WARN);
    CHECK_EQ(flex_batt_fill_rgb(FLEX_BATT_CRITICAL), FLEX_C_ERR);
    CHECK_EQ(flex_batt_fill_rgb(FLEX_BATT_CHARGING), FLEX_C_OK);
    CHECK_EQ(flex_batt_fill_rgb(FLEX_BATT_FULL), FLEX_C_OK);
    CHECK_EQ(flex_batt_outline_rgb(FLEX_BATT_UNKNOWN), FLEX_C_MUTE);
    CHECK_EQ(flex_batt_outline_rgb(FLEX_BATT_CRITICAL), FLEX_C_ERR);
    /* Glifos: rayo solo con carga activa; atenuado con USB sin carga */
    CHECK_EQ(flex_batt_glyph(FLEX_BATT_CHARGING), FLEX_BATT_GLYPH_BOLT);
    CHECK_EQ(flex_batt_glyph(FLEX_BATT_EXTERNAL), FLEX_BATT_GLYPH_BOLT_DIM);
    CHECK_EQ(flex_batt_glyph(FLEX_BATT_FULL), FLEX_BATT_GLYPH_CHECK);
    CHECK_EQ(flex_batt_glyph(FLEX_BATT_ABSENT), FLEX_BATT_GLYPH_CROSS);
    CHECK_EQ(flex_batt_glyph(FLEX_BATT_NORMAL), FLEX_BATT_GLYPH_NONE);
    /* Relleno proporcional y redondeado */
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_NORMAL, 100), 24);
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_NORMAL, 50), 12);
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_NORMAL, 0), 0);
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_LOW, 1), 2);       /* siempre visible si > 0 % */
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_NORMAL, 130), 24); /* saturado */
    /* Sin dato o sin batería: nunca un relleno inventado */
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_NORMAL, -1), 0);
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_UNKNOWN, 80), 0);
    CHECK_EQ(flex_batt_fill_width(24, FLEX_BATT_ABSENT, 80), 0);
    CHECK_EQ(flex_batt_fill_width(0, FLEX_BATT_NORMAL, 80), 0);
    /* Monótono: más % nunca da menos relleno */
    int prev = -1;
    for (int p = 0; p <= 100; p++) {
        const int w = flex_batt_fill_width(24, FLEX_BATT_NORMAL, p);
        CHECK(w >= prev);
        prev = w;
    }
}

static void test_charge_gate(void)
{
    ui_charge_gate_t g;
    ui_charge_gate_init(&g);
    /* Primera lectura: solo fija la referencia (eventos previos a la interfaz) */
    CHECK(!ui_charge_gate_update(&g, 3, BATT_STATE_DISCHARGING, BATT_STATE_CHARGING));
    /* Sin eventos nuevos: nada, aunque siga cargando */
    CHECK(!ui_charge_gate_update(&g, 3, BATT_STATE_DISCHARGING, BATT_STATE_CHARGING));
    /* Enchufar desde descarga: una sola vez */
    CHECK(ui_charge_gate_update(&g, 4, BATT_STATE_DISCHARGING, BATT_STATE_CHARGING));
    CHECK(!ui_charge_gate_update(&g, 4, BATT_STATE_DISCHARGING, BATT_STATE_CHARGING));
    /* Desde "externa sin carga" o "completa" también es un inicio real */
    CHECK(ui_charge_gate_update(&g, 5, BATT_STATE_EXTERNAL_IDLE, BATT_STATE_CHARGING));
    CHECK(ui_charge_gate_update(&g, 6, BATT_STATE_FULL, BATT_STATE_CHARGING));
    /* Desde "sin datos" (arranque o error de lectura): no se anima */
    CHECK(!ui_charge_gate_update(&g, 7, BATT_STATE_UNKNOWN, BATT_STATE_CHARGING));
    /* El evento llegó pero ya se desenchufó antes de leerlo: no se anima */
    CHECK(!ui_charge_gate_update(&g, 8, BATT_STATE_CHARGING, BATT_STATE_DISCHARGING));
    /* Arranque ya cargando: la 1.a confirmación viene de UNKNOWN */
    ui_charge_gate_t b;
    ui_charge_gate_init(&b);
    CHECK(!ui_charge_gate_update(&b, 0, BATT_STATE_UNKNOWN, BATT_STATE_UNKNOWN));
    CHECK(!ui_charge_gate_update(&b, 1, BATT_STATE_UNKNOWN, BATT_STATE_CHARGING));
}

static void test_face_time(void)
{
    ui_face_time_t t;
    wt_datetime_t dt = {2026, 10, 15, 9, 41, 7, 4};
    ui_face_time(WATCH_TIME_OK, &dt, &t);
    CHECK(t.valid);
    CHECK(strcmp(t.time, "9:41") == 0);
    CHECK(strcmp(t.date, "Jueves, 15 de octubre") == 0);
    const int64_t k = t.key;
    CHECK(k >= 0);

    /* Otro segundo del mismo minuto: misma clave (no se repinta). */
    dt.second = 59;
    ui_face_time(WATCH_TIME_OK, &dt, &t);
    CHECK_EQ(t.key, k);
    /* Minuto siguiente: clave distinta. */
    dt.minute = 42;
    ui_face_time(WATCH_TIME_OK, &dt, &t);
    CHECK(t.key != k);
    CHECK(strcmp(t.time, "9:42") == 0);
    /* Mismo minuto de otro día: clave distinta. */
    dt.minute = 41;
    dt.day = 16;
    dt.weekday = 5;
    ui_face_time(WATCH_TIME_OK, &dt, &t);
    CHECK(t.key != k);

    /* Sin hora fiable: "--:--" y el motivo, claves distintas por estado. */
    static const struct { watch_time_state_t st; const char *txt; } cases[] = {
        {WATCH_TIME_STARTING, "Leyendo la hora…"},
        {WATCH_TIME_NOT_SET, "Hora sin configurar"},
        {WATCH_TIME_NO_RTC, "Reloj RTC no disponible"},
    };
    int64_t keys[3];
    for (int i = 0; i < 3; i++) {
        ui_face_time(cases[i].st, &dt, &t); /* aunque llegue una fecha, no se usa */
        CHECK(!t.valid);
        CHECK(strcmp(t.time, "--:--") == 0);
        CHECK(strcmp(t.date, cases[i].txt) == 0);
        CHECK(t.key < 0);
        keys[i] = t.key;
    }
    CHECK(keys[0] != keys[1] && keys[1] != keys[2] && keys[0] != keys[2]);
    /* OK sin fecha: se trata como no válida. */
    ui_face_time(WATCH_TIME_OK, NULL, &t);
    CHECK(!t.valid);
    CHECK(strcmp(t.time, "--:--") == 0);
}

static void test_brightness(void)
{
    CHECK_EQ(ui_brightness_clamp(-5), UI_BRIGHT_MIN);
    CHECK_EQ(ui_brightness_clamp(0), UI_BRIGHT_MIN);
    CHECK_EQ(ui_brightness_clamp(9), UI_BRIGHT_MIN);
    CHECK_EQ(ui_brightness_clamp(10), 10);
    CHECK_EQ(ui_brightness_clamp(70), 70);
    CHECK_EQ(ui_brightness_clamp(100), 100);
    CHECK_EQ(ui_brightness_clamp(250), UI_BRIGHT_MAX);

    /* Mismo valor: nunca se reenvía. */
    CHECK(!ui_brightness_should_apply(70, 70, 0, 1000, false));
    CHECK(!ui_brightness_should_apply(70, 70, 0, 1000, true));
    /* Durante el arrastre: como mucho cada 60 ms. */
    CHECK(!ui_brightness_should_apply(71, 70, 1000, 1059, false));
    CHECK(ui_brightness_should_apply(71, 70, 1000, 1060, false));
    /* Al soltar: siempre el valor final. */
    CHECK(ui_brightness_should_apply(71, 70, 1000, 1001, true));
    /* El reloj de milisegundos da la vuelta. */
    CHECK(ui_brightness_should_apply(50, 40, 0xFFFFFFF0u, 0x30u, false));
    CHECK(!ui_brightness_should_apply(50, 40, 0xFFFFFFF0u, 0x10u, false));
}

static void test_formats(void)
{
    char b[24];
    ui_fmt_volts(b, sizeof(b), 4085);
    CHECK(strcmp(b, "4,08 V") == 0);
    ui_fmt_volts(b, sizeof(b), 3009);
    CHECK(strcmp(b, "3,00 V") == 0);
    ui_fmt_volts(b, sizeof(b), 0);
    CHECK(strcmp(b, "0,00 V") == 0);
    ui_fmt_volts(b, sizeof(b), -1);
    CHECK(strcmp(b, "--") == 0);
    ui_fmt_ma(b, sizeof(b), 200);
    CHECK(strcmp(b, "200 mA") == 0);
    ui_fmt_ma(b, sizeof(b), -1);
    CHECK(strcmp(b, "--") == 0);
    ui_fmt_bytes(b, sizeof(b), 180u * 1024u);
    CHECK(strcmp(b, "180 KB") == 0);
    ui_fmt_bytes(b, sizeof(b), 1023u);
    CHECK(strcmp(b, "0 KB") == 0);
    ui_fmt_bytes(b, sizeof(b), 1024u * 1024u);
    CHECK(strcmp(b, "1,0 MB") == 0);
    ui_fmt_bytes(b, sizeof(b), 7u * 1024u * 1024u + 1024u * 1024u / 2u);
    CHECK(strcmp(b, "7,5 MB") == 0);
    ui_fmt_bytes(b, sizeof(b), 0xFFFFFFFFu);
    CHECK(strcmp(b, "4095,9 MB") == 0);
    char tiny[4];
    ui_fmt_volts(tiny, sizeof(tiny), 4085);
    CHECK(strcmp(tiny, "4,0") == 0);
    CHECK(strcmp(ui_time_state_text(WATCH_TIME_OK), "Correcta") == 0);
    CHECK(strcmp(ui_time_state_text(WATCH_TIME_NOT_SET), "Sin configurar") == 0);
    CHECK(strcmp(ui_time_state_text(WATCH_TIME_NO_RTC), "No responde") == 0);
    CHECK(strcmp(ui_time_state_text(WATCH_TIME_STARTING), "Leyendo…") == 0);
}

static void test_mix(void)
{
    CHECK_EQ(flex_mix_rgb(0xFFFFFF, 0x000000, 255), 0xFFFFFF);
    CHECK_EQ(flex_mix_rgb(0xFFFFFF, 0x000000, 0), 0x000000);
    CHECK_EQ(flex_mix_rgb(0x123456, 0xABCDEF, 255), 0x123456);
    CHECK_EQ(flex_mix_rgb(0x123456, 0xABCDEF, 0), 0xABCDEF);
    CHECK_EQ(flex_mix_rgb(0xFF0000, 0x0000FF, 128), 0x80007F); /* redondeo por canal */
    /* vidrio de la paleta sobre negro con el tinte de las filas (56) */
    CHECK_EQ(flex_mix_rgb(0x303648, 0x000000, 56), 0x0B0C10);
    /* nunca se sale del rango de un canal */
    CHECK_EQ(flex_mix_rgb(0xFFFFFF, 0xFFFFFF, 77), 0xFFFFFF);
}

void test_ui_all(void)
{
    test_mix();
    test_formats();
    test_brightness();
    test_face_time();
    test_batt_kind();
    test_batt_style();
    test_charge_gate();
}
