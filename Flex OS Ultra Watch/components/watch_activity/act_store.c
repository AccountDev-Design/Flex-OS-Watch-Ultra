/*
 * act_store — ver include/act_store.h
 */
#define _DEFAULT_SOURCE /* fileno/fsync/stat/mkdir con -std=c11 en el PC */
#include "act_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ws_logic.h"

#define PATH_MAX_LEN 200
#define SES_DEDUP    8   /* ids recientes con los que se compara al añadir y reparar */

/* ---- Bytes ---- */

static void p16(uint8_t **p, uint16_t v)
{
    (*p)[0] = (uint8_t)v;
    (*p)[1] = (uint8_t)(v >> 8);
    *p += 2;
}

static void p32(uint8_t **p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        (*p)[i] = (uint8_t)(v >> (8 * i));
    }
    *p += 4;
}

static void p64(uint8_t **p, int64_t v)
{
    p32(p, (uint32_t)((uint64_t)v & 0xFFFFFFFFu));
    p32(p, (uint32_t)((uint64_t)v >> 32));
}

static void p8(uint8_t **p, uint8_t v)
{
    *(*p)++ = v;
}

static uint16_t g16(const uint8_t **p)
{
    const uint16_t v = (uint16_t)((*p)[0] | ((*p)[1] << 8));
    *p += 2;
    return v;
}

static uint32_t g32(const uint8_t **p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)(*p)[i] << (8 * i);
    }
    *p += 4;
    return v;
}

static int64_t g64(const uint8_t **p)
{
    const uint64_t lo = g32(p), hi = g32(p);
    return (int64_t)(lo | (hi << 32));
}

static uint8_t g8(const uint8_t **p)
{
    return *(*p)++;
}

/* Cabecera y CRC de un registro de len bytes (el CRC son los 4 últimos). */
static void seal(uint8_t *rec, size_t len, char kind)
{
    rec[0] = (uint8_t)kind;
    rec[1] = ACT_REC_VER;
    const uint32_t crc = ws_crc32(rec, len - 4);
    uint8_t *p = rec + len - 4;
    p32(&p, crc);
}

static bool check(const uint8_t *rec, size_t len, char kind)
{
    if (rec[0] != (uint8_t)kind || rec[1] != ACT_REC_VER) {
        return false;
    }
    const uint8_t *p = rec + len - 4;
    return g32(&p) == ws_crc32(rec, len - 4);
}

/* ---- Día ---- */

static void day_body(const act_day_t *d, uint8_t **p)
{
    p32(p, (uint32_t)d->day);
    p32(p, d->steps);
    p32(p, d->steps_run);
    p32(p, d->walk_cm);
    p32(p, d->run_cm);
    p32(p, d->ee);
    p32(p, d->ex_wk_s);
    p32(p, d->ex_auto_s);
    p32(p, d->move_s);
    p8(p, (uint8_t)d->hours);
    p8(p, (uint8_t)(d->hours >> 8));
    p8(p, (uint8_t)(d->hours >> 16));
    p16(p, d->kcal);
    p8(p, d->workouts);
    p8(p, d->goal_bits);
    p8(p, d->flags);
}

static void day_read(const uint8_t **p, act_day_t *d)
{
    d->day = (int32_t)g32(p);
    d->steps = g32(p);
    d->steps_run = g32(p);
    d->walk_cm = g32(p);
    d->run_cm = g32(p);
    d->ee = g32(p);
    d->ex_wk_s = g32(p);
    d->ex_auto_s = g32(p);
    d->move_s = g32(p);
    d->hours = g8(p);
    d->hours |= (uint32_t)g8(p) << 8;
    d->hours |= (uint32_t)g8(p) << 16;
    d->kcal = g16(p);
    d->workouts = g8(p);
    d->goal_bits = g8(p);
    d->flags = g8(p);
}

size_t act_day_pack(const act_day_t *d, uint8_t out[ACT_DAY_REC_LEN])
{
    memset(out, 0, ACT_DAY_REC_LEN);
    uint8_t *p = out + 2;
    day_body(d, &p);
    seal(out, ACT_DAY_REC_LEN, ACT_REC_DAY);
    return ACT_DAY_REC_LEN;
}

/* Valores imposibles en un registro con CRC correcto (otra versión del
 * código, por ejemplo): no se usan. */
