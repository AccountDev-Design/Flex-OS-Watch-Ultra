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

#include "test_util.h"

int g_checks, g_fail;

/* Registros base: chip correcto, batería presente, ADC VBAT y gauge activos. */
static axp2101_regs_t regs_base(void)
{
    axp2101_regs_t r = {0};
    r.chip_id = 0x4A;
    r.status1 = 0x08;      /* bit3 batería */
    r.status2 = 0x40;      /* dir=2 descarga, sin VINDPM, etapa 0 */
    r.adc_ctrl = 0x01;     /* VBAT */
    r.gauge_ctrl = 0x0A;   /* gauge (bit3) + cargador habilitado (bit1), valores de POR */
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
    CHECK(s.vbus_present);
    CHECK(!s.vindpm);
    CHECK(s.charger_enabled);
    CHECK_EQ(s.dir, AXP_DIR_CHARGE);
    CHECK_EQ(s.stage, AXP_CHG_CC);
    CHECK_EQ(s.vbat_mv, 4122);
    CHECK_EQ(s.percent, 85);
    CHECK_EQ(batt_classify(&s), BATT_STATE_CHARGING);

    /* 0x34 [7:6] son bits de depuración (ch_dbg_en), no VBAT */
    r.vbat_h = 0xC0 | 0x0F;
    r.vbat_l = 0xA0;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.vbat_mv, 4000);
    /* VBAT es de 14 bits: 0x34 bit5 = 8192 mV (fuera de rango para una LiPo) */
    r.vbat_h = 0x20 | 0x0F;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.vbat_mv, -1);

    /* Datasheet: REG01 bit3 es VINDPM, NO "sin USB". USB presente = VBUS good. */
    r = regs_base();
    r.status1 = 0x28;
    r.status2 = 0x08 | 0x05;
    axp2101_decode(&r, &s);
    CHECK(s.vbus_present);
    CHECK(s.vindpm);
    CHECK_EQ(batt_classify(&s), BATT_STATE_EXTERNAL_IDLE);

    /* Cargando con la entrada en VINDPM: sigue siendo carga activa */
    r.status2 = 0x20 | 0x08 | 0x02;
    axp2101_decode(&r, &s);
    CHECK(s.vindpm);
    CHECK_EQ(batt_classify(&s), BATT_STATE_CHARGING);

    /* REG01 bit7 reservado: no altera el sentido de la corriente */
    r.status2 = 0x80 | 0x20 | 0x02;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.dir, AXP_DIR_CHARGE);
    /* Sentido 11 (reservado) -> OTHER, nunca "cargando" */
    r.status2 = 0x60 | 0x02;
    axp2101_decode(&r, &s);
    CHECK_EQ(s.dir, AXP_DIR_OTHER);
    CHECK_EQ(batt_classify(&s), BATT_STATE_EXTERNAL_IDLE);

    /* Banderas de REG00 */
    r = regs_base();
    r.status1 = 0x08 | 0x02 | 0x01;
    axp2101_decode(&r, &s);
    CHECK(s.thermal_reg);
    CHECK(s.input_limited);
    /* Cargador deshabilitado en REG18 bit1 */
    r.gauge_ctrl = 0x08;
    axp2101_decode(&r, &s);
    CHECK(!s.charger_enabled);

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
    r.status2 = 0x00;
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


