/*
 * Flex Phone (F5.2/F5.3/F5.5, F9.9, F9.10) en la vista previa, con el enlace
 * v2 REAL contra el teléfono simulado de sim_phone.c: activar el enlace,
 * teléfono sin vincular, código de emparejamiento, código equivocado,
 * código bueno y confirmación en el reloj, sesión con estado y capacidades,
 * notificaciones (lista, detalle, respuesta con resultado real, descartar),
 * aviso emergente, Música, Conexión (hacer sonar), caída y reconexión con
 * reto-respuesta, la app Android de hoy (sin v2), el mosaico Bluetooth del
 * panel rápido, integridad visual y fugas.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_phone.h"
#include "watch_theme.h"

void sim_phone_reset(void);
void sim_phone_type(const char *code);
void sim_phone_mute(bool on);
void sim_phone_drop(void);
bool sim_phone_finding(void);
void sim_phone_push(const char *app, const char *title, const char *text, bool reply);

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

/* Lo pulsable que contiene ese texto */
static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static bool enabled(const char *text)
{
    lv_obj_t *b = tap_of(text);
    return b && !lv_obj_has_state(b, LV_STATE_DISABLED);
}

/* La lista con desplazamiento de una página (la cabecera va antes) */
static lv_obj_t *list_of(lv_obj_t *page)
{
    for (int32_t i = (int32_t)lv_obj_get_child_count(page) - 1; i >= 0; i--) {
        lv_obj_t *c = lv_obj_get_child(page, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_style_layout(c, 0) == LV_LAYOUT_FLEX) {
            return c;
        }
    }
    return NULL;
}

static watch_phone_status_t st(void)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    return s;
}

