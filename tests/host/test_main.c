/*
 * Pruebas de PC de la lógica pura de Flex OS Watch Ultra (sin ESP-IDF).
 *   make -C tests/host
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery_model.h"
#include "flash_probe_logic.h"
#include "flex_glass_math.h"
#include "flex_tokens.h"
#include "flex_wallpaper.h"

static int g_checks, g_fail;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); g_checks++; if (_a != _b) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s = %ld, esperado %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* Registros base: chip correcto, batería presente, ADC VBAT y gauge activos. */
static axp2101_regs_t regs_base(void)
{
    axp2101_regs_t r = {0};
    r.chip_id = 0x4A;
    r.status1 = 0x08;      /* bit3 batería */
    r.status2 = 0x48;      /* dir=2 descarga, bit3=1 (sin VBUS), etapa 0 */
    r.adc_ctrl = 0x01;     /* VBAT */
    r.gauge_ctrl = 0x08;   /* gauge */
    r.vbat_h = 0x0F;       /* 0x0F<<8 | 0xA0 = 4000 mV */
    r.vbat_l = 0xA0;
    r.percent = 80;
    return r;
}

static void test_decode(void)
{
    axp2101_regs_t r = regs_base();
    axp2101_status_t s;

    /* Cargando en corriente constante con USB: STATUS1 0x28, STATUS2 0x22 */
    r.status1 = 0x28;
    r.status2 = 0x22;
    r.vbat_h = 0x10;
    r.vbat_l = 0x1A;
    r.percent = 85;
    axp2101_decode(&r, &s);
    CHECK(s.chip_ok);
    CHECK(s.batt_present);
    CHECK(s.vbus_good);
    CHECK(s.vbus_in);
    CHECK_EQ(s.dir, AXP_DIR_CHARGE);
    CHECK_EQ(s.stage, AXP_CHG_CC);
    CHECK_EQ(s.vbat_mv, 4122);
    CHECK_EQ(s.percent, 85);
    CHECK_EQ(batt_classify(&s), BATT_STATE_CHARGING);

    /* Los 3 bits altos de 0x34 no forman parte de VBAT */
    r.vbat_h = 0xE0 | 0x0F;
    r.vbat_l = 0xA0;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.vbat_mv, 4000);

    /* VBUS correcto pero bit3 de STATUS2 a 1: no cuenta como conectado */
    r = regs_base();
    r.status1 = 0x28;
    r.status2 = 0x08 | 0x05;
    axp2101_decode(&r, &s);
    CHECK(s.vbus_good);
    CHECK(!s.vbus_in);

    /* ADC de VBAT apagado -> VBAT no disponible (no se activa: solo lectura) */
    r = regs_base();
    r.adc_ctrl = 0x00;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.vbat_mv, -1);
    CHECK(!s.vbat_adc_enabled);

    /* Gauge apagado o valor imposible -> % no disponible */
    r = regs_base();
    r.gauge_ctrl = 0x00;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.percent, -1);
    r = regs_base();
    r.percent = 0xFF;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.percent, -1);

    /* VBAT fuera de rango plausible */
    r = regs_base();
    r.vbat_h = 0x00;
    r.vbat_l = 0x10;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.vbat_mv, -1);

    /* Sin batería: ni VBAT ni % */
    r = regs_base();
    r.status1 = 0x20;
    r.status2 = 0x05;
    axp2101_decode(&r, &s);
    CHECK(!s.batt_present);
    CHECK_EQ(s.vbat_mv, -1);
    CHECK_EQ(s.percent, -1);
    CHECK_EQ(batt_classify(&s), BATT_STATE_NO_BATTERY);

    /* Identificador incorrecto -> sin datos */
    r = regs_base();
    r.chip_id = 0x47;
    axp2101_decode(&r, &s);
    CHECK(!s.chip_ok);
    CHECK_EQ(batt_classify(&s), BATT_STATE_UNKNOWN);
}

