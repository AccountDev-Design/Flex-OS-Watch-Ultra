/*
 * watch_diag — ver include/watch_diag.h
 */
#include "watch_diag.h"
#include "watch_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "watch_diag";

#define SIXTEEN_MB   (16u * 1024u * 1024u)
#define WINDOW_BYTES 4096u

/* Ventanas de la zona baja con contenido conocido tras grabar el firmware. */
static const uint32_t kProbeOffsets[] = {
    0x0000,  /* bootloader */
    0x8000,  /* tabla de particiones */
    0x10000, /* cabecera de la app en ota_0 */
};
#define PROBE_N (sizeof(kProbeOffsets) / sizeof(kProbeOffsets[0]))

void watch_diag_flash_probe(watch_flash_report_t *out)
{
    memset(out, 0, sizeof(*out));
    out->verdict = FLASH_PROBE_NOT_RUN;

    esp_flash_read_id(NULL, &out->jedec_id);
    esp_flash_get_physical_size(NULL, &out->physical_size);
    esp_flash_get_size(NULL, &out->configured_size);

    const esp_partition_t *st = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                         ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "storage");
    if (st) {
        out->storage_found = true;
        out->storage_offset = st->address;
        out->storage_size = st->size;
    }

    ESP_LOGI(TAG, "flash: JEDEC 0x%06lX, fisica %lu KB, declarada %lu KB",
             (unsigned long)out->jedec_id, (unsigned long)(out->physical_size / 1024),
             (unsigned long)(out->configured_size / 1024));

    if (out->configured_size <= SIXTEEN_MB) {
        out->verdict = FLASH_PROBE_NOT_CONFIGURED;
        ESP_LOGW(TAG, "el firmware no declara mas de 16 MB: sonda omitida");
        return;
    }

    uint8_t *lo = heap_caps_malloc(WINDOW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *hi = heap_caps_malloc(WINDOW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lo || !hi) {
        free(lo);
        free(hi);
        out->verdict = FLASH_PROBE_READ_ERROR;
        ESP_LOGE(TAG, "sin memoria para la sonda");
        return;
    }

    bool read_ok[PROBE_N];
    bool equal[PROBE_N];
    for (size_t i = 0; i < PROBE_N; i++) {
        const uint32_t a = kProbeOffsets[i];
        const esp_err_t e1 = esp_flash_read(NULL, lo, a, WINDOW_BYTES);
        const esp_err_t e2 = esp_flash_read(NULL, hi, a + SIXTEEN_MB, WINDOW_BYTES);
        read_ok[i] = (e1 == ESP_OK) && (e2 == ESP_OK);
        equal[i] = read_ok[i] && memcmp(lo, hi, WINDOW_BYTES) == 0;
        ESP_LOGI(TAG, "sonda 0x%07lX vs 0x%07lX: %s%s", (unsigned long)a, (unsigned long)(a + SIXTEEN_MB),
                 read_ok[i] ? (equal[i] ? "IGUALES" : "distintas") : "error ",
                 read_ok[i] ? "" : esp_err_to_name(e1 != ESP_OK ? e1 : e2));
    }
    free(lo);
    free(hi);

    out->verdict = flash_probe_classify(PROBE_N, read_ok, equal);
    ESP_LOGI(TAG, "veredicto >16 MB: %s", flash_probe_verdict_name(out->verdict));
    if (st) {
        ESP_LOGI(TAG, "particion storage: 0x%07lX, %lu KB (sin montar en B1)",
                 (unsigned long)st->address, (unsigned long)(st->size / 1024));
    }
}

/* Gancho de reservas fallidas: solo variables (puede llegar de cualquier
 * tarea, con la reserva en curso; registrar desde aquí podría volver a
 * reservar). */
static volatile uint32_t s_fails, s_fail_size, s_fail_caps;
static uint32_t s_ev_int, s_ev_ps, s_ev_fails;
static bool s_ev_have;

static void alloc_failed_hook(size_t size, uint32_t caps, const char *function_name)
{
    (void)function_name;
    s_fails++;
    s_fail_size = (uint32_t)size;
    s_fail_caps = caps;
}

