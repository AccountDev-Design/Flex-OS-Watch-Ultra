/*
 * gal_fake — ver gal_fake.h.
 */
#include "gal_fake.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void reset_conn(gf_conn_t *c)
{
    c->in_n = 0;
    free(c->out);
    c->out = NULL;
    c->out_n = c->out_p = 0;
    c->close_after = c->stall = c->dead = false;
}

void gf_init(gf_conn_t *c, gf_handler_t h, void *ud, uint32_t *clock)
{
    memset(c, 0, sizeof(*c));
    c->handler = h;
    c->ud = ud;
    c->clock = clock;
}

void gf_free(gf_conn_t *c)
{
    free(c->in);
    free(c->out);
    c->in = c->out = NULL;
}

static int io_connect(void *ctx, const char *host, uint16_t port, bool tls, uint32_t to_ms)
{
    gf_conn_t *c = ctx;
    (void)to_ms;
    reset_conn(c);
    c->open = false;
    if (c->fail_connect > 0) {
        c->fail_connect--;
        return c->fail_code ? c->fail_code : GAL_IO_E_CONNECT;
    }
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->port = port;
    c->tls = tls;
    c->open = true;
    c->connects++;
    return GAL_IO_OK;
}

static const char *hdr(const char *h, const char *he, const char *name, size_t *vl)
{
    const size_t nl = strlen(name);
    for (const char *l = h; l && l < he;) {
        const char *le = memchr(l, '\n', (size_t)(he - l));
        if (!le) {
            break;
        }
        if ((size_t)(le - l) > nl + 1 && !strncasecmp(l, name, nl) && l[nl] == ':') {
            const char *v = l + nl + 1;
            while (*v == ' ') {
                v++;
            }
            const char *ve = le;
            while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) {
                ve--;
            }
            *vl = (size_t)(ve - v);
            return v;
        }
        l = le + 1;
    }
    return NULL;
}

static void cpy(char *out, size_t n, const char *v, size_t vl)
{
    if (!v) {
        out[0] = 0;
        return;
    }
    if (vl >= n) {
        vl = n - 1;
    }
    memcpy(out, v, vl);
    out[vl] = 0;
}

/* ¿Hay una petición entera en c->in? Si la hay, la sirve. */
static void try_serve(gf_conn_t *c)
{
    const char *b = (const char *)c->in;
    const char *end = NULL;
    for (size_t i = 0; i + 3 < c->in_n; i++) {
        if (!memcmp(b + i, "\r\n\r\n", 4)) {
            end = b + i + 4;
            break;
        }
    }
    if (!end) {
        return;
    }
    size_t vl = 0;
    const char *v = hdr(b, end, "Content-Length", &vl);
    const size_t blen = v ? (size_t)strtoull(v, NULL, 10) : 0;
    const size_t hlen = (size_t)(end - b);
    if (c->in_n < hlen + blen) {
        return;
    }
    gf_req_t *rq = &c->last;
    memset(rq, 0, sizeof(*rq));
    sscanf(b, "%7s %511s", rq->method, rq->path);
    v = hdr(b, end, "Host", &vl), cpy(rq->host, sizeof(rq->host), v, vl);
    v = hdr(b, end, "Cookie", &vl), cpy(rq->cookie, sizeof(rq->cookie), v, vl);
    v = hdr(b, end, "Authorization", &vl), cpy(rq->auth, sizeof(rq->auth), v, vl);
    v = hdr(b, end, "Content-Type", &vl), cpy(rq->ctype, sizeof(rq->ctype), v, vl);
    v = hdr(b, end, "Range", &vl), cpy(rq->range, sizeof(rq->range), v, vl);
    v = hdr(b, end, "If-Range", &vl), cpy(rq->if_range, sizeof(rq->if_range), v, vl);
    v = hdr(b, end, "X-Part-SHA256", &vl), cpy(rq->part_sha, sizeof(rq->part_sha), v, vl);
    v = hdr(b, end, "User-Agent", &vl), cpy(rq->agent, sizeof(rq->agent), v, vl);
    v = hdr(b, end, "X-Flex", &vl);
    rq->xflex = v && vl == 1 && *v == '1';
    rq->body = c->in + hlen;
    rq->body_len = blen;
    c->requests++;
    gf_resp_t rs;
    memset(&rs, 0, sizeof(rs));
    rs.cut_after = -1;
    c->handler(c->ud, rq, &rs);
    /* la respuesta entera */
    const size_t cap = 1024 + rs.len * 3 + 64; /* chunked de 7 en 7: 11 bytes de más por trozo */
    char *o = malloc(cap);
    size_t w = 0;
    w += (size_t)snprintf(o + w, cap - w, "HTTP/1.1 %d X\r\n", rs.status ? rs.status : 500);
    if (rs.ctype[0]) {
        w += (size_t)snprintf(o + w, cap - w, "Content-Type: %s\r\n", rs.ctype);
    }
    if (rs.chunked) {
        w += (size_t)snprintf(o + w, cap - w, "Transfer-Encoding: chunked\r\n");
    } else if (!rs.no_length) {
        w += (size_t)snprintf(o + w, cap - w, "Content-Length: %zu\r\n", rs.len);
    }
    if (rs.close || rs.no_length) {
        w += (size_t)snprintf(o + w, cap - w, "Connection: close\r\n");
    }
    w += (size_t)snprintf(o + w, cap - w, "%s\r\n", rs.extra);
    if (strcmp(rq->method, "HEAD") != 0) {
        if (rs.chunked) {
            for (size_t p = 0; p < rs.len;) {
                const size_t k = rs.len - p < 7 ? rs.len - p : 7; /* trozos pequeños: más fronteras */
                w += (size_t)snprintf(o + w, cap - w, "%zx;ext=1\r\n", k);
                memcpy(o + w, rs.body + p, k);
                w += k;
                memcpy(o + w, "\r\n", 2);
                w += 2;
                p += k;
            }
            w += (size_t)snprintf(o + w, cap - w, "0\r\nX-Trailer: 1\r\n\r\n");
        } else if (rs.len) {
            memcpy(o + w, rs.body, rs.len);
            w += rs.len;
        }
    }
    free(rs.body);
    if (rs.cut_after >= 0 && (size_t)rs.cut_after < w) {
        w = (size_t)rs.cut_after;
        c->close_after = true;
    }
    free(c->out);
    c->out = (uint8_t *)o;
    c->out_n = w;
    c->out_p = 0;
    c->close_after = c->close_after || rs.close || rs.no_length || c->server_closes_idle;
    c->stall = rs.stall;
    if (!w && c->close_after) {
        c->dead = true; /* cortada antes de contestar */
    }
    /* lo consumido sale del búfer de entrada (keep-alive) */
    memmove(c->in, c->in + hlen + blen, c->in_n - hlen - blen);
    c->in_n -= hlen + blen;
}

