/*
 * Rediseño 2026-10 en la vista previa: cada pantalla rediseñada se renderiza
 * con los MISMOS datos que muestra su referencia (design/referencias, PNG)
 * para compararlas lado a lado (tools/sim/compare.py), y se comprueba lo
 * funcional de cada una: Reloj (pestañas, formato 12/24 h, cronómetro,
 * temporizador, alarmas con interruptor real), integridad visual, glifos y que
 * no queden temporizadores ni memoria al salir.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sim.h"
#include "ui_internal.h"
#include "watch_theme.h"
#include "watch_time.h"
#include "flexfs.h"
#include "watch_weather.h"
void sim_wx_reset(void);

static lv_obj_t *clickable_of(lv_obj_t *o)
{
    while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        o = lv_obj_get_parent(o);
    }
    return o;
}

static bool tap_obj(lv_obj_t *o)
{
    if (!o || lv_obj_has_state(o, LV_STATE_DISABLED)) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    sim_run(300);
    return true;
}

static bool tap_text(const char *text)
{
    return tap_obj(clickable_of(sim_find_label(ui_nav_top(), text)));
}

static void shot(const char *out, const char *name)
{
    sim_run(400);
    sim_lint(name);
    sim_check_glyphs(ui_nav_top(), name);
    sim_save(out, name);
}

static lv_obj_t *find_type(lv_obj_t *o, const lv_obj_class_t *cls, int *n, int idx)
{
    if (lv_obj_check_type(o, cls) && (*n)++ == idx) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_type(lv_obj_get_child(o, (int32_t)i), cls, n, idx);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *nth(const lv_obj_class_t *cls, int idx)
{
    int n = 0;
    return find_type(ui_nav_top(), cls, &n, idx);
}

static void suite_reloj(const char *out)
{
    ui_nav_push(ui_reloj_create);
    sim_run(400);
    lv_obj_t *page = ui_nav_top();
    ui_reloj_select(page, UI_RELOJ_HORA);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.h24 = false;
    watch_settings_update(&ws);
    sim_run(1100);
    EXPECT(sim_find_label(page, "9:41") && sim_find_label(page, "Sábado, 24 de octubre"),
           "Reloj > Hora: no muestra la hora y la fecha reales");
    EXPECT(sim_find_label(page, "sáb, 24 oct") && sim_find_label(page, "12 h"), "Reloj > Hora: tarjetas");
    shot(out, "rd_reloj_hora");
    /* Formato: la tarjeta cambia el ajuste real */
    EXPECT(tap_text("12 h"), "Reloj > Hora: no se puede tocar Formato");
    watch_settings_get(&ws);
    EXPECT(ws.h24 && sim_find_label(page, "24 h") && sim_find_label(page, "9:41"), "Formato no pasa a 24 h");
    tap_text("24 h");
    watch_settings_get(&ws);
    EXPECT(!ws.h24, "Formato no vuelve a 12 h");
    /* Fecha: abre Fecha y hora */
    EXPECT(tap_text("Fecha"), "Reloj > Hora: no se puede tocar Fecha");
    EXPECT(ui_nav_depth() == 3 && sim_find_label(ui_nav_top(), "Fecha y hora"), "Fecha no abre Fecha y hora");
    ui_nav_back();
    sim_run(400);

    /* ---- Cronómetro ---- */
    sw_reset(ui_sw());
    sim_tap(164, 62); /* pestaña por el dedo */
    sim_run(400);
    EXPECT(ui_reloj_tab(page) == UI_RELOJ_CRONO, "la pestaña del cronometro no responde al toque");
    EXPECT(sim_find_label(page, "00:00.00") && sim_find_label(page, "Iniciar"), "cronometro: estado inicial");
    shot(out, "rd_reloj_cronometro");
    EXPECT(!tap_text("Parcial"), "cronometro: Parcial activo parado");
    EXPECT(tap_text("Iniciar"), "cronometro: no hay Iniciar");
    sim_run(1500);
    EXPECT(tap_text("Parcial"), "cronometro: no hay Parcial en marcha");
    sim_run(1200);
    tap_text("Parcial");
    sim_run(800);
    tap_text("Parcial");
    sim_run(500);
    EXPECT(ui_sw()->n == 3 && ui_sw()->st == SW_RUN, "cronometro: %d vueltas", ui_sw()->n);
    EXPECT(sim_find_label_n(page, "Parcial", 0), "cronometro: falta el boton");
    shot(out, "rd_reloj_cronometro_marcha");
    /* sale de la pestaña y sigue contando */
    ui_reloj_select(page, UI_RELOJ_HORA);
    const int64_t e0 = sw_elapsed(ui_sw(), ui_mono_us());
    sim_run(2000);
    EXPECT(sw_elapsed(ui_sw(), ui_mono_us()) >= e0 + 1900000, "cronometro: no sigue contando fuera de su pestana");
    ui_reloj_select(page, UI_RELOJ_CRONO);
    sim_run(200);
    EXPECT(tap_text("Pausa"), "cronometro: no hay Pausa");
    EXPECT(tap_text("Reiniciar"), "cronometro: no hay Reiniciar en pausa");
    EXPECT(ui_sw()->st == SW_IDLE && ui_sw()->n == 0, "cronometro: no se reinicio");

    /* ---- Alarmas: las de la referencia ---- */
    alarm_set_t set = {0};
    set.n = 3;
    set.a[0] = (alarm_t){1, 0, 0, 0x7F};  /* 12:00 a. m., diariamente, activa */
    set.a[1] = (alarm_t){0, 5, 45, 0x7E}; /* 5:45 a. m., L a S, desactivada */
    set.a[2] = (alarm_t){0, 6, 0, 0x7F};  /* 6:00 a. m., diariamente, desactivada */
    ui_alarms_set(&set);
    ui_reloj_select(page, UI_RELOJ_ALARMAS);
    sim_run(700);
    EXPECT(sim_find_label(page, "12:00") && sim_find_label(page, "5:45") && sim_find_label(page, "6:00"),
           "alarmas: no estan las tres");
    EXPECT(sim_find_label_n(page, "Diariamente", 1), "alarmas: falta 'Diariamente'");
    shot(out, "rd_reloj_alarmas");
    /* el interruptor de la segunda la activa y se guarda */
    lv_obj_t *sw1 = nth(&lv_switch_class, 1);
    EXPECT(sw1 && !lv_obj_has_state(sw1, LV_STATE_CHECKED), "alarmas: el segundo interruptor no esta apagado");
    tap_obj(sw1);
    sim_run(700);
    EXPECT(ui_alarms()->a[1].on, "alarmas: el interruptor no activa la alarma");
    sw1 = nth(&lv_switch_class, 1);
    EXPECT(sw1 && lv_obj_has_state(sw1, LV_STATE_CHECKED), "alarmas: el interruptor no refleja el estado real");
    tap_obj(sw1);
    sim_run(700);
    EXPECT(!ui_alarms()->a[1].on, "alarmas: el interruptor no desactiva la alarma");
    /* tocar la tarjeta abre el editor */
    EXPECT(tap_text("6:00"), "alarmas: no se puede tocar la tarjeta");
    EXPECT(sim_find_label(ui_nav_top(), "Editar alarma"), "alarmas: la tarjeta no abre el editor");
    ui_nav_back();
    sim_run(400);

    /* ---- Temporizador ---- */
    tmr_cancel(ui_tmr());
    ui_reloj_select(page, UI_RELOJ_TEMP);
    sim_run(400);
    EXPECT(sim_find_label(page, "Horas") && sim_find_label(page, "Minutos") && sim_find_label(page, "Segundos"),
           "temporizador: faltan las columnas");
    lv_obj_t *start = clickable_of(sim_find_label(page, "Iniciar"));
    EXPECT(start && lv_obj_has_state(start, LV_STATE_DISABLED), "temporizador: Iniciar activo con 00:00.00");
    shot(out, "rd_reloj_temporizador");
    EXPECT(tap_text("00:15:00"), "temporizador: no hay atajo de 15 min");
    sim_run(400);
    EXPECT(lv_roller_get_selected(nth(&lv_roller_class, 1)) == 15, "temporizador: el atajo no pone 15 min");
    EXPECT(tap_text("Iniciar"), "temporizador: no se puede iniciar");
    sim_run(1100);
    EXPECT(ui_tmr()->st == TMR_RUN && sim_find_label(page, "14:59"), "temporizador: no cuenta");
    shot(out, "rd_reloj_temporizador_marcha");
    EXPECT(tap_text("Pausa"), "temporizador: no hay Pausa");
    EXPECT(tap_text("Seguir"), "temporizador: no hay Seguir");
    EXPECT(tap_text("Cancelar"), "temporizador: no hay Cancelar");
    EXPECT(ui_tmr()->st == TMR_IDLE, "temporizador: Cancelar no lo para");

    ui_alarms_set(&(alarm_set_t){0});
    ui_nav_home();
    sim_run(500);
}