static void test_cfg_decode(void)
{
    /* Valores por defecto (POR) del datasheet */
    axp2101_cfg_regs_t r = {0};
    r.vindpm = 0x06;
    r.iin_lim = 0x04;
    r.gauge_ctrl = 0x0A;
    r.adc_ctrl = 0x03;
    r.ts_ctrl = 0x02;
    r.iprechg = 0x05;
    r.icc = 0x0A;
    r.iterm = 0x15;
    r.cv = 0x03;
    r.bat_det = 0x01;
    axp2101_cfg_t c;
    axp2101_decode_cfg(&r, &c);
    CHECK_EQ(c.vindpm_mv, 4360);
    CHECK_EQ(c.input_limit_ma, 1500);
    CHECK_EQ(c.precharge_ma, 125);
    CHECK_EQ(c.charge_ma, 400);
    CHECK_EQ(c.term_ma, 125);
    CHECK(c.term_enabled);
    CHECK_EQ(c.cv_mv, 4200);
    CHECK(c.charger_enabled);
    CHECK(c.gauge_enabled);
    CHECK(!c.button_cell_charge);
    CHECK(c.vbat_adc_enabled);
    CHECK(c.ts_adc_enabled);
    CHECK(c.ts_affects_charger);
    CHECK_EQ(c.ts_current_mode, 0);
    CHECK(c.batt_detect_enabled);

    /* Tabla de ICC (REG62): 25*N hasta 200 mA, luego +100 mA por paso */
    const struct { uint8_t raw; int ma; } icc[] = {
        {0x00, 0}, {0x04, 100}, {0x08, 200}, {0x09, 300}, {0x0A, 400},
        {0x0B, 500}, {0x10, 1000}, {0x11, -1}, {0xE9, 300},
    };
    for (size_t i = 0; i < sizeof(icc) / sizeof(icc[0]); i++) {
        r.icc = icc[i].raw;
        axp2101_decode_cfg(&r, &c);
        CHECK_EQ(c.charge_ma, icc[i].ma);
    }
    /* CV (REG64): 000 y 11x reservados */
    const struct { uint8_t raw; int mv; } cv[] = {
        {0x00, -1}, {0x01, 4000}, {0x02, 4100}, {0x04, 4350}, {0x05, 4400}, {0x06, -1}, {0x07, -1},
    };
    for (size_t i = 0; i < sizeof(cv) / sizeof(cv[0]); i++) {
        r.cv = cv[i].raw;
        axp2101_decode_cfg(&r, &c);
        CHECK_EQ(c.cv_mv, cv[i].mv);
    }
    /* Límite de entrada (REG16): 110/111 reservados; VINDPM 3.88 V + 80 mV*N */
    r.iin_lim = 0x06;
    r.vindpm = 0x0F;
    r.iterm = 0x0F;      /* N=15 reservado, terminación desactivada */
    r.iprechg = 0x09;    /* reservado */
    r.ts_ctrl = 0x10 | 0x0C;  /* TS no afecta al cargador, fuente siempre encendida */
    r.gauge_ctrl = 0x04; /* solo la carga de la pila de botón */
    axp2101_decode_cfg(&r, &c);
    CHECK_EQ(c.input_limit_ma, -1);
    CHECK_EQ(c.vindpm_mv, 5080);
    CHECK_EQ(c.term_ma, -1);
    CHECK(!c.term_enabled);
    CHECK_EQ(c.precharge_ma, -1);
    CHECK(!c.ts_affects_charger);
    CHECK_EQ(c.ts_current_mode, 3);
    CHECK(!c.charger_enabled);
    CHECK(c.button_cell_charge);
    CHECK(!c.gauge_enabled);
}

/* Muestras: D descarga, C cargando (CC), E USB sin carga activa, F carga terminada */
static axp2101_regs_t mk(char kind, int pct)
{
    axp2101_regs_t r = regs_base();
    r.percent = (uint8_t)pct;
    switch (kind) {
    case 'C': r.status1 = 0x28; r.status2 = 0x22; break;
    case 'V': r.status1 = 0x28; r.status2 = 0x23; break; /* tensión constante */
    case 'E': r.status1 = 0x28; r.status2 = 0x03; break; /* CV con corriente ~0: sentido "reposo" */
    case 'F': r.status1 = 0x28; r.status2 = 0x04; break;
    default:  r.status1 = 0x08; r.status2 = 0x40; break;
    }
    return r;
}

static void test_transitions(void)
{
    batt_tracker_t t;
    batt_tracker_init(&t);
    feed(&t, mk('D', 50));
    feed(&t, mk('D', 50));
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);
    CHECK_EQ(t.charge_events, 0);

    /* Enchufar: confirmado en la 2.ª muestra, un solo evento */
    feed(&t, mk('C', 50));
    feed(&t, mk('C', 51));
    CHECK_EQ(t.state, BATT_STATE_CHARGING);
    CHECK_EQ(t.charge_events, 1);

    /* Final de la tensión constante: el sentido oscila entre carga y reposo.
     * Con menos de 3 muestras seguidas de "reposo" NO se sale de "cargando",
     * y por tanto no se repite el evento de inicio de carga. */
    const char flicker[] = "VEVEEVEEV";
    for (const char *k = flicker; *k; k++) {
        feed(&t, mk(*k, 97));
        CHECK_EQ(t.state, BATT_STATE_CHARGING);
    }
    CHECK_EQ(t.charge_events, 1);
    /* 3 seguidas: ahora sí, "externa sin carga" */
    feed(&t, mk('E', 98));
    feed(&t, mk('E', 98));
    CHECK_EQ(t.state, BATT_STATE_CHARGING);
    feed(&t, mk('E', 98));
    CHECK_EQ(t.state, BATT_STATE_EXTERNAL_IDLE);
    CHECK_EQ(t.percent, 98);

    /* El cargador indica terminada -> "carga completa" con 2 muestras */
    feed(&t, mk('F', 99));
    feed(&t, mk('F', 100));
    CHECK_EQ(t.state, BATT_STATE_FULL);
    CHECK_EQ(t.percent, 100);
    CHECK_EQ(t.charge_events, 1);

    /* Desenchufar: inequívoco (sin VBUS), 2 muestras; el % se toma de nuevo */
    feed(&t, mk('D', 98));
    feed(&t, mk('D', 98));
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);
    CHECK_EQ(t.percent, 98);
    feed(&t, mk('D', 99));   /* en descarga no sube */
    CHECK_EQ(t.percent, 98);

    /* De "cargando" a desenchufado: 2 muestras, no 3 */
    feed(&t, mk('C', 90));
    feed(&t, mk('C', 90));
    CHECK_EQ(t.state, BATT_STATE_CHARGING);
    CHECK_EQ(t.charge_events, 2);
    feed(&t, mk('D', 90));
    feed(&t, mk('D', 90));
    CHECK_EQ(t.state, BATT_STATE_DISCHARGING);

    /* "Cargando" -> "carga completa" directo también con 2 muestras */
    feed(&t, mk('C', 99));
    feed(&t, mk('C', 99));
    CHECK_EQ(t.charge_events, 3);
    feed(&t, mk('F', 100));
    feed(&t, mk('F', 100));
    CHECK_EQ(t.state, BATT_STATE_FULL);
    CHECK_EQ(t.charge_events, 3);
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

