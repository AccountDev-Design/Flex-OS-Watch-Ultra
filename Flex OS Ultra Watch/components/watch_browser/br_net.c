/*
 * br_net — ver include/br_net.h. Las citas son de FlexOS_BrowserApp.cpp.
 */
#include "br_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define WS_POLL_MS        40    /* BR_WS_POLL_MS */
#define WS_MSG_MS         9000  /* BR_WS_MSG_MS */
#define HDR_LINE_MS       6000
#define WRITE_MS          5000
#define WELCOME_MS        10000
#define FRAME_CONSUME_MS  5000
#define ACK_MS            250
#define PING_MS           15000
#define FIRSTFRAME_MS     12000 /* BR_FIRSTFRAME_WARN_MS */
#define VP_SETTLE_MS      140
#define VP_MAXDELAY_MS    400
#define BACKOFF_MIN       1000
#define BACKOFF_MAX       16000

static uint32_t now(br_net_t *s)
{
    return s->io->now(s->io->ctx);
}

void br_net_lock(br_net_t *s)
{
    if (s->io->lock) {
        s->io->lock(s->io->ctx);
    }
}

void br_net_unlock(br_net_t *s)
{
    if (s->io->unlock) {
        s->io->unlock(s->io->ctx);
    }
}

static void set_info(br_net_t *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->info, sizeof(s->info), fmt, ap);
    va_end(ap);
    s->info_gen++;
}

static void set_err(br_net_t *s, const char *msg)
{
    snprintf(s->err, sizeof(s->err), "%s", msg);
    s->err_gen++;
    s->stats.errCount++;
}

const char *br_net_state_name(int state)
{
    switch (state) {
    case BRN_OFF: return "Sin sesión";
    case BRN_NOWIFI: return "Sin Wi-Fi";
    case BRN_CONNECTING: return "Conectando";
    case BRN_HANDSHAKE: return "Autenticando";
    case BRN_READY: return "Sesión activa";
    default: return "Con errores";
    }
}

void br_net_init(br_net_t *s, const br_io_t *io, uint8_t *rx, size_t rx_cap)
{
    memset(s, 0, sizeof(*s));
    s->io = io;
    s->rx = rx;
    s->rx_cap = rx_cap;
    s->state = BRN_OFF;
    s->backoff = BACKOFF_MIN;
    s->seq = 1;
    s->active_ch = 1;
    s->stats.relayBuild = -1;
    flexBrSettingsDefaults(&s->set);
}

static void disconnect(br_net_t *s)
{
    if (s->open) {
        s->io->close(s->io->ctx);
    }
    s->open = false;
}

void br_net_configure(br_net_t *s, const char *url, const char *token, const BrSettings *st, const char *device,
                      uint32_t caps)
{
    br_net_lock(s);
    const bool changed = strcmp(s->url, url ? url : "") != 0 || strcmp(s->token, token ? token : "") != 0;
    snprintf(s->url, sizeof(s->url), "%s", url ? url : "");
    snprintf(s->token, sizeof(s->token), "%s", token ? token : "");
    snprintf(s->device, sizeof(s->device), "%s", device ? device : "");
    if (st) {
        const bool vp = st->quality != s->set.quality || st->profile != s->set.profile ||
                        st->scalePct != s->set.scalePct;
        s->set = *st;
        s->vp_dirty |= vp; /* calidad, perfil o escala: VIEWPORT (brSendViewport) */
    }
    s->caps = caps;
    if (changed && (s->state == BRN_READY || s->state == BRN_HANDSHAKE)) {
        s->state = BRN_OFF; /* otro servidor u otra credencial: sesión nueva */
        s->wait_until = 0;
        s->backoff = BACKOFF_MIN;
        disconnect(s);
    } else if (changed) {
        s->wait_until = 0;
        s->backoff = BACKOFF_MIN;
    }
    br_net_unlock(s);
}

void br_net_set_online(br_net_t *s, bool online)
{
    br_net_lock(s);
    if (online && !s->online && s->state == BRN_NOWIFI) {
        s->wait_until = 0; /* vuelve el Wi-Fi: no se espera al siguiente segundo */
    }
    s->online = online;
    br_net_unlock(s);
}