static bool day_sane(const act_day_t *d)
{
    return d->steps_run <= d->steps && d->steps <= 300000u && d->ex_wk_s <= 86400u * 2 &&
           d->ex_auto_s <= 86400u && d->move_s <= 86400u && d->hours < (1u << 24);
}

bool act_day_unpack(const uint8_t in[ACT_DAY_REC_LEN], act_day_t *d)
{
    if (!check(in, ACT_DAY_REC_LEN, ACT_REC_DAY)) {
        return false;
    }
    const uint8_t *p = in + 2;
    day_read(&p, d);
    return day_sane(d);
}

/* ---- Sesión ---- */

void act_ses_from(const act_workout_t *w, const act_config_t *c, act_ses_rec_t *r)
{
    memset(r, 0, sizeof(*r));
    const act_session_t *s = &w->s;
    const int64_t end = s->state == ACT_SES_COMPLETED ? s->end_ms : s->seg_ms;
    r->id = s->id;
    r->type = s->type;
    r->end_reason = (uint8_t)s->end_reason;
    r->pauses = s->pauses;
    r->auto_pauses = s->auto_pauses;
    r->auto_resumes = s->auto_resumes;
    r->start_utc = w->start_utc;
    r->day = w->day;
    r->active_s = act_session_active_ms(s, end) / 1000u;
    r->paused_s = act_session_paused_ms(s, end) / 1000u;
    r->total_s = r->active_s + r->paused_s;
    r->steps = w->steps;
    r->dist_m = w->dist_cm / 100u;
    r->ee = w->ee_steps + w->ee_met;
    uint32_t kcal = 0;
    r->kcal_valid = act_kcal(c, r->ee, &kcal);
    r->kcal = (uint16_t)(kcal > 0xFFFF ? 0xFFFF : kcal);
    if (w->steps && r->active_s >= 60) {
        const uint32_t cad = (uint32_t)((uint64_t)w->steps * 60u / r->active_s);
        r->avg_cadence = (uint16_t)(cad > 400 ? 400 : cad);
    }
    r->max_cadence = w->max_cadence;
    r->motion_mg = w->motion_n ? (uint16_t)(w->motion_sum / w->motion_n) : 0;
}

size_t act_ses_pack(const act_ses_rec_t *r, uint8_t out[ACT_SES_REC_LEN])
{
    memset(out, 0, ACT_SES_REC_LEN);
    uint8_t *p = out + 2;
    p32(&p, r->id);
    p8(&p, r->type);
    p8(&p, r->end_reason);
    p8(&p, r->pauses);
    p8(&p, r->auto_pauses);
    p8(&p, r->auto_resumes);
    p64(&p, r->start_utc);
    p32(&p, (uint32_t)r->day);
    p32(&p, r->total_s);
    p32(&p, r->active_s);
    p32(&p, r->paused_s);
    p32(&p, r->steps);
    p32(&p, r->dist_m);
    p32(&p, r->ee);
    p16(&p, r->kcal);
    p8(&p, r->kcal_valid ? 1 : 0);
    p16(&p, r->avg_cadence);
    p16(&p, r->max_cadence);
    p16(&p, r->motion_mg);
    seal(out, ACT_SES_REC_LEN, ACT_REC_SES);
    return ACT_SES_REC_LEN;
}

bool act_ses_unpack(const uint8_t in[ACT_SES_REC_LEN], act_ses_rec_t *r)
{
    if (!check(in, ACT_SES_REC_LEN, ACT_REC_SES)) {
        return false;
    }
    const uint8_t *p = in + 2;
    r->id = g32(&p);
    r->type = g8(&p);
    r->end_reason = g8(&p);
    r->pauses = g8(&p);
    r->auto_pauses = g8(&p);
    r->auto_resumes = g8(&p);
    r->start_utc = g64(&p);
    r->day = (int32_t)g32(&p);
    r->total_s = g32(&p);
    r->active_s = g32(&p);
    r->paused_s = g32(&p);
    r->steps = g32(&p);
    r->dist_m = g32(&p);
    r->ee = g32(&p);
    r->kcal = g16(&p);
    r->kcal_valid = g8(&p) != 0;
    r->avg_cadence = g16(&p);
    r->max_cadence = g16(&p);
    r->motion_mg = g16(&p);
    return r->id != 0 && r->active_s + r->paused_s == r->total_s && r->total_s <= 7u * 86400u;
}

/* ---- Punto de control ---- */

