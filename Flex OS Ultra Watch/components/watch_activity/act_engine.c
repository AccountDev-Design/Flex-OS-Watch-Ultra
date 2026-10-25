/*
 * act_engine — ver include/act_engine.h
 */
#include "act_engine.h"

#include <string.h>

#define SUGGEST_WALK_MS 180000 /* 3 min seguidos caminando: ¿entrenar? */
#define SUGGEST_RUN_MS  120000
#define SUGGEST_RESET_MS 120000
#define BRISK_CADENCE   100    /* pasos/min: caminar a buen ritmo */
#define BRISK_MIN_MS    60000  /* sostenido al menos 1 min para contar como ejercicio */
#define HOUR_MOVE_S     60
#define MAX_TICK_MS     5000   /* un tick más largo (tarea parada) no suma tiempo */

static bool running(const act_engine_t *e)
{
    return act_session_running(&e->wk.s);
}

static const act_type_t *wk_type(const act_engine_t *e)
{
    return act_catalog_get(e->wk.s.type);
}

static void new_day(act_day_t *d, int32_t day)
{
    memset(d, 0, sizeof(*d));
    d->day = day;
}

void act_engine_init(act_engine_t *e, const act_config_t *cfg)
{
    memset(e, 0, sizeof(*e));
    if (cfg) {
        e->cfg = *cfg;
    } else {
        act_config_default(&e->cfg);
    }
    act_config_normalize(&e->cfg);
    act_step_params_t p;
    act_step_params_default(&p, e->cfg.sensitivity);
    act_steps_init(&e->det, &p);
    new_day(&e->day, ACT_NO_DAY);
    e->hour = -1;
    e->next_id = 1;
    e->sensor_ok = true;
}

void act_engine_set_config(act_engine_t *e, const act_config_t *cfg)
{
    e->cfg = *cfg;
    act_config_normalize(&e->cfg);
    act_step_params_t p;
    act_step_params_default(&p, e->cfg.sensitivity);
    act_steps_set_params(&e->det, &p);
    e->gen++;
}

void act_engine_set_sensor(act_engine_t *e, bool ok)
{
    if (e->sensor_ok != ok) {
        e->sensor_ok = ok;
        e->gen++;
    }
}

uint32_t act_engine_take_events(act_engine_t *e)
{
    const uint32_t ev = e->events;
    e->events = 0;
    return ev;
}

static uint32_t wk_ee(const act_workout_t *w)
{
    return w->ee_steps + w->ee_met;
}

/* Lo que la sesión aporta al día desde su última "base". */
static void session_to_day(act_engine_t *e, int64_t now_ms)
{
    act_workout_t *w = &e->wk;
    const uint32_t ee = wk_ee(w);
    if (ee >= w->base_ee) {
        e->day.ee += ee - w->base_ee;
    } else {
        /* La pausa automática retrasó el final del tramo activo: menos energía. */
        const uint32_t less = w->base_ee - ee;
        e->day.ee -= less < e->day.ee ? less : e->day.ee;
    }
    w->base_ee = ee;
    const uint32_t act = act_session_active_ms(&w->s, now_ms);
    const uint32_t s_now = act / 1000u, s_base = w->base_ex_ms / 1000u;
    if (s_now >= s_base) {
        e->day.ex_wk_s += s_now - s_base;
    } else {
        const uint32_t less = s_base - s_now;
        e->day.ex_wk_s -= less < e->day.ex_wk_s ? less : e->day.ex_wk_s;
    }
    w->base_ex_ms = act;
}

static void close_day_fields(const act_config_t *c, act_day_t *d)
{
    uint32_t kcal = 0;
    const bool ok = act_kcal(c, d->ee, &kcal);
    d->kcal = (uint16_t)(kcal > 0xFFFF ? 0xFFFF : kcal);
    if (!ok) {
        d->flags |= ACT_DAY_NO_WEIGHT;
    }
    act_rings_t r;
    act_rings(c, kcal, ok, d->ex_wk_s + d->ex_auto_s, d->steps, &r);
    d->goal_bits = r.met_bits;
}

/*
 * ¿La fecha local `day` cierra el día en curso `cur`? Hacia delante, siempre.
 * Un día hacia atrás es la hora corregida cerca de la medianoche (SNTP con el
 * RTC adelantado, o a mano): el día en curso sigue abierto, porque cerrarlo y
 * abrir otra vez el anterior le daría dos registros (y uno taparía al otro).
 * Más atrás, la fecha que había estaba mal: se cierra y empieza la buena.
 */
