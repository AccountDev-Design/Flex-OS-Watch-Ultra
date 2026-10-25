/*
 * act_steps — ver include/act_steps.h
 */
#include "act_steps.h"

#include <math.h>
#include <string.h>

#define PI_F          3.14159265f
#define GRAV_TAU_S    1.5f   /* media lenta del módulo: la gravedad */
#define GRAV_CLAMP_MG 250.0f /* un golpe no arrastra la gravedad: entrada acotada */
#define LP_FC_HZ      4.0f   /* paso bajo de la parte dinámica */
#define TIME_GAIN     0.1f   /* ajuste de la marca del sensor al reloj monótono */
#define TIME_JUMP_MS  250    /* error mayor: el reloj y el contador no cuadran */
#define AXIS_CLIP_MG  3990   /* ±4 g: a partir de aquí el eje está saturado */
#define MAG_MIN_MG    50.0f  /* por debajo: caída libre o dato sin sentido */
#define STILL_RMS_MG  8      /* ventana de 1 s por debajo: quieto */
#define RUN_CADENCE   140    /* pasos por minuto */
#define RUN_CAD_AMP   120    /* ...o desde 120 con amplitud de carrera */
#define RUN_AMP_MG    900.0f
#define AMP_ALPHA     0.2f
#define EXIT_K        22     /* sin paso en 2,2 medianas: el tren termina */
#define SWING_MIN     0.18f  /* un paso baja de la gravedad al menos esta parte de su amplitud */

void act_step_params_default(act_step_params_t *p, int sensitivity)
{
    /* Amplitud mínima según la sensibilidad. Las de "normal" salen de las
     * señales de referencia de tests/host (caminar despacio en la muñeca da
     * unos 150-300 mg pico-valle tras el filtro); se ajustan con las pruebas
     * físicas T187-T192. */
    static const uint16_t kMinP2v[ACT_SENS_COUNT] = {160, 110, 75};
    if (sensitivity < 0 || sensitivity >= ACT_SENS_COUNT) {
        sensitivity = ACT_SENS_NORMAL;
    }
    p->min_p2v_mg = kMinP2v[sensitivity];
    p->max_p2v_mg = 5000;
    p->min_int_ms = 250;
    p->max_int_ms = 2000;
    p->confirm = 6;
    p->tol_pct = 35;
}

static void reset_signal(act_steps_t *d)
{
    d->warm = ACT_WARMUP_SAMPLES;
    d->seek_valley = false;
    d->have_valley = false;
    d->cand_n = 0;
    d->have_cand = false;
    d->walking = false;
    d->w_e = 0.0f;
    d->w_n = 0;
    d->w_t0 = d->t_us / 1000;
}

void act_steps_init(act_steps_t *d, const act_step_params_t *p)
{
    memset(d, 0, sizeof(*d));
    if (p) {
        d->p = *p;
    } else {
        act_step_params_default(&d->p, ACT_SENS_NORMAL);
    }
    d->motion = ACT_MOT_STILL;
    reset_signal(d);
}

void act_steps_set_params(act_steps_t *d, const act_step_params_t *p)
{
    if (p) {
        d->p = *p;
    }
}

void act_steps_gap(act_steps_t *d)
{
    d->gaps++;
    d->have = false; /* la próxima muestra vuelve a anclar el tiempo y los filtros */
    reset_signal(d);
}

int64_t act_steps_now(const act_steps_t *d)
{
    return d->t_us / 1000;
}

uint16_t act_steps_cadence(const act_steps_t *d)
{
    return (d->walking && d->med_int) ? (uint16_t)((60000u + d->med_int / 2) / d->med_int) : 0;
}

static uint16_t median_u16(const uint16_t *v, int n)
{
    uint16_t s[ACT_STEP_HIST];
    for (int i = 0; i < n; i++) {
        s[i] = v[i];
    }
    for (int i = 1; i < n; i++) { /* inserción: n <= 8 */
        const uint16_t x = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > x) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = x;
    }
    return n ? s[n / 2] : 0;
}