size_t act_state_pack(const act_engine_t *e, int64_t now_ms, int64_t utc_s, uint8_t *out, size_t cap)
{
    const size_t len = 2 + 8 + 4 + ACT_DAY_REC_LEN + 1 + 71 + 4;
    if (cap < len || len > ACT_STATE_MAX) {
        return 0;
    }
    memset(out, 0, len);
    uint8_t *p = out + 2;
    p64(&p, utc_s);
    p32(&p, e->next_id);
    act_day_pack(&e->day, p);
    p += ACT_DAY_REC_LEN;
    const act_workout_t *w = &e->wk;
    const bool run = act_session_running(&w->s);
    p8(&p, run ? 1 : 0);
    if (run) {
        p8(&p, (uint8_t)w->s.state);
        p8(&p, w->s.type);
        p8(&p, w->s.auto_ok ? 1 : 0);
        p32(&p, w->s.id);
        p8(&p, (uint8_t)w->s.pause_kind);
        p8(&p, (uint8_t)w->s.end_reason);
        p8(&p, w->s.pauses);
        p8(&p, w->s.auto_pauses);
        p8(&p, w->s.auto_resumes);
        /* tramos cerrados al momento del guardado */
        p32(&p, act_session_active_ms(&w->s, now_ms));
        p32(&p, act_session_paused_ms(&w->s, now_ms));
        p64(&p, w->start_utc);
        p32(&p, (uint32_t)w->day);
        p32(&p, w->steps);
        p32(&p, w->steps_run);
        p32(&p, w->dist_cm);
        p32(&p, w->ee_steps);
        p32(&p, w->ee_met);
        p32(&p, w->motion_sum);
        p32(&p, w->motion_n);
        p16(&p, w->max_cadence);
        p32(&p, w->base_ee);
        p32(&p, w->base_ex_ms);
    }
    seal(out, len, ACT_REC_STATE);
    return len;
}

bool act_state_unpack(const uint8_t *in, size_t n, act_day_t *day, act_workout_t *wk, uint32_t *next_id,
                      int64_t *saved_utc)
{
    const size_t len = 2 + 8 + 4 + ACT_DAY_REC_LEN + 1 + 71 + 4;
    if (n != len || !check(in, len, ACT_REC_STATE)) {
        return false;
    }
    const uint8_t *p = in + 2;
    *saved_utc = g64(&p);
    *next_id = g32(&p);
    if (!act_day_unpack(p, day)) {
        return false;
    }
    p += ACT_DAY_REC_LEN;
    memset(wk, 0, sizeof(*wk));
    if (g8(&p)) {
        const uint8_t st = g8(&p);
        if (st != ACT_SES_ACTIVE && st != ACT_SES_PAUSED) {
            return false;
        }
        wk->s.state = (act_ses_state_t)st;
        wk->s.type = g8(&p);
        wk->s.auto_ok = g8(&p) != 0;
        wk->s.id = g32(&p);
        wk->s.pause_kind = (act_pause_t)g8(&p);
        wk->s.end_reason = (act_end_t)g8(&p);
        wk->s.pauses = g8(&p);
        wk->s.auto_pauses = g8(&p);
        wk->s.auto_resumes = g8(&p);
        wk->s.active_ms = g32(&p);
        wk->s.paused_ms = g32(&p);
        wk->start_utc = g64(&p);
        wk->day = (int32_t)g32(&p);
        wk->steps = g32(&p);
        wk->steps_run = g32(&p);
        wk->dist_cm = g32(&p);
        wk->ee_steps = g32(&p);
        wk->ee_met = g32(&p);
        wk->motion_sum = g32(&p);
        wk->motion_n = g32(&p);
        wk->max_cadence = g16(&p);
        wk->base_ee = g32(&p);
        wk->base_ex_ms = g32(&p);
        if (!wk->s.id || !act_catalog_find(wk->s.type) || wk->s.pause_kind > ACT_PAUSE_RESTORED ||
            wk->s.active_ms > 7u * 86400000u) {
            return false;
        }
    }
    return true;
}

/* ---- Archivos ---- */

static bool path(char *out, const char *dir, const char *name)
{
    return snprintf(out, PATH_MAX_LEN, "%s/%s", dir, name) < PATH_MAX_LEN;
}

