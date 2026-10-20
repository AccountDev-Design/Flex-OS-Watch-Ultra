/*
 * Suite "ota" (F7): Ajustes > Actualizaciones y la instalación sobre el motor
 * real con el servidor y la flash falsos de sim_ota.c. Recorre lo que verá
 * el usuario: sin comprobar, sin Wi-Fi, 404, versión nueva con su aviso en la
 * esfera, la descarga (sin gesto "volver", pantalla encendida), el reinicio,
 * cancelar, sin clave, firma mala, batería baja, al día y el aviso tras
 * reiniciar.
 */
#include <stdio.h>
#include <string.h>

#include "ota_core.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_ota.h"

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

static bool has_in(lv_obj_t *o, const char *t)
{
    lv_obj_t *l = sim_find_label(o, t);
    while (l) {
        if (lv_obj_has_flag(l, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        l = lv_obj_get_parent(l);
    }
    return sim_find_label(o, t) != NULL;
}

static bool has(const char *t)
{
    return has_in(ui_nav_top(), t);
}

static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

/* ¿Alguna etiqueta visible de o contiene t? */
static bool has_sub(lv_obj_t *o, const char *t)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(o, &lv_label_class) && strstr(lv_label_get_text(o), t)) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (has_sub(lv_obj_get_child(o, (int32_t)i), t)) {
            return true;
        }
    }
    return false;
}

/* Lleva el reloj simulado hasta que se cumpla cond (o max_ms). */
static bool run_until(bool (*cond)(void), uint32_t max_ms)
{
    for (uint32_t t = 0; t < max_ms; t += 50) {
        if (cond()) {
            return true;
        }
        sim_run(50);
    }
    return cond();
}

static bool idle(void)
{
    return !sim_ota_busy();
}

static int pct_now(void)
{
    watch_ota_status_t s;
    watch_ota_get(&s);
    return s.pct;
}

static bool half(void)
{
    return pct_now() >= 40;
}

static bool downloading(void)
{
    watch_ota_status_t s;
    watch_ota_get(&s);
    return s.state == OTA_DOWNLOADING && s.pct >= 10;
}

/* Abre Ajustes > Actualizaciones desde la esfera. */
static void open_page(void)
{
    ui_nav_home();
    sim_run(400);
    ui_nav_push(ui_ota_create);
    sim_run(400);
}

static void shot(const char *out, const char *name)
{
    sim_lint(name);
    sim_check_glyphs(ui_nav_top(), name);
    sim_save(out, name);
}

/* Instala la versión publicada desde la página; devuelve el estado final. */
static int install_from_page(void)
{
    click(tap_of("Actualizar ahora"));
    sim_run(300);
    run_until(idle, 20000);
    sim_run(300);
    watch_ota_status_t s;
    watch_ota_get(&s);
    return s.state == OTA_ERROR ? s.err : 100 + s.state;
}