static void push_int(act_steps_t *d, uint16_t ms)
{
    d->ints[d->ints_i] = ms;
    d->ints_i = (uint8_t)((d->ints_i + 1) % ACT_STEP_HIST);
    if (d->ints_n < ACT_STEP_HIST) {
        d->ints_n++;
    }
    d->med_int = median_u16(d->ints, d->ints_n);
}

static bool is_run(const act_steps_t *d)
{
    const uint16_t cad = act_steps_cadence(d);
    return cad >= RUN_CADENCE || (cad >= RUN_CAD_AMP && d->amp >= RUN_AMP_MG);
}

static void count_step(act_steps_t *d, int64_t t)
{
    d->steps++;
    if (is_run(d)) {
        d->steps_run++;
    }
    d->last_step_t = t;
}

static void start_train(act_steps_t *d, int64_t t, float p2v)
{
    d->walking = false;
    d->cand_n = 1;
    d->cand_t[0] = t;
    d->cand_a[0] = p2v;
    d->have_cand = true;
    d->last_cand_t = t;
}

/* ¿Los últimos n candidatos tienen intervalos regulares? Devuelve su mediana. */
static uint16_t regular(const act_steps_t *d, int n)
{
    uint16_t iv[ACT_STEP_HIST];
    const int first = d->cand_n - n;
    for (int i = 0; i < n - 1; i++) {
        const int64_t dt = d->cand_t[first + i + 1] - d->cand_t[first + i];
        if (dt <= 0 || dt > 0xFFFF) {
            return 0;
        }
        iv[i] = (uint16_t)dt;
    }
    const uint16_t m = median_u16(iv, n - 1);
    if (m < d->p.min_int_ms || m > d->p.max_int_ms) {
        return 0;
    }
    const uint32_t lo = (uint32_t)m * (100u - d->p.tol_pct) / 100u;
    const uint32_t hi = (uint32_t)m * (100u + d->p.tol_pct) / 100u;
    for (int i = 0; i < n - 1; i++) {
        if (iv[i] < lo || iv[i] > hi) {
            return 0;
        }
    }
    return m;
}

static void on_peak(act_steps_t *d, float peak, int64_t t)
{
    if (!d->have_valley) {
        return; /* primer pico tras empezar: falta su valle */
    }
    const float p2v = peak - d->valley;
    float thr = (float)d->p.min_p2v_mg;
    if (d->walking && d->amp * 0.4f > thr) {
        thr = d->amp * 0.4f; /* umbral adaptado a la intensidad del tren */
    }
    if (p2v < thr) {
        return; /* oscilación pequeña: no es un paso */
    }
    /* Un paso oscila alrededor de la gravedad (el valle queda por debajo); un
     * golpe o un aplauso es un pico aislado sobre una muñeca quieta: su
     * "valle" apenas baja de la línea base. */
    if (-d->valley < p2v * SWING_MIN) {
        d->ignored++;
        return;
    }
    if (p2v > (float)d->p.max_p2v_mg && !d->walking) {
        d->impacts++; /* golpe aislado: rompe cualquier tren a medio confirmar */
        d->cand_n = 0;
        d->have_cand = false;
        return;
    }
    const int64_t iv = d->have_cand ? t - d->last_cand_t : INT64_MAX;
    if (iv < d->p.min_int_ms) {
        d->ignored++; /* segundo pico del mismo paso: no cuenta dos veces */
        return;
    }
    if (iv > d->p.max_int_ms) {
        start_train(d, t, p2v);
        return;
    }
    if (d->walking) {
        /* En un tren confirmado: un pico mucho antes de lo esperado es un
         * rebote del mismo paso. */
        if ((uint32_t)iv * 100u < (uint32_t)d->med_int * (100u - d->p.tol_pct)) {
            d->ignored++;
            return;
        }
        if ((uint32_t)iv * 10u <= (uint32_t)d->med_int * 16u) {
            push_int(d, (uint16_t)iv); /* la cadencia sigue al ritmo real */
        }
        d->amp += (p2v - d->amp) * AMP_ALPHA;
        d->last_cand_t = t;
        count_step(d, t);
        return;
    }
    /* Buscando un tren: candidato. */
    if (d->cand_n == ACT_STEP_HIST) {
        memmove(&d->cand_t[0], &d->cand_t[1], sizeof(d->cand_t[0]) * (ACT_STEP_HIST - 1));
        memmove(&d->cand_a[0], &d->cand_a[1], sizeof(d->cand_a[0]) * (ACT_STEP_HIST - 1));
        d->cand_n--;
    }
    d->cand_t[d->cand_n] = t;
    d->cand_a[d->cand_n] = p2v;
    d->cand_n++;
    d->last_cand_t = t;
    const int n = d->p.confirm;
    if (n < 3 || d->cand_n < n) {
        return;
    }
    const uint16_t m = regular(d, n);
    if (!m) {
        return;
    }
    /* Confirmado: los n candidatos eran pasos. */
    const int first = d->cand_n - n;
    d->ints_n = 0;
    d->ints_i = 0;
    float a = 0.0f;
    for (int i = first; i < d->cand_n; i++) {
        a += d->cand_a[i];
        if (i > first) {
            push_int(d, (uint16_t)(d->cand_t[i] - d->cand_t[i - 1]));
        }
    }
    d->amp = a / (float)n;
    d->walking = true;
    d->bout_start_t = d->cand_t[first];
    for (int i = 0; i < n; i++) {
        count_step(d, t);
    }
    d->cand_n = 0;
}