bool act_hist_mkdir(const char *dir)
{
    /* Crea los niveles que falten (p. ej. ".sistema" y "bienestar"). */
    char p[PATH_MAX_LEN];
    if (snprintf(p, sizeof(p), "%s", dir) >= (int)sizeof(p)) {
        return false;
    }
    for (char *c = p + 1; *c; c++) {
        if (*c == '/') {
            *c = 0;
            mkdir(p, 0775);
            *c = '/';
        }
    }
    struct stat st;
    return (mkdir(p, 0775) == 0 || errno == EEXIST) && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool write_all(const char *real, const char *tmp, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(buf, 1, n, f) == n && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, real) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

bool act_state_save(const char *dir, const uint8_t *buf, size_t n)
{
    char real[PATH_MAX_LEN], tmp[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_STATE) || !path(tmp, dir, ACT_FILE_STATE ".tmp")) {
        return false;
    }
    return write_all(real, tmp, buf, n);
}

size_t act_state_load(const char *dir, uint8_t *buf, size_t cap)
{
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_STATE)) {
        return 0;
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return 0;
    }
    const size_t n = fread(buf, 1, cap, f);
    const bool more = fgetc(f) != EOF; /* más largo de lo que cabe: no es nuestro */
    fclose(f);
    return more ? 0 : n;
}

static long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static bool append(const char *real, const uint8_t *rec, size_t len)
{
    FILE *f = fopen(real, "ab");
    if (!f) {
        return false;
    }
    bool ok = fwrite(rec, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
    return fclose(f) == 0 && ok;
}

/* Lee el registro i (de len bytes). */
static bool read_rec(FILE *f, long i, uint8_t *rec, size_t len)
{
    return fseek(f, i * (long)len, SEEK_SET) == 0 && fread(rec, 1, len, f) == len;
}

/*
 * Dos registros del mismo día: un reinicio entre guardar el día y el punto de
 * control lo vuelve a cerrar con el estado guardado ANTES (menos pasos). Los
 * contadores de un día solo crecen, así que se queda el más completo; a
 * igualdad, el último (puede llevar las calorías con otro peso).
 */
static bool day_replaces(const act_day_t *later, const act_day_t *earlier)
{
    return later->steps >= earlier->steps && later->ee >= earlier->ee;
}

bool act_hist_append_day(const char *dir, const act_day_t *d)
{
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_DAYS) || d->day == ACT_NO_DAY) {
        return false;
    }
    long sz = file_size(real);
    if (sz > 0 && sz % ACT_DAY_REC_LEN) {
        act_hist_repair(dir, NULL); /* un añadido a medias: primero se arregla */
        sz = file_size(real);
    }
    uint8_t rec[ACT_DAY_REC_LEN], last[ACT_DAY_REC_LEN];
    act_day_pack(d, rec);
    bool compact = false;
    if (sz >= ACT_DAY_REC_LEN) {
        FILE *f = fopen(real, "rb");
        act_day_t ld;
        const bool same_day = f && read_rec(f, sz / ACT_DAY_REC_LEN - 1, last, sizeof(last)) &&
                              act_day_unpack(last, &ld) && ld.day == d->day;
        if (f) {
            fclose(f);
        }
        if (same_day && (memcmp(last, rec, sizeof(rec)) == 0 || !day_replaces(d, &ld))) {
            return true; /* ya estaba, o el que hay está más completo */
        }
        compact = same_day;
    }
    if (!append(real, rec, sizeof(rec))) {
        return false;
    }
    if (compact) {
        act_hist_repair(dir, NULL); /* el anterior del mismo día sobra: fuera ya, no al arrancar */
    }
    return true;
}

bool act_hist_append_ses(const char *dir, const act_ses_rec_t *r)
{
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_SES) || !r->id) {
        return false;
    }
    long sz = file_size(real);
    if (sz > 0 && sz % ACT_SES_REC_LEN) {
        act_hist_repair(dir, NULL);
        sz = file_size(real);
    }
    if (sz >= ACT_SES_REC_LEN) {
        FILE *f = fopen(real, "rb");
        const long n = sz / ACT_SES_REC_LEN;
        bool dup = false;
        for (long i = n - 1; f && i >= 0 && i >= n - SES_DEDUP && !dup; i--) {
            uint8_t buf[ACT_SES_REC_LEN];
            act_ses_rec_t o;
            dup = read_rec(f, i, buf, sizeof(buf)) && act_ses_unpack(buf, &o) && o.id == r->id;
        }
        if (f) {
            fclose(f);
        }
        if (dup) {
            return true;
        }
    }
    uint8_t rec[ACT_SES_REC_LEN];
    act_ses_pack(r, rec);
    return append(real, rec, sizeof(rec));
}

