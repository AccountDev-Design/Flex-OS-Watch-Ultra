/*
 * Pruebas de PC del QMI8658 (decodificación, unidades, configuración).
 */
#include <string.h>

#include "qmi8658.h"
#include "imu_heading.h"
#include "test_util.h"

static void test_decode(void)
{
    /* STATUS0 = 3 (datos nuevos), TS = 0x123456, TEMP = 25,5 °C (0x1980),
     * A = (8192, -8192, 4096), G = (64, -128, 0) */
    const uint8_t raw[QMI_BURST_LEN] = {0x03, 0x00, 0x56, 0x34, 0x12, 0x80, 0x19, 0x00, 0x20, 0x00,
                                        0xE0, 0x00, 0x10, 0x40, 0x00, 0x80, 0xFF, 0x00, 0x00};
    qmi_sample_t s;
    qmi_decode(raw, &s);
    CHECK(s.fresh);
    CHECK_EQ(s.ts, 0x123456);
    CHECK_EQ(s.temp, 0x1980);
    CHECK_EQ(s.a[0], 8192);
    CHECK_EQ(s.a[1], -8192);
    CHECK_EQ(s.a[2], 4096);
    CHECK_EQ(s.g[0], 64);
    CHECK_EQ(s.g[1], -128);
    CHECK_EQ(s.g[2], 0);
    CHECK_EQ(qmi_mg(s.a[0]), 1000);
    CHECK_EQ(qmi_mg(s.a[1]), -1000);
    CHECK_EQ(qmi_mg(s.a[2]), 500);
    CHECK_EQ(qmi_mdps(s.g[0]), 1000);
    CHECK_EQ(qmi_mdps(s.g[1]), -2000);
    CHECK_EQ(qmi_temp_c100(s.temp), 2550);
    /* bajo 0 °C (la librería de Arduino lo calcula mal) */
    CHECK_EQ(qmi_temp_c100((int16_t)0xFB00), -500);
    /* sin datos nuevos de los dos sensores */
    uint8_t r2[QMI_BURST_LEN];
    memcpy(r2, raw, sizeof(r2));
    r2[0] = 0x01;
    qmi_decode(r2, &s);
    CHECK(!s.fresh);
    /* contador de muestras de 24 bits con vuelta */
    CHECK_EQ(qmi_ts_delta(10, 11), 1);
    CHECK_EQ(qmi_ts_delta(0xFFFFFF, 0), 1);
    CHECK_EQ(qmi_ts_delta(0xFFFFFE, 3), 5);
    CHECK_EQ(qmi_ts_delta(5, 5), 0);
}

static void test_tilt(void)
{
    int32_t p, r;
    const int32_t flat[3] = {0, 0, 1000};
    CHECK(qmi_tilt(flat, &p, &r));
    CHECK_EQ(p, 0);
    CHECK_EQ(r, 0);
    const int32_t side[3] = {1000, 0, 0};
    CHECK(qmi_tilt(side, &p, &r));
    CHECK_EQ(p, 900);
    const int32_t t45[3] = {0, 707, 707};
    CHECK(qmi_tilt(t45, &p, &r));
    CHECK(r >= 449 && r <= 451);
    const int32_t fall[3] = {10, 20, 30};
    CHECK(!qmi_tilt(fall, &p, &r));
}

