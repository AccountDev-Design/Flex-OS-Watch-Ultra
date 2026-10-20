/*
 * Pruebas de la sesión del navegador (br_net) contra el servicio falso a
 * nivel de bytes (fbp_fake): el WebSocket propio, el apretón, el HELLO, la
 * contrapresión de un FRAME, los plazos y las frases de la referencia.
 * LÍMITE: aquí no hay sockets ni TLS reales; la conexión de verdad
 * (esp_transport) solo se prueba en el reloj (docs/PRUEBAS-RELOJ.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "br_lists.h"
#include "br_net.h"
#include "fbp_fake.h"
#include "test_util.h"

#define RX_CAP (FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 64)

static uint8_t *s_rx;
static br_net_t S;

static fbp_fake_t *setup(const fbp_fake_cfg_t *cfg, const char *url, bool insecure, size_t rx_cap)
{
    fbp_fake_t *f = fbp_fake_new(cfg);
    br_net_init(&S, fbp_fake_io(f), s_rx, rx_cap);
    BrSettings st;
    flexBrSettingsDefaults(&st);
    st.allowInsecure = insecure;
    br_net_configure(&S, url, "credencial-de-prueba-1234567890", &st, "flex-watch-test",
                     FLEXBR_CAP_REMOTE | FLEXBR_CAP_TLS | FLEXBR_CAP_PSRAM);
    br_net_set_online(&S, true);
    br_net_set_viewport(&S, 410, 400, 1);
    return f;
}

/* Avanza la sesión hasta ms de reloj virtual (o hasta cond) */
static void run(fbp_fake_t *f, uint32_t ms)
{
    const uint32_t end = fbp_fake_now(f) + ms;
    for (int guard = 0; guard < 200000 && (int32_t)(fbp_fake_now(f) - end) < 0; guard++) {
        uint32_t w = br_net_step(&S);
        const uint32_t left = end - fbp_fake_now(f);
        if ((int32_t)left <= 0) {
            break;
        }
        if (w) {
            fbp_fake_advance(f, w < left ? w : left); /* sin pasarse del plazo pedido */
        }
    }
}

static bool run_until_ready(fbp_fake_t *f)
{
    for (int i = 0; i < 2000 && S.state != BRN_READY; i++) {
        const uint32_t w = br_net_step(&S);
        if (w) {
            fbp_fake_advance(f, w);
        }
    }
    return S.state == BRN_READY;
}

/* consume (dibuja) el FRAME pendiente, como la interfaz */
static bool take_frame(fbp_fake_t *f, FbpFrame *out)
{
    for (int i = 0; i < 400; i++) {
        uint8_t ch;
        uint16_t seq;
        if (br_net_frame_peek(&S, out, &ch, &seq)) {
            br_net_frame_done(&S, BR_FR_DRAWN);
            return true;
        }
        const uint32_t w = br_net_step(&S);
        if (w) {
            fbp_fake_advance(f, w);
        }
    }
    return false;
}

static const fbp_fake_cfg_t kOk = {.token = "credencial-de-prueba-1234567890", .session_id = "sess-123"};

