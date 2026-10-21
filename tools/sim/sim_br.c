/*
 * Navegador de la vista previa: la API de watch_browser sobre la sesión REAL
 * (br_net: WebSocket propio, FBP/1, plazos) y el servicio falso a nivel de
 * bytes de tests/host (fbp_fake), con el reloj de LVGL. Las páginas son los
 * JPEG baseline de fixtures/ (gen_br_pages.py), elegidos por el
 * desplazamiento que pide el reloj; se decodifican con el TJpgDec de LVGL
 * igual que en el reloj. Lo único simulado es la red.
 *
 * La fuente sale de br_backend con el estado del Flex Phone simulado
 * (sim_phone.c, el mismo phone_core que el reloj): con su relay arriba, el
 * servicio falso hace de relay del teléfono (acepta la credencial que deriva
 * el TELÉFONO y empuja imágenes cada 150 ms sin que se pidan).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fbp_fake.h"
#include "lvgl.h"
#include "sim.h"
#include "watch_browser.h"
#include "br_phone_view.h"

#define RX_CAP (FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 64)
#define NPAGES 8

extern const uint8_t _binary_br_page_0_jpg_start[], _binary_br_page_0_jpg_end[];
extern const uint8_t _binary_br_page_1_jpg_start[], _binary_br_page_1_jpg_end[];
extern const uint8_t _binary_br_page_2_jpg_start[], _binary_br_page_2_jpg_end[];
extern const uint8_t _binary_br_page_3_jpg_start[], _binary_br_page_3_jpg_end[];
extern const uint8_t _binary_br_page_4_jpg_start[], _binary_br_page_4_jpg_end[];
extern const uint8_t _binary_br_page_5_jpg_start[], _binary_br_page_5_jpg_end[];
extern const uint8_t _binary_br_page_6_jpg_start[], _binary_br_page_6_jpg_end[];
extern const uint8_t _binary_br_page_7_jpg_start[], _binary_br_page_7_jpg_end[];
extern const uint8_t _binary_br_prog_jpg_start[], _binary_br_prog_jpg_end[];

static const uint8_t *const kStart[NPAGES] = {
    _binary_br_page_0_jpg_start, _binary_br_page_1_jpg_start, _binary_br_page_2_jpg_start,
    _binary_br_page_3_jpg_start, _binary_br_page_4_jpg_start, _binary_br_page_5_jpg_start,
    _binary_br_page_6_jpg_start, _binary_br_page_7_jpg_start};
static const uint8_t *const kEnd[NPAGES] = {
    _binary_br_page_0_jpg_end, _binary_br_page_1_jpg_end, _binary_br_page_2_jpg_end, _binary_br_page_3_jpg_end,
    _binary_br_page_4_jpg_end, _binary_br_page_5_jpg_end, _binary_br_page_6_jpg_end, _binary_br_page_7_jpg_end};
static const int kOff[NPAGES] = {0, 150, 300, 450, 600, 750, 900, 1124};

static fbp_fake_t *s_fake;
static br_io_t s_io;
static br_net_t s_net;
static uint8_t *s_rx;
static bool s_open, s_online = true, s_prog;
static BrSettings s_set;
static br_lists_t s_lists;
static char s_last[FLEXBR_URL_MAX];
static int s_probe;
static br_backend_t s_be;
static watch_phone_status_t s_ps;
static uint32_t s_be_ms, s_relay_ask_ms;
static char s_relay_tok[33];

#define PC_TOKEN "credencial-de-prueba-1234567890"

/* Igual que refresh_backend() de watch_browser.c */
static void refresh_backend(bool force, bool ask)
{
    br_phone_view_t v;
    br_backend_t be;
    br_phone_view_get(&v, &s_ps);
    br_backend_resolve(&s_set, &v, &be);
    const bool changed = force || br_backend_differs(&be, &s_be);
    if (changed) {
        s_be = be;
        /* El servicio falso hace de lo que haya al otro lado */
        fbp_fake_cfg_t *c = fbp_fake_cfg(s_fake);
        if (be.src == BRSRC_PHONE) {
            sim_phone_relay_token(s_relay_tok, sizeof(s_relay_tok));
            c->token = s_relay_tok;
            c->session_id = "flexphone-relay.r6";
            c->push_ms = 150;
        } else {
            c->token = PC_TOKEN;
            c->session_id = "s-7f3a";
            c->push_ms = 0;
        }
        if (s_open) {
            BrSettings set = s_set;
            set.allowInsecure |= be.insecure_ok;
            br_net_configure(&s_net, be.url, be.token, &set, watch_browser_device(), watch_browser_caps());
            br_net_set_source(&s_net, be.src, be.why);
        }
    }
    const uint32_t now = lv_tick_get();
    if (ask && be.want_relay && (!s_relay_ask_ms || now - s_relay_ask_ms >= 15000)) {
        s_relay_ask_ms = now;
        watch_phone_relay(true);
    }
}

static uint32_t clock_ms(void)
{
    return lv_tick_get();
}

static const uint8_t *page_img(void *user, int scroll_y, uint32_t *len, uint16_t *w, uint16_t *h)
{
    (void)user;
    if (s_prog) {
        *len = (uint32_t)(_binary_br_prog_jpg_end - _binary_br_prog_jpg_start);
        *w = 64;
        *h = 64;
        return _binary_br_prog_jpg_start;
    }
    int k = 0;
    for (int i = 0; i < NPAGES; i++) {
        if (abs(kOff[i] - scroll_y) < abs(kOff[k] - scroll_y)) {
            k = i;
        }
    }
    *len = (uint32_t)(kEnd[k] - kStart[k]);
    *w = 410;
    *h = 376; /* la imagen mide lo que pinta: con el teclado abierto se recorta */
    return kStart[k];
}

