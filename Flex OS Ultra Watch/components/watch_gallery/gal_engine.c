/*
 * gal_engine — ver include/gal_engine.h. Aquí: vida, peticiones de la
 * interfaz y la planificación de la tarea. La red está en gal_net.c y el
 * LittleFS del reloj en gal_local.c.
 */
#include "gal_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ajustes de la Galería (el resto de claves son de Flex Account: gal_acct.c). */
#define K_PICK  "g.pick"
#define K_UHOST "g.uhost"
#define K_UPORT "g.uport"

void gal__lock(gal_engine_t *e)
{
    if (e->env.lock) {
        e->env.lock(e->env.ctx);
    }
}

void gal__unlock(gal_engine_t *e)
{
    if (e->env.unlock) {
        e->env.unlock(e->env.ctx);
    }
}

uint32_t gal__now(gal_engine_t *e)
{
    return e->env.now ? e->env.now(e->env.ctx) : 0;
}

void *gal__alloc(gal_engine_t *e, size_t n)
{
    return e->env.alloc ? e->env.alloc(n) : malloc(n);
}

void gal__free(gal_engine_t *e, void *p)
{
    if (!p) {
        return;
    }
    if (e->env.free) {
        e->env.free(p);
    } else {
        free(p);
    }
}

bool gal__mem_ok(gal_engine_t *e, size_t n)
{
    return !e->env.mem_ok || e->env.mem_ok(e->env.ctx, n);
}

static void kick(gal_engine_t *e)
{
    if (e->env.kick) {
        e->env.kick(e->env.ctx);
    }
}

gal_xfer_t *gal__xfer(gal_engine_t *e, int id)
{
    for (int i = 0; i < GAL_XFERS; i++) {
        if (e->xq[i].state != GAL_XS_FREE && e->xq[i].id == id) {
            return &e->xq[i];
        }
    }
    return NULL;
}

void gal__mark_onwatch(gal_engine_t *e)
{
    gal_ref_t none;
    gal_view_build(e->local, e->nl, e->remote, e->nr, GAL_TAB_REMOTE, &none, 0); /* solo las marcas */
}

/* ======================= estado de las fuentes ======================= */

/* Lo que se sabe sin preguntar a nadie: sin configurar, sin Wi-Fi, a comprobar. */
static void health_from_config(gal_engine_t *e, uint32_t now)
{
    gal_health_t *u = &e->health[GAL_SRC_ULTRA], *c = &e->health[GAL_SRC_CLOUD], *p = &e->health[GAL_SRC_PHONE];
    e->health[GAL_SRC_LOCAL].state = GAL_ST_READY;
    if (!e->ultra_host[0]) {
        memset(u, 0, sizeof(*u));
        memset(p, 0, sizeof(*p));
    } else if (!e->wifi) {
        u->state = GAL_ST_NOWIFI;
        gal_copyz(u->detail, sizeof(u->detail), "Sin Wi-Fi");
    } else if (u->state == GAL_ST_UNSET || u->state == GAL_ST_NOWIFI) {
        u->state = GAL_ST_CHECKING;
        u->next_ms = now;
        u->detail[0] = 0;
    }
    if (!e->ultra_host[0] || (u->state != GAL_ST_READY && p->state == GAL_ST_READY)) {
        memset(p, 0, sizeof(*p)); /* el teléfono solo existe a través del P4 */
    }
    if (!e->acct.have) {
        memset(c, 0, sizeof(*c));
    } else if (!e->wifi) {
        c->state = GAL_ST_NOWIFI;
        gal_copyz(c->detail, sizeof(c->detail), "Sin Wi-Fi");
    } else if (!gal_acct_usable(&e->acct)) {
        c->state = GAL_ST_PAIR;
        gal_copyz(c->detail, sizeof(c->detail), e->acct.link_detail);
    } else if (c->state == GAL_ST_UNSET || c->state == GAL_ST_NOWIFI || c->state == GAL_ST_PAIR) {
        c->state = GAL_ST_CHECKING;
        c->next_ms = now;
        c->detail[0] = 0;
    }
}

/* ======================= vida ======================= */

static void load_settings(gal_engine_t *e)
{
    const gal_store_t *st = e->env.store;
    uint8_t pick = GAL_PICK_AUTO;
    size_t len = 1;
    if (st && st->get && st->get(st->ctx, K_PICK, &pick, &len) && len == 1 && pick < GAL_PICK_N) {
        e->pick = pick;
    }
    char host[40];
    len = sizeof(host) - 1;
    if (st && st->get && st->get(st->ctx, K_UHOST, host, &len) && len < sizeof(host)) {
        host[len] = 0;
        gal_copyz(e->ultra_host, sizeof(e->ultra_host), host);
    }
    uint16_t port = 0;
    len = sizeof(port);
    if (st && st->get && st->get(st->ctx, K_UPORT, &port, &len) && len == sizeof(port) && port) {
        e->ultra_port = port;
    }
}

gal_engine_t *gal_engine_new(const gal_env_t *env)
{
    gal_engine_t *e = env->alloc ? env->alloc(sizeof(*e)) : malloc(sizeof(*e));
    if (!e) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    e->env = *env;
    e->pick = GAL_PICK_AUTO;
    e->ultra_port = GAL_ULTRA_PORT;
    gal_copyz(e->cloud_host, sizeof(e->cloud_host), GAL_ACCT_HOST);
    e->cloud_port = 443;
    e->cloud_tls = true;
    e->active = GAL_SRC_NONE;
    e->remote_src = GAL_SRC_NONE;
    e->next_xid = 1;
    load_settings(e);
    gal_acct_init(&e->acct, env->store, env->mac_hex, env->version);
    gal_conn_init(&e->lan, env->io_lan);
    gal_conn_init(&e->cloud, env->io_cloud);
    health_from_config(e, gal__now(e));
    return e;
}

static void free_view(gal_engine_t *e)
{
    gal__free(e, e->view_zoom);
    gal__free(e, e->view_fit);
    e->view_zoom = e->view_fit = NULL;
    e->view_zoom_sz = e->view_fit_sz = 0;
}