void watch_diag_mem_start(void)
{
    heap_caps_register_failed_alloc_callback(alloc_failed_hook);
}

void watch_diag_mem(watch_mem_report_t *out)
{
    out->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    out->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    out->internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    out->alloc_fails = s_fails;
    out->last_fail_size = s_fail_size;
    out->last_fail_caps = s_fail_caps;
}

void watch_diag_mem_event(const char *what)
{
    watch_mem_report_t m;
    watch_diag_mem(&m);
    const int32_t d_int = s_ev_have ? (int32_t)(m.internal_free - s_ev_int) : 0;
    const int32_t d_ps = s_ev_have ? (int32_t)(m.psram_free - s_ev_ps) : 0;
    ESP_LOGI(TAG,
             "[mem] %s: interna %lu B (%+ld, min %lu, bloque %lu) | PSRAM %lu B (%+ld, min %lu, bloque %lu)", what,
             (unsigned long)m.internal_free, (long)d_int, (unsigned long)m.internal_min,
             (unsigned long)m.internal_largest, (unsigned long)m.psram_free, (long)d_ps,
             (unsigned long)m.psram_min, (unsigned long)m.psram_largest);
    if (m.alloc_fails != s_ev_fails) {
        ESP_LOGW(TAG, "[mem] %lu reservas fallidas desde el arranque (la última: %lu B, capacidades 0x%lx)",
                 (unsigned long)m.alloc_fails, (unsigned long)m.last_fail_size, (unsigned long)m.last_fail_caps);
    }
    s_ev_int = m.internal_free;
    s_ev_ps = m.psram_free;
    s_ev_fails = m.alloc_fails;
    s_ev_have = true;
}

bool watch_mem_psram_fits(size_t n)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= n;
}

bool watch_mem_internal_fits(size_t n, size_t min_left)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= n &&
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + min_left;
}

void watch_diag_log_mem(const char *when)
{
    watch_mem_report_t m;
    watch_diag_mem(&m);
    ESP_LOGI(TAG,
             "[%s] RAM interna libre %lu B (bloque %lu B, DMA %lu B, min %lu B) | PSRAM %lu/%lu B libres (bloque %lu B)",
             when, (unsigned long)m.internal_free, (unsigned long)m.internal_largest,
             (unsigned long)m.dma_free, (unsigned long)m.internal_min, (unsigned long)m.psram_free,
             (unsigned long)m.psram_total, (unsigned long)m.psram_largest);
}

bool watch_task_spawn(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                      BaseType_t core, bool psram_first, TaskHandle_t *out, char *why, size_t why_n)
{
    const UBaseType_t in = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, ps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const bool int_first = mem_stack_internal_first(psram_first, stack, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    const UBaseType_t order[2] = {int_first ? in : ps, int_first ? ps : in};
    for (int i = 0; i < 2; i++) {
        TaskHandle_t h = NULL;
        if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, &h, core, order[i]) == pdPASS) {
            if (out) {
                *out = h;
            }
            ESP_LOGI(TAG, "tarea %s: pila de %lu B en %s", name, (unsigned long)stack,
                     order[i] == ps ? "PSRAM" : "RAM interna");
            return true;
        }
    }
    /* Lo que falta casi siempre es el TCB (RAM interna) o la pila en PSRAM. */
    const size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t ps_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    char t[112];
    mem_why_text(t, sizeof(t), stack, ps_free < stack + 64u * 1024u, int_free, ps_free);
    ESP_LOGW(TAG, "tarea %s no creada: %s", name, t);
    if (why && why_n) {
        snprintf(why, why_n, "%s", t);
    }
    return false;
}

void watch_task_exit(void)
{
    vTaskDeleteWithCaps(NULL);
    for (;;) {
        /* no se llega: vTaskDeleteWithCaps(NULL) no vuelve */
    }
}

void watch_mem_text(char *out, size_t n)
{
    char a[16], b[16];
    mem_fmt_size(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), a, sizeof(a));
    mem_fmt_size(heap_caps_get_free_size(MALLOC_CAP_SPIRAM), b, sizeof(b));
    snprintf(out, n, "RAM interna %s · PSRAM %s libres", a, b);
}
