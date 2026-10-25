/*
 * act_session — ver include/act_session.h
 */
#include "act_session.h"

#include <string.h>

void act_auto_cfg_default(act_auto_cfg_t *c)
{
    c->auto_pause = true;
    c->auto_resume = true;
    c->auto_end = true;
    c->pause_after_s = 20;
    c->end_after_s = 300;
}

void act_session_reset(act_session_t *s)
{
    memset(s, 0, sizeof(*s));
}

static uint32_t span(int64_t a, int64_t b)
{
    return b > a ? (uint32_t)(b - a) : 0;
}

bool act_session_running(const act_session_t *s)
{
    return s->state == ACT_SES_ACTIVE || s->state == ACT_SES_PAUSED;
}

bool act_session_start(act_session_t *s, uint32_t id, uint8_t type, bool auto_ok, int64_t now_ms)
{
    if (act_session_running(s)) {
        return false;
    }
    act_session_reset(s);
    s->state = ACT_SES_ACTIVE;
    s->id = id;
    s->type = type;
    s->auto_ok = auto_ok;
    s->start_ms = now_ms;
    s->seg_ms = now_ms;
    s->evidence_ms = now_ms;
    return true;
}

/* ACTIVE -> PAUSED en el instante at (>= inicio del tramo). */
static void to_paused(act_session_t *s, int64_t at, act_pause_t kind, int64_t now_ms)
{
    if (at < s->seg_ms) {
        at = s->seg_ms;
    }
    s->pause_ms = now_ms > at ? now_ms : at;
    s->active_ms += span(s->seg_ms, at);
    s->seg_ms = at;
    s->state = ACT_SES_PAUSED;
    s->pause_kind = kind;
    s->pauses++;
}

static void to_active(act_session_t *s, int64_t at)
{
    if (at < s->seg_ms) {
        at = s->seg_ms;
    }
    s->paused_ms += span(s->seg_ms, at);
    s->seg_ms = at;
    s->state = ACT_SES_ACTIVE;
    s->pause_kind = ACT_PAUSE_NONE;
    s->evidence_ms = at;
}

bool act_session_pause(act_session_t *s, int64_t now_ms)
{
    if (s->state != ACT_SES_ACTIVE) {
        return false;
    }
    to_paused(s, now_ms, ACT_PAUSE_MANUAL, now_ms);
    return true;
}

bool act_session_resume(act_session_t *s, int64_t now_ms)
{
    if (s->state != ACT_SES_PAUSED) {
        return false;
    }
    to_active(s, now_ms);
    return true;
}

bool act_session_end(act_session_t *s, int64_t now_ms)
{
    if (!act_session_running(s)) {
        return false;
    }
    if (s->state == ACT_SES_ACTIVE) {
        s->active_ms += span(s->seg_ms, now_ms);
    } else {
        s->paused_ms += span(s->seg_ms, now_ms);
    }
    s->end_ms = now_ms > s->seg_ms ? now_ms : s->seg_ms;
    s->seg_ms = s->end_ms;
    s->state = ACT_SES_COMPLETED;
    s->end_reason = ACT_END_MANUAL;
    return true;
}

void act_session_evidence(act_session_t *s, int64_t t_ms)
{
    if (s->state == ACT_SES_ACTIVE && t_ms > s->evidence_ms) {
        s->evidence_ms = t_ms;
    }
}

act_sev_t act_session_tick(act_session_t *s, const act_auto_cfg_t *c, int64_t now_ms, bool walking,
                           int64_t bout_start_ms)
{
    if (s->state == ACT_SES_ACTIVE) {
        if (s->auto_ok && c->auto_pause && c->pause_after_s &&
            now_ms - s->evidence_ms >= (int64_t)c->pause_after_s * 1000 && !walking) {
            to_paused(s, s->evidence_ms, ACT_PAUSE_AUTO, now_ms);
            s->auto_pauses++;
            return ACT_SEV_AUTO_PAUSE;
        }
        return ACT_SEV_NONE;
    }
    if (s->state != ACT_SES_PAUSED) {
        return ACT_SEV_NONE;
    }
    /* Reanudar solo: pausa automática + movimiento sostenido (no un gesto). */
    if (s->pause_kind == ACT_PAUSE_AUTO && s->auto_ok && c->auto_resume && walking &&
        now_ms - bout_start_ms >= ACT_RESUME_MS) {
        to_active(s, bout_start_ms);
        s->evidence_ms = now_ms;
        s->auto_resumes++;
        return ACT_SEV_AUTO_RESUME;
    }
    if (c->auto_end && c->end_after_s && now_ms - s->pause_ms >= (int64_t)c->end_after_s * 1000) {
        /* La pausa final no cuenta: termina donde empezó a estar parado. */
        s->end_ms = s->seg_ms;
        s->state = ACT_SES_COMPLETED;
        s->end_reason = ACT_END_AUTO;
        return ACT_SEV_AUTO_END;
    }
    return ACT_SEV_NONE;
}

uint32_t act_session_active_ms(const act_session_t *s, int64_t now_ms)
{
    return s->active_ms + (s->state == ACT_SES_ACTIVE ? span(s->seg_ms, now_ms) : 0);
}

uint32_t act_session_paused_ms(const act_session_t *s, int64_t now_ms)
{
    return s->paused_ms + (s->state == ACT_SES_PAUSED ? span(s->seg_ms, now_ms) : 0);
}

uint32_t act_session_total_ms(const act_session_t *s, int64_t now_ms)
{
    return act_session_active_ms(s, now_ms) + act_session_paused_ms(s, now_ms);
}

void act_session_restore(act_session_t *s, int64_t now_ms, uint32_t gap_ms)
{
    if (!act_session_running(s)) {
        return;
    }
    /* Los tramos abiertos ya se cerraron al guardar (act_store): aquí la
     * sesión se reancla al reloj monótono de este arranque. */
    s->state = ACT_SES_PAUSED;
    s->pause_kind = ACT_PAUSE_RESTORED;
    s->seg_ms = now_ms - (int64_t)gap_ms;
    s->pause_ms = s->seg_ms; /* apagado = pausa desde el último guardado */
    s->start_ms = now_ms - (int64_t)(s->active_ms + s->paused_ms) - (int64_t)gap_ms;
    s->evidence_ms = s->seg_ms;
}
