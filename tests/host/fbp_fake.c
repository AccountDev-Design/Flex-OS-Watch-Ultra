/*
 * fbp_fake — ver fbp_fake.h.
 */
#define _GNU_SOURCE /* strcasestr */
#include "fbp_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct fbp_fake {
    fbp_fake_cfg_t cfg;
    fbp_fake_log_t log;
    br_io_t io;
    uint32_t clock, rng;
    bool open, upgraded, closing, welcomed;
    uint8_t *in;
    size_t in_n, in_cap;
    uint8_t *out;
    size_t out_n, out_off, out_cap;
    uint16_t seq;
    uint32_t frame_id;
};

static void grow(uint8_t **b, size_t *cap, size_t need)
{
    if (need <= *cap) {
        return;
    }
    size_t c = *cap ? *cap : 4096;
    while (c < need) {
        c *= 2;
    }
    *b = realloc(*b, c);
    *cap = c;
}

static void out_put(fbp_fake_t *f, const void *p, size_t n)
{
    grow(&f->out, &f->out_cap, f->out_n + n);
    memcpy(f->out + f->out_n, p, n);
    f->out_n += n;
}

/* trama WebSocket del servidor: sin máscara */
static void ws_out(fbp_fake_t *f, uint8_t op, const uint8_t *p, size_t n)
{
    uint8_t h[10];
    size_t k = 0;
    h[k++] = (uint8_t)(0x80 | op);
    if (n < 126) {
        h[k++] = (uint8_t)n;
    } else if (n <= 0xFFFF) {
        h[k++] = 126;
        h[k++] = (uint8_t)(n >> 8);
        h[k++] = (uint8_t)n;
    } else {
        h[k++] = 127;
        for (int i = 7; i >= 0; i--) {
            h[k++] = (uint8_t)((uint64_t)n >> (8 * i));
        }
    }
    out_put(f, h, k);
    if (n) {
        out_put(f, p, n);
    }
}

void fbp_fake_send(fbp_fake_t *f, uint8_t type, uint8_t ch, const uint8_t *payload, uint32_t n)
{
    uint8_t *m = malloc(FBP_HDR_SIZE + n);
    fbpWriteHeader(m, type, 0, ch, f->seq++, n);
    if (n) {
        memcpy(m + FBP_HDR_SIZE, payload, n);
    }
    ws_out(f, 0x2, m, FBP_HDR_SIZE + n);
    free(m);
}

static size_t put_str(uint8_t *p, const char *s)
{
    const size_t l = strlen(s);
    p[0] = (uint8_t)l;
    p[1] = (uint8_t)(l >> 8);
    memcpy(p + 2, s, l);
    return 2 + l;
}

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void send_state(fbp_fake_t *f, uint8_t ch, uint8_t flags, uint8_t prog, const char *title, const char *url)
{
    uint8_t p[1200];
    size_t o = 0;
    p[o++] = flags;
    p[o++] = prog;
    o += put_str(p + o, title);
    o += put_str(p + o, url);
    fbp_fake_send(f, FBP_S_STATE, ch, p, (uint32_t)o);
}

static void send_frame(fbp_fake_t *f, uint8_t ch)
{
    static const uint8_t dummy[] = {0xFF, 0xD8, 0xFF, 0xDB, 1, 2, 3, 4, 0xFF, 0xD9};
    uint32_t len = sizeof(dummy);
    uint16_t w = f->log.vp_w ? f->log.vp_w : f->log.hello_w, h = f->log.vp_h ? f->log.vp_h : f->log.hello_h;
    const uint8_t *img = dummy;
    if (f->cfg.img) {
        img = f->cfg.img(f->cfg.img_user, f->log.scroll_y, &len, &w, &h);
    }
    f->frame_id++;
    const int parts = f->cfg.bands ? 2 : 1;
    for (int i = 0; i < parts; i++) {
        const uint16_t bh = (uint16_t)(h / parts), y = (uint16_t)(bh * i);
        uint8_t *p = malloc(18 + len);
        w16(p, 0);
        w16(p + 2, y);
        w16(p + 4, w);
        w16(p + 6, i == parts - 1 ? (uint16_t)(h - y) : bh);
        p[8] = FBP_IMG_JPEG;
        p[9] = (uint8_t)(FBP_FR_KEYFRAME | (i == parts - 1 ? FBP_FR_LAST : 0));
        w32(p + 10, f->frame_id);
        w32(p + 14, len);
        memcpy(p + 18, img, len);
        fbp_fake_send(f, FBP_S_FRAME, ch, p, 18 + len);
        free(p);
    }
}

