/*
 * Pruebas del núcleo Flex Link v2 portado (components/flexlink):
 *   1) vectores DORADOS copiados de las pruebas de la referencia
 *      (FlexLinkTest.kt, FlexAuthTest.kt, test_flexlink_vectors.cpp),
 *   2) las pruebas de comportamiento de esos mismos ficheros,
 *   3) vectores DERIVADOS (calculados aparte, docs/F5-CONECTIVIDAD.md),
 *   4) el apretón de manos completo contra un teléfono simulado que
 *      sigue el lado Android de v2 (WifiLinkServer.kt), por un transporte
 *      de lazo en memoria.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flex_auth.h"
#include "flex_link.h"
#include "flex_phone.h"
#include "flex_phone_link.h"
#include "flex_phone_transport.h"
#include "test_util.h"

/* ---------- ayudas ---------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static size_t unhex(const char *h, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (h[0] && h[1] && n < cap) {
        out[n++] = (uint8_t)(hexval(h[0]) << 4 | hexval(h[1]));
        h += 2;
    }
    return n;
}

/* true si los n bytes de b son exactamente el hex dado */
static bool hexeq(const uint8_t *b, size_t n, const char *hex)
{
    uint8_t want[1024];
    const size_t wn = unhex(hex, want, sizeof(want));
    if (wn != n || memcmp(b, want, n) != 0) {
        fprintf(stderr, "  obtenido: ");
        for (size_t i = 0; i < n; i++) {
            fprintf(stderr, "%02X", b[i]);
        }
        fprintf(stderr, "\n  esperado: %s\n", hex);
        return false;
    }
    return true;
}

static int frame(uint8_t *out, uint8_t type, uint16_t session, uint16_t packet, uint32_t counter, uint8_t frag,
                 uint8_t count, const void *pay, size_t n)
{
    FlexLinkHeader h = {.version = FLNK_VERSION,
                        .type = type,
                        .session = session,
                        .packet = packet,
                        .frag = frag,
                        .fragCount = count,
                        .len = (uint16_t)n,
                        .counter = counter};
    return flexLinkWriteFrame(out, FLNK_MAX_FRAME, &h, (const uint8_t *)pay, n);
}

/* ================= 1) Trama (FlexLinkTest.kt / C-VEC) ================= */

static void test_frames(void)
{
    uint8_t f[FLNK_MAX_FRAME];
    int n = frame(f, FLNK_T_PING, 0x1234, 7, 1, 0, 1, NULL, 0);
    CHECK(hexeq(f, (size_t)n, "F15802033412070000010000010000004D2A"));
    n = frame(f, FLNK_T_NOTIF_ADD, 0x1234, 7, 1, 0, 1, "hola flex", 9);
    CHECK(hexeq(f, (size_t)n, "F15802203412070000010900010000007050686F6C6120666C6578"));
    CHECK_EQ(flexLinkCrc16((const uint8_t *)"123456789", 9), 0x29B1);
    CHECK_EQ(flexLinkCrc16((const uint8_t *)"", 0), 0xFFFF);

    /* Ida y vuelta (T-FL:72-85) */
    n = frame(f, FLNK_T_NOTIF_ADD, 9, 3, 77, 0, 1, "carga", 5);
    FlexLinkHeader h;
    const uint8_t *pay;
    size_t pn;
    CHECK_EQ(flexLinkReadFrame(f, (size_t)n, &h, &pay, &pn), FLNK_OK);
    CHECK(h.type == FLNK_T_NOTIF_ADD && h.session == 9 && h.packet == 3 && h.counter == 77);
    CHECK(pn == 5 && memcmp(pay, "carga", 5) == 0);

    /* Rechazos (T-FL:90-115) */
    uint8_t base[64], m[64];
    const int bn = frame(base, FLNK_T_NOTIF_ADD, 1, 1, 1, 0, 1, "abcdef", 6);
    memcpy(m, base, (size_t)bn);
    m[19] ^= 0x01;
    CHECK_EQ(flexLinkReadFrame(m, (size_t)bn, &h, &pay, &pn), FLNK_ERR_CRC);
    memcpy(m, base, (size_t)bn);
    m[6] ^= 0x08;
    CHECK_EQ(flexLinkReadFrame(m, (size_t)bn, &h, &pay, &pn), FLNK_ERR_CRC);
    memcpy(m, base, (size_t)bn);
    m[0] = 0;
    CHECK_EQ(flexLinkReadFrame(m, (size_t)bn, &h, &pay, &pn), FLNK_ERR_MAGIC);
    CHECK_EQ(flexLinkReadFrame(base, 4, &h, &pay, &pn), FLNK_ERR_SHORT);
    memcpy(m, base, (size_t)bn);
    m[10] = 0xFF;
    CHECK_EQ(flexLinkReadFrame(m, (size_t)bn, &h, &pay, &pn), FLNK_ERR_LEN);
    memcpy(m, base, (size_t)bn);
    m[2] = 99; /* cae por CRC, no por versión (§7.14) */
    CHECK_EQ(flexLinkReadFrame(m, (size_t)bn, &h, &pay, &pn), FLNK_ERR_CRC);
    for (int k = 0; k < bn; k++) { /* ningún prefijo revienta (ASan) */
        CHECK(flexLinkReadFrame(base, (size_t)k, &h, &pay, &pn) != FLNK_OK);
    }
    /* Versión futura con su CRC bien calculado: se rechaza por versión */
    FlexLinkHeader hv = {.version = 3, .type = FLNK_T_PING, .fragCount = 1};
    n = flexLinkWriteFrame(f, sizeof(f), &hv, NULL, 0);
    CHECK_EQ(flexLinkReadFrame(f, (size_t)n, &h, &pay, &pn), FLNK_ERR_VERSION);
}

/* Trocea como el emisor Kotlin: fragmentos llenos de 226 B salvo el último */
static int fragment(uint8_t out[][FLNK_MAX_FRAME], int *lens, uint8_t type, uint16_t session, uint16_t packet,
                    uint32_t counter0, const uint8_t *msg, size_t n)
{
    const int k = flexLinkFragCount(n, FLNK_MAX_FRAME);
    for (int i = 0; i < k; i++) {
        const size_t at = (size_t)i * FLNK_MAX_PAYLOAD;
        const size_t take = n - at < FLNK_MAX_PAYLOAD ? n - at : FLNK_MAX_PAYLOAD;
        lens[i] = frame(out[i], type, session, packet, counter0 + (uint32_t)i, (uint8_t)i, (uint8_t)k, msg + at, take);
    }
    return k;
}

