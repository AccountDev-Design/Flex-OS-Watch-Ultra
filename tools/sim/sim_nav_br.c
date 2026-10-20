/*
 * Suite "navegador" (F6): la app Navegador con la sesión FBP/1 real contra
 * el servicio falso a nivel de bytes (sim_br.c) y los JPEG de fixtures/
 * decodificados con el TJpgDec de LVGL, como en el reloj.
 */
#include <stdio.h>
#include <string.h>

#include "fbp_fake.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_browser.h"

void sim_br_reset(void);
void *sim_br_fake(void);
void sim_br_online(bool on);
void sim_br_progressive(bool on);

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

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

static bool key(const char *t)
{
    lv_obj_t *kb = find_class(ui_nav_top(), &lv_buttonmatrix_class);
    if (!kb) {
        return false;
    }
    const char *const *map = lv_buttonmatrix_get_map(kb);
    for (uint32_t i = 0, b = 0; map[i][0]; i++) {
        if (strcmp(map[i], "\n") == 0) {
            continue;
        }
        if (strcmp(map[i], t) == 0) {
            lv_buttonmatrix_set_selected_button(kb, b);
            lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
            sim_run(30);
            return true;
        }
        b++;
    }
    return false;
}

/* El campo de texto de una edición (omnibox, servidor...): se rellena y "OK" */
static bool enter_text(const char *t)
{
    lv_obj_t *top = ui_nav_top();
    lv_obj_t *ta = NULL;
    for (uint32_t i = 0; !ta && i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        ta = find_class(c, &lv_textarea_class);
    }
    if (!ta) {
        return false;
    }
    lv_textarea_set_text(ta, t);
    return key("OK");
}

/* El omnibox: arriba y ancho */
static lv_obj_t *omni(void)
{
    lv_obj_t *top = ui_nav_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if (a.y1 < 30 && lv_area_get_width(&a) > 200 && lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) {
            return c;
        }
    }
    return NULL;
}

/* El lienzo de la página (el único lv_canvas de la app) */
static lv_obj_t *canvas(void)
{
    return find_class(ui_nav_top(), &lv_canvas_class);
}

/* ¿El píxel (x,y) de la pantalla se parece a rgb? */
static bool px_like(int x, int y, uint32_t rgb)
{
    const uint16_t v = sim_fb[y * SIM_W + x];
    const int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    return LV_ABS(r - (int)(rgb >> 16)) < 24 && LV_ABS(g - (int)((rgb >> 8) & 255)) < 24 &&
           LV_ABS(b - (int)(rgb & 255)) < 24;
}

