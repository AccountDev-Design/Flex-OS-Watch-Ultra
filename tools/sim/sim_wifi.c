/*
 * Wi-Fi (F5.1) en la vista previa, con el servicio simulado de sim_net.c:
 * encender, buscar, contraseña incorrecta, contraseña buena escrita con el
 * teclado propio, hora por Internet, señal perdida y reconexión, olvidar una
 * red, apagar, el mosaico del panel rápido, integridad visual y fugas.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_net.h"
#include "watch_theme.h"

int sim_lint(const char *where);
void sim_net_reset(void);
void sim_net_drop(void);

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (lv_obj_check_type(o, cls)) {
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

/* La fila pulsable que contiene ese texto (la última, si hay varias). */
static lv_obj_t *row_of(const char *text, int nth)
{
    lv_obj_t *l = sim_find_label_n(ui_nav_top(), text, nth);
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

static bool has(const char *t)
{
    return sim_find_label(ui_nav_top(), t) != NULL;
}

/* Pulsa la tecla con ese texto en el teclado de la contraseña. */
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

static void type(const char *s)
{
    for (; *s; s++) {
        const char k[2] = {*s, 0};
        if (!key(k)) {
            /* otra página: dígitos en "123", minúsculas en "abc" */
            if (*s >= '0' && *s <= '9') {
                key("123");
            } else {
                key("abc");
            }
            EXPECT(key(k), "teclado: no hay tecla %s", k);
        }
    }
}

void sim_suite_wifi(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_net_reset();
    sim_run(600);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* Ajustes muestra el estado del Wi-Fi y abre la página */
    ui_nav_push(ui_settings_create);
    sim_run(400);
    EXPECT(has("Apagado"), "Ajustes no muestra el Wi-Fi apagado");
    click(row_of("Wi-Fi", 0));
    sim_run(400);
    EXPECT(has("Estado") && has("Enciende el Wi-Fi para buscar redes. Apagado no gasta batería."),
           "Wi-Fi: no muestra el estado apagado");
    lv_obj_t *sw = find_class(ui_nav_top(), &lv_switch_class);
    EXPECT(sw && !lv_obj_has_state(sw, LV_STATE_CHECKED), "Wi-Fi: interruptor encendido sin estarlo");
    sim_lint("wifi apagado");
    sim_save(out, "f5_wifi_apagado");

    /* Encender: busca y lista las redes (sin guardadas, no conecta solo) */
    click(sw ? lv_obj_get_parent(sw) : NULL);
    sim_run(600);
    EXPECT(has("Buscando redes"), "Wi-Fi: no dice que busca");
    sim_run(1200);
    EXPECT(sw && lv_obj_has_state(sw, LV_STATE_CHECKED), "Wi-Fi: el interruptor no se enciende");
    EXPECT(has("Casa") && has("Vecino") && has("Cafe libre") && has("Sin conexión"), "Wi-Fi: no lista las redes");

    /* Contraseña incorrecta */
    click(row_of("Casa", 0));
    sim_run(400);
    EXPECT(has("Contraseña") && has("Casa"), "Wi-Fi: la red segura no pide contraseña");
    type("abc");
    lv_obj_t *ta = find_class(ui_nav_top(), &lv_textarea_class);
    EXPECT(ta && strcmp(lv_textarea_get_text(ta), "abc") == 0, "teclado: no escribe");
    EXPECT(key("OK"), "teclado: sin OK");
    sim_run(400);
    EXPECT(has("Conectando a Casa"), "Wi-Fi: no muestra que conecta");
    sim_run(1500);
    EXPECT(has("Contraseña incorrecta"), "Wi-Fi: no dice contraseña incorrecta");

    /* Contraseña buena: minúsculas, números, mayúscula de una letra, borrar */
    click(row_of("Casa", 0));
    sim_run(400);
    type("clavx");
    key("DEL");
    type("e1234");
    ta = find_class(ui_nav_top(), &lv_textarea_class);
    EXPECT(ta && strcmp(lv_textarea_get_text(ta), "clave1234") == 0, "teclado: texto %s",
           ta ? lv_textarea_get_text(ta) : "-");
    EXPECT(ta && lv_textarea_get_password_mode(ta), "contraseña visible sin pedirlo");
    click(row_of("Ver", 0));
    EXPECT(ta && !lv_textarea_get_password_mode(ta) && has("Ocultar"), "Ver no muestra la contraseña");
    key("abc");
    key("ABC");
    sim_lint("contraseña wifi");
    sim_check_glyphs(ui_nav_top(), "contraseña wifi");
    sim_save(out, "f5_wifi_contrasena");
    key("abc");
    EXPECT(key("OK"), "teclado: sin OK");
    sim_run(1600);
    EXPECT(has("Conectado a Casa") && has("192.168.1.50"), "Wi-Fi: no conecta con la contraseña buena");
    EXPECT(has("Conectada") && has("Redes guardadas"), "Wi-Fi: la red no se marca ni se guarda");
    sim_run(1200);
    EXPECT(has("Sincronizada"), "Wi-Fi: la hora no se sincroniza");
    sim_lint("wifi conectado");
    sim_save(out, "f5_wifi");

    /* Señal perdida: lo dice, reintenta y vuelve */
    sim_net_drop();
    sim_run(500);
    EXPECT(has("Señal perdida · 1 s"), "Wi-Fi: no dice que reintenta");
    sim_run(2800);
    EXPECT(has("Conectado a Casa"), "Wi-Fi: no se reconecta");

    /* Olvidar (dos toques) */
    lv_obj_t *fr = row_of("Olvidar", 0);
    click(fr);
    sim_run(100);
    EXPECT(has("Toca para olvidar"), "olvidar: sin aviso");
    click(fr);
    sim_run(700);
    EXPECT(!has("Redes guardadas") && !has("Conectado a Casa"), "olvidar: la red sigue guardada");

    /* Apagar */
    sw = find_class(ui_nav_top(), &lv_switch_class);
    click(sw ? lv_obj_get_parent(sw) : NULL);
    sim_run(700);
    EXPECT(has("Apagado") && !has("Casa"), "Wi-Fi: no se apaga");
    ui_nav_home();
    sim_run(500);

    /* Panel rápido: el mosaico enciende y apaga, y dice el estado */
    ui_qp_open();
    sim_run(600);
    lv_obj_t *qp = ui_qp_obj();
    lv_obj_t *wl = qp ? sim_find_label(qp, "Wi-Fi") : NULL;
    lv_obj_t *tile = wl ? lv_obj_get_parent(wl) : NULL;
    EXPECT(tile && !lv_obj_has_state(tile, LV_STATE_DISABLED) && sim_find_label(tile, "Apagado"),
           "panel: el mosaico Wi-Fi no funciona");
    click(tile);
    EXPECT(tile && lv_obj_has_state(tile, LV_STATE_CHECKED), "panel: el mosaico no responde al toque");
    sim_run(1100);
    EXPECT(tile && lv_obj_has_state(tile, LV_STATE_CHECKED) && sim_find_label(tile, "Buscando redes"),
           "panel: el mosaico no enciende el Wi-Fi");
    sim_lint("panel con wifi");
    click(tile);
    EXPECT(tile && !lv_obj_has_state(tile, LV_STATE_CHECKED), "panel: el mosaico no apaga al toque");
    sim_run(1100);
    EXPECT(tile && !lv_obj_has_state(tile, LV_STATE_CHECKED) && sim_find_label(tile, "Apagado"),
           "panel: el mosaico no apaga");
    ui_qp_close();
    sim_run(800);

    ui_nav_home();
    sim_run(1200);
    EXPECT(sim_mem_blocks() == blocks0, "fuga en Wi-Fi: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("wifi F5.1: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