/* ---- Reparación ---- */

/* Copia los registros válidos y únicos de real a tmp (los keep últimos) y
 * sustituye real si algo cambió. day: dedupe por fecha (day_replaces); si
 * no, por id de sesión. */
static bool repair_file(const char *dir, const char *name, size_t len, bool day, uint32_t keep, uint32_t max,
                        uint32_t *valid_out, uint32_t *dropped_out, bool *rewritten)
{
    char real[PATH_MAX_LEN], tmp[PATH_MAX_LEN];
    if (!path(real, dir, name) || snprintf(tmp, sizeof(tmp), "%s.tmp", real) >= (int)sizeof(tmp)) {
        return false;
    }
    unlink(tmp); /* un temporal de una reparación cortada */
    const long sz = file_size(real);
    if (sz <= 0) {
        return true;
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return false;
    }
    /* Pasada 1: cuántos válidos y únicos hay. */
    uint8_t buf[ACT_SES_REC_LEN], prev[ACT_SES_REC_LEN];
    uint32_t ids[SES_DEDUP] = {0};
    uint32_t valid = 0, dropped = 0;
    const long n = sz / (long)len;
    int32_t prev_day = ACT_NO_DAY;
    for (long i = 0; i < n; i++) {
        if (!read_rec(f, i, buf, len)) {
            dropped++;
            continue;
        }
        if (day) {
            act_day_t d;
            if (!act_day_unpack(buf, &d)) {
                dropped++;
                continue;
            }
            if (valid && d.day == prev_day) {
                dropped++; /* el mismo día otra vez: se queda el último */
                continue;
            }
            prev_day = d.day;
        } else {
            act_ses_rec_t r;
            bool dup = false;
            if (!act_ses_unpack(buf, &r)) {
                dropped++;
                continue;
            }
            for (int k = 0; k < SES_DEDUP; k++) {
                dup |= ids[k] == r.id;
            }
            if (dup) {
                dropped++;
                continue;
            }
            ids[valid % SES_DEDUP] = r.id;
        }
        valid++;
    }
    const bool partial = sz % (long)len != 0;
    const uint32_t skip = valid > max ? valid - keep : 0;
    if (!dropped && !partial && !skip) {
        fclose(f);
        *valid_out = valid;
        return true;
    }
    /* Pasada 2: reescribir. */
    FILE *o = fopen(tmp, "wb");
    if (!o) {
        fclose(f);
        return false;
    }
    bool ok = true, have_prev = false;
    uint32_t seen = 0, written = 0;
    memset(ids, 0, sizeof(ids));
    uint32_t nid = 0;
    for (long i = 0; i < n && ok; i++) {
        if (!read_rec(f, i, buf, len)) {
            continue;
        }
        if (day) {
            act_day_t d, pd;
            if (!act_day_unpack(buf, &d)) {
                continue;
            }
            if (have_prev && act_day_unpack(prev, &pd) && pd.day == d.day) {
                if (day_replaces(&d, &pd)) {
                    memcpy(prev, buf, len); /* sustituye al anterior del mismo día */
                }
                continue;
            }
            if (have_prev) {
                if (seen++ >= skip) {
                    ok = fwrite(prev, 1, len, o) == len;
                    written++;
                }
            }
            memcpy(prev, buf, len);
            have_prev = true;
        } else {
            act_ses_rec_t r;
            bool dup = false;
            if (!act_ses_unpack(buf, &r)) {
                continue;
            }
            for (int k = 0; k < SES_DEDUP; k++) {
                dup |= ids[k] == r.id;
            }
            if (dup) {
                continue;
            }
            ids[nid++ % SES_DEDUP] = r.id;
            if (seen++ >= skip) {
                ok = fwrite(buf, 1, len, o) == len;
                written++;
            }
        }
    }
    if (ok && day && have_prev && seen++ >= skip) {
        ok = fwrite(prev, 1, len, o) == len;
        written++;
    }
    fclose(f);
    ok = ok && fflush(o) == 0 && fsync(fileno(o)) == 0;
    ok = fclose(o) == 0 && ok;
    if (!ok || rename(tmp, real) != 0) {
        unlink(tmp);
        return false;
    }
    *valid_out = written;
    *dropped_out += dropped + (partial ? 1 : 0);
    *rewritten = true;
    return true;
}

