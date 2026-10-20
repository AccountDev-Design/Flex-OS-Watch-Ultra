/*
 * flexlink_watch_peer — el motor de Flex Link v2 del RELOJ (components/flexlink,
 * el mismo C que va en el firmware) como un proceso de PC controlado por
 * texto. Lo arranca la prueba de interoperabilidad de Flex Phone
 * (FlexPhone/protocol/src/test/.../WatchPeerInteropTest.kt), que hace de
 * teléfono con el PhoneLinkPeer de Kotlin: los bytes que se cruzan son los
 * de verdad, en los dos sentidos. Lo único que falta es la radio.
 *
 * Órdenes (una por línea, en stdin):
 *   start                 enciende el enlace con el canal abierto (como el BLE conectado)
 *   pair                  "Emparejar teléfono": imprime "code NNNNNN"
 *   confirm               "Confirmar aquí"
 *   frame <hex>           una trama del teléfono
 *   tick <ms>             avanza el reloj en pasos de 20 ms
 *   send <tipo> <ack> <hex>  mensaje de la app (FLNK_T_*), con o sin acuse
 *   drop / reopen         el canal se cae / vuelve (sin borrar el vínculo)
 *   state                 imprime el estado
 *   quit
 * Salida: "frame <hex>" por cada trama del reloj al teléfono, "code …",
 * "state …" y "ok". Todo en una línea y con fflush.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_phone_link.h"

static struct {
    FlexFrameRing w2p, p2w;
    uint8_t st;
} lp;

static uint8_t lp_state(FlexPhoneTransport *t) { (void)t; return lp.st; }
static uint16_t lp_mtu(FlexPhoneTransport *t) { (void)t; return FLNK_MAX_FRAME; }
static bool lp_start(FlexPhoneTransport *t) { (void)t; return true; }
static void lp_stop(FlexPhoneTransport *t) { (void)t; }
static int lp_send(FlexPhoneTransport *t, const uint8_t *f, size_t n)
{
    (void)t;
    if (lp.st != FLP_TC_OPEN) {
        return FLP_TR_ECLOSED;
    }
    if (flexRingFull(&lp.w2p)) {
        return FLP_TR_EAGAIN;
    }
    flexRingPush(&lp.w2p, f, n);
    return FLP_TR_OK;
}
static int lp_recv(FlexPhoneTransport *t, uint8_t *o, size_t cap)
{
    (void)t;
    return lp.st == FLP_TC_OPEN ? (int)flexRingPop(&lp.p2w, o, cap) : 0;
}
static int lp_room(FlexPhoneTransport *t) { (void)t; return (int)flexRingFree(&lp.w2p); }
static const char *lp_status(FlexPhoneTransport *t) { (void)t; return "interop"; }

static FlexPhoneTransport tr = {.kind = FLP_TR_BLE, .start = lp_start, .stop = lp_stop, .state = lp_state,
                                .mtu = lp_mtu, .send = lp_send, .recv = lp_recv, .status = lp_status,
                                .room = lp_room};
static FlexPhoneLink L;
static FlexPhoneModel M;
static uint32_t now_ms = 1000;
static uint32_t rnd_state = 0x12345678u;

static uint32_t rnd_fn(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static void put_hex(const uint8_t *b, size_t n)
{
    static const char *D = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        putchar(D[b[i] >> 4]);
        putchar(D[b[i] & 15]);
    }
}

static size_t get_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (s[0] && s[1] && n < cap) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) {
            break;
        }
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void drain(void)
{
    uint8_t f[FLNK_MAX_FRAME];
    size_t n;
    while ((n = flexRingPop(&lp.w2p, f, sizeof(f))) > 0) {
        fputs("frame ", stdout);
        put_hex(f, n);
        putchar('\n');
    }
}

static void print_state(void)
{
    char tok[40];
    const bool has_tok = flexPhoneLinkRelayToken(&L, tok, sizeof(tok));
    const FlexPhoneRelay *r = &M.relay;
    printf("state link=%u bonded=%d peer=%s session=%u notifs=%u lastnotif=%s relay=%u ip=%u.%u.%u.%u port=%u "
           "token=%s phone=%s caps=%d err=%s\n",
           L.state, L.bond.valid ? 1 : 0, L.bond.valid ? L.bond.peerId : "-", L.session, (unsigned)M.notifCount,
           M.notifCount ? M.notif[0].title : "-", r->state, r->ip[0], r->ip[1], r->ip[2], r->ip[3],
           (unsigned)r->port, has_tok ? tok : "-", M.phone.name[0] ? M.phone.name : "-", M.caps.valid ? 1 : 0,
           L.err[0] ? L.err : "-");
}

int main(void)
{
    flexRingInit(&lp.w2p);
    flexRingInit(&lp.p2w);
    flexPhoneLinkInit(&L);
    flexPhoneModelInit(&M);
    flexPhoneLinkSetIdentity(&L, "flexos-00a1b2c3d4e5f607");
    flexPhoneLinkSetModel(&L, "Flex OS Watch Ultra", FLP_MODEL_WATCH_ULTRA);
    flexPhoneLinkSetRandom(&L, rnd_fn);
    flexPhoneLinkSetTransport(&L, &tr);
    setvbuf(stdout, NULL, _IOLBF, 0);

    static char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "quit")) {
            break;
        } else if (!strcmp(line, "start")) {
            lp.st = FLP_TC_OPEN;
            flexPhoneLinkStart(&L);
        } else if (!strcmp(line, "pair")) {
            flexPhoneLinkBeginPairing(&L, rnd_fn, now_ms);
            printf("code %s\n", flexPhoneLinkCode(&L));
        } else if (!strcmp(line, "confirm")) {
            flexPhoneLinkConfirm(&L, now_ms);
        } else if (!strncmp(line, "frame ", 6)) {
            uint8_t f[FLNK_MAX_FRAME];
            const size_t n = get_hex(line + 6, f, sizeof(f));
            flexRingPush(&lp.p2w, f, n);
        } else if (!strncmp(line, "tick ", 5)) {
            const unsigned ms = (unsigned)atoi(line + 5);
            for (unsigned t = 0; t < ms; t += 20) {
                now_ms += 20;
                flexPhoneLinkTick(&L, &M, now_ms);
            }
        } else if (!strncmp(line, "send ", 5)) {
            unsigned type = 0, ack = 0;
            int off = 0;
            if (sscanf(line + 5, "%x %u %n", &type, &ack, &off) >= 2) {
                uint8_t b[FLP_LINK_TXBUF];
                const size_t n = get_hex(line + 5 + off, b, sizeof(b));
                printf("sent %d\n", flexPhoneLinkSend(&L, (uint8_t)type, b, n, ack != 0) ? 1 : 0);
            }
        } else if (!strcmp(line, "drop")) {
            lp.st = FLP_TC_SEARCHING;
            flexRingInit(&lp.w2p);
            flexRingInit(&lp.p2w);
        } else if (!strcmp(line, "reopen")) {
            lp.st = FLP_TC_OPEN;
        } else if (!strcmp(line, "state")) {
            print_state();
        }
        drain();
        puts("ok");
    }
    return 0;
}
