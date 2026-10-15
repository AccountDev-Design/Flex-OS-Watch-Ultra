/*
 * watch_diag — ver include/watch_diag.h
 */
#include "watch_diag.h"

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

void watch_diag_mem(watch_mem_report_t *out)
{
    out->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    out->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

void watch_diag_log_mem(const char *when)
{
    watch_mem_report_t m;
    watch_diag_mem(&m);
    ESP_LOGI(TAG, "[%s] RAM interna libre %lu B (bloque %lu B, DMA %lu B) | PSRAM %lu/%lu B libres (bloque %lu B)",
             when, (unsigned long)m.internal_free, (unsigned long)m.internal_largest,
             (unsigned long)m.dma_free, (unsigned long)m.psram_free, (unsigned long)m.psram_total,
             (unsigned long)m.psram_largest);
}
