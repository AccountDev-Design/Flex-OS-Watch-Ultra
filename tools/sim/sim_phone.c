/*
 * Flex Phone en la vista previa: la API de watch_phone sobre el MISMO
 * núcleo que el firmware (flexlink + phone_core), con un teléfono simulado
 * al otro lado de un transporte en memoria. Lo que se ve (código, estados,
 * notificaciones) sale del enlace v2 de verdad, no de un guion de textos.
 *
 * El teléfono simulado hace lo que hace el lado Android de v2
 * (WifiLinkServer.kt): contesta HELLO con WELCOME, guarda la sal del
 * PAIR_CODE, demuestra el código que "teclea" su usuario, verifica el
 * AUTH_OK antes de guardar la clave, responde al reto con vínculo, contesta
 * PING/PONG, acusa las órdenes y, al abrir sesión, manda CAPS y PHONE_STATE.
 *  - Canal BLE: "buscando" 400 ms, "conectando" 300 ms, abierto.
 *  - Al abrir sesión llegan tres notificaciones y lo que suena.
 *  - sim_phone_type(): el usuario teclea un código en el teléfono.
 *  - sim_phone_auto(true): lo teclea solo, bien, a los 1,5 s (monkey).
 *  - sim_phone_mute(true): la app de hoy (GattServer sin v2): no contesta.
 *  - sim_phone_drop(): se pierde la conexión y vuelve al segundo.
 *  - sim_phone_push(): llega una notificación nueva.
 */
#include <stdio.h>
#include <string.h>

#include "flex_auth.h"
#include "flex_link.h"
#include "lvgl.h"
#include "phone_core.h"
#include "sim.h"
#include "watch_phone.h"
#include "watch_time.h"

#define STEP_MS 20

/* ---------- canal en memoria ---------- */

static struct {
    FlexFrameRing w2p, p2w;
    bool want, present;
    uint8_t st;
    uint32_t next_at; /* cambio de fase del canal */
} C;

static uint8_t tr_state(FlexPhoneTransport *t)
{
    (void)t;
    return C.st;
}
static uint16_t tr_mtu(FlexPhoneTransport *t)
{
    (void)t;
    return FLNK_MAX_FRAME;
}
static bool tr_start(FlexPhoneTransport *t)
{
    (void)t;
    C.want = true;
    if (C.st == FLP_TC_DOWN) {
        C.st = FLP_TC_SEARCHING;
        C.next_at = lv_tick_get() + 400;
    }
    return true;
}
static void tr_stop(FlexPhoneTransport *t)
{
    (void)t;
    C.want = false;
    C.st = FLP_TC_DOWN;
    flexRingInit(&C.w2p);
    flexRingInit(&C.p2w);
}
static int tr_send(FlexPhoneTransport *t, const uint8_t *f, size_t n)
{
    (void)t;
    if (C.st != FLP_TC_OPEN) {
        return FLP_TR_ECLOSED;
    }
    if (flexRingFull(&C.w2p)) {
        return FLP_TR_EAGAIN;
    }
    flexRingPush(&C.w2p, f, n);
    return FLP_TR_OK;
}
static int tr_recv(FlexPhoneTransport *t, uint8_t *o, size_t cap)
{
    (void)t;
    return C.st == FLP_TC_OPEN ? (int)flexRingPop(&C.p2w, o, cap) : 0;
}
static int tr_room(FlexPhoneTransport *t)
{
    (void)t;
    return C.st == FLP_TC_OPEN ? (int)flexRingFree(&C.w2p) : 0;
}
static const char *tr_status(FlexPhoneTransport *t)
{
    (void)t;
    return "Bluetooth sin incidencias";
}
static void tr_peer(FlexPhoneTransport *t, char *out, size_t n)
{
    (void)t;
    snprintf(out, n, "%s", C.st == FLP_TC_OPEN ? "5C:E8:83:1A:7B:22" : "");
}