static void test_config(void)
{
    /* CTRL1 primero (autoincremento) y los sensores apagados antes de tocar
     * rangos y frecuencias */
    CHECK(qmi_config_len >= 5);
    CHECK_EQ(qmi_config[0].reg, QMI_REG_CTRL1);
    CHECK_EQ(qmi_config[0].val & 0x80, 0); /* nunca SIM (SPI de 3 hilos) */
    int ctrl7_off = -1, ctrl2 = -1;
    for (size_t i = 0; i < qmi_config_len; i++) {
        if (qmi_config[i].reg == QMI_REG_CTRL7 && qmi_config[i].val == 0) {
            ctrl7_off = (int)i;
        }
        if (qmi_config[i].reg == QMI_REG_CTRL2) {
            ctrl2 = (int)i;
        }
        CHECK(qmi_config[i].reg != 0x05 && qmi_config[i].reg != 0x07); /* reservados */
    }
    CHECK(ctrl7_off >= 0 && ctrl7_off < ctrl2);
    /* misma frecuencia para los dos sensores (si no, nunca están listos a la vez) */
    CHECK_EQ(QMI_CTRL2_VAL & 0x0F, QMI_CTRL3_VAL & 0x0F);
    uint8_t ok[9] = {QMI_CTRL1_RUN, QMI_CTRL2_VAL, QMI_CTRL3_VAL, 0, 0, 0, 0, 0, 0};
    CHECK(qmi_config_verify(ok));
    ok[2] = 0x53;
    CHECK(!qmi_config_verify(ok));
    ok[2] = QMI_CTRL3_VAL;
    ok[6] = 0x03; /* sensores ya encendidos */
    CHECK(!qmi_config_verify(ok));
}

/* Rumbo relativo: proyección del giro sobre la gravedad, sesgo en reposo y
 * deriva. */
static void feed(imu_heading_t *h, int32_t ax, int32_t ay, int32_t az, int32_t gx, int32_t gy, int32_t gz, int ms)
{
    const int32_t a[3] = {ax, ay, az}, g[3] = {gx, gy, gz};
    for (int t = 0; t < ms; t += 18) {
        imu_heading_update(h, a, g, 18);
    }
}

static void test_heading(void)
{
    imu_heading_t h;
    imu_heading_init(&h);
    /* Plano, quieto con un sesgo de 0,5 °/s en Z: se estima y no se integra */
    feed(&h, 0, 0, 1000, 0, 0, 500, 3000);
    CHECK(h.bias_ok);
    CHECK(h.yaw < 0.5f || h.yaw > 359.5f);
    CHECK(h.drift < 0.01f);
    CHECK(h.bias < -0.3f && h.bias > -0.6f); /* horario positivo: Z+ antihorario */
    /* Girar 90° en sentido horario (Z negativo, 45 °/s durante 2 s) + el sesgo */
    feed(&h, 0, 0, 1000, 0, 0, -45000 + 500, 2016);
    CHECK(h.yaw > 85.0f && h.yaw < 95.0f);
    CHECK(h.drift > 0.0f && h.drift < 5.0f);
    /* Inclinado 60°: el mismo giro alrededor de la vertical se mide igual */
    imu_heading_reset(&h);
    feed(&h, 0, 866, 500, 0, -38971, -22500, 2016);
    CHECK(h.yaw > 85.0f && h.yaw < 95.0f);
    /* Giro antihorario: el rumbo da la vuelta por 360 */
    imu_heading_reset(&h);
    feed(&h, 0, 0, 1000, 0, 0, 30000, 1008);
    CHECK(h.yaw > 325.0f && h.yaw < 335.0f);
    /* Sin gravedad (caída libre): no se integra y se avisa */
    const float y0 = h.yaw;
    feed(&h, 0, 0, 100, 0, 0, 90000, 500);
    CHECK(!h.valid && h.yaw == y0);
    /* Un hueco largo (muestras perdidas) no se integra */
    const int32_t a[3] = {0, 0, 1000}, g[3] = {0, 0, -90000};
    imu_heading_update(&h, a, g, 500);
    CHECK(h.yaw == y0);
    /* Sin sesgo estimado la incertidumbre crece más deprisa */
    imu_heading_t n;
    imu_heading_init(&n);
    feed(&n, 0, 0, 1000, 0, 0, -45000, 2016);
    CHECK(!n.bias_ok && n.drift > h.drift);
}

void test_imu_all(void)
{
    test_decode();
    test_tilt();
    test_config();
    test_heading();
}