void br_net_set_viewport(br_net_t *s, uint16_t w, uint16_t h, uint8_t active_ch)
{
    br_net_lock(s);
    if (w && h && (w != s->vp_want_w || h != s->vp_want_h)) {
        s->vp_want_w = w;
        s->vp_want_h = h;
        s->vp_set_ms = now(s);
        s->vp_dirty = true;
    }
    if (active_ch >= 1 && active_ch <= FLEXBR_MAX_TABS) {
        s->active_ch = active_ch;
    }
    br_net_unlock(s);
}

/* ---------------- órdenes ---------------- */

static bool push(br_net_t *s, const br_cmd_t *c, const char *url)
{
    br_net_lock(s);
    bool ok = s->qn < BR_CMDQ && (!url || s->un < BR_URLQ);
    if (ok) {
        s->q[(s->qh + s->qn) % BR_CMDQ] = *c;
        s->qn++;
        if (url) {
            snprintf(s->urlq[(s->uh + s->un) % BR_URLQ], FLEXBR_URL_MAX, "%s", url);
            s->un++;
        }
    }
    br_net_unlock(s);
    return ok;
}

bool br_net_navigate(br_net_t *s, uint8_t ch, const char *url)
{
    const br_cmd_t c = {.type = FBP_C_NAVIGATE, .ch = ch};
    return url && url[0] && push(s, &c, url);
}

bool br_net_cmd(br_net_t *s, uint8_t type, uint8_t ch)
{
    const br_cmd_t c = {.type = type, .ch = ch};
    return push(s, &c, NULL);
}

bool br_net_tap(br_net_t *s, uint8_t ch, uint16_t x, uint16_t y)
{
    const br_cmd_t c = {.type = FBP_C_POINTER, .ch = ch, .u1 = FBP_PTR_TAP, .a = (int16_t)x, .b = (int16_t)y};
    return push(s, &c, NULL);
}

bool br_net_scroll(br_net_t *s, uint8_t ch, int16_t dx, int16_t dy)
{
    const br_cmd_t c = {.type = FBP_C_SCROLL, .ch = ch, .a = dx, .b = dy};
    return push(s, &c, NULL);
}

bool br_net_key_text(br_net_t *s, uint8_t ch, const char *utf8)
{
    br_cmd_t c = {.type = FBP_C_KEY, .ch = ch, .u1 = FBP_KEY_TEXT};
    if (!utf8 || strlen(utf8) >= sizeof(c.text)) {
        return false; /* RELOJ: nunca se corta un carácter (la referencia cabía 7 B) */
    }
    snprintf(c.text, sizeof(c.text), "%s", utf8);
    return push(s, &c, NULL);
}

bool br_net_key(br_net_t *s, uint8_t ch, uint8_t vk)
{
    const br_cmd_t c = {.type = FBP_C_KEY, .ch = ch, .u1 = FBP_KEY_DOWN, .u2 = vk};
    return push(s, &c, NULL);
}

bool br_net_media_cmd(br_net_t *s, uint8_t cmd, uint32_t arg)
{
    const br_cmd_t c = {.type = FBP_C_MEDIA, .ch = FBP_CH_MEDIA, .u1 = cmd, .arg = arg};
    return push(s, &c, NULL);
}

/* ---------------- socket y WebSocket ---------------- */

static bool read_full(br_net_t *s, uint8_t *p, size_t n, uint32_t to_ms)
{
    size_t got = 0;
    uint32_t t0 = now(s);
    while (got < n) {
        if (s->stop) {
            return false;
        }
        const int r = s->io->read(s->io->ctx, p + got, n - got, to_ms);
        if (r < 0) {
            return false;
        }
        if (r > 0) {
            got += (size_t)r;
            t0 = now(s);
            continue;
        }
        if (now(s) - t0 > to_ms) {
            return false;
        }
    }
    s->stats.bytesIn += (uint32_t)n;
    return true;
}

static bool write_all(br_net_t *s, const uint8_t *p, size_t n)
{
    size_t sent = 0;
    uint32_t t0 = now(s);
    while (sent < n) {
        const int w = s->io->write(s->io->ctx, p + sent, n - sent, WRITE_MS);
        if (w < 0) {
            return false;
        }
        if (w > 0) {
            sent += (size_t)w;
            t0 = now(s);
            continue;
        }
        if (now(s) - t0 > WRITE_MS) {
            return false;
        }
    }
    s->stats.bytesOut += (uint32_t)n;
    return true;
}

