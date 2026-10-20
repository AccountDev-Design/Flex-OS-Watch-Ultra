/*
 * Suite "relay": el navegador del reloj por el Browser Relay de Flex Phone,
 * de extremo a extremo en la vista previa.
 *
 *  reloj (phone_core + flexlink, el código del firmware)
 *    ── Flex Link v2 por el canal en memoria ──>  Flex Phone simulado
 *    <── RELAY_INFO 192.168.1.34:8765 ──
 *  navegador (br_backend + br_net, el código del firmware)
 *    ── ws://192.168.1.34:8765/v1/session, credencial del vínculo ──>
 *  servicio FBP/1 falso a nivel de bytes que hace de relay: acepta SOLO la
 *  credencial que deriva el teléfono de SU copia de la clave y empuja
 *  imágenes cada 150 ms sin que nadie las pida.
 *
 * Comprueba: el reloj pide el relay solo al abrir el navegador; la sesión va
 * al teléfono con la credencial correcta; la página se repinta sola sin
 * tocar la pantalla; si el teléfono para el relay se dice el motivo y no se
 * cae al PC en modo "Solo Flex Phone"; un relay que falla o que no contesta
 * se explica; una sesión larga (10 min de reloj) no pierde memoria ni
 * temporizadores, y la app Flex Phone del reloj enseña el icono y el estado.
 * LÍMITE: radio, Wi-Fi y el relay Android de verdad solo en el hardware
 * (docs/PRUEBAS-RELOJ.md, T-FP*).
 */
#include <stdio.h>
#include <string.h>

#include "fbp_fake.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_browser.h"
#include "watch_phone.h"

void sim_br_reset(void);
void *sim_br_fake(void);
void sim_phone_auto(bool on);

static bool has(const char *t)
{
    return sim_find_label(ui_nav_top(), t) != NULL;
}

static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

static watch_phone_status_t st(void)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    return s;
}

/* Empareja de verdad: código en el reloj, el "usuario" lo teclea en el teléfono y confirma en el reloj */
static bool pair(void)
{
    sim_phone_auto(true);
    watch_phone_set_enabled(true);
    sim_run(1500);
    watch_phone_pair_begin();
    sim_run(2200);
    watch_phone_pair_confirm();
    for (int i = 0; i < 40 && st().link != FLP_LS_READY; i++) {
        sim_run(100);
    }
    sim_phone_auto(false);
    return st().link == FLP_LS_READY && st().bonded;
}

static int frames_ok(void)
{
    br_net_t *n = watch_browser_net();
    return n ? (int)n->stats.framesOk : -1;
}

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (lv_obj_check_type(o, cls) && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_class(lv_obj_get_child(o, (int32_t)i), cls);
        if (r) {
            return r;
        }
    }
    return NULL;
}

/* Escribe en el omnibox (arriba y ancho) y pulsa OK */
static bool go(const char *url)
{
    lv_obj_t *top = ui_nav_top(), *om = NULL;
    for (uint32_t i = 0; !om && i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if (a.y1 < 30 && lv_area_get_width(&a) > 200 && lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) {
            om = c;
        }
    }
    if (!om) {
        return false;
    }
    click(om);
    sim_run(300);
    lv_obj_t *ta = find_class(ui_nav_top(), &lv_textarea_class);
    lv_obj_t *kb = find_class(ui_nav_top(), &lv_buttonmatrix_class);
    if (!ta || !kb) {
        return false;
    }
    lv_textarea_set_text(ta, url);
    const char *const *map = lv_buttonmatrix_get_map(kb);
    for (uint32_t i = 0, b = 0; map[i][0]; i++) {
        if (strcmp(map[i], "\n") == 0) {
            continue;
        }
        if (strcmp(map[i], "OK") == 0) {
            lv_buttonmatrix_set_selected_button(kb, b);
            lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
            sim_run(30);
            return true;
        }
        b++;
    }
    return false;
}

