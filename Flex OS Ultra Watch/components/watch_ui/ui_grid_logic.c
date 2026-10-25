/*
 * ui_grid_logic — ver include/ui_grid_logic.h
 */
#include "ui_grid_logic.h"

#include <math.h>
#include <string.h>

static int mod4(int v)
{
    const int m = v % GRID_TORUS;
    return m < 0 ? m + GRID_TORUS : m;
}

int grid_slot(int q, int r)
{
    return mod4(q) + GRID_TORUS * mod4(r);
}

grid_vec_t grid_cell_pos(int q, int r)
{
    const grid_vec_t p = {GRID_PITCH * ((float)q + (float)r * 0.5f), GRID_ROW * (float)r};
    return p;
}

void grid_nearest_cell(float x, float y, int *q, int *r)
{
    /* Coordenadas axiales fraccionarias y redondeo cúbico (x + y + z = 0). */
    const float fr = y / GRID_ROW;
    const float fq = x / GRID_PITCH - fr * 0.5f;
    const float fs = -fq - fr;
    float rq = roundf(fq), rr = roundf(fr), rs = roundf(fs);
    const float dq = fabsf(rq - fq), dr = fabsf(rr - fr), ds = fabsf(rs - fs);
    if (dq > dr && dq > ds) {
        rq = -rr - rs;
    } else if (dr > ds) {
        rr = -rq - rs;
    }
    *q = (int)rq;
    *r = (int)rr;
}

void grid_nearest_copy(int slot, float x, float y, int *q, int *r)
{
    const int sq = slot % GRID_TORUS, sr = slot / GRID_TORUS;
    int cq, cr;
    grid_nearest_cell(x, y, &cq, &cr);
    float best = 1e30f;
    /* Las copias de un hueco están cada 4 celdas: basta mirar las del entorno. */
    for (int dr = -GRID_TORUS; dr <= GRID_TORUS; dr++) {
        for (int dq = -GRID_TORUS; dq <= GRID_TORUS; dq++) {
            const int tq = cq + dq, tr = cr + dr;
            if (mod4(tq) != sq || mod4(tr) != sr) {
                continue;
            }
            const grid_vec_t p = grid_cell_pos(tq, tr);
            const float d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
            if (d < best) {
                best = d;
                *q = tq;
                *r = tr;
            }
        }
    }
}

float grid_lens_scale(float d)
{
    if (d >= GRID_LENS_R) {
        return GRID_MIN_SCALE;
    }
    const float c = 0.5f * (1.0f + cosf(3.14159265f * d / GRID_LENS_R));
    return GRID_MIN_SCALE + (1.0f - GRID_MIN_SCALE) * c;
}

float grid_visibility(float d)
{
    if (d <= GRID_FADE_R) {
        return 1.0f;
    }
    if (d >= GRID_VIS_R) {
        return 0.0f;
    }
    const float u = (GRID_VIS_R - d) / (GRID_VIS_R - GRID_FADE_R); /* 1 -> 0 */
    return u * u * (3.0f - 2.0f * u);                               /* suave */
}

int grid_layout(grid_vec_t off, grid_vec_t lens, grid_vec_t center, grid_item_t *out, int max)
{
    int cq, cr;
    grid_nearest_cell(-off.x, -off.y, &cq, &cr); /* celda bajo el centro de la vista */
    int n = 0;
    for (int dr = -3; dr <= 3; dr++) {
        for (int dq = -3; dq <= 3; dq++) {
            const int q = cq + dq, r = cr + dr;
            const grid_vec_t p = grid_cell_pos(q, r);
            const float bx = p.x + off.x, by = p.y + off.y; /* respecto al centro, sin lupa */
            const float dc = sqrtf(bx * bx + by * by);
            const float vis = grid_visibility(dc);
            const uint8_t opa = (uint8_t)(255.0f * vis + 0.5f);
            if (opa == 0 || n >= max) {
                continue; /* fuera de la zona visible (o ya invisible al redondear) */
            }
            /* Lupa: distancia al punto de interacción, en pantalla */
            const float sx = center.x + bx, sy = center.y + by;
            const float lx = sx - lens.x, ly = sy - lens.y;
            const float dl = sqrtf(lx * lx + ly * ly);
            float x = sx, y = sy;
            if (dl > 0.5f && dl < GRID_PUSH_R) {
                const float push = GRID_PUSH_PX * sinf(3.14159265f * dl / GRID_PUSH_R);
                x += lx / dl * push;
                y += ly / dl * push;
            }
            grid_item_t *it = &out[n++];
            it->q = q;
            it->r = r;
            it->slot = grid_slot(q, r);
            it->x = x;
            it->y = y;
            it->scale = grid_lens_scale(dl) * (0.55f + 0.45f * vis);
            it->opa = opa;
        }
    }
    return n;
}

