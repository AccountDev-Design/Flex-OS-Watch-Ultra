/*
 * Pruebas de la fuente del navegador (br_backend) y de la sesión contra un
 * servicio que EMPUJA imágenes sin que nadie las pida, como el Browser Relay
 * de Flex Phone (2-12 por segundo). Incluye una prueba larga (2 h de reloj
 * virtual) que comprueba que nada crece: la sesión no reserva memoria y
 * ASan/LSan avisan de cualquier fuga al salir.
 * LÍMITE: aquí no hay radio ni Wi-Fi reales; el relay de verdad (Android)
 * y su conexión solo se prueban con el teléfono y el reloj
 * (docs/PRUEBAS-RELOJ.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "br_backend.h"
#include "br_net.h"
#include "fbp_fake.h"
#include "test_util.h"

#define RX_CAP (FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 64)
#define TOKEN "5ae3f87bd1b53ec61eab7c39250623a4" /* el del vínculo 00..1f (test_flexlink) */

enum { RL_OFF = 0, RL_STARTING, RL_UP, RL_ERROR, RL_SUSPENDED };

static br_phone_view_t phone_up(void)
{
    br_phone_view_t v;
    memset(&v, 0, sizeof(v));
    v.link_ready = true;
    v.relay_supported = true;
    v.relay_state = RL_UP;
    v.ip[0] = 192;
    v.ip[1] = 168;
    v.ip[2] = 1;
    v.ip[3] = 34;
    v.port = 8765;
    snprintf(v.token, sizeof(v.token), "%s", TOKEN);
    return v;
}

static void t_resolve(void)
{
    BrSettings st;
    flexBrSettingsDefaults(&st);
    CHECK_EQ(st.source, BRSRC_AUTO);
    br_backend_t b;

    /* Automático sin nada: sin fuente y sin inventar dirección */
    br_backend_resolve(&st, NULL, &b);
    CHECK_EQ(b.src, BRSRC_NONE);
    CHECK(b.url[0] == 0);
    CHECK(b.why[0] != 0);
    CHECK(!b.want_relay);

    /* Automático con el relay arriba: el teléfono, ws:// permitido, credencial del vínculo */
    br_phone_view_t v = phone_up();
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_PHONE);
    CHECK(strcmp(b.url, "ws://192.168.1.34:8765/v1/session") == 0);
    CHECK(strcmp(b.token, TOKEN) == 0);
    CHECK(b.insecure_ok);
    CHECK(!b.want_relay);
    CHECK(b.why[0] == 0);

    /* TLS anunciado por el teléfono: wss:// */
    v.tls = true;
    br_backend_resolve(&st, &v, &b);
    CHECK(strncmp(b.url, "wss://", 6) == 0);
    v.tls = false;

    /* Teléfono primero aunque haya PC configurado (como el P4) */
    snprintf(st.server, sizeof(st.server), "wss://pc.example.com/v1/session");
    snprintf(st.token, sizeof(st.token), "tok-pc");
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_PHONE);

    /* Relay parado: cae al PC y pide el relay */
    v.relay_state = RL_OFF;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_MANUAL);
    CHECK(strcmp(b.url, st.server) == 0);
    CHECK(strcmp(b.token, "tok-pc") == 0);
    CHECK(!b.insecure_ok);
    CHECK(b.want_relay);

    /* Parado A PROPÓSITO en el teléfono: no se vuelve a levantar solo */
    v.relay_by_user = true;
    br_backend_resolve(&st, &v, &b);
    CHECK(!b.want_relay);
    v.relay_by_user = false;

    /* Solo Flex Phone: nunca el PC por detrás, y el motivo real */
    st.source = BRSRC_PHONE;
    v.relay_state = RL_STARTING;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_NONE);
    CHECK(b.url[0] == 0);
    CHECK(strstr(b.why, "Pidiendo") != NULL);
    CHECK(!b.want_relay); /* ya está pedido */

    v.relay_state = RL_ERROR;
    snprintf(v.relay_err, sizeof(v.relay_err), "sin Wi-Fi en el teléfono");
    br_backend_resolve(&st, &v, &b);
    CHECK(strstr(b.why, "sin Wi-Fi en el teléfono") != NULL);
    CHECK(b.want_relay); /* un error se reintenta (con límite de ritmo arriba) */

    v.relay_state = RL_SUSPENDED;
    br_backend_resolve(&st, &v, &b);
    CHECK(strstr(b.why, "suspendió") != NULL);
    CHECK(!b.want_relay);

    v = phone_up();
    v.link_ready = false;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_NONE);
    CHECK(strstr(b.why, "Bluetooth") != NULL);
    CHECK(!b.want_relay);

    v = phone_up();
    v.token[0] = 0; /* sin vínculo no hay credencial: no se conecta */
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_NONE);
    CHECK(strstr(b.why, "Empareja") != NULL);

    v = phone_up();
    v.relay_supported = false;
    v.relay_state = RL_OFF;
    br_backend_resolve(&st, &v, &b);
    CHECK(strstr(b.why, "no ofrece") != NULL);
    CHECK(!b.want_relay);

    /* Una dirección sin puerto no está arriba */
    v = phone_up();
    v.port = 0;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_NONE);

    /* Solo nube y solo PC */
    v = phone_up();
    st.source = BRSRC_CLOUD;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_NONE);
    CHECK(!b.want_relay); /* el teléfono no es la fuente elegida */
    snprintf(st.cloudServer, sizeof(st.cloudServer), "wss://nube.example.com/v1/session");
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_CLOUD);
    CHECK(strcmp(b.url, st.cloudServer) == 0);
    st.source = BRSRC_MANUAL;
    br_backend_resolve(&st, &v, &b);
    CHECK_EQ(b.src, BRSRC_MANUAL);

    /* Cambios que obligan a rehacer la sesión */
    br_backend_t a, c;
    st.source = BRSRC_AUTO;
    br_backend_resolve(&st, &v, &a);
    c = a;
    CHECK(!br_backend_differs(&a, &c));
    v.port = 9000;
    br_backend_resolve(&st, &v, &c);
    CHECK(br_backend_differs(&a, &c));
    br_backend_resolve(&st, NULL, &c);
    CHECK(br_backend_differs(&a, &c));
}

