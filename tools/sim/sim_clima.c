/*
 * Suite "clima" (F9.8): la app Clima sobre el motor y el parser reales con
 * la red simulada de sim_wx.c. Recorre lo que hará el usuario: estado vacío,
 * buscar y añadir una ciudad, el pronóstico, la antigüedad, un fallo de red,
 * sin Wi-Fi, varias ubicaciones, borrar y la caché tras un reinicio.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "wx_core.h"

const wx_core_t *sim_wx_core(void);

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

/* ¿Algún texto de la página empieza por p? */
static bool has_prefix_in(lv_obj_t *o, const char *p)
{
    if (lv_obj_check_type(o, &lv_label_class) && strncmp(lv_label_get_text(o), p, strlen(p)) == 0) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (has_prefix_in(lv_obj_get_child(o, (int32_t)i), p)) {
            return true;
        }
    }
    return false;
}

static bool has_prefix(const char *p)
{
    return has_prefix_in(ui_nav_top(), p);
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

static void type(const char *cap, const char *rest)
{
    key("ABC");
    key(cap);
    for (const char *c = rest; *c; c++) {
        const char s[2] = {*c, 0};
        key(s);
    }
}

static lv_obj_t *list_of_top(void)
{
    /* la lista desplazable de la página (watch_theme_list) */
    lv_obj_t *top = ui_nav_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (lv_obj_get_scroll_dir(c) == LV_DIR_VER && lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE)) {
            return c;
        }
    }
    return NULL;
}