static bool day_changed(int32_t cur, int32_t day)
{
    return day > cur || day < cur - 1;
}

static void rollover(act_engine_t *e, int32_t day)
{
    if (running(e)) {
        session_to_day(e, e->now_ms);
    }
    close_day_fields(&e->cfg, &e->day);
    e->closed = e->day;
    e->events |= ACT_EV_DAY_CLOSED;
    new_day(&e->day, day); /* la sesión conserva su fecha de inicio */
    e->hour_move_s = 0;
    e->move_ms = 0;
    e->auto_ms = 0;
    e->bout_auto_s = 0;
}

/* Reparte los pasos nuevos del detector entre el día, el tren y la sesión. */
static void distribute(act_engine_t *e)
{
    const uint32_t d = e->det.steps - e->seen_steps;
    if (!d) {
        return;
    }
    uint32_t r = e->det.steps_run - e->seen_run;
    if (r > d) {
        r = d;
    }
    e->seen_steps = e->det.steps;
    e->seen_run = e->det.steps_run;
    const uint32_t w = d - r;
    const uint32_t cm_w = w * act_stride_cm(&e->cfg, false, NULL);
    const uint32_t cm_r = r * act_stride_cm(&e->cfg, true, NULL);
    const uint32_t ee = act_ee_steps(cm_w, false) + act_ee_steps(cm_r, true);
    e->day.steps += d;
    e->day.steps_run += r;
    e->day.walk_cm += cm_w;
    e->day.run_cm += cm_r;
    if (e->bout_on) {
        e->bout_steps += d;
        e->bout_run += r;
        e->bout_cm += cm_w + cm_r;
    }
    act_workout_t *wk = &e->wk;
    if (wk->s.state == ACT_SES_ACTIVE) {
        const act_type_t *t = wk_type(e);
        if (t->flags & ACT_F_STEPS) {
            wk->steps += d;
            wk->steps_run += r;
            if (t->flags & ACT_F_DIST) {
                wk->dist_cm += cm_w + cm_r;
            }
        }
        if (t->kcal == ACT_KCAL_STEPS) {
            wk->ee_steps += ee;
        }
        act_session_evidence(&wk->s, e->det.last_step_t);
    } else {
        /* Sin sesión activa (ninguna o en pausa): el gasto de los pasos es del día. */
        e->day.ee += ee;
        if (e->bout_on) {
            e->bout_ee_bg += ee;
        }
    }
    e->gen++;
}

void act_engine_feed(act_engine_t *e, const act_sample_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        act_steps_feed(&e->det, &s[i]);
        if (e->det.walking && !e->bout_on) {
            e->bout_on = true;
            e->bout_steps = e->bout_run = e->bout_cm = e->bout_ee_bg = e->bout_auto_s = 0;
        }
        distribute(e);
        if (!e->det.walking && e->bout_on) {
            e->bout_on = false;
        }
    }
}

void act_engine_gap(act_engine_t *e)
{
    act_steps_gap(&e->det);
    e->bout_on = false;
}

/* Sesión terminada (manual o automáticamente): totales y al día. */
static void finish_session(act_engine_t *e)
{
    act_workout_t *w = &e->wk;
    const act_type_t *t = wk_type(e);
    if (t->kcal == ACT_KCAL_MET) {
        w->ee_met = act_ee_met(t->met10, act_session_active_ms(&w->s, w->s.end_ms));
    }
    session_to_day(e, w->s.end_ms);
    e->day.workouts++;
    e->done = *w;
    e->events |= ACT_EV_SESSION_DONE | ACT_EV_SESSION;
    e->gen++;
}

/* El tren de pasos que hizo reanudar pasa a la sesión (y deja de ser del día). */
static void bout_to_session(act_engine_t *e)
{
    act_workout_t *w = &e->wk;
    const act_type_t *t = wk_type(e);
    if (t->flags & ACT_F_STEPS) {
        w->steps += e->bout_steps;
        w->steps_run += e->bout_run;
        if (t->flags & ACT_F_DIST) {
            w->dist_cm += e->bout_cm;
        }
    }
    if (t->kcal == ACT_KCAL_STEPS) {
        w->ee_steps += e->bout_ee_bg;
    }
    /* Con MET, la energía de ese rato la da el tiempo activo (que empieza en
     * el primer paso); con pasos, la de arriba. Del día se quita lo que
     * apuntó mientras estaba en pausa. */
    e->day.ee -= e->bout_ee_bg < e->day.ee ? e->bout_ee_bg : e->day.ee;
    e->day.ex_auto_s -= e->bout_auto_s < e->day.ex_auto_s ? e->bout_auto_s : e->day.ex_auto_s;
    e->bout_ee_bg = 0;
    e->bout_auto_s = 0;
    e->bout_steps = e->bout_run = e->bout_cm = 0;
}