static void t_happy(void)
{
    fbp_fake_t *f = setup(&kOk, "ws://127.0.0.1:8443/v1/session", true, RX_CAP);
    fbp_fake_log_t *L = fbp_fake_log(f);
    CHECK(run_until_ready(f));
    CHECK_EQ(L->connects, 1);
    CHECK_EQ(L->hellos, 1);
    CHECK(L->hello_w == 410 && L->hello_h == 400);
    CHECK(strcmp(L->last_token, "credencial-de-prueba-1234567890") == 0);
    CHECK(strcmp(L->ua, "FlexOS-Browser/1.0 (Flex OS Watch Ultra)") == 0);
    CHECK(strcmp(S.info, "Sesión abierta: esperando la página") == 0);
    CHECK(strcmp(S.session_id, "sess-123") == 0 && S.stats.relayKind == BRRELAY_OTHER);
    run(f, 50);
    CHECK_EQ(L->viewports, 1); /* RELOJ: VIEWPORT tras el WELCOME (escala) */
    CHECK(L->vp_w == 410 && L->vp_scale == 100 && L->vp_quality == 62);

    /* navegar: NAVIGATE, STATE, NAVIGATED y una imagen */
    CHECK(br_net_navigate(&S, 1, "https://example.com/"));
    FbpFrame fr;
    CHECK(take_frame(f, &fr));
    CHECK_EQ(L->navigates, 1);
    CHECK(strcmp(L->last_url, "https://example.com/") == 0);
    CHECK(fr.w == 410 && fr.h == 400 && fr.imgLen == 10 && fr.img[0] == 0xFF);
    run(f, 300);
    CHECK(S.tab[1].nav_gen == 1 && strcmp(S.tab[1].url, "https://example.com/") == 0);
    CHECK(S.tab[1].progress == 100 && strcmp(S.tab[1].title, "Página de prueba") == 0);
    CHECK(S.tab[1].flags & FBP_ST_CAN_BACK);
    CHECK(L->acks >= 1);
    CHECK_EQ(S.stats.framesOk, 1);

    /* desplazar, tocar y escribir */
    br_net_scroll(&S, 1, 0, 300);
    CHECK(take_frame(f, &fr));
    CHECK(L->scrolls == 1 && L->scroll_y == 300);
    br_net_tap(&S, 1, 120, 260);
    CHECK(br_net_key_text(&S, 1, "h"));
    CHECK(br_net_key_text(&S, 1, "\xC3\xB1"));
    br_net_key(&S, 1, FBP_VK_ENTER);
    CHECK(!br_net_key_text(&S, 1, "una frase demasiado larga")); /* nunca se corta un carácter */
    br_net_cmd(&S, FBP_C_BACK, 1);
    run(f, 100);
    CHECK(L->taps == 1 && L->tap_x == 120 && L->tap_y == 260);
    CHECK(strcmp(L->typed, "h\xC3\xB1") == 0 && L->last_vk == FBP_VK_ENTER);
    CHECK_EQ(L->backs, 1);

    /* cambio de tamaño: un VIEWPORT con el último tamaño, sin reconectar */
    br_net_set_viewport(&S, 400, 300, 1);
    br_net_set_viewport(&S, 400, 280, 1);
    run(f, 600);
    CHECK_EQ(L->viewports, 2);
    /* tras un rato quieto sale enseguida; otro justo después espera a que
     * se asiente (140 ms) o a 400 ms del último envío (brSyncGeometry) */
    br_net_set_viewport(&S, 400, 260, 1);
    run(f, 60);
    CHECK(L->viewports == 3 && L->vp_h == 260);
    br_net_set_viewport(&S, 400, 240, 1);
    run(f, 60);
    CHECK_EQ(L->viewports, 3);
    run(f, 200);
    CHECK(L->viewports == 4 && L->vp_h == 240);
    CHECK_EQ(L->connects, 1);

    /* PING cada 15 s y su PONG da la ida y vuelta */
    run(f, 15500);
    CHECK(L->pings >= 1);
    CHECK(S.stats.rttMs < 1000);
    CHECK_EQ(S.stats.reconnects, 0);
    fbp_fake_free(f);
}