void sim_suite_relay(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_phone_reset();
    sim_phone_relay_mode(SIM_RELAY_OK);
    sim_br_reset();
    sim_run(400);
    fbp_fake_t *fk = sim_br_fake();
    fbp_fake_log_t *L = fbp_fake_log(fk);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* 1) Emparejados por Flex Link v2; el relay NO se pide mientras el navegador está cerrado */
    EXPECT(pair(), "relay: no se emparejó (estado %d)", st().link);
    sim_run(2000);
    for (int i = 0; i < 100 && ui_banner_active(); i++) { /* el aviso de las notificaciones que llegan */
        sim_run(100);
    }
    EXPECT(st().relay.state == FLP_RELAY_OFF && !sim_phone_relay_up(), "relay: se pidió sin abrir el navegador");

    /* 2) La app Flex Phone del reloj: icono del teléfono y sección del navegador */
    ui_nav_push(ui_flexphone_create);
    sim_run(400);
    EXPECT(has("Navegador del teléfono") && has("Conectado"), "flexphone: falta la sección del navegador");
    sim_lint("flexphone con icono");
    sim_save(out, "fp_flexphone_icono");
    click(tap_of("Navegador del teléfono"));
    sim_run(400);
    EXPECT(has("Web del teléfono") && has("Parado") && has("Encender el servidor del teléfono") &&
               has("Abrir el navegador"),
           "relay: página del relay incompleta");
    sim_lint("relay parado");
    sim_save(out, "fp_relay_parado");

    /* 3) Abrir el navegador: pide el relay por BLE, el teléfono contesta y la sesión va a él */
    click(tap_of("Abrir el navegador"));
    sim_run(300);
    EXPECT(watch_browser_net() != NULL, "relay: el navegador no abrió sesión con el teléfono vinculado");
    EXPECT(st().relay.state == FLP_RELAY_STARTING || sim_phone_relay_up(), "relay: no se pidió al abrir (%d)",
           st().relay.state);
    sim_run(1600);
    watch_phone_status_t s = st();
    EXPECT(s.relay.state == FLP_RELAY_UP && s.relay.port == 8765 && s.relay.ip[0] == 192 && s.relay.ip[3] == 34,
           "relay: RELAY_INFO no llegó (%d %u)", s.relay.state, s.relay.port);
    char tok_w[33], tok_p[33];
    watch_phone_relay_token(tok_w, sizeof(tok_w));
    sim_phone_relay_token(tok_p, sizeof(tok_p));
    EXPECT(strlen(tok_w) == 32 && strcmp(tok_w, tok_p) == 0, "relay: credenciales distintas");
    sim_run(800);
    br_net_t *n = watch_browser_net();
    EXPECT(n && n->state == BRN_READY && n->src == BRSRC_PHONE, "relay: sesión no lista (%d)", n ? n->state : -1);
    EXPECT(strcmp(L->last_host, "192.168.1.34") == 0 && L->last_port == 8765 && !L->last_tls,
           "relay: conectó a %s:%u", L->last_host, L->last_port);
    EXPECT(strcmp(L->last_token, tok_p) == 0, "relay: HELLO con otra credencial");
    sim_lint("navegador por el teléfono");
    sim_save(out, "fp_nav_relay_lista");

    /* 4) Navegar y que la página se actualice sola, sin tocar nada */
    EXPECT(go("ejemplo.com"), "relay: sin omnibox");
    sim_run(1200);
    EXPECT(L->navigates >= 1 && strcmp(L->last_url, "https://ejemplo.com") == 0, "relay: NAVIGATE %s",
           L->last_url);
    const int f_a = frames_ok(), p_a = L->pushes;
    sim_run(3000); /* 3 s sin tocar la pantalla */
    const int f_b = frames_ok(), p_b = L->pushes;
    EXPECT(p_b - p_a >= 12, "relay: el servicio no empujó (%d)", p_b - p_a);
    EXPECT(f_b - f_a >= 12, "relay: la página no se repintó sola (%d imágenes en 3 s)", f_b - f_a);
    sim_save(out, "fp_nav_relay_pagina");

    /* 5) Prueba larga: 10 min de reloj con la página viva; ni fugas ni cortes */
    const uint32_t blk_mid = sim_mem_blocks();
    const size_t used_mid = sim_mem_used();
    const int f_c = frames_ok();
    for (int k = 0; k < 60; k++) {
        sim_run(10000);
    }
    n = watch_browser_net();
    const int f_d = frames_ok();
    EXPECT(n && n->state == BRN_READY, "relay largo: la sesión se cayó (%d)", n ? n->state : -1);
    EXPECT(f_d - f_c >= 600 * 5, "relay largo: solo %d imágenes en 10 min", f_d - f_c);
    EXPECT(n && n->stats.reconnects == 0, "relay largo: %u reconexiones", n ? (unsigned)n->stats.reconnects : 0);
    EXPECT(sim_mem_blocks() <= blk_mid + 2, "relay largo: bloques %u -> %u", (unsigned)blk_mid,
           (unsigned)sim_mem_blocks());
    printf("   relay 10 min: %d imágenes pintadas, %d empujadas, memoria LVGL %zu -> %zu B, bloques %u -> %u\n",
           f_d - f_c, L->pushes, used_mid, sim_mem_used(), (unsigned)blk_mid, (unsigned)sim_mem_blocks());

    /* 6) El teléfono cambia de red: dirección nueva -> sesión nueva sola (aquí: parar y volver) */
    /* 6a) Parado desde el teléfono: "Solo Flex Phone" no cae al PC y dice el motivo */
    BrSettings set;
    watch_browser_get_settings(&set);
    snprintf(set.server, sizeof(set.server), "wss://pc.example.com/v1/session");
    snprintf(set.token, sizeof(set.token), "credencial-de-prueba-1234567890");
    set.source = BRSRC_PHONE;
    watch_browser_set_settings(&set);
    sim_run(600);
    EXPECT(watch_browser_net()->src == BRSRC_PHONE, "solo teléfono: cambió de origen");
    sim_phone_relay_user_stop();
    sim_run(1500);
    n = watch_browser_net();
    EXPECT(n && n->state != BRN_READY && n->src == BRSRC_NONE, "relay parado: la sesión sigue (%d)",
           n ? n->state : -1);
    EXPECT(strcmp(L->last_host, "192.168.1.34") == 0, "solo teléfono: cayó al PC (%s)", L->last_host);
    watch_browser_source_t src;
    watch_browser_source(&src);
    EXPECT(strstr(src.why, "detenido") != NULL, "relay parado: motivo \"%s\"", src.why);
    EXPECT(!sim_phone_relay_up(), "relay parado por el usuario: el reloj lo volvió a levantar");
    EXPECT(has("Servidor del navegador detenido en el teléfono") && has("Sin origen"),
           "relay parado: la pantalla no enseña el motivo");
    sim_lint("relay parado por el usuario");
    sim_save(out, "fp_nav_relay_parado");

    /* 6b) Automático: con el relay parado, el PC; al volver el relay, el teléfono otra vez */
    set.source = BRSRC_AUTO;
    watch_browser_set_settings(&set);
    sim_run(1500);
    n = watch_browser_net();
    EXPECT(n && n->src == BRSRC_MANUAL && n->state == BRN_READY && strcmp(L->last_host, "pc.example.com") == 0,
           "automático sin relay: no fue al PC (%d %s)", n ? n->src : -9, L->last_host);
    /* el usuario lo enciende desde el reloj */
    ui_nav_back();
    sim_run(300);
    EXPECT(has("Web del teléfono"), "volver a la página del relay");
    click(tap_of("Encender el servidor del teléfono"));
    sim_run(1200);
    EXPECT(sim_phone_relay_up() && st().relay.state == FLP_RELAY_UP, "relay: el botón no lo encendió");
    EXPECT(has("En marcha") && has("192.168.1.34:8765"), "relay: la página no lo enseña en marcha");
    sim_lint("relay en marcha");
    sim_save(out, "fp_relay_en_marcha");
    click(tap_of("Abrir el navegador"));
    sim_run(1500);
    n = watch_browser_net();
    EXPECT(n && n->src == BRSRC_PHONE && n->state == BRN_READY && strcmp(L->last_host, "192.168.1.34") == 0,
           "automático con relay: no volvió al teléfono (%d %s)", n ? n->src : -9, L->last_host);
    ui_nav_back();
    sim_run(400);

    /* 7) El relay falla en el teléfono: el motivo real llega a la pantalla */
    sim_phone_relay_user_stop();
    sim_run(500);
    set.source = BRSRC_PHONE;
    set.server[0] = 0;
    watch_browser_set_settings(&set);
    sim_phone_relay_mode(SIM_RELAY_FAIL);
    click(tap_of("Encender el servidor del teléfono"));
    sim_run(800);
    EXPECT(st().relay.state == FLP_RELAY_ERROR && has("sin Wi-Fi en el teléfono"), "relay con error: no se explica");
    sim_save(out, "fp_relay_error");

    /* 8) Una app que no contesta al RELAY_START: a los 15 s se dice */
    sim_phone_relay_mode(SIM_RELAY_MUTE);
    click(tap_of("Encender el servidor del teléfono"));
    sim_run(1000);
    EXPECT(st().relay.state == FLP_RELAY_STARTING, "relay mudo: no queda pidiendo (%d)", st().relay.state);
    sim_run(15500);
    EXPECT(st().relay.state == FLP_RELAY_ERROR && strstr(st().relay.err, "no contestó"),
           "relay mudo: sin motivo (%d \"%s\")", st().relay.state, st().relay.err);

    /* 9) Al salir todo se suelta */
    sim_phone_relay_mode(SIM_RELAY_OK);
    ui_nav_home();
    sim_run(800);
    EXPECT(watch_browser_net() == NULL, "relay: la sesión sigue abierta al salir");
    EXPECT(sim_mem_blocks() == blocks0, "fuga en el relay: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "relay: temporizadores %d -> %d", timers0, sim_timer_count());
    watch_phone_set_enabled(false);
    sim_run(200);
    printf("relay Flex Phone -> navegador: %s\n", sim_fails == f0 ? "OK" : "revisar");
}