static void free_thumbs(gal_engine_t *e)
{
    gal__free(e, e->thumb_mem);
    e->thumb_mem = NULL;
    memset(e->thumbs, 0, sizeof(e->thumbs));
}

void gal_engine_free(gal_engine_t *e)
{
    if (!e) {
        return;
    }
    gal_conn_close(&e->lan);
    gal_conn_close(&e->cloud);
    free_view(e);
    free_thumbs(e);
    gal__free(e, e->json);
    gal__free(e, e->parts);
    memset(e->cookie, 0, sizeof(e->cookie));
    memset(&e->acct, 0, sizeof(e->acct));
    void (*fr)(void *) = e->env.free;
    if (fr) {
        fr(e);
    } else {
        free(e);
    }
}

bool gal_engine_open(gal_engine_t *e)
{
    const size_t tb = (size_t)GAL_THUMBS * THUMB_PX * sizeof(uint16_t);
    gal__lock(e);
    bool ok = true;
    if (!e->json) {
        e->json = gal__mem_ok(e, GAL_JSON_MAX) ? gal__alloc(e, GAL_JSON_MAX) : NULL;
        ok = e->json != NULL;
    }
    if (ok && !e->thumb_mem) {
        e->thumb_mem = gal__mem_ok(e, tb) ? gal__alloc(e, tb) : NULL;
        if (e->thumb_mem) {
            memset(e->thumbs, 0, sizeof(e->thumbs));
            for (int i = 0; i < GAL_THUMBS; i++) {
                e->thumbs[i].px = e->thumb_mem + (size_t)i * THUMB_PX;
            }
        }
        ok = e->thumb_mem != NULL;
    }
    if (ok) {
        const uint32_t now = gal__now(e);
        e->open = true;
        e->scan_req = true;
        e->list_req = true;
        e->abort_job = false;
        for (int s = GAL_SRC_ULTRA; s < GAL_SRC_N; s++) {
            if (e->health[s].state != GAL_ST_UNSET) {
                e->health[s].next_ms = now; /* se comprueba al abrir, sin esperar a la espera */
            }
        }
        health_from_config(e, now);
    }
    gal__unlock(e);
    kick(e);
    return ok;
}

void gal_engine_close(gal_engine_t *e)
{
    gal__lock(e);
    e->open = false;
    e->abort_job = true;
    e->view_req = false;
    e->view_state = GAL_V_IDLE;
    e->view_gen++;
    e->pair_req = false;
    gal__unlock(e);
    kick(e);
}

bool gal_engine_busy(gal_engine_t *e)
{
    gal__lock(e);
    bool busy = false;
    for (int i = 0; i < GAL_XFERS; i++) {
        const uint8_t s = e->xq[i].state;
        busy = busy || s == GAL_XS_QUEUED || s == GAL_XS_RUN || s == GAL_XS_WAIT;
    }
    gal__unlock(e);
    return busy;
}

bool gal_engine_should_abort(gal_engine_t *e)
{
    return e->abort_job;
}

/* ======================= configuración ======================= */

void gal_set_wifi(gal_engine_t *e, bool connected)
{
    gal__lock(e);
    if (e->wifi != connected) {
        e->wifi = connected;
        if (!connected) {
            e->abort_job = true;
        }
        health_from_config(e, gal__now(e));
        if (connected) {
            e->list_req = true;
        }
    }
    gal__unlock(e);
    kick(e);
}

void gal_set_pick(gal_engine_t *e, gal_pick_t p)
{
    if (p >= GAL_PICK_N) {
        return;
    }
    gal__lock(e);
    if (e->pick != p) {
        e->pick = p;
        const uint8_t v = (uint8_t)p;
        if (e->env.store && e->env.store->set) {
            e->env.store->set(e->env.store->ctx, K_PICK, &v, 1);
        }
    }
    gal__unlock(e);
    kick(e);
}

static bool host_ok(const char *h)
{
    /* una IPv4 o un nombre de la red local: letras, cifras, '.', '-' */
    if (!h || !h[0] || strlen(h) >= 40) {
        return false;
    }
    for (const char *p = h; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) {
            return false;
        }
    }
    return true;
}

bool gal_set_ultra(gal_engine_t *e, const char *host, uint16_t port)
{
    if (!host_ok(host)) {
        return false;
    }
    gal__lock(e);
    const bool changed = strcmp(e->ultra_host, host) || e->ultra_port != (port ? port : GAL_ULTRA_PORT);
    if (changed) {
        gal_copyz(e->ultra_host, sizeof(e->ultra_host), host);
        e->ultra_port = port ? port : GAL_ULTRA_PORT;
        memset(e->cookie, 0, sizeof(e->cookie)); /* la sesión era de otro servidor */
        memset(&e->health[GAL_SRC_ULTRA], 0, sizeof(gal_health_t));
        memset(&e->health[GAL_SRC_PHONE], 0, sizeof(gal_health_t));
        const gal_store_t *st = e->env.store;
        if (st && st->set) {
            st->set(st->ctx, K_UHOST, e->ultra_host, strlen(e->ultra_host));
            st->set(st->ctx, K_UPORT, &e->ultra_port, sizeof(e->ultra_port));
        }
        health_from_config(e, gal__now(e));
    }
    gal__unlock(e);
    kick(e);
    return true;
}

bool gal_ultra_pair(gal_engine_t *e, const char *code)
{
    if (!code || strlen(code) != 6 || strspn(code, "0123456789") != 6) {
        return false;
    }
    gal__lock(e);
    const bool ok = e->ultra_host[0] != 0;
    if (ok) {
        gal_copyz(e->pair_code, sizeof(e->pair_code), code);
        e->pair_req = true;
        e->pairing = true;
        gal_copyz(e->pair_msg, sizeof(e->pair_msg), "Conectando con Flex OS Ultra…");
    }
    gal__unlock(e);
    kick(e);
    return ok;
}