/* wsSendFrame: FIN, enmascarada con máscara aleatoria, en trozos de 128 B */
static bool ws_send(br_net_t *s, uint8_t opcode, const uint8_t *data, size_t n)
{
    uint8_t hdr[14], mask[4];
    size_t h = 0;
    hdr[h++] = (uint8_t)(0x80 | opcode);
    const uint32_t r = s->io->rand(s->io->ctx);
    for (int i = 0; i < 4; i++) {
        mask[i] = (uint8_t)(r >> (8 * i));
    }
    if (n < 126) {
        hdr[h++] = (uint8_t)(0x80 | n);
    } else if (n <= 0xFFFF) {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(n >> 8);
        hdr[h++] = (uint8_t)(n & 0xFF);
    } else {
        hdr[h++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            hdr[h++] = (uint8_t)(((uint64_t)n >> (8 * i)) & 0xFF);
        }
    }
    memcpy(hdr + h, mask, 4);
    h += 4;
    if (!write_all(s, hdr, h)) {
        return false;
    }
    uint8_t chunk[128];
    for (size_t i = 0; i < n;) {
        const size_t c = n - i < sizeof(chunk) ? n - i : sizeof(chunk);
        for (size_t j = 0; j < c; j++) {
            chunk[j] = (uint8_t)(data[i + j] ^ mask[(i + j) & 3]);
        }
        if (!write_all(s, chunk, c)) {
            return false;
        }
        i += c;
    }
    return true;
}

static bool drain(br_net_t *s, uint64_t len)
{
    uint8_t junk[128];
    while (len > 0) {
        const size_t c = len < sizeof(junk) ? (size_t)len : sizeof(junk);
        if (!read_full(s, junk, c, WS_MSG_MS)) {
            return false;
        }
        len -= c;
    }
    return true;
}

/* wsReadMessage: >0 bytes del mensaje, 0 nada, -1 fatal, -2 descartado */
static int ws_read(br_net_t *s, uint32_t poll_ms)
{
    size_t total = 0;
    bool started = false;
    for (;;) {
        uint8_t h[2];
        if (!started) {
            /* el primer byte con el plazo corto: si no llega, no hay mensaje */
            const int r = s->io->read(s->io->ctx, h, 1, poll_ms);
            if (r < 0) {
                return -1;
            }
            if (r == 0) {
                return 0;
            }
            s->stats.bytesIn++;
            if (!read_full(s, h + 1, 1, WS_MSG_MS)) {
                return -1;
            }
        } else if (!read_full(s, h, 2, WS_MSG_MS)) {
            return -1;
        }
        started = true;
        const bool fin = (h[0] & 0x80) != 0;
        const uint8_t op = (uint8_t)(h[0] & 0x0F);
        uint64_t len = (uint64_t)(h[1] & 0x7F);
        if (len == 126) {
            uint8_t e[2];
            if (!read_full(s, e, 2, WS_MSG_MS)) {
                return -1;
            }
            len = ((uint64_t)e[0] << 8) | e[1];
        } else if (len == 127) {
            uint8_t e[8];
            if (!read_full(s, e, 8, WS_MSG_MS)) {
                return -1;
            }
            len = 0;
            for (int i = 0; i < 8; i++) {
                len = (len << 8) | e[i];
            }
        }
        if (h[1] & 0x80) { /* el servidor no debería enmascarar: se ignora */
            uint8_t m[4];
            if (!read_full(s, m, 4, WS_MSG_MS)) {
                return -1;
            }
        }
        if (len > (uint64_t)(FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 1024)) {
            return -1;
        }
        if (op == 0x8) { /* CLOSE */
            ws_send(s, 0x8, NULL, 0);
            return -1;
        }
        if (op == 0x9) { /* PING -> PONG con el mismo cuerpo */
            uint8_t pp[125];
            const size_t pl = len < sizeof(pp) ? (size_t)len : sizeof(pp);
            if ((pl && !read_full(s, pp, pl, WS_MSG_MS)) || !drain(s, len - pl)) {
                return -1;
            }
            ws_send(s, 0xA, pp, pl);
            continue;
        }
        if (op == 0xA) {
            if (!drain(s, len)) {
                return -1;
            }
            continue;
        }
        if (total + (size_t)len > s->rx_cap) {
            if (!drain(s, len)) {
                return -1;
            }
            s->stats.framesBad++;
            if (fin) {
                return -2;
            }
            continue;
        }
        if (len && !read_full(s, s->rx + total, (size_t)len, WS_MSG_MS)) {
            return -1;
        }
        total += (size_t)len;
        if (fin) {
            return (int)total;
        }
    }
}