/* ---- sesión con el relay que empuja imágenes ---- */

static uint8_t *s_rx;
static br_net_t S;

static void step(fbp_fake_t *f)
{
    const uint32_t w = br_net_step(&S);
    if (w) {
        fbp_fake_advance(f, w > 50 ? 50 : w);
    }
}

static void t_relay_session(void)
{
    const fbp_fake_cfg_t cfg = {.token = TOKEN, .session_id = "flexphone-relay.r6", .push_ms = 100};
    fbp_fake_t *f = fbp_fake_new(&cfg);
    br_net_init(&S, fbp_fake_io(f), s_rx, RX_CAP);
    BrSettings st;
    flexBrSettingsDefaults(&st);
    br_phone_view_t v = phone_up();
    br_backend_t be;
    br_backend_resolve(&st, &v, &be);
    CHECK_EQ(be.src, BRSRC_PHONE);
    st.allowInsecure |= be.insecure_ok; /* lo mismo que hace watch_browser.c */
    br_net_configure(&S, be.url, be.token, &st, "flexwatch-test", FLEXBR_CAP_REMOTE | FLEXBR_CAP_PSRAM);
    br_net_set_source(&S, be.src, be.why);
    br_net_set_online(&S, true);
    br_net_set_viewport(&S, 410, 400, 1);
    for (int i = 0; i < 2000 && S.state != BRN_READY; i++) {
        step(f);
    }
    CHECK_EQ(S.state, BRN_READY);
    CHECK_EQ(S.src, BRSRC_PHONE);
    fbp_fake_log_t *L = fbp_fake_log(f);
    CHECK(strcmp(L->last_host, "192.168.1.34") == 0);
    CHECK_EQ(L->last_port, 8765);
    CHECK(!L->last_tls);
    CHECK(strcmp(L->last_token, TOKEN) == 0);

    CHECK(br_net_navigate(&S, 1, "https://example.com/"));
    /* Sin tocar nada: las imágenes siguen llegando y se pintan.
     * 2 h de reloj virtual a 10 por segundo. */
    const uint32_t t0 = fbp_fake_now(f);
    int drawn = 0, guard = 0;
    uint16_t last_seq = 0;
    bool seq_moves = true;
    while (fbp_fake_now(f) - t0 < 2u * 3600u * 1000u && guard++ < 20000000) {
        FbpFrame fr;
        uint8_t ch;
        uint16_t seq;
        if (br_net_frame_peek(&S, &fr, &ch, &seq)) {
            if (drawn && seq == last_seq) {
                seq_moves = false;
            }
            last_seq = seq;
            br_net_frame_done(&S, BR_FR_DRAWN);
            drawn++;
        }
        step(f);
    }
    CHECK_EQ(S.state, BRN_READY); /* ni un corte en 2 h */
    CHECK(seq_moves);
    CHECK(L->pushes > 36000);    /* > 5/s sostenido (el relay real da 2-12/s) */
    CHECK(drawn >= L->pushes * 9 / 10);
    CHECK(L->pings > 100);       /* el latido sigue con imágenes entrando */
    printf("   relay del teléfono: %d imágenes empujadas, %d pintadas en 2 h virtuales, %d PING\n", L->pushes, drawn,
           L->pings);

    /* El teléfono cambia de dirección (otra red): sesión nueva al sitio nuevo */
    v.ip[3] = 77;
    br_backend_t be2;
    br_backend_resolve(&st, &v, &be2);
    CHECK(br_backend_differs(&be, &be2));
    br_net_configure(&S, be2.url, be2.token, &st, "flexwatch-test", FLEXBR_CAP_REMOTE | FLEXBR_CAP_PSRAM);
    for (int i = 0; i < 4000 && !(S.state == BRN_READY && strcmp(L->last_host, "192.168.1.77") == 0); i++) {
        step(f);
    }
    CHECK(strcmp(L->last_host, "192.168.1.77") == 0);
    CHECK_EQ(S.state, BRN_READY);

    /* El relay se apaga: sin fuente, con el motivo real en la pantalla */
    v.relay_state = RL_OFF;
    v.relay_by_user = true;
    br_backend_t be3;
    st.source = BRSRC_PHONE;
    br_backend_resolve(&st, &v, &be3);
    CHECK_EQ(be3.src, BRSRC_NONE);
    br_net_configure(&S, be3.url, be3.token, &st, "flexwatch-test", FLEXBR_CAP_PSRAM);
    br_net_set_source(&S, be3.src, be3.why);
    for (int i = 0; i < 400; i++) {
        step(f);
    }
    CHECK(S.state != BRN_READY);
    CHECK(strstr(S.info, "detenido") != NULL);
    br_net_shutdown(&S);
    fbp_fake_free(f);
}

