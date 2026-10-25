/*
 * act_metrics — ver include/act_metrics.h
 */
#include "act_metrics.h"

#include <string.h>
#include "act_catalog.h"
#include "act_steps.h"

#define CFG_LEN 32

void act_config_default(act_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->tracking = true;
    c->auto_detect = true;
    c->sensitivity = ACT_SENS_NORMAL;
    act_auto_cfg_default(&c->autos);
    c->goal_kcal = 400;
    c->goal_ex_min = 30;
    c->goal_steps = 8000;
}

static bool clamp_u16(uint16_t *v, uint16_t lo, uint16_t hi)
{
    const uint16_t o = *v;
    *v = *v < lo ? lo : (*v > hi ? hi : *v);
    return *v != o;
}

/* Opcional: 0 = sin definir; si no, dentro del rango. */
static bool opt_u16(uint16_t *v, uint16_t lo, uint16_t hi)
{
    return *v ? clamp_u16(v, lo, hi) : false;
}

static bool opt_u8(uint8_t *v, uint8_t lo, uint8_t hi)
{
    if (!*v) {
        return false;
    }
    uint16_t w = *v;
    const bool ch = clamp_u16(&w, lo, hi);
    *v = (uint8_t)w;
    return ch;
}

static bool valid_id(uint8_t id)
{
    return id && act_catalog_find(id);
}

/* Quita ids vacíos, desconocidos o repetidos y compacta la lista. */
static bool clean_ids(uint8_t *v, int n)
{
    uint8_t out[ACT_FAV_MAX] = {0};
    int k = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < k; j++) {
            dup |= out[j] == v[i];
        }
        if (valid_id(v[i]) && !dup) {
            out[k++] = v[i];
        }
    }
    const bool ch = memcmp(out, v, (size_t)n) != 0;
    memcpy(v, out, (size_t)n);
    return ch;
}