void sim_suite_browser(const char *out)
{
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    sim_phone_reset(); /* sin teléfono vinculado: el origen es solo el servidor de Ajustes */
    sim_br_reset();
    fbp_fake_t *fk = sim_br_fake();
    fbp_fake_log_t *L = fbp_fake_log(fk);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* 1) sin servidor: lo dice y lleva a Ajustes; no se abre ninguna sesión */
    ui_nav_push(ui_browser_create);
    sim_run(600);
    EXPECT(has("Navegador de Flex OS Ultra") && has("Empareja Flex Phone o configura un servidor en Ajustes"),
           "navegador: no explica que falta el servidor");
    EXPECT(watch_browser_net() == NULL && L->connects == 0, "navegador: sesión sin servidor");
    sim_lint("navegador sin servidor");
    sim_save(out, "f6_nav_sin_servidor");

    /* 2) servidor y credencial desde flex://settings (la credencial nunca se ve) */
    click(tap_of("Ajustes del navegador"));
    sim_run(400);
    EXPECT(has("Servidor del PC/Ubuntu") && has("sin configurar") && has("Origen de la web"), "ajustes del navegador");
    click(tap_of("Servidor del PC/Ubuntu"));
    sim_run(300);
    EXPECT(has("Servidor (wss://...)"), "editar servidor");
    enter_text("https://mal.example.com/");
    sim_run(300);
    EXPECT(has("Debe ser wss://host:puerto/ruta"), "servidor no válido aceptado");
    click(tap_of("Servidor del PC/Ubuntu"));
    sim_run(300);
    enter_text("wss://nav.example.com/v1/session");
    sim_run(300);
    EXPECT(has("wss://nav.example.com/v1/session"), "servidor no guardado");
    click(tap_of("Credencial del PC y la nube"));
    sim_run(300);
    enter_text("credencial-de-prueba-1234567890");
    sim_run(300);
    EXPECT(has("Credencial guardada") && has("configurada"), "credencial no guardada");
    EXPECT(!has("credencial-de-prueba-1234567890"), "la credencial se ve en pantalla");
    sim_lint("ajustes del navegador");
    sim_save(out, "f6_nav_ajustes");
    click(tap_of("Probar el servidor"));
    sim_run(200);
    EXPECT(has("Servidor accesible"), "probar el servidor");

    /* 3) la sesión: HELLO con la credencial, WELCOME y "lista" */
    sim_run(800);
    /* sin credencial el servicio contesta ERROR 1; con ella, sesión nueva */
    EXPECT(L->hellos >= 1 && strcmp(L->last_token, "credencial-de-prueba-1234567890") == 0, "sin HELLO (%d)",
           L->hellos);
    EXPECT(L->hello_w == 410 && L->hello_h == 376, "HELLO %ux%u", L->hello_w, L->hello_h);
    EXPECT(watch_browser_net() && watch_browser_net()->state == BRN_READY, "la sesión no queda lista");
    EXPECT(strcmp(L->ua, "FlexOS-Browser/1.0 (Flex OS Watch Ultra)") == 0, "User-Agent %s", L->ua);

    /* 4) omnibox: una página interna y luego una dirección */
    click(omni());
    sim_run(300);
    EXPECT(has("Dirección o búsqueda"), "omnibox: no abre la edición");
    enter_text("flex://history");
    sim_run(300);
    EXPECT(has("No hay nada en el historial"), "flex://history");
    click(omni());
    sim_run(300);
    enter_text("ejemplo.com");
    sim_run(1500);
    EXPECT(L->navigates == 1 && strcmp(L->last_url, "https://ejemplo.com") == 0, "NAVIGATE %s", L->last_url);
    br_net_t *n = watch_browser_net();
    EXPECT(n && n->stats.framesOk >= 1, "ninguna imagen dibujada");
    lv_obj_t *cv = canvas();
    EXPECT(cv && !lv_obj_has_flag(cv, LV_OBJ_FLAG_HIDDEN), "el lienzo no se ve");
    EXPECT(has("ejemplo.com"), "el omnibox no dice el sitio");
    sim_run(100);
    sim_save(out, "f6_nav_pagina");
    EXPECT(px_like(300, 68 + 48, 0x245CC4), "la cabecera azul de la página no está en el lienzo (0x%04x)",
           sim_fb[(68 + 48) * SIM_W + 300]);
    sim_lint("navegador página");

    /* 5) arrastrar = SCROLL (signo invertido); tocar = POINTER TAP en la página */
    sim_drag(200, 400, 200, 250, 8, 40);
    sim_release();
    sim_run(800);
    EXPECT(L->scrolls >= 1 && L->scroll_y >= 120 && L->scroll_y <= 170, "SCROLL: %d veces, y=%d", L->scrolls,
           L->scroll_y);
    sim_save(out, "f6_nav_desplazada");
    sim_tap(150, 68 + 100);
    sim_run(300);
    EXPECT(L->taps == 1 && L->tap_x == 150 && L->tap_y == 100, "TAP %d (%u,%u)", L->taps, L->tap_x, L->tap_y);

    /* 6) teclado para la página: las teclas van al servicio, el reloj no las guarda */
    lv_obj_t *menu = NULL;
    {
        lv_obj_t *top = ui_nav_top();
        lv_area_t a;
        for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) { /* el botón de menú: arriba a la derecha */
            lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
            lv_obj_get_coords(c, &a);
            if (a.x1 > 240 && a.y1 > 440 && lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) &&
                lv_area_get_width(&a) < 60) {
                menu = c;
            }
        }
    }
    EXPECT(menu != NULL, "sin botón de menú");
    click(menu);
    sim_run(200);
    EXPECT(has("Teclado para la página") && has("Añadir a favoritos") && has("Cerrar el navegador"), "menú");
    sim_save(out, "f6_nav_menu");
    click(tap_of("Teclado para la página"));
    sim_run(400);
    key("h");
    key("o");
    key("DEL");
    key("OK");
    sim_run(400);
    EXPECT(strcmp(L->typed, "ho") == 0 && L->last_vk == FBP_VK_ENTER, "teclas: \"%s\" vk %u", L->typed, L->last_vk);
    EXPECT(L->vp_h == 236 - 68, "el viewport no se renegoció con el teclado (%u)", L->vp_h);
    sim_save(out, "f6_nav_teclado");
    click(lv_obj_get_child(ui_nav_top(), 1)); /* atrás (el primero de abajo) cierra el teclado */
    sim_run(500);
    EXPECT(L->vp_h == 376, "el viewport no vuelve (%u)", L->vp_h);

    /* 7) destinos prohibidos: barrera del reloj */
    const int navs = L->navigates;
    click(omni());
    sim_run(200);
    enter_text("http://192.168.1.1/admin");
    sim_run(300);
    EXPECT(has("Las direcciones IP directas están bloqueadas") && L->navigates == navs, "IP privada no bloqueada");

    /* 8) favoritos e historial */
    click(menu);
    sim_run(200);
    click(tap_of("Añadir a favoritos"));
    sim_run(200);
    EXPECT(has("Añadido a favoritos"), "favorito");
    click(menu);
    sim_run(200);
    click(tap_of("Favoritos"));
    sim_run(300);
    EXPECT(has("Página de prueba"), "lista de favoritos");
    click(menu);
    sim_run(200);
    click(tap_of("Historial"));
    sim_run(300);
    EXPECT(has("Página de prueba"), "historial: sin el título de la página");
    sim_save(out, "f6_nav_historial");
    click(tap_of("Página de prueba")); /* volver a la página desde el historial */
    sim_run(1200);
    EXPECT(L->navigates == 2 && has("ejemplo.com"), "abrir desde el historial");

    /* 9) pestañas */
    click(menu);
    sim_run(200);
    click(tap_of("Nueva pestaña"));
    sim_run(300);
    EXPECT(has("Dirección o búsqueda"), "pestaña nueva: no abre el omnibox");
    enter_text("");
    sim_run(300);
    click(menu);
    sim_run(200);
    EXPECT(has("Pestañas (2)"), "no hay 2 pestañas");
    click(tap_of("Pestañas (2)"));
    sim_run(300);
    sim_save(out, "f6_nav_pestanas");
    click(tap_of("Página de prueba"));
    sim_run(1200);
    EXPECT(n->stats.framesOk >= 3, "al volver a la pestaña no se pide la página (%u)", (unsigned)n->stats.framesOk);

    /* 10) recargar; una imagen progresiva (prohibida en FBP/1) no se pinta */
    click(lv_obj_get_child(ui_nav_top(), 3)); /* recargar */
    sim_run(300);
    EXPECT(L->reloads == 1, "RELOAD");
    const uint32_t bad0 = n->stats.framesBad;
    sim_br_progressive(true);
    br_net_cmd(n, FBP_C_REQ_FRAME, 1);
    sim_run(400);
    sim_br_progressive(false);
    sim_run(400);
    EXPECT(n->stats.framesBad > bad0, "el JPEG progresivo se aceptó");
    EXPECT(n->stats.framesOk >= 4, "tras la imagen mala no se volvió a pedir la página");

    /* 11) sin Wi-Fi */
    sim_br_online(false);
    sim_run(1500);
    EXPECT(n->state == BRN_NOWIFI, "sin Wi-Fi: estado %d", n->state);
    click(omni());
    sim_run(200);
    enter_text("otra.example.org");
    sim_run(300);
    EXPECT(has("El dispositivo no está conectado a ninguna red Wi-Fi."), "sin Wi-Fi: página offline");
    sim_save(out, "f6_nav_sin_wifi");
    sim_br_online(true);

    /* 12) al salir se cierra la sesión y se recuerda la última página */
    ui_nav_home();
    sim_run(800);
    EXPECT(watch_browser_net() == NULL, "la sesión sigue abierta al salir");
    EXPECT(sim_mem_blocks() == blocks0, "fuga en el navegador: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("navegador F6: %s (%d HELLO, %d NAVIGATE, %d SCROLL, imágenes %u)\n", sim_fails ? "revisar" : "OK",
           L->hellos, L->navigates, L->scrolls, (unsigned)(n ? n->stats.framesOk : 0));
}