void act_engine_tick(act_engine_t *e, int64_t now_ms, int64_t local_s, int64_t utc_s)
{
    (void)utc_s;
    int64_t dt = e->have_tick ? now_ms - e->tick_ms : 0;
    if (dt < 0 || dt > MAX_TICK_MS) {
        dt = 0;
    }
    e->tick_ms = now_ms;
    e->have_tick = true;
    e->now_ms = now_ms;

    /* Día y hora locales */
    if (local_s >= 0) {
        const int32_t day = (int32_t)(local_s / 86400);
        if (e->day.day == ACT_NO_DAY) {
            if (e->day.steps || e->day.ee || e->day.workouts) {
                e->day.flags |= ACT_DAY_LATE_DATE;
            }
            e->day.day = day;
            if (running(e) && e->wk.day == ACT_NO_DAY) {
                e->wk.day = day;
            }
        } else if (day_changed(e->day.day, day)) {
            rollover(e, day);
        }
        const int hour = (int)((local_s % 86400) / 3600);
        if (hour != e->hour) {
            e->hour = hour;
            e->hour_move_s = 0;
        }
    } else {
        e->hour = -1;
    }

    const bool moving = e->det.motion == ACT_MOT_WALK || e->det.motion == ACT_MOT_RUN;
    if (moving && dt) {
        e->move_ms += (uint32_t)dt;
        while (e->move_ms >= 1000) {
            e->move_ms -= 1000;
            e->day.move_s++;
            if (e->hour >= 0 && ++e->hour_move_s >= HOUR_MOVE_S) {
                e->day.hours |= 1u << e->hour;
            }
        }
    }

    /* Funciones automáticas del entrenamiento */
    if (running(e)) {
        act_auto_cfg_t ac = e->cfg.autos;
        if (!e->sensor_ok) {
            ac.auto_pause = ac.auto_resume = false; /* sin datos no hay evidencia */
        }
        const act_sev_t ev = act_session_tick(&e->wk.s, &ac, now_ms, e->det.walking, e->det.bout_start_t);
        if (ev == ACT_SEV_AUTO_PAUSE) {
            e->events |= ACT_EV_AUTO_PAUSE | ACT_EV_SESSION;
        } else if (ev == ACT_SEV_AUTO_RESUME) {
            if (e->bout_on) {
                bout_to_session(e);
            }
            e->events |= ACT_EV_AUTO_RESUME | ACT_EV_SESSION;
        } else if (ev == ACT_SEV_AUTO_END) {
            finish_session(e);
            e->events |= ACT_EV_AUTO_END;
        }
    }
    if (running(e)) {
        act_workout_t *w = &e->wk;
        const act_type_t *t = wk_type(e);
        if (t->kcal == ACT_KCAL_MET) {
            /* Siempre desde el tiempo activo total: una pausa con fecha
             * anterior corrige sola lo que sobraba. */
            w->ee_met = act_ee_met(t->met10, act_session_active_ms(&w->s, now_ms));
        }
        if (w->s.state == ACT_SES_ACTIVE) {
            w->motion_sum += e->det.rms;
            w->motion_n++;
            const uint16_t cad = act_steps_cadence(&e->det);
            if (cad > w->max_cadence && (t->flags & ACT_F_STEPS)) {
                w->max_cadence = cad;
            }
        }
    }

    /* Ejercicio detectado fuera de entrenamientos: trenes sostenidos y a
     * buen ritmo; nunca movimiento casual. */
    const bool brisk = e->det.walking &&
                       (act_steps_cadence(&e->det) >= BRISK_CADENCE || e->det.motion == ACT_MOT_RUN);
    if (!running(e) && e->cfg.auto_detect && brisk && dt) {
        const uint32_t before = e->brisk_ms;
        e->brisk_ms += (uint32_t)dt;
        if (e->brisk_ms >= BRISK_MIN_MS) {
            /* Al llegar al minuto se suma el minuto entero; después, lo que pase. */
            e->auto_ms += before < BRISK_MIN_MS ? e->brisk_ms : (uint32_t)dt;
            while (e->auto_ms >= 1000) {
                e->auto_ms -= 1000;
                e->day.ex_auto_s++;
                if (e->bout_on) {
                    e->bout_auto_s++;
                }
            }
        }
    } else if (!brisk) {
        e->brisk_ms = 0;
    }

    /* Sugerencia: "¿estás entrenando?" tras un tren largo. */
    if (e->cfg.auto_detect && !running(e) && e->det.walking && e->sensor_ok) {
        e->walk_off_ms = 0;
        const bool run = e->det.motion == ACT_MOT_RUN;
        const int64_t need = run ? SUGGEST_RUN_MS : SUGGEST_WALK_MS;
        if (!e->suggest_done && now_ms - e->det.bout_start_t >= need) {
            e->suggest = true;
            e->suggest_done = true;
            e->suggest_type = run ? ACT_TYPE_RUN_OUT : ACT_TYPE_WALK_OUT;
            e->events |= ACT_EV_SUGGEST;
        }
    } else if (!e->det.walking) {
        if (!e->walk_off_ms) {
            e->walk_off_ms = now_ms;
        }
        if (now_ms - e->walk_off_ms >= SUGGEST_RESET_MS) {
            e->suggest = false;
            e->suggest_done = false;
        }
    }
    if (running(e) || !e->cfg.auto_detect) {
        e->suggest = false;
    }
    e->gen++;
}

