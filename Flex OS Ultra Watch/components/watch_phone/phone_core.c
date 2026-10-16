/*
 * phone_core — ver include/phone_core.h
 */
#include "phone_core.h"

#include <stdio.h>
#include <string.h>

void phone_core_init(phone_core_t *c, FlexPhoneLink *L, FlexPhoneModel *M, FlexAuthRandFn rnd, bool enabled)
{
    memset(c, 0, sizeof(*c));
    c->L = L;
    c->M = M;
    c->rnd = rnd;
    c->st.enabled = enabled;
    c->st.rtt_ms = 0xFFFF;
    phone_core_snapshot(c, 1);
}

static bool ready(const phone_core_t *c)
{
    return flexPhoneLinkReady(c->L);
}

bool phone_core_can(const phone_core_t *c, uint16_t cap)
{
    return ready(c) && c->M->caps.valid && (c->M->caps.granted & cap);
}

static void send(phone_core_t *c, uint8_t type, const uint8_t *p, int n, bool ack)
{
    if (n >= 0 && ready(c)) {
        flexPhoneLinkSend(c->L, type, p, (size_t)n, ack);
    }
}

void phone_core_handle(phone_core_t *c, const phone_msg_t *m, uint32_t now)
{
    uint8_t b[FLP_REPLY_MAX + 8];
    FlexPhoneLink *L = c->L;
    FlexPhoneModel *M = c->M;
    switch (m->type) {
    case PM_ENABLE:
        c->st.enabled = true;
        flexPhoneLinkStart(L);
        break;
    case PM_DISABLE:
        c->st.enabled = false;
        flexPhoneLinkStop(L);
        break;
    case PM_PAIR:
        c->st.enabled = true;
        flexPhoneLinkStart(L); /* no tira nada si ya estaba vivo */
        flexPhoneLinkBeginPairing(L, c->rnd, now);
        break;
    case PM_CONFIRM:
        flexPhoneLinkConfirm(L, now);
        break;
    case PM_CANCEL:
        flexPhoneLinkCancelPairing(L, now);
        break;
    case PM_FORGET:
        flexPhoneLinkForget(L);
        flexPhoneModelClear(M, true); /* sus notificaciones se van con él */
        c->local++;
        if (c->forget_transport) {
            c->forget_transport();
        }
        if (c->st.enabled) {
            flexPhoneLinkStart(L);
        }
        break;
    case PM_DISMISS:
        send(c, FLNK_T_NOTIF_REMOVE, b, flexPhoneEncNotifRemove(b, sizeof(b), m->id), false);
        if (flexPhoneNotifRemove(M, m->id)) {
            flexPhoneConvRebuild(M);
            c->local++;
        }
        break;
    case PM_CLEAR:
        flexPhoneNotifClearAll(M);
        flexPhoneConvRebuild(M);
        c->local++;
        break;
    case PM_REPLY: {
        const int i = flexPhoneNotifFind(M, m->id);
        if (i >= 0 && flexPhoneReplyCheck(M, m->id, m->text) == FLNK_E_NONE) {
            send(c, FLNK_T_REPLY_REQ, b, flexPhoneEncReply(b, sizeof(b), m->id, M->notif[i].replyAction, m->text),
                 true);
        }
        break;
    }
    case PM_ACTION:
        send(c, FLNK_T_ACTION_REQ, b, flexPhoneEncAction(b, sizeof(b), m->id, m->arg), true);
        break;
    case PM_FIND:
        send(c, m->arg ? FLNK_T_FIND_START : FLNK_T_FIND_STOP, NULL, 0, true);
        break;
    case PM_MEDIA:
        send(c, FLNK_T_MEDIA_CMD, b, flexPhoneEncMediaCmd(b, sizeof(b), m->arg), false);
        break;
    default:
        break;
    }
}