static int io_write(void *ctx, const uint8_t *p, size_t n, uint32_t to_ms)
{
    gf_conn_t *c = ctx;
    (void)to_ms;
    if (!c->open || c->dead) {
        return -1;
    }
    if (c->in_n + n > c->in_cap) {
        c->in_cap = (c->in_n + n) * 2 + 4096;
        c->in = realloc(c->in, c->in_cap);
    }
    memcpy(c->in + c->in_n, p, n);
    c->in_n += n;
    c->bytes_in += n;
    try_serve(c);
    return (int)n;
}

static int io_read(void *ctx, uint8_t *p, size_t n, uint32_t to_ms)
{
    gf_conn_t *c = ctx;
    if (!c->open || c->dead) {
        return -1;
    }
    if (c->stall || !c->out || c->out_p >= c->out_n) {
        if (c->clock) {
            *c->clock += to_ms; /* el plazo pasa sin esperar de verdad */
        }
        return 0;
    }
    size_t k = c->out_n - c->out_p;
    if (k > n) {
        k = n;
    }
    if (k > 1500) {
        k = 1500; /* como llegaría por Wi-Fi: a trozos */
    }
    memcpy(p, c->out + c->out_p, k);
    c->out_p += k;
    c->bytes_out += k;
    if (c->out_p == c->out_n && c->close_after) {
        c->dead = true; /* el servidor cerró tras la respuesta (lo diga o no) */
    }
    return (int)k;
}

static void io_close(void *ctx)
{
    gf_conn_t *c = ctx;
    if (c->open) {
        c->closes++;
    }
    c->open = false;
    reset_conn(c);
}

static uint32_t io_now(void *ctx)
{
    gf_conn_t *c = ctx;
    return c->clock ? *c->clock : 0;
}

gal_io_t gf_io(gf_conn_t *c)
{
    return (gal_io_t){.ctx = c, .connect = io_connect, .write = io_write, .read = io_read, .close = io_close, .now = io_now};
}

void gf_json(gf_resp_t *rs, int status, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char probe[1];
    const int n = vsnprintf(probe, sizeof(probe), fmt, ap); /* solo la longitud */
    va_end(ap);
    rs->body = malloc((size_t)n + 1);
    if (!rs->body) {
        va_end(ap2);
        rs->status = 500;
        return;
    }
    vsnprintf((char *)rs->body, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    rs->len = (size_t)n;
    rs->status = status;
    snprintf(rs->ctype, sizeof(rs->ctype), "application/json; charset=utf-8");
}

void gf_bytes(gf_resp_t *rs, int status, const char *ctype, const void *data, size_t n)
{
    rs->body = malloc(n ? n : 1);
    if (rs->body && n) {
        memcpy(rs->body, data, n);
    }
    rs->len = n;
    rs->status = status;
    snprintf(rs->ctype, sizeof(rs->ctype), "%s", ctype);
}

void gf_query(const char *path, const char *key, char *out, size_t n)
{
    out[0] = 0;
    const char *q = strchr(path, '?');
    const size_t kl = strlen(key);
    for (const char *p = q ? q + 1 : NULL; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1, *e = strchr(v, '&');
            const size_t vl = e ? (size_t)(e - v) : strlen(v);
            cpy(out, n, v, vl);
            return;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
}

uint8_t *gf_read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    *n = 0;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l > 0 ? (size_t)l : 1);
    *n = l > 0 ? fread(b, 1, (size_t)l, f) : 0;
    fclose(f);
    return b;
}
