/*
 * gal_net — la red del motor. Solo habla los contratos que ya existen:
 *  - Flex Web Server del P4 (FlexOS_MediaWeb.cpp): /api/hello, /api/pair,
 *    /api/library, /api/thumb/<id>, /api/file/<id>, /api/upload,
 *    /api/local/delete y /api/fs/overview; y su pasarela /api/cloud hacia el
 *    teléfono de Flex Storage;
 *  - Flex Cloud (cloud/docs/API.md de Flex Developer Studio): /health, /me,
 *    /files, /files/<id>/thumbnail, /download/<id>, /uploads...
 * Se llama desde la tarea, sin el cerrojo (lo toma para leer la
 * configuración y publicar).
 */
#include "gal_priv.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "files_logic.h"
#include "gal_json.h"
#include "ota_sig.h"

#define THUMB_BODY_MAX (512u * 1024u) /* thumbMaxBytes de Flex Cloud */
#define IO_CHUNK       (16u * 1024u)  /* FLEXWEB_IO_BUF: 4 bloques de LittleFS */

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

/* ======================= peticiones ======================= */

typedef struct {
    gal_engine_t *e;
    int src;
    gal_conn_t *c;
    gal_req_t rq;
    gal_resp_t rs;
    bool cloud_api;   /* respuesta con el formato de error de Flex Cloud */
    char host[GAL_HTTP_HOST_MAX];
    char path[384];
    char cookie[48];
    char bearer[48];
    char extra[96];
} call_t;

static void call_wipe(call_t *k)
{
    wipe(k->cookie, sizeof(k->cookie));
    wipe(k->bearer, sizeof(k->bearer));
}

bool gal__cloud_base(gal_engine_t *e, int src, const char **host, uint16_t *port, bool *tls, const char **prefix)
{
    if (src == GAL_SRC_CLOUD) {
        *host = e->cloud_host;
        *port = e->cloud_port;
        *tls = e->cloud_tls;
    } else {
        *host = e->ultra_host;
        *port = e->ultra_port;
        *tls = false;
    }
    *prefix = src == GAL_SRC_ULTRA ? "" : "/api/cloud";
    return (*host)[0] != 0;
}

/*
 * Prepara una petición a una fuente. path empieza por '/' y va DETRÁS del
 * prefijo de la API de Flex Cloud para CLOUD y PHONE ("/files"), o es la
 * ruta entera del Flex Web Server para ULTRA ("/api/library").
 */