static void put_file(const char *root, const char *rel, size_t n)
{
    char p[512];
    snprintf(p, sizeof(p), "%s%s", root, rel);
    FILE *f = fopen(p, "wb");
    if (f) {
        for (size_t i = 0; i < n; i++) {
            fputc((int)(i * 7), f);
        }
        fclose(f);
    }
}

static void suite_storage(const char *out)
{
    /* Almacenamiento montado sobre una carpeta del PC con archivos de verdad */
    char root[400], cmd[460];
    snprintf(root, sizeof(root), "%s/fs_almacen", out);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    EXPECT(system(cmd) == 0, "no se pudo limpiar %s", root);
    mkdir(root, 0775);
    ffs_set_root(root);
    const watch_storage_info_t stg0 = sim_stg;
    sim_stg.state = STG_MOUNTED;
    sim_stg.gate = STG_GATE_OK;
    sim_stg.total = 16u * 1024 * 1024;
    sim_stg.usage_gen = 0;
    ffs_init_dirs();
    put_file(root, "/Documentos/informe.txt", 61440);
    put_file(root, "/Notas/lista.txt", 40960);
    put_file(root, "/Papelera/viejo.txt", 102400);
    mkdir(strcat(strcpy(cmd, root), "/System"), 0775);
    put_file(root, "/System/cache.bin", 90000);
    sim_stg.used = 61440 + 40960 + 102400 + 90000 + 8192 * 4; /* archivos + metadatos de LittleFS */
    sim_psram_free = 3584u * 1024;                            /* 4,5 MB en uso, como la referencia */

    ui_nav_push(ui_storage_create);
    sim_run(1200);
    lv_obj_t *page = ui_nav_top();
    EXPECT(sim_find_label(page, "Almacenamiento") && sim_find_label(page, "Memoria interna") &&
               sim_find_label(page, "PSRAM"),
           "almacenamiento: faltan los rotulos");
    EXPECT(sim_find_label(page, "16/32 MB  (51%)"), "almacenamiento: memoria interna mal calculada");
    EXPECT(sim_find_label(page, "4,5/8 MB  (56%)"), "almacenamiento: PSRAM mal calculada");
    EXPECT(sim_find_label(page, "100 KB") && sim_find_label(page, "16,0 MB") && sim_find_label(page, "Óptimo"),
           "almacenamiento: documentos, sistema o estado");
    EXPECT(sim_stg.usage_gen == 1, "almacenamiento: no pidio el recuento por categorias (o lo pidio %u veces)",
           (unsigned)sim_stg.usage_gen);
    shot(out, "rd_almacenamiento");
    /* Optimizar: sin cachés no hay nada que liberar; con el fondo desenfocado, sí */
    EXPECT(tap_text("Optimizar"), "almacenamiento: no hay Optimizar");
    sim_run(300);
    EXPECT(ui_banner_active(), "Optimizar no dice nada");
    sim_run(4000);
    ui_nav_home();
    sim_run(400);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.wallpaper = FLEX_WALL_AURORA;
    watch_settings_update(&ws);
    watch_theme_set_wallpaper(FLEX_WALL_AURORA);
    ui_nav_replace_root(ui_face_create);
    sim_run(1500);
    EXPECT(watch_theme_blur_get() != NULL, "almacenamiento: no hay fondo desenfocado que liberar");
    ui_nav_push(ui_storage_create);
    sim_run(600);
    EXPECT(tap_text("Optimizar"), "almacenamiento: no hay Optimizar");
    sim_run(300);
    EXPECT(watch_theme_blur_get() == NULL, "Optimizar no solto el fondo desenfocado");
    sim_run(4000);
    /* La tarjeta abre los detalles (pruebas y formateo siguen ahí) */
    EXPECT(tap_text("Detalles de memoria\ny sistema"), "almacenamiento: la tarjeta no se puede tocar");
    EXPECT(sim_find_label(ui_nav_top(), "Detalles") && sim_find_label(ui_nav_top(), "Probar escritura"),
           "almacenamiento: no abre los detalles");
    ui_nav_back();
    sim_run(400);
    /* Documentos abre el explorador */
    EXPECT(tap_text("Documentos"), "almacenamiento: Documentos no se puede tocar");
    EXPECT(ui_nav_depth() == 3, "almacenamiento: Documentos no abre el explorador");
    ui_nav_back();
    sim_run(400);
    /* Sin montar: lo dice y no inventa tamaños */
    sim_stg.state = STG_UNFORMATTED;
    sim_run(1200);
    EXPECT(sim_find_label(ui_nav_top(), "Datos sin montar") && sim_find_label_n(ui_nav_top(), "--", 2),
           "almacenamiento: sin montar inventa datos");
    shot(out, "rd_almacenamiento_sin_montar");
    ui_nav_home();
    sim_run(400);
    ws.wallpaper = FLEX_WALL_BLACK;
    watch_settings_update(&ws);
    watch_theme_set_wallpaper(FLEX_WALL_BLACK);
    ui_nav_replace_root(ui_face_create);
    sim_run(500);
    sim_psram_free = 7200u * 1024;
    sim_stg = stg0;
    ffs_set_root(NULL);
}