bool act_config_normalize(act_config_t *c)
{
    bool ch = false;
    if (c->sensitivity >= ACT_SENS_COUNT) {
        c->sensitivity = ACT_SENS_NORMAL;
        ch = true;
    }
    ch |= clamp_u16(&c->autos.pause_after_s, ACT_PAUSE_MIN_S, ACT_PAUSE_MAX_S);
    ch |= clamp_u16(&c->autos.end_after_s, ACT_END_MIN_S, ACT_END_MAX_S);
    ch |= clamp_u16(&c->goal_kcal, ACT_GOAL_KCAL_MIN, ACT_GOAL_KCAL_MAX);
    ch |= clamp_u16(&c->goal_ex_min, ACT_GOAL_EX_MIN, ACT_GOAL_EX_MAX);
    ch |= clamp_u16(&c->goal_steps, ACT_GOAL_STEPS_MIN, ACT_GOAL_STEPS_MAX);
    ch |= opt_u16(&c->weight_dkg, ACT_WEIGHT_MIN_DKG, ACT_WEIGHT_MAX_DKG);
    ch |= opt_u8(&c->height_cm, ACT_HEIGHT_MIN_CM, ACT_HEIGHT_MAX_CM);
    ch |= opt_u8(&c->age, ACT_AGE_MIN, ACT_AGE_MAX);
    ch |= opt_u8(&c->stride_walk_cm, ACT_STRIDE_MIN_CM, ACT_STRIDE_MAX_CM);
    ch |= opt_u8(&c->stride_run_cm, ACT_STRIDE_MIN_CM, ACT_STRIDE_MAX_CM);
    ch |= clean_ids(c->fav, ACT_FAV_MAX);
    ch |= clean_ids(c->recent, ACT_RECENT_MAX);
    if (c->calib_walk_err < -100 || c->calib_walk_err > 100) {
        c->calib_walk_err = 0;
        ch = true;
    }
    if (c->calib_run_err < -100 || c->calib_run_err > 100) {
        c->calib_run_err = 0;
        ch = true;
    }
    return ch;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

size_t act_config_pack(const act_config_t *c, uint8_t *o, size_t cap)
{
    if (cap < CFG_LEN) {
        return 0;
    }
    memset(o, 0, CFG_LEN);
    o[0] = (uint8_t)((c->tracking ? 0x01 : 0) | (c->auto_detect ? 0x02 : 0) | (c->autos.auto_pause ? 0x04 : 0) |
                     (c->autos.auto_resume ? 0x08 : 0) | (c->autos.auto_end ? 0x10 : 0));
    o[1] = c->sensitivity;
    put16(&o[2], c->autos.pause_after_s);
    put16(&o[4], c->autos.end_after_s);
    put16(&o[6], c->goal_kcal);
    put16(&o[8], c->goal_ex_min);
    put16(&o[10], c->goal_steps);
    put16(&o[12], c->weight_dkg);
    o[14] = c->height_cm;
    o[15] = c->age;
    o[16] = c->stride_walk_cm;
    o[17] = c->stride_run_cm;
    memcpy(&o[18], c->fav, ACT_FAV_MAX);
    memcpy(&o[26], c->recent, ACT_RECENT_MAX);
    o[30] = (uint8_t)c->calib_walk_err;
    o[31] = (uint8_t)c->calib_run_err;
    return CFG_LEN;
}

bool act_config_unpack(const uint8_t *in, size_t n, uint8_t ver, act_config_t *c)
{
    act_config_default(c);
    if (ver != ACT_CFG_VER || n != CFG_LEN || !in) {
        return false;
    }
    c->tracking = in[0] & 0x01;
    c->auto_detect = in[0] & 0x02;
    c->autos.auto_pause = in[0] & 0x04;
    c->autos.auto_resume = in[0] & 0x08;
    c->autos.auto_end = in[0] & 0x10;
    c->sensitivity = in[1];
    c->autos.pause_after_s = get16(&in[2]);
    c->autos.end_after_s = get16(&in[4]);
    c->goal_kcal = get16(&in[6]);
    c->goal_ex_min = get16(&in[8]);
    c->goal_steps = get16(&in[10]);
    c->weight_dkg = get16(&in[12]);
    c->height_cm = in[14];
    c->age = in[15];
    c->stride_walk_cm = in[16];
    c->stride_run_cm = in[17];
    memcpy(c->fav, &in[18], ACT_FAV_MAX);
    memcpy(c->recent, &in[26], ACT_RECENT_MAX);
    c->calib_walk_err = (int8_t)in[30];
    c->calib_run_err = (int8_t)in[31];
    act_config_normalize(c);
    return true;
}

bool act_config_is_fav(const act_config_t *c, uint8_t id)
{
    for (int i = 0; i < ACT_FAV_MAX; i++) {
        if (id && c->fav[i] == id) {
            return true;
        }
    }
    return false;
}

bool act_config_toggle_fav(act_config_t *c, uint8_t id)
{
    if (!valid_id(id)) {
        return false;
    }
    for (int i = 0; i < ACT_FAV_MAX; i++) {
        if (c->fav[i] == id) {
            c->fav[i] = 0;
            clean_ids(c->fav, ACT_FAV_MAX);
            return true;
        }
    }
    for (int i = 0; i < ACT_FAV_MAX; i++) {
        if (!c->fav[i]) {
            c->fav[i] = id;
            return true;
        }
    }
    return false;
}

void act_config_push_recent(act_config_t *c, uint8_t id)
{
    if (!valid_id(id)) {
        return;
    }
    uint8_t out[ACT_RECENT_MAX] = {id};
    int k = 1;
    for (int i = 0; i < ACT_RECENT_MAX && k < ACT_RECENT_MAX; i++) {
        if (c->recent[i] && c->recent[i] != id) {
            out[k++] = c->recent[i];
        }
    }
    memcpy(c->recent, out, sizeof(out));
}

uint16_t act_stride_cm(const act_config_t *c, bool run, act_stride_src_t *src)
{
    const uint8_t user = run ? c->stride_run_cm : c->stride_walk_cm;
    act_stride_src_t s;
    uint16_t cm;
    if (user) {
        s = ACT_STRIDE_USER;
        cm = user;
    } else if (c->height_cm) {
        s = ACT_STRIDE_HEIGHT;
        cm = (uint16_t)(((uint32_t)c->height_cm * (run ? 650u : 415u) + 500u) / 1000u);
    } else {
        s = ACT_STRIDE_GENERIC;
        cm = run ? 100 : 70;
    }
    if (src) {
        *src = s;
    }
    return cm;
}

uint16_t act_calibrate_stride(uint32_t real_m, uint32_t steps)
{
    if (steps < 50 || real_m == 0 || real_m > 50000) {
        return 0;
    }
    const uint32_t cm = (real_m * 100u + steps / 2u) / steps;
    return (cm >= ACT_STRIDE_MIN_CM && cm <= ACT_STRIDE_MAX_CM) ? (uint16_t)cm : 0;
}

int act_calib_error_pct(uint32_t est_m, uint32_t real_m)
{
    if (!real_m) {
        return 0;
    }
    int64_t e = ((int64_t)est_m - (int64_t)real_m) * 100 / (int64_t)real_m;
    return (int)(e < -100 ? -100 : (e > 100 ? 100 : e));
}

uint32_t act_ee_steps(uint32_t dist_cm, bool run)
{
    /* 0,0005 kcal/kg/m = 5 ukcal/kg/cm al caminar; el doble al correr. */
    return dist_cm * (run ? 10u : 5u);
}

uint32_t act_ee_met(uint8_t met10, uint32_t active_ms)
{
    if (met10 <= 10) {
        return 0;
    }
    /* (MET - 1) kcal/kg/h -> ukcal/kg: (met10 - 10)/10 * 1e6 * ms/3 600 000 */
    return (uint32_t)(((uint64_t)(met10 - 10u) * active_ms + 18u) / 36u);
}

bool act_kcal(const act_config_t *c, uint32_t ee, uint32_t *kcal)
{
    if (!c->weight_dkg) {
        *kcal = 0;
        return false;
    }
    /* ee (ukcal/kg) x peso (dkg / 10) / 1e6 */
    *kcal = (uint32_t)(((uint64_t)ee * c->weight_dkg + 5000000u) / 10000000u);
    return true;
}

static uint16_t pct(uint32_t v, uint32_t goal)
{
    if (!goal) {
        return 0;
    }
    const uint32_t p = (uint32_t)(((uint64_t)v * 100u) / goal);
    return (uint16_t)(p > 999 ? 999 : p);
}

void act_rings(const act_config_t *c, uint32_t kcal, bool kcal_valid, uint32_t ex_s, uint32_t steps,
               act_rings_t *out)
{
    out->kcal_valid = kcal_valid;
    out->pct[0] = kcal_valid ? pct(kcal, c->goal_kcal) : 0;
    out->pct[1] = pct(ex_s / 60u, c->goal_ex_min);
    out->pct[2] = pct(steps, c->goal_steps);
    out->met_bits = (uint8_t)((out->pct[0] >= 100 ? 1 : 0) | (out->pct[1] >= 100 ? 2 : 0) |
                              (out->pct[2] >= 100 ? 4 : 0));
}