void gal_ultra_forget(gal_engine_t *e)
{
    gal__lock(e);
    memset(e->cookie, 0, sizeof(e->cookie));
    e->ultra_host[0] = 0;
    e->pair_msg[0] = 0;
    memset(&e->phone, 0, sizeof(e->phone));
    const gal_store_t *st = e->env.store;
    if (st && st->erase) {
        st->erase(st->ctx, K_UHOST);
        st->erase(st->ctx, K_UPORT);
    }
    health_from_config(e, gal__now(e));
    gal__unlock(e);
    kick(e);
}

/* Las órdenes de Flex Account se aplican en la tarea (gal_engine_step), que es dueña de e->acct. */
enum { AR_LINK = 0, AR_CANCEL, AR_FORGET };

void gal_acct_link(gal_engine_t *e)
{
    gal__lock(e);
    e->acct_req[AR_LINK] = 1;
    gal__unlock(e);
    kick(e);
}

void gal_acct_link_cancel(gal_engine_t *e)
{
    gal__lock(e);
    e->acct_req[AR_CANCEL] = 1;
    gal__unlock(e);
    kick(e);
}

void gal_acct_unlink(gal_engine_t *e)
{
    gal__lock(e);
    e->acct_req[AR_FORGET] = 1;
    gal__unlock(e);
    kick(e);
}

void gal_engine_set_cloud(gal_engine_t *e, const char *host, uint16_t port, bool tls, const uint8_t pub[65],
                          const char *key_id)
{
    gal__lock(e);
    gal_copyz(e->cloud_host, sizeof(e->cloud_host), host);
    e->cloud_port = port;
    e->cloud_tls = tls;
    gal_acct_set_service(&e->acct, host, port, tls, pub, key_id);
    gal__unlock(e);
}

/* ======================= lectura ======================= */

void gal_engine_lock(gal_engine_t *e)
{
    gal__lock(e);
}

void gal_engine_unlock(gal_engine_t *e)
{
    gal__unlock(e);
}

void gal_status(gal_engine_t *e, gal_status_t *o)
{
    memset(o, 0, sizeof(*o));
    gal__lock(e);
    o->open = e->open;
    o->wifi = e->wifi;
    o->pick = (gal_pick_t)e->pick;
    o->choice = e->choice;
    memcpy(o->health, e->health, sizeof(o->health));
    gal_copyz(o->ultra_host, sizeof(o->ultra_host), e->ultra_host);
    o->ultra_port = e->ultra_port;
    o->ultra_paired = e->cookie[0] != 0;
    o->ultra_pairing = e->pairing;
    gal_copyz(o->ultra_pair_msg, sizeof(o->ultra_pair_msg), e->pair_msg);
    o->phone = e->phone;
    o->acct_flow = e->acct.flow;
    o->acct_link = e->acct.link;
    gal_copyz(o->acct_code, sizeof(o->acct_code), e->acct.flow == GAL_ACCT_CODE ? e->acct.code : "");
    gal_copyz(o->acct_stage, sizeof(o->acct_stage), e->acct.stage);
    gal_copyz(o->acct_error, sizeof(o->acct_error), e->acct.error);
    gal_copyz(o->acct_address, sizeof(o->acct_address), e->acct.address);
    gal_copyz(o->acct_detail, sizeof(o->acct_detail), e->acct.link_detail);
    o->local_gen = e->local_gen;
    o->remote_gen = e->remote_gen;
    o->nl = e->nl;
    o->nr = e->nr;
    o->remote_state = e->remote_state;
    o->remote_src = e->remote_src;
    o->remote_more = e->remote_more;
    gal_copyz(o->remote_err, sizeof(o->remote_err), e->remote_err);
    o->scanning = e->scanning || e->scan_req;
    if (e->me_valid) {
        o->q_total = e->me.q_total;
        o->q_used = e->me.q_used;
        o->q_avail = e->me.q_avail;
        gal_copyz(o->q_state, sizeof(o->q_state), e->me.q_state);
    }
    o->view_state = e->view_state;
    o->view_gen = e->view_gen;
    for (int i = 0; i < GAL_XFERS; i++) {
        const uint8_t s = e->xq[i].state;
        o->xfers_active += s == GAL_XS_QUEUED || s == GAL_XS_RUN || s == GAL_XS_WAIT;
    }
    o->xfer_gen = e->xfer_gen;
    gal__unlock(e);
}

const gal_item_t *gal_list(gal_engine_t *e, int which, int *n)
{
    *n = which ? e->nr : e->nl;
    return which ? e->remote : e->local;
}

int gal_build_view(gal_engine_t *e, gal_tab_t tab, gal_ref_t *out, int cap)
{
    return gal_view_build(e->local, e->nl, e->remote, e->nr, tab, out, cap);
}

const gal_item_t *gal_ref_item(gal_engine_t *e, gal_ref_t r)
{
    if (r.list == 0) {
        return r.idx < e->nl ? &e->local[r.idx] : NULL;
    }
    return r.idx < e->nr ? &e->remote[r.idx] : NULL;
}

void gal_refresh(gal_engine_t *e)
{
    gal__lock(e);
    const uint32_t now = gal__now(e);
    if (now - e->scan_ms >= 3000u) {
        e->scan_req = true; /* "Actualizar" seguido no deja sin turno a la lista remota */
    }
    e->list_req = true;
    for (int s = GAL_SRC_ULTRA; s < GAL_SRC_N; s++) {
        if (e->health[s].state == GAL_ST_DOWN || e->health[s].state == GAL_ST_CHECKING) {
            e->health[s].next_ms = now; /* "Actualizar" no espera a la espera creciente */
        }
    }
    gal__unlock(e);
    kick(e);
}

void gal_more(gal_engine_t *e)
{
    gal__lock(e);
    if (e->remote_more && e->remote_state == GAL_L_READY) {
        e->more_req = true;
    }
    gal__unlock(e);
    kick(e);
}

/* ======================= miniaturas ======================= */

