/*
 * imu_heading — ver include/imu_heading.h
 */
#include "imu_heading.h"

#include <math.h>
#include <string.h>

#define BIAS_ALPHA      0.02f  /* filtro del sesgo en reposo */
#define DRIFT_NOBIAS    0.30f  /* °/s de incertidumbre sin sesgo estimado */
#define DRIFT_BIAS      0.03f  /* °/s con sesgo estimado */
#define DRIFT_SCALE     0.02f  /* error de escala: 2 % de lo girado */

void imu_heading_init(imu_heading_t *h)
{
    memset(h, 0, sizeof(*h));
}

void imu_heading_reset(imu_heading_t *h)
{
    h->yaw = 0.0f;
    h->drift = 0.0f;
}

void imu_heading_update(imu_heading_t *h, const int32_t acc_mg[3], const int32_t gyro_mdps[3], uint32_t dt_ms)
{
    const float ax = (float)acc_mg[0], ay = (float)acc_mg[1], az = (float)acc_mg[2];
    const float an = sqrtf(ax * ax + ay * ay + az * az);
    h->valid = an >= IMU_HDG_GRAV_MIN_MG && an <= IMU_HDG_GRAV_MAX_MG;
    if (!h->valid || dt_ms == 0 || dt_ms > IMU_HDG_MAX_DT_MS) {
        h->still_ms = 0;
        return;
    }
    const float gx = (float)gyro_mdps[0] / 1000.0f, gy = (float)gyro_mdps[1] / 1000.0f,
                gz = (float)gyro_mdps[2] / 1000.0f;
    /* Giro alrededor de la vertical, en sentido horario visto desde arriba. */
    const float rate = -(ax * gx + ay * gy + az * gz) / an;
    const float gmag = sqrtf(gx * gx + gy * gy + gz * gz);
    const float dt = (float)dt_ms / 1000.0f;
    const bool still = (gmag - fabsf(h->bias)) < IMU_HDG_STILL_DPS && fabsf(an - 1000.0f) < IMU_HDG_STILL_MG;
    if (still) {
        h->still_ms += dt_ms;
        h->bias += (rate - h->bias) * BIAS_ALPHA;
        if (h->still_ms >= IMU_HDG_CAL_MS) {
            h->bias_ok = true;
        }
        return; /* quieto: no se integra (la deriva no crece) */
    }
    h->still_ms = 0;
    const float d = (rate - h->bias) * dt;
    h->yaw = fmodf(h->yaw + d, 360.0f);
    if (h->yaw < 0.0f) {
        h->yaw += 360.0f;
    }
    h->drift += (h->bias_ok ? DRIFT_BIAS : DRIFT_NOBIAS) * dt + DRIFT_SCALE * fabsf(d);
    if (h->drift > 180.0f) {
        h->drift = 180.0f;
    }
}
