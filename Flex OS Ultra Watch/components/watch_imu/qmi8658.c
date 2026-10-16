/*
 * qmi8658 — ver include/qmi8658.h
 */
#include "qmi8658.h"

#include <math.h>

const qmi_write_t qmi_config[] = {
    {QMI_REG_CTRL1, QMI_CTRL1_RUN}, /* autoincremento (ráfagas), BE por defecto */
    {QMI_REG_CTRL7, 0x00},          /* sensores apagados mientras se configura */
    {QMI_REG_CTRL2, QMI_CTRL2_VAL},
    {QMI_REG_CTRL3, QMI_CTRL3_VAL},
    {QMI_REG_CTRL5, 0x00},          /* sin filtro paso bajo propio */
    {QMI_REG_CTRL8, 0x00},          /* sin movimiento, podómetro ni toques */
};
const size_t qmi_config_len = sizeof(qmi_config) / sizeof(qmi_config[0]);

bool qmi_config_verify(const uint8_t r[9])
{
    /* r[0] = 0x02 (CTRL1) ... r[8] = 0x0A (CTRL9) */
    return r[0] == QMI_CTRL1_RUN && r[1] == QMI_CTRL2_VAL && r[2] == QMI_CTRL3_VAL && r[4] == 0x00 && r[6] == 0x00 &&
           r[7] == 0x00;
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void qmi_decode(const uint8_t raw[QMI_BURST_LEN], qmi_sample_t *out)
{
    out->fresh = (raw[0] & 0x03) == 0x03;
    out->ts = (uint32_t)raw[2] | ((uint32_t)raw[3] << 8) | ((uint32_t)raw[4] << 16);
    out->temp = le16(&raw[5]);
    for (int i = 0; i < 3; i++) {
        out->a[i] = le16(&raw[7 + 2 * i]);
        out->g[i] = le16(&raw[13 + 2 * i]);
    }
}

uint32_t qmi_ts_delta(uint32_t prev, uint32_t now)
{
    return (now - prev) & 0xFFFFFFu;
}

int32_t qmi_mg(int16_t raw)
{
    return (int32_t)raw * 1000 / 8192;
}

int32_t qmi_mdps(int16_t raw)
{
    return (int32_t)raw * 1000 / 64;
}

int32_t qmi_temp_c100(int16_t raw)
{
    return (int32_t)raw * 100 / 256;
}

bool qmi_tilt(const int32_t mg[3], int32_t *pitch_d10, int32_t *roll_d10)
{
    const double x = mg[0], y = mg[1], z = mg[2];
    const double n = sqrt(x * x + y * y + z * z);
    if (n < 300.0) {
        return false; /* caída libre o dato sin sentido */
    }
    const double rad = 1800.0 / 3.14159265358979323846;
    *pitch_d10 = (int32_t)lround(atan2(x, sqrt(y * y + z * z)) * rad);
    *roll_d10 = (int32_t)lround(atan2(y, sqrt(x * x + z * z)) * rad);
    return true;
}