static void thumb_key(const gal_item_t *it, char *out, size_t n)
{
    if (it->src == GAL_SRC_LOCAL) {
        /* con el volumen: la misma ruta en el reloj y en la microSD son dos fotos */
        snprintf(out, n, "0|%u|%s|%llu|%lu", (unsigned)it->vol, it->path, (unsigned long long)it->size,
                 (unsigned long)it->tver);
    } else {
        snprintf(out, n, "%d|%s|%lu", it->src, it->id, (unsigned long)it->tver);
    }
}

static int thumb_find(gal_engine_t *e, const char *key)
{
    for (int i = 0; i < GAL_THUMBS; i++) {
        if (e->thumbs[i].state != GAL_TH_EMPTY && !strcmp(e->thumbs[i].key, key)) {
            return i;
        }
    }
    return -1;
}

void gal_thumb_request(gal_engine_t *e, const gal_item_t *it)
{
    /* Local: un JPEG (se decodifica) o lo que ya tiene miniatura en la
     * biblioteca del reloj (también vídeos y dibujos); remoto: si el servidor la tiene. */
    const bool lib_thumb = it && it->src == GAL_SRC_LOCAL && (it->flags & GAL_I_THUMB);
    if (!it || (it->flags & GAL_I_LOCKED) || (it->kind == GAL_K_DRAW && !lib_thumb) ||
        (it->src != GAL_SRC_LOCAL && !(it->flags & GAL_I_THUMB)) ||
        (it->src == GAL_SRC_LOCAL && it->fmt != GAL_F_JPEG && !lib_thumb)) {
        return; /* sin miniatura posible: la celda dice por qué, sin imagen inventada */
    }
    char key[sizeof(e->thumbs[0].key)];
    thumb_key(it, key, sizeof(key));
    gal__lock(e);
    if (!e->thumb_mem) {
        gal__unlock(e);
        return;
    }
    const uint32_t now = gal__now(e);
    int i = thumb_find(e, key);
    if (i >= 0) {
        gal_thumb_t *t = &e->thumbs[i];
        t->lru = now;
        if (t->state == GAL_TH_FAILED && now - t->fail_ms > 60000u) {
            t->state = GAL_TH_WANTED; /* se reintenta, pero no en bucle */
        }
        const bool wanted = t->state == GAL_TH_WANTED; /* leído con el cerrojo: la tarea lo cambia */
        gal__unlock(e);
        if (wanted) {
            kick(e);
        }
        return;
    }
    int victim = -1;
    for (int k = 0; k < GAL_THUMBS && victim < 0; k++) {
        if (e->thumbs[k].state == GAL_TH_EMPTY) {
            victim = k;
        }
    }
    for (int k = 0; k < GAL_THUMBS && victim < 0; k++) {
        (void)k;
        uint32_t best = 0;
        for (int j = 0; j < GAL_THUMBS; j++) {
            const gal_thumb_t *t = &e->thumbs[j];
            if (t->pins || t->state == GAL_TH_LOADING) {
                continue;
            }
            const uint32_t age = now - t->lru;
            if (victim < 0 || age > best) {
                best = age;
                victim = j;
            }
        }
        break;
    }
    if (victim >= 0) {
        gal_thumb_t *t = &e->thumbs[victim];
        gal_copyz(t->key, sizeof(t->key), key);
        t->item = *it;
        t->state = GAL_TH_WANTED;
        t->lru = now;
        t->pins = 0;
    }
    gal__unlock(e);
    kick(e);
}

int gal_thumb_acquire(gal_engine_t *e, const gal_item_t *it, const uint16_t **px, int *state)
{
    char key[sizeof(e->thumbs[0].key)];
    thumb_key(it, key, sizeof(key));
    gal__lock(e);
    const int i = e->thumb_mem ? thumb_find(e, key) : -1;
    int ret = -1;
    if (state) {
        *state = i >= 0 ? e->thumbs[i].state : GAL_TH_EMPTY;
    }
    if (i >= 0 && e->thumbs[i].state == GAL_TH_READY && e->thumbs[i].pins < 255) {
        e->thumbs[i].pins++;
        e->thumbs[i].lru = gal__now(e);
        *px = e->thumbs[i].px;
        ret = i;
    }
    gal__unlock(e);
    return ret;
}

void gal_thumb_release(gal_engine_t *e, int slot)
{
    if (slot < 0 || slot >= GAL_THUMBS) {
        return;
    }
    gal__lock(e);
    if (e->thumbs[slot].pins) {
        e->thumbs[slot].pins--;
    }
    gal__unlock(e);
}

/* ======================= visor ======================= */

bool gal_view_open(gal_engine_t *e, const gal_item_t *it)
{
    if (!it || (it->flags & GAL_I_LOCKED) || !gal_fmt_viewable((gal_fmt_t)it->fmt) || it->kind == GAL_K_DRAW) {
        return false;
    }
    gal__lock(e);
    e->view_item = *it;
    e->view_req = true;
    e->view_state = GAL_V_LOADING;
    e->view_gen++;
    atomic_store(&e->view_done, 0);
    e->view_total = 0;
    e->view_err[0] = 0;
    e->abort_job = true; /* lo que se estuviera haciendo cede el sitio (una descarga sigue después) */
    gal__unlock(e);
    kick(e);
    return true;
}

void gal_view_close(gal_engine_t *e)
{
    gal__lock(e);
    e->view_req = false;
    e->view_state = GAL_V_IDLE;
    e->view_gen++;
    e->abort_job = true;
    gal__unlock(e);
    kick(e);
}

void gal_view_get(gal_engine_t *e, gal_view_t *o)
{
    memset(o, 0, sizeof(*o));
    gal__lock(e);
    o->state = e->view_state;
    o->gen = e->view_gen;
    o->done = atomic_load_explicit(&e->view_done, memory_order_relaxed);
    o->total = e->view_total;
    o->src_w = e->src_w;
    o->src_h = e->src_h;
    gal_copyz(o->err, sizeof(o->err), e->view_err);
    if (e->view_state == GAL_V_READY) {
        o->fit = e->view_fit;
        o->fit_w = e->fit_w;
        o->fit_h = e->fit_h;
        o->zoom = e->view_zoom;
        o->zoom_w = e->zoom_w;
        o->zoom_h = e->zoom_h;
    }
    gal__unlock(e);
}

