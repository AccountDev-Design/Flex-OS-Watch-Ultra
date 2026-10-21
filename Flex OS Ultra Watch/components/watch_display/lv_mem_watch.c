/*
 * lv_mem_watch — asignador de LVGL del reloj (LV_STDLIB_CUSTOM).
 *
 * Por qué: con LV_STDLIB_CLIB, LVGL usa malloc() y, con
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL = 16384, TODO lo que reserva por debajo
 * de 16 KB (objetos, estilos, textos de etiquetas, temporizadores,
 * animaciones, el estado de cada página) acababa en la RAM INTERNA. Cada
 * página abierta, cada app suspendida y el selector de esferas restaban RAM
 * interna a lo que solo puede vivir ahí: Wi-Fi (búferes RX estáticos), BLE
 * (controlador), lwIP, las pilas de las tareas y la sesión del navegador
 * (que se negaba a abrir con < 20 KB internos: "Memoria insuficiente").
 *
 * Aquí todo lo de LVGL va a PSRAM (8 MB, con caché de datos) y solo si la
 * PSRAM no puede, a la RAM interna. Los búferes de pantalla NO pasan por aquí
 * (los reserva esp_lvgl_port con DMA en RAM interna).
 *
 * Se cuentan las reservas fallidas y las que tuvieron que ir a la RAM interna
 * (watch_display_lv_mem_stats) para el diagnóstico; nunca se ocultan: un
 * fallo devuelve NULL y LVGL lo trata como siempre.
 */
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <string.h>
#include "esp_heap_caps.h"
#include "watch_display.h"

#define CAPS_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define CAPS_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static volatile uint32_t s_fallback; /* reservas que no cupieron en PSRAM */
static volatile uint32_t s_failed;   /* reservas que no cupieron en ningún sitio */

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, CAPS_PSRAM);
    if (!p) {
        p = heap_caps_malloc(size, CAPS_INTERNAL);
        if (p) {
            s_fallback++;
        } else {
            s_failed++;
        }
    }
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    if (!p) {
        return lv_malloc_core(new_size);
    }
    /* Conserva la región del bloque original si puede; si no, la otra. */
    void *n = heap_caps_realloc_prefer(p, new_size, 2, CAPS_PSRAM, CAPS_INTERNAL);
    if (!n && new_size) {
        s_failed++;
    }
    return n;
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t i;
    heap_caps_get_info(&i, CAPS_PSRAM);
    memset(mon, 0, sizeof(*mon));
    mon->total_size = i.total_free_bytes + i.total_allocated_bytes;
    mon->free_size = i.total_free_bytes;
    mon->free_biggest_size = i.largest_free_block;
    mon->free_cnt = i.free_blocks;
    mon->used_cnt = i.allocated_blocks;
    mon->max_used = mon->total_size - i.minimum_free_bytes;
    mon->used_pct = mon->total_size ? (uint8_t)(100 - (100ULL * i.total_free_bytes) / mon->total_size) : 0;
    mon->frag_pct = i.total_free_bytes ? (uint8_t)(100 - (100ULL * i.largest_free_block) / i.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(CAPS_PSRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

void watch_display_lv_mem_stats(uint32_t *fallback, uint32_t *failed)
{
    if (fallback) {
        *fallback = s_fallback;
    }
    if (failed) {
        *failed = s_failed;
    }
}

#else

void watch_display_lv_mem_stats(uint32_t *fallback, uint32_t *failed)
{
    if (fallback) {
        *fallback = 0;
    }
    if (failed) {
        *failed = 0;
    }
}

#endif