static bool get_str(const uint8_t *p, uint32_t n, uint32_t *o, char *out, size_t cap)
{
    if (*o + 2 > n) {
        return false;
    }
    const uint16_t l = (uint16_t)(p[*o] | p[*o + 1] << 8);
    *o += 2;
    if (*o + l > n) {
        return false;
    }
    const size_t c = l < cap - 1 ? l : cap - 1;
    memcpy(out, p + *o, c);
    out[c] = 0;
    *o += l;
    return true;
}

static void on_fbp(fbp_fake_t *f, const uint8_t *m, size_t n)
{
    FbpHeader h;
    if (!fbpReadHeader(m, n, &h) || h.len != n - FBP_HDR_SIZE) {
        return;
    }
    const uint8_t *p = m + FBP_HDR_SIZE;
    fbp_fake_log_t *L = &f->log;
    switch (h.type) {
    case FBP_C_HELLO: {
        L->hellos++;
        L->hello_w = (uint16_t)(p[1] | p[2] << 8);
        L->hello_h = (uint16_t)(p[3] | p[4] << 8);
        uint32_t o = 17;
        char dev[64];
        get_str(p, h.len, &o, L->last_token, sizeof(L->last_token));
        get_str(p, h.len, &o, dev, sizeof(dev));
        if (f->cfg.mute_hello) {
            break;
        }
        if (strcmp(L->last_token, f->cfg.token ? f->cfg.token : "") != 0) {
            uint8_t e[40];
            size_t k = 0;
            w16(e, FBP_ERR_AUTH);
            k = 2 + put_str(e + 2, "Credencial no valida");
            fbp_fake_send(f, FBP_S_ERROR, 0, e, (uint32_t)k);
            ws_out(f, 0x8, NULL, 0);
            f->closing = true;
            break;
        }
        uint8_t w[64];
        w[0] = 1;
        w16(w + 1, L->hello_w);
        w16(w + 3, L->hello_h);
        w32(w + 5, 0x0F);
        w16(w + 9, 4);
        w32(w + 11, FBP_PAYLOAD_MAX);
        const size_t k = 15 + put_str(w + 15, f->cfg.session_id ? f->cfg.session_id : "sess-1");
        fbp_fake_send(f, FBP_S_WELCOME, 0, w, (uint32_t)k);
        f->welcomed = true;
        break;
    }
    case FBP_C_NAVIGATE: {
        L->navigates++;
        uint32_t o = 0;
        get_str(p, h.len, &o, L->last_url, sizeof(L->last_url));
        L->scroll_y = 0;
        send_state(f, h.channel, FBP_ST_LOADING | FBP_ST_SECURE, 10, "", L->last_url);
        uint8_t nv[600];
        fbp_fake_send(f, FBP_S_NAVIGATED, h.channel, nv, (uint32_t)put_str(nv, L->last_url));
        if (!f->cfg.no_frames) {
            send_frame(f, h.channel);
        }
        send_state(f, h.channel, FBP_ST_SECURE | FBP_ST_CAN_BACK, 100, "Página de prueba", L->last_url);
        break;
    }
    case FBP_C_SCROLL: {
        L->scrolls++;
        const int16_t dy = (int16_t)(p[2] | p[3] << 8);
        L->scroll_y += dy;
        if (L->scroll_y < 0) {
            L->scroll_y = 0;
        }
        if (!f->cfg.no_frames) {
            send_frame(f, h.channel);
        }
        break;
    }
    case FBP_C_POINTER:
        L->taps++;
        L->tap_x = (uint16_t)(p[1] | p[2] << 8);
        L->tap_y = (uint16_t)(p[3] | p[4] << 8);
        break;
    case FBP_C_KEY:
        L->keys++;
        if (p[0] == FBP_KEY_TEXT) {
            uint32_t o = 3;
            char t[32];
            if (get_str(p, h.len, &o, t, sizeof(t)) && strlen(L->typed) + strlen(t) < sizeof(L->typed)) {
                strcat(L->typed, t);
            }
        } else {
            L->last_vk = p[1];
        }
        break;
    case FBP_C_VIEWPORT:
        L->viewports++;
        L->vp_w = (uint16_t)(p[0] | p[1] << 8);
        L->vp_h = (uint16_t)(p[2] | p[3] << 8);
        L->vp_quality = p[4];
        L->vp_scale = p[6];
        break;
    case FBP_C_ACK: L->acks++; break;
    case FBP_C_PING:
        L->pings++;
        fbp_fake_send(f, FBP_S_PONG, 0, NULL, 0);
        break;
    case FBP_C_BACK: L->backs++; break;
    case FBP_C_RELOAD: L->reloads++; break;
    case FBP_C_REQ_FRAME:
        L->req_frames++;
        if (!f->cfg.no_frames) {
            send_frame(f, h.channel);
        }
        break;
    default: break;
    }
}

