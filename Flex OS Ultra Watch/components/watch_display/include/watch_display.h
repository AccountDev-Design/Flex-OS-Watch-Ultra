/*
 * watch_display — pantalla AMOLED CO5300 (410x502, QSPI) + LVGL 9.
 *
 * La inicialización del panel es la del BSP oficial de Waveshare
 * (bsp_display_new: comandos de arranque del CO5300 y desfase de columna 0x16).
 * Lo propio de Flex OS Watch Ultra es la configuración de LVGL: buffers
 * parciales dimensionados según la RAM interna medida en el arranque, y el
 * "rounder" que exige el CO5300 (áreas que empiezan en coordenada par y
 * terminan en impar).
 *
 * Toda llamada a LVGL fuera de la tarea de LVGL debe ir entre
 * watch_display_lock() / watch_display_unlock().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_DISPLAY_H_RES 410
#define WATCH_DISPLAY_V_RES 502

/** Datos de la configuración elegida en el arranque (para diagnóstico). */
typedef struct {
    uint16_t buf_lines;        /**< líneas por buffer parcial (par) */
    uint32_t buf_bytes;        /**< bytes por buffer */
    bool     double_buffer;    /**< dos buffers (envío y dibujo solapados) */
    bool     buf_in_psram;     /**< true = no cupo en RAM interna DMA */
    uint32_t dma_free_before;  /**< RAM interna DMA libre antes de reservar */
    uint32_t dma_largest_before;
} watch_display_info_t;

/**
 * Inicializa el panel (BSP oficial), LVGL y su tarea, y registra la pantalla.
 * @param[out] out_disp pantalla LVGL creada
 */
esp_err_t watch_display_start(lv_display_t **out_disp);

/** Bloquea LVGL (timeout_ms = 0 espera indefinidamente). */
bool watch_display_lock(uint32_t timeout_ms);
void watch_display_unlock(void);

/** Brillo 0..100 % mediante el comando DCS 0x51 del panel (vía BSP). */
esp_err_t watch_display_set_brightness(int percent);

/** Último brillo aplicado con éxito (0..100), o -1 si nunca se aplicó. No se guarda (F4.1). */
int watch_display_get_brightness(void);

void watch_display_get_info(watch_display_info_t *out);

#ifdef __cplusplus
}
#endif