static void test_classify(void)
{
    axp2101_regs_t r = regs_base();
    axp2101_status_t s;

    axp2101_decode(&r, &s);
    CHECK_EQ(batt_classify(&s), BATT_STATE_DISCHARGING);

    /* Carga terminada con USB */
    r.status1 = 0x28;
    r.status2 = 0x04;
    axp2101_decode(&r, &s);
    CHECK_EQ(batt_classify(&s), BATT_STATE_FULL);

    /* USB sin carga activa (cargador parado) */
    r.status2 = 0x05;
    axp2101_decode(&r, &s);
    CHECK_EQ(batt_classify(&s), BATT_STATE_EXTERNAL_IDLE);

    /* Etapas 0..3 con sentido "carga" cuentan como cargando */
    for (int st = 0; st <= 3; st++) {
        r.status2 = (uint8_t)(0x20 | st);
        axp2101_decode(&r, &s);
        CHECK_EQ(batt_classify(&s), BATT_STATE_CHARGING);
    }

    /* Sentido "carga" pero etapa "terminada": no es carga activa */
    r.status2 = 0x24;
    axp2101_decode(&r, &s);
    CHECK_EQ(batt_classify(&s), BATT_STATE_FULL);

    /* Sin VBUS y en reposo -> descarga (la batería alimenta el sistema) */
    r.status1 = 0x08;
    r.status2 = 0x08;
    axp2101_decode(&r, &s);
    CHECK_EQ(batt_classify(&s), BATT_STATE_DISCHARGING);
}

static void feed(batt_tracker_t *t, axp2101_regs_t r)
{
    axp2101_status_t s;
    axp2101_decode(&r, &s);
    batt_tracker_update(t, &s, true);
}

static void test_tracker(void)
{
    batt_tracker_t t;
    batt_tracker_init(&t);
    CHECK_EQ(t.state, BATT_STATE_UNKNOWN);
    CHECK_EQ(t.percent, -1);

    /* Confirmación con 2 muestras y % que no sube en descarga */
    axp2101_regs_t r = regs_base();
    r.percent = 80; feed(&t, r);
    CHECK_EQ(t.state, BATT_STATE_UNKNOWN);
    CHECK_EQ(t.percent, 80);
    r.percent = 81; feed(&t, r);
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);
    CHECK_EQ(t.percent, 81);
    r.percent = 79; feed(&t, r);
    CHECK_EQ(t.percent, 79);
    r.percent = 82; feed(&t, r);  /* mediana 81 > 79: se mantiene 79 */
    CHECK_EQ(t.percent, 79);
    r.percent = 78; feed(&t, r);  /* mediana(79,82,78) = 79 */
    CHECK_EQ(t.percent, 79);
    r.percent = 77; feed(&t, r);  /* mediana(82,78,77) = 78 */
    CHECK_EQ(t.percent, 78);
    CHECK_EQ(t.level, BATT_LEVEL_NORMAL);

    /* VBAT filtrada: EMA 1/4 */
    CHECK_EQ(t.vbat_mv, 4000);
    r.vbat_h = 0x0F; r.vbat_l = 0xE0; /* 4064 mV */
    feed(&t, r);
    CHECK_EQ(t.vbat_mv, 4016);

    /* Enchufar: una sola muestra de "cargando" no confirma */
    axp2101_regs_t c = r;
    c.status1 = 0x28;
    c.status2 = 0x22;
    feed(&t, c);
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);
    CHECK(!t.charging_started);
    /* Un parpadeo vuelve atrás: tampoco confirma */
    feed(&t, r);
    feed(&t, c);
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);
    /* Segunda seguida: confirmado, y el aviso de inicio sale una sola vez */
    feed(&t, c);
    CHECK_EQ(t.state, BATT_STATE_CHARGING);
    CHECK(t.charging_started);
    feed(&t, c);
    CHECK(!t.charging_started);

    /* Solo conectar USB sin carga activa NO es "cargando" */
    batt_tracker_t u;
    batt_tracker_init(&u);
    axp2101_regs_t e = regs_base();
    e.status1 = 0x28;
    e.status2 = 0x05;
    feed(&u, e);
    feed(&u, e);
    CHECK_EQ(u.state, BATT_STATE_EXTERNAL_IDLE);
    CHECK(!u.charging_started);

    /* Fallos de lectura: se conservan los valores; con 2 seguidos -> sin datos */
    batt_tracker_t f;
    batt_tracker_init(&f);
    axp2101_regs_t g = regs_base();
    feed(&f, g);
    feed(&f, g);
    CHECK_EQ(f.state, BATT_STATE_DISCHARGING);
    axp2101_status_t dummy = {0};
    batt_tracker_update(&f, &dummy, false);
    CHECK_EQ(f.state, BATT_STATE_DISCHARGING);
    CHECK_EQ(f.percent, 80);
    batt_tracker_update(&f, &dummy, false);
    CHECK_EQ(f.state, BATT_STATE_UNKNOWN);
    CHECK_EQ(f.percent, -1);
    CHECK_EQ(f.vbat_mv, -1);
    CHECK_EQ(f.level, BATT_LEVEL_UNKNOWN);
}