static bool ws_handshake(br_net_t *s, const char *host, uint16_t port, const char *path)
{
    uint8_t nonce[16];
    for (int i = 0; i < 16; i += 4) {
        const uint32_t r = s->io->rand(s->io->ctx);
        memcpy(nonce + i, &r, 4);
    }
    char key[32], want[40], req[512];
    if (flexBrBase64(nonce, 16, key, sizeof(key)) < 0 || !flexBrWsAccept(key, want, sizeof(want))) {
        return false;
    }
    const int n = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%u\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "Sec-WebSocket-Protocol: fbp.v1\r\n"
                           "User-Agent: FlexOS-Browser/1.0 (%s)\r\n"
                           "\r\n",
                           path, host, (unsigned)port, key, FLEXBR_PLAT_NAME);
    if (n <= 0 || n >= (int)sizeof(req) || !write_all(s, (const uint8_t *)req, (size_t)n)) {
        return false;
    }
    char line[256];
    bool ok101 = false, ok_accept = false;
    int lines = 0;
    for (;;) {
        size_t li = 0;
        for (;;) {
            uint8_t c;
            if (!read_full(s, &c, 1, HDR_LINE_MS)) {
                return false;
            }
            if (c == '\n') {
                break;
            }
            if (c != '\r' && li + 1 < sizeof(line)) {
                line[li++] = (char)c;
            }
        }
        line[li] = 0;
        if (li == 0) {
            break;
        }
        if (++lines > 40) {
            return false;
        }
        if (lines == 1) {
            ok101 = strstr(line, " 101") != NULL;
        } else if (!strncasecmp(line, "Sec-WebSocket-Accept:", 21)) {
            const char *v = line + 21;
            while (*v == ' ') {
                v++;
            }
            ok_accept = strcmp(v, want) == 0;
        }
    }
    return ok101 && ok_accept;
}

/* ---------------- mensajes del servicio ---------------- */

static uint8_t tab_of(const br_net_t *s, uint8_t ch)
{
    return ch >= 1 && ch <= FLEXBR_MAX_TABS ? ch : s->active_ch;
}

static bool send_viewport(br_net_t *s)
{
    const int n = fbpBuildViewport(s->tx, sizeof(s->tx), s->seq++, s->active_ch, s->vp_want_w, s->vp_want_h,
                                   s->set.quality, s->set.profile, s->set.scalePct);
    s->vp_sent_w = s->vp_want_w;
    s->vp_sent_h = s->vp_want_h;
    s->vp_sent_ms = now(s);
    s->vp_dirty = false;
    s->stats.viewW = s->vp_want_w;
    s->stats.viewH = s->vp_want_h;
    s->stats.viewportMsgs++;
    return n > 0 && ws_send(s, 0x2, s->tx, (size_t)n);
}

static void on_frame(br_net_t *s, const FbpHeader *h, const uint8_t *p)
{
    FbpFrame f;
    if (!fbpParseFrame(p, h->len, &f)) {
        s->stats.framesBad++;
        set_info(s, "Imagen no válida del servicio");
        return;
    }
    s->nav_waiting = false;
    /* RELOJ: solo la pestaña activa y el reproductor (ambigüedad 3) */
    if (h->channel != s->active_ch && h->channel != FBP_CH_MEDIA) {
        s->stats.framesDropped++;
        s->fr_last_drawn = h->seq;
        return;
    }
    s->fr = f;
    s->fr_seq = h->seq;
    s->fr_ch = h->channel;
    s->fr_ready = true;
    s->fr_since = now(s);
    s->fr_gen++;
}