static void t_chunks_and_backpressure(void)
{
    fbp_fake_cfg_t c = kOk;
    c.chunk = 7; /* el servicio entrega de 7 en 7 bytes */
    c.bands = true;
    fbp_fake_t *f = setup(&c, "ws://h:1/s", true, RX_CAP);
    CHECK(run_until_ready(f));
    br_net_navigate(&S, 1, "https://example.com/");
    FbpFrame fr;
    CHECK(take_frame(f, &fr));
    CHECK(fr.y == 0 && fr.h == 200 && !(fr.flags & FBP_FR_LAST));
    CHECK(take_frame(f, &fr));
    CHECK(fr.y == 200 && (fr.flags & FBP_FR_LAST));
    /* un FRAME que nadie consume: la sesión deja de leer y a los 5 s corta */
    br_net_scroll(&S, 1, 0, 50);
    for (int i = 0; i < 400 && !S.fr_ready; i++) {
        fbp_fake_advance(f, br_net_step(&S));
    }
    CHECK(S.fr_ready);
    const uint32_t before = S.msgs_in;
    run(f, 3000);
    CHECK_EQ(S.msgs_in, before); /* no se leyó nada más */
    run(f, 2500);
    CHECK(S.state != BRN_READY || S.stats.reconnects == 0);
    CHECK(S.stats.framesDropped >= 1);
    fbp_fake_free(f);
}

static void t_errors(void)
{
    /* ws:// sin el permiso de desarrollo: ni se conecta */
    fbp_fake_t *f = setup(&kOk, "ws://h:1/s", false, RX_CAP);
    run(f, 100);
    CHECK(S.state == BRN_ERROR && strcmp(S.err, "El servidor debe usar wss:// (TLS)") == 0);
    CHECK_EQ(fbp_fake_log(f)->connects, 0);
    fbp_fake_free(f);

    /* URL inválida y sin servidor */
    f = setup(&kOk, "https://h/s", true, RX_CAP);
    run(f, 100);
    CHECK(strcmp(S.err, "La dirección del servidor no es válida") == 0);
    fbp_fake_free(f);
    f = setup(&kOk, "", true, RX_CAP);
    run(f, 100);
    CHECK(S.state == BRN_OFF && strcmp(S.info, "Sin servidor configurado (flex://settings)") == 0);
    fbp_fake_free(f);

    /* sin Wi-Fi */
    f = setup(&kOk, "ws://h:1/s", true, RX_CAP);
    br_net_set_online(&S, false);
    run(f, 100);
    CHECK(S.state == BRN_NOWIFI && strcmp(S.info, "Sin Wi-Fi: conéctate desde Ajustes") == 0);
    br_net_set_online(&S, true);
    CHECK(run_until_ready(f));
    fbp_fake_free(f);

    /* credencial mala: ERROR 1 con el texto propio, y se espera antes de reintentar */
    fbp_fake_cfg_t c = kOk;
    c.token = "otra";
    f = setup(&c, "ws://h:1/s", true, RX_CAP);
    run(f, 200);
    CHECK(strcmp(S.err, "El servicio rechazó este dispositivo") == 0);
    CHECK_EQ(S.state, BRN_ERROR);
    const int conn = fbp_fake_log(f)->connects;
    run(f, 500);
    CHECK_EQ(fbp_fake_log(f)->connects, conn); /* RELOJ: backoff tras ERROR 1 */
    fbp_fake_free(f);

    /* servidor que rechaza: backoff 1, 2, 4, 8, 16 y 16 s */
    c = kOk;
    c.refuse_connect = true;
    f = setup(&c, "ws://h:1/s", true, RX_CAP);
    static const uint32_t kBack[] = {1000, 2000, 4000, 8000, 16000, 16000};
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        const uint32_t w = br_net_step(&S);
        ok = ok && w == kBack[i];
        fbp_fake_advance(f, w);
    }
    CHECK(ok);
    CHECK(strcmp(S.err, "No se pudo conectar con el servicio") == 0 && S.stats.reconnects == 6);
    fbp_fake_free(f);

    /* certificado que no verifica (RELOJ: se dice como tal) */
    c = kOk;
    c.tls_bad = true;
    f = setup(&c, "wss://nav.example.com/v1/session", false, RX_CAP);
    br_net_step(&S);
    CHECK(strcmp(S.err, "El certificado del servidor no es válido") == 0);
    fbp_fake_free(f);

    /* Upgrade con una clave equivocada */
    c = kOk;
    c.bad_accept = true;
    f = setup(&c, "ws://h:1/s", true, RX_CAP);
    br_net_step(&S);
    CHECK(strcmp(S.err, "El servicio rechazó la conexión") == 0);
    CHECK(strcmp(S.info, "El servicio no aceptó el WebSocket (ruta /s)") == 0);
    fbp_fake_free(f);

    /* sin WELCOME en 10 s */
    c = kOk;
    c.mute_hello = true;
    f = setup(&c, "ws://h:1/s", true, RX_CAP);
    run(f, 9000);
    CHECK_EQ(S.state, BRN_HANDSHAKE);
    run(f, 1500);
    CHECK(strcmp(S.err, "El servicio no respondió al saludo") == 0);
    fbp_fake_free(f);

    /* navega pero no manda imágenes: aviso a los 12 s */
    c = kOk;
    c.no_frames = true;
    f = setup(&c, "ws://h:1/s", true, RX_CAP);
    CHECK(run_until_ready(f));
    br_net_navigate(&S, 1, "https://example.com/");
    run(f, 13000);
    CHECK(strcmp(S.err, "El servicio no envía imágenes de la página") == 0);
    CHECK(strncmp(S.info, "Sin imágenes tras 12s", 21) == 0);
    fbp_fake_free(f);
}