static FlexPhoneTransport s_tr = {
    .kind = FLP_TR_BLE, .start = tr_start, .stop = tr_stop, .state = tr_state, .mtu = tr_mtu,
    .send = tr_send, .recv = tr_recv, .status = tr_status, .peer = tr_peer, .room = tr_room,
};

static void chan_text(char *out, size_t n)
{
    const char *t = "Bluetooth apagado";
    switch (C.st) {
    case FLP_TC_SEARCHING:
        t = "Buscando el teléfono";
        break;
    case FLP_TC_OPENING:
        t = "Conectando por Bluetooth";
        break;
    case FLP_TC_OPEN:
        t = "Conectado por Bluetooth";
        break;
    default:
        break;
    }
    snprintf(out, n, "%s", t);
}

static bool rx_pending(void)
{
    return !flexRingEmpty(&C.p2w);
}

/* ---------- el teléfono simulado ---------- */

static struct {
    const char *id, *name;
    uint8_t key[32];
    bool bonded, authed, wait_pair_ok, wait_auth_ok, have_pair;
    uint8_t salt[16], pnonce[16], pkey[32], cnonce[16];
    char host[32];
    uint16_t session, packet;
    uint32_t counter;
    FlexLinkReasm reasm;
    bool mute, autotype, finding;
    uint32_t type_at, burst_at;
    uint8_t media_status;
    uint32_t next_id;
} P;

static FlexPhoneLink s_L;
static FlexPhoneModel s_M;
static phone_core_t s_core;
static uint32_t s_now, s_rnd_state = 0x2545F491u;
static bool s_started;

static uint32_t rnd(void)
{
    s_rnd_state ^= s_rnd_state << 13; /* xorshift: la vista previa es determinista */
    s_rnd_state ^= s_rnd_state >> 17;
    s_rnd_state ^= s_rnd_state << 5;
    return s_rnd_state;
}

static void ph_send(uint8_t type, uint16_t session, const uint8_t *pay, size_t n)
{
    const int k = flexLinkFragCount(n, FLNK_MAX_FRAME);
    const uint16_t packet = P.packet++;
    for (int i = 0; i < k; i++) {
        const size_t at = (size_t)i * FLNK_MAX_PAYLOAD;
        const size_t take = n - at < FLNK_MAX_PAYLOAD ? n - at : FLNK_MAX_PAYLOAD;
        FlexLinkHeader h = {.version = FLNK_VERSION,
                            .type = type,
                            .session = session,
                            .packet = packet,
                            .frag = (uint8_t)i,
                            .fragCount = (uint8_t)k,
                            .len = (uint16_t)take,
                            .counter = ++P.counter};
        uint8_t f[FLNK_MAX_FRAME];
        const int fn = flexLinkWriteFrame(f, sizeof(f), &h, take ? pay + at : NULL, take);
        if (fn > 0) {
            flexRingPush(&C.p2w, f, (size_t)fn);
        }
    }
}

static uint32_t epoch_now(void)
{
    int64_t utc = 0;
    return watch_time_now_utc(&utc) ? (uint32_t)utc : 1792000000u;
}

static void ph_notif(const char *pkg, const char *app, const char *title, const char *text, uint8_t cat,
                     bool reply, uint32_t ago_s, bool sensitive)
{
    FlexPhoneNotif n;
    memset(&n, 0, sizeof(n));
    n.id = P.next_id++;
    snprintf(n.pkg, sizeof(n.pkg), "%s", pkg);
    snprintf(n.app, sizeof(n.app), "%s", app);
    snprintf(n.title, sizeof(n.title), "%s", title);
    snprintf(n.text, sizeof(n.text), "%s", sensitive ? "" : text);
    n.hidden = sensitive; /* el teléfono no manda el cuerpo de lo sensible (hideSensitive) */
    n.sensitive = sensitive;
    n.whenMs = epoch_now() - ago_s;
    n.cat = cat;
    n.pri = FLP_PRI_HIGH;
    if (reply) {
        n.actionCount = 2;
        snprintf(n.actions[0], sizeof(n.actions[0]), "Marcar como leído");
        snprintf(n.actions[1], sizeof(n.actions[1]), "Responder");
        n.canReply = true;
        n.replyAction = 1;
    } else {
        n.actionCount = 1;
        snprintf(n.actions[0], sizeof(n.actions[0]), "Archivar");
    }
    uint8_t b[512];
    const int k = flexPhoneEncNotif(b, sizeof(b), &n);
    if (k > 0) {
        ph_send(FLNK_T_NOTIF_ADD, P.session, b, (size_t)k);
    }
}