static void test_levels(void)
{
    batt_level_t l = BATT_LEVEL_UNKNOWN;
    l = batt_level_next(l, 16); CHECK_EQ(l, BATT_LEVEL_NORMAL);
    l = batt_level_next(l, 15); CHECK_EQ(l, BATT_LEVEL_LOW);
    l = batt_level_next(l, 17); CHECK_EQ(l, BATT_LEVEL_LOW);      /* histéresis */
    l = batt_level_next(l, 18); CHECK_EQ(l, BATT_LEVEL_NORMAL);
    l = batt_level_next(l, 5);  CHECK_EQ(l, BATT_LEVEL_CRITICAL); /* empeora de golpe */
    l = batt_level_next(l, 7);  CHECK_EQ(l, BATT_LEVEL_CRITICAL);
    l = batt_level_next(l, 8);  CHECK_EQ(l, BATT_LEVEL_LOW);
    l = batt_level_next(l, 30); CHECK_EQ(l, BATT_LEVEL_NORMAL);
    l = batt_level_next(l, -1); CHECK_EQ(l, BATT_LEVEL_UNKNOWN);
}

static void test_flash_probe(void)
{
    const bool ok3[3] = {true, true, true};
    const bool eq_all[3] = {true, true, true};
    const bool eq_none[3] = {false, false, false};
    const bool eq_mix[3] = {false, true, false};
    const bool rd_err[3] = {true, false, true};
    CHECK_EQ(flash_probe_classify(0, ok3, eq_none), FLASH_PROBE_NOT_RUN);
    CHECK_EQ(flash_probe_classify(3, ok3, eq_all), FLASH_PROBE_ALIASED);
    CHECK_EQ(flash_probe_classify(3, ok3, eq_none), FLASH_PROBE_UPPER_DISTINCT);
    CHECK_EQ(flash_probe_classify(3, ok3, eq_mix), FLASH_PROBE_INCONCLUSIVE);
    CHECK_EQ(flash_probe_classify(3, rd_err, eq_none), FLASH_PROBE_READ_ERROR);
}

static void test_glass(void)
{
    CHECK_EQ(flex_glass_luma565(0x0000), 0);
    CHECK_EQ(flex_glass_luma565(0xFFFF), 266);
    const uint16_t tint = flex_rgb565(0x30, 0x36, 0x48); /* FLEX_C_GLASS */
    CHECK_EQ(flex_glass_luma565(tint), 56);
    /* Sobre negro: 46 + 56*24/128 = 56 */
    CHECK_EQ(flex_glass_tint_mix(0, 10, tint), 56);
    /* Sin muestras: el valor base del nivel 50 */
    CHECK_EQ(flex_glass_tint_mix(0, 0, tint), FLEX_GLASS_TINT_BASE);
    /* Fondo blanco: diferencia saturada a 128 -> máximo */
    CHECK_EQ(flex_glass_tint_mix(266u * 4, 4, tint), FLEX_GLASS_TINT_MAX);
}

static void test_wallpaper(void)
{
    const int w = FLEX_SCREEN_W, h = FLEX_SCREEN_H;
    uint16_t *buf = malloc((size_t)w * h * sizeof(uint16_t));
    CHECK(buf != NULL);
    if (!buf) {
        return;
    }
    flex_wallpaper_onyx(buf, w, h);
    /* Arriba a la izquierda queda fuera del halo: negro puro (AMOLED apagado) */
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(buf[10 * w + 10], 0);
    /* Centro del halo: azul dominante, muy oscuro (alfa 46 de #1E40B4) */
    const int cx = 240 * w / 480, cy = 690 * h / 800;
    const uint16_t p = buf[cy * w + cx];
    const int r5 = p >> 11, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
    CHECK(r5 <= 1);
    CHECK(g6 >= 2 && g6 <= 3);
    CHECK(b5 >= 4 && b5 <= 5);
    /* Ningún píxel supera el color pleno del halo con alfa 46 (+1 por tramado) */
    int over = 0;
    for (int i = 0; i < w * h; i++) {
        if ((buf[i] & 0x1F) > 5 || (buf[i] >> 11) > 1) {
            over++;
        }
    }
    CHECK_EQ(over, 0);
    free(buf);
}

int main(void)
{
    test_decode();
    test_classify();
    test_tracker();
    test_levels();
    test_flash_probe();
    test_glass();
    test_wallpaper();
    printf("%d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