void grid_spring_step(grid_vec_t *pos, grid_vec_t *vel, grid_vec_t target, float omega, float dt)
{
    /* x'' = -w²(x - t) - 2w·x' (amortiguamiento crítico), Euler semiimplícito
     * en subpasos de 4 ms como mucho: estable para cualquier dt de cuadro. */
    while (dt > 0.0f) {
        const float h = dt > 0.004f ? 0.004f : dt;
        dt -= h;
        vel->x += (-omega * omega * (pos->x - target.x) - 2.0f * omega * vel->x) * h;
        vel->y += (-omega * omega * (pos->y - target.y) - 2.0f * omega * vel->y) * h;
        pos->x += vel->x * h;
        pos->y += vel->y * h;
    }
}

void grid_inertia_step(grid_vec_t *pos, grid_vec_t *vel, float dt)
{
    /* Solución exacta de v' = -v / tau: distancia recorrida v·tau·(1 - e^(-dt/tau)) */
    const float k = expf(-dt / GRID_FRICTION_TAU);
    pos->x += vel->x * GRID_FRICTION_TAU * (1.0f - k);
    pos->y += vel->y * GRID_FRICTION_TAU * (1.0f - k);
    vel->x *= k;
    vel->y *= k;
}

grid_vec_t grid_snap_target(grid_vec_t off)
{
    int q, r;
    grid_nearest_cell(-off.x, -off.y, &q, &r);
    const grid_vec_t p = grid_cell_pos(q, r);
    const grid_vec_t t = {-p.x, -p.y};
    return t;
}

void grid_vel_reset(grid_vel_t *v)
{
    memset(v, 0, sizeof(*v));
}

void grid_vel_add(grid_vel_t *v, float x, float y, uint32_t t_ms)
{
    v->head = (uint8_t)((v->head + 1) % GRID_VEL_SAMPLES);
    v->p[v->head].x = x;
    v->p[v->head].y = y;
    v->t[v->head] = t_ms;
    if (v->n < GRID_VEL_SAMPLES) {
        v->n++;
    }
}

grid_vec_t grid_vel_get(const grid_vel_t *v, uint32_t now_ms)
{
    grid_vec_t z = {0, 0};
    if (v->n < 2 || now_ms - v->t[v->head] > 80u) {
        return z; /* sin muestras o el dedo se quedó quieto antes de soltar */
    }
    /* La muestra más reciente con al menos GRID_VEL_WINDOW_MS de antigüedad
     * (o la más antigua guardada). */
    int idx = v->head;
    for (int k = 1; k < v->n; k++) {
        idx = (v->head - k + GRID_VEL_SAMPLES) % GRID_VEL_SAMPLES;
        if (v->t[v->head] - v->t[idx] >= GRID_VEL_WINDOW_MS) {
            break;
        }
    }
    const uint32_t dt = v->t[v->head] - v->t[idx];
    if (dt == 0) {
        return z;
    }
    grid_vec_t r = {(v->p[v->head].x - v->p[idx].x) * 1000.0f / (float)dt,
                    (v->p[v->head].y - v->p[idx].y) * 1000.0f / (float)dt};
    const float s = sqrtf(r.x * r.x + r.y * r.y);
    if (s > GRID_MAX_SPEED) {
        r.x *= GRID_MAX_SPEED / s;
        r.y *= GRID_MAX_SPEED / s;
    }
    return r;
}