void phone_core_snapshot(phone_core_t *c, uint32_t now)
{
    watch_phone_status_t *s = &c->st;
    FlexPhoneLink *L = c->L;
    FlexPhoneModel *M = c->M;
    s->link = L->state;
    s->chan = flexPhoneTrState(L->tr);
    s->bonded = L->bond.valid;
    if (L->bond.valid && !L->bond.peerName[0] && M->phone.name[0]) {
        /* el nombre llega en WELCOME/PHONE_STATE: se guarda con el vínculo */
        snprintf(L->bond.peerName, sizeof(L->bond.peerName), "%s", M->phone.name);
    }
    const char *name = L->bond.valid && L->bond.peerName[0] ? L->bond.peerName : M->phone.name;
    snprintf(s->peer_name, sizeof(s->peer_name), "%s", name);
    snprintf(s->err, sizeof(s->err), "%s", L->err);
    if (c->chan_text) {
        c->chan_text(s->chan_text, sizeof(s->chan_text));
    } else {
        s->chan_text[0] = 0;
    }
    snprintf(s->code, sizeof(s->code), "%s", flexPhoneLinkCode(L));
    s->pair_left_ms = flexPhoneLinkPairRemainingMs(L, now);
    s->pair_user_ok = L->pair.userConfirmed;
    s->pair_peer_ok = L->pair.peerConfirmed;
    if (!flexPhoneLinkLatency(L, &s->rtt_ms)) {
        s->rtt_ms = 0xFFFF;
    }
    s->phone = M->phone;
    s->caps = M->caps;
    s->media = M->media;
    s->notif_count = M->notifCount;
    const FlexPhoneStats *st = &M->stats;
    const uint32_t sig = st->rxNotif + st->rxUpdate * 3u + st->rxRemove * 7u + st->evicted * 11u +
                         (uint32_t)M->notifCount * 65537u + c->local * 13u + M->putSeq * 17u;
    if (sig != c->sig) {
        c->sig = sig;
        s->notif_gen++;
    }
    s->put_id = M->lastPutId;
    s->put_seq = M->putSeq;
    s->replies_ok = st->repliesOk;
    s->replies_fail = st->repliesFail;
}

void phone_core_tick(phone_core_t *c, uint32_t now, int rounds, bool (*rx_pending)(void))
{
    /* Un tick recoge hasta FLP_LINK_RX_PER_TICK tramas; si el teléfono mandó
     * una ráfaga, se vacía aquí mismo (con tope). */
    for (int i = 0; i < (rounds > 0 ? rounds : 1); i++) {
        flexPhoneLinkTick(c->L, c->M, now);
        if (!rx_pending || !rx_pending()) {
            break;
        }
    }
    phone_core_snapshot(c, now);
}

/* La más reciente primero: hora del teléfono (whenMs, en segundos) y, a
 * igualdad, llegada al reloj. Una actualización (otro mensaje en el mismo
 * chat) trae hora nueva y sube, como en el teléfono. */
static bool newer(const FlexPhoneNotif *a, const FlexPhoneNotif *b)
{
    return a->whenMs != b->whenMs ? a->whenMs > b->whenMs : a->rxMs > b->rxMs;
}

int phone_core_notifs(const phone_core_t *c, FlexPhoneNotif *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }
    const FlexPhoneModel *M = c->M;
    uint8_t idx[FLP_NOTIF_MAX];
    int n = 0;
    /* Se ordenan índices (1 B) y se copia cada notificación una sola vez */
    for (int i = 0; i < FLP_NOTIF_MAX; i++) {
        if (!M->notif[i].used) {
            continue;
        }
        int j = n++;
        while (j > 0 && newer(&M->notif[i], &M->notif[idx[j - 1]])) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = (uint8_t)i;
    }
    if (n > max) {
        n = max;
    }
    for (int k = 0; k < n; k++) {
        out[k] = M->notif[idx[k]];
    }
    return n;
}

uint8_t phone_core_reply_check(const phone_core_t *c, uint32_t id, const char *text)
{
    if (!text) {
        return FLNK_E_INTERNAL;
    }
    if (!ready(c)) {
        return FLNK_E_NOTPAIRED;
    }
    if (c->M->caps.valid && !(c->M->caps.granted & FLP_CAP_REPLY)) {
        return FLNK_E_DENIED;
    }
    return flexPhoneReplyCheck(c->M, id, text);
}