/* Credencial equivocada (otro vínculo): el relay la rechaza y se dice */
static void t_relay_wrong_token(void)
{
    const fbp_fake_cfg_t cfg = {.token = TOKEN, .session_id = "flexphone-relay.r6", .push_ms = 100};
    fbp_fake_t *f = fbp_fake_new(&cfg);
    br_net_init(&S, fbp_fake_io(f), s_rx, RX_CAP);
    BrSettings st;
    flexBrSettingsDefaults(&st);
    br_phone_view_t v = phone_up();
    snprintf(v.token, sizeof(v.token), "00000000000000000000000000000000");
    br_backend_t be;
    br_backend_resolve(&st, &v, &be);
    st.allowInsecure = true;
    br_net_configure(&S, be.url, be.token, &st, "flexwatch-test", FLEXBR_CAP_REMOTE);
    br_net_set_online(&S, true);
    for (int i = 0; i < 2000; i++) {
        step(f);
    }
    CHECK(S.state != BRN_READY);
    CHECK(S.err[0] != 0);
    br_net_shutdown(&S);
    fbp_fake_free(f);
}

void test_br_backend_all(void)
{
    t_resolve();
    s_rx = malloc(RX_CAP);
    t_relay_session();
    t_relay_wrong_token();
    free(s_rx);
}