static void on_download(br_net_t *s, const uint8_t *p, uint32_t n)
{
    if (n < 6 || s->dl_n >= BR_DL_MAX) {
        return;
    }
    br_download_t *d = &s->dl[s->dl_n];
    uint32_t o = 2;
    const uint16_t l = (uint16_t)(p[0] | (p[1] << 8));
    if (o + l > n) {
        return;
    }
    const size_t c = l < sizeof(d->name) - 1 ? l : sizeof(d->name) - 1;
    memcpy(d->name, p + o, c);
    d->name[c] = 0;
    o += l;
    if (o + 4 > n) {
        return;
    }
    d->size = (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8) | ((uint32_t)p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24);
    o += 4;
    d->url[0] = 0;
    if (o + 2 <= n) {
        const uint16_t ul = (uint16_t)(p[o] | (p[o + 1] << 8));
        o += 2;
        if (o + ul <= n) {
            const size_t uc = ul < sizeof(d->url) - 1 ? ul : sizeof(d->url) - 1;
            memcpy(d->url, p + o, uc);
            d->url[uc] = 0;
        }
    }
    for (char *q = d->name; *q; q++) {
        if ((unsigned char)*q < 0x20) {
            *q = ' ';
        }
    }
    s->dl_n++;
}

static void handle(br_net_t *s, int len)
{
    FbpHeader h;
    if (len < FBP_HDR_SIZE || !fbpReadHeader(s->rx, (size_t)len, &h) || h.len != (uint32_t)(len - FBP_HDR_SIZE)) {
        s->stats.framesBad++;
        return;
    }
    const uint8_t *p = s->rx + FBP_HDR_SIZE;
    switch (h.type) {
    case FBP_S_WELCOME: {
        FbpWelcome w;
        if (!fbpParseWelcome(p, h.len, &w)) {
            set_info(s, "Respuesta del servicio no reconocida");
            set_err(s, flexBrErrorText(FBP_ERR_PROTOCOL));
            s->state = BRN_ERROR;
            return;
        }
        s->state = BRN_READY;
        set_info(s, "Sesión abierta: esperando la página");
        snprintf(s->session_id, sizeof(s->session_id), "%s", w.sessionId);
        s->stats.relayKind = (uint8_t)flexBrRelayKind(w.sessionId);
        s->stats.relayBuild = (int16_t)flexBrRelayBuild(w.sessionId);
        if (flexBrRelayIsStale(w.sessionId) && !s->relay_warned) {
            s->relay_warned = true;
            set_err(s, "Reinstala Flex Phone: su Relay es antiguo (web ampliada)");
        }
        /* RELOJ: escala y calidad desde el principio (ambigüedad 14) */
        if (!send_viewport(s)) {
            s->state = BRN_ERROR;
        }
        break;
    }
    case FBP_S_STATE: {
        FbpState st;
        if (fbpParseState(p, h.len, &st)) {
            br_tab_state_t *t = &s->tab[tab_of(s, h.channel)];
            t->flags = st.flags;
            t->progress = st.progress;
            if (st.title[0]) {
                memcpy(t->title, st.title, sizeof(t->title));
            }
            if (st.url[0]) {
                memcpy(t->url, st.url, sizeof(t->url));
            }
            t->gen++;
        }
        break;
    }
    case FBP_S_FRAME: on_frame(s, &h, p); break;
    case FBP_S_MEDIA:
        if (fbpParseMedia(p, h.len, &s->media)) {
            s->media_avail = s->media.kind != 0;
            s->media_gen++;
        }
        break;
    case FBP_S_DOWNLOAD: on_download(s, p, h.len); break;
    case FBP_S_PONG: s->stats.rttMs = now(s) - s->stats.rttMs; break;
    case FBP_S_NAVIGATED:
        if (h.len >= 2) {
            const uint16_t l = (uint16_t)(p[0] | (p[1] << 8));
            if (2u + l <= h.len) {
                char url[FLEXBR_URL_MAX];
                const size_t c = l < sizeof(url) - 1 ? l : sizeof(url) - 1;
                memcpy(url, p + 2, c);
                url[c] = 0;
                for (size_t i = 0; i < c; i++) {
                    if ((unsigned char)url[i] < 0x20) {
                        url[i] = ' ';
                    }
                }
                const uint8_t ti = tab_of(s, h.channel);
                /* una redirección a un destino prohibido se para aquí también */
                if (flexBrCheckUrl(url, &s->set) != BRB_NONE) {
                    set_err(s, "Redirección a un destino bloqueado");
                    const br_cmd_t c2 = {.type = FBP_C_STOP, .ch = ti};
                    if (s->qn < BR_CMDQ) {
                        s->q[(s->qh + s->qn++) % BR_CMDQ] = c2;
                    }
                    break;
                }
                memcpy(s->tab[ti].url, url, sizeof(url));
                s->tab[ti].nav_gen++;
                s->stats.navCount++;
            }
        }
        break;
    case FBP_S_ERROR: {
        FbpError e;
        if (fbpParseError(p, h.len, &e)) {
            set_err(s, flexBrErrorText(e.code));
            set_info(s, "%s", flexBrErrorText(e.code));
            if (e.code == FBP_ERR_AUTH || e.code == FBP_ERR_EXPIRED) {
                /* RELOJ: se cierra y se espera el backoff (ambigüedad 17) */
                s->state = BRN_ERROR;
                disconnect(s);
                s->stats.reconnects++;
                s->wait_until = now(s) + s->backoff;
                if (s->backoff < BACKOFF_MAX) {
                    s->backoff *= 2;
                }
            }
        }
        break;
    }
    default: break; /* TABS y desconocidos: se ignoran */
    }
}

