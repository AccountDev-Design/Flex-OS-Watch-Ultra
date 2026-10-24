/*
 * gal_http — ver include/gal_http.h.
 */
#include "gal_http.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define SLICE_MS      250   /* las esperas se hacen a trozos para atender la cancelación */
#define LINE_MAX      1024  /* línea de tamaño de un trozo (chunked) o de remolque */
#define DRAIN_MAX     16384 /* cuerpo sin leer que se descarta para conservar la conexión */
#define E_EMPTY       (-100) /* interno: cerrada sin un solo byte de respuesta */

static uint32_t now_ms(const gal_conn_t *c)
{
    return c->io->now ? c->io->now(c->io->ctx) : 0;
}

static bool cancelled(const gal_conn_t *c)
{
    return c->io->cancelled && c->io->cancelled(c->io->ctx);
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

void gal_conn_init(gal_conn_t *c, const gal_io_t *io)
{
    memset(c, 0, sizeof(*c));
    c->io = io;
}

void gal_conn_close(gal_conn_t *c)
{
    if (c->open) {
        c->io->close(c->io->ctx);
    }
    c->open = false;
    c->in_body = false;
    c->bn = c->bp = 0;
}

static int write_all(gal_conn_t *c, const uint8_t *p, size_t n)
{
    uint32_t t0 = now_ms(c);
    while (n) {
        if (cancelled(c)) {
            return GAL_HTTP_E_CANCEL;
        }
        const size_t step = n > 4096 ? 4096 : n;
        const int r = c->io->write(c->io->ctx, p, step, SLICE_MS);
        if (r < 0) {
            return GAL_HTTP_E_CLOSED;
        }
        if (r == 0) {
            if (now_ms(c) - t0 >= c->to_ms) {
                return GAL_HTTP_E_TIMEOUT;
            }
            continue;
        }
        p += r;
        n -= (size_t)r;
        t0 = now_ms(c);
    }
    return 0;
}

/* Lee del transporte con plazo y cancelación: >0 bytes, 0 cerrada, <0 error. */
static int io_read(gal_conn_t *c, uint8_t *b, size_t n)
{
    const uint32_t t0 = now_ms(c);
    for (;;) {
        if (cancelled(c)) {
            return GAL_HTTP_E_CANCEL;
        }
        const int r = c->io->read(c->io->ctx, b, n, SLICE_MS);
        if (r > 0) {
            return r;
        }
        if (r < 0) {
            return 0;
        }
        if (now_ms(c) - t0 >= c->to_ms) {
            return GAL_HTTP_E_TIMEOUT;
        }
    }
}

/* Más bytes al búfer (compactando). >0 leídos, 0 cerrada, <0 error. */
static int fill(gal_conn_t *c)
{
    if (c->bp == c->bn) {
        c->bp = c->bn = 0;
    } else if (c->bp > 0 && c->bn == sizeof(c->buf)) {
        memmove(c->buf, c->buf + c->bp, c->bn - c->bp);
        c->bn -= c->bp;
        c->bp = 0;
    }
    if (c->bn >= sizeof(c->buf)) {
        return GAL_HTTP_E_TOOBIG;
    }
    const int r = io_read(c, c->buf + c->bn, sizeof(c->buf) - c->bn);
    if (r > 0) {
        c->bn += (size_t)r;
    }
    return r;
}

static bool safe_value(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            return false; /* nada de saltos de línea: no se inyectan cabeceras */
        }
    }
    return true;
}

static bool safe_path(const char *s)
{
    if (!s || s[0] != '/') {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7F) {
            return false;
        }
    }
    return true;
}