void sim_suite_phone(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_phone_reset();
    sim_set_time(WATCH_TIME_OK, 2026, 10, 16, 18, 30, 0);
    sim_run(600);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* Lanzador: la app Flex Phone y Música con sus iconos originales */
    ui_nav_push(ui_apps_create);
    sim_run(300);
    EXPECT(ui_apps_find(ui_nav_top(), "Flex Phone") && ui_apps_find(ui_nav_top(), "Música"),
           "cajon: faltan Flex Phone o Musica");
    ui_nav_back();
    sim_run(300);

    /* Apagado: lo dice y ofrece activarlo */
    ui_nav_push(ui_flexphone_create);
    sim_run(400);
    EXPECT(has("Sin teléfono vinculado") && has("Apagado") && has("Activar el enlace"),
           "Flex Phone: portada apagada incompleta");
    EXPECT(has("--"), "Flex Phone: sin datos debe enseñar --, no números");
    sim_lint("flexphone apagado");
    sim_save(out, "f5_flexphone_apagado");

    /* Activar: busca, conecta por BLE, saluda; el teléfono no está vinculado */
    click(tap_of("Activar el enlace"));
    sim_run(300);
    EXPECT(st().link == FLP_LS_SEARCHING && has("Buscando"), "no dice que busca");
    sim_run(1200);
    watch_phone_status_t s = st();
    EXPECT(s.link == FLP_LS_CONNECTING, "sin vincular: estado %d", s.link);
    EXPECT(has("Emparejar teléfono") && sim_find_label(ui_nav_top(), s.err), "sin vincular: falta motivo o botón");
    sim_lint("flexphone sin vincular");
    sim_save(out, "f5_flexphone_sin_vincular");

    /* Emparejar: código de 6 cifras con su plazo, esperando al teléfono */
    click(tap_of("Emparejar teléfono"));
    sim_run(300);
    s = st();
    EXPECT(strlen(s.code) == 6 && s.link == FLP_LS_PAIRING, "emparejar: sin código");
    char shown[16];
    snprintf(shown, sizeof(shown), "%.3s %.3s", s.code, s.code + 3);
    EXPECT(has("Teclea este código en el teléfono") && has(shown) && has("Caduca en 2:00"),
           "emparejar: tarjeta del código incompleta (%s)", shown);
    EXPECT(has("Esperando al teléfono") && has("Confirmar aquí") && has("Cancelar"), "emparejar: faltan estado o botones");
    sim_lint("flexphone código");
    sim_save(out, "f5_flexphone_codigo");

    /* Código equivocado en el teléfono: se dice y el código NO cambia */
    char wrong[7];
    snprintf(wrong, sizeof(wrong), "%s", strcmp(s.code, "000000") ? "000000" : "111111");
    char code[7];
    snprintf(code, sizeof(code), "%s", s.code);
    sim_phone_type(wrong);
    sim_run(400);
    s = st();
    EXPECT(strcmp(s.code, code) == 0 && has("el código tecleado en el teléfono no coincide"),
           "código equivocado: no se explica o el código cambió");
    /* Código bueno: verificado en el teléfono, falta confirmar en el reloj */
    sim_phone_type(code);
    sim_run(400);
    EXPECT(st().pair_peer_ok && has("Código verificado en el teléfono: confirma aquí"),
           "código bueno: no se ve verificado");
    EXPECT(st().link == FLP_LS_PAIRING, "se emparejó sin confirmar en el reloj");
    click(tap_of("Confirmar aquí"));
    sim_run(800);
    s = st();
    EXPECT(s.link == FLP_LS_READY && s.bonded, "confirmar: sin sesión (%d)", s.link);
    EXPECT(has("Conectado") && has("Pixel 8") && has("76 %") && has("Móvil"), "conectado: faltan datos del teléfono");
    EXPECT(s.code[0] == 0 && !has("Teclea este código en el teléfono"), "el código sigue tras emparejar");
    EXPECT(s.notif_count == 3, "llegaron %u notificaciones, esperadas 3", s.notif_count);
    sim_run(9500); /* latido: la latencia medida aparece */
    EXPECT(st().rtt_ms != 0xFFFF && !has("--"), "latencia sin medir tras el latido");
    sim_lint("flexphone conectado");
    sim_save(out, "f5_flexphone_conectado");

    /* Notificaciones: más reciente primero; lo sensible no se enseña */
    click(tap_of("Notificaciones"));
    sim_run(400);
    EXPECT(has("WhatsApp") && has("Ana") && has("¿Quedamos a las 8 en la plaza?"), "lista: falta WhatsApp");
    EXPECT(has("Contenido sensible: míralo en el teléfono") && !has("Tu código es 482913"),
           "lista: el código bancario se ve");
    EXPECT(has("Gmail") && has("Hace 2 min") && has("Borrar el historial"), "lista: faltan Gmail, hora o borrar");
    lv_obj_t *nl = list_of(ui_nav_top());
    lv_obj_t *first = nl ? lv_obj_get_child(nl, 0) : NULL;
    EXPECT(first && sim_find_label(first, "Ana"), "lista: la más reciente no va primero");
    sim_lint("notificaciones");
    sim_save(out, "f5_notificaciones");

    /* Detalle con las acciones de Android; Responder con resultado REAL */
    click(tap_of("Ana"));
    sim_run(400);
    EXPECT(has("Marcar como leído") && has("Responder") && has("Descartar"), "detalle: faltan acciones");
    sim_lint("notificación");
    sim_save(out, "f5_notificacion");
    click(tap_of("Responder"));
    sim_run(300);
    EXPECT(has("Vale") && has("Voy de camino"), "responder: sin respuestas rápidas");
    const uint32_t ok0 = st().replies_ok;
    click(tap_of("Vale"));
    sim_run(400);
    EXPECT(st().replies_ok == ok0 + 1 && has("Entregada a la app del teléfono"), "responder: sin resultado real");
    sim_lint("responder");
    sim_save(out, "f5_responder");
    ui_nav_back();
    sim_run(300);
    click(tap_of("Marcar como leído"));
    sim_run(300);
    EXPECT(has("Enviado al teléfono"), "acción: no se confirma");
    click(tap_of("Descartar"));
    sim_run(400);
    EXPECT(!has("Ana") && st().notif_count == 2, "descartar: sigue en la lista");
    ui_nav_back();
    sim_run(300);

    /* Aviso emergente en la esfera; tocarlo abre el detalle */
    ui_nav_home();
    sim_run(400);
    sim_phone_push("WhatsApp", "Ana", "Perfecto, nos vemos allí", true);
    sim_run(500);
    EXPECT(ui_banner_active(), "no salta el aviso de la notificación");
    lv_obj_t *bt = sim_find_label(lv_layer_top(), "Ana: Perfecto, nos vemos allí");
    /* El enlace simulado entrega la notificación con un retardo variable: se
     * revisa el aviso ya quieto (al final de su entrada de 220 ms) */
    for (int k = 0, y = INT32_MIN; bt && k < 20; k++) {
        const int32_t y1 = lv_obj_get_y(lv_obj_get_parent(bt));
        if (y1 == y) {
            break;
        }
        y = y1;
        sim_run(50);
    }
    EXPECT(bt != NULL, "aviso: falta el texto");
    sim_lint("aviso notificación");
    sim_save(out, "f5_aviso");
    while (bt && !lv_obj_has_flag(bt, LV_OBJ_FLAG_CLICKABLE)) {
        bt = lv_obj_get_parent(bt);
    }
    click(bt);
    sim_run(600);
    EXPECT(has("Perfecto, nos vemos allí") && has("Responder"), "aviso: tocar no abre el detalle");
    EXPECT(!ui_banner_active(), "aviso: no se cierra al tocar");
    ui_nav_home();
    sim_run(300);

    /* Música: lo que suena en el teléfono y su mando */
    ui_nav_push(ui_music_create);
    sim_run(400);
    EXPECT(has("Bohemian Rhapsody") && has("Queen") && has("Sonando") && has("En el teléfono · Spotify"),
           "música: no enseña lo que suena");
    sim_lint("música");
    sim_save(out, "f9_musica");
    lv_obj_t *play = NULL;
    /* el botón grande (104 px) es el de reproducir/pausa */
    lv_obj_t *list = list_of(ui_nav_top());
    for (uint32_t i = 0; list && i < lv_obj_get_child_count(list); i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t)i);
        for (uint32_t j = 0; j < lv_obj_get_child_count(row); j++) {
            lv_obj_t *b = lv_obj_get_child(row, (int32_t)j);
            if (lv_obj_get_width(b) == 104 && lv_obj_has_flag(b, LV_OBJ_FLAG_CLICKABLE)) {
                play = b;
            }
        }
    }
    click(play);
    sim_run(400);
    EXPECT(has("En pausa"), "música: pausa no llega al teléfono");
    click(play);
    sim_run(400);
    EXPECT(has("Sonando"), "música: reproducir no llega al teléfono");
    ui_nav_back();
    sim_run(300);

    /* Estado del teléfono y capacidades */
    ui_nav_push(ui_phone_state_create);
    sim_run(400);
    EXPECT(has("Pixel 8") && has("Android 15") && has("Disponible"), "estado del teléfono incompleto");
    sim_lint("estado del teléfono");
    sim_save(out, "f5_estado_telefono");
    ui_nav_back();
    sim_run(300);

    /* Conexión: hacer sonar el teléfono (con ACK) y pararlo */
    ui_nav_push(ui_phone_conn_create);
    sim_run(400);
    EXPECT(has("Bluetooth LE") && has("Conectado por Bluetooth"), "conexión: falta el canal");
    EXPECT(enabled("Hacer sonar el teléfono"), "conexión: no deja hacer sonar");
    click(tap_of("Hacer sonar el teléfono"));
    sim_run(300);
    EXPECT(sim_phone_finding() && has("Parar el sonido"), "hacer sonar: no llega");
    click(tap_of("Parar el sonido"));
    sim_run(300);
    EXPECT(!sim_phone_finding(), "parar el sonido: no llega");
    sim_lint("conexión");
    sim_save(out, "f5_conexion");

    /* Caída: estado instantáneo fuera, historial dentro; vuelve con reto, sin código */
    sim_phone_drop();
    sim_run(300);
    s = st();
    EXPECT(s.link == FLP_LS_SEARCHING && !s.phone.valid && s.notif_count == 3,
           "caída: estado %d, teléfono %d, %u notificaciones", s.link, s.phone.valid, s.notif_count);
    EXPECT(!enabled("Hacer sonar el teléfono"), "caída: sigue ofreciendo hacer sonar");
    sim_run(1800);
    s = st();
    EXPECT(s.link == FLP_LS_READY && s.code[0] == 0 && s.phone.valid, "reconexión: no vuelve sola (%d)", s.link);

    /* Olvidar (dos toques) y la app Android de hoy, sin servidor v2 */
    click(tap_of("Olvidar el teléfono"));
    sim_run(100);
    EXPECT(has("Toca otra vez para olvidar"), "olvidar: sin confirmación");
    click(tap_of("Toca otra vez para olvidar"));
    sim_run(300);
    EXPECT(!st().bonded && st().notif_count == 0, "olvidar: queda el vínculo o sus notificaciones");
    ui_nav_back();
    sim_run(300);
    sim_phone_mute(true);
    sim_phone_drop();
    ui_nav_push(ui_flexphone_create);
    sim_run(1600);
    sim_run(FLP_LINK_HELLO_TIMEOUT_MS + 500);
    s = st();
    EXPECT(s.link == FLP_LS_CONNECTING && strstr(s.err, "saludo"), "app sin v2: no se explica (%s)", s.err);
    EXPECT(sim_find_label(ui_nav_top(), s.err) != NULL, "app sin v2: el motivo no está en la portada");
    sim_lint("flexphone app sin v2");
    sim_save(out, "f5_flexphone_app_sin_v2");
    sim_phone_mute(false);

    /* Panel rápido: el mosaico Bluetooth enciende y apaga el enlace */
    ui_nav_home();
    sim_run(300);
    ui_qp_open();
    sim_run(600);
    lv_obj_t *qp = ui_qp_obj();
    lv_obj_t *bl = qp ? sim_find_label(qp, "Bluetooth") : NULL;
    lv_obj_t *tile = bl ? lv_obj_get_parent(bl) : NULL;
    EXPECT(tile && lv_obj_has_state(tile, LV_STATE_CHECKED) && !lv_obj_has_state(tile, LV_STATE_DISABLED),
           "panel: Bluetooth no está activo");
    click(tile);
    sim_run(400);
    EXPECT(tile && !lv_obj_has_state(tile, LV_STATE_CHECKED) && sim_find_label(tile, "Apagado") &&
               st().link == FLP_LS_OFF,
           "panel: no apaga el enlace");
    sim_lint("panel con bluetooth");
    sim_save(out, "f5_panel_bluetooth");
    click(tile);
    sim_run(400);
    EXPECT(tile && lv_obj_has_state(tile, LV_STATE_CHECKED) && st().enabled, "panel: no enciende el enlace");
    click(tile);
    sim_run(300);
    ui_qp_close();
    sim_run(800);

    ui_nav_home();
    sim_run(1200);
    EXPECT(sim_mem_blocks() == blocks0, "fuga en Flex Phone: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("telefono F5.2/F5.3/F5.5: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