static bool send_pending(br_net_t *s)
{
    for (;;) {
        br_net_lock(s);
        if (s->qn == 0) {
            br_net_unlock(s);
            return true;
        }
        const br_cmd_t c = s->q[s->qh];
        s->qh = (uint8_t)((s->qh + 1) % BR_CMDQ);
        s->qn--;
        char url[FLEXBR_URL_MAX];
        url[0] = 0;
        if (c.type == FBP_C_NAVIGATE && s->un) {
            memcpy(url, s->urlq[s->uh], sizeof(url));
            s->uh = (uint8_t)((s->uh + 1) % BR_URLQ);
            s->un--;
        }
        int n = -1;
        switch (c.type) {
        case FBP_C_NAVIGATE:
            if (!url[0]) {
                break;
            }
            n = fbpBuildNavigate(s->tx, sizeof(s->tx), s->seq++, c.ch, url);
            s->nav_sent_ms = now(s);
            s->nav_waiting = true;
            s->warned_noframes = false;
            set_info(s, "Cargando la página...");
            break;
        case FBP_C_POINTER:
            n = fbpBuildPointer(s->tx, sizeof(s->tx), s->seq++, c.ch, c.u1, (uint16_t)c.a, (uint16_t)c.b, c.u2);
            break;
        case FBP_C_SCROLL: n = fbpBuildScroll(s->tx, sizeof(s->tx), s->seq++, c.ch, c.a, c.b); break;
        case FBP_C_KEY:
            n = fbpBuildKey(s->tx, sizeof(s->tx), s->seq++, c.ch, c.u1, c.u2, c.u3, c.u1 == FBP_KEY_TEXT ? c.text : NULL);
            break;
        case FBP_C_MEDIA: n = fbpBuildMedia(s->tx, sizeof(s->tx), s->seq++, c.ch, c.u1, c.arg); break;
        default: n = fbpBuildSimple(s->tx, sizeof(s->tx), s->seq++, c.ch, c.type); break;
        }
        br_net_unlock(s);
        if (n > 0 && !ws_send(s, 0x2, s->tx, (size_t)n)) {
            return false;
        }
    }
}

/* ---------------- el bucle ---------------- */

static uint32_t fail_backoff(br_net_t *s)
{
    disconnect(s);
    s->state = BRN_ERROR;
    s->stats.reconnects++;
    const uint32_t w = s->backoff;
    if (s->backoff < BACKOFF_MAX) {
        s->backoff *= 2;
    }
    s->wait_until = now(s) + w;
    return w;
}

static uint32_t wait_ms(br_net_t *s, uint32_t ms)
{
    s->wait_until = now(s) + ms;
    return ms;
}

