/*
 * Vista previa y pruebas de PC de la interfaz (tools/sim).
 *
 * Compila el MISMO código de watch_ui y watch_theme del firmware contra LVGL
 * 9.5.0 en el PC, con una pantalla virtual de 410x502, un dedo simulado y
 * datos de batería SIMULADOS (escenarios fijos). Sirve para revisar
 * composición, colores, textos, navegación y ciclo de vida de los objetos; no
 * prueba hardware.
 *
 * Uso: make -C tools/sim LVGL_DIR=/ruta/lvgl-9.5.0 && tools/sim/build/sim out/
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"
#include "watch_diag.h"

uint16_t sim_fb[SIM_W * SIM_H];
int sim_fails;
watch_power_snapshot_t sim_power;

static uint32_t s_ms;
static struct {
    lv_point_t p;
    bool pressed;
} s_touch;

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    const int aw = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(&sim_fb[y * SIM_W + a->x1], &src[(y - a->y1) * aw], (size_t)aw * 2);
    }
    lv_display_flush_ready(d);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = s_touch.p;
    data->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

void sim_save(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.rgb565", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite(sim_fb, 2, SIM_W * SIM_H, f);
    fclose(f);
    printf("escrito %s\n", path);
}

void sim_press(int32_t x, int32_t y)
{
    s_touch.p.x = x;
    s_touch.p.y = y;
    s_touch.pressed = true;
}

void sim_release(void)
{
    s_touch.pressed = false;
}

void sim_tap(int32_t x, int32_t y)
{
    sim_press(x, y);
    sim_run(60);
    sim_release();
    sim_run(60);
}

size_t sim_mem_used(void)
{
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.total_size - m.free_size;
}

/* ---- Datos simulados que sustituyen a watch_power / watch_diag ---- */
bool watch_power_get(watch_power_snapshot_t *out)
{
    *out = sim_power;
    return true;
}

void watch_diag_mem(watch_mem_report_t *out)
{
    memset(out, 0, sizeof(*out));
    out->internal_free = 180 * 1024;   /* valores de ejemplo, no medidos */
    out->psram_free = 7200 * 1024;
}

void sim_scenario(int pct, int mv, batt_state_t st, batt_level_t lv, bool vbus)
{
    memset(&sim_power, 0, sizeof(sim_power));
    sim_power.driver_ok = true;
    sim_power.raw.chip_ok = true;
    sim_power.raw.batt_present = (st != BATT_STATE_NO_BATTERY && st != BATT_STATE_UNKNOWN);
    sim_power.raw.vbat_adc_enabled = true;
    sim_power.raw.gauge_enabled = true;
    sim_power.raw.vbus_present = vbus;
    sim_power.track.state = st;
    sim_power.track.level = lv;
    sim_power.track.percent = pct;
    sim_power.track.vbat_mv = mv;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(SIM_W, SIM_H);
    static uint16_t buf[SIM_W * 64];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);

    sim_suite_theme(out);
    sim_suite_power(out);

    printf("%s (%d fallos)\n", sim_fails ? "FALLOS" : "TODO OK", sim_fails);
    return sim_fails ? 1 : 0;
}