static void window(act_steps_t *d, int64_t t)
{
    if (t - d->w_t0 < 1000 || d->w_n == 0) {
        return;
    }
    d->rms = (uint16_t)lroundf(sqrtf(d->w_e / (float)d->w_n));
    d->w_e = 0.0f;
    d->w_n = 0;
    d->w_t0 = t;
    act_motion_t raw;
    if (d->walking) {
        raw = is_run(d) ? ACT_MOT_RUN : ACT_MOT_WALK;
    } else {
        raw = d->rms < STILL_RMS_MG ? ACT_MOT_STILL : ACT_MOT_LIGHT;
    }
    if (raw == d->motion) {
        d->pend_n = 0;
    } else {
        if (raw != d->pend_cls) {
            d->pend_cls = (uint8_t)raw;
            d->pend_n = 0;
        }
        d->pend_n++;
        /* Entrar en caminar/correr ya exigió `confirm` pasos regulares: basta
         * una ventana. Cambiar entre caminar y correr, o volver a quieto o
         * ligero, pide varias (sin alternar a cada segundo). */
        const bool both_steps =
            (raw == ACT_MOT_WALK || raw == ACT_MOT_RUN) && (d->motion == ACT_MOT_WALK || d->motion == ACT_MOT_RUN);
        const int need = (raw == ACT_MOT_WALK || raw == ACT_MOT_RUN) ? (both_steps ? 3 : 1) : 3;
        if (d->pend_n >= need) {
            d->motion = raw;
            d->pend_n = 0;
        }
    }
    if (d->motion == ACT_MOT_STILL) {
        if (d->still_s < UINT16_MAX) {
            d->still_s++;
        }
    } else {
        d->still_s = 0;
    }
}