void sim_suite_weather(const char *out)
{
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    sim_wx_reset();
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* 1) Sin ubicaciones: estado vacío de la referencia, sin tocar la red */
    ui_nav_push(ui_weather_create);
    sim_run(5000);
    EXPECT(has("Sin datos meteorológicos") && has("Añade una ciudad para ver su clima"), "clima: estado vacío");
    EXPECT(tap_of("Añadir ubicación") != NULL, "clima: falta «Añadir ubicación»");
    EXPECT(sim_wx_fetches() == 0, "clima: descarga sin ubicación (%d)", sim_wx_fetches());
    sim_lint("clima vacío");
    sim_save(out, "f9_clima_vacio");

    /* 2) Buscar una ciudad con el teclado del sistema */
    click(tap_of("Añadir ubicación"));
    sim_run(500);
    EXPECT(has("Buscar") && find_class(ui_nav_top(), &lv_buttonmatrix_class), "buscar: sin campo o teclado");
    type("M", "ad");
    key("OK");
    sim_run(100);
    EXPECT(has("Buscando..."), "buscar: no dice que está buscando");
    sim_run(1200);
    EXPECT(has("Comunidad de Madrid, España") && has("Cundinamarca, Colombia"), "buscar: resultados (\\u00f1)");
    EXPECT(sim_wx_searches() == 1, "buscar: %d búsquedas", sim_wx_searches());
    sim_lint("clima buscar");
    sim_check_glyphs(ui_nav_top(), "clima buscar");
    sim_save(out, "f9_clima_buscar");

    /* 3) Elegir el resultado: vuelve a la app, carga y pinta el pronóstico */
    lv_obj_t *page_wx = ui_nav_page(ui_nav_depth() - 2);
    click(tap_of("Comunidad de Madrid, España"));
    sim_run(300);
    EXPECT(ui_nav_top() == page_wx, "buscar: no vuelve a Clima");
    EXPECT(has("Cargando el pronóstico..."), "clima: no dice que carga");
    sim_run(1200);
    EXPECT(has("Madrid") && has("Comunidad de Madrid, España"), "clima: falta la ciudad");
    EXPECT(has("Pronóstico") && has("Humedad") && has("Índice UV") && has("Presión"), "clima: secciones");
    EXPECT(has("Datos meteorológicos: Open-Meteo") && has("Actualizado ahora"), "clima: pie");
    EXPECT(has("No te pierdas el atardecer"), "clima: tarjeta del sol");
    EXPECT(sim_wx_fetches() == 1, "clima: %d descargas", sim_wx_fetches());
    const wx_core_t *c = sim_wx_core();
    EXPECT(c->have && c->data.hour_count == WX_HOURS && c->data.day_count == WX_DAYS, "clima: %d h, %d días",
           c->data.hour_count, c->data.day_count);
    EXPECT(c->data.hours[5].pop == WX_NOVAL_U8, "clima: el null de la API se volvió un número");
    sim_lint("clima");
    sim_check_glyphs(ui_nav_top(), "clima");
    sim_save(out, "f9_clima");
    lv_obj_t *list = list_of_top();
    EXPECT(list != NULL, "clima: sin lista");
    if (list) {
        lv_obj_scroll_to_y(list, 362, LV_ANIM_OFF);
        sim_run(200);
        sim_save(out, "f9_clima_dias");
        lv_obj_scroll_to_y(list, 980, LV_ANIM_OFF);
        sim_run(200);
        sim_save(out, "f9_clima_detalle");
        lv_obj_scroll_to_y(list, 4000, LV_ANIM_OFF); /* hasta el final */
        sim_run(200);
        EXPECT(lv_obj_get_scroll_bottom(list) <= 0, "clima: la lista no llega al pie");
        sim_save(out, "f9_clima_pie");
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    }

    /* 4) La antigüedad es real; abrir de nuevo antes de 5 min no descarga */
    sim_run(61000);
    EXPECT(has("Actualizado hace 1 min"), "clima: antigüedad");
    ui_nav_back();
    sim_run(400);
    ui_nav_push(ui_weather_create);
    sim_run(1500);
    EXPECT(sim_wx_fetches() == 1, "clima: reabrir descargó otra vez (%d)", sim_wx_fetches());

    /* 5) "Actualizar ahora" fuerza; un fallo deja los datos y dice cuál */
    sim_wx_fail(WXE_TIMEOUT);
    click(tap_of("Actualizar ahora"));
    sim_run(600); /* la página mira el motor cada 500 ms */
    EXPECT(has("Actualizando..."), "clima: no dice que actualiza");
    sim_run(1500);
    EXPECT(has("Madrid") && has_prefix("Actualizado hace 1 min  ·  "), "clima: fallo sin aviso o sin datos");
    EXPECT(has("Actualizado hace 1 min  ·  Sin conexión"), "clima: sin Wi-Fi en la vista previa se dice así");
    EXPECT(c->err == WXE_TIMEOUT && wx_core_status(c) == WXS_STALE, "clima: estado tras el fallo");
    EXPECT(c->next_ms - lv_tick_get() <= WX_RETRY_MS + 1000, "clima: reintento a los 2 min");
    sim_lint("clima con fallo");
    sim_wx_fail(WXE_NONE);

    /* 6) Mis ubicaciones: añadir otra desde aquí vuelve hasta la portada */
    click(tap_of("Mis ubicaciones"));
    sim_run(500);
    EXPECT(has("Mis ubicaciones") && has("Madrid") && tap_of("Quitar"), "ubicaciones: lista");
    sim_lint("clima ubicaciones");
    click(tap_of("Añadir ubicación"));
    sim_run(500);
    type("L", "ima");
    key("OK");
    sim_run(1500);
    EXPECT(has("Lima, Perú"), "buscar: Lima");
    click(tap_of("Lima, Perú"));
    sim_run(1500);
    EXPECT(ui_nav_top() == ui_nav_page(ui_nav_depth() - 1) && has("Lima") && !has("Mis ubicaciones  "),
           "añadir: no vuelve a la portada");
    sim_run(1500);
    EXPECT(c->nloc == 2 && c->sel == 1 && has("Lima") && has("Lima, Perú"), "añadir: %d ubicaciones", c->nloc);
    EXPECT(!has("Comunidad de Madrid, España"), "añadir: se ven datos de la otra ciudad");
    click(tap_of("Mis ubicaciones"));
    sim_run(500);
    sim_save(out, "f9_clima_ubicaciones");
    /* elegir Madrid vuelve y la descarga es de Madrid */
    click(tap_of("Comunidad de Madrid, España"));
    sim_run(2000);
    EXPECT(c->sel == 0 && has("Comunidad de Madrid, España"), "elegir: no cambia a Madrid");

    /* 7) Quitar pide confirmación y la elegida sigue siendo la misma */
    click(tap_of("Mis ubicaciones"));
    sim_run(500);
    click(tap_of("Quitar")); /* la primera fila: Madrid */
    sim_run(100);
    EXPECT(has("¿Seguro?") && c->nloc == 2, "quitar: sin confirmación");
    click(tap_of("¿Seguro?"));
    sim_run(600);
    EXPECT(c->nloc == 1 && strcmp(c->locs[0].name, "Lima") == 0 && c->sel == 0, "quitar: %d", c->nloc);
    ui_nav_back();
    sim_run(2000);
    EXPECT(has("Lima, Perú"), "quitar: la portada no enseña Lima");

    /* 8) Sin Wi-Fi y "reinicio": la caché vuelve marcada, sin descargar */
    sim_wx_online(false);
    const int f0 = sim_wx_fetches();
    sim_wx_reboot();
    ui_nav_back();
    sim_run(400);
    ui_nav_push(ui_weather_create);
    sim_run(6000);
    EXPECT(has("Lima") && has_prefix("Actualizado hace"), "caché: no vuelve tras el reinicio");
    EXPECT(has_prefix("Actualizado hace") && wx_core_status(c) == WXS_STALE, "caché: no se marca");
    EXPECT(sim_wx_fetches() == f0, "caché: sin Wi-Fi se intentó descargar");
    sim_lint("clima caché");
    sim_save(out, "f9_clima_cache");
    sim_wx_online(true);

    ui_nav_home();
    sim_run(800);
    EXPECT(sim_mem_blocks() == blocks0, "fuga en Clima: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    printf("clima: OK (%d descargas, %d búsquedas)\n", sim_wx_fetches(), sim_wx_searches());
}
