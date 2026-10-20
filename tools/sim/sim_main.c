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

#include "lvgl_private.h"
#include "sim.h"
#include "watch_diag.h"
#include "watch_time.h"

uint16_t sim_fb[SIM_W * SIM_H];
int sim_fails;
watch_power_snapshot_t sim_power;

static uint32_t s_ms;
static uint32_t s_flushes;
static struct {
    watch_time_state_t state;
    int64_t base_sec;
    uint32_t base_ms;
} s_time;
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
    s_flushes++;
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

void sim_drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int n, uint32_t step_ms)
{
    sim_press(x0, y0);
    sim_run(step_ms);
    for (int i = 1; i <= n; i++) {
        sim_press(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
        sim_run(step_ms);
    }
}

static int next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int cp, n;
    if (p[0] < 0x80) {
        cp = p[0];
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else {
        cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        n = 4;
    }
    *s += n;
    return cp;
}

/* Cuenta los caracteres de las etiquetas que su fuente no puede dibujar. */
int sim_missing_glyphs(lv_obj_t *obj, const char *where)
{
    int missing = 0;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        const char *t = lv_label_get_text(obj);
        while (t && *t) {
            const int cp = next_cp(&t);
            lv_font_glyph_dsc_t g;
            const bool ok = lv_font_get_glyph_dsc(font, &g, (uint32_t)cp, 0) && !g.is_placeholder;
            if (cp != '\n' && !ok) {
                printf("  sin glifo U+%04X en \"%s\" (%s)\n", cp, lv_label_get_text(obj), where);
                missing++;
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        missing += sim_missing_glyphs(lv_obj_get_child(obj, (int32_t)i), where);
    }
    return missing;
}

void sim_check_glyphs(lv_obj_t *obj, const char *where)
{
    const int m = sim_missing_glyphs(obj, where);
    EXPECT(m == 0, "%s: %d caracteres sin glifo", where, m);
}

size_t sim_mem_used(void)
{
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.total_size - m.free_size;
}

/* Bloques de LVGL en uso, sin contar las cachés (círculos del dibujo por
 * software, imágenes): crecen al dibujar por primera vez un radio o una
 * imagen y no son fugas. */
uint32_t sim_mem_blocks(void)
{
    lv_draw_sw_mask_cleanup();
    lv_image_cache_drop(NULL);
    lv_image_header_cache_drop(NULL);
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.used_cnt;
}

/* Pantalla (F4.5): encendida/apagada y brillo momentáneo (-1 = el del usuario) */
bool sim_display_on = true;
int sim_display_level = -1;
int sim_button = 0;

bool sim_display_power_cb(bool on)
{
    sim_display_on = on;
    if (on) {
        sim_display_level = -1;
    }
    return true;
}

bool sim_display_level_cb(int percent)
{
    sim_display_level = percent;
    return true;
}

int sim_button_level(void)
{
    return sim_button;
}

int sim_brightness = 70;
int sim_brightness_calls;

int sim_brightness_get(void)
{
    return sim_brightness;
}

bool sim_brightness_set(int percent)
{
    sim_brightness = percent;
    sim_brightness_calls++;
    return true;
}

uint32_t sim_flush_count(void)
{
    return s_flushes;
}

lv_obj_t *sim_find_label(lv_obj_t *obj, const char *text)
{
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) {
        return obj;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *r = sim_find_label(lv_obj_get_child(obj, (int32_t)i), text);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *find_nth(lv_obj_t *obj, const char *text, int *n)
{
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0 && (*n)-- == 0) {
        return obj;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *r = find_nth(lv_obj_get_child(obj, (int32_t)i), text, n);
        if (r) {
            return r;
        }
    }
    return NULL;
}

lv_obj_t *sim_find_label_n(lv_obj_t *obj, const char *text, int nth)
{
    return find_nth(obj, text, &nth);
}

int sim_timer_count(void)
{
    int n = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        n++;
    }
    return n;
}

void sim_set_time(watch_time_state_t st, int y, int mo, int d, int h, int mi, int s)
{
    const wt_datetime_t dt = {y, mo, d, h, mi, s, 0};
    s_time.state = st;
    s_time.base_sec = wt_to_seconds(&dt);
    s_time.base_ms = s_ms;
}

int64_t esp_timer_get_time(void)
{
    return (int64_t)s_ms * 1000;
}

/* IMU de la vista previa: lo que ponga cada prueba en sim_imu. */
watch_imu_snapshot_t sim_imu;
int sim_imu_users;
static uint32_t s_imu_gen;

bool watch_imu_get(watch_imu_snapshot_t *out)
{
    *out = sim_imu;
    return true;
}

uint32_t watch_imu_generation(void)
{
    return ++s_imu_gen; /* cada consulta ve datos "nuevos" */
}

void watch_imu_set_active(bool on)
{
    sim_imu_users += on ? 1 : -1;
}

/* Almacenamiento de la vista previa: la misma máquina de estados, sin flash.
 * sim_stg.gate decide si se permite escribir; sim_stg_fail hace fallar la
 * prueba del sector. */
watch_storage_info_t sim_stg = {.state = STG_LOCKED, .gate = STG_GATE_PROBE, .detail = "Sonda de 32 MB no superada"};
bool sim_stg_fail;

void watch_storage_info(watch_storage_info_t *out)
{
    *out = sim_stg;
}

esp_err_t watch_storage_request(stg_req_t req)
{
    if (sim_stg.gate != STG_GATE_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    switch (req) {
    case STG_REQ_SECTOR_TEST:
        if (sim_stg.state == STG_UNFORMATTED) {
            sim_stg.sector_test = sim_stg_fail ? -1 : 1;
            sim_stg.detail = sim_stg_fail ? "LA ZONA BAJA CAMBIÓ: no formatear" : "Prueba superada";
        }
        break;
    case STG_REQ_FORMAT:
        if (sim_stg.state == STG_UNFORMATTED && sim_stg.sector_test == 1) {
            sim_stg.state = STG_MOUNTED;
            sim_stg.total = 16u * 1024 * 1024;
            sim_stg.used = 8192;
        }
        break;
    case STG_REQ_FILE_TEST:
        if (sim_stg.state == STG_MOUNTED) {
            sim_stg.file_test = 1;
            sim_stg.file_count++;
            sim_stg.used += 4096;
        }
        break;
    }
    return ESP_OK;
}

/* ---- Datos simulados que sustituyen a watch_power / watch_time / watch_diag ---- */
/* La hora simulada (s_time) es LOCAL; la zona solo convierte a/desde UTC. */
static int s_tz = -300;
int sim_time_sets;
int64_t sim_time_set_utc;
wt_set_result_t sim_time_set_result = WT_SET_OK;

void watch_time_info(watch_time_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = s_time.state;
    out->tz_min = (int16_t)s_tz;
    out->marker_ok = s_time.state == WATCH_TIME_OK;
    out->writes = (uint32_t)sim_time_sets;
    out->set_result = sim_time_sets ? sim_time_set_result : WT_SET_NONE;
}

bool watch_time_now_utc(int64_t *utc_s)
{
    if (s_time.state != WATCH_TIME_OK) {
        return false;
    }
    *utc_s = s_time.base_sec + (s_ms - s_time.base_ms) / 1000 - (int64_t)s_tz * 60;
    return true;
}

esp_err_t watch_time_set_utc(int64_t utc_s)
{
    if (utc_s < 1735689600LL || utc_s > 4102444799LL) {
        return ESP_ERR_INVALID_ARG;
    }
    sim_time_sets++;
    sim_time_set_utc = utc_s;
    s_time.state = WATCH_TIME_OK;
    s_time.base_sec = utc_s + (int64_t)s_tz * 60;
    s_time.base_ms = s_ms;
    return ESP_OK;
}

void watch_time_set_tz(int off_min)
{
    s_time.base_sec += (int64_t)(off_min - s_tz) * 60; /* misma hora UTC, otra local */
    s_tz = off_min;
}

int watch_time_get_tz(void)
{
    return s_tz;
}

watch_time_state_t watch_time_now(wt_datetime_t *out)
{
    if (s_time.state == WATCH_TIME_OK) {
        wt_from_seconds(s_time.base_sec + (s_ms - s_time.base_ms) / 1000, out);
    }
    return s_time.state;
}

/* Generación: cambia con cualquier cambio de sim_power (las pruebas lo
 * modifican directamente), como la del servicio real con los datos visibles. */
uint32_t watch_power_generation(void)
{
    const uint8_t *p = (const uint8_t *)&sim_power;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(sim_power); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

bool watch_power_get(watch_power_snapshot_t *out)
{
    *out = sim_power;
    return true;
}

void watch_diag_mem(watch_mem_report_t *out)
{
    memset(out, 0, sizeof(*out));
    out->internal_free = 180 * 1024;   /* valores de ejemplo, no medidos */
    out->internal_largest = 110 * 1024;
    out->psram_total = 8 * 1024 * 1024;
    out->psram_free = 7200 * 1024;
    out->psram_largest = 6900 * 1024;
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

    /* SIM_ONLY=nombre: solo esa batería (tras el arranque, que crea la
     * interfaz). Para repetir deprisa una prueba concreta. */
    const char *only = getenv("SIM_ONLY");
    static const struct {
        const char *name;
        void (*run)(const char *);
    } suites[] = {
        {"tema", sim_suite_theme},       {"arranque", sim_suite_boot}, {"energia", sim_suite_power},
        {"esfera", sim_suite_face},      {"nav", sim_suite_nav},       {"panel", sim_suite_qp},
        {"ajustes", sim_suite_settings}, {"hora", sim_suite_datetime}, {"apps", sim_suite_apps},
        {"pantalla", sim_suite_screen},  {"wifi", sim_suite_wifi},       {"telefono", sim_suite_phone}, {"notas", sim_suite_notes},
        {"clima", sim_suite_weather},
        {"navegador", sim_suite_browser},
        {"ota", sim_suite_ota},
        {"mono", sim_suite_monkey},      {"visual", sim_suite_visual}, {"banco", sim_suite_bench},
    };
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (!only || strcmp(only, suites[i].name) == 0 || strcmp(suites[i].name, "arranque") == 0) {
            suites[i].run(out);
        }
    }

    printf("%s (%d fallos)\n", sim_fails ? "FALLOS" : "TODO OK", sim_fails);
    return sim_fails ? 1 : 0;
}