static void set_face(int style)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    ws.face = (uint8_t)style;
    ws.h24 = true;
    watch_settings_update(&ws);
    ui_nav_replace_root(ui_face_create);
    sim_run(1200);
}

static void suite_faces(const char *out)
{
    /* Los datos de cada referencia: día, hora y batería */
    static const struct {
        int style;
        int y, mo, d, h, mi, pct;
        const char *time, *date, *png;
    } kF[] = {
        {UI_FACE_MONTANA, 2026, 10, 24, 13, 30, 90, "13:30", "SÁB. 24", "rd_esfera_montana"},
        {UI_FACE_ULTRA, 2026, 10, 23, 10, 9, 65, NULL, "VIE 23", "rd_esfera_ultra"},
        {UI_FACE_LUNA, 2026, 10, 24, 13, 25, 90, "13:25", "SÁB. 24", "rd_esfera_luna"},
        {UI_FACE_TIERRA, 2026, 10, 23, 10, 9, 90, "10:09", "VIE 23", "rd_esfera_tierra"},
        {UI_FACE_FLORES, 2026, 10, 25, 10, 10, 53, NULL, "DOM 25", "rd_esfera_flores"},
        {UI_FACE_TAJ, 2026, 10, 24, 13, 35, 90, "13:35", "SÁB. 24", "rd_esfera_taj"},
    };
    ui_nav_home();
    sim_run(300);
    for (size_t i = 0; i < sizeof(kF) / sizeof(kF[0]); i++) {
        sim_set_time(WATCH_TIME_OK, kF[i].y, kF[i].mo, kF[i].d, kF[i].h, kF[i].mi, 30);
        sim_scenario(kF[i].pct, 4000, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
        set_face(kF[i].style);
        lv_obj_t *face = ui_nav_page(0);
        EXPECT(watch_theme_has_background(), "%s: sin fondo propio", kF[i].png);
        if (kF[i].time) {
            EXPECT(sim_find_label(face, kF[i].time), "%s: no muestra %s", kF[i].png, kF[i].time);
        }
        EXPECT(sim_find_label(face, kF[i].date), "%s: no muestra %s", kF[i].png, kF[i].date);
        if (kF[i].style == UI_FACE_FLORES) {
            EXPECT(sim_find_label_n(face, "10", 1) && sim_find_label(face, "53%"), "flores: horas, minutos o bateria");
        }
        if (kF[i].style == UI_FACE_ULTRA) {
            EXPECT(sim_find_label(face, "65"), "ultra: la complicacion no muestra la bateria");
        }
        if (kF[i].style == UI_FACE_TIERRA) {
            lv_obj_t *wx = NULL;
            for (uint32_t k = 0; k < lv_obj_get_child_count(lv_obj_get_child(face, 1)); k++) {
                lv_obj_t *o = lv_obj_get_child(lv_obj_get_child(face, 1), (int32_t)k);
                if (lv_obj_check_type(o, &lv_label_class) && lv_obj_get_y(o) > 400) {
                    wx = o;
                }
            }
            EXPECT(wx && lv_obj_has_flag(wx, LV_OBJ_FLAG_HIDDEN), "tierra: sin datos del clima no debe inventarlos");
            /* con datos reales (servicio simulado de Open-Meteo) */
            sim_wx_reset();
            wx_loc_t loc = {0};
            snprintf(loc.name, sizeof(loc.name), "Lima");
            snprintf(loc.region, sizeof(loc.region), "Lima, Perú");
            loc.lat = -12.04318f;
            loc.lon = -77.02824f;
            watch_weather_add(&loc);
            sim_run(3000);
            wx_data_t d;
            EXPECT(watch_weather_data(&d), "tierra: el servicio simulado no dio datos");
            EXPECT(wx && !lv_obj_has_flag(wx, LV_OBJ_FLAG_HIDDEN) && strstr(lv_label_get_text(wx), "°"),
                   "tierra: no muestra el clima real");
        }
        sim_lint(kF[i].png);
        sim_check_glyphs(ui_nav_page(0), kF[i].png);
        sim_save(out, kF[i].png);
    }
    sim_wx_reset();
    set_face(UI_FACE_DIGITAL);
}

static void suite_compass(const char *out)
{
    /* La escena de la referencia: 10:09, rumbo 315° (aquí relativo), plano */
    sim_set_time(WATCH_TIME_OK, 2026, 10, 24, 10, 9, 0);
    memset(&sim_imu, 0, sizeof(sim_imu));
    sim_imu.state = IMU_RUNNING;
    sim_imu.samples = 100;
    sim_imu.acc_mg[2] = 1000;
    ui_nav_push(ui_compass_create);
    sim_run(500);
    sim_imu.heading.valid = true;
    sim_imu.heading.yaw = 315.0f;
    sim_imu.heading.drift = 12.0f;
    sim_imu.heading.bias_ok = true;
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "315° REL") && sim_find_label(ui_nav_top(), "10:09"),
           "brujula: falta el rumbo relativo o la hora");
    EXPECT(strcmp(ui_compass_ring_text(0), "INCLINACIÓN 0°") == 0, "brujula: inclinacion (%s)", ui_compass_ring_text(0));
    shot(out, "rd_brujula");
    /* Sin IMU: nada inventado */
    sim_imu.state = IMU_ERROR;
    sim_run(400);
    EXPECT(sim_find_label(ui_nav_top(), "--° REL") && strcmp(ui_compass_ring_text(0), "INCLINACIÓN --") == 0,
           "brujula: sin IMU sigue mostrando datos");
    shot(out, "rd_brujula_sin_imu");
    ui_nav_back();
    sim_run(500);
    memset(&sim_imu, 0, sizeof(sim_imu));
}

void sim_suite_redesign(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    sim_ws_reset(NULL);
    watch_time_set_tz(-300);
    sim_set_time(WATCH_TIME_OK, 2026, 10, 24, 9, 41, 0); /* sábado, 24 de octubre */
    sim_scenario(90, 4100, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    sim_run(1100);
    /* Calentamiento: la primera vez LVGL guarda en caché los degradados del
     * vidrio (no es una fuga: no crece al repetir); se mide después. */
    ui_nav_push(ui_reloj_create);
    sim_run(500);
    ui_nav_home();
    sim_run(500);
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    suite_reloj(out);
    suite_storage(out);
    suite_faces(out);
    suite_compass(out);

    EXPECT(sim_timer_count() == timers0, "temporizadores: %d -> %d", timers0, sim_timer_count());
    EXPECT(sim_mem_blocks() == blocks0, "bloques de memoria: %u -> %u", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    printf("rediseno: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