static void t_session_events(void)
{
    fbp_fake_cfg_t c = kOk;
    c.session_id = "flexphone-viejo";
    fbp_fake_t *f = setup(&c, "ws://h:1/s", true, RX_CAP);
    CHECK(run_until_ready(f));
    CHECK(S.stats.relayKind == BRRELAY_PHONE && S.stats.relayBuild == -1);
    CHECK(strcmp(S.err, "Reinstala Flex Phone: su Relay es antiguo (web ampliada)") == 0);

    /* una redirección a un destino interno se para */
    uint8_t p[64];
    const char *u = "http://192.168.1.1/admin";
    p[0] = (uint8_t)strlen(u);
    p[1] = 0;
    memcpy(p + 2, u, strlen(u));
    fbp_fake_send(f, FBP_S_NAVIGATED, 1, p, (uint32_t)(2 + strlen(u)));
    run(f, 60);
    CHECK(strcmp(S.err, "Redirección a un destino bloqueado") == 0);
    CHECK(S.tab[1].nav_gen == 0);

    /* RELOJ: un FRAME de otra pestaña no se pinta */
    const uint8_t fr[] = {0, 0, 0, 0, 10, 0, 10, 0, FBP_IMG_JPEG, 3, 1, 0, 0, 0, 2, 0, 0, 0, 0xFF, 0xD8};
    fbp_fake_send(f, FBP_S_FRAME, 2, fr, sizeof(fr));
    run(f, 60);
    CHECK(!S.fr_ready && S.stats.framesDropped == 1);

    /* un mensaje que no cabe en el búfer se descarta sin cortar la sesión */
    static uint8_t big[RX_CAP];
    fbp_fake_send(f, FBP_S_TABS, 0, big, (uint32_t)RX_CAP);
    run(f, 60);
    CHECK(S.state == BRN_READY && S.stats.framesBad == 1);

    /* ERROR del servicio: texto propio por código, nunca el del servicio */
    const uint8_t e[] = {FBP_ERR_TIMEOUT, 0, 5, 0, 'h', 'o', 'l', 'a', '!'};
    fbp_fake_send(f, FBP_S_ERROR, 1, e, sizeof(e));
    run(f, 60);
    CHECK(strcmp(S.err, "Se agotó el tiempo de espera") == 0 && S.state == BRN_READY);

    /* se corta: reconecta, repite el HELLO y lo encolado no se pierde */
    fbp_fake_drop(f);
    run(f, 50);
    CHECK(strcmp(S.info, "Se perdió la conexión con el servicio") == 0 || S.state != BRN_READY);
    br_net_navigate(&S, 1, "https://example.org/");
    CHECK(run_until_ready(f));
    FbpFrame frm;
    CHECK(take_frame(f, &frm));
    CHECK(fbp_fake_log(f)->hellos == 2 && strcmp(fbp_fake_log(f)->last_url, "https://example.org/") == 0);
    CHECK(S.stats.reconnects >= 1);

    /* configurar otro servidor rehace la sesión */
    BrSettings st = S.set;
    br_net_configure(&S, "ws://otro:2/s", "credencial-de-prueba-1234567890", &st, "x", S.caps);
    CHECK_EQ(S.state, BRN_OFF);
    CHECK(run_until_ready(f));
    fbp_fake_free(f);
}