bool act_engine_start(act_engine_t *e, uint8_t type, int64_t now_ms, int64_t utc_s, bool from_bout)
{
    const act_type_t *t = act_catalog_find(type);
    if (!t || running(e)) {
        return false;
    }
    memset(&e->wk, 0, sizeof(e->wk));
    act_session_start(&e->wk.s, e->next_id++, type, (t->flags & ACT_F_AUTO) != 0, now_ms);
    e->wk.start_utc = utc_s;
    e->wk.day = e->day.day;
    if (from_bout && e->det.walking && e->bout_on && e->det.bout_start_t < now_ms &&
        now_ms - e->det.bout_start_t < 3600000) {
        /* Sugerencia aceptada: el entrenamiento empezó con el primer paso. */
        const int64_t back = now_ms - e->det.bout_start_t;
        e->wk.s.start_ms = e->wk.s.seg_ms = e->det.bout_start_t;
        if (utc_s) {
            e->wk.start_utc = utc_s - back / 1000;
        }
        bout_to_session(e);
    }
    act_config_push_recent(&e->cfg, type);
    e->suggest = false;
    e->suggest_done = true;
    e->events |= ACT_EV_SESSION | ACT_EV_CONFIG;
    e->gen++;
    return true;
}

bool act_engine_pause(act_engine_t *e, int64_t now_ms)
{
    if (!act_session_pause(&e->wk.s, now_ms)) {
        return false;
    }
    e->events |= ACT_EV_SESSION;
    e->gen++;
    return true;
}

bool act_engine_resume(act_engine_t *e, int64_t now_ms)
{
    if (!act_session_resume(&e->wk.s, now_ms)) {
        return false;
    }
    e->events |= ACT_EV_SESSION;
    e->gen++;
    return true;
}

bool act_engine_end(act_engine_t *e, int64_t now_ms)
{
    if (!act_session_end(&e->wk.s, now_ms)) {
        return false;
    }
    finish_session(e);
    return true;
}

void act_engine_dismiss_suggest(act_engine_t *e)
{
    e->suggest = false;
    e->suggest_done = true;
    e->gen++;
}

void act_engine_calib_start(act_engine_t *e, bool run)
{
    e->calib = true;
    e->calib_run = run;
    e->calib_steps0 = e->det.steps;
    e->gen++;
}

uint32_t act_engine_calib_steps(const act_engine_t *e)
{
    return e->calib ? e->det.steps - e->calib_steps0 : 0;
}

void act_engine_calib_stop(act_engine_t *e)
{
    e->calib = false;
    e->gen++;
}

static uint8_t popcount24(uint32_t v)
{
    uint8_t n = 0;
    for (int i = 0; i < 24; i++) {
        n += (v >> i) & 1u;
    }
    return n;
}