static bool put(gal_conn_t *c, size_t *w, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static bool put(gal_conn_t *c, size_t *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf((char *)c->buf + *w, sizeof(c->buf) - *w, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(c->buf) - *w) {
        return false;
    }
    *w += (size_t)n;
    return true;
}

static int send_request(gal_conn_t *c, const gal_req_t *rq)
{
    size_t w = 0;
    const bool def_port = rq->port == (rq->tls ? 443 : 80);
    const bool m_body = !strcmp(rq->method, "POST") || !strcmp(rq->method, "PUT") || !strcmp(rq->method, "PATCH");
    bool ok = put(c, &w, "%s %s HTTP/1.1\r\n", rq->method, rq->path);
    ok = ok && (def_port ? put(c, &w, "Host: %s\r\n", rq->host) : put(c, &w, "Host: %s:%u\r\n", rq->host, rq->port));
    ok = ok && put(c, &w, "Accept-Encoding: identity\r\n");
    if (rq->agent) {
        ok = ok && put(c, &w, "User-Agent: %s\r\n", rq->agent);
    }
    if (rq->bearer) {
        ok = ok && put(c, &w, "Authorization: Bearer %s\r\n", rq->bearer);
    }
    if (rq->cookie) {
        ok = ok && put(c, &w, "Cookie: %s\r\n", rq->cookie);
    }
    if (rq->xflex) {
        ok = ok && put(c, &w, "X-Flex: 1\r\n");
    }
    if (rq->ctype) {
        ok = ok && put(c, &w, "Content-Type: %s\r\n", rq->ctype);
    }
    if (rq->body_len || m_body) {
        ok = ok && put(c, &w, "Content-Length: %llu\r\n", (unsigned long long)rq->body_len);
    }
    if (rq->range_from >= 0) {
        ok = ok && put(c, &w, "Range: bytes=%lld-\r\n", (long long)rq->range_from);
    }
    if (rq->if_range && rq->if_range[0]) {
        ok = ok && put(c, &w, "If-Range: \"%s\"\r\n", rq->if_range);
    }
    if (rq->extra) {
        ok = ok && put(c, &w, "%s", rq->extra);
    }
    ok = ok && put(c, &w, "\r\n");
    if (!ok) {
        wipe(c->buf, sizeof(c->buf));
        return GAL_HTTP_E_ARG;
    }
    int r = write_all(c, c->buf, w);
    wipe(c->buf, w); /* la credencial no se queda en el búfer */
    if (r < 0) {
        return r;
    }
    if (rq->body_len && rq->body) {
        r = write_all(c, rq->body, (size_t)rq->body_len);
    } else if (rq->body_len && rq->body_cb) {
        uint64_t left = rq->body_len;
        while (left && r == 0) {
            const size_t want = left < sizeof(c->buf) ? (size_t)left : sizeof(c->buf);
            const int got = rq->body_cb(rq->body_ud, c->buf, want);
            if (got <= 0 || (size_t)got > want) {
                return GAL_HTTP_E_BODY;
            }
            r = write_all(c, c->buf, (size_t)got);
            left -= (uint64_t)got;
        }
    } else if (rq->body_len) {
        return GAL_HTTP_E_ARG;
    }
    return r;
}

/* ¿La cabecera name (sin distinguir mayúsculas) es la de la línea [l, le)? Devuelve el valor recortado. */
static bool hdr_is(const char *l, const char *le, const char *name, const char **vs, const char **ve)
{
    const size_t nl = strlen(name);
    if ((size_t)(le - l) <= nl || strncasecmp(l, name, nl) != 0 || l[nl] != ':') {
        return false;
    }
    const char *v = l + nl + 1;
    while (v < le && (*v == ' ' || *v == '\t')) {
        v++;
    }
    const char *e = le;
    while (e > v && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
    }
    *vs = v;
    *ve = e;
    return true;
}

static bool parse_u64(const char *s, const char *e, uint64_t *out)
{
    uint64_t v = 0;
    if (s >= e) {
        return false;
    }
    for (; s < e; s++) {
        if (*s < '0' || *s > '9' || v > (UINT64_MAX - 9) / 10) {
            return false;
        }
        v = v * 10 + (uint64_t)(*s - '0');
    }
    *out = v;
    return true;
}

static void copy_span(char *out, size_t n, const char *s, const char *e)
{
    size_t k = (size_t)(e - s);
    if (k >= n) {
        k = n - 1;
    }
    memcpy(out, s, k);
    out[k] = 0;
}

static bool contains_ci(const char *s, const char *e, const char *tok)
{
    const size_t tl = strlen(tok);
    for (; s + tl <= e; s++) {
        if (strncasecmp(s, tok, tl) == 0) {
            return true;
        }
    }
    return false;
}

static int parse_head(gal_conn_t *c, size_t hlen, gal_resp_t *rs, bool *http11)
{
    const char *b = (const char *)c->buf, *end = b + hlen;
    /* "HTTP/1.x NNN texto" */
    if (hlen < 14 || strncmp(b, "HTTP/1.", 7) != 0 || (b[7] != '0' && b[7] != '1') || b[8] != ' ' || b[9] < '1' ||
        b[9] > '5' || b[10] < '0' || b[10] > '9' || b[11] < '0' || b[11] > '9' || (b[12] != ' ' && b[12] != '\r')) {
        return GAL_HTTP_E_PROTO;
    }
    *http11 = b[7] == '1';
    rs->status = (b[9] - '0') * 100 + (b[10] - '0') * 10 + (b[11] - '0');
    const char *l = memchr(b, '\n', hlen);
    bool have_len = false, conn_close = false, conn_keep = false;
    while (l && ++l < end) {
        const char *le = memchr(l, '\n', (size_t)(end - l));
        if (!le) {
            break;
        }
        const char *lr = le > l && le[-1] == '\r' ? le - 1 : le;
        if (lr == l) {
            break; /* línea vacía: fin de las cabeceras */
        }
        const char *vs, *ve;
        uint64_t u;
        if (hdr_is(l, lr, "Content-Length", &vs, &ve)) {
            if (!parse_u64(vs, ve, &u) || u > (uint64_t)INT64_MAX || (have_len && (int64_t)u != rs->length)) {
                return GAL_HTTP_E_PROTO; /* longitudes distintas: no se adivina cuál vale */
            }
            rs->length = (int64_t)u;
            have_len = true;
        } else if (hdr_is(l, lr, "Transfer-Encoding", &vs, &ve)) {
            if (contains_ci(vs, ve, "chunked")) {
                rs->chunked = true;
            } else if (ve > vs && !contains_ci(vs, ve, "identity")) {
                return GAL_HTTP_E_PROTO; /* gzip u otra: no se pidió (Accept-Encoding: identity) */
            }
        } else if (hdr_is(l, lr, "Connection", &vs, &ve)) {
            conn_close = conn_close || contains_ci(vs, ve, "close");
            conn_keep = conn_keep || contains_ci(vs, ve, "keep-alive");
        } else if (hdr_is(l, lr, "Content-Range", &vs, &ve)) {
            /* "bytes a-b/total" o "bytes star/total" */
            if ((size_t)(ve - vs) > 6 && strncasecmp(vs, "bytes ", 6) == 0) {
                const char *p = vs + 6, *dash = memchr(p, '-', (size_t)(ve - p)), *sl = memchr(p, '/', (size_t)(ve - p));
                uint64_t a, z, t;
                if (sl && parse_u64(sl + 1, ve, &t)) {
                    rs->range_total = t;
                    if (dash && dash < sl && parse_u64(p, dash, &a) && parse_u64(dash + 1, sl, &z) && a <= z && z < t) {
                        rs->range_start = a;
                        rs->range_end = z;
                        rs->has_range = true;
                    }
                }
            }
        } else if (hdr_is(l, lr, "ETag", &vs, &ve)) {
            if (ve - vs > 2 && vs[0] == 'W' && vs[1] == '/') {
                vs += 2;
            }
            if (ve - vs >= 2 && *vs == '"' && ve[-1] == '"') {
                vs++;
                ve--;
            }
            copy_span(rs->etag, sizeof(rs->etag), vs, ve);
        } else if (hdr_is(l, lr, "Content-Type", &vs, &ve)) {
            copy_span(rs->ctype, sizeof(rs->ctype), vs, ve);
        } else if (hdr_is(l, lr, "Retry-After", &vs, &ve)) {
            if (parse_u64(vs, ve, &u)) {
                rs->retry_after_s = u > 3600 ? 3600 : (uint32_t)u;
            }
        } else if (hdr_is(l, lr, "Set-Cookie", &vs, &ve)) {
            /* "fxs=<32 hex>; Path=/; HttpOnly; SameSite=Strict" (FlexOS_MediaWeb) */
            if (ve - vs >= 4 && strncmp(vs, "fxs=", 4) == 0) {
                const char *v = vs + 4, *semi = memchr(v, ';', (size_t)(ve - v));
                copy_span(rs->cookie, sizeof(rs->cookie), v, semi ? semi : ve);
                rs->cookie_set = true;
                if (contains_ci(vs, ve, "max-age=0")) {
                    rs->cookie[0] = 0;
                }
            }
        }
        l = le;
    }
    c->keep = *http11 ? !conn_close : conn_keep && !conn_close;
    return rs->status;
}

static int read_head(gal_conn_t *c, gal_resp_t *rs)
{
    c->bp = c->bn = 0;
    for (;;) {
        /* fin de las cabeceras en lo que ya hay */
        size_t hlen = 0;
        for (size_t i = c->bp; i + 3 < c->bn; i++) {
            if (c->buf[i] == '\r' && c->buf[i + 1] == '\n' && c->buf[i + 2] == '\r' && c->buf[i + 3] == '\n') {
                hlen = i + 4 - c->bp;
                break;
            }
        }
        if (!hlen) {
            const bool empty = c->bn == 0;
            const int r = fill(c);
            if (r == 0) {
                return empty ? E_EMPTY : GAL_HTTP_E_CLOSED;
            }
            if (r < 0) {
                return r;
            }
            continue;
        }
        if (c->bp) {
            memmove(c->buf, c->buf + c->bp, c->bn - c->bp);
            c->bn -= c->bp;
            c->bp = 0;
        }
        memset(rs, 0, sizeof(*rs));
        rs->length = -1;
        bool http11 = true;
        const int st = parse_head(c, hlen, rs, &http11);
        c->bp = hlen;
        if (st < 0) {
            return st;
        }
        if (st >= 100 && st < 200) {
            continue; /* 100 Continue u otra informativa: viene otra cabecera */
        }
        return st;
    }
}

int gal_http_begin(gal_conn_t *c, const gal_req_t *rq, gal_resp_t *rs)
{
    memset(rs, 0, sizeof(*rs));
    rs->length = -1;
    if (!c || !c->io || !rq || !rq->method || !rq->host || !rq->host[0] || strlen(rq->host) >= GAL_HTTP_HOST_MAX ||
        !safe_path(rq->path) || !safe_value(rq->host) || !safe_value(rq->bearer) || !safe_value(rq->cookie) ||
        !safe_value(rq->agent) || !safe_value(rq->ctype) || !safe_value(rq->if_range)) {
        return rs->status = GAL_HTTP_E_ARG;
    }
    if (c->in_body) {
        gal_http_end(c); /* la anterior no se cerró: no se mezclan respuestas */
    }
    c->to_ms = rq->timeout_ms ? rq->timeout_ms : GAL_HTTP_TIMEOUT_MS;
    const bool same = c->open && !strcmp(c->host, rq->host) && c->port == rq->port && c->tls == rq->tls;
    if (c->open && (!same || now_ms(c) - c->idle_since > GAL_HTTP_IDLE_MS)) {
        gal_conn_close(c);
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool reused = c->open;
        if (!c->open) {
            if (cancelled(c)) {
                return rs->status = GAL_HTTP_E_CANCEL;
            }
            const int r = c->io->connect(c->io->ctx, rq->host, rq->port, rq->tls, c->to_ms);
            if (r != GAL_IO_OK) {
                c->io->close(c->io->ctx);
                return rs->status = r == GAL_IO_E_TLS       ? GAL_HTTP_E_TLS
                                    : r == GAL_IO_E_DNS     ? GAL_HTTP_E_DNS
                                    : r == GAL_IO_E_TIMEOUT ? GAL_HTTP_E_TIMEOUT
                                                            : GAL_HTTP_E_CONNECT;
            }
            c->open = true;
            c->opens++;
            snprintf(c->host, sizeof(c->host), "%s", rq->host);
            c->port = rq->port;
            c->tls = rq->tls;
        }
        c->reqs++;
        c->bn = c->bp = 0;
        int r = send_request(c, rq);
        if (r == 0) {
            r = read_head(c, rs);
        }
        /* reutilizada y cerrada por el otro lado antes de contestar: una vez más, por una nueva */
        const bool retry = reused && attempt == 0 && !rq->body_cb && (r == E_EMPTY || r == GAL_HTTP_E_CLOSED);
        if (r < 0) {
            gal_conn_close(c);
            if (retry) {
                continue;
            }
            return rs->status = r == E_EMPTY ? GAL_HTTP_E_CLOSED : r;
        }
        c->in_body = true;
        c->head = !strcmp(rq->method, "HEAD");
        c->body_done = c->head || rs->status == 204 || rs->status == 304;
        c->chunked = !c->body_done && rs->chunked;
        c->cstate = 0;
        c->chunk_left = 0;
        c->left = c->body_done ? 0 : c->chunked ? -1 : rs->length;
        if (!c->body_done && !c->chunked && rs->length < 0) {
            c->keep = false; /* hasta el cierre */
        }
        return rs->status;
    }
    return rs->status = GAL_HTTP_E_CLOSED;
}

/* Copia del búfer o lee directamente del transporte. >0, 0 cerrada, <0 error. */
static int take(gal_conn_t *c, uint8_t *out, size_t n)
{
    if (c->bp < c->bn) {
        const size_t k = c->bn - c->bp < n ? c->bn - c->bp : n;
        memcpy(out, c->buf + c->bp, k);
        c->bp += k;
        return (int)k;
    }
    return io_read(c, out, n);
}

/* Asegura una línea completa en el búfer; devuelve su longitud (sin CRLF) o <0. */
static int line(gal_conn_t *c, const char **ls)
{
    for (;;) {
        for (size_t i = c->bp; i + 1 < c->bn; i++) {
            if (c->buf[i] == '\r' && c->buf[i + 1] == '\n') {
                *ls = (const char *)c->buf + c->bp;
                const int n = (int)(i - c->bp);
                c->bp = i + 2;
                return n;
            }
        }
        if (c->bn - c->bp >= LINE_MAX) {
            return GAL_HTTP_E_PROTO;
        }
        if (c->bp > 0) {
            memmove(c->buf, c->buf + c->bp, c->bn - c->bp);
            c->bn -= c->bp;
            c->bp = 0;
        }
        const int r = fill(c);
        if (r == 0) {
            return GAL_HTTP_E_CLOSED;
        }
        if (r < 0) {
            return r;
        }
    }
}

int gal_http_read(gal_conn_t *c, uint8_t *out, size_t n)
{
    if (!c || !c->in_body || !out) {
        return GAL_HTTP_E_ARG;
    }
    if (c->body_done || !n) {
        return 0;
    }
    if (cancelled(c)) {
        return GAL_HTTP_E_CANCEL;
    }
    if (!c->chunked) {
        if (c->left == 0) {
            c->body_done = true;
            return 0;
        }
        const size_t want = c->left > 0 && (uint64_t)c->left < n ? (size_t)c->left : n;
        const int r = take(c, out, want);
        if (r == 0) {
            if (c->left < 0) {
                c->body_done = true; /* hasta el cierre: este es el final */
                return 0;
            }
            return GAL_HTTP_E_CLOSED; /* faltaban bytes: respuesta incompleta */
        }
        if (r > 0 && c->left > 0) {
            c->left -= r;
        }
        return r;
    }
    for (;;) {
        const char *ls;
        int k;
        switch (c->cstate) {
        case 0: {
            k = line(c, &ls);
            if (k < 0) {
                return k;
            }
            uint64_t sz = 0;
            int digits = 0;
            for (int i = 0; i < k && ls[i] != ';' && ls[i] != ' '; i++) {
                const char h = ls[i];
                const int v = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                if (v < 0 || ++digits > 15) {
                    return GAL_HTTP_E_PROTO;
                }
                sz = sz << 4 | (uint64_t)v;
            }
            if (!digits) {
                return GAL_HTTP_E_PROTO;
            }
            c->chunk_left = sz;
            c->cstate = sz ? 1 : 3;
            break;
        }
        case 1: {
            const size_t want = c->chunk_left < n ? (size_t)c->chunk_left : n;
            const int r = take(c, out, want);
            if (r == 0) {
                return GAL_HTTP_E_CLOSED;
            }
            if (r < 0) {
                return r;
            }
            c->chunk_left -= (uint64_t)r;
            if (!c->chunk_left) {
                c->cstate = 2;
            }
            return r;
        }
        case 2:
            k = line(c, &ls);
            if (k != 0) {
                return k < 0 ? k : GAL_HTTP_E_PROTO;
            }
            c->cstate = 0;
            break;
        case 3:
            k = line(c, &ls);
            if (k < 0) {
                return k;
            }
            if (k == 0) {
                c->cstate = 4;
                c->body_done = true;
                return 0;
            }
            break; /* remolque: se ignora */
        default: c->body_done = true; return 0;
        }
    }
}

int gal_http_read_all(gal_conn_t *c, char *out, size_t cap, size_t *len)
{
    size_t w = 0;
    if (len) {
        *len = 0;
    }
    if (!out || cap < 2) {
        return GAL_HTTP_E_ARG;
    }
    for (;;) {
        if (w + 1 >= cap) {
            uint8_t probe;
            const int r = gal_http_read(c, &probe, 1);
            if (r == 0) {
                break;
            }
            out[0] = 0;
            gal_conn_close(c);
            return r < 0 ? r : GAL_HTTP_E_TOOBIG;
        }
        const int r = gal_http_read(c, (uint8_t *)out + w, cap - 1 - w);
        if (r < 0) {
            out[w] = 0;
            return r;
        }
        if (r == 0) {
            break;
        }
        w += (size_t)r;
    }
    out[w] = 0;
    if (len) {
        *len = w;
    }
    return 0;
}

void gal_http_end(gal_conn_t *c)
{
    if (!c || !c->in_body) {
        return;
    }
    if (!c->body_done && c->keep) {
        uint8_t scratch[512];
        size_t drained = 0;
        while (drained < DRAIN_MAX) {
            const int r = gal_http_read(c, scratch, sizeof(scratch));
            if (r <= 0) {
                break;
            }
            drained += (size_t)r;
        }
    }
    const bool reuse = c->keep && c->body_done && c->bp == c->bn;
    c->in_body = false;
    if (!reuse) {
        gal_conn_close(c);
    } else {
        c->idle_since = now_ms(c);
        c->bp = c->bn = 0;
    }
}

const char *gal_http_err_text(int err)
{
    switch (err) {
    case GAL_HTTP_E_CONNECT: return "No se pudo conectar con el servidor";
    case GAL_HTTP_E_TLS: return "Conexión segura no verificada (certificado no válido)";
    case GAL_HTTP_E_DNS: return "No se encuentra el servidor (DNS)";
    case GAL_HTTP_E_TIMEOUT: return "El servidor no contestó a tiempo";
    case GAL_HTTP_E_CLOSED: return "La conexión se cortó a mitad de la respuesta";
    case GAL_HTTP_E_PROTO: return "Respuesta del servidor no válida";
    case GAL_HTTP_E_TOOBIG: return "Respuesta demasiado grande";
    case GAL_HTTP_E_CANCEL: return "Cancelado";
    case GAL_HTTP_E_BODY: return "No se pudo leer el archivo que se enviaba";
    default: return "Error de comunicación";
    }
}

bool gal_url_encode(const char *s, char *out, size_t n)
{
    static const char H[] = "0123456789ABCDEF";
    size_t w = 0;
    if (!out || !n) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        const bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                           *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (w + (plain ? 1 : 3) >= n) {
            out[0] = 0;
            return false;
        }
        if (plain) {
            out[w++] = (char)*p;
        } else {
            out[w++] = '%';
            out[w++] = H[*p >> 4];
            out[w++] = H[*p & 15];
        }
    }
    out[w] = 0;
    return true;
}
