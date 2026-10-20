/*
 * Navegador de la vista previa: la API de watch_browser sobre la sesión REAL
 * (br_net: WebSocket propio, FBP/1, plazos) y el servicio falso a nivel de
 * bytes de tests/host (fbp_fake), con el reloj de LVGL. Las páginas son los
 * JPEG baseline de fixtures/ (gen_br_pages.py), elegidos por el
 * desplazamiento que pide el reloj; se decodifican con el TJpgDec de LVGL
 * igual que en el reloj. Lo único simulado es la red.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fbp_fake.h"
#include "lvgl.h"
#include "sim.h"
#include "watch_browser.h"

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
    const fbp_fake_cfg_t cfg = {.token = "credencial-de-prueba-1234567890",
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

esp_err_t watch_browser_open(void)
{
    if (!s_fake) {
        sim_br_reset();
    }
    if (s_open) {
        return ESP_OK;
    }
    s_rx = malloc(RX_CAP);
    br_net_init(&s_net, &s_io, s_rx, RX_CAP);
    br_net_configure(&s_net, s_set.server, s_set.token, &s_set, watch_browser_device(), watch_browser_caps());
    s_open = true;
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
    if (s_open) {
        br_net_configure(&s_net, s_set.server, s_set.token, &s_set, watch_browser_device(), watch_browser_caps());
    }
}

const char *watch_browser_device(void)
{
    return "flexwatch-sim";
}

uint32_t watch_browser_caps(void)
{
    uint32_t c = FLEXBR_CAP_TILES | FLEXBR_CAP_TABS | FLEXBR_CAP_TLS | FLEXBR_CAP_PSRAM;
    if (s_set.server[0]) {
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
    snprintf(msg, n, "%s", !s_online ? "Sin Wi-Fi" : fbp_fake_cfg(s_fake)->refuse_connect ? "El servidor no responde"
                                                                                           : "Servidor accesible");
    return 1;
}
