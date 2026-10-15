/*
 * Vista previa de PC de la pantalla de inicio (tools/sim).
 *
 * Compila el MISMO watch_home.c y watch_theme.c del firmware contra LVGL
 * 9.5.0 en el PC, con una pantalla virtual de 410x502 y datos de batería
 * SIMULADOS (escenarios fijos). Sirve para revisar composición, colores y
 * textos; no prueba hardware.
 *
 * Uso: make -C tools/sim LVGL_DIR=/ruta/lvgl-9.5.0 && tools/sim/build/sim out/
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "watch_power.h"
#include "watch_ui.h"
#include "ui_internal.h"
#include "flex_tokens.h"

#define W FLEX_SCREEN_W
#define H FLEX_SCREEN_H

static uint16_t s_fb[W * H];
static uint32_t s_ms;

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    const int aw = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * W + a->x1], &src[(y - a->y1) * aw], (size_t)aw * 2);
    }
    lv_display_flush_ready(d);
}

/* ---- Datos simulados que sustituyen a watch_power / watch_diag ---- */
static watch_power_snapshot_t s_sim;

bool watch_power_get(watch_power_snapshot_t *out)
{
    *out = s_sim;
    return true;
}

void watch_diag_mem(watch_mem_report_t *out)
{
    memset(out, 0, sizeof(*out));
    out->internal_free = 180 * 1024;   /* valores de ejemplo, no medidos */
    out->psram_free = 7200 * 1024;
}

static void scenario(int pct, int mv, batt_state_t st, batt_level_t lv, bool vbus)
{
    memset(&s_sim, 0, sizeof(s_sim));
    s_sim.driver_ok = true;
    s_sim.raw.chip_ok = true;
    s_sim.raw.batt_present = (st != BATT_STATE_NO_BATTERY && st != BATT_STATE_UNKNOWN);
    s_sim.raw.vbat_adc_enabled = true;
    s_sim.raw.gauge_enabled = true;
    s_sim.raw.vbus_present = vbus;
    s_sim.track.state = st;
    s_sim.track.level = lv;
    s_sim.track.percent = pct;
    s_sim.track.vbat_mv = mv;
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void save_png(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.rgb565", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite(s_fb, 2, W * H, f);
    fclose(f);
    printf("escrito %s\n", path);
}

static int g_fails;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FALLO: %s\n", msg); g_fails++; } } while (0)

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    static uint16_t buf[W * 64];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    watch_ui_boot_info_t boot = {0};
    boot.touch_ok = true;
    boot.power_ok = true;
    boot.flash.configured_size = 32u * 1024 * 1024;
    boot.flash.verdict = FLASH_PROBE_UPPER_DISTINCT;

    scenario(84, 4085, BATT_STATE_DISCHARGING, BATT_LEVEL_NORMAL, false);
    watch_ui_home_create(&boot);
    run(1200);
    save_png(out, "b1_descargando");

    scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    run(1200);
    save_png(out, "b1_cargando");

    scenario(12, 3640, BATT_STATE_DISCHARGING, BATT_LEVEL_LOW, false);
    run(1200);
    save_png(out, "b1_baja");
    /* B2-5: aviso de batería baja visible y que se va solo (~4,6 s) */
    EXPECT(ui_banner_active(), "el aviso de bateria baja no se mostro");
    run(4000);
    EXPECT(!ui_banner_active(), "el aviso de bateria baja no se oculto");
    run(2000);
    EXPECT(!ui_banner_active(), "el aviso de bateria baja se repitio");

    scenario(-1, -1, BATT_STATE_UNKNOWN, BATT_LEVEL_UNKNOWN, false);
    s_sim.driver_ok = false;
    run(1200);
    save_png(out, "b1_sin_datos");

    scenario(98, 4180, BATT_STATE_FULL, BATT_LEVEL_NORMAL, true);
    run(1200);
    save_png(out, "b2_completa");

    scenario(97, 4170, BATT_STATE_EXTERNAL_IDLE, BATT_LEVEL_NORMAL, true);
    run(1200);
    save_png(out, "b2_externa");

    scenario(4, 3450, BATT_STATE_DISCHARGING, BATT_LEVEL_CRITICAL, false);
    run(1200);
    save_png(out, "b2_critica");
    EXPECT(ui_banner_active(), "el aviso critico no se mostro");
    run(4000);
    EXPECT(!ui_banner_active(), "el aviso critico no se oculto");

    /* B2-4: enchufar desde descarga -> una animación, que se borra sola. */
    int fails = g_fails;
    scenario(62, 4012, BATT_STATE_CHARGING, BATT_LEVEL_NORMAL, true);
    s_sim.track.prev_state = BATT_STATE_DISCHARGING;
    s_sim.track.charge_events = 1;
    run(1200);
    save_png(out, "b2_animacion_carga");
    if (!ui_charge_anim_active()) { printf("FALLO: la animacion de carga no se mostro\n"); fails++; }
    run(2600);
    if (ui_charge_anim_active()) { printf("FALLO: la animacion sigue activa a los 3,8 s\n"); fails++; }
    if (lv_anim_count_running() != 0) { printf("FALLO: %u animaciones vivas\n", (unsigned)lv_anim_count_running()); fails++; }
    if (lv_obj_get_child_count(lv_layer_top()) != 0) { printf("FALLO: quedan objetos en layer_top\n"); fails++; }
    /* Sin evento nuevo no se repite */
    run(1200);
    if (ui_charge_anim_active()) { printf("FALLO: la animacion se repitio sin evento\n"); fails++; }
    printf("animacion de carga: %s\n", fails ? "FALLOS" : "OK");
    return fails ? 1 : 0;
}
