/*
 * Fecha y hora (F4.2) en la vista previa: formato 12/24 h en la esfera, el
 * panel y Ajustes; zona horaria (cambia lo que se ve, no la hora UTC);
 * poner en hora con los rodillos (UTC correcto, resultado real del
 * servicio, error y reintento); y la integridad visual de las páginas.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "ui_internal.h"
#include "ui_logic.h"
#include "watch_theme.h"
#include "watch_time.h"

int sim_lint(const char *where);
int sim_check_residue(const char *where);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

/* Toca la fila que contiene ese texto (desplazándola a la vista). */
static bool tap_row(const char *text)
{
    lv_obj_t *row = clickable_of(sim_find_label(ui_nav_top(), text));
    if (!row) {
        return false;
    }
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    sim_run(50);
    lv_area_t a;
    lv_obj_get_coords(row, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    sim_run(400);
    return true;
}

static lv_obj_t *find_roller(lv_obj_t *o, int idx, int *n)
{
    if (lv_obj_check_type(o, &lv_roller_class)) {
        if ((*n)++ == idx) {
            return o;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_roller(lv_obj_get_child(o, (int32_t)i), idx, n);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *roller_n(int idx)
{
    int n = 0;
    return find_roller(ui_nav_top(), idx, &n);
}

static void lint_both(const char *name)
{
    char w[64];
    snprintf(w, sizeof(w), "%s (arriba)", name);
    sim_lint(w);
    for (uint32_t i = 0; i < lv_obj_get_child_count(ui_nav_top()); i++) {
        lv_obj_t *c = lv_obj_get_child(ui_nav_top(), (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(c) > 0) {
            lv_obj_scroll_to_y(c, LV_COORD_MAX, LV_ANIM_OFF);
            sim_run(50);
            snprintf(w, sizeof(w), "%s (final)", name);
            sim_lint(w);
            lv_obj_scroll_to_y(c, 0, LV_ANIM_OFF);
            sim_run(50);
        }
    }
}

void sim_suite_datetime(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(600);
    sim_ws_reset(NULL);
    watch_time_set_tz(-300);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 21, 41, 0); /* 21:41 local (Lima) */
    sim_scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1100);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    EXPECT(sim_find_label(ui_nav_page(0), "21:41"), "la esfera no muestra 21:41 en 24 h");

    /* ---- Ajustes > Fecha y hora ---- */
    EXPECT(ui_nav_push(ui_settings_create), "no se abrio Ajustes");
    sim_run(1300);
    EXPECT(sim_find_label(ui_nav_top(), "21:41"), "Ajustes no muestra la hora");
    EXPECT(tap_row("Fecha y hora"), "no hay fila Fecha y hora");
    EXPECT(sim_find_label(ui_nav_top(), "jue, 15 oct"), "Fecha y hora no muestra la hora actual");
    EXPECT(sim_find_label(ui_nav_top(), "UTC-05:00"), "no muestra la zona por defecto");
    EXPECT(sim_find_label(ui_nav_top(), "Sí"), "no muestra el formato de 24 h");
    EXPECT(sim_find_label(ui_nav_top(), "Pendiente"), "la sincronizacion por Internet no esta pendiente");
    sim_check_glyphs(ui_nav_top(), "fecha y hora");
    lint_both("fecha y hora");
    sim_save(out, "f4_fecha_hora");

    /* Formato 12 h: la fila, la esfera (9:41 + PM) y el panel ("9:41 PM") */
    EXPECT(tap_row("Formato 24 h"), "no hay fila Formato 24 h");
    watch_settings_t ws;
    watch_settings_get(&ws);
    EXPECT(!ws.h24, "el formato 12 h no se guardo");
    sim_run(1100);
    EXPECT(sim_find_label(ui_nav_top(), "9:41 PM"), "la fila no pasa a 12 h");
    ui_nav_home();
    sim_run(1500);
    EXPECT(sim_find_label(ui_nav_page(0), "9:41") && sim_find_label(ui_nav_page(0), "PM"),
           "la esfera no muestra 9:41 PM");
    sim_lint("esfera 12 h");
    sim_save(out, "f4_esfera_12h");
    sim_set_time(WATCH_TIME_OK, 2026, 9, 30, 12, 59, 0); /* texto más ancho: 12:59 PM, mié, 30 sept */
    sim_run(1100);
    sim_lint("esfera 12 h ancha");
    ui_qp_open();
    sim_run(400);
    EXPECT(sim_find_label(ui_qp_obj(), "12:59 PM"), "el panel no muestra 12:59 PM");
    lv_obj_t *qd = sim_find_label(ui_qp_obj(), "mié, 30 sep");
    EXPECT(!qd || lv_obj_has_flag(qd, LV_OBJ_FLAG_HIDDEN), "la fecha del panel no cabe y sigue visible");
    sim_lint("panel rapido 12 h");
    sim_save(out, "f4_panel_12h");
    ui_qp_close();
    sim_run(500);
    ws.h24 = true;
    watch_settings_update(&ws);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 21, 41, 0);
    sim_run(1100);

    /* ---- Zona horaria: +1 h (Lagos) -> 21:41 en Lima = 03:41 del 16 ---- */
    ui_nav_push(ui_datetime_create);
    sim_run(400);
    EXPECT(tap_row("Zona horaria"), "no hay fila Zona horaria");
    lv_obj_t *lima = sim_find_label(ui_nav_top(), "Lima, Bogotá");
    EXPECT(lima, "la lista de zonas no tiene Lima");
    if (lima) {
        /* la zona elegida se ve al abrir, sin desplazar a mano */
        lv_area_t a;
        lv_obj_get_coords(lima, &a);
        EXPECT(a.y1 >= FLEX_HDR_H && a.y2 < FLEX_SCREEN_H - FLEX_LIST_BOTTOM, "la zona elegida no se ve al abrir (y %d)",
               (int)a.y1);
    }
    lint_both("zonas horarias");
    sim_save(out, "f4_zonas");
    EXPECT(tap_row("Lagos, Argel"), "no hay zona Lagos");
    sim_run(400);
    watch_settings_get(&ws);
    EXPECT(ws.tz_min == 60, "la zona no se guardo (%d)", ws.tz_min);
    EXPECT(watch_time_get_tz() == 60, "el servicio de hora no recibio la zona");
    sim_run(1100);
    EXPECT(sim_find_label(ui_nav_top(), "UTC+01:00"), "la fila no muestra UTC+01:00");
    EXPECT(sim_find_label(ui_nav_top(), "3:41") && sim_find_label(ui_nav_top(), "vie, 16 oct"),
           "la hora no cambio con la zona");
    watch_time_set_tz(-300);
    ws.tz_min = -300;
    watch_settings_update(&ws);
    sim_run(1100);

    /* ---- Poner en hora: 20 oct 2026 07:30 local (Lima) ---- */
    EXPECT(tap_row("Poner en hora"), "no hay fila Poner en hora");
    lv_obj_t *h = roller_n(0), *mi = roller_n(1), *d = roller_n(2), *mo = roller_n(3), *y = roller_n(4);
    EXPECT(h && mi && d && mo && y, "faltan rodillos");
    if (h && mi && d && mo && y) {
        EXPECT(lv_roller_get_selected(h) == 21 && lv_roller_get_selected(mi) == 41, "los rodillos no empiezan en la hora");
        EXPECT(lv_roller_get_selected(d) == 14 && lv_roller_get_selected(mo) == 9 && lv_roller_get_selected(y) == 1,
               "los rodillos no empiezan en la fecha");
        sim_lint("poner en hora");
        sim_save(out, "f4_poner_en_hora");
        /* Febrero de 2027: el día 31 pasa a 28 */
        lv_roller_set_selected(d, 30, LV_ANIM_OFF);
        lv_roller_set_selected(mo, 1, LV_ANIM_OFF);
        lv_obj_send_event(mo, LV_EVENT_VALUE_CHANGED, NULL);
        lv_roller_set_selected(y, 2, LV_ANIM_OFF);
        lv_obj_send_event(y, LV_EVENT_VALUE_CHANGED, NULL);
        EXPECT(lv_roller_get_option_count(d) == 28 && lv_roller_get_selected(d) == 27, "febrero de 2027 no tiene 28 dias");
        lv_roller_set_selected(y, 1, LV_ANIM_OFF);
        lv_roller_set_selected(mo, 9, LV_ANIM_OFF);
        lv_obj_send_event(mo, LV_EVENT_VALUE_CHANGED, NULL);
        lv_roller_set_selected(d, 19, LV_ANIM_OFF);
        lv_roller_set_selected(h, 7, LV_ANIM_OFF);
        lv_roller_set_selected(mi, 30, LV_ANIM_OFF);

        /* Error del RTC: se ve y se puede reintentar */
        sim_time_set_result = WT_SET_ERR_I2C;
        const int sets0 = sim_time_sets;
        EXPECT(tap_row("Guardar"), "no hay boton Guardar");
        sim_run(300);
        EXPECT(sim_time_sets == sets0 + 1, "no se pidio poner en hora");
        const wt_datetime_t want = {2026, 10, 20, 7, 30, 0, 0};
        EXPECT(sim_time_set_utc == wt_local_to_utc(&want, -300), "UTC pedido incorrecto");
        EXPECT(sim_find_label(ui_nav_top(), "El RTC no responde (reintenta)"), "no se muestra el error");
        EXPECT(sim_find_label(ui_nav_top(), "Reintentar"), "el boton no pasa a Reintentar");
        sim_lint("poner en hora (error)");
        sim_save(out, "f4_poner_en_hora_error");

        /* Reintento correcto: confirma y vuelve sola */
        sim_time_set_result = WT_SET_OK;
        EXPECT(tap_row("Reintentar"), "no hay boton Reintentar");
        sim_run(200);
        EXPECT(sim_find_label(ui_nav_top(), "Hora guardada y comprobada"), "no se confirma la hora");
        sim_run(1200);
        EXPECT(sim_find_label(ui_nav_top(), "Fecha y hora"), "no volvio a Fecha y hora");
        sim_run(1100);
        EXPECT(sim_find_label(ui_nav_top(), "7:30") && sim_find_label(ui_nav_top(), "mar, 20 oct"),
               "la hora nueva no se muestra");
        EXPECT(sim_find_label(ui_nav_top(), "Guardada y comprobada"), "no se muestra el resultado");
    }

    ui_nav_home();
    sim_run(1500);
    sim_check_residue("tras fecha y hora");
    EXPECT(sim_mem_blocks() == blocks0, "fuga en fecha y hora: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    sim_set_time(WATCH_TIME_OK, 2026, 10, 15, 9, 41, 0);
    sim_run(1100);
    printf("fecha y hora F4.2: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