static void t_lists(void)
{
    static br_lists_t L, M;
    br_lists_init(&L);
    br_hist_add(&L, "https://a.es/", "A", 10);
    br_hist_add(&L, "https://b.es/", "B", 20);
    br_hist_add(&L, "https://a.es/", "", 30); /* repetida: sube con su título */
    br_hist_add(&L, "flex://history", "Historial", 40);
    CHECK(L.hn == 2 && strcmp(L.h[0].url, "https://a.es/") == 0 && L.h[0].visits == 2);
    CHECK(strcmp(L.h[0].title, "A") == 0 && L.h[0].lastMs == 30);
    const uint32_t g = L.gen;
    br_hist_title(&L, "https://b.es/", "Be");
    CHECK(strcmp(L.h[1].title, "Be") == 0 && L.h[1].visits == 1 && L.gen == g + 1);
    br_hist_title(&L, "https://b.es/", "Be");
    CHECK_EQ(L.gen, g + 1);
    for (int i = 0; i < 100; i++) {
        char u[40];
        snprintf(u, sizeof(u), "https://s%d.es/", i);
        br_hist_add(&L, u, "t\nx", (uint32_t)i);
    }
    CHECK_EQ(L.hn, FLEXBR_HIST_MAX);
    CHECK(strcmp(L.h[0].url, "https://s99.es/") == 0 && strcmp(L.h[0].title, "t x") == 0);
    bool added;
    CHECK(br_bm_toggle(&L, "https://a.es/", "A", &added) && added && L.bn == 1);
    CHECK(br_bm_toggle(&L, "https://a.es/", "A", &added) && !added && L.bn == 0);
    CHECK(!br_bm_toggle(&L, "flex://about", "x", &added));
    br_bm_toggle(&L, "https://c.es/", "", &added);
    CHECK(strcmp(L.b[0].title, "https://c.es/") == 0);
    char path[256];
    snprintf(path, sizeof(path), "%s/br_hist_test.bin", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(br_lists_save(&L, true, path));
    br_lists_init(&M);
    CHECK(br_lists_load(&M, true, path));
    CHECK(M.hn == L.hn && memcmp(M.h, L.h, sizeof(BrHistEntry) * (size_t)L.hn) == 0);
    CHECK(br_lists_save(&L, false, path) && br_lists_load(&M, false, path) && M.bn == 1);
    FILE *f = fopen(path, "r+b"); /* un byte roto en la cuenta: se rechaza entero */
    fseek(f, 6, SEEK_SET);
    fputc(99, f);
    fclose(f);
    CHECK(!br_lists_load(&M, false, path) && M.bn == 0);
    CHECK(!br_lists_load(&M, true, "/no/existe"));
    remove(path);
}

void test_br_net_all(void)
{
    t_lists();
    s_rx = malloc(RX_CAP);
    t_happy();
    t_chunks_and_backpressure();
    t_errors();
    t_session_events();
    printf("   navegador: br_net_t %zu B, búfer de recepción %u B\n", sizeof(br_net_t), (unsigned)RX_CAP);
    free(s_rx);
}