static bool prep(gal_engine_t *e, int src, call_t *k, const char *method, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
static bool prep(gal_engine_t *e, int src, call_t *k, const char *method, const char *fmt, ...)
{
    memset(k, 0, sizeof(*k));
    k->e = e;
    k->src = src;
    const char *host, *prefix;
    uint16_t port;
    bool tls;
    gal__lock(e);
    bool ok = gal__cloud_base(e, src, &host, &port, &tls, &prefix);
    gal_copyz(k->host, sizeof(k->host), host);
    if (src == GAL_SRC_CLOUD) {
        ok = ok && gal_acct_bearer(&e->acct, k->bearer, sizeof(k->bearer)) && gal_acct_usable(&e->acct);
    } else if (e->cookie[0]) {
        snprintf(k->cookie, sizeof(k->cookie), "fxs=%s", e->cookie);
    }
    gal__unlock(e);
    if (!ok) {
        call_wipe(k);
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    const size_t pl = strlen(prefix);
    memcpy(k->path, prefix, pl);
    const int n = vsnprintf(k->path + pl, sizeof(k->path) - pl, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(k->path) - pl) {
        call_wipe(k);
        return false;
    }
    k->c = src == GAL_SRC_CLOUD ? &e->cloud : &e->lan;
    k->cloud_api = src != GAL_SRC_ULTRA;
    const bool get = !strcmp(method, "GET") || !strcmp(method, "HEAD");
    k->rq = (gal_req_t){
        .method = method, .host = k->host, .port = port, .tls = tls, .path = k->path, .agent = GAL_AGENT,
        .bearer = k->bearer[0] ? k->bearer : NULL, .cookie = k->cookie[0] ? k->cookie : NULL,
        .xflex = src != GAL_SRC_CLOUD && !get, /* CSRF del P4 (también en su pasarela) */
        .range_from = -1,
    };
    return true;
}

/* La sesión del P4 cambió (Set-Cookie): se apunta o se borra. */
static void take_cookie(call_t *k)
{
    if (k->src != GAL_SRC_CLOUD && k->rs.cookie_set) {
        gal__lock(k->e);
        gal_copyz(k->e->cookie, sizeof(k->e->cookie), k->rs.cookie);
        gal__unlock(k->e);
    }
}

/* Petición con respuesta JSON en e->json. Devuelve el estado HTTP o GAL_HTTP_E_*. */
static int run_json(call_t *k, size_t *n, char *code, size_t ccap)
{
    gal_engine_t *e = k->e;
    *n = 0;
    if (code && ccap) {
        code[0] = 0;
    }
    if (!e->json) {
        call_wipe(k);
        return GAL_HTTP_E_ARG;
    }
    const int st = gal_http_begin(k->c, &k->rq, &k->rs);
    call_wipe(k);
    e->json[0] = 0;
    if (st < 0) {
        return st;
    }
    take_cookie(k);
    const int r = gal_http_read_all(k->c, e->json, GAL_JSON_MAX, n);
    gal_http_end(k->c);
    if (r < 0) {
        return r;
    }
    if (code && k->cloud_api && (st < 200 || st >= 300)) {
        gal_parse_cloud_error(e->json, *n, code, ccap, NULL, 0);
    }
    return st;
}

/* {"error":"texto"} del Flex Web Server del P4. */
static void ultra_msg(const char *j, size_t n, char *out, size_t cap)
{
    const char *e = j + n, *r = gal_j_ws(j, e);
    out[0] = 0;
    if (r && r < e && *r == '{') {
        gal_j_get_str(r, e, "error", out, cap);
    }
}

static gal_err_t err_of(const call_t *k, int st, const char *code)
{
    return gal_err_from_http(st, k->cloud_api ? (code ? code : "") : NULL);
}

/* Una respuesta de "ya no tienes sesión": el P4 olvida la cookie; Flex Cloud revalida la cuenta. */
static void auth_lost(gal_engine_t *e, int src, uint32_t now)
{
    gal__lock(e);
    if (src == GAL_SRC_CLOUD) {
        gal_acct_report_rejected(&e->acct, now);
        gal_health_fail(&e->health[GAL_SRC_CLOUD], GAL_ST_CHECKING, "Comprobando tu Flex Account", now);
    } else {
        const bool had = e->cookie[0] != 0;
        memset(e->cookie, 0, sizeof(e->cookie));
        gal_health_fail(&e->health[GAL_SRC_ULTRA], GAL_ST_PAIR,
                        had ? "La sesión con Flex OS Ultra caducó: escribe su código otra vez"
                            : "Escribe el código de 6 cifras que enseña Flex OS Ultra",
                        now);
        memset(&e->health[GAL_SRC_PHONE], 0, sizeof(gal_health_t));
    }
    gal__unlock(e);
}

static void net_down(gal_engine_t *e, int src, int st, uint32_t now)
{
    char why[96];
    if (st == GAL_HTTP_E_CANCEL) {
        return; /* lo cortó el propio reloj (sin Wi-Fi, el visor...): no dice nada de la fuente */
    }
    if (src == GAL_SRC_ULTRA || src == GAL_SRC_PHONE) {
        if (st == GAL_HTTP_E_CONNECT || st == GAL_HTTP_E_TIMEOUT || st == GAL_HTTP_E_DNS) {
            snprintf(why, sizeof(why), "Flex OS Ultra no responde: ¿está encendido su Flex Web Server?");
        } else if (st < 0) {
            snprintf(why, sizeof(why), "Flex OS Ultra: %s", gal_http_err_text(st));
        } else {
            snprintf(why, sizeof(why), "Flex OS Ultra respondió HTTP %d", st);
        }
    } else if (st < 0) {
        snprintf(why, sizeof(why), "Flex Cloud: %s", gal_http_err_text(st));
    } else {
        snprintf(why, sizeof(why), "Flex Cloud respondió HTTP %d", st);
    }
    gal__lock(e);
    gal_health_fail(&e->health[src == GAL_SRC_PHONE ? GAL_SRC_ULTRA : src], GAL_ST_DOWN, why, now);
    if (src != GAL_SRC_CLOUD) {
        memset(&e->health[GAL_SRC_PHONE], 0, sizeof(gal_health_t));
    }
    gal__unlock(e);
}

/* ======================= salud ======================= */

static void phone_health(gal_engine_t *e, const gal_phone_t *ph, bool ok, uint32_t now)
{
    gal__lock(e);
    gal_health_t *h = &e->health[GAL_SRC_PHONE];
    e->phone = *ph;
    if (ok && !strcmp(ph->state, "ready")) {
        gal_health_ok(h, now, GAL_RECHECK_MS);
    } else {
        memset(h, 0, sizeof(*h));
        gal_copyz(h->detail, sizeof(h->detail),
              !ok                            ? "Este Flex OS Ultra no tiene Flex Storage"
              : !strcmp(ph->state, "off")      ? "El teléfono está desactivado en Flex Storage del P4"
              : !strcmp(ph->state, "rejected") ? "El teléfono ya no reconoce a Flex OS Ultra"
                                               : "Flex OS Ultra no tiene un teléfono emparejado");
    }
    gal__unlock(e);
}

static void health_ultra(gal_engine_t *e, uint32_t now)
{
    call_t k;
    size_t n;
    if (!prep(e, GAL_SRC_ULTRA, &k, "GET", "/api/hello")) {
        return;
    }
    const bool had = k.cookie[0] != 0;
    k.rq.timeout_ms = 6000;
    const int st = run_json(&k, &n, NULL, 0);
    if (st != 200) {
        net_down(e, GAL_SRC_ULTRA, st, now);
        return;
    }
    gal_hello_t h;
    if (!gal_parse_hello(e->json, n, &h) || !gal_hello_is_ultra(&h)) {
        gal__lock(e);
        gal_health_fail(&e->health[GAL_SRC_ULTRA], GAL_ST_DOWN, "En esa dirección no hay un Flex OS Ultra", now);
        gal__unlock(e);
        return;
    }
    gal__lock(e);
    e->hello = h;
    gal__unlock(e);
    if (!h.paired) {
        (void)had;
        auth_lost(e, GAL_SRC_ULTRA, now);
        return;
    }
    gal__lock(e);
    gal_health_ok(&e->health[GAL_SRC_ULTRA], now, GAL_RECHECK_MS);
    gal__unlock(e);
    /* el teléfono de Flex Storage (si este P4 lo tiene) */
    gal_phone_t ph;
    memset(&ph, 0, sizeof(ph));
    if (prep(e, GAL_SRC_ULTRA, &k, "GET", "/api/fs/overview")) {
        k.rq.timeout_ms = 6000;
        const int s2 = run_json(&k, &n, NULL, 0);
        phone_health(e, &ph, s2 == 200 && gal_parse_overview(e->json, n, &ph), now);
    }
}

static void health_cloud(gal_engine_t *e, uint32_t now)
{
    call_t k;
    size_t n;
    char code[40];
    /* el servicio (sin credencial) ... */
    if (!prep(e, GAL_SRC_CLOUD, &k, "GET", "/health")) {
        gal__lock(e);
        e->health[GAL_SRC_CLOUD].next_ms = now + GAL_RECHECK_MS;
        gal__unlock(e);
        return;
    }
    k.rq.bearer = NULL;
    int st = run_json(&k, &n, code, sizeof(code));
    if (st != 200) {
        net_down(e, GAL_SRC_CLOUD, st, now);
        return;
    }
    /* ... y la cuenta: /me valida la credencial y trae la cuota */
    if (!prep(e, GAL_SRC_CLOUD, &k, "GET", "/me")) {
        return;
    }
    st = run_json(&k, &n, code, sizeof(code));
    gal_me_t me;
    if (st == 200 && gal_parse_cloud_me(e->json, n, &me) && gal_acct_valid_address(me.address)) {
        gal__lock(e);
        e->me = me;
        e->me_valid = true;
        e->me_ms = now;
        if (e->acct.link != GAL_LINK_LINKED) {
            e->acct.link = GAL_LINK_LINKED;
            gal_copyz(e->acct.link_detail, sizeof(e->acct.link_detail), "Conectada con Flex Account");
            e->acct.verified = true;
            e->acct.last_ok = now;
            e->acct.want_validate = false;
            e->acct.fails = 0;
        }
        gal_health_ok(&e->health[GAL_SRC_CLOUD], now, GAL_RECHECK_MS);
        gal__unlock(e);
        return;
    }
    const gal_err_t er = err_of(&k, st, code);
    if (er == GAL_E_AUTH) {
        auth_lost(e, GAL_SRC_CLOUD, now);
    } else {
        net_down(e, GAL_SRC_CLOUD, st == 200 ? GAL_HTTP_E_PROTO : st, now);
    }
}

void gal__net_health(gal_engine_t *e, int src, uint32_t now)
{
    if (src == GAL_SRC_ULTRA) {
        health_ultra(e, now);
    } else if (src == GAL_SRC_CLOUD) {
        health_cloud(e, now);
    }
}

/* ======================= emparejar con el P4 ======================= */

void gal__net_pair(gal_engine_t *e)
{
    const uint32_t now = gal__now(e);
    char code[8], msg[96] = "";
    gal__lock(e);
    gal_copyz(code, sizeof(code), e->pair_code);
    wipe(e->pair_code, sizeof(e->pair_code));
    gal__unlock(e);
    call_t k;
    size_t n;
    bool ok = false;
    if (prep(e, GAL_SRC_ULTRA, &k, "GET", "/api/hello")) {
        k.rq.cookie = NULL;
        k.rq.timeout_ms = 6000;
        int st = run_json(&k, &n, NULL, 0);
        gal_hello_t h;
        if (st != 200) {
            snprintf(msg, sizeof(msg), "Flex OS Ultra no responde (%s)", st < 0 ? gal_http_err_text(st) : "HTTP");
        } else if (!gal_parse_hello(e->json, n, &h) || !gal_hello_is_ultra(&h)) {
            gal_copyz(msg, sizeof(msg), "En esa dirección no hay un Flex OS Ultra");
        } else if (prep(e, GAL_SRC_ULTRA, &k, "POST", "/api/pair")) {
            char body[16];
            snprintf(body, sizeof(body), "code=%s", code);
            k.rq.cookie = NULL; /* una sesión nueva */
            k.rq.ctype = "application/x-www-form-urlencoded";
            k.rq.body = (const uint8_t *)body;
            k.rq.body_len = strlen(body);
            st = run_json(&k, &n, NULL, 0);
            wipe(body, sizeof(body));
            const char *je = e->json + n, *r = gal_j_ws(e->json, je);
            int64_t left = -1, wait = 0;
            if (r && r < je && *r == '{') {
                gal_j_get_i64(r, je, "left", &left);
                gal_j_get_i64(r, je, "wait", &wait);
            }
            if (st == 200 && k.rs.cookie_set && strlen(k.rs.cookie) == 32) {
                ok = true;
                gal_copyz(msg, sizeof(msg), "Conectado con Flex OS Ultra");
            } else if (st == 403) {
                if (left > 0) {
                    snprintf(msg, sizeof(msg), "Código incorrecto. Quedan %d intento%s", (int)left, left == 1 ? "" : "s");
                } else {
                    gal_copyz(msg, sizeof(msg), "Código incorrecto");
                }
            } else if (st == 429) {
                snprintf(msg, sizeof(msg), "Demasiados intentos. Espera %d s y vuelve a probar", (int)(wait > 0 ? wait : 30));
            } else if (st < 0) {
                snprintf(msg, sizeof(msg), "Flex OS Ultra: %s", gal_http_err_text(st));
            } else {
                ultra_msg(e->json, n, msg, sizeof(msg));
                if (!msg[0]) {
                    snprintf(msg, sizeof(msg), "Flex OS Ultra respondió HTTP %d", st);
                }
            }
        }
    }
    wipe(code, sizeof(code));
    gal__lock(e);
    e->pairing = false;
    gal_copyz(e->pair_msg, sizeof(e->pair_msg), msg);
    if (ok) {
        e->health[GAL_SRC_ULTRA].state = GAL_ST_CHECKING;
        e->health[GAL_SRC_ULTRA].next_ms = now;
        e->health[GAL_SRC_ULTRA].fails = 0;
        e->list_req = true;
    }
    gal__unlock(e);
}

/* ======================= listas ======================= */

void gal__net_list(gal_engine_t *e, bool more)
{
    const uint32_t now = gal__now(e);
    gal__lock(e);
    const int src = e->choice.src;
    const uint32_t rev = more ? 0 : e->ultra_rev;
    char cursor[64];
    gal_copyz(cursor, sizeof(cursor), more ? e->cursor : "");
    if (!more && e->remote_state != GAL_L_READY) {
        e->remote_state = GAL_L_LOADING;
        e->remote_gen++;
    }
    gal__unlock(e);
    if (src < GAL_SRC_ULTRA || (more && !cursor[0])) {
        return;
    }
    call_t k;
    bool ok;
    if (src == GAL_SRC_ULTRA) {
        ok = rev ? prep(e, src, &k, "GET", "/api/library?since=%lu", (unsigned long)rev)
                 : prep(e, src, &k, "GET", "/api/library");
    } else {
        char enc[96] = "";
        gal_url_encode(cursor, enc, sizeof(enc));
        ok = prep(e, src, &k, "GET", "/files?view=recent&kind=media&limit=%d%s%s", GAL_PAGE, enc[0] ? "&cursor=" : "",
                  enc);
    }
    if (!ok) {
        return;
    }
    size_t n;
    char code[40];
    const int st = run_json(&k, &n, code, sizeof(code));
    gal_item_t *tmp = NULL;
    int got = -1;
    bool same = false;
    uint32_t nrev = 0;
    char next[64] = "";
    gal_err_t er = err_of(&k, st, code);
    if (er == GAL_E_OK) {
        tmp = gal__alloc(e, sizeof(gal_item_t) * GAL_LIST_MAX);
        if (!tmp) {
            er = GAL_E_NOMEM;
        } else if (src == GAL_SRC_ULTRA) {
            got = gal_parse_ultra_library(e->json, n, tmp, GAL_LIST_MAX, &nrev, &same);
        } else {
            got = gal_parse_cloud_list(e->json, n, (uint8_t)src, tmp, GAL_LIST_MAX, next, sizeof(next));
        }
        if (er == GAL_E_OK && got < 0) {
            er = GAL_E_PROTO;
        }
    }
    if (er == GAL_E_AUTH) {
        auth_lost(e, src, now);
    } else if (er == GAL_E_NET || er == GAL_E_TLS) {
        net_down(e, src, st, now);
    }
    gal__lock(e);
    if (e->remote_src != src || er == GAL_E_CANCEL) { /* otra fuente o cortada: este resultado no vale */
        if (er == GAL_E_CANCEL && e->remote_src == src) {
            e->list_req = true; /* se vuelve a pedir en cuanto se pueda */
        }
        gal__unlock(e);
        gal__free(e, tmp);
        return;
    }
    e->list_ms = now;
    if (er != GAL_E_OK) {
        e->remote_state = GAL_L_ERROR;
        snprintf(e->remote_err, sizeof(e->remote_err), "%s: %s", gal_src_name(src), gal_err_text(er));
        if (more) {
            e->remote_more = false;
        }
    } else if (!same) {
        int base = more ? e->nr : 0;
        if (base + got > GAL_LIST_MAX) {
            got = GAL_LIST_MAX - base;
        }
        memcpy(e->remote + base, tmp, sizeof(gal_item_t) * (size_t)got);
        e->nr = base + got;
        if (src == GAL_SRC_ULTRA) {
            e->ultra_rev = nrev;
            e->remote_more = false;
        } else {
            gal_copyz(e->cursor, sizeof(e->cursor), next);
            e->remote_more = next[0] && e->nr < GAL_LIST_MAX;
        }
        gal__mark_onwatch(e);
        e->remote_state = GAL_L_READY;
        e->remote_err[0] = 0;
        e->remote_gen++;
    } else if (e->remote_state != GAL_L_READY) {
        e->remote_state = GAL_L_READY;
        e->remote_err[0] = 0;
        e->remote_gen++;
    }
    gal__unlock(e);
    gal__free(e, tmp);
}

/* ======================= cuerpos por flujo ======================= */

typedef struct {
    gal_engine_t *e;
    gal_conn_t *c;
    uint64_t left;      /* tope de bytes que se aceptan */
    _Atomic uint32_t *done; /* progreso del visor (puede ser NULL; atómico: lo lee la interfaz) */
    bool failed;
} body_rd_t;

static int body_read(void *ud, uint8_t *buf, size_t n)
{
    body_rd_t *b = ud;
    if (!b->left) {
        b->failed = true; /* más grande de lo aceptado */
        return -1;
    }
    if (n > b->left) {
        n = (size_t)b->left;
    }
    const int r = gal_http_read(b->c, buf, n);
    if (r < 0) {
        b->failed = true;
        return -1;
    }
    b->left -= (uint64_t)r;
    if (b->done) {
        atomic_fetch_add_explicit(b->done, (uint32_t)r, memory_order_relaxed);
    }
    return r;
}

static bool should_abort(void *ud)
{
    return ((body_rd_t *)ud)->e->abort_job;
}

/* ======================= miniaturas ======================= */

static uint16_t *thumb_dst(void *ud, int sw, int sh, int *dw, int *dh)
{
    (void)sw;
    (void)sh;
    *dw = *dh = GAL_THUMB_SIDE;
    return ud;
}

bool gal__net_thumb(gal_engine_t *e, gal_thumb_t *t, uint16_t *px)
{
    const gal_item_t *it = &t->item;
    call_t k;
    const bool ok = it->src == GAL_SRC_ULTRA ? prep(e, it->src, &k, "GET", "/api/thumb/%s?v=%lu", it->id, (unsigned long)it->tver)
                                             : prep(e, it->src, &k, "GET", "/files/%s/thumbnail", it->id);
    if (!ok) {
        return false;
    }
    const int st = gal_http_begin(k.c, &k.rq, &k.rs);
    call_wipe(&k);
    if (st != 200) {
        if (st > 0) {
            gal_http_end(k.c);
        }
        if (st == 401) {
            auth_lost(e, it->src, gal__now(e));
        }
        return false;
    }
    if (k.rs.ctype[0] && strncmp(k.rs.ctype, "image/jpeg", 10) != 0) {
        gal_http_end(k.c); /* PNG o WebP de la web: el reloj solo decodifica JPEG */
        return false;
    }
    body_rd_t b = {.e = e, .c = k.c, .left = THUMB_BODY_MAX};
    gal_jpeg_src_t src = {.read = body_read, .ud = &b, .cancel = should_abort, .alloc = e->env.alloc, .free = e->env.free};
    const int jr = gal_jpeg_decode(&src, GAL_FIT_COVER, thumb_dst, px, 0, NULL);
    gal_http_end(k.c);
    return jr == GAL_JPG_OK && !b.failed;
}

/* ======================= visor ======================= */

typedef struct {
    body_rd_t b;
    call_t k;
} view_rd_t;

int gal__net_view_stream(gal_engine_t *e, const gal_item_t *it, gal_jpeg_src_t *src, void **ctx)
{
    *ctx = NULL;
    view_rd_t *v = gal__alloc(e, sizeof(*v));
    if (!v) {
        return GAL_E_NOMEM;
    }
    memset(v, 0, sizeof(*v));
    const bool ok = it->src == GAL_SRC_ULTRA ? prep(e, it->src, &v->k, "GET", "/api/file/%s", it->id)
                                             : prep(e, it->src, &v->k, "GET", "/download/%s", it->id);
    if (!ok) {
        gal__free(e, v);
        return it->src == GAL_SRC_CLOUD ? GAL_E_AUTH : GAL_E_ARG;
    }
    const int st = gal_http_begin(v->k.c, &v->k.rq, &v->k.rs);
    call_wipe(&v->k);
    if (st != 200) {
        char code[40] = "";
        if (st > 0) {
            size_t n = 0;
            gal_http_read_all(v->k.c, e->json, GAL_JSON_MAX, &n);
            if (v->k.cloud_api) {
                gal_parse_cloud_error(e->json, n, code, sizeof(code), NULL, 0);
            } else if (st == 403 && strstr(e->json, "\"lock\":1")) {
                gal_http_end(v->k.c);
                gal__free(e, v);
                return GAL_E_LOCKED;
            }
            gal_http_end(v->k.c);
        }
        const gal_err_t er = err_of(&v->k, st, code);
        if (er == GAL_E_AUTH) {
            auth_lost(e, it->src, gal__now(e));
        }
        gal__free(e, v);
        return er == GAL_E_OK ? GAL_E_PROTO : er;
    }
    gal__lock(e);
    e->view_total = v->k.rs.length > 0 ? (uint64_t)v->k.rs.length : it->size;
    gal__unlock(e);
    v->b = (body_rd_t){.e = e, .c = v->k.c, .left = UINT64_MAX, .done = &e->view_done};
    src->read = body_read;
    src->ud = &v->b;
    src->cancel = should_abort;
    src->alloc = e->env.alloc;
    src->free = e->env.free;
    *ctx = v;
    return 0;
}

void gal__net_view_end(gal_engine_t *e, void *ctx)
{
    view_rd_t *v = ctx;
    if (!v) {
        return;
    }
    gal_http_end(v->k.c);
    gal__free(e, v);
}

/* ======================= transferencias ======================= */

static void x_msg(gal_engine_t *e, gal_xfer_t *x, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void x_msg(gal_engine_t *e, gal_xfer_t *x, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    gal__lock(e);
    vsnprintf(x->msg, sizeof(x->msg), fmt, ap);
    e->xfer_gen++;
    gal__unlock(e);
    va_end(ap);
}

static void finish(gal_engine_t *e, gal_xfer_t *x, gal_err_t er, const char *ok_text)
{
    const uint32_t now = gal__now(e);
    gal__lock(e);
    if (er == GAL_E_OK) {
        x->state = GAL_XS_DONE;
        x->up_id[0] = 0;
        gal_copyz(x->msg, sizeof(x->msg), ok_text);
    } else if (x->cancel) {
        x->state = GAL_XS_CANCEL;
        gal_copyz(x->msg, sizeof(x->msg), "Cancelada");
    } else if (er == GAL_E_CANCEL) {
        x->state = GAL_XS_QUEUED; /* cedió el sitio al visor o se cerró la app: sigue después */
        gal_copyz(x->msg, sizeof(x->msg), "En pausa");
    } else if (gal_err_retry(er) && x->fails + 1 < GAL_XFER_TRIES) {
        x->fails++;
        x->state = GAL_XS_WAIT;
        const uint32_t w = gal_backoff_ms(x->fails);
        x->next_ms = now + w;
        snprintf(x->msg, sizeof(x->msg), "%s. Reintento en %lu s", gal_err_text(er), (unsigned long)(w / 1000u));
    } else {
        x->state = GAL_XS_FAIL;
        gal_copyz(x->msg, sizeof(x->msg), x->fail_why[0] ? x->fail_why : gal_err_text(er));
    }
    x->fail_why[0] = 0;
    e->xfer_gen++;
    gal__unlock(e);
}

/* El motivo concreto de un fallo, para la persona (lo enseña finish). */
static void fail_why(gal_engine_t *e, gal_xfer_t *x, const char *why)
{
    gal__lock(e);
    gal_copyz(x->fail_why, sizeof(x->fail_why), why);
    gal__unlock(e);
}

typedef struct {
    FILE *f;
    uint64_t left;
} file_rd_t;

static int file_read(void *ud, uint8_t *buf, size_t n)
{
    file_rd_t *r = ud;
    if (n > r->left) {
        n = (size_t)r->left;
    }
    const size_t k = n ? fread(buf, 1, n, r->f) : 0;
    r->left -= k;
    return k ? (int)k : -1;
}

static void down_done(gal_engine_t *e, gal_xfer_t *x, const char *final_vpath)
{
    gal_origin_t o;
    memset(&o, 0, sizeof(o));
    o.src = (int8_t)x->item.src;
    gal_copyz(o.id, sizeof(o.id), x->item.id);
    gal_copyz(o.name, sizeof(o.name), fl_base(final_vpath));
    o.size = x->total;
    gal__origin_add(e, &o);
    gal__lock(e);
    e->scan_req = true;
    gal__unlock(e);
}

static gal_err_t do_download(gal_engine_t *e, gal_xfer_t *x, char *saved, size_t saved_n)
{
    const gal_item_t *it = &x->item;
    const gal_local_io_t *io = e->env.local;
    char why[96];
    /* El destino lo eligió la persona: si no está listo, no se descarga (y
     * nunca se guarda en el otro volumen por detrás). */
    if (io && io->dest_ready && !io->dest_ready(e->env.ctx, why, sizeof(why))) {
        fail_why(e, x, why);
        return GAL_E_NOSPACE;
    }
    char tmpv[GAL_PATH_MAX], tmpr[GAL_PATH_MAX + 32];
    snprintf(tmpv, sizeof(tmpv), GAL_DIR_TMP "/%d-%s.part", it->src, it->id);
    gal__mkdirs(GAL_DIR_TMP);
    gal__mkdirs(GAL_DIR_PHOTO);
    if (!gal__real(tmpv, tmpr, sizeof(tmpr)) || !e->json) {
        return GAL_E_FS;
    }
    struct stat sb;
    uint64_t s0 = stat(tmpr, &sb) == 0 ? (uint64_t)sb.st_size : 0;
    if (it->size && s0 >= it->size) {
        remove(tmpr); /* de una versión anterior o completa sin terminar de verificar: se rehace */
        s0 = 0;
    }
    if (e->env.fs_free && it->size && e->env.fs_free(e->env.ctx) < it->size - s0 + GAL_RESERVE + 16384u) {
        return GAL_E_NOSPACE;
    }
    ota_hash_t *h = ota_hash_new(false);
    if (!h) {
        return GAL_E_NOMEM;
    }
    uint8_t *buf = (uint8_t *)e->json;
    if (s0) { /* lo que ya hay cuenta para la huella */
        FILE *pf = fopen(tmpr, "rb");
        uint64_t got = 0;
        size_t r;
        while (pf && (r = fread(buf, 1, IO_CHUNK, pf)) > 0) {
            ota_hash_update(h, buf, r);
            got += r;
        }
        if (pf) {
            fclose(pf);
        }
        if (got != s0) {
            remove(tmpr);
            ota_hash_free(h);
            return GAL_E_FS;
        }
    }
    call_t k;
    const bool ok = it->src == GAL_SRC_ULTRA ? prep(e, it->src, &k, "GET", "/api/file/%s?dl=1", it->id)
                                             : prep(e, it->src, &k, "GET", "/download/%s", it->id);
    if (!ok) {
        ota_hash_free(h);
        return GAL_E_AUTH;
    }
    if (s0) {
        k.rq.range_from = (int64_t)s0;
        k.rq.if_range = it->sha256[0] ? it->sha256 : NULL; /* nunca mezclar dos versiones */
    }
    const int st = gal_http_begin(k.c, &k.rq, &k.rs);
    call_wipe(&k);
    if (st != 200 && st != 206) {
        char code[40] = "";
        if (st > 0) {
            size_t n = 0;
            gal_http_read_all(k.c, e->json, GAL_JSON_MAX, &n);
            if (k.cloud_api) {
                gal_parse_cloud_error(e->json, n, code, sizeof(code), NULL, 0);
            }
            gal_http_end(k.c);
        }
        ota_hash_free(h);
        if (st == 416) {
            remove(tmpr);
            return GAL_E_INTEGRITY;
        }
        const gal_err_t er = err_of(&k, st, code);
        if (er == GAL_E_AUTH) {
            auth_lost(e, it->src, gal__now(e));
        }
        return er == GAL_E_OK ? GAL_E_PROTO : er;
    }
    uint64_t total;
    if (st == 206 && k.rs.has_range && k.rs.range_start == s0) {
        total = k.rs.range_total;
    } else {
        if (s0) { /* el servidor no reanudó (o cambió el archivo): desde cero */
            ota_hash_free(h);
            h = ota_hash_new(false);
            s0 = 0;
            if (!h) {
                gal_http_end(k.c);
                return GAL_E_NOMEM;
            }
        }
        total = k.rs.length >= 0 ? (uint64_t)k.rs.length : it->size;
    }
    if (!total || (it->size && total != it->size)) {
        gal_http_end(k.c);
        ota_hash_free(h);
        remove(tmpr);
        return GAL_E_INTEGRITY; /* no es lo que dice la lista: se vuelve a pedir */
    }
    FILE *f = fopen(tmpr, s0 ? "ab" : "wb");
    if (!f) {
        gal_http_end(k.c);
        ota_hash_free(h);
        return GAL_E_FS;
    }
    gal__lock(e);
    x->total = total;
    x->done = s0;
    gal__unlock(e);
    gal_err_t er = GAL_E_OK;
    uint64_t have = s0;
    for (;;) {
        if (e->abort_job || x->cancel) {
            er = GAL_E_CANCEL;
            break;
        }
        const int r = gal_http_read(k.c, buf, IO_CHUNK);
        if (r == 0) {
            break;
        }
        if (r < 0) {
            er = r == GAL_HTTP_E_CANCEL ? GAL_E_CANCEL : GAL_E_NET;
            break;
        }
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            er = e->env.fs_free && e->env.fs_free(e->env.ctx) < 65536u ? GAL_E_NOSPACE : GAL_E_FS;
            break;
        }
        ota_hash_update(h, buf, (size_t)r);
        have += (uint64_t)r;
        gal__lock(e); /* 64 bits: con el cerrojo, la interfaz nunca lee una mitad */
        x->done = have;
        gal__unlock(e);
    }
    if (fclose(f) != 0 && er == GAL_E_OK) {
        er = GAL_E_FS;
    }
    gal_http_end(k.c);
    uint8_t dg[32];
    ota_hash_finish(h, dg, NULL);
    if (er != GAL_E_OK) {
        return er; /* el .part se queda para reanudar */
    }
    if (have != total) {
        remove(tmpr);
        return GAL_E_INTEGRITY;
    }
    char hex[65];
    gal_hex(dg, sizeof(dg), hex);
    if (it->sha256[0] && strcmp(hex, it->sha256)) {
        remove(tmpr);
        return GAL_E_INTEGRITY; /* llegó otra cosa: se repite desde cero */
    }
    if (it->fmt == GAL_F_JPEG) { /* sin huella del P4: al menos que sea un JPEG */
        FILE *cf = fopen(tmpr, "rb");
        uint8_t sig[2] = {0, 0};
        const bool jpg = cf && fread(sig, 1, 2, cf) == 2 && sig[0] == 0xFF && sig[1] == 0xD8;
        if (cf) {
            fclose(cf);
        }
        if (!jpg) {
            remove(tmpr);
            return GAL_E_INTEGRITY;
        }
    }
    char name[GAL_NAME_MAX], fv[GAL_PATH_MAX], fr[GAL_PATH_MAX + 32];
    gal_safe_name(it->name, name, sizeof(name));
    if (io && io->import) {
        /* a la biblioteca, en el destino elegido (reloj o microSD) */
        why[0] = 0;
        if (!io->import(e->env.ctx, tmpr, name, it->kind, it->src, fv, sizeof(fv), why, sizeof(why))) {
            remove(tmpr); /* lo bajado no se queda ocupando el LittleFS */
            if (why[0]) {
                fail_why(e, x, why);
            }
            return GAL_E_FS;
        }
        gal_copyz(saved, saved_n, why[0] ? why : "Guardada");
    } else if (!gal__unique_photo(name, fv, sizeof(fv)) || !gal__real(fv, fr, sizeof(fr)) || rename(tmpr, fr) != 0) {
        return GAL_E_FS;
    }
    down_done(e, x, fv);
    return GAL_E_OK;
}

/* Huellas SHA-256 del archivo entero y de cada parte en UNA pasada (como el P4). */
static gal_err_t hash_parts(gal_engine_t *e, gal_xfer_t *x, FILE *f, uint64_t size, uint64_t chunk, uint32_t parts,
                            uint8_t whole[32])
{
    uint8_t *buf = (uint8_t *)e->json;
    ota_hash_t *all = ota_hash_new(false), *part = ota_hash_new(false);
    if (!all || !part) {
        ota_hash_free(all);
        ota_hash_free(part);
        return GAL_E_NOMEM;
    }
    uint64_t off = 0, in_part = 0;
    uint32_t p = 0;
    while (off < size) {
        if (e->abort_job || x->cancel) {
            ota_hash_free(all);
            ota_hash_free(part);
            return GAL_E_CANCEL;
        }
        uint64_t want = size - off < IO_CHUNK ? size - off : IO_CHUNK;
        if (want > chunk - in_part) {
            want = chunk - in_part;
        }
        const size_t r = fread(buf, 1, (size_t)want, f);
        if (r != want) {
            ota_hash_free(all);
            ota_hash_free(part);
            return GAL_E_FS;
        }
        ota_hash_update(all, buf, r);
        ota_hash_update(part, buf, r);
        off += r;
        in_part += r;
        if (in_part == chunk || off == size) {
            ota_hash_finish(part, e->parts + (size_t)p * 32u, NULL);
            p++;
            in_part = 0;
            part = off < size ? ota_hash_new(false) : NULL;
            if (off < size && !part) {
                ota_hash_free(all);
                return GAL_E_NOMEM;
            }
        }
    }
    ota_hash_finish(all, whole, NULL);
    return p == parts ? GAL_E_OK : GAL_E_FS;
}

static void cloud_cancel_upload(gal_engine_t *e, gal_xfer_t *x)
{
    call_t k;
    size_t n;
    if (x->up_id[0] && prep(e, x->dest, &k, "DELETE", "/uploads/%s", x->up_id)) {
        run_json(&k, &n, NULL, 0); /* suelta la reserva de cuota; si no contesta, caduca sola (72 h) */
    }
    x->up_id[0] = 0;
}

static gal_err_t do_upload_cloud(gal_engine_t *e, gal_xfer_t *x, const char *real)
{
    const gal_item_t *it = &x->item;
    struct stat sb;
    if (stat(real, &sb) != 0 || sb.st_size <= 0 || !e->json) {
        return GAL_E_FS;
    }
    const uint64_t size = (uint64_t)sb.st_size;
    gal__lock(e);
    const uint64_t lo = x->dest == GAL_SRC_CLOUD && e->me_valid && e->me.chunk_min ? e->me.chunk_min : 64u * 1024u;
    const uint64_t hi = x->dest == GAL_SRC_CLOUD && e->me_valid && e->me.chunk_max ? e->me.chunk_max
                        : x->dest == GAL_SRC_PHONE                                 ? 16u * 1024u * 1024u
                                                                                   : 64u * 1024u * 1024u;
    gal__unlock(e);
    const uint64_t chunk = gal_chunk_for(size, lo, hi);
    if (!chunk) {
        return GAL_E_TOOBIG;
    }
    const uint32_t parts = (uint32_t)(size / chunk + (size % chunk ? 1 : 0));
    const size_t pb = (size_t)parts * 32u;
    if (e->parts_bytes < pb) {
        gal__free(e, e->parts);
        e->parts = gal__alloc(e, pb);
        e->parts_bytes = e->parts ? pb : 0;
        if (!e->parts) {
            return GAL_E_NOMEM;
        }
    }
    FILE *f = fopen(real, "rb");
    if (!f) {
        return GAL_E_FS;
    }
    x_msg(e, x, "Calculando la huella");
    uint8_t whole[32];
    gal_err_t er = hash_parts(e, x, f, size, chunk, parts, whole);
    char sha[65];
    gal_hex(whole, sizeof(whole), sha);
    /* la sesión: misma clave de cliente => la nube reanuda la que ya tenía (cliente "s3w:<sha>:<tamaño>") */
    char qname[GAL_NAME_MAX * 2 + 4], body[512];
    gal_upload_t up;
    call_t k;
    size_t n;
    char code[40];
    if (er == GAL_E_OK) {
        if (!gal_j_quote(it->name, qname, sizeof(qname))) {
            er = GAL_E_ARG;
        }
    }
    if (er == GAL_E_OK) {
        snprintf(body, sizeof(body),
                 "{\"name\":%s,\"size\":%llu,\"mimeType\":\"image/jpeg\",\"sha256\":\"%s\",\"chunkSize\":%llu,"
                 "\"clientKey\":\"s3w:%s:%llu\",\"conflict\":\"rename\"}",
                 qname, (unsigned long long)size, sha, (unsigned long long)chunk, sha, (unsigned long long)size);
        if (!prep(e, x->dest, &k, "POST", "/uploads")) {
            er = GAL_E_AUTH;
        } else {
            k.rq.ctype = "application/json";
            k.rq.body = (const uint8_t *)body;
            k.rq.body_len = strlen(body);
            const int st = run_json(&k, &n, code, sizeof(code));
            er = err_of(&k, st, code);
            if (er == GAL_E_OK && (!gal_parse_cloud_upload(e->json, n, &up) || up.chunk != chunk || up.parts != parts)) {
                er = GAL_E_PROTO;
            }
        }
    }
    if (er == GAL_E_OK) {
        gal__lock(e);
        gal_copyz(x->up_id, sizeof(x->up_id), up.id);
        x->total = size;
        x->done = (uint64_t)up.got_n * chunk > size ? size : (uint64_t)up.got_n * chunk;
        gal__unlock(e);
        x_msg(e, x, up.resumed ? "Reanudando la subida" : "Subiendo");
    }
    for (int round = 0; er == GAL_E_OK && round < 2; round++) {
        for (uint32_t p = 1; er == GAL_E_OK && p <= parts; p++) {
            if (gal_upload_has(&up, p)) {
                continue;
            }
            if (e->abort_job || x->cancel) {
                er = GAL_E_CANCEL;
                break;
            }
            const uint64_t off = (uint64_t)(p - 1) * chunk, len = size - off < chunk ? size - off : chunk;
            char hex[65];
            gal_hex(e->parts + (size_t)(p - 1) * 32u, 32, hex);
            if (fseek(f, (long)off, SEEK_SET) != 0 || !prep(e, x->dest, &k, "PUT", "/uploads/%s/parts/%lu", up.id, (unsigned long)p)) {
                er = GAL_E_FS;
                break;
            }
            snprintf(k.extra, sizeof(k.extra), "X-Part-SHA256: %s\r\n", hex);
            file_rd_t fr = {.f = f, .left = len};
            k.rq.extra = k.extra;
            k.rq.ctype = "application/octet-stream";
            k.rq.body_len = len;
            k.rq.body_cb = file_read;
            k.rq.body_ud = &fr;
            k.rq.timeout_ms = 30000;
            const int st = run_json(&k, &n, code, sizeof(code));
            er = err_of(&k, st, code);
            if (er == GAL_E_OK) {
                up.got[(p - 1) / 8] |= (uint8_t)(1u << ((p - 1) % 8));
                gal__lock(e);
                x->done = x->done + len > size ? size : x->done + len;
                gal__unlock(e);
            } else if (er == GAL_E_EXPIRED || er == GAL_E_NOTFOUND) {
                x->up_id[0] = 0; /* la sesión ya no existe: la próxima vez, una nueva */
                er = GAL_E_EXPIRED;
            }
        }
        if (er != GAL_E_OK) {
            break;
        }
        snprintf(body, sizeof(body), "{\"sha256\":\"%s\"}", sha);
        if (!prep(e, x->dest, &k, "POST", "/uploads/%s/complete", up.id)) {
            er = GAL_E_AUTH;
            break;
        }
        k.rq.ctype = "application/json";
        k.rq.body = (const uint8_t *)body;
        k.rq.body_len = strlen(body);
        k.rq.timeout_ms = 60000; /* el servidor une las partes y comprueba la huella entera */
        const int st = run_json(&k, &n, code, sizeof(code));
        if (st == 409 && !strcmp(code, "incomplete_upload") && round == 0) {
            /* faltan partes que creíamos enviadas: se pregunta cuáles y se mandan */
            if (!prep(e, x->dest, &k, "GET", "/uploads/%s", up.id)) {
                er = GAL_E_AUTH;
                break;
            }
            const int s2 = run_json(&k, &n, code, sizeof(code));
            er = err_of(&k, s2, code);
            if (er == GAL_E_OK && !gal_parse_cloud_upload(e->json, n, &up)) {
                er = GAL_E_PROTO;
            }
            continue;
        }
        er = err_of(&k, st, code);
        char fid[GAL_ID_MAX] = "", fsha[GAL_SHA_HEX] = "";
        if (er == GAL_E_OK && (!gal_parse_cloud_complete(e->json, n, fid, sizeof(fid), fsha, sizeof(fsha)) || strcmp(fsha, sha))) {
            er = GAL_E_INTEGRITY;
        }
        if (er == GAL_E_OK) {
            gal_origin_t o;
            memset(&o, 0, sizeof(o));
            o.src = x->dest;
            gal_copyz(o.id, sizeof(o.id), fid);
            gal_copyz(o.name, sizeof(o.name), fl_base(it->path));
            o.size = size;
            gal__origin_add(e, &o);
            gal__lock(e);
            x->done = size;
            if (e->remote_src == x->dest) {
                e->list_req = true;
            }
            e->scan_req = true;
            gal__unlock(e);
        }
        break;
    }
    fclose(f);
    if (x->cancel) {
        cloud_cancel_upload(e, x);
        return GAL_E_CANCEL;
    }
    if (er == GAL_E_AUTH) {
        auth_lost(e, x->dest, gal__now(e));
    }
    return er;
}

static gal_err_t do_upload_ultra(gal_engine_t *e, gal_xfer_t *x, const char *real)
{
    const gal_item_t *it = &x->item;
    struct stat sb;
    if (stat(real, &sb) != 0 || sb.st_size <= 0 || !e->json) {
        return GAL_E_FS;
    }
    const uint64_t size = (uint64_t)sb.st_size;
    FILE *f = fopen(real, "rb");
    if (!f) {
        return GAL_E_FS;
    }
    /* CRC-32 de extremo a extremo: el P4 lo comprueba antes de publicar (handleUploadCore) */
    x_msg(e, x, "Calculando la suma de comprobación");
    uint32_t crc = 0;
    uint8_t *buf = (uint8_t *)e->json;
    size_t r;
    uint64_t got = 0;
    while ((r = fread(buf, 1, IO_CHUNK, f)) > 0) {
        crc = gal_crc32(crc, buf, r);
        got += r;
        if (e->abort_job || x->cancel) {
            fclose(f);
            return GAL_E_CANCEL;
        }
    }
    if (got != size || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return GAL_E_FS;
    }
    char enc[GAL_NAME_MAX * 3 + 4];
    gal_url_encode(fl_base(it->path), enc, sizeof(enc));
    call_t k;
    if (!prep(e, GAL_SRC_ULTRA, &k, "POST", "/api/upload?kind=photo&name=%s&size=%llu&crc=%lu&w=%u&h=%u", enc,
              (unsigned long long)size, (unsigned long)crc, it->w, it->h)) {
        fclose(f);
        return GAL_E_AUTH;
    }
    file_rd_t fr = {.f = f, .left = size};
    k.rq.ctype = "application/octet-stream";
    k.rq.body_len = size;
    k.rq.body_cb = file_read;
    k.rq.body_ud = &fr;
    k.rq.timeout_ms = 30000; /* el P4 decodifica la foto entera para validarla */
    gal__lock(e);
    x->total = size;
    x->done = 0;
    gal__unlock(e);
    x_msg(e, x, "Enviando a Flex OS Ultra");
    size_t n;
    const int st = run_json(&k, &n, NULL, 0);
    fclose(f);
    gal_err_t er = err_of(&k, st, NULL);
    const char *je = e->json + n, *rr = gal_j_ws(e->json, je);
    uint64_t id = 0;
    bool dup = false;
    if (er == GAL_E_OK && (!rr || rr >= je || *rr != '{' || !gal_j_get_u64(rr, je, "id", &id) || !id)) {
        er = GAL_E_PROTO;
    }
    if (er == GAL_E_OK) {
        gal_j_get_bool(rr, je, "dup", &dup);
        gal_origin_t o;
        memset(&o, 0, sizeof(o));
        o.src = GAL_SRC_ULTRA;
        snprintf(o.id, sizeof(o.id), "%llu", (unsigned long long)id);
        gal_copyz(o.name, sizeof(o.name), fl_base(it->path));
        o.size = size;
        gal__origin_add(e, &o);
        gal__lock(e);
        x->done = size;
        if (e->remote_src == GAL_SRC_ULTRA) {
            e->list_req = true;
        }
        e->scan_req = true;
        gal__unlock(e);
        x_msg(e, x, dup ? "Ya estaba en Flex OS Ultra" : "Enviada a Flex OS Ultra");
    } else if (st > 0) {
        char m[96];
        ultra_msg(e->json, n, m, sizeof(m));
        if (m[0]) {
            x_msg(e, x, "%s", m);
        }
    }
    if (er == GAL_E_AUTH) {
        auth_lost(e, GAL_SRC_ULTRA, gal__now(e));
    }
    return er;
}

static gal_err_t do_trash_remote(gal_engine_t *e, gal_xfer_t *x)
{
    const gal_item_t *it = &x->item;
    call_t k;
    size_t n;
    char code[40];
    int st;
    if (it->src == GAL_SRC_ULTRA) {
        if (!prep(e, GAL_SRC_ULTRA, &k, "POST", "/api/local/delete")) {
            return GAL_E_AUTH;
        }
        char body[48];
        snprintf(body, sizeof(body), "ids=%s", it->id);
        k.rq.ctype = "application/x-www-form-urlencoded";
        k.rq.body = (const uint8_t *)body;
        k.rq.body_len = strlen(body);
        st = run_json(&k, &n, NULL, 0);
        if (st == 403 && strstr(e->json, "\"lock\":1")) {
            return GAL_E_LOCKED;
        }
        gal_err_t er = err_of(&k, st, NULL);
        const char *je = e->json + n, *r = gal_j_ws(e->json, je);
        uint64_t ok = 0;
        if (er == GAL_E_OK && (!r || r >= je || *r != '{' || !gal_j_get_u64(r, je, "ok", &ok) || ok < 1)) {
            er = GAL_E_SERVER;
        }
        if (er == GAL_E_AUTH) {
            auth_lost(e, GAL_SRC_ULTRA, gal__now(e));
        }
        return er;
    }
    if (!prep(e, it->src, &k, "DELETE", "/files/%s", it->id)) {
        return GAL_E_AUTH;
    }
    st = run_json(&k, &n, code, sizeof(code));
    const gal_err_t er = err_of(&k, st, code);
    if (er == GAL_E_AUTH) {
        auth_lost(e, it->src, gal__now(e));
    }
    return er;
}

void gal__net_xfer(gal_engine_t *e, gal_xfer_t *x)
{
    gal_err_t er;
    const char *ok_text = "Hecho";
    char saved[96] = "Guardada en el reloj";
    if (x->cancel && x->up_id[0]) {
        cloud_cancel_upload(e, x);
        finish(e, x, GAL_E_CANCEL, "");
        return;
    }
    switch (x->op) {
    case GAL_X_DOWNLOAD:
        x_msg(e, x, "Descargando de %s", gal_src_name(x->item.src));
        er = do_download(e, x, saved, sizeof(saved));
        ok_text = saved;
        break;
    case GAL_X_UPLOAD: {
        /* El archivo, mientras dura la subida: con la microSD, arrendado (no se desmonta). */
        char real[GAL_PATH_MAX + 32];
        if (!gal__local_acquire(e, &x->item, real, sizeof(real))) {
            er = GAL_E_NOTFOUND;
        } else {
            er = x->dest == GAL_SRC_ULTRA ? do_upload_ultra(e, x, real) : do_upload_cloud(e, x, real);
            gal__local_release(e, &x->item);
        }
        ok_text = x->dest == GAL_SRC_ULTRA ? "Enviada a Flex OS Ultra"
                  : x->dest == GAL_SRC_PHONE ? "Subida al Flex Cloud del teléfono"
                                             : "Subida a Flex Cloud";
        if (er == GAL_E_OK && x->dest == GAL_SRC_ULTRA) {
            gal__lock(e);
            char t[96];
            gal_copyz(t, sizeof(t), x->msg);
            gal__unlock(e);
            finish(e, x, er, t);
            return;
        }
        break;
    }
    case GAL_X_TRASH:
        er = do_trash_remote(e, x);
        ok_text = x->item.src == GAL_SRC_ULTRA   ? "En la papelera de Flex OS Ultra"
                  : x->item.src == GAL_SRC_PHONE ? "En la papelera del Flex Cloud del teléfono"
                                                 : "En la papelera de Flex Cloud";
        if (er == GAL_E_OK) {
            gal__lock(e);
            for (int i = 0; i < e->nr; i++) { /* fuera de la lista ya; la recarga lo confirma */
                if (e->remote[i].src == x->item.src && !strcmp(e->remote[i].id, x->item.id)) {
                    memmove(&e->remote[i], &e->remote[i + 1], sizeof(gal_item_t) * (size_t)(e->nr - i - 1));
                    e->nr--;
                    e->remote_gen++;
                    break;
                }
            }
            e->list_req = true;
            gal__unlock(e);
        }
        break;
    default: er = GAL_E_ARG; break;
    }
    finish(e, x, er, ok_text);
}