/* ======================= transferencias ======================= */

static bool remote_src(int s)
{
    return s == GAL_SRC_ULTRA || s == GAL_SRC_CLOUD || s == GAL_SRC_PHONE;
}

static bool same_job(const gal_xfer_t *x, int op, const gal_item_t *it, int dest)
{
    const bool live = x->state == GAL_XS_QUEUED || x->state == GAL_XS_RUN || x->state == GAL_XS_WAIT;
    return live && x->op == op && x->dest == dest && x->item.src == it->src && !strcmp(x->item.id, it->id) &&
           !strcmp(x->item.path, it->path);
}

/* Con el cerrojo. why: por qué no (para la interfaz). */
static bool can_op(gal_engine_t *e, int op, const gal_item_t *it, int dest, char *why, size_t n)
{
    char tmp[2];
    if (!why) {
        why = tmp;
        n = sizeof(tmp);
    }
    why[0] = 0;
    if (!it) {
        return false;
    }
    if (it->flags & GAL_I_LOCKED) {
        gal_copyz(why, n, "Contenido protegido: ábrelo en Flex OS Ultra");
        return false;
    }
    for (int i = 0; i < GAL_XFERS; i++) {
        if (same_job(&e->xq[i], op, it, dest)) {
            gal_copyz(why, n, "Ya está en la cola de transferencias");
            return false;
        }
    }
    if (op == GAL_X_DOWNLOAD) {
        if (!remote_src(it->src)) {
            return false;
        }
        if (it->flags & GAL_I_ONWATCH) {
            gal_copyz(why, n, "Ya está en el reloj");
            return false;
        }
        if (it->kind != GAL_K_PHOTO) {
            gal_copyz(why, n, "El reloj solo guarda fotos");
            return false;
        }
        if (it->size && e->env.fs_free) {
            const uint64_t fr = e->env.fs_free(e->env.ctx);
            if (fr < it->size + GAL_RESERVE + 16384u) {
                gal_copyz(why, n, "No queda espacio en el reloj");
                return false;
            }
        }
        return true;
    }
    if (op == GAL_X_UPLOAD) {
        if (it->src != GAL_SRC_LOCAL || it->kind != GAL_K_PHOTO || !remote_src(dest)) {
            return false;
        }
        if ((it->flags & GAL_I_REMOTE) && it->origin_src == dest) {
            snprintf(why, n, "Ya está en %s (de ahí vino)", gal_src_name(dest));
            return false;
        }
        const gal_health_t *h = &e->health[dest];
        if (h->state == GAL_ST_UNSET || h->state == GAL_ST_PAIR) {
            snprintf(why, n, "%s no está conectado", gal_src_name(dest));
            return false;
        }
        if (dest == GAL_SRC_ULTRA && it->size > 6u * 1024u * 1024u) {
            gal_copyz(why, n, "Flex OS Ultra admite fotos de hasta 6 MB"); /* FML_LIMIT_PHOTO */
            return false;
        }
        return true;
    }
    if (op == GAL_X_TRASH) {
        if (it->src == GAL_SRC_LOCAL) {
            if (e->env.local && e->env.local->trash) {
                return true; /* la biblioteca decide: papelera en el reloj, borrado en la microSD */
            }
            return !strncmp(it->path, GAL_DIR_PHOTO "/", 10) || !strncmp(it->path, GAL_DIR_PAINT "/", 7);
        }
        const gal_health_t *h = &e->health[it->src];
        if (h->state == GAL_ST_UNSET || h->state == GAL_ST_PAIR) {
            snprintf(why, n, "%s no está conectado", gal_src_name(it->src));
            return false;
        }
        return remote_src(it->src);
    }
    return false;
}

static int enqueue(gal_engine_t *e, int op, const gal_item_t *it, int dest, char *why, size_t n)
{
    gal__lock(e);
    int id = 0;
    if (can_op(e, op, it, dest, why, n)) {
        gal_xfer_t *x = NULL;
        for (int i = 0; i < GAL_XFERS && !x; i++) {
            if (e->xq[i].state == GAL_XS_FREE) {
                x = &e->xq[i];
            }
        }
        for (int i = 0; i < GAL_XFERS && !x; i++) { /* las terminadas dejan su sitio */
            const uint8_t s = e->xq[i].state;
            if (s == GAL_XS_DONE || s == GAL_XS_CANCEL) {
                x = &e->xq[i];
            }
        }
        if (!x) {
            gal_copyz(why, n, "La cola de transferencias está llena");
        } else {
            memset(x, 0, sizeof(*x));
            x->id = e->next_xid++;
            if (!e->next_xid) {
                e->next_xid = 1;
            }
            x->op = (uint8_t)op;
            x->dest = (int8_t)dest;
            x->item = *it;
            x->total = it->size;
            x->state = GAL_XS_QUEUED;
            gal_copyz(x->msg, sizeof(x->msg), "En cola");
            id = x->id;
            e->xfer_gen++;
        }
    }
    gal__unlock(e);
    if (id) {
        kick(e);
    }
    return id;
}

int gal_download(gal_engine_t *e, const gal_item_t *it, char *why, size_t n)
{
    return enqueue(e, GAL_X_DOWNLOAD, it, it ? it->src : -1, why, n);
}

int gal_upload(gal_engine_t *e, const gal_item_t *it, int dest, char *why, size_t n)
{
    return enqueue(e, GAL_X_UPLOAD, it, dest, why, n);
}

int gal_trash(gal_engine_t *e, const gal_item_t *it, char *why, size_t n)
{
    return enqueue(e, GAL_X_TRASH, it, it ? it->src : -1, why, n);
}

bool gal_can_download(gal_engine_t *e, const gal_item_t *it)
{
    gal__lock(e);
    const bool ok = can_op(e, GAL_X_DOWNLOAD, it, it ? it->src : -1, NULL, 0);
    gal__unlock(e);
    return ok;
}

bool gal_can_upload(gal_engine_t *e, const gal_item_t *it, int dest)
{
    gal__lock(e);
    const bool ok = can_op(e, GAL_X_UPLOAD, it, dest, NULL, 0);
    gal__unlock(e);
    return ok;
}