static void ph_media(void)
{
    uint8_t b[160];
    FlexLinkWr w;
    flexLinkWrInit(&w, b, sizeof(b));
    flexLinkWrStr(&w, "Bohemian Rhapsody", 47);
    flexLinkWrStr(&w, "Queen", 47);
    flexLinkWrStr(&w, "Spotify", 31);
    flexLinkWrU8(&w, P.media_status);
    ph_send(FLNK_T_MEDIA_STATE, P.session, b, w.at);
}

static void ph_after_session(void)
{
    FlexPhoneCaps c;
    memset(&c, 0, sizeof(c));
    c.supported = FLP_CAP_NOTIF | FLP_CAP_REPLY | FLP_CAP_MEDIA | FLP_CAP_RELAY | FLP_CAP_FIND | FLP_CAP_STATE |
                  FLP_CAP_TIME | FLP_CAP_BLE;
    c.granted = FLP_CAP_NOTIF | FLP_CAP_REPLY | FLP_CAP_MEDIA | FLP_CAP_FIND | FLP_CAP_STATE | FLP_CAP_TIME;
    c.protoVer = 2;
    snprintf(c.model, sizeof(c.model), "Pixel 8");
    snprintf(c.vendor, sizeof(c.vendor), "Google");
    snprintf(c.osver, sizeof(c.osver), "Android 15");
    uint8_t b[160];
    int k = flexPhoneEncCaps(b, sizeof(b), &c);
    ph_send(FLNK_T_CAPS, P.session, b, (size_t)k);
    FlexLinkWr w;
    flexLinkWrInit(&w, b, sizeof(b));
    flexLinkWrStr(&w, P.name, 31);
    flexLinkWrU8(&w, 76);
    flexLinkWrU8(&w, 0);
    flexLinkWrU8(&w, FLP_NET_MOBILE);
    flexLinkWrU32(&w, 41250);
    flexLinkWrU32(&w, 122070);
    flexLinkWrU32(&w, 3120);
    flexLinkWrU32(&w, 7680);
    flexLinkWrU8(&w, 0);
    ph_send(FLNK_T_PHONE_STATE, P.session, b, w.at);
    P.burst_at = s_now + 300;
}

