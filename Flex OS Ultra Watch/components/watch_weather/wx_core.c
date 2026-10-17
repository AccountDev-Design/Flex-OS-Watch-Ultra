/*
 * wx_core — ver include/wx_core.h. Secciones 2, 5, 9 y 10 de
 * FlexOS_Weather.cpp con la hora y la red como parámetros.
 */
#include "wx_core.h"

#include <stdio.h>
#include <string.h>

#define CLOCK_MIN_UTC 1600000000LL /* antes de 2020 es la semilla de fábrica */

static void hide(wx_core_t *c)
{
    c->have = c->fresh = c->ok_valid = false;
    c->gen++;
}

static void queue_fetch(wx_core_t *c)
{
    if (c->sel < 0 || c->sel >= (int8_t)c->nloc) {
        c->err = WXE_NOLOC;
        return;
    }
    c->cmd_loc = c->locs[c->sel]; /* copia ANTES de pedir: nunca a medio cambiar */
    c->req_fetch = true;
}

void wx_core_init(wx_core_t *c, uint32_t now_ms)
{
    memset(c, 0, sizeof(*c));
    c->sel = -1;
    c->next_ms = now_ms + WX_FIRST_MS; /* primer intento cuando el sistema ya vive */
    c->began = true;
}

void wx_core_set_locs(wx_core_t *c, const wx_loc_t *locs, uint8_t n, int8_t sel)
{
    memset(c->locs, 0, sizeof(c->locs));
    if (locs && n <= WX_LOCS) {
        memcpy(c->locs, locs, sizeof(wx_loc_t) * n);
    } else {
        n = 0;
    }
    c->nloc = wx_locs_sanitize(c->locs, n, &sel);
    if (c->nloc == 0) {
        memset(c->locs, 0, sizeof(c->locs));
    }
    c->sel = sel;
}

bool wx_core_load_cache(wx_core_t *c, const wx_data_t *w, size_t n)
{
    if (!w || n != sizeof(wx_data_t)) {
        return false;
    }
    wx_data_t tmp = *w;
    if (!wx_cache_ok(&tmp, n)) {
        return false;
    }
    /* RELOJ: la caché solo vale si es de la ciudad elegida */
    if (c->sel < 0 || !wx_loc_same(&tmp.loc, &c->locs[c->sel])) {
        return false;
    }
    c->data = tmp;
    c->have = true;
    c->fresh = false;    /* es caché: la interfaz lo dice */
    c->ok_valid = false; /* y su edad sale del reloj del sistema, no de ms */
    c->gen++;
    return true;
}

void wx_core_set_clock(wx_core_t *c, int64_t utc, uint32_t now_ms)
{
    if (utc < CLOCK_MIN_UTC) {
        return;
    }
    c->sys_utc = utc;
    c->sys_ms = now_ms;
}

static int64_t sys_now(const wx_core_t *c, uint32_t now_ms)
{
    return c->sys_utc ? c->sys_utc + (int64_t)((uint32_t)(now_ms - c->sys_ms) / 1000u) : 0;
}

void wx_core_tick(wx_core_t *c, uint32_t now_ms, bool online)
{
    if (!c->began || c->busy || c->req_fetch || c->sel < 0 || !online) {
        return; /* sin red no se molesta a nadie */
    }
    if ((int32_t)(now_ms - c->next_ms) < 0) {
        return;
    }
    /* datos frescos y dentro de la ventana: se mira otra vez en un minuto */
    if (c->have && c->fresh && c->ok_valid && now_ms - c->ok_ms < WX_REFRESH_MS) {
        c->next_ms = now_ms + 60000u;
        return;
    }
    c->next_ms = now_ms + WX_REFRESH_MS; /* lo ajusta fetch_done al terminar */
    queue_fetch(c);
}