static uint32_t connect_phase(br_net_t *s)
{
    br_net_lock(s);
    if (!s->online) {
        s->state = BRN_NOWIFI;
        set_info(s, "Sin Wi-Fi: conéctate desde Ajustes");
        br_net_unlock(s);
        return wait_ms(s, 1000);
    }
    if (!s->url[0]) {
        s->state = BRN_OFF;
        set_info(s, "Sin servidor configurado (flex://settings)");
        br_net_unlock(s);
        return wait_ms(s, 1000);
    }
    bool tls;
    char host[FLEXBR_HOST_MAX], path[128], token[FLEXBR_TOKEN_MAX];
    uint16_t port;
    if (!flexBrParseWsUrl(s->url, &tls, host, sizeof(host), &port, path, sizeof(path))) {
        set_info(s, "La dirección del servidor no es válida");
        set_err(s, "La dirección del servidor no es válida");
        s->state = BRN_ERROR;
        br_net_unlock(s);
        return wait_ms(s, 3000);
    }
    if (!tls && !s->set.allowInsecure) {
        set_err(s, "El servidor debe usar wss:// (TLS)");
        set_info(s, "El servidor debe usar wss:// (TLS)");
        s->state = BRN_ERROR;
        br_net_unlock(s);
        return wait_ms(s, 3000);
    }
    if (s->io->mem_ok && !s->io->mem_ok(s->io->ctx)) {
        set_err(s, "Memoria insuficiente para abrir la sesión");
        s->state = BRN_ERROR;
        br_net_unlock(s);
        return wait_ms(s, 3000);
    }
    s->state = BRN_CONNECTING;
    set_info(s, "Conectando con %s:%u%s", host, (unsigned)port, tls ? " (TLS)" : "");
    memcpy(token, s->token, sizeof(token));
    br_net_unlock(s);

    disconnect(s);
    const int r = s->io->connect(s->io->ctx, host, port, tls);
    if (r != BR_IO_OK) {
        br_net_lock(s);
        if (r == BR_IO_E_TLS) { /* RELOJ: se verifica el certificado */
            set_info(s, "El certificado de %s no es válido", host);
            set_err(s, "El certificado del servidor no es válido");
        } else {
            set_info(s, "Conexión rechazada por %s:%u", host, (unsigned)port);
            set_err(s, "No se pudo conectar con el servicio");
        }
        const uint32_t w = fail_backoff(s);
        br_net_unlock(s);
        return w;
    }
    s->open = true;
    br_net_lock(s);
    s->state = BRN_HANDSHAKE;
    br_net_unlock(s);
    if (!ws_handshake(s, host, port, path)) {
        br_net_lock(s);
        set_info(s, "El servicio no aceptó el WebSocket (ruta %s)", path);
        set_err(s, "El servicio rechazó la conexión");
        const uint32_t w = fail_backoff(s);
        br_net_unlock(s);
        return w;
    }
    br_net_lock(s);
    const uint16_t hw = s->vp_want_w ? s->vp_want_w : 410, hh = s->vp_want_h ? s->vp_want_h : 400;
    const int n = fbpBuildHello(s->tx, sizeof(s->tx), 0, token, s->device, hw, hh, s->set.quality, s->set.profile,
                                s->caps);
    br_net_unlock(s);
    if (n <= 0 || !ws_send(s, 0x2, s->tx, (size_t)n)) {
        br_net_lock(s);
        disconnect(s);
        s->state = BRN_ERROR;
        br_net_unlock(s);
        return 0;
    }
    br_net_lock(s);
    s->vp_sent_w = hw;
    s->vp_sent_h = hh;
    s->vp_sent_ms = now(s);
    s->vp_dirty = false;
    s->stats.viewW = hw;
    s->stats.viewH = hh;
    set_info(s, "Autenticando (%s credencial)", token[0] ? "con" : "SIN");
    s->backoff = BACKOFF_MIN;
    s->hello_ms = s->last_ping = s->last_ack = now(s);
    br_net_unlock(s);
    return 0;
}

