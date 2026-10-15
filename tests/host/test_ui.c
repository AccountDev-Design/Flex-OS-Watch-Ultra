/*
 * Pruebas de PC de la lógica de interfaz (watch_ui/ui_logic, watch_theme/flex_batt_style).
 */
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

void test_ui_all(void)
{
    test_batt_kind();
    test_batt_style();
}