void wx_core_refresh(wx_core_t *c, uint32_t now_ms, bool force)
{
    if (!c->began || c->busy || c->req_fetch) {
        return;
    }
    if (c->sel < 0) {
        c->err = WXE_NOLOC;
        return;
    }
    /* al abrir la app no se descarga si hay algo de hace menos de 5 min */
    if (!force && c->ok_valid && now_ms - c->ok_ms < WX_MANUAL_MIN_MS) {
        return;
    }
    c->next_ms = now_ms + WX_REFRESH_MS;
    queue_fetch(c);
}

bool wx_core_take_job(wx_core_t *c, wx_job_t *job)
{
    memset(job, 0, sizeof(*job));
    if (c->req_search) {
        c->req_search = false;
        c->qst = WXQ_BUSY;
        job->kind = WXJ_SEARCH;
        snprintf(job->query, sizeof(job->query), "%s", c->query);
        return true;
    }
    if (c->req_fetch) {
        c->req_fetch = false;
        c->busy = true; /* "Actualizando...": lo dice el estado, no gen */
        job->kind = WXJ_FETCH;
        job->loc = c->cmd_loc;
        return true;
    }
    return false;
}

void wx_core_fetch_done(wx_core_t *c, uint32_t now_ms, const wx_job_t *job, int err, const wx_data_t *data)
{
    c->busy = false;
    /* RELOJ: si mientras tanto se eligió otra ciudad (o se borró), estos
     * datos no se enseñan; la descarga de la nueva ya está pedida */
    const bool same = c->sel >= 0 && wx_loc_same(&job->loc, &c->locs[c->sel]);
    if (!same) {
        return;
    }
    if (err == WXE_NONE && data) {
        c->data = *data;
        c->have = c->fresh = c->ok_valid = true;
        c->gen++;
        c->err = WXE_NONE;
        c->fails = 0;
        c->ok_ms = now_ms;
        c->cache_dirty = true;
        c->next_ms = now_ms + WX_REFRESH_MS;
        return;
    }
    c->err = (uint8_t)(err == WXE_NONE ? WXE_JSON : err);
    if (c->fails < 250) {
        c->fails++;
    }
    /* nada de insistir cada segundo contra un servidor que no responde */
    c->next_ms = now_ms + (c->fails >= WX_RETRY_FAILS ? WX_RETRY_LONG_MS : WX_RETRY_MS);
}

void wx_core_search_done(wx_core_t *c, int err, const wx_loc_t *res, int n)
{
    c->qgen++;
    if (err != WXE_NONE) {
        c->nres = 0;
        c->qst = WXQ_ERROR;
        c->err = (uint8_t)err;
        return;
    }
    if (n > WX_RESULTS) {
        n = WX_RESULTS;
    }
    c->nres = (uint8_t)(n > 0 ? n : 0);
    if (c->nres) {
        memcpy(c->res, res, sizeof(wx_loc_t) * c->nres);
    }
    c->qst = c->nres ? WXQ_OK : WXQ_EMPTY;
}

uint8_t wx_core_status(const wx_core_t *c)
{
    if (c->busy) {
        return c->have ? WXS_UPDATING : WXS_LOADING;
    }
    if (!c->have) {
        return c->err != WXE_NONE ? WXS_ERROR : WXS_NEVER;
    }
    if (!c->fresh || c->err != WXE_NONE) {
        return WXS_STALE;
    }
    return WXS_OK;
}

int32_t wx_core_age_s(const wx_core_t *c, uint32_t now_ms)
{
    if (!c->have) {
        return -1;
    }
    if (c->ok_valid) { /* descarga de esta sesión: los ms son exactos */
        return (int32_t)((uint32_t)(now_ms - c->ok_ms) / 1000u);
    }
    const int64_t now = sys_now(c, now_ms); /* caché: solo con hora real */
    if (now && c->data.obs_time > 0 && now > c->data.obs_time) {
        return (int32_t)(now - c->data.obs_time);
    }
    return -1;
}