uint32_t br_net_step(br_net_t *s)
{
    if (s->stop) {
        return 50;
    }
    const uint32_t t = now(s);
    if ((int32_t)(t - s->wait_until) < 0) {
        return s->wait_until - t;
    }
    if (s->state != BRN_READY && s->state != BRN_HANDSHAKE) {
        return connect_phase(s);
    }
    br_net_lock(s);
    if (s->state == BRN_HANDSHAKE && now(s) - s->hello_ms > WELCOME_MS) {
        set_info(s, "Sin respuesta al saludo: revisa la credencial");
        set_err(s, "El servicio no respondió al saludo");
        const uint32_t w = fail_backoff(s);
        br_net_unlock(s);
        return w;
    }
    /* un FRAME sin consumir: no se lee el socket (la ventana TCP frena al servicio) */
    if (s->fr_ready) {
        if (now(s) - s->fr_since > FRAME_CONSUME_MS) {
            s->fr_ready = false;
            s->stats.framesDropped++;
            s->state = BRN_ERROR;
            disconnect(s);
        }
        br_net_unlock(s);
        return 20;
    }
    const bool ready = s->state == BRN_READY;
    const bool vp = ready && s->vp_dirty &&
                    (now(s) - s->vp_set_ms >= VP_SETTLE_MS || now(s) - s->vp_sent_ms >= VP_MAXDELAY_MS);
    br_net_unlock(s);

    if (ready && !send_pending(s)) {
        br_net_lock(s);
        set_info(s, "Se perdió la conexión al enviar");
        disconnect(s);
        s->state = BRN_ERROR;
        s->stats.reconnects++;
        br_net_unlock(s);
        return 0;
    }
    if (vp) {
        br_net_lock(s);
        const bool ok = send_viewport(s);
        br_net_unlock(s);
        if (!ok) {
            br_net_lock(s);
            disconnect(s);
            s->state = BRN_ERROR;
            s->stats.reconnects++;
            br_net_unlock(s);
            return 0;
        }
    }
    const int r = ws_read(s, WS_POLL_MS);
    br_net_lock(s);
    if (r == -1) {
        set_info(s, "Se perdió la conexión con el servicio");
        disconnect(s);
        s->state = BRN_ERROR;
        s->stats.reconnects++;
        br_net_unlock(s);
        return wait_ms(s, 200);
    }
    if (r > 0) {
        s->msgs_in++;
        handle(s, r);
    }
    const uint32_t tn = now(s);
    if (s->nav_waiting && !s->warned_noframes && s->nav_sent_ms && tn - s->nav_sent_ms > FIRSTFRAME_MS) {
        s->warned_noframes = true;
        set_info(s, "Sin imágenes tras %us (%u mensajes)", (unsigned)((tn - s->nav_sent_ms) / 1000),
                 (unsigned)s->msgs_in);
        set_err(s, "El servicio no envía imágenes de la página");
    }
    bool ping = false, ack = false;
    uint16_t last = s->fr_last_drawn;
    if (s->state == BRN_READY && s->open) {
        if (tn - s->last_ping > PING_MS) {
            s->last_ping = tn;
            s->stats.rttMs = tn;
            ping = true;
        }
        if (tn - s->last_ack > ACK_MS) {
            s->last_ack = tn;
            ack = true;
        }
    }
    br_net_unlock(s);
    if (ping) {
        const int n = fbpBuildSimple(s->tx, sizeof(s->tx), 0, 0, FBP_C_PING);
        if (n > 0) {
            ws_send(s, 0x2, s->tx, (size_t)n);
        }
    }
    if (ack) {
        const int n = fbpBuildAck(s->tx, sizeof(s->tx), 0, last, 0);
        if (n > 0) {
            ws_send(s, 0x2, s->tx, (size_t)n);
        }
    }
    return r > 0 ? 0 : 2;
}

void br_net_shutdown(br_net_t *s)
{
    br_net_lock(s);
    s->stop = true;
    disconnect(s);
    s->state = BRN_OFF;
    s->fr_ready = false;
    br_net_unlock(s);
}

bool br_net_frame_peek(br_net_t *s, FbpFrame *f, uint8_t *ch, uint16_t *seq)
{
    br_net_lock(s);
    const bool ok = s->fr_ready;
    if (ok) {
        *f = s->fr;
        *ch = s->fr_ch;
        *seq = s->fr_seq;
    }
    br_net_unlock(s);
    return ok;
}

void br_net_frame_done(br_net_t *s, int result)
{
    br_net_lock(s);
    if (s->fr_ready) {
        if (result == BR_FR_DRAWN) {
            s->stats.framesOk++;
        } else if (result == BR_FR_SKIPPED) {
            s->stats.framesDropped++;
        } else {
            s->stats.framesBad++;
        }
        s->fr_last_drawn = s->fr_seq;
        s->fr_ready = false;
    }
    br_net_unlock(s);
}