bool gal_can_trash(gal_engine_t *e, const gal_item_t *it)
{
    gal__lock(e);
    const bool ok = can_op(e, GAL_X_TRASH, it, it ? it->src : -1, NULL, 0);
    gal__unlock(e);
    return ok;
}

bool gal_can(gal_engine_t *e, int op, const gal_item_t *it, int dest, char *why, size_t n)
{
    gal__lock(e);
    const bool ok = can_op(e, op, it, op == GAL_X_UPLOAD ? dest : (it ? it->src : -1), why, n);
    gal__unlock(e);
    return ok;
}

void gal_xfer_cancel(gal_engine_t *e, int id)
{
    gal__lock(e);
    gal_xfer_t *x = gal__xfer(e, id);
    if (x && (x->state == GAL_XS_QUEUED || x->state == GAL_XS_WAIT || x->state == GAL_XS_FAIL)) {
        x->cancel = true;
        if (!x->up_id[0]) {
            x->state = GAL_XS_CANCEL;
            gal_copyz(x->msg, sizeof(x->msg), "Cancelada");
        } else {
            x->state = GAL_XS_QUEUED; /* la tarea suelta la reserva de cuota en el servidor */
        }
        e->xfer_gen++;
    } else if (x && x->state == GAL_XS_RUN) {
        x->cancel = true;
        e->abort_job = true;
    }
    gal__unlock(e);
    kick(e);
}

void gal_xfer_retry(gal_engine_t *e, int id)
{
    gal__lock(e);
    gal_xfer_t *x = gal__xfer(e, id);
    if (x && (x->state == GAL_XS_FAIL || x->state == GAL_XS_WAIT)) {
        x->state = GAL_XS_QUEUED;
        x->fails = 0;
        x->next_ms = 0;
        gal_copyz(x->msg, sizeof(x->msg), "En cola");
        e->xfer_gen++;
    }
    gal__unlock(e);
    kick(e);
}

void gal_xfer_clear(gal_engine_t *e)
{
    gal__lock(e);
    for (int i = 0; i < GAL_XFERS; i++) {
        const uint8_t s = e->xq[i].state;
        if (s == GAL_XS_DONE || s == GAL_XS_CANCEL || s == GAL_XS_FAIL) {
            memset(&e->xq[i], 0, sizeof(e->xq[i]));
        }
    }
    e->xfer_gen++;
    gal__unlock(e);
}

int gal_xfers(gal_engine_t *e, gal_xfer_info_t *out, int cap)
{
    int n = 0;
    gal__lock(e);
    for (int i = 0; i < GAL_XFERS && n < cap; i++) {
        const gal_xfer_t *x = &e->xq[i];
        if (x->state == GAL_XS_FREE) {
            continue;
        }
        gal_xfer_info_t *o = &out[n++];
        o->id = x->id;
        o->op = x->op;
        o->state = x->state;
        o->dest = x->dest;
        gal_copyz(o->name, sizeof(o->name), x->item.name);
        o->done = x->done;
        o->total = x->total;
        gal_copyz(o->msg, sizeof(o->msg), x->msg);
    }
    gal__unlock(e);
    return n;
}

void gal_engine_mem(gal_engine_t *e, gal_mem_t *o)
{
    memset(o, 0, sizeof(*o));
    gal__lock(e);
    o->engine_bytes = sizeof(*e);
    o->json_bytes = e->json ? GAL_JSON_MAX : 0;
    o->thumb_bytes = e->thumb_mem ? (size_t)GAL_THUMBS * THUMB_PX * 2 : 0;
    o->view_bytes = e->view_zoom_sz + e->view_fit_sz;
    o->parts_bytes = e->parts_bytes;
    o->http_reqs = e->lan.reqs + e->cloud.reqs;
    o->http_opens = e->lan.opens + e->cloud.opens;
    o->thumbs_done = e->thumbs_done;
    o->thumbs_failed = e->thumbs_failed;
    o->views_done = e->views_done;
    gal__unlock(e);
}

/* ======================= la tarea ======================= */

typedef struct {
    gal_engine_t *e;
    int w, h;          /* tamaños pedidos al decodificar */
    bool want_zoom;
    uint16_t *buf;
    size_t sz;
} view_dst_t;

static uint16_t *view_dst(void *ud, int sw, int sh, int *dw, int *dh)
{
    view_dst_t *d = ud;
    gal_engine_t *e = d->e;
    int fw, fh, zw, zh;
    gal_jpeg_fit(sw, sh, GAL_VIEW_W, GAL_VIEW_H, &fw, &fh);
    gal_jpeg_fit(sw, sh, GAL_VIEW_MAX_W, GAL_VIEW_MAX_H, &zw, &zh);
    /* la banda de gal_jpeg y la tabla de columnas, con margen */
    const size_t band = (size_t)GAL_JPEG_BAND * (size_t)zw * 16u + (size_t)sw * 2u + GAL_JPEG_WORK;
    d->want_zoom = (zw > fw || zh > fh) && gal__mem_ok(e, (size_t)zw * zh * 2u + (size_t)fw * fh * 2u + band);
    if (d->want_zoom) {
        d->buf = gal__alloc(e, (size_t)zw * zh * 2u);
        d->want_zoom = d->buf != NULL; /* si no hay sitio para el 2x, solo encajada */
    }
    d->w = d->want_zoom ? zw : fw;
    d->h = d->want_zoom ? zh : fh;
    const size_t need = (size_t)d->w * (size_t)d->h * 2u;
    if (!d->buf) {
        if (!gal__mem_ok(e, need + (size_t)GAL_JPEG_BAND * (size_t)fw * 16u + GAL_JPEG_WORK)) {
            return NULL;
        }
        d->buf = gal__alloc(e, need);
    }
    d->sz = d->buf ? need : 0;
    *dw = d->w;
    *dh = d->h;
    return d->buf;
}