static void test_reasm(void)
{
    static uint8_t msg[700], fr[16][FLNK_MAX_FRAME];
    int lens[16];
    for (int i = 0; i < 700; i++) {
        msg[i] = (uint8_t)((i * 7 + 3) & 0xFF);
    }
    int k = fragment(fr, lens, FLNK_T_NOTIF_ADD, 1, 5, 100, msg, 700);
    CHECK_EQ(k, 4);
    CHECK(lens[0] == 244 && lens[1] == 244 && lens[2] == 244 && lens[3] == 40);
    /* Derivados (§4.5): CRC de cada trama y cabeceras primera/última */
    CHECK(fr[0][16] == 0x65 && fr[0][17] == 0x92 && fr[1][16] == 0x41 && fr[1][17] == 0xC1);
    CHECK(fr[2][16] == 0x2D && fr[2][17] == 0x58 && fr[3][16] == 0x16 && fr[3][17] == 0xA1);
    CHECK(hexeq(fr[0], 18, "F1580220010005000004E200640000006592"));
    CHECK(hexeq(fr[3], 18, "F158022001000500030416006700000016A1"));

    FlexLinkReasm r;
    flexLinkReasmInit(&r);
    FlexLinkHeader h;
    const uint8_t *pay;
    size_t pn;
    int rr = 0;
    for (int i = 0; i < k; i++) {
        CHECK_EQ(flexLinkReadFrame(fr[i], (size_t)lens[i], &h, &pay, &pn), FLNK_OK);
        rr = flexLinkReasmFeed(&r, &h, pay, pn, 1000);
    }
    CHECK_EQ(rr, FLNK_R_DONE);
    CHECK(r.len == 700 && memcmp(r.buf, msg, 700) == 0);

    /* Repetidos (T-FL:138-162) */
    for (int i = 0; i < 500; i++) {
        msg[i] = (uint8_t)(i & 0xFF);
    }
    k = fragment(fr, lens, FLNK_T_NOTIF_ADD, 1, 6, 1, msg, 500);
    flexLinkReasmInit(&r);
    flexLinkReadFrame(fr[k - 1], (size_t)lens[k - 1], &h, &pay, &pn);
    CHECK_EQ(flexLinkReasmFeed(&r, &h, pay, pn, 1000), FLNK_R_DROP);
    flexLinkReadFrame(fr[0], (size_t)lens[0], &h, &pay, &pn);
    CHECK_EQ(flexLinkReasmFeed(&r, &h, pay, pn, 1000), FLNK_R_NEED_MORE);
    CHECK_EQ(flexLinkReasmFeed(&r, &h, pay, pn, 1000), FLNK_R_DROP);
    CHECK_EQ(r.nDup, 1);

    /* Caducidad (T-FL:164-173) */
    CHECK(!flexLinkReasmExpire(&r, 2000, 5000));
    CHECK(flexLinkReasmExpire(&r, 9000, 5000));

    /* Límites (T-FL:175-180) */
    CHECK_EQ(flexLinkFragCount(2049, 244), 0);
    CHECK_EQ(flexLinkFragCount(100, 25), 0);
    CHECK_EQ(flexLinkFragCount(0, 244), 1);

    /* Fragmento intermedio corto: se tira el mensaje entero */
    flexLinkReasmInit(&r);
    uint8_t a[FLNK_MAX_FRAME];
    int an = frame(a, FLNK_T_NOTIF_ADD, 1, 9, 1, 0, 2, msg, 100);
    flexLinkReadFrame(a, (size_t)an, &h, &pay, &pn);
    CHECK_EQ(flexLinkReasmFeed(&r, &h, pay, pn, 0), FLNK_R_DROP);
    CHECK(!r.active);
}

static void test_antireplay_retry(void)
{
    FlexLinkAntiReplay a;
    flexLinkAntiReplayInit(&a);
    const uint32_t seq[] = {100, 100, 101, 103, 102, 102, 1, 100000, 103};
    const bool want[] = {true, false, true, true, true, false, false, true, false};
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        CHECK(flexLinkAntiReplayCheck(&a, seq[i]) == want[i]);
    }
    uint32_t prev = 0;
    for (uint8_t i = 0; i < 5; i++) {
        const uint32_t d = flexLinkRetryDelayMs(i);
        CHECK(d > 0 && d >= prev && d <= 30000);
        prev = d;
    }
    CHECK_EQ(flexLinkRetryDelayMs(5), 0);
    CHECK_EQ(flexLinkRetryDelayMs(200), 0);
}

static void test_fields_utf8(void)
{
    CHECK_EQ(flexLinkUtf8Trunc("café", 4), 3);
    CHECK_EQ(flexLinkUtf8Trunc("café", 5), 5);
    CHECK_EQ(flexLinkUtf8Trunc("📱", 3), 0);
    CHECK_EQ(flexLinkUtf8Trunc("📱", 4), 4);
    CHECK_EQ(flexLinkUtf8Trunc("ab\xC3", 8), 2); /* secuencia cortada: se corta ahí */
    uint8_t b[64];
    FlexLinkWr w;
    flexLinkWrInit(&w, b, sizeof(b));
    flexLinkWrStr(&w, "café", 4);
    flexLinkWrU8(&w, 0xAB);
    flexLinkWrU16(&w, 0x1234);
    flexLinkWrU32(&w, 0xDEADBEEF);
    flexLinkWrStr(&w, "WhatsApp", 32);
    CHECK(flexLinkWrOk(&w));
    FlexLinkRd r;
    flexLinkRdInit(&r, b, w.at);
    char s[32];
    flexLinkRdStr(&r, s, sizeof(s));
    CHECK(strcmp(s, "caf") == 0);
    CHECK_EQ(flexLinkRdU8(&r), 0xAB);
    CHECK_EQ(flexLinkRdU16(&r), 0x1234);
    CHECK(flexLinkRdU32(&r) == 0xDEADBEEFu);
    flexLinkRdStr(&r, s, sizeof(s));
    CHECK(strcmp(s, "WhatsApp") == 0 && flexLinkRdOk(&r));
    const uint8_t bad[] = {0xFF, 0x61, 0x62};
    flexLinkRdInit(&r, bad, sizeof(bad));
    flexLinkRdStr(&r, s, sizeof(s));
    CHECK(s[0] == 0 && !flexLinkRdOk(&r));
    /* El escritor no se sale del búfer */
    flexLinkWrInit(&w, b, 3);
    flexLinkWrU32(&w, 1);
    CHECK(!flexLinkWrOk(&w));
}

/* ================= 2) FlexAuth (FlexAuthTest.kt / C-FA) ================= */

static void sha_hex(const void *d, size_t n, const char *hex)
{
    uint8_t o[32];
    flexSha256(d, n, o);
    CHECK(hexeq(o, 32, hex));
}

static void hmac_hex(const uint8_t *k, size_t kn, const char *msg, const char *hex)
{
    uint8_t o[32];
    flexHmacSha256(k, kn, msg, strlen(msg), o);
    CHECK(hexeq(o, 32, hex));
}