void act_engine_view(const act_engine_t *e, act_view_t *v)
{
    memset(v, 0, sizeof(*v));
    const act_workout_t *w = &e->wk;
    const bool run = running(e);
    const int64_t now = e->now_ms;
    v->day = e->day.day;
    v->steps = e->day.steps;
    v->dist_m = (e->day.walk_cm + e->day.run_cm) / 100u;
    act_stride_src_t src;
    act_stride_cm(&e->cfg, false, &src);
    v->dist_generic = src == ACT_STRIDE_GENERIC;
    uint32_t ee = e->day.ee;
    uint32_t ex = e->day.ex_wk_s + e->day.ex_auto_s;
    if (run) {
        const uint32_t wee = wk_ee(w);
        ee = wee >= w->base_ee ? ee + (wee - w->base_ee) : ee - ((w->base_ee - wee) < ee ? (w->base_ee - wee) : ee);
        const uint32_t act_s = act_session_active_ms(&w->s, now) / 1000u, base_s = w->base_ex_ms / 1000u;
        ex = act_s >= base_s ? ex + (act_s - base_s) : ex;
    }
    v->kcal_valid = act_kcal(&e->cfg, ee, &v->kcal);
    v->ex_s = ex;
    v->move_s = e->day.move_s;
    v->move_hours = popcount24(e->day.hours);
    act_rings(&e->cfg, v->kcal, v->kcal_valid, ex, v->steps, &v->rings);
    v->workouts = e->day.workouts;
    v->motion = e->det.motion;
    v->walking = e->det.walking;
    v->cadence = act_steps_cadence(&e->det);
    v->motion_mg = e->det.rms;

    v->ses = w->s.state;
    v->pause_kind = w->s.pause_kind;
    v->end_reason = w->s.end_reason;
    v->type = w->s.type;
    v->ses_id = w->s.id;
    v->total_ms = act_session_total_ms(&w->s, now);
    v->active_ms = act_session_active_ms(&w->s, now);
    v->paused_ms = act_session_paused_ms(&w->s, now);
    v->ses_steps = w->steps;
    v->ses_dist_m = w->dist_cm / 100u;
    v->ses_kcal_valid = act_kcal(&e->cfg, wk_ee(w), &v->ses_kcal);
    if (v->ses_dist_m >= 50) {
        v->pace_s_km = (uint32_t)((uint64_t)v->active_ms / v->ses_dist_m); /* ms/m = s/km */
    }
    if (w->s.state == ACT_SES_ACTIVE && e->det.walking && (wk_type(e)->flags & ACT_F_STEPS)) {
        v->ses_cadence = v->cadence;
    }
    v->ses_motion_mg = w->motion_n ? (uint16_t)(w->motion_sum / w->motion_n) : 0;
    v->auto_pauses = w->s.auto_pauses;
    v->start_utc = w->start_utc;
    v->suggest = e->suggest;
    v->suggest_type = e->suggest_type;
    v->calib = e->calib;
    v->calib_steps = act_engine_calib_steps(e);
    v->gen = e->gen;
}

bool act_day_kcal(const act_config_t *c, const act_day_t *d, uint32_t *kcal)
{
    if (!(d->flags & ACT_DAY_NO_WEIGHT)) {
        *kcal = d->kcal;
        return true;
    }
    return act_kcal(c, d->ee, kcal);
}

void act_engine_restore(act_engine_t *e, const act_day_t *day, const act_workout_t *wk, uint32_t next_id,
                        int64_t now_ms, int32_t local_day, uint32_t gap_ms)
{
    e->now_ms = now_ms;
    if (next_id > e->next_id) {
        e->next_id = next_id;
    }
    if (wk && wk->s.state != ACT_SES_IDLE) {
        e->wk = *wk;
        if (act_session_running(&e->wk.s)) {
            act_session_restore(&e->wk.s, now_ms, gap_ms);
        }
    }
    if (day) {
        e->day = *day;
        e->day.flags |= ACT_DAY_RESTORED;
        if (local_day != ACT_NO_DAY && day->day != ACT_NO_DAY && day_changed(day->day, local_day)) {
            rollover(e, local_day); /* el día guardado ya pasó: se cierra (y se guarda) */
        } else if (e->day.day == ACT_NO_DAY && local_day != ACT_NO_DAY) {
            e->day.day = local_day;
            e->day.flags |= ACT_DAY_LATE_DATE;
        }
    }
    e->gen++;
}