void act_steps_feed(act_steps_t *d, const act_sample_t *s)
{
    /* 1. Validación y tiempo */
    if (d->have && !s->restart) {
        const uint32_t di = s->index - d->last_index;
        if (di == 0) {
            d->dups++; /* la misma muestra leída dos veces: no se procesa */
            return;
        }
        const int64_t dt_clock = s->t_ms - d->last_tms;
        const int64_t dt_sensor = (int64_t)di * ACT_SAMPLE_PERIOD_US / 1000;
        if (di > ACT_MAX_GAP_SAMPLES || di > 0x7FFFFFFFu || dt_clock < 0 ||
            dt_clock - dt_sensor > TIME_JUMP_MS || dt_sensor - dt_clock > TIME_JUMP_MS) {
            act_steps_gap(d);
        }
    } else if (d->have && s->restart) {
        act_steps_gap(d);
    }
    const int32_t ax = s->a[0], ay = s->a[1], az = s->a[2];
    const float m = sqrtf((float)(ax * ax + ay * ay + az * az));
    if (m < MAG_MIN_MG || m > 7000.0f) {
        d->invalid++;
        return; /* sin sentido físico (no cambia el estado) */
    }
    if (ax >= AXIS_CLIP_MG || ax <= -AXIS_CLIP_MG || ay >= AXIS_CLIP_MG || ay <= -AXIS_CLIP_MG ||
        az >= AXIS_CLIP_MG || az <= -AXIS_CLIP_MG) {
        d->clipped++; /* saturado: el módulo es una cota inferior; se usa igual */
    }
    float dt_s;
    if (!d->have) {
        d->have = true;
        d->t_us = s->t_ms * 1000;
        d->grav = m;
        d->lp1 = d->lp2 = 0.0f;
        d->w_t0 = s->t_ms;
        dt_s = ACT_SAMPLE_PERIOD_US / 1e6f;
    } else {
        const uint32_t di = s->index - d->last_index;
        d->t_us += (int64_t)di * ACT_SAMPLE_PERIOD_US;
        const int64_t err_us = s->t_ms * 1000 - d->t_us;
        d->t_us += (int64_t)((float)err_us * TIME_GAIN);
        dt_s = (float)di * (ACT_SAMPLE_PERIOD_US / 1e6f);
    }
    d->last_index = s->index;
    d->last_tms = s->t_ms;
    d->samples++;
    const int64_t t = d->t_us / 1000;

    /* 2. Gravedad y paso bajo (coeficientes con el dt real) */
    const float ag = dt_s / (GRAV_TAU_S + dt_s);
    float dg = m - d->grav;
    dg = dg > GRAV_CLAMP_MG ? GRAV_CLAMP_MG : (dg < -GRAV_CLAMP_MG ? -GRAV_CLAMP_MG : dg);
    d->grav += dg * ag;
    const float rc = 1.0f / (2.0f * PI_F * LP_FC_HZ);
    const float al = dt_s / (rc + dt_s);
    d->lp1 += ((m - d->grav) - d->lp1) * al;
    d->lp2 += (d->lp1 - d->lp2) * al;
    const float v = d->lp2;
    d->w_e += v * v;
    d->w_n++;

    if (d->warm) {
        d->warm--;
        if (!d->warm) {
            d->seek_valley = false;
            d->ext = v;
            d->ext_t = t;
            d->have_valley = false;
        }
        window(d, t);
        return;
    }

    /* 3. Picos y valles con histéresis */
    float h = (float)d->p.min_p2v_mg * 0.3f;
    if (d->walking && d->amp * 0.12f > h) {
        h = d->amp * 0.12f;
    }
    if (!d->seek_valley) {
        if (v > d->ext) {
            d->ext = v;
            d->ext_t = t;
        } else if (v < d->ext - h) {
            on_peak(d, d->ext, d->ext_t);
            d->seek_valley = true;
            d->ext = v;
            d->ext_t = t;
        }
    } else {
        if (v < d->ext) {
            d->ext = v;
            d->ext_t = t;
        } else if (v > d->ext + h) {
            d->valley = d->ext;
            d->have_valley = true;
            d->seek_valley = false;
            d->ext = v;
            d->ext_t = t;
        }
    }

    /* 4. Fin del tren: sin pasos durante 2,2 intervalos (y nunca más de max_int) */
    if (d->walking) {
        int64_t lim = (int64_t)d->med_int * EXIT_K / 10;
        if (lim < d->p.max_int_ms) {
            lim = d->p.max_int_ms;
        }
        if (t - d->last_cand_t > lim) {
            d->walking = false;
            d->cand_n = 0;
            d->have_cand = false;
        }
    } else if (d->have_cand && t - d->last_cand_t > d->p.max_int_ms) {
        d->cand_n = 0;
        d->have_cand = false;
    }
    window(d, t);
}