int64_t wx_core_now_utc(const wx_core_t *c, uint32_t now_ms)
{
    const int64_t sys = sys_now(c, now_ms);
    if (sys) {
        return sys;
    }
    if (!c->have || c->data.obs_time == 0) {
        return 0;
    }
    /* sin hora del sistema: la de la respuesta más lo corrido desde entonces */
    const uint32_t el = c->ok_valid ? (uint32_t)(now_ms - c->ok_ms) / 1000u : 0;
    return (int64_t)c->data.obs_time + el;
}

int32_t wx_core_now_local(const wx_core_t *c, uint32_t now_ms)
{
    if (!c->have || c->data.obs_time == 0) {
        return 0;
    }
    return (int32_t)(wx_core_now_utc(c, now_ms) + c->data.utc_offset);
}

void wx_core_loc_select(wx_core_t *c, uint8_t i, uint32_t now_ms)
{
    if (i >= c->nloc) {
        return;
    }
    if (c->sel == (int8_t)i && c->have && c->fresh) {
        return;
    }
    if (c->sel != (int8_t)i) {
        c->sel = (int8_t)i;
        c->locs_dirty = true;
    }
    /* los datos de otra ciudad no valen para esta */
    if (c->have && !wx_loc_same(&c->data.loc, &c->locs[i])) {
        hide(c);
    }
    c->err = WXE_NONE;
    c->fails = 0;
    c->next_ms = now_ms + WX_REFRESH_MS;
    c->req_fetch = false; /* la que hubiera pedida era de la otra */
    queue_fetch(c);
}

bool wx_core_loc_add(wx_core_t *c, const wx_loc_t *l, uint32_t now_ms)
{
    if (!l || l->name[0] == 0) {
        return false;
    }
    for (uint8_t i = 0; i < c->nloc; i++) {
        if (wx_loc_same(&c->locs[i], l)) { /* ya guardada: solo se elige */
            wx_core_loc_select(c, i, now_ms);
            return true;
        }
    }
    if (c->nloc >= WX_LOCS) {
        return false;
    }
    c->locs[c->nloc] = *l;
    c->locs[c->nloc].name[WX_NAME - 1] = 0;
    c->locs[c->nloc].region[WX_REGION - 1] = 0;
    c->nloc++;
    c->locs_dirty = true;
    wx_core_loc_select(c, (uint8_t)(c->nloc - 1), now_ms);
    return true;
}

void wx_core_loc_remove(wx_core_t *c, uint8_t i, uint32_t now_ms)
{
    if (i >= c->nloc) {
        return;
    }
    for (uint8_t k = i; k + 1 < c->nloc; k++) {
        c->locs[k] = c->locs[k + 1];
    }
    c->nloc--;
    memset(&c->locs[c->nloc], 0, sizeof(wx_loc_t));
    c->locs_dirty = true;
    if (c->nloc == 0) {
        c->sel = -1;
        c->req_fetch = false;
        if (c->have) {
            hide(c); /* sin ubicación no hay clima que enseñar */
        }
        c->err = WXE_NONE;
        return;
    }
    if (c->sel > (int8_t)i || c->sel >= (int8_t)c->nloc) {
        c->sel--; /* RELOJ: la elegida sigue siendo la misma ciudad */
    }
    if (c->sel < 0) {
        c->sel = 0;
    }
    if (!c->have || !wx_loc_same(&c->data.loc, &c->locs[c->sel])) {
        if (c->have) {
            hide(c);
        }
        c->err = WXE_NONE;
        c->fails = 0;
        c->req_fetch = false;
        queue_fetch(c);
    }
    (void)now_ms;
}

void wx_core_search(wx_core_t *c, const char *q)
{
    if (!c->began || !q || c->req_search) {
        return;
    }
    snprintf(c->query, sizeof(c->query), "%s", q);
    c->nres = 0;
    c->qst = WXQ_BUSY;
    c->req_search = true; /* convive con una descarga en curso */
    c->qgen++;
}

void wx_core_search_clear(wx_core_t *c)
{
    c->nres = 0;
    c->qst = WXQ_IDLE;
    c->req_search = false;
    c->qgen++;
}
