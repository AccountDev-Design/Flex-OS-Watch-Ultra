/*
 * OTA de la vista previa: la API de watch_ota sobre el motor REAL (ota_core:
 * manifiesto, instalación paso a paso, SHA-256 y firma ECDSA P-256 de
 * verdad) contra el servidor y la flash falsos de las pruebas de PC
 * (tests/host/ota_fake.c). Como en el reloj, el motor corre en su propio
 * hilo; aquí va a compás de la vista previa: cada 20 ms de reloj simulado
 * avanza una lectura (4 KB), así las capturas y los tiempos son
 * deterministas. Mientras el hilo trabaja, el hilo de LVGL espera (no hay
 * nada concurrente).
 *  - sim_ota_publish(ver, firma_buena, novedades): el servidor publica una
 *    imagen de prueba de SIM_OTA_IMG bytes y su manifiesto firmado;
 *  - sim_ota_online(), sim_ota_key(), sim_ota_batt(): Wi-Fi, clave de
 *    confianza y batería;
 *  - sim_ota_boot(aviso): como si el reloj acabara de reiniciar.
 * La comprobación automática (cada 24 h) no corre en la vista previa: la
 * prueban ota_auto_due() en tests/host.
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ota_fake.h"
#include "ota_sig.h"
#include "sim.h"
#include "watch_ota.h"

#define SIM_OTA_IMG  (600 * 1024)
#define SIM_MAN_URL  WATCH_OTA_MANIFEST_URL
#define SIM_BIN_URL  "https://ota.example/flex_os_watch_ultra.bin"
#define PUMP_MS      20

static ota_t *O;
static ota_fake_t F;
static ota_io_t IO, BASE;
static sem_t s_go, s_done;
static bool s_sem, s_active;
static lv_timer_t *s_pump;
static uint8_t *s_img;
static char s_man[OTA_MANIFEST_MAX];
static uint8_t s_pub[65];
static bool s_key = true;
static uint8_t s_boot;
static uint32_t s_extra_gen;
static int s_runs;

static uint32_t tick(void)
{
    return lv_tick_get();
}

/* El hilo del motor cede el turno y espera el siguiente paso. */
static void step(void)
{
    sem_post(&s_done);
    sem_wait(&s_go);
}

static int w_read(void *c, uint8_t *b, int n)
{
    step();
    return BASE.read(c, b, n);
}

static void w_sleep(void *c, uint32_t ms)
{
    (void)c;
    const uint32_t until = lv_tick_get() + ms;
    while ((int32_t)(lv_tick_get() - until) < 0) {
        step();
    }
}

static void pump_cb(lv_timer_t *t)
{
    (void)t;
    if (s_active) {
        sem_post(&s_go);
        sem_wait(&s_done);
    }
}

static void *worker(void *arg)
{
    const bool install = arg != NULL;
    sem_wait(&s_go);
    if (install) {
        ota_run_install(O, &IO);
    } else {
        ota_run_check(O, &IO);
    }
    s_runs++;
    s_active = false;
    sem_post(&s_done);
    return NULL;
}

static void start(bool install)
{
    s_active = true;
    pthread_t th;
    pthread_create(&th, NULL, worker, install ? (void *)1 : NULL);
    pthread_detach(th);
}

void sim_ota_reset(void)
{
    /* Un hilo a medias se deja terminar antes de rehacer nada. */
    while (s_active) {
        pump_cb(NULL);
    }
    if (!s_sem) {
        sem_init(&s_go, 0, 0);
        sem_init(&s_done, 0, 0);
        s_sem = true;
        O = calloc(1, sizeof(ota_t));
        s_img = malloc(SIM_OTA_IMG);
        ota_fake_pubkey(s_pub);
    }
    if (!s_pump) {
        s_pump = lv_timer_create(pump_cb, PUMP_MS, NULL);
    }
    ota_fake_free(&F);
    ota_fake_init(&F);
    F.ext_clock = tick;
    BASE = ota_fake_io(&F);
    IO = BASE;
    IO.read = w_read;
    IO.sleep = w_sleep;
    IO.lock = NULL;
    IO.unlock = NULL;
    s_key = true;
    s_boot = OTA_BOOT_NONE;
    ota_init(O, OTA_PRODUCT, "0.1.0-b1", SIM_MAN_URL, s_pub);
}