static void test_auth(void)
{
    sha_hex("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha_hex("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *s448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha_hex(s448, strlen(s448), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {
        static uint8_t aa[1000];
        memset(aa, 'a', sizeof(aa));
        FlexSha256 c;
        uint8_t o[32];
        flexSha256Init(&c);
        for (int i = 0; i < 1000; i++) {
            flexSha256Update(&c, aa, sizeof(aa));
        }
        flexSha256Final(&c, o);
        CHECK(hexeq(o, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    }
    uint8_t k[131];
    memset(k, 0x0b, 20);
    hmac_hex(k, 20, "Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    hmac_hex((const uint8_t *)"Jefe", 4, "what do ya want for nothing?",
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(k, 0xaa, 131);
    hmac_hex(k, 131, "Test Using Larger Than Block-Size Key - Hash Key First",
             "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    uint8_t salt[16], nonce[16], key[32], p[32], q[32];
    for (int i = 0; i < 16; i++) {
        salt[i] = (uint8_t)i;
        nonce[i] = (uint8_t)(0xA0 + i);
    }
    flexAuthDeriveKey("012345", salt, "flexos-1", "phone-1", key);
    CHECK(hexeq(key, 32, "66840f7c66ababb9f042d87c18e1f6b28d1bf08db58316823bb99edb38954176"));
    flexAuthProof(key, FLXA_ROLE_PHONE, nonce, 0x1234, p);
    CHECK(hexeq(p, 32, "a259af02d131f300fd204569b46a9d16ea7754c2d88e62768566ce72f169786a"));
    flexAuthProof(key, FLXA_ROLE_HOST, nonce, 0x1234, p);
    CHECK(hexeq(p, 32, "df2848a174c2210e9ae82e2bb5b0888aa50fbf5cc32476612844ad3359385377"));
    /* Derivado (§4.5): prueba de emparejamiento, sesión 0 */
    flexAuthProof(key, FLXA_ROLE_PHONE, nonce, 0, p);
    CHECK(hexeq(p, 32, "b6624a6c5b772ca17ca3501ccc3f31a0eae8b6090b93f1c3e2021c07a226ee04"));

    /* Propiedades (T-FA:84-127) */
    flexAuthDeriveKey("012346", salt, "flexos-1", "phone-1", q);
    CHECK(memcmp(key, q, 32) != 0);
    salt[0] ^= 0xFF;
    flexAuthDeriveKey("012345", salt, "flexos-1", "phone-1", q);
    CHECK(memcmp(key, q, 32) != 0);
    salt[0] ^= 0xFF;
    flexAuthDeriveKey("012345", salt, "ab", "c", p);
    flexAuthDeriveKey("012345", salt, "a", "bc", q);
    CHECK(memcmp(p, q, 32) != 0);
    flexAuthProof(key, FLXA_ROLE_PHONE, nonce, 0x1234, p);
    CHECK(flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0x1234, p));
    CHECK(!flexAuthVerify(key, FLXA_ROLE_HOST, nonce, 0x1234, p));
    CHECK(!flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0x1235, p));
    nonce[0] ^= 1;
    CHECK(!flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0x1234, p));

    const uint8_t e1[] = {1, 2, 3, 4}, e2[] = {9, 2, 3, 4}, e3[] = {1, 2, 3, 9};
    CHECK(flexAuthEqual(e1, e1, 4) && !flexAuthEqual(e1, e2, 4) && !flexAuthEqual(e1, e3, 4));

    char code[7];
    flexAuthFormatCode(0, code);
    CHECK(strcmp(code, "000000") == 0);
    flexAuthFormatCode(42, code);
    CHECK(strcmp(code, "000042") == 0);
    flexAuthFormatCode(1234567, code);
    CHECK(strcmp(code, "234567") == 0);
    flexAuthFormatCode(0xFFFFFFFFu, code);
    CHECK(strlen(code) == 6 && strcmp(code, "967295") == 0);
    uint8_t rb[7];
    memset(rb, 0x77, sizeof(rb));
    flexAuthRandomBytes(NULL, rb, sizeof(rb));
    CHECK(rb[0] == 0 && rb[6] == 0);

    /* Derivados de PairingSessionTest (§4.5) */
    uint8_t s1[16], n40[16];
    for (int i = 0; i < 16; i++) {
        s1[i] = (uint8_t)(1 + i);
        n40[i] = (uint8_t)(40 + i);
    }
    const char *codes[] = {"123456", "012345", "098765"};
    const char *keys[] = {"e506d675f4eff9829d3be9cbd5678cc5a8fe363fb2f9de0f16d51a582c908b44",
                          "be551752a3c61f57f173c7fcf55ddae0db6eeeac0f25d184dfaaf8f1e2e3f9e0",
                          "485f2604d8a46824a3166f8223c03176ba9787063b06323dcebc1b247ce1c3cd"};
    const char *proofs[] = {"87fb2fa93beff8527056ecc65385a1f6ae23cb652d3fb0d49b25fbb95a711f07",
                            "55aca867544561eb5f5c0425486ab94248ebc651bc594e57617ff9a1392c84e3",
                            "891ee6d2cd7b24bd27ad69102a3d4920db62befcb36b97950be48d85bf60737c"};
    for (int i = 0; i < 3; i++) {
        flexAuthDeriveKey(codes[i], s1, "flexos-0123456789abcdef", "phone-fedcba9876543210", key);
        CHECK(hexeq(key, 32, keys[i]));
        flexAuthProof(key, FLXA_ROLE_PHONE, n40, 0, p);
        CHECK(hexeq(p, 32, proofs[i]));
    }
}

/* ================= 3) Códecs de Flex Phone ================= */

static void test_codecs(void)
{
    FlexPhoneNotif n;
    memset(&n, 0, sizeof(n));
    n.id = 42;
    strcpy(n.pkg, "com.whatsapp");
    strcpy(n.app, "WhatsApp");
    strcpy(n.title, "Ana");
    strcpy(n.text, "un mensaje");
    n.whenMs = 123456;
    n.cat = FLP_CAT_MSG;
    n.pri = FLP_PRI_HIGH;
    n.canReply = true;
    n.replyAction = 1;
    n.actionCount = 2;
    strcpy(n.actions[0], "Marcar leido");
    strcpy(n.actions[1], "Responder");
    uint8_t b[512];
    int k = flexPhoneEncNotif(b, sizeof(b), &n);
    CHECK(hexeq(b, (size_t)k,
                "2A0000000C636F6D2E776861747361707008576861747341707003416E610A756E206D656E73616A6540E2010001030401020C4D"
                "6172636172206C6569646F09526573706F6E646572"));
    FlexPhoneNotif d;
    CHECK(flexPhoneDecNotif(b, (size_t)k, &d));
    CHECK(d.id == 42 && strcmp(d.title, "Ana") == 0 && d.canReply && d.actionCount == 2 && d.used);
    /* Saneado: acción de respuesta inexistente, categoría/prioridad fuera de rango */
    n.replyAction = 5;
    n.cat = 99;
    n.pri = 9;
    k = flexPhoneEncNotif(b, sizeof(b), &n);
    CHECK(flexPhoneDecNotif(b, (size_t)k, &d));
    CHECK(!d.canReply && d.cat == FLP_CAT_OTHER && d.pri == FLP_PRI_DEFAULT);
    /* Declara más acciones de las que trae: se rechaza */
    b[k - 1 - 9 - 1 - 12 - 1] = 4; /* nActions = 4 con solo 2 etiquetas */
    CHECK(!flexPhoneDecNotif(b, (size_t)k, &d));

    /* CAPS (C-VEC:110-121) */
    FlexPhoneCaps c;
    memset(&c, 0, sizeof(c));
    c.supported = FLP_CAP_NOTIF | FLP_CAP_REPLY | FLP_CAP_MEDIA | FLP_CAP_RELAY | FLP_CAP_STATE | FLP_CAP_TIME;
    c.granted = FLP_CAP_NOTIF | FLP_CAP_STATE | FLP_CAP_TIME;
    c.protoVer = 2;
    strcpy(c.model, "SM-A556B");
    strcpy(c.vendor, "samsung");
    strcpy(c.osver, "Android 14");
    k = flexPhoneEncCaps(b, sizeof(b), &c);
    CHECK(hexeq(b, (size_t)k, "6F0061000208534D2D41353536420773616D73756E670A416E64726F6964203134"));
    FlexPhoneCaps cd;
    CHECK(flexPhoneDecCaps(b, (size_t)k, &cd) && cd.valid && cd.granted == 0x61 && strcmp(cd.osver, "Android 14") == 0);
    b[2] = 0xFF; /* concede lo que no soporta: se recorta */
    CHECK(flexPhoneDecCaps(b, (size_t)k, &cd) && cd.granted == (0x00FF & 0x6F));

    /* Derivados (flexphone-messages §6.2) */
    k = flexPhoneEncReply(b, sizeof(b), 42, 1, "vale");
    CHECK(hexeq(b, (size_t)k, "2A000000010476616C65"));
    k = flexPhoneEncAction(b, sizeof(b), 42, 1);
    CHECK(hexeq(b, (size_t)k, "2A00000001"));
    k = flexPhoneEncNotifRemove(b, sizeof(b), 42);
    CHECK(hexeq(b, (size_t)k, "2A000000"));
    k = flexPhoneEncMediaCmd(b, sizeof(b), FLP_MCMD_PLAY);
    CHECK(hexeq(b, (size_t)k, "01"));
    CHECK(flexPhoneEncMediaCmd(b, sizeof(b), 0) < 0 && flexPhoneEncMediaCmd(b, sizeof(b), 5) < 0);
    uint8_t f[FLNK_MAX_FRAME];
    int fn = frame(f, FLNK_T_REPLY_REQ, 0x1234, 7, 1, 0, 1, "\x2A\x00\x00\x00\x01\x04vale", 10);
    CHECK(hexeq(f, (size_t)fn, "F15802303412070000010A0001000000D4D82A000000010476616C65"));
    fn = frame(f, FLNK_T_FIND_START, 0x1234, 7, 1, 0, 1, NULL, 0);
    CHECK(hexeq(f, (size_t)fn, "F1580242341207000001000001000000FE35"));
    fn = frame(f, FLNK_T_PHONE_STATE, 0x1234, 7, 1, 0, 1, NULL, 0);
    CHECK(hexeq(f, (size_t)fn, "F158024034120700000100000100000038BF"));

    FlexPhoneState st;
    k = (int)unhex("07506978656C203857010200C8000000000400B80B00000020000000", b, sizeof(b));
    CHECK(flexPhoneDecPhoneState(b, (size_t)k, &st));
    CHECK(strcmp(st.name, "Pixel 8") == 0 && st.battery == 87 && st.charging && st.net == FLP_NET_WIFI);
    CHECK(st.storageFreeMb == 51200 && st.storageTotalMb == 262144 && st.ramFreeMb == 3000 && st.ramTotalMb == 8192);
    CHECK(flexPhoneDecPhoneState(b, 11, &st) && st.storageTotalMb == 0); /* sin bloque opcional */
    b[8] = 200;                                                          /* batería imposible -> desconocida */
    CHECK(flexPhoneDecPhoneState(b, (size_t)k, &st) && st.battery == 255);

    FlexPhoneMedia md;
    k = (int)unhex("04536F6E67064172746973740753706F7469667901", b, sizeof(b));
    CHECK(flexPhoneDecMedia(b, (size_t)k, &md) && md.valid && md.status == FLP_MEDIA_PLAYING);
    CHECK(strcmp(md.artist, "Artist") == 0 && strcmp(md.app, "Spotify") == 0);

    /* Comprobación de respuesta antes de enviar */
    static FlexPhoneModel M;
    flexPhoneModelInit(&M);
    n.replyAction = 1;
    n.cat = FLP_CAT_MSG;
    n.pri = FLP_PRI_HIGH;
    CHECK(flexPhoneNotifPut(&M, &n, 10) >= 0);
    CHECK_EQ(flexPhoneReplyCheck(&M, 42, "vale"), FLNK_E_NONE);
    CHECK_EQ(flexPhoneReplyCheck(&M, 43, "vale"), FLNK_E_GONE);
    CHECK_EQ(flexPhoneReplyCheck(&M, 42, ""), FLNK_E_INTERNAL);
    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    CHECK_EQ(flexPhoneReplyCheck(&M, 42, big), FLNK_E_TOOBIG);

    /* Caída del enlace: el historial se queda si keepHistory (RELOJ) */
    M.phone.valid = true;
    M.caps.valid = true;
    flexPhoneModelLinkLost(&M);
    CHECK(M.notifCount == 1 && !M.phone.valid && !M.caps.valid && M.phone.battery == 255);
    M.priv.keepHistory = false;
    flexPhoneModelLinkLost(&M);
    CHECK_EQ(M.notifCount, 0);
}

/* ================= 4) Enlace contra un teléfono simulado ================= */

typedef struct {
    FlexFrameRing w2p, p2w; /* reloj -> teléfono, teléfono -> reloj */
    uint8_t st;
} loop_t;

static uint8_t lp_state(FlexPhoneTransport *t)
{
    return ((loop_t *)t->ctx)->st;
}
static uint16_t lp_mtu(FlexPhoneTransport *t)
{
    (void)t;
    return FLNK_MAX_FRAME;
}
static bool lp_start(FlexPhoneTransport *t)
{
    (void)t;
    return true;
}
static void lp_stop(FlexPhoneTransport *t)
{
    (void)t;
}
static int lp_send(FlexPhoneTransport *t, const uint8_t *f, size_t n)
{
    loop_t *l = t->ctx;
    if (l->st != FLP_TC_OPEN) {
        return FLP_TR_ECLOSED;
    }
    if (flexRingFull(&l->w2p)) {
        return FLP_TR_EAGAIN;
    }
    flexRingPush(&l->w2p, f, n);
    return FLP_TR_OK;
}
static int lp_recv(FlexPhoneTransport *t, uint8_t *o, size_t cap)
{
    loop_t *l = t->ctx;
    return l->st == FLP_TC_OPEN ? (int)flexRingPop(&l->p2w, o, cap) : 0;
}
static int lp_room(FlexPhoneTransport *t)
{
    return (int)flexRingFree(&((loop_t *)t->ctx)->w2p);
}
static const char *lp_status(FlexPhoneTransport *t)
{
    (void)t;
    return "lazo";
}

typedef struct {
    loop_t *lp;
    const char *id, *name;
    uint8_t key[32];
    bool bonded, authed;
    uint8_t salt[16], pnonce[16], pkey[32], cnonce[16];
    char host[32];
    bool have_pair, wait_pair_ok, wait_auth_ok, bad_host_proof;
    uint16_t session;
    uint32_t counter;
    uint16_t packet;
    FlexLinkReasm reasm;
    bool mute, no_pong;
    int n_hello, n_err, last_err, n_ping, n_reply, n_remove, n_find, n_frames;
    uint32_t last_id;
    uint8_t last[FLNK_MAX_FRAME]; /* última trama recibida, cruda */
    int last_n;
} phone_t;

static void ph_send(phone_t *p, uint8_t type, uint16_t session, const uint8_t *pay, size_t n)
{
    static uint8_t fr[16][FLNK_MAX_FRAME];
    int lens[16];
    const int k = fragment(fr, lens, type, session, p->packet++, p->counter + 1, pay, n);
    p->counter += (uint32_t)k;
    for (int i = 0; i < k; i++) {
        flexRingPush(&p->lp->p2w, fr[i], (size_t)lens[i]);
    }
}

static void ph_conn(phone_t *p) /* conexión nueva: el teléfono cuenta desde 1 */
{
    p->counter = 0;
    p->packet = 1;
    p->authed = false;
    p->wait_auth_ok = p->wait_pair_ok = false;
    p->have_pair = false;
    flexLinkReasmInit(&p->reasm);
}

static void ph_after_session(phone_t *p)
{
    uint8_t b[128];
    size_t k = unhex("6F0061000208534D2D41353536420773616D73756E670A416E64726F6964203134", b, sizeof(b));
    ph_send(p, FLNK_T_CAPS, p->session, b, k);
    k = unhex("07506978656C203857010200C8000000000400B80B00000020000000", b, sizeof(b));
    ph_send(p, FLNK_T_PHONE_STATE, p->session, b, k);
}

static void ph_type(phone_t *p, const char *code)
{
    if (!p->have_pair) {
        return;
    }
    flexAuthDeriveKey(code, p->salt, p->host, p->id, p->pkey);
    uint8_t pr[32];
    flexAuthProof(p->pkey, FLXA_ROLE_PHONE, p->pnonce, 0, pr);
    ph_send(p, FLNK_T_PAIR_CONFIRM, 0, pr, sizeof(pr));
    p->wait_pair_ok = true;
}

static void ph_handle(phone_t *p, const FlexLinkHeader *h, const uint8_t *m, size_t n)
{
    FlexLinkRd r;
    flexLinkRdInit(&r, m, n);
    uint8_t b[64];
    FlexLinkWr w;
    switch (h->type) {
    case FLNK_T_HELLO:
        p->n_hello++;
        if (p->mute) {
            return; /* GattServer.kt de hoy: el HELLO va a onMessage y nadie contesta */
        }
        flexLinkWrInit(&w, b, sizeof(b));
        flexLinkWrU8(&w, 2);
        flexLinkWrStr(&w, p->id, 31);
        flexLinkWrStr(&w, p->name, 31);
        ph_send(p, FLNK_T_WELCOME, 0, b, w.at);
        break;
    case FLNK_T_PAIR_CODE:
        flexLinkRdBytes(&r, p->salt, 16);
        flexLinkRdBytes(&r, p->pnonce, 16);
        flexLinkRdStr(&r, p->host, sizeof(p->host));
        p->have_pair = flexLinkRdOk(&r);
        break;
    case FLNK_T_AUTH_CHALLENGE: {
        flexLinkRdBytes(&r, p->cnonce, 16);
        const uint16_t s = flexLinkRdU16(&r);
        if (!p->bonded) {
            const uint8_t e = FLNK_E_NOTPAIRED;
            ph_send(p, FLNK_T_ERR, 0, &e, 1);
            return;
        }
        p->session = s;
        uint8_t pr[32];
        flexAuthProof(p->key, FLXA_ROLE_PHONE, p->cnonce, s, pr);
        ph_send(p, FLNK_T_AUTH_RESPONSE, s, pr, sizeof(pr));
        p->wait_auth_ok = true;
        break;
    }
    case FLNK_T_AUTH_OK: {
        uint8_t pr[32];
        flexLinkRdBytes(&r, pr, 32);
        if (p->wait_pair_ok) {
            p->wait_pair_ok = false;
            if (!flexAuthVerify(p->pkey, FLXA_ROLE_HOST, p->pnonce, h->session, pr)) {
                p->bad_host_proof = true;
                return;
            }
            memcpy(p->key, p->pkey, 32); /* se guarda SOLO tras verificar AUTH_OK */
            p->bonded = true;
        } else if (p->wait_auth_ok) {
            p->wait_auth_ok = false;
            if (!flexAuthVerify(p->key, FLXA_ROLE_HOST, p->cnonce, h->session, pr)) {
                p->bad_host_proof = true;
                return;
            }
        } else {
            return;
        }
        p->session = h->session;
        p->authed = true;
        ph_after_session(p);
        break;
    }
    case FLNK_T_PING:
        p->n_ping++;
        if (!p->no_pong) {
            ph_send(p, FLNK_T_PONG, h->session, NULL, 0);
        }
        break;
    case FLNK_T_ERR:
        p->n_err++;
        p->last_err = flexLinkRdU8(&r);
        break;
    case FLNK_T_REPLY_REQ:
    case FLNK_T_FIND_START: {
        const uint8_t ack[2] = {(uint8_t)(h->packet & 0xFF), (uint8_t)(h->packet >> 8)};
        ph_send(p, FLNK_T_ACK, h->session, ack, 2);
        if (h->type == FLNK_T_FIND_START) {
            p->n_find++;
            break;
        }
        p->n_reply++;
        p->last_id = flexLinkRdU32(&r);
        const uint8_t res[5] = {(uint8_t)p->last_id, (uint8_t)(p->last_id >> 8), (uint8_t)(p->last_id >> 16),
                                (uint8_t)(p->last_id >> 24), FLNK_E_NONE};
        ph_send(p, FLNK_T_REPLY_RESULT, p->session, res, 5);
        break;
    }
    case FLNK_T_NOTIF_REMOVE:
        p->n_remove++;
        p->last_id = flexLinkRdU32(&r);
        break;
    default:
        break;
    }
}

static void ph_pump(phone_t *p)
{
    uint8_t f[FLNK_MAX_FRAME];
    size_t n;
    while ((n = flexRingPop(&p->lp->w2p, f, sizeof(f))) > 0) {
        memcpy(p->last, f, n);
        p->last_n = (int)n;
        p->n_frames++;
        FlexLinkHeader h;
        const uint8_t *pay;
        size_t pn;
        if (flexLinkReadFrame(f, n, &h, &pay, &pn) != FLNK_OK) {
            continue;
        }
        if (flexLinkReasmFeed(&p->reasm, &h, pay, pn, 0) == FLNK_R_DONE) {
            ph_handle(p, &h, p->reasm.buf, p->reasm.len);
        }
    }
}

/* Azar determinista: código "012345", sal 00..0F, reto A0..AF, y luego retos de sesión */
static uint32_t g_rnd[16];
static int g_rnd_at;
static uint32_t rnd_fn(void)
{
    return g_rnd_at < 16 ? g_rnd[g_rnd_at++] : 0x9E3779B9u * (uint32_t)(g_rnd_at++);
}
static void rnd_reset(void)
{
    g_rnd_at = 0;
    g_rnd[0] = 12345;
    for (int i = 0; i < 4; i++) {
        g_rnd[1 + i] = (uint32_t)(4 * i) | (uint32_t)(4 * i + 1) << 8 | (uint32_t)(4 * i + 2) << 16 |
                       (uint32_t)(4 * i + 3) << 24;
        g_rnd[5 + i] = (uint32_t)(0xA0 + 4 * i) | (uint32_t)(0xA1 + 4 * i) << 8 | (uint32_t)(0xA2 + 4 * i) << 16 |
                       (uint32_t)(0xA3 + 4 * i) << 24;
    }
    for (int i = 9; i < 16; i++) {
        g_rnd[i] = 0x01020304u * (uint32_t)i;
    }
}

typedef struct {
    loop_t lp;
    FlexPhoneTransport tr;
    FlexPhoneLink L;
    FlexPhoneModel M;
    phone_t ph;
    uint32_t now;
} rig_t;

static void rig_init(rig_t *g)
{
    memset(g, 0, sizeof(*g));
    flexRingInit(&g->lp.w2p);
    flexRingInit(&g->lp.p2w);
    g->tr = (FlexPhoneTransport){.kind = FLP_TR_LOOP,
                                 .start = lp_start,
                                 .stop = lp_stop,
                                 .state = lp_state,
                                 .mtu = lp_mtu,
                                 .send = lp_send,
                                 .recv = lp_recv,
                                 .status = lp_status,
                                 .room = lp_room,
                                 .ctx = &g->lp};
    rnd_reset();
    flexPhoneLinkInit(&g->L);
    flexPhoneModelInit(&g->M);
    flexPhoneLinkSetIdentity(&g->L, "flexos-1");
    flexPhoneLinkSetRandom(&g->L, rnd_fn);
    flexPhoneLinkSetTransport(&g->L, &g->tr);
    g->ph.lp = &g->lp;
    g->ph.id = "phone-1";
    g->ph.name = "Pixel";
    ph_conn(&g->ph);
    g->now = 1000;
}

/* Avanza `ms` en pasos de 20 ms: tick del reloj y bombeo del teléfono */
static void run(rig_t *g, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 20) {
        g->now += 20;
        flexPhoneLinkTick(&g->L, &g->M, g->now);
        ph_pump(&g->ph);
    }
}

static void open_channel(rig_t *g)
{
    ph_conn(&g->ph);
    flexRingInit(&g->lp.w2p);
    flexRingInit(&g->lp.p2w);
    g->lp.st = FLP_TC_OPEN;
}

static void test_link_pairing(void)
{
    static rig_t g;
    rig_init(&g);
    CHECK_EQ(g.L.state, FLP_LS_OFF);
    CHECK(flexPhoneLinkStart(&g.L));
    CHECK_EQ(g.L.state, FLP_LS_SEARCHING);
    g.lp.st = FLP_TC_SEARCHING;
    flexPhoneLinkBeginPairing(&g.L, rnd_fn, g.now);
    CHECK_EQ(g.L.state, FLP_LS_PAIRING);
    CHECK(strcmp(flexPhoneLinkCode(&g.L), "012345") == 0);
    CHECK_EQ(flexPhoneLinkPairRemainingMs(&g.L, g.now), FLP_LINK_PAIR_WINDOW_MS);
    run(&g, 200); /* sin canal todavía: nada sale */
    CHECK_EQ(g.ph.n_frames, 0);

    open_channel(&g);
    g.now += 20;
    flexPhoneLinkTick(&g.L, &g.M, g.now);
    /* La primera trama del reloj es el HELLO del vector derivado */
    uint8_t f[FLNK_MAX_FRAME];
    size_t fn = flexRingPop(&g.lp.w2p, f, sizeof(f));
    CHECK(hexeq(f, fn, "F15802010000010000010A0001000000B58D0208666C65786F732D31"));
    ph_handle(&g.ph, &(FlexLinkHeader){.type = FLNK_T_HELLO}, f + 18, fn - 18);
    /* ... y el WELCOME del teléfono simulado, el del vector derivado */
    size_t wn = flexRingPop(&g.lp.p2w, f, sizeof(f));
    CHECK(hexeq(f, wn, "F15802020000010000010F0001000000A61A020770686F6E652D3105506978656C"));
    flexRingPush(&g.lp.p2w, f, wn);
    g.now += 20;
    flexPhoneLinkTick(&g.L, &g.M, g.now);
    fn = flexRingPop(&g.lp.w2p, f, sizeof(f));
    CHECK(hexeq(f, fn,
                "F15802110000020000012900020000009B37000102030405060708090A0B0C0D0E0FA0A1A2A3A4A5A6A7A8A9AAABACADAEAF0866"
                "6C65786F732D31"));
    flexRingPush(&g.lp.w2p, f, fn);
    ph_pump(&g.ph);
    CHECK(g.ph.have_pair && strcmp(g.ph.host, "flexos-1") == 0);
    CHECK(strcmp(g.M.phone.name, "Pixel") == 0);

    /* Código equivocado: el teléfono se entera (E_AUTH) y el código sigue */
    ph_type(&g.ph, "111111");
    run(&g, 100);
    CHECK_EQ(g.ph.last_err, FLNK_E_AUTH);
    CHECK_EQ(g.L.state, FLP_LS_PAIRING);
    CHECK(strcmp(flexPhoneLinkCode(&g.L), "012345") == 0 && g.L.pair.rejects == 1);

    /* Código bueno: la trama PAIR_CONFIRM es la del vector derivado (paquete 3 aquí) */
    ph_type(&g.ph, "012345");
    fn = flexRingPop(&g.lp.p2w, f, sizeof(f));
    CHECK(fn == 50 && hexeq(f + 18, 32, "B6624A6C5B772CA17CA3501CCC3F31A0EAE8B6090B93F1C3E2021C07A226EE04"));
    flexRingPush(&g.lp.p2w, f, fn);
    run(&g, 100);
    CHECK(g.L.pair.peerConfirmed && !g.L.bond.valid); /* falta confirmar en el reloj */
    CHECK_EQ(g.L.state, FLP_LS_PAIRING);
    flexPhoneLinkConfirm(&g.L, g.now);
    CHECK_EQ(g.L.state, FLP_LS_READY);
    CHECK(g.L.err[0] == 0); /* RELOJ: el "no coincide" del intento anterior no se queda */
    CHECK(flexPhoneLinkPairComplete(&g.L) && g.L.bond.valid);
    CHECK(hexeq(g.L.bond.key, 32, "66840f7c66ababb9f042d87c18e1f6b28d1bf08db58316823bb99edb38954176"));
    CHECK(strcmp(g.L.bond.peerId, "phone-1") == 0 && (g.L.session & 1));
    run(&g, 100);
    CHECK(g.ph.authed && g.ph.bonded && !g.ph.bad_host_proof && g.ph.session == g.L.session);
    CHECK(g.M.caps.valid && g.M.caps.granted == 0x61 && strcmp(g.M.caps.model, "SM-A556B") == 0);
    CHECK(g.M.phone.valid && g.M.phone.battery == 87 && strcmp(g.M.phone.name, "Pixel 8") == 0);
    CHECK(flexPhoneLinkCode(&g.L)[0] == 0); /* el código deja de existir */

    /* Notificación dorada y otra máxima (429 B, dos fragmentos) */
    uint8_t b[512];
    size_t k = unhex("2A0000000C636F6D2E776861747361707008576861747341707003416E610A756E206D656E73616A6540E2010001030401020C4D"
                     "6172636172206C6569646F09526573706F6E646572",
                     b, sizeof(b));
    ph_send(&g.ph, FLNK_T_NOTIF_ADD, g.ph.session, b, k);
    FlexPhoneNotif big;
    memset(&big, 0, sizeof(big));
    big.id = 7;
    memset(big.pkg, 'p', FLP_PKG_MAX - 1);
    memset(big.app, 'a', FLP_APPNAME_MAX - 1);
    memset(big.title, 't', FLP_TITLE_MAX - 1);
    memset(big.text, 'x', FLP_TEXT_MAX - 1);
    big.actionCount = 4;
    for (int i = 0; i < 4; i++) {
        memset(big.actions[i], 'A' + i, FLP_ACTLABEL_MAX - 1);
    }
    const int bk = flexPhoneEncNotif(b, sizeof(b), &big);
    CHECK_EQ(bk, 429);
    ph_send(&g.ph, FLNK_T_NOTIF_ADD, g.ph.session, b, (size_t)bk);
    run(&g, 100);
    CHECK_EQ(g.M.notifCount, 2);
    const int i42 = flexPhoneNotifFind(&g.M, 42), i7 = flexPhoneNotifFind(&g.M, 7);
    CHECK(i42 >= 0 && i7 >= 0);
    CHECK(i42 >= 0 && strcmp(g.M.notif[i42].app, "WhatsApp") == 0 && g.M.notif[i42].canReply);
    CHECK(i7 >= 0 && strlen(g.M.notif[i7].text) == FLP_TEXT_MAX - 1 && g.M.notif[i7].actions[3][0] == 'D');
    CHECK(g.M.conv[0].used || g.M.conv[1].used);

    /* Respuesta: sale con ACK pedido y el resultado REAL se cuenta */
    CHECK_EQ(flexPhoneReplyCheck(&g.M, 42, "vale"), FLNK_E_NONE);
    k = (size_t)flexPhoneEncReply(b, sizeof(b), 42, g.M.notif[i42].replyAction, "vale");
    CHECK(flexPhoneLinkSend(&g.L, FLNK_T_REPLY_REQ, b, k, true));
    run(&g, 100);
    CHECK(g.ph.n_reply == 1 && g.ph.last_id == 42 && g.M.stats.repliesOk == 1);
    k = (size_t)flexPhoneEncNotifRemove(b, sizeof(b), 7);
    CHECK(flexPhoneLinkSend(&g.L, FLNK_T_NOTIF_REMOVE, b, k, false));
    CHECK(flexPhoneLinkSend(&g.L, FLNK_T_FIND_START, NULL, 0, true));
    run(&g, 100);
    CHECK(g.ph.n_remove == 1 && g.ph.last_id == 7 && g.ph.n_find == 1);

    /* Otra sesión en la cabecera: se descarta */
    const uint32_t drop0 = g.L.nDropped;
    ph_send(&g.ph, FLNK_T_NOTIF_REMOVE, (uint16_t)(g.ph.session ^ 0x0100), (const uint8_t *)"\x2A\0\0\0", 4);
    run(&g, 60);
    CHECK(g.L.nDropped == drop0 + 1 && flexPhoneNotifFind(&g.M, 42) >= 0);

    /* Latido: 8 s sin transmitir -> PING; el PONG da la latencia REAL */
    uint16_t rtt = 0;
    CHECK(!flexPhoneLinkLatency(&g.L, &rtt));
    run(&g, 9000);
    CHECK(g.ph.n_ping >= 1 && flexPhoneLinkLatency(&g.L, &rtt) && rtt <= 40);

    /* Se cae el canal: fuera la sesión y el estado instantáneo; el historial se queda */
    g.lp.st = FLP_TC_SEARCHING;
    run(&g, 40);
    CHECK_EQ(g.L.state, FLP_LS_SEARCHING);
    CHECK(g.L.session == 0 && !g.M.phone.valid && !g.M.caps.valid && g.M.notifCount == 2);

    /* Reconexión con vínculo: reto-respuesta mutuo, sin código */
    open_channel(&g);
    run(&g, 200);
    CHECK_EQ(g.L.state, FLP_LS_READY);
    CHECK(g.ph.authed && !g.ph.bad_host_proof && g.L.session == g.ph.session);
    CHECK(g.M.caps.valid && g.M.phone.valid);
    uint8_t zero[16] = {0};
    CHECK(memcmp(g.L.nonce, zero, 16) != 0); /* reto de sesión sembrado */

    /* El teléfono se despide: a buscar, sin "error" */
    ph_send(&g.ph, FLNK_T_BYE, g.ph.session, NULL, 0);
    run(&g, 40);
    CHECK(g.L.state == FLP_LS_SEARCHING || g.L.state == FLP_LS_CONNECTING);

    /* Teléfono que deja de contestar: muerto a los 30 s */
    g.lp.st = FLP_TC_DOWN;
    run(&g, 40);
    open_channel(&g);
    run(&g, 200);
    CHECK_EQ(g.L.state, FLP_LS_READY);
    g.ph.mute = true;
    g.ph.no_pong = true;
    run(&g, 31000);
    CHECK_EQ(g.L.state, FLP_LS_SEARCHING);
    CHECK(strstr(g.L.err, "dejó de responder") != NULL);
}

static void test_link_edges(void)
{
    static rig_t g;

    /* Caducidad del código: el teléfono recibe E_TIMEOUT */
    rig_init(&g);
    flexPhoneLinkStart(&g.L);
    open_channel(&g);
    run(&g, 100);
    CHECK_EQ(g.L.state, FLP_LS_CONNECTING); /* desconocido y nadie pidió emparejar */
    CHECK(strstr(g.L.err, "sin emparejar") != NULL);
    /* Sin sesión, una notificación perfecta se descarta */
    ph_send(&g.ph, FLNK_T_NOTIF_ADD, 0, (const uint8_t *)"\x01\0\0\0\x01p\0\0\0\0\0\0\0\0\0\0\0\0", 18);
    run(&g, 60);
    CHECK_EQ(g.M.notifCount, 0);
    flexPhoneLinkBeginPairing(&g.L, rnd_fn, g.now);
    run(&g, 100);
    CHECK(g.ph.have_pair && g.L.state == FLP_LS_PAIRING);
    run(&g, FLP_LINK_PAIR_WINDOW_MS + 100);
    CHECK_EQ(g.ph.last_err, FLNK_E_TIMEOUT);
    CHECK(g.L.state == FLP_LS_CONNECTING && flexPhoneLinkCode(&g.L)[0] == 0);

    /* Cancelar: el teléfono se entera y el canal no se toca */
    flexPhoneLinkBeginPairing(&g.L, rnd_fn, g.now);
    run(&g, 60);
    g.ph.last_err = 0;
    flexPhoneLinkCancelPairing(&g.L, g.now);
    run(&g, 60);
    CHECK(g.ph.last_err == FLNK_E_TIMEOUT && g.L.state == FLP_LS_CONNECTING);

    /* RELOJ: saludo sin respuesta (GattServer.kt sin v2): se explica y se reintenta espaciado */
    rig_init(&g);
    g.ph.mute = true;
    flexPhoneLinkStart(&g.L);
    open_channel(&g);
    run(&g, 100);
    CHECK_EQ(g.ph.n_hello, 1);
    CHECK(g.L.err[0] == 0);
    run(&g, FLP_LINK_HELLO_TIMEOUT_MS + 200);
    CHECK(strstr(g.L.err, "saludo") != NULL && g.L.state == FLP_LS_CONNECTING);
    { /* el motivo cabe entero: termina en "v2" y no se recorta */
        const size_t el = strlen(g.L.err);
        CHECK(el + 1 < FLP_LINK_ERR_MAX && el > 2 && strcmp(g.L.err + el - 2, "v2") == 0);
    }
    CHECK_EQ(g.ph.n_hello, 1);
    run(&g, FLP_LINK_HELLO_TIMEOUT_MS); /* reintento a los 10 s */
    CHECK_EQ(g.ph.n_hello, 2);
    CHECK(strstr(g.L.err, "saludo") != NULL); /* el motivo no parpadea al reintentar */
    run(&g, FLP_LINK_HELLO_TIMEOUT_MS + 2 * FLP_LINK_HELLO_TIMEOUT_MS + 200); /* luego 20 s */
    CHECK_EQ(g.ph.n_hello, 3);
    g.ph.mute = false; /* app actualizada: el siguiente saludo sí tiene respuesta */
    run(&g, 4 * FLP_LINK_HELLO_TIMEOUT_MS + 2 * FLP_LINK_HELLO_TIMEOUT_MS);
    CHECK(g.ph.n_hello == 4 && g.L.helloMiss == 0);
    CHECK(strstr(g.L.err, "sin emparejar") != NULL);

    /* El telefono no tiene la clave: ERR(E_NOTPAIRED) y el plazo de AUTH */
    rig_init(&g);
    FlexPhoneBond bd;
    memset(&bd, 0, sizeof(bd));
    bd.valid = true;
    strcpy(bd.peerId, "phone-1");
    memset(bd.key, 0x33, 32);
    flexPhoneLinkSetBond(&g.L, &bd);
    flexPhoneLinkStart(&g.L);
    open_channel(&g);
    run(&g, 100);
    CHECK_EQ(g.L.state, FLP_LS_AUTH);
    CHECK(strstr(g.L.err, "no emparejado") != NULL);
    run(&g, FLP_LINK_AUTH_TIMEOUT_MS + 100);
    CHECK_EQ(g.L.state, FLP_LS_SEARCHING);
    /* Con una clave distinta, la prueba del teléfono no cuadra */
    g.ph.bonded = true;
    memset(g.ph.key, 0x44, 32);
    g.lp.st = FLP_TC_DOWN;
    run(&g, 40);
    open_channel(&g);
    run(&g, 200);
    CHECK(g.L.state != FLP_LS_READY && g.L.nAuthFail >= 2 && !g.ph.authed);

    /* Olvidar borra la clave de verdad */
    flexPhoneLinkForget(&g.L);
    uint8_t z[32] = {0};
    CHECK(!g.L.bond.valid && memcmp(g.L.bond.key, z, 32) == 0 && g.L.state == FLP_LS_OFF);

    /* Entrega: un mensaje que no cabe entero espera (no sale medio) */
    rig_init(&g);
    flexPhoneLinkStart(&g.L);
    open_channel(&g);
    run(&g, 100);
    for (int i = 0; i < 9; i++) { /* llena el anillo de salida a mano */
        flexRingPush(&g.lp.w2p, (const uint8_t *)"x", 1);
    }
    CHECK(flexPhoneLinkSend(&g.L, FLNK_T_PING, NULL, 0, false));
    static uint8_t m3[500];
    CHECK(flexPhoneLinkSend(&g.L, FLNK_T_PAIR_REQ, m3, sizeof(m3), false)); /* 3 tramas */
    g.now += 20;
    flexPhoneLinkTick(&g.L, &g.M, g.now);
    CHECK_EQ(flexRingCount(&g.lp.w2p), 10); /* el PING sí, el de 3 tramas no */
}

void test_flexlink_all(void)
{
    test_frames();
    test_reasm();
    test_antireplay_retry();
    test_fields_utf8();
    test_auth();
    test_codecs();
    test_link_pairing();
    test_link_edges();
}
