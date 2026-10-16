/*
 * ui_clock_logic — ver include/ui_clock_logic.h
 */
#include "ui_clock_logic.h"

#include <stdio.h>
#include <string.h>

/* ======================= Cronómetro ======================= */

void sw_reset(sw_t *s)
{
    memset(s, 0, sizeof(*s));
    s->first_no = 1;
}

void sw_start(sw_t *s, int64_t now_us)
{
    if (s->st == SW_RUN) {
        return;
    }
    if (s->st == SW_IDLE) {
        sw_reset(s);
    }
    s->t0_us = now_us;
    s->st = SW_RUN;
}

void sw_pause(sw_t *s, int64_t now_us)
{
    if (s->st != SW_RUN) {
        return;
    }
    s->accum_us += now_us - s->t0_us;
    s->st = SW_PAUSE;
}

int64_t sw_elapsed(const sw_t *s, int64_t now_us)
{
    return s->st == SW_RUN ? s->accum_us + (now_us - s->t0_us) : s->accum_us;
}

void sw_lap(sw_t *s, int64_t now_us)
{
    if (s->st != SW_RUN) {
        return;
    }
    const int64_t total = sw_elapsed(s, now_us);
    const int64_t prev = s->n ? s->laps[s->n - 1].total_us : 0;
    if (s->n == SW_MAX_LAPS) {
        /* Se descarta la más antigua; los números siguen creciendo. */
        memmove(&s->laps[0], &s->laps[1], sizeof(s->laps[0]) * (SW_MAX_LAPS - 1));
        s->n--;
        s->first_no++;
    }
    s->laps[s->n].total_us = total;
    s->laps[s->n].split_us = total - prev;
    s->n++;
}

bool sw_best_worst(const sw_t *s, int *best, int *worst)
{
    if (s->n < 3) {
        return false;
    }
    int b = 0, w = 0;
    for (int i = 1; i < s->n; i++) {
        if (s->laps[i].split_us < s->laps[b].split_us) {
            b = i;
        }
        if (s->laps[i].split_us > s->laps[w].split_us) {
            w = i;
        }
    }
    if (s->laps[b].split_us == s->laps[w].split_us) {
        return false; /* todas iguales */
    }
    *best = b;
    *worst = w;
    return true;
}

size_t sw_format(int64_t us, bool hundredths, char *buf, size_t n)
{
    if (us < 0) {
        us = 0;
    }
    const int64_t cs = us / 10000;
    const int64_t s = cs / 100;
    const int h = (int)(s / 3600), m = (int)(s / 60 % 60), sec = (int)(s % 60), c = (int)(cs % 100);
    int r;
    if (h > 0) {
        r = hundredths ? snprintf(buf, n, "%d:%02d:%02d.%02d", h, m, sec, c) : snprintf(buf, n, "%d:%02d:%02d", h, m, sec);
    } else {
        r = hundredths ? snprintf(buf, n, "%02d:%02d.%02d", m, sec, c) : snprintf(buf, n, "%02d:%02d", m, sec);
    }
    return r < 0 ? 0 : (size_t)r;
}

/* ======================= Temporizador ======================= */

static int64_t clamp_dur(int64_t us)
{
    return us < 0 ? 0 : (us > TMR_MAX_US ? TMR_MAX_US : us);
}

void tmr_start(tmr_t *t, int64_t dur_us, int64_t now_us)
{
    dur_us = clamp_dur(dur_us);
    if (dur_us <= 0) {
        return;
    }
    t->dur_us = dur_us;
    t->end_us = now_us + dur_us;
    t->left_us = dur_us;
    t->st = TMR_RUN;
}

void tmr_pause(tmr_t *t, int64_t now_us)
{
    if (t->st != TMR_RUN) {
        return;
    }
    t->left_us = t->end_us - now_us;
    if (t->left_us < 0) {
        t->left_us = 0;
    }
    t->st = TMR_PAUSE;
}

void tmr_resume(tmr_t *t, int64_t now_us)
{
    if (t->st != TMR_PAUSE) {
        return;
    }
    t->end_us = now_us + t->left_us;
    t->st = TMR_RUN;
}

void tmr_cancel(tmr_t *t)
{
    memset(t, 0, sizeof(*t));
}

void tmr_add(tmr_t *t, int64_t us, int64_t now_us)
{
    switch (t->st) {
    case TMR_RUN:
        t->end_us += us;
        if (t->end_us - now_us > TMR_MAX_US) {
            t->end_us = now_us + TMR_MAX_US;
        }
        t->dur_us = clamp_dur(t->dur_us + us);
        break;
    case TMR_PAUSE:
        t->left_us = clamp_dur(t->left_us + us);
        t->dur_us = clamp_dur(t->dur_us + us);
        break;
    default: /* parado o sonando: empieza de nuevo con ese tiempo */
        tmr_start(t, us, now_us);
        break;
    }
}

int64_t tmr_left(const tmr_t *t, int64_t now_us)
{
    switch (t->st) {
    case TMR_RUN: {
        const int64_t l = t->end_us - now_us;
        return l > 0 ? l : 0;
    }
    case TMR_PAUSE:
        return t->left_us;
    default:
        return 0;
    }
}

bool tmr_check(tmr_t *t, int64_t now_us)
{
    if (t->st == TMR_RUN && now_us >= t->end_us) {
        t->st = TMR_RING;
        t->left_us = 0;
        return true;
    }
    return false;
}

int tmr_permille(const tmr_t *t, int64_t now_us)
{
    if (t->dur_us <= 0) {
        return 0;
    }
    const int64_t l = tmr_left(t, now_us);
    return (int)(l * 1000 / t->dur_us);
}