void sim_ota_publish(const char *ver, bool good_sig, const char *changelog)
{
    ota_fake_image(s_img, SIM_OTA_IMG, OTA_PRODUCT, ver, 9);
    char sig[OTA_SIG_HEX + 1];
    ota_fake_sign(good_sig ? ver : "9.9.9", s_img, SIM_OTA_IMG, sig);
    ota_fake_manifest(s_man, sizeof(s_man), ver, SIM_BIN_URL, SIM_OTA_IMG, NULL, sig, changelog);
    /* El manifiesto llega de 32 en 32 B: la búsqueda dura unos 0,5 s. */
    ota_fake_route(&F, SIM_MAN_URL, s_man, strlen(s_man))->chunk = 32;
    ota_fake_route_t *r = ota_fake_route(&F, SIM_BIN_URL, s_img, SIM_OTA_IMG);
    r->chunk = OTA_CHUNK;
}

void sim_ota_online(bool on)
{
    F.online = on;
}

void sim_ota_key(bool on)
{
    s_key = on;
    O->have_key = on;
}

void sim_ota_batt(int pct, bool charging)
{
    F.batt_pct = pct;
    F.charging = charging;
}

void sim_ota_boot(int notice)
{
    s_boot = (uint8_t)notice;
    s_extra_gen++;
}

bool sim_ota_busy(void)
{
    return s_active;
}

int sim_ota_runs(void)
{
    return s_runs;
}

/* La flash falsa: imagen escrita entera, arranque fijado y "pend" guardado. */
bool sim_ota_installed(const char *ver)
{
    return F.fl_finished && F.fl_written == SIM_OTA_IMG && F.flash && !memcmp(F.flash, s_img, SIM_OTA_IMG) &&
           !strcmp(F.pend, ver);
}

bool sim_ota_aborted(void)
{
    return F.fl_aborted && !F.fl_finished && !F.pend[0];
}

/* ---- API de watch_ota ---- */

static void ensure(void)
{
    if (!O) {
        sim_ota_reset();
    }
}

esp_err_t watch_ota_start(void)
{
    ensure();
    return ESP_OK;
}

void watch_ota_get(watch_ota_status_t *out)
{
    ensure();
    memset(out, 0, sizeof(*out));
    out->state = O->state;
    out->err = O->err;
    out->has_upd = O->has_upd;
    out->gen = O->gen + s_extra_gen;
    memcpy(out->local, O->local, sizeof(out->local));
    memcpy(out->ver, O->info.ver, sizeof(out->ver));
    out->size = O->info.size;
    memcpy(out->changelog, O->info.changelog, sizeof(out->changelog));
    out->total = O->total;
    out->done = O->done;
    out->bps = O->bps;
    out->pct = O->pct;
    out->have_key = O->have_key;
    if (O->have_key) {
        uint8_t fp[32];
        ota_sha256(s_pub, sizeof(s_pub), fp);
        ota_hex_encode(fp, 8, out->key_fp);
    }
    out->boot = s_boot;
}

void watch_ota_status_text(char *out, size_t n)
{
    ensure();
    ota_status_text(O, out, n);
}

void watch_ota_check(void)
{
    if (O && ota_req_check(O, F.online)) {
        start(false);
    }
}

void watch_ota_install(void)
{
    if (O && ota_req_install(O, F.online)) {
        start(true);
    }
}

void watch_ota_cancel(void)
{
    if (O) {
        ota_req_cancel(O);
    }
}

bool watch_ota_busy(void)
{
    return O && ota_busy_state(O->state);
}

void watch_ota_boot_seen(void)
{
    if (s_boot) {
        s_boot = OTA_BOOT_NONE;
        s_extra_gen++;
    }
}