static void do_view(gal_engine_t *e)
{
    gal__lock(e);
    const gal_item_t it = e->view_item;
    const uint32_t gen = e->view_gen;
    e->view_req = false;
    e->abort_job = false;
    free_view(e); /* la interfaz ya no enseña los anteriores (pidió otro) */
    gal__unlock(e);

    gal_jpeg_src_t src;
    void *sctx = NULL;
    memset(&src, 0, sizeof(src));
    int st = it.src == GAL_SRC_LOCAL ? gal__local_view_stream(e, &it, &src, &sctx)
                                     : gal__net_view_stream(e, &it, &src, &sctx);
    view_dst_t d = {.e = e};
    gal_jpeg_info_t info;
    memset(&info, 0, sizeof(info));
    int jr = GAL_JPG_E_INPUT;
    char err[112] = "";
    if (st == 0) {
        jr = gal_jpeg_decode(&src, GAL_FIT_CONTAIN, view_dst, &d, 0x0000, &info);
    } else {
        gal_copyz(err, sizeof(err), gal_err_text((gal_err_t)st));
    }
    if (it.src == GAL_SRC_LOCAL) {
        gal__local_view_end(e, sctx);
    } else {
        gal__net_view_end(e, sctx);
    }
    uint16_t *fit = NULL;
    size_t fit_sz = 0;
    int fw = d.w, fh = d.h;
    if (jr == GAL_JPG_OK && d.want_zoom) {
        /* el "Ajustar" sale del 2x (sin decodificar otra vez), al tamaño exacto de la pantalla */
        gal_jpeg_fit(d.w, d.h, GAL_VIEW_W, GAL_VIEW_H, &fw, &fh);
        fit_sz = (size_t)fw * fh * 2u;
        fit = gal__alloc(e, fit_sz);
        if (fit) {
            gal_rgb565_shrink(d.buf, d.w, d.h, fit, fw, fh);
        }
    }
    gal__lock(e);
    if (gen != e->view_gen) { /* se cerró o se pidió otra mientras tanto */
        gal__unlock(e);
        gal__free(e, d.buf);
        gal__free(e, fit);
        return;
    }
    if (jr == GAL_JPG_OK && (!d.want_zoom || fit)) {
        if (d.want_zoom) {
            e->view_zoom = d.buf;
            e->view_zoom_sz = d.sz;
            e->zoom_w = d.w;
            e->zoom_h = d.h;
            e->view_fit = fit;
            e->view_fit_sz = fit_sz;
        } else {
            e->view_fit = d.buf;
            e->view_fit_sz = d.sz;
            e->zoom_w = e->zoom_h = 0;
        }
        e->fit_w = fw;
        e->fit_h = fh;
        e->src_w = info.src_w;
        e->src_h = info.src_h;
        e->view_state = GAL_V_READY;
        e->views_done++;
    } else {
        gal__free(e, d.buf);
        gal__free(e, fit);
        e->view_state = GAL_V_ERROR;
        if (!err[0]) {
            gal_copyz(err, sizeof(err), jr == GAL_JPG_E_SIZE || (jr == GAL_JPG_OK && !fit) ? gal_err_text(GAL_E_NOMEM)
                                                                                       : gal_jpeg_err_text(jr));
        }
        gal_copyz(e->view_err, sizeof(e->view_err), err);
    }
    gal__unlock(e);
}

static bool do_thumb(gal_engine_t *e)
{
    gal__lock(e);
    int best = -1;
    for (int i = 0; e->thumb_mem && i < GAL_THUMBS; i++) {
        const gal_thumb_t *c = &e->thumbs[i];
        /* la de una fuente que no está lista espera a que vuelva */
        const bool reachable = c->item.src == GAL_SRC_LOCAL || e->health[c->item.src].state == GAL_ST_READY;
        if (c->state == GAL_TH_WANTED && reachable && (best < 0 || c->lru > e->thumbs[best].lru)) {
            best = i;
        }
    }
    if (best < 0) {
        gal__unlock(e);
        return false;
    }
    gal_thumb_t *t = &e->thumbs[best];
    t->state = GAL_TH_LOADING;
    e->abort_job = false;
    uint16_t *px = t->px;
    gal__unlock(e);
    const bool ok = t->item.src == GAL_SRC_LOCAL ? gal__local_thumb(e, t, px) : gal__net_thumb(e, t, px);
    gal__lock(e);
    t->state = ok ? GAL_TH_READY : GAL_TH_FAILED;
    t->fail_ms = gal__now(e);
    if (ok) {
        e->thumbs_done++;
    } else {
        e->thumbs_failed++;
    }
    gal__unlock(e);
    return true;
}

static gal_xfer_t *next_xfer(gal_engine_t *e, uint32_t now)
{
    gal_xfer_t *best = NULL;
    for (int i = 0; i < GAL_XFERS; i++) {
        gal_xfer_t *x = &e->xq[i];
        const bool due = x->state == GAL_XS_QUEUED || (x->state == GAL_XS_WAIT && (int32_t)(now - x->next_ms) >= 0);
        if (due && (!best || (uint8_t)(x->id - best->id) > 128)) {
            best = x; /* la más antigua (ids que dan la vuelta) */
        }
    }
    return best;
}