static void consume(fbp_fake_t *f)
{
    if (!f->upgraded) {
        uint8_t *end = NULL;
        for (size_t i = 3; i < f->in_n; i++) {
            if (!memcmp(f->in + i - 3, "\r\n\r\n", 4)) {
                end = f->in + i + 1;
                break;
            }
        }
        if (!end) {
            return;
        }
        char req[1024];
        const size_t rl = (size_t)(end - f->in) < sizeof(req) - 1 ? (size_t)(end - f->in) : sizeof(req) - 1;
        memcpy(req, f->in, rl);
        req[rl] = 0;
        char key[64] = "", acc[40] = "";
        const char *k = strcasestr(req, "Sec-WebSocket-Key:");
        if (k) {
            sscanf(k + 18, " %63s", key);
        }
        const char *ua = strcasestr(req, "User-Agent:");
        if (ua) {
            sscanf(ua + 11, " %127[^\r]", f->log.ua);
        }
        flexBrWsAccept(f->cfg.bad_accept ? "AAAAAAAAAAAAAAAAAAAAAA==" : key, acc, sizeof(acc));
        char resp[256];
        const int n = snprintf(resp, sizeof(resp),
                               "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                               "Sec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: fbp.v1\r\n\r\n",
                               acc);
        out_put(f, resp, (size_t)n);
        f->upgraded = true;
        const size_t used = (size_t)(end - f->in);
        memmove(f->in, f->in + used, f->in_n - used);
        f->in_n -= used;
    }
    for (;;) {
        if (f->in_n < 2) {
            return;
        }
        const uint8_t *b = f->in;
        size_t hl = 2;
        uint64_t len = b[1] & 0x7F;
        if (len == 126) {
            if (f->in_n < 4) {
                return;
            }
            len = (uint64_t)b[2] << 8 | b[3];
            hl = 4;
        } else if (len == 127) {
            if (f->in_n < 10) {
                return;
            }
            len = 0;
            for (int i = 0; i < 8; i++) {
                len = len << 8 | b[2 + i];
            }
            hl = 10;
        }
        const bool masked = b[1] & 0x80;
        if (!masked) {
            f->closing = true; /* RFC 6455: el cliente SIEMPRE enmascara */
            return;
        }
        if (f->in_n < hl + 4 + len) {
            return;
        }
        const uint8_t *mk = b + hl;
        uint8_t *data = f->in + hl + 4;
        for (uint64_t i = 0; i < len; i++) {
            data[i] ^= mk[i & 3];
        }
        const uint8_t op = b[0] & 0x0F;
        if (op == 0x2) {
            on_fbp(f, data, (size_t)len);
        } else if (op == 0x8) {
            f->closing = true;
        }
        const size_t used = hl + 4 + (size_t)len;
        memmove(f->in, f->in + used, f->in_n - used);
        f->in_n -= used;
    }
}

