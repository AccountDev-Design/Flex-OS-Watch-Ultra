/*
 * watch_display — ver include/watch_display.h
 */
#include "watch_display.h"

#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "watch_display";

/* Buffers parciales: se intenta la mayor altura de esta lista que quepa en la
 * RAM interna con DMA dejando margen para el resto del sistema (Wi-Fi/BLE en
 * fases posteriores necesitan RAM interna). Alturas pares por el rounder. */
#define BUF_LINES_MAX      64
#define BUF_LINES_MIN      16
/* Fracción máxima de la RAM interna DMA libre que pueden ocupar los buffers. */
#define BUF_DMA_BUDGET_DIV 3

/* Tarea de LVGL: en el núcleo 1; el núcleo 0 queda para radio y servicios. */
#define LVGL_TASK_PRIO     4
#define LVGL_TASK_STACK    (10 * 1024)
#define LVGL_TASK_CORE     1

/* Brillo inicial: el BSP deja el panel al 100 % (0x51 = 0xFF). Un AMOLED a
 * pleno brillo consume más y no es necesario en interiores. */
#define DEFAULT_BRIGHTNESS 70

static watch_display_info_t s_info;
static int s_brightness = -1;           /* brillo del usuario (el que se guarda) */
static esp_lcd_panel_handle_t s_panel;  /* para encender y apagar el panel (F4.5) */
static bool s_panel_on = true;

/* El CO5300 solo acepta ventanas que empiezan en coordenada par y terminan en
 * impar (mismo criterio que rounder_event_cb del BSP oficial). Con 410x502 los
 * extremos 409 y 501 ya son impares, así que el redondeo nunca se sale. */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static void choose_buffers(watch_display_info_t *info)
{
    const uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    info->dma_free_before = heap_caps_get_free_size(caps);
    info->dma_largest_before = heap_caps_get_largest_free_block(caps);
    info->double_buffer = true;
    info->buf_in_psram = false;

    const uint32_t budget = info->dma_free_before / BUF_DMA_BUDGET_DIV;
    for (uint16_t lines = BUF_LINES_MAX; lines >= BUF_LINES_MIN; lines -= 2) {
        const uint32_t bytes = (uint32_t)WATCH_DISPLAY_H_RES * lines * sizeof(uint16_t);
        if (2 * bytes <= budget && bytes <= info->dma_largest_before) {
            info->buf_lines = lines;
            info->buf_bytes = bytes;
            return;
        }
    }
    /* No cabe ni el mínimo en RAM interna: PSRAM (más lento, pero arranca). */
    info->buf_lines = BUF_LINES_MAX;
    info->buf_bytes = (uint32_t)WATCH_DISPLAY_H_RES * BUF_LINES_MAX * sizeof(uint16_t);
    info->buf_in_psram = true;
}

esp_err_t watch_display_start(lv_display_t **out_disp)
{
    ESP_RETURN_ON_FALSE(out_disp, ESP_ERR_INVALID_ARG, TAG, "out_disp NULL");

    choose_buffers(&s_info);
    ESP_LOGI(TAG, "RAM interna DMA libre %lu B (bloque mayor %lu B) -> buffers 2 x %u lineas = 2 x %lu B en %s",
             (unsigned long)s_info.dma_free_before, (unsigned long)s_info.dma_largest_before,
             s_info.buf_lines, (unsigned long)s_info.buf_bytes,
             s_info.buf_in_psram ? "PSRAM" : "RAM interna DMA");

    /* Panel: inicialización oficial del BSP (CO5300 por QSPI, desfase 0x16). */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    const bsp_display_config_t bsp_cfg = {
        .max_transfer_sz = (int)s_info.buf_bytes,
    };
    ESP_RETURN_ON_ERROR(bsp_display_new(&bsp_cfg, &panel, &io), TAG, "bsp_display_new");
    s_panel = panel;

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = LVGL_TASK_PRIO;
    port_cfg.task_stack = LVGL_TASK_STACK;
    port_cfg.task_affinity = LVGL_TASK_CORE;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = (uint32_t)WATCH_DISPLAY_H_RES * s_info.buf_lines,
        .double_buffer = s_info.double_buffer,
        .hres = WATCH_DISPLAY_H_RES,
        .vres = WATCH_DISPLAY_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = !s_info.buf_in_psram,
            .buff_spiram = s_info.buf_in_psram,
            .sw_rotate = false,
            .swap_bytes = true, /* el panel espera RGB565 big-endian */
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl_port_add_disp");
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    /* La pantalla por defecto del tema claro de LVGL es blanca: se pone en
     * negro antes de encender el brillo para que el arranque no destelle. */
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_display_get_screen_active(disp);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lvgl_port_unlock();
    }

    if (watch_display_set_brightness(DEFAULT_BRIGHTNESS) != ESP_OK) {
        ESP_LOGW(TAG, "no se pudo fijar el brillo inicial");
    }

    *out_disp = disp;
    return ESP_OK;
}

bool watch_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void watch_display_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t watch_display_set_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    const esp_err_t err = bsp_display_brightness_set(percent);
    if (err == ESP_OK) {
        s_brightness = percent;
    }
    return err;
}

int watch_display_get_brightness(void)
{
    return s_brightness;
}

esp_err_t watch_display_set_level(int percent)
{
    if (percent < 0) {
        percent = s_brightness >= 0 ? s_brightness : DEFAULT_BRIGHTNESS;
    } else if (percent > 100) {
        percent = 100;
    }
    return bsp_display_brightness_set(percent);
}

esp_err_t watch_display_set_power(bool on)
{
    if (!s_panel) {
        return ESP_ERR_INVALID_STATE;
    }
    if (on == s_panel_on) {
        return ESP_OK;
    }
    esp_err_t err;
    if (on) {
        /* DISPON y después el brillo del usuario: sin destello. */
        err = esp_lcd_panel_disp_on_off(s_panel, true);
        if (err == ESP_OK) {
            err = watch_display_set_level(-1);
        }
    } else {
        /* Brillo 0 no basta: el AMOLED seguiría barriendo. Brillo 0 y DISPOFF
         * (0x28): los píxeles se apagan y el contenido queda en la RAM del
         * panel. Sin SLPIN (0x10): el controlador del CO5300 no tiene función
         * de reposo en el driver y su temporización no está documentada. */
        err = bsp_display_brightness_set(0);
        if (err == ESP_OK) {
            err = esp_lcd_panel_disp_on_off(s_panel, false);
        }
    }
    if (err == ESP_OK) {
        s_panel_on = on;
        ESP_LOGI(TAG, "pantalla %s", on ? "encendida" : "apagada (DISPOFF)");
    } else {
        ESP_LOGW(TAG, "no se pudo %s la pantalla: %s", on ? "encender" : "apagar", esp_err_to_name(err));
    }
    return err;
}

void watch_display_get_info(watch_display_info_t *out)
{
    if (out) {
        *out = s_info;
    }
}