static void ph_handle(const FlexLinkHeader *h, const uint8_t *m, size_t n)
{
    FlexLinkRd r;
    flexLinkRdInit(&r, m, n);
    uint8_t b[64];
    FlexLinkWr w;
    if (P.mute) {
        return; /* GattServer.kt de hoy: todo va a onMessage y nadie contesta */
    }
    switch (h->type) {
    case FLNK_T_HELLO:
        flexLinkWrInit(&w, b, sizeof(b));
        flexLinkWrU8(&w, 2);
        flexLinkWrStr(&w, P.id, 31);
        flexLinkWrStr(&w, P.name, 31);
        ph_send(FLNK_T_WELCOME, 0, b, w.at);
        break;
    case FLNK_T_PAIR_CODE:
        flexLinkRdBytes(&r, P.salt, 16);
        flexLinkRdBytes(&r, P.pnonce, 16);
        flexLinkRdStr(&r, P.host, sizeof(P.host));
        P.have_pair = flexLinkRdOk(&r);
        if (P.have_pair && P.autotype) {
            P.type_at = s_now + 1500;
        }
        break;
    case FLNK_T_AUTH_CHALLENGE: {
        flexLinkRdBytes(&r, P.cnonce, 16);
        const uint16_t s = flexLinkRdU16(&r);
        if (!P.bonded) {
            const uint8_t e = FLNK_E_NOTPAIRED;
            ph_send(FLNK_T_ERR, 0, &e, 1);
            break;
        }
        P.session = s;
        uint8_t pr[32];
        flexAuthProof(P.key, FLXA_ROLE_PHONE, P.cnonce, s, pr);
        ph_send(FLNK_T_AUTH_RESPONSE, s, pr, sizeof(pr));
        P.wait_auth_ok = true;
        break;
    }
    case FLNK_T_AUTH_OK: {
        uint8_t pr[32];
        flexLinkRdBytes(&r, pr, 32);
        if (P.wait_pair_ok && flexAuthVerify(P.pkey, FLXA_ROLE_HOST, P.pnonce, h->session, pr)) {
            memcpy(P.key, P.pkey, 32); /* se guarda SOLO tras verificar a Flex OS */
            P.bonded = true;
        } else if (!(P.wait_auth_ok && flexAuthVerify(P.key, FLXA_ROLE_HOST, P.cnonce, h->session, pr))) {
            break;
        }
        P.wait_pair_ok = P.wait_auth_ok = false;
        P.session = h->session;
        P.authed = true;
        ph_after_session();
        break;
    }
    case FLNK_T_PING:
        ph_send(FLNK_T_PONG, h->session, NULL, 0);
        break;
    case FLNK_T_REPLY_REQ:
    case FLNK_T_ACTION_REQ:
    case FLNK_T_FIND_START:
    case FLNK_T_FIND_STOP: {
        const uint8_t ack[2] = {(uint8_t)(h->packet & 0xFF), (uint8_t)(h->packet >> 8)};
        ph_send(FLNK_T_ACK, h->session, ack, 2);
        if (h->type == FLNK_T_FIND_START || h->type == FLNK_T_FIND_STOP) {
            P.finding = h->type == FLNK_T_FIND_START;
            break;
        }
        const uint32_t id = flexLinkRdU32(&r);
        const uint8_t res[5] = {(uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24),
                                FLNK_E_NONE};
        ph_send(h->type == FLNK_T_REPLY_REQ ? FLNK_T_REPLY_RESULT : FLNK_T_ACTION_RESULT, P.session, res, 5);
        break;
    }
    case FLNK_T_MEDIA_CMD: {
        const uint8_t cmd = flexLinkRdU8(&r);
        if (cmd == FLP_MCMD_PLAY || cmd == FLP_MCMD_NEXT || cmd == FLP_MCMD_PREV) {
            P.media_status = FLP_MEDIA_PLAYING;
        } else if (cmd == FLP_MCMD_PAUSE) {
            P.media_status = FLP_MEDIA_PAUSED;
        }
        ph_media();
        break;
    }
    default:
        break;
    }
}

static void ph_pump(void)
{
    uint8_t f[FLNK_MAX_FRAME];
    size_t n;
    while ((n = flexRingPop(&C.w2p, f, sizeof(f))) > 0) {
        FlexLinkHeader h;
        const uint8_t *pay;
        size_t pn;
        if (flexLinkReadFrame(f, n, &h, &pay, &pn) == FLNK_OK &&
            flexLinkReasmFeed(&P.reasm, &h, pay, pn, 0) == FLNK_R_DONE) {
            ph_handle(&h, P.reasm.buf, P.reasm.len);
        }
    }
}

static void ph_conn(void)
{
    P.counter = 0;
    P.packet = 1;
    P.authed = P.wait_auth_ok = P.wait_pair_ok = P.have_pair = false;
    flexLinkReasmInit(&P.reasm);
}