bool act_hist_repair(const char *dir, act_repair_t *out)
{
    act_repair_t r = {0};
    const bool a = repair_file(dir, ACT_FILE_DAYS, ACT_DAY_REC_LEN, true, ACT_DAYS_KEEP, ACT_DAYS_MAX, &r.days,
                               &r.dropped, &r.rewritten);
    const bool b = repair_file(dir, ACT_FILE_SES, ACT_SES_REC_LEN, false, ACT_SES_KEEP, ACT_SES_MAX, &r.sessions,
                               &r.dropped, &r.rewritten);
    if (out) {
        *out = r;
    }
    return a && b;
}

/* ---- Consultas ---- */

int act_hist_days(const char *dir, int32_t from, int32_t to, act_day_t *out, int cap)
{
    if (to < from || (int64_t)to - from + 1 > cap) {
        return -1;
    }
    const int n = (int)(to - from + 1);
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].day = ACT_NO_DAY;
    }
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_DAYS)) {
        return 0;
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return 0;
    }
    int found = 0;
    uint8_t buf[ACT_DAY_REC_LEN];
    while (fread(buf, 1, sizeof(buf), f) == sizeof(buf)) {
        act_day_t d;
        if (act_day_unpack(buf, &d) && d.day >= from && d.day <= to) {
            act_day_t *o = &out[d.day - from];
            if (o->day == ACT_NO_DAY) {
                found++;
                *o = d;
            } else if (day_replaces(&d, o)) {
                *o = d;
            }
        }
    }
    fclose(f);
    return found;
}

int act_hist_sessions(const char *dir, uint32_t skip, act_ses_rec_t *out, int max)
{
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_SES) || max <= 0) {
        return 0;
    }
    const long sz = file_size(real);
    if (sz < ACT_SES_REC_LEN) {
        return 0;
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return 0;
    }
    const long count = sz / ACT_SES_REC_LEN;
    int k = 0;
    for (long i = count - 1 - (long)skip; i >= 0 && k < max; i--) {
        uint8_t buf[ACT_SES_REC_LEN];
        if (read_rec(f, i, buf, sizeof(buf)) && act_ses_unpack(buf, &out[k])) {
            k++;
        }
    }
    fclose(f);
    return k;
}

uint32_t act_hist_max_id(const char *dir)
{
    char real[PATH_MAX_LEN];
    if (!path(real, dir, ACT_FILE_SES)) {
        return 0;
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return 0;
    }
    uint32_t mx = 0;
    uint8_t buf[ACT_SES_REC_LEN];
    while (fread(buf, 1, sizeof(buf), f) == sizeof(buf)) {
        act_ses_rec_t r;
        if (act_ses_unpack(buf, &r) && r.id > mx) {
            mx = r.id;
        }
    }
    fclose(f);
    return mx;
}

/* ---- Estadísticas ---- */

static const char *const kAchName[ACT_ACH_COUNT] = {
    "Primer entrenamiento", "10 entrenamientos", "50 entrenamientos", "10 000 pasos",
    "20 000 pasos",         "Racha de 7 días",   "Racha de 30 días",  "Tres anillos",
    "100 km",
};
static const char *const kAchDesc[ACT_ACH_COUNT] = {
    "Terminaste tu primer entrenamiento",
    "Diez entrenamientos guardados",
    "Cincuenta entrenamientos guardados",
    "Un día con 10 000 pasos o más",
    "Un día con 20 000 pasos o más",
    "7 días seguidos cumpliendo los pasos",
    "30 días seguidos cumpliendo los pasos",
    "Un día con los tres objetivos",
    "100 km estimados en total",
};

const char *act_ach_name(int i)
{
    return (i >= 0 && i < ACT_ACH_COUNT) ? kAchName[i] : "";
}

const char *act_ach_desc(int i)
{
    return (i >= 0 && i < ACT_ACH_COUNT) ? kAchDesc[i] : "";
}

void act_stats_init(act_stats_t *s)
{
    memset(s, 0, sizeof(*s));
    s->best_day = ACT_NO_DAY;
}