void sim_br_reset(void)
{
    if (s_open) {
        watch_browser_close();
    }
    if (s_fake) {
        fbp_fake_free(s_fake);
    }
    const fbp_fake_cfg_t cfg = {.token = PC_TOKEN,
                                .session_id = "s-7f3a",
                                .img = page_img,
                                .clock = clock_ms};
    s_fake = fbp_fake_new(&cfg);
    s_io = *fbp_fake_io(s_fake);
    flexBrSettingsDefaults(&s_set);
    br_lists_init(&s_lists);
    s_last[0] = 0;
    s_online = true;
    s_prog = false;
    memset(&s_be, 0, sizeof(s_be));
    s_be.src = BRSRC_MANUAL;
    s_be_ms = s_relay_ask_ms = 0;
}

void *sim_br_fake(void)
{
    if (!s_fake) {
        sim_br_reset();
    }
    return s_fake;
}

void sim_br_online(bool on)
{
    if (!on && s_online && s_fake) {
        fbp_fake_drop(s_fake); /* sin Wi-Fi el socket se cae */
    }
    s_online = on;
}

void sim_br_progressive(bool on)
{
    s_prog = on;
}

/* ---- API de watch_browser ---- */

/* Misma regla que el reloj (watch_browser.c), sobre la memoria simulada. */
bool watch_browser_mem_check(char *why, size_t n)
{
    if (sim_mem_internal_free < 10240 + 20 * 1024) {
        if (why && n) {
            snprintf(why, n, "Falta RAM interna: %u KB libres", (unsigned)(sim_mem_internal_free / 1024));
        }
        return false;
    }
    if (sim_mem_psram_block < RX_CAP) {
        if (why && n) {
            snprintf(why, n, "Falta PSRAM: bloque %u KB", (unsigned)(sim_mem_psram_block / 1024));
        }
        return false;
    }
    return true;
}

esp_err_t watch_browser_open(void)
{
    if (!s_fake) {
        sim_br_reset();
    }
    if (s_open) {
        return ESP_OK;
    }
    if (!watch_browser_mem_check(NULL, 0)) {
        return ESP_ERR_NO_MEM;
    }
    s_rx = malloc(RX_CAP);
    br_net_init(&s_net, &s_io, s_rx, RX_CAP);
    s_open = true;
    s_relay_ask_ms = 0;
    refresh_backend(true, false);
    return ESP_OK;
}

void watch_browser_close(void)
{
    if (!s_open) {
        return;
    }
    br_net_shutdown(&s_net);
    free(s_rx);
    s_rx = NULL;
    s_open = false;
}

br_net_t *watch_browser_net(void)
{
    if (!s_open) {
        return NULL;
    }
    /* la "tarea de red": unas vueltas de la sesión real en cada consulta */
    const uint32_t now = lv_tick_get();
    if (!s_be_ms || now - s_be_ms >= 500) {
        s_be_ms = now ? now : 1;
        refresh_backend(false, true);
    }
    br_net_set_online(&s_net, s_online);
    for (int i = 0; i < 64; i++) {
        if (br_net_step(&s_net) > 2 || s_net.fr_ready) {
            break;
        }
    }
    return &s_net;
}

br_lists_t *watch_browser_lists(void)
{
    return &s_lists;
}

void watch_browser_persist(bool force)
{
    (void)force; /* en la vista previa las listas viven en memoria */
}

void watch_browser_get_settings(BrSettings *out)
{
    if (!s_fake) {
        sim_br_reset();
    }
    *out = s_set;
}

void watch_browser_set_settings(const BrSettings *st)
{
    s_set = *st;
    refresh_backend(true, s_open);
}

void watch_browser_source(watch_browser_source_t *out)
{
    if (!s_fake) {
        sim_br_reset();
    }
    if (!s_open) {
        refresh_backend(false, false);
    }
    out->src = s_be.src;
    out->pref = s_set.source;
    snprintf(out->why, sizeof(out->why), "%s", s_be.why);
    out->relay_state = s_ps.relay.state;
    memcpy(out->relay_ip, s_ps.relay.ip, 4);
    out->relay_port = s_ps.relay.port;
    out->phone_possible = s_ps.bonded && (s_set.source == BRSRC_AUTO || s_set.source == BRSRC_PHONE);
}

const char *watch_browser_device(void)
{
    return "flexwatch-sim";
}

uint32_t watch_browser_caps(void)
{
    uint32_t c = FLEXBR_CAP_TILES | FLEXBR_CAP_TABS | FLEXBR_CAP_TLS | FLEXBR_CAP_PSRAM;
    if (s_be.src != BRSRC_NONE && s_be.url[0]) {
        c |= FLEXBR_CAP_REMOTE;
    }
    return c;
}

bool watch_browser_online(void)
{
    return s_online;
}

void watch_browser_get_last(char *url, size_t n)
{
    snprintf(url, n, "%s", s_last);
}

void watch_browser_set_last(const char *url)
{
    snprintf(s_last, sizeof(s_last), "%s", url ? url : "");
}

void watch_browser_probe_start(void)
{
    s_probe = 1;
}

int watch_browser_probe_result(char *msg, size_t n)
{
    if (s_probe != 1) {
        return 0;
    }
    s_probe = 0;
    if (!s_open) {
        refresh_backend(false, false);
    }
    snprintf(msg, n, "%s", !s_be.url[0]                         ? (s_be.why[0] ? s_be.why : "Configura antes la dirección del servidor")
                           : !s_online                           ? "Sin Wi-Fi"
                           : fbp_fake_cfg(s_fake)->refuse_connect ? "El servidor no responde"
                                                                 : "Servidor accesible");
    return 1;
}