/* El usuario del teléfono teclea el código que ve en el reloj */
void sim_phone_type(const char *code)
{
    if (!P.have_pair) {
        return;
    }
    flexAuthDeriveKey(code, P.salt, P.host, P.id, P.pkey);
    uint8_t pr[32];
    flexAuthProof(P.pkey, FLXA_ROLE_PHONE, P.pnonce, 0, pr);
    ph_send(FLNK_T_PAIR_CONFIRM, 0, pr, sizeof(pr));
    P.wait_pair_ok = true;
}

static void step(void)
{
    s_now += STEP_MS;
    /* Fases del canal BLE */
    if (C.want && C.next_at && s_now >= C.next_at) {
        if (C.st == FLP_TC_SEARCHING && C.present) {
            C.st = FLP_TC_OPENING;
            C.next_at = s_now + 300;
        } else if (C.st == FLP_TC_OPENING) {
            C.st = FLP_TC_OPEN;
            C.next_at = 0;
            flexRingInit(&C.w2p);
            flexRingInit(&C.p2w);
            ph_conn();
        } else if (C.st == FLP_TC_SEARCHING) {
            C.next_at = s_now + 400;
        }
    }
    if (P.type_at && s_now >= P.type_at) {
        P.type_at = 0;
        sim_phone_type(flexPhoneLinkCode(&s_L));
    }
    if (P.burst_at && s_now >= P.burst_at && P.authed) {
        P.burst_at = 0;
        ph_notif("com.google.android.gm", "Gmail", "Laura Martín", "Te paso el informe del viernes revisado.",
                 FLP_CAT_EMAIL, false, 3600, false);
        ph_notif("com.bbva.app", "BBVA", "Código de verificación", "Tu código es 482913", FLP_CAT_SYS, false, 600,
                 true);
        ph_notif("com.whatsapp", "WhatsApp", "Ana", "¿Quedamos a las 8 en la plaza?", FLP_CAT_MSG, true, 120,
                 false);
        P.media_status = FLP_MEDIA_PLAYING;
        ph_media();
    }
    phone_core_tick(&s_core, s_now, 8, rx_pending);
    if (C.st == FLP_TC_OPEN) {
        ph_pump();
    }
}

/* Lleva el enlace hasta "ahora" (lv_tick) en pasos de 20 ms */
static void advance(void)
{
    if (!s_started) {
        return;
    }
    const uint32_t now = lv_tick_get();
    int guard = 0;
    while ((int32_t)(now - s_now) >= STEP_MS && guard++ < 4000) {
        step();
    }
}

void sim_phone_reset(void)
{
    memset(&C, 0, sizeof(C));
    memset(&P, 0, sizeof(P));
    flexRingInit(&C.w2p);
    flexRingInit(&C.p2w);
    C.present = true;
    P.id = "phone-3f9a1c7e5b2d4a60";
    P.name = "Pixel 8";
    P.next_id = 1001;
    s_rnd_state = 0x2545F491u;
    flexPhoneLinkInit(&s_L);
    flexPhoneModelInit(&s_M);
    flexPhoneLinkSetIdentity(&s_L, "flexos-5a1c09e2d47b3f80");
    flexPhoneLinkSetModel(&s_L, "Flex OS Watch Ultra", FLP_MODEL_WATCH_ULTRA);
    flexPhoneLinkSetRandom(&s_L, rnd);
    flexPhoneLinkSetTransport(&s_L, &s_tr);
    s_now = lv_tick_get();
    phone_core_init(&s_core, &s_L, &s_M, rnd, false);
    s_core.chan_text = chan_text;
    s_started = true;
}

void sim_phone_present(bool on)
{
    C.present = on;
}

void sim_phone_auto(bool on)
{
    P.autotype = on;
}

void sim_phone_mute(bool on)
{
    P.mute = on;
}