/* ---- br_io_t ---- */

static int io_connect(void *ctx, const char *host, uint16_t port, bool tls)
{
    fbp_fake_t *f = ctx;
    (void)host;
    (void)port;
    f->log.connects++;
    f->clock += 30;
    if (f->cfg.refuse_connect) {
        return BR_IO_E_CONNECT;
    }
    if (tls && f->cfg.tls_bad) {
        return BR_IO_E_TLS;
    }
    f->open = true;
    f->upgraded = f->closing = f->welcomed = false;
    f->in_n = f->out_n = f->out_off = 0;
    return BR_IO_OK;
}

static int io_write(void *ctx, const uint8_t *b, size_t n, uint32_t to_ms)
{
    fbp_fake_t *f = ctx;
    (void)to_ms;
    if (!f->open || f->closing) {
        return -1;
    }
    grow(&f->in, &f->in_cap, f->in_n + n);
    memcpy(f->in + f->in_n, b, n);
    f->in_n += n;
    consume(f);
    return (int)n;
}

static int io_read(void *ctx, uint8_t *b, size_t n, uint32_t to_ms)
{
    fbp_fake_t *f = ctx;
    if (!f->open) {
        return -1;
    }
    const size_t av = f->out_n - f->out_off;
    if (av == 0) {
        if (f->closing) {
            return -1;
        }
        f->clock += to_ms; /* plazo sin datos: corre el reloj virtual */
        return 0;
    }
    size_t c = av < n ? av : n;
    if (f->cfg.chunk && c > f->cfg.chunk) {
        c = f->cfg.chunk;
    }
    memcpy(b, f->out + f->out_off, c);
    f->out_off += c;
    if (f->out_off == f->out_n) {
        f->out_off = f->out_n = 0;
    }
    f->clock += 1;
    return (int)c;
}

static void io_close(void *ctx)
{
    fbp_fake_t *f = ctx;
    f->open = false;
}

static uint32_t io_now(void *ctx)
{
    return ((fbp_fake_t *)ctx)->clock;
}

static uint32_t io_rand(void *ctx)
{
    fbp_fake_t *f = ctx;
    f->rng = f->rng * 1664525u + 1013904223u;
    return f->rng;
}

fbp_fake_t *fbp_fake_new(const fbp_fake_cfg_t *cfg)
{
    fbp_fake_t *f = calloc(1, sizeof(*f));
    f->cfg = *cfg;
    f->clock = 1000;
    f->rng = 12345;
    f->seq = 1;
    f->io = (br_io_t){.ctx = f, .connect = io_connect, .write = io_write, .read = io_read, .close = io_close,
                      .now = io_now, .rand = io_rand};
    return f;
}

void fbp_fake_free(fbp_fake_t *f)
{
    free(f->in);
    free(f->out);
    free(f);
}

const br_io_t *fbp_fake_io(fbp_fake_t *f)
{
    return &f->io;
}

fbp_fake_log_t *fbp_fake_log(fbp_fake_t *f)
{
    return &f->log;
}

fbp_fake_cfg_t *fbp_fake_cfg(fbp_fake_t *f)
{
    return &f->cfg;
}

void fbp_fake_drop(fbp_fake_t *f)
{
    ws_out(f, 0x8, NULL, 0);
    f->closing = true;
}

uint32_t fbp_fake_now(fbp_fake_t *f)
{
    return f->clock;
}

void fbp_fake_advance(fbp_fake_t *f, uint32_t ms)
{
    f->clock += ms;
}