void sim_suite_ota(const char *out)
{
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    sim_ota_reset();
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* 1) Fila de Ajustes y página sin comprobar (RELOJ: no dice "al día", A8) */
    ui_nav_push(ui_settings_create);
    sim_run(400);
    lv_obj_t *row = tap_of("Actualizaciones");
    EXPECT(row != NULL, "ajustes: falta la fila Actualizaciones");
    click(row);
    sim_run(400);
    EXPECT(has("Sin comprobar todavía") && has("Flex OS Watch Ultra 0.1.0-b1"), "ota: estado inicial");
    EXPECT(tap_of("Buscar actualización") != NULL, "ota: falta «Buscar actualización»");
    EXPECT(has("Clave de firma"), "ota: no dice la clave");
    shot(out, "f7_ota_inicio");

    /* 2) Sin Wi-Fi: el error al momento, sin tocar la red (E1) */
    sim_ota_online(false);
    click(tap_of("Buscar actualización"));
    sim_run(400);
    EXPECT(has("No se pudo comprobar") && has("Sin conexión a Internet"), "ota: sin Wi-Fi");
    EXPECT(sim_ota_runs() == 0, "ota: con el Wi-Fi apagado ha usado la red");
    shot(out, "f7_ota_sin_wifi");
    sim_ota_online(true);

    /* 3) El manifiesto no está publicado (HTTP 404) */
    click(tap_of("Buscar actualización"));
    sim_run(100);
    run_until(idle, 3000);
    sim_run(400);
    EXPECT(has("Archivo no encontrado"), "ota: 404");

    /* 4) Versión nueva: versión, tamaño y novedades */
    sim_ota_publish("0.2.0", true, "Actualizaciones por Wi-Fi.\\nNavegador remoto.\\nClima con Open-Meteo.");
    click(tap_of("Buscar actualización"));
    sim_run(280);
    EXPECT(has("Buscando...") && has("Buscando actualizaciones..."), "ota: no dice que busca");
    run_until(idle, 3000);
    sim_run(400);
    EXPECT(has("Actualización disponible") && has("0.2.0") && has("600 KB"), "ota: disponible");
    EXPECT(has("Novedades") && has("Actualizaciones por Wi-Fi.\nNavegador remoto.\nClima con Open-Meteo."),
           "ota: novedades");
    EXPECT(tap_of("Actualizar ahora") && tap_of("Buscar de nuevo"), "ota: botones de disponible");
    shot(out, "f7_ota_disponible");

    /* 5) Aviso en la esfera, una vez por versión; tocarlo abre la página */
    ui_nav_home();
    sim_run(1500);
    EXPECT(ui_banner_active() && sim_find_label(lv_layer_top(), "Nueva versión 0.2.0"), "ota: no avisa en la esfera");
    sim_save(out, "f7_ota_aviso");
    sim_run(9800);
    EXPECT(!ui_banner_active(), "ota: el aviso no se cierra solo (9 s)");
    sim_run(2000);
    EXPECT(!ui_banner_active(), "ota: avisa dos veces de la misma versión");

    /* 6) Instalar: descarga con progreso; sin "volver"; pantalla encendida */
    open_page();
    click(tap_of("Actualizar ahora"));
    sim_run(300);
    lv_obj_t *inst = ui_nav_top();
    EXPECT(has("Actualizando Flex OS"), "instalar: no abre la pantalla");
    run_until(downloading, 5000);
    run_until(half, 5000);
    EXPECT(has("Descargando...") && tap_of("Cancelar"), "instalar: fase o «Cancelar»");
    EXPECT(has_sub(inst, " KB / 600 KB") && has_sub(inst, " KB/s"), "instalar: bytes o velocidad");
    shot(out, "f7_ota_descargando");
    const int depth = ui_nav_depth();
    sim_drag(2, 250, 260, 250, 10, 16);
    sim_release();
    sim_run(400);
    EXPECT(ui_nav_depth() == depth && ui_nav_top() == inst, "instalar: el gesto «volver» cierra la pantalla");
    run_until(idle, 20000);
    sim_run(300);
    EXPECT(has("Reiniciando...") && has("El reloj se reiniciará solo") && !has("Cancelar"),
           "instalar: pantalla de reinicio");
    EXPECT(sim_ota_installed("0.2.0"), "instalar: la flash no tiene la imagen o falta pend");
    shot(out, "f7_ota_reinicio");

    /* 7) Cancelar a media descarga: nada cambia (E11) */
    sim_ota_reset();
    sim_ota_publish("0.2.0", true, "Linea.");
    open_page();
    click(tap_of("Buscar actualización"));
    run_until(idle, 3000);
    sim_run(400);
    click(tap_of("Actualizar ahora"));
    sim_run(300);
    run_until(half, 8000);
    click(tap_of("Cancelar"));
    run_until(idle, 3000);
    sim_run(300);
    EXPECT(has("No se pudo actualizar") && has("Actualización cancelada") && has("El reloj no se ha modificado."),
           "cancelar: pantalla de error");
    EXPECT(sim_ota_aborted(), "cancelar: la flash no se abortó");
    shot(out, "f7_ota_error");
    /* La pantalla de instalación no deja que se apague (se atenúa a los 12 s
     * y se apaga a los 15 s en cualquier otra página). */
    ui_screen_enable(true);
    sim_run(20000);
    EXPECT(ui_screen_state() == 0 && sim_display_on, "instalar: la pantalla se apaga (estado %d)", ui_screen_state());
    sim_tap(FLEX_SCREEN_W / 2, 422); /* un toque de verdad en «Entendido»: también es actividad */
    sim_run(500);
    EXPECT(has("Último intento") && has("Actualización cancelada") && has("Actualizar ahora"),
           "cancelar: Ajustes no deja reintentar");
    sim_run(20000);
    EXPECT(!sim_display_on, "instalar: tras «Entendido» la pantalla ya no se apaga");
    ui_screen_wake();
    ui_screen_enable(false);
    sim_run(1500);

    /* 8) Errores que se ven igual: sin clave, firma mala, batería baja */
    static const struct {
        int what;
        const char *text;
    } errs[] = {
        {0, "Este reloj no tiene clave para verificar actualizaciones"},
        {1, "La firma de la actualización no es válida"},
        {2, "Batería baja: pon el reloj a cargar"},
    };
    for (size_t i = 0; i < sizeof(errs) / sizeof(errs[0]); i++) {
        sim_ota_reset();
        sim_ota_publish("0.2.0", errs[i].what != 1, NULL);
        open_page();
        click(tap_of("Buscar actualización"));
        run_until(idle, 3000);
        sim_run(400);
        if (errs[i].what == 0) {
            sim_ota_key(false);
        } else if (errs[i].what == 2) {
            sim_ota_batt(20, false);
        }
        install_from_page();
        EXPECT(has(errs[i].text) && has("El reloj no se ha modificado."), "instalar: falta «%s»", errs[i].text);
        EXPECT(!sim_ota_installed("0.2.0"), "instalar: instalada pese a «%s»", errs[i].text);
        if (errs[i].what == 1) {
            shot(out, "f7_ota_firma_mala");
        }
        click(tap_of("Entendido"));
        sim_run(400);
        if (errs[i].what == 0) {
            EXPECT(has("Ninguna"), "sin clave: Ajustes no lo dice");
            shot(out, "f7_ota_sin_clave");
        }
    }
    /* Cargando, 20 % sí vale */
    sim_ota_reset();
    sim_ota_publish("0.2.0", true, NULL);
    open_page();
    click(tap_of("Buscar actualización"));
    run_until(idle, 3000);
    sim_run(400);
    sim_ota_batt(20, true);
    EXPECT(install_from_page() == 100 + OTA_REBOOTING, "instalar: cargando al 20 %% no instala");

    /* 9) Al día (y la versión de hoy es una preliberación: 0.1.0-b1) */
    sim_ota_reset();
    sim_ota_publish("0.1.0-b1", true, NULL);
    open_page();
    click(tap_of("Buscar actualización"));
    run_until(idle, 3000);
    sim_run(400);
    EXPECT(has("El sistema está al día"), "ota: al día");
    shot(out, "f7_ota_al_dia");

    /* 10) Aviso tras reiniciar con la versión nueva, y tras una reversión */
    ui_nav_home();
    sim_run(600);
    sim_ota_boot(OTA_BOOT_UPDATED);
    sim_run(1500);
    EXPECT(ui_banner_active() && sim_find_label(lv_layer_top(), "Flex OS actualizado"), "ota: aviso de actualizado");
    sim_save(out, "f7_ota_actualizado");
    sim_run(11000);
    sim_ota_boot(OTA_BOOT_REVERTED);
    sim_run(1500);
    EXPECT(sim_find_label(lv_layer_top(), "Vuelve la versión anterior"), "ota: aviso de reversión");
    sim_run(11000);

    /* Sin fugas: páginas, temporizadores y memoria de LVGL */
    ui_nav_home();
    sim_run(600);
    sim_ota_reset();
    EXPECT(sim_timer_count() == timers0, "ota: temporizadores vivos %d (antes %d)", sim_timer_count(), timers0);
    EXPECT(sim_mem_blocks() <= blocks0 + 2, "ota: bloques de LVGL %u (antes %u)", (unsigned)sim_mem_blocks(),
           (unsigned)blocks0);
    printf("ota: OK salvo lo indicado\n");
}