/* Canales de 8 bits (aprox.) de un RGB565 */
static void rgb_of(uint16_t p, int *r, int *g, int *b)
{
    *r = ((p >> 11) & 0x1F) * 255 / 31;
    *g = ((p >> 5) & 0x3F) * 255 / 63;
    *b = (p & 0x1F) * 255 / 31;
}

static void test_wallpapers(void)
{
    const int w = FLEX_SCREEN_W, h = FLEX_SCREEN_H;
    uint16_t *a = malloc((size_t)w * h * 2), *b = malloc((size_t)w * h * 2);
    CHECK(a && b);
    if (!a || !b) {
        free(a);
        free(b);
        return;
    }
    CHECK(strcmp(flex_wallpaper_name(FLEX_WP_AURORA), "Aurora") == 0);
    CHECK(strcmp(flex_wallpaper_name((flex_wp_t)99), "Flex Original") == 0);
    int r, g, bl;

    /* Flex Original: arriba a la derecha verde (80,224,74), abajo a la izquierda violeta (112,46,230) */
    flex_wallpaper_render(FLEX_WP_FLEX_ORIGINAL, a, w, h);
    rgb_of(a[0 * w + (w - 1)], &r, &g, &bl);
    CHECK(g > 180 && g > r && g > bl);
    rgb_of(a[(h - 1) * w + 0], &r, &g, &bl);
    CHECK(bl > 180 && r > g);
    /* Fuera de rango = Flex Original (la ruta segura de la referencia) */
    flex_wallpaper_render((flex_wp_t)99, b, w, h);
    CHECK(memcmp(a, b, (size_t)w * h * 2) == 0);

    /* Por bandas = de una vez (el selector y el reloj pueden repartir el trabajo) */
    for (int id = 0; id < FLEX_WP_COUNT_; id++) {
        flex_wallpaper_render((flex_wp_t)id, a, w, h);
        memset(b, 0xAA, (size_t)w * h * 2);
        for (int y = 0; y < h; y += 37) {
            flex_wallpaper_render_rows((flex_wp_t)id, b, w, h, y, y + 36);
        }
        CHECK(memcmp(a, b, (size_t)w * h * 2) == 0);
    }

    /* Aurora: esquina superior izquierda clara (205,238,236): luma > 140 */
    flex_wallpaper_render(FLEX_WP_AURORA, a, w, h);
    CHECK(flex_wallpaper_region_luma(a, w, h, 0, 0, 20, 20) > 140);
    /* Halo: el núcleo (17,22,58) es oscuro y el anillo fino de r=122 (176,198,255) brilla */
    flex_wallpaper_render(FLEX_WP_HALO, a, w, h);
    const int hx = 206 * w / 480, hy = 402 * h / 800, rr = 122 * w / 480;
    rgb_of(a[hy * w + hx], &r, &g, &bl);
    CHECK(bl < 90 && r < 40);
    rgb_of(a[hy * w + hx + rr], &r, &g, &bl);
    CHECK(bl > 200 && g > 140);
    CHECK(flex_wallpaper_region_luma(a, w, h, 0, 0, w, h) < 140);
    /* Onyx: negro fuera del halo */
    flex_wallpaper_render(FLEX_WP_ONYX, a, w, h);
    CHECK_EQ(flex_wallpaper_region_luma(a, w, h, 0, 0, 40, 40), 0);
    free(a);
    free(b);
}

int main(void)
{
    test_decode();
    test_classify();
    test_tracker();
    test_levels();
    test_cfg_decode();
    test_transitions();
    test_flash_probe();
    test_glass();
    test_wallpaper();
    test_wallpapers();
    test_ui_all();
    test_alerts_all();
    test_time_all();
    test_gesture_all();
    test_power_sched_all();
    test_safe_all();
    test_settings_all();
    test_clock_all();
    test_screen_all();
    test_imu_all();
    test_storage_all();
    test_apps_all();
    printf("%d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