void act_stats_add_day(act_stats_t *s, const act_day_t *d, int32_t *prev_day, uint16_t *run)
{
    if (d->day == ACT_NO_DAY) {
        return;
    }
    s->days++;
    s->total_steps += d->steps;
    s->total_dist_m += (d->walk_cm + d->run_cm) / 100u;
    if (d->steps > s->best_steps) {
        s->best_steps = d->steps;
        s->best_day = d->day;
    }
    if ((d->goal_bits & 7u) == 7u) {
        s->all_rings_days++;
    }
    const bool met = (d->goal_bits & 4u) != 0;
    if (met) {
        *run = (*prev_day != ACT_NO_DAY && d->day == *prev_day + 1 && *run) ? (uint16_t)(*run + 1) : 1;
        if (*run > s->best_streak) {
            s->best_streak = *run;
        }
    } else {
        *run = 0;
    }
    *prev_day = d->day;
}

void act_stats_finish(act_stats_t *s, int32_t today, bool today_met, int32_t last_day, uint16_t run)
{
    /* La racha actual sigue viva si llega a ayer (hoy aún puede cumplirse) o a hoy. */
    if (today == ACT_NO_DAY) {
        s->streak = 0;
    } else if (last_day == today) {
        s->streak = today_met ? run : 0;
    } else if (last_day == today - 1) {
        s->streak = run;
    } else {
        s->streak = 0;
    }
    uint32_t a = 0;
    a |= s->sessions >= 1 ? 1u << ACT_ACH_FIRST_WORKOUT : 0;
    a |= s->sessions >= 10 ? 1u << ACT_ACH_WORKOUTS_10 : 0;
    a |= s->sessions >= 50 ? 1u << ACT_ACH_WORKOUTS_50 : 0;
    a |= s->best_steps >= 10000 ? 1u << ACT_ACH_STEPS_10K : 0;
    a |= s->best_steps >= 20000 ? 1u << ACT_ACH_STEPS_20K : 0;
    a |= s->best_streak >= 7 ? 1u << ACT_ACH_STREAK_7 : 0;
    a |= s->best_streak >= 30 ? 1u << ACT_ACH_STREAK_30 : 0;
    a |= s->all_rings_days ? 1u << ACT_ACH_ALL_RINGS : 0;
    a |= s->total_dist_m >= 100000 ? 1u << ACT_ACH_DIST_100K : 0;
    s->ach = a;
}

bool act_stats_build(const char *dir, const act_day_t *today, const act_config_t *c, act_stats_t *out)
{
    act_stats_init(out);
    int32_t prev = ACT_NO_DAY;
    uint16_t run = 0;
    char real[PATH_MAX_LEN];
    bool ok = true;
    if (path(real, dir, ACT_FILE_DAYS)) {
        FILE *f = fopen(real, "rb");
        if (f) {
            uint8_t buf[ACT_DAY_REC_LEN];
            while (fread(buf, 1, sizeof(buf), f) == sizeof(buf)) {
                act_day_t d;
                if (act_day_unpack(buf, &d) && (!today || d.day != today->day)) {
                    act_stats_add_day(out, &d, &prev, &run);
                }
            }
            fclose(f);
        }
    } else {
        ok = false;
    }
    if (path(real, dir, ACT_FILE_SES)) {
        FILE *f = fopen(real, "rb");
        if (f) {
            uint8_t buf[ACT_SES_REC_LEN];
            while (fread(buf, 1, sizeof(buf), f) == sizeof(buf)) {
                act_ses_rec_t r;
                if (act_ses_unpack(buf, &r)) {
                    out->sessions++;
                    out->workout_s += r.active_s;
                }
            }
            fclose(f);
        }
    }
    bool today_met = false;
    if (today && today->day != ACT_NO_DAY) {
        act_day_t t = *today;
        uint32_t kcal = 0;
        const bool kv = act_kcal(c, t.ee, &kcal);
        act_rings_t r;
        act_rings(c, kcal, kv, t.ex_wk_s + t.ex_auto_s, t.steps, &r);
        t.goal_bits = r.met_bits;
        today_met = (r.met_bits & 4u) != 0;
        const int32_t before = prev;
        const uint16_t run_before = run;
        act_stats_add_day(out, &t, &prev, &run);
        if (!today_met) {
            /* hoy no rompe la racha mientras no termine: se cuenta hasta ayer */
            prev = before;
            run = run_before;
        }
    }
    act_stats_finish(out, today ? today->day : ACT_NO_DAY, today_met, prev, run);
    return ok;
}