bool sim_phone_finding(void)
{
    return P.finding;
}

void sim_phone_drop(void)
{
    if (C.st == FLP_TC_OPEN) {
        C.st = FLP_TC_SEARCHING;
        C.next_at = s_now + 1000;
    }
}

void sim_phone_push(const char *app, const char *title, const char *text, bool reply)
{
    advance();
    if (P.authed) {
        ph_notif("com.whatsapp", app, title, text, FLP_CAT_MSG, reply, 0, false);
    }
}

/* ---------- API de watch_phone ---------- */

esp_err_t watch_phone_start(void)
{
    if (!s_started) {
        sim_phone_reset();
    }
    return ESP_OK;
}

void watch_phone_get(watch_phone_status_t *out)
{
    if (!s_started) {
        sim_phone_reset();
    }
    advance();
    *out = s_core.st;
}

int watch_phone_notifs(FlexPhoneNotif *out, int max)
{
    advance();
    return phone_core_notifs(&s_core, out, max);
}

bool watch_phone_notif(uint32_t id, FlexPhoneNotif *out)
{
    advance();
    const int i = flexPhoneNotifFind(&s_M, id);
    if (i >= 0) {
        *out = s_M.notif[i];
    }
    return i >= 0;
}

bool watch_phone_notif_body(const FlexPhoneNotif *n, bool on_lock, char *out, size_t outN)
{
    return flexPhoneNotifBody(&s_M, n, on_lock, out, outN);
}

static void order(phone_msg_type_t t, uint32_t id, uint8_t arg)
{
    if (!s_started) {
        sim_phone_reset();
    }
    advance();
    phone_msg_t m = {.type = (uint8_t)t, .id = id, .arg = arg};
    phone_core_handle(&s_core, &m, s_now);
    phone_core_snapshot(&s_core, s_now);
}

void watch_phone_set_enabled(bool on)
{
    order(on ? PM_ENABLE : PM_DISABLE, 0, 0);
}
void watch_phone_pair_begin(void)
{
    order(PM_PAIR, 0, 0);
}
void watch_phone_pair_confirm(void)
{
    order(PM_CONFIRM, 0, 0);
}
void watch_phone_pair_cancel(void)
{
    order(PM_CANCEL, 0, 0);
}
void watch_phone_forget(void)
{
    order(PM_FORGET, 0, 0);
    memset(P.key, 0, sizeof(P.key)); /* olvidar en el reloj NO avisa al teléfono (§7.5) */
}
void watch_phone_dismiss(uint32_t id)
{
    order(PM_DISMISS, id, 0);
}
void watch_phone_clear_all(void)
{
    order(PM_CLEAR, 0, 0);
}

uint8_t watch_phone_reply(uint32_t id, const char *text)
{
    advance();
    const uint8_t e = phone_core_reply_check(&s_core, id, text);
    if (e == FLNK_E_NONE) {
        phone_msg_t m = {.type = PM_REPLY, .id = id};
        snprintf(m.text, sizeof(m.text), "%s", text);
        phone_core_handle(&s_core, &m, s_now);
    }
    return e;
}

bool watch_phone_action(uint32_t id, uint8_t idx)
{
    advance();
    if (!phone_core_can(&s_core, FLP_CAP_NOTIF)) {
        return false;
    }
    order(PM_ACTION, id, idx);
    return true;
}

bool watch_phone_find(bool start)
{
    advance();
    if (!phone_core_can(&s_core, FLP_CAP_FIND)) {
        return false;
    }
    order(PM_FIND, 0, start ? 1 : 0);
    return true;
}

bool watch_phone_media(uint8_t cmd)
{
    advance();
    if (cmd < FLP_MCMD_PLAY || cmd > FLP_MCMD_PREV || !phone_core_can(&s_core, FLP_CAP_MEDIA)) {
        return false;
    }
    order(PM_MEDIA, 0, cmd);
    return true;
}