size_t tmr_format(int64_t left_us, char *buf, size_t n)
{
    if (left_us < 0) {
        left_us = 0;
    }
    const int64_t s = (left_us + 999999) / 1000000; /* hacia arriba */
    const int h = (int)(s / 3600), m = (int)(s / 60 % 60), sec = (int)(s % 60);
    const int r = h > 0 ? snprintf(buf, n, "%d:%02d:%02d", h, m, sec) : snprintf(buf, n, "%02d:%02d", m, sec);
    return r < 0 ? 0 : (size_t)r;
}

/* ======================= Alarmas ======================= */

static bool alarm_valid(const alarm_t *a)
{
    return a->on <= 1 && a->hour < 24 && a->minute < 60 && a->days < 0x80;
}

bool alarm_matches(const alarm_t *a, const wt_datetime_t *local)
{
    if (!a->on || local->hour != a->hour || local->minute != a->minute) {
        return false;
    }
    return a->days == 0 || (a->days & (1u << local->weekday)) != 0;
}

int64_t alarm_next_local(const alarm_t *a, int64_t now_local_s)
{
    if (!a->on) {
        return -1;
    }
    const int64_t day0 = (now_local_s >= 0 ? now_local_s / 86400 : (now_local_s - 86399) / 86400);
    for (int k = 0; k <= 7; k++) {
        const int64_t d = day0 + k;
        const int64_t t = d * 86400 + a->hour * 3600 + a->minute * 60;
        if (t <= now_local_s) {
            continue;
        }
        if (a->days == 0 || (a->days & (1u << wt_weekday_from_days(d))) != 0) {
            return t;
        }
    }
    return -1;
}

int64_t alarms_next(const alarm_set_t *s, int64_t now_local_s, int *idx)
{
    int64_t best = -1;
    int bi = -1;
    for (int i = 0; i < s->n && i < AL_MAX; i++) {
        const int64_t t = alarm_next_local(&s->a[i], now_local_s);
        if (t >= 0 && (best < 0 || t < best)) {
            best = t;
            bi = i;
        }
    }
    if (idx) {
        *idx = bi;
    }
    return best;
}

size_t alarm_days_text(uint8_t days, char *buf, size_t n)
{
    static const char *const L[7] = {"D", "L", "M", "X", "J", "V", "S"};
    int r;
    days &= 0x7F;
    if (days == 0) {
        r = snprintf(buf, n, "Una vez");
    } else if (days == 0x7F) {
        r = snprintf(buf, n, "Todos los días");
    } else if (days == 0x3E) {
        r = snprintf(buf, n, "L a V");
    } else if (days == 0x41) {
        r = snprintf(buf, n, "Fin de semana");
    } else {
        /* de lunes a domingo */
        static const int order[7] = {1, 2, 3, 4, 5, 6, 0};
        size_t used = 0;
        buf[0] = '\0';
        for (int k = 0; k < 7; k++) {
            if (days & (1u << order[k])) {
                const int w = snprintf(buf + used, used < n ? n - used : 0, "%s%s", used ? " " : "", L[order[k]]);
                used += w > 0 ? (size_t)w : 0;
            }
        }
        r = (int)used;
    }
    return r < 0 ? 0 : (size_t)r;
}

size_t alarm_when_text(int64_t when_local_s, int64_t now_local_s, bool h24, char *buf, size_t n)
{
    static const char *const WD[7] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
    wt_datetime_t w;
    wt_from_seconds(when_local_s, &w);
    char t[12];
    if (h24) {
        wt_fmt_hhmm(t, sizeof(t), &w);
    } else {
        char h[8];
        wt_fmt_hhmm12(h, sizeof(h), &w);
        snprintf(t, sizeof(t), "%s %s", h, wt_ampm(&w));
    }
    const int64_t dn = (now_local_s >= 0 ? now_local_s / 86400 : (now_local_s - 86399) / 86400);
    const int64_t dw = (when_local_s >= 0 ? when_local_s / 86400 : (when_local_s - 86399) / 86400);
    int r;
    if (dw == dn) {
        r = snprintf(buf, n, "hoy %s", t);
    } else if (dw == dn + 1) {
        r = snprintf(buf, n, "mañana %s", t);
    } else {
        r = snprintf(buf, n, "%s %s", WD[w.weekday], t);
    }
    return r < 0 ? 0 : (size_t)r;
}

size_t alarm_pack(const alarm_set_t *s, uint8_t *out, size_t cap)
{
    const size_t n = s->n > AL_MAX ? AL_MAX : s->n;
    if (cap < n * 4) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        out[i * 4 + 0] = s->a[i].on ? 1 : 0;
        out[i * 4 + 1] = s->a[i].hour;
        out[i * 4 + 2] = s->a[i].minute;
        out[i * 4 + 3] = s->a[i].days & 0x7F;
    }
    return n * 4;
}

bool alarm_unpack(const uint8_t *in, size_t n, alarm_set_t *out)
{
    memset(out, 0, sizeof(*out));
    if (n % 4 != 0 || n / 4 > AL_MAX) {
        return false;
    }
    alarm_set_t t;
    memset(&t, 0, sizeof(t));
    t.n = (uint8_t)(n / 4);
    for (size_t i = 0; i < t.n; i++) {
        t.a[i] = (alarm_t){in[i * 4 + 0], in[i * 4 + 1], in[i * 4 + 2], in[i * 4 + 3]};
        if (!alarm_valid(&t.a[i])) {
            return false;
        }
    }
    *out = t;
    return true;
}