uint32_t gal_engine_step(gal_engine_t *e)
{
    uint32_t now = gal__now(e);

    /* Flex Account: las órdenes de la interfaz y una vuelta de su máquina (solo con la app abierta). */
    gal__lock(e);
    if (!e->view_req) {
        e->abort_job = false; /* el aviso de cortar era para el trabajo anterior, que ya terminó */
    }
    const bool open = e->open, wifi = e->wifi;
    if (e->acct_req[AR_FORGET]) {
        gal_acct_forget(&e->acct, e->env.store);
        memset(&e->me, 0, sizeof(e->me));
        e->me_valid = false;
    }
    if (e->acct_req[AR_CANCEL]) {
        gal_acct_cancel(&e->acct);
    }
    if (e->acct_req[AR_LINK]) {
        gal_acct_request_link(&e->acct);
    }
    memset(e->acct_req, 0, sizeof(e->acct_req));
    gal_acct_t acct = e->acct;
    gal__unlock(e);
    if (open && e->json) {
        gal_acct_step(&acct, &e->cloud, e->env.store, wifi, now, e->json, GAL_JSON_MAX);
        gal__lock(e);
        const bool forgot = e->acct_req[AR_FORGET];
        acct.cancel = acct.cancel || e->acct.cancel;
        if (!forgot) {
            e->acct = acct;
        }
        health_from_config(e, now);
        gal__unlock(e);
        memset(&acct, 0, sizeof(acct));
    }

    gal__lock(e);
    if (!e->view_req && e->view_state == GAL_V_IDLE && (e->view_fit || e->view_zoom)) {
        free_view(e); /* el visor se cerró: sus lienzos (hasta 2 MB) no esperan a otra foto */
    }
    gal__unlock(e);
    /* La biblioteca local cambió (algo nuevo, borrado, la microSD entra o sale): otra lista. */
    if (open && e->env.local && e->env.local->rev && e->env.local->list) {
        const uint32_t rev = e->env.local->rev(e->env.ctx);
        gal__lock(e);
        if (rev != e->local_rev && !e->scanning) {
            e->scan_req = true;
        }
        gal__unlock(e);
    }
    gal__lock(e);
    const bool pair = e->pair_req, view = e->view_req, scan = e->scan_req && open;
    e->pair_req = false;
    if (scan) {
        e->scan_req = false;
    }
    gal__unlock(e);
    if (pair) {
        gal__net_pair(e);
        return 10;
    }
    if (view && open) {
        do_view(e);
        return 10;
    }
    if (scan) {
        gal__local_scan(e);
        return 10;
    }
    now = gal__now(e);
    /* salud de las fuentes (la del teléfono sale de la del P4) */
    if (open && wifi) {
        for (int s = GAL_SRC_ULTRA; s <= GAL_SRC_CLOUD; s++) {
            gal__lock(e);
            const gal_health_t h = e->health[s];
            gal__unlock(e);
            if (h.state != GAL_ST_UNSET && h.state != GAL_ST_NOWIFI && (int32_t)(now - h.next_ms) >= 0 &&
                !(s == GAL_SRC_CLOUD && h.state == GAL_ST_PAIR)) {
                gal__net_health(e, s, now);
                return 10;
            }
        }
    }
    /* la fuente en uso */
    gal__lock(e);
    gal_pick_resolve((gal_pick_t)e->pick, e->health, wifi, e->active, now, &e->choice);
    const int8_t chosen = e->choice.src >= 0 && e->health[e->choice.src].state == GAL_ST_READY ? e->choice.src : GAL_SRC_NONE;
    if (e->choice.src != e->remote_src) {
        /* otra fuente: su lista, nunca la de antes con otra etiqueta */
        e->remote_src = e->choice.src;
        e->nr = 0;
        e->remote_more = false;
        e->cursor[0] = 0;
        e->ultra_rev = 0;
        e->remote_state = GAL_L_IDLE;
        e->remote_err[0] = 0;
        e->remote_gen++;
        e->list_req = true;
    }
    e->active = e->choice.src;
    const bool due = chosen >= 0 && e->remote_state == GAL_L_READY &&
                     now - e->list_ms >= (chosen == GAL_SRC_ULTRA ? GAL_ULTRA_POLL_MS : GAL_CLOUD_POLL_MS);
    const bool list = open && chosen >= 0 && (e->list_req || due);
    const bool more = open && chosen >= 0 && e->more_req && !list;
    if (list) {
        e->list_req = false;
    }
    if (more) {
        e->more_req = false;
    }
    if (open && chosen < 0 && e->choice.src >= 0 && e->remote_state != GAL_L_ERROR) {
        /* elegida pero no lista: la lista lo dice (sin inventar contenido) */
        e->remote_state = GAL_L_ERROR;
        gal_copyz(e->remote_err, sizeof(e->remote_err), e->choice.why);
        e->remote_gen++;
    }
    gal__unlock(e);
    if (list || more) {
        gal__net_list(e, more);
        return 10;
    }
    if (open && do_thumb(e)) {
        return 5;
    }
    /* transferencias (también con la app cerrada) */
    gal__lock(e);
    gal_xfer_t *x = next_xfer(e, now);
    if (x) {
        x->state = GAL_XS_RUN;
        e->abort_job = false;
        e->xfer_gen++;
    }
    gal__unlock(e);
    if (x) {
        if (x->op == GAL_X_TRASH && x->item.src == GAL_SRC_LOCAL) {
            gal__local_trash(e, x);
        } else {
            gal__net_xfer(e, x);
        }
        gal__lock(e);
        e->xfer_gen++;
        gal__unlock(e);
        return 10;
    }
    /* sin nada que hacer: cerrada y sin transferencias, se suelta lo grande */
    gal__lock(e);
    if (!e->open) {
        free_view(e);
        bool pinned = false;
        for (int i = 0; i < GAL_THUMBS; i++) {
            pinned = pinned || e->thumbs[i].pins;
        }
        if (!pinned) {
            free_thumbs(e);
        }
        bool live = false;
        for (int i = 0; i < GAL_XFERS; i++) {
            live = live || e->xq[i].state == GAL_XS_QUEUED || e->xq[i].state == GAL_XS_RUN || e->xq[i].state == GAL_XS_WAIT;
        }
        if (!live) {
            gal__free(e, e->json);
            e->json = NULL;
            gal__free(e, e->parts);
            e->parts = NULL;
            e->parts_bytes = 0;
        }
    }
    uint32_t wait = 1000;
    for (int s = GAL_SRC_ULTRA; e->open && s <= GAL_SRC_CLOUD; s++) {
        const gal_health_t *h = &e->health[s];
        if (h->state != GAL_ST_UNSET && h->state != GAL_ST_NOWIFI) {
            const int32_t left = (int32_t)(h->next_ms - now);
            if (left > 0 && (uint32_t)left < wait) {
                wait = (uint32_t)left;
            }
        }
    }
    gal__unlock(e);
    return wait < 20 ? 20 : wait;
}
