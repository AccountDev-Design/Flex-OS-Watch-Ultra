/*
 * watch_storage — ver include/watch_storage.h
 */
#include "watch_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "watch_storage";

#define LABEL       "storage"
#define TASK_STACK  4096
#define TASK_PRIO   1
#define TEST_DIR    WATCH_STORAGE_BASE "/flexos"
#define TEST_FILE   TEST_DIR "/prueba.txt"
#define TEST_TMP    TEST_DIR "/prueba.tmp"
#define ALIAS_DIST  0x1000000u /* 16 MB */

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static stg_flash_t s_flash;
static watch_storage_info_t s_info;
static bool s_mounted;
static bool s_started;

static stg_state_t idle_state(void)
{
    return s_mounted ? STG_MOUNTED : STG_UNFORMATTED;
}

static void set_info(stg_state_t st, esp_err_t err, const char *detail)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.state = st;
    s_info.last_err = err;
    if (detail) {
        s_info.detail = detail;
    }
    xSemaphoreGive(s_lock);
}

static void refresh_usage(void)
{
    size_t total = 0, used = 0;
    if (esp_littlefs_info(LABEL, &total, &used) == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_info.total = total;
        s_info.used = used;
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t mount(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = WATCH_STORAGE_BASE,
        .partition_label = LABEL,
        .format_if_mount_failed = false, /* nunca se formatea sin pedirlo */
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };
    const esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_OK) {
        s_mounted = true;
        refresh_usage();
        set_info(STG_MOUNTED, ESP_OK, "Montado");
        ESP_LOGI(TAG, "LittleFS montado en %s: %u KB de %u KB usados", WATCH_STORAGE_BASE,
                 (unsigned)(s_info.used / 1024), (unsigned)(s_info.total / 1024));
    } else {
        set_info(STG_UNFORMATTED, err, "Sin formato de LittleFS");
        ESP_LOGW(TAG, "storage sin LittleFS (%s): datos de fabrica o vacio; no se formatea solo",
                 esp_err_to_name(err));
    }
    return err;
}

/* CRC de 4 KB leídos directamente de la flash (dirección absoluta). */
static esp_err_t crc_at(uint32_t addr, uint8_t *buf, uint32_t *crc)
{
    const esp_err_t err = esp_flash_read(NULL, buf, addr, STG_SECTOR);
    if (err == ESP_OK) {
        *crc = stg_crc32(buf, STG_SECTOR);
    }
    return err;
}

static void sector_test(void)
{
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, LABEL);
    if (!p || s_mounted) {
        set_info(idle_state(), ESP_ERR_INVALID_STATE, p ? "Ya montado: no hace falta" : "Sin partición");
        return;
    }
    uint8_t *pat = heap_caps_malloc(STG_SECTOR, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *buf = heap_caps_malloc(STG_SECTOR, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!pat || !buf) {
        free(pat);
        free(buf);
        set_info(STG_UNFORMATTED, ESP_ERR_NO_MEM, "Sin memoria para la prueba");
        return;
    }
    const uint32_t off = stg_test_offset(p->size);
    const uint32_t abs = p->address + off;
    uint32_t boot0 = 0, boot1 = 0, alias0 = 0, alias1 = 0;
    const char *res = "Prueba superada";
    int ok = 1;
    esp_err_t err = crc_at(0x0, buf, &boot0);
    if (err == ESP_OK) {
        err = crc_at(abs - ALIAS_DIST, buf, &alias0);
    }
    stg_pattern(pat, STG_SECTOR, abs);
    bool touched = false; /* solo se vuelve a borrar si se llegó a borrar */
    if (err == ESP_OK) {
        err = esp_partition_erase_range(p, off, STG_SECTOR);
        touched = true;
    }
    if (err == ESP_OK) {
        err = esp_partition_write(p, off, pat, STG_SECTOR);
    }
    if (err == ESP_OK) {
        err = esp_partition_read(p, off, buf, STG_SECTOR);
    }
    if (err != ESP_OK) {
        ok = -1;
        res = touched ? "Error de flash" : "No se pudo leer la zona baja (no se escribió)";
    } else if (memcmp(buf, pat, STG_SECTOR) != 0) {
        ok = -1;
        res = "Lo leído no coincide";
    }
    /* ¿Se tocó algo de abajo? */
    if (!touched) {
        /* nada escrito: no hay nada que comprobar ni que borrar */
    } else if (crc_at(0x0, buf, &boot1) != ESP_OK || crc_at(abs - ALIAS_DIST, buf, &alias1) != ESP_OK) {
        ok = -1;
        res = "Error al releer la zona baja";
    } else if (boot0 != boot1 || alias0 != alias1) {
        ok = -1;
        res = "LA ZONA BAJA CAMBIÓ: no formatear";
        ESP_LOGE(TAG, "la escritura en 0x%08lx cambio la zona baja (bootloader %08lx->%08lx, -16 MB %08lx->%08lx)",
                 (unsigned long)abs, (unsigned long)boot0, (unsigned long)boot1, (unsigned long)alias0,
                 (unsigned long)alias1);
    }
    /* El sector queda borrado (como estaba en una flash nueva). */
    if (touched) {
        esp_partition_erase_range(p, off, STG_SECTOR);
    }
    free(pat);
    free(buf);
    ESP_LOGI(TAG, "prueba del sector 0x%08lx: %s", (unsigned long)abs, res);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.sector_test = ok;
    s_info.state = STG_UNFORMATTED;
    s_info.last_err = err;
    s_info.detail = res;
    xSemaphoreGive(s_lock);
}

static void format(void)
{
    if (s_info.sector_test != 1 || s_mounted) {
        set_info(idle_state(), ESP_ERR_INVALID_STATE, s_mounted ? "Ya montado" : "Antes, la prueba del sector");
        return;
    }
    ESP_LOGW(TAG, "formateando storage (borra los datos de fabrica de esa zona)");
    const esp_err_t err = esp_littlefs_format(LABEL);
    if (err != ESP_OK) {
        set_info(STG_ERROR, err, "No se pudo formatear");
        return;
    }
    mount();
}

/* Escritura atómica: temporal, fsync y rename (un corte deja el viejo o el nuevo). */
static int write_atomic(const char *path, const char *tmp, const void *data, size_t n)
{
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return -1;
    }
    const bool ok = fwrite(data, 1, n, f) == n && fflush(f) == 0 && fsync(fileno(f)) == 0;
    fclose(f);
    if (!ok) {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path);
}

static void file_test(void)
{
    if (!s_mounted) {
        set_info(idle_state(), ESP_ERR_INVALID_STATE, "No montado");
        return;
    }
    mkdir(TEST_DIR, 0775);
    uint32_t count = 0;
    char line[96];
    FILE *f = fopen(TEST_FILE, "rb");
    if (f) {
        if (fgets(line, sizeof(line), f)) {
            sscanf(line, "Flex OS Watch Ultra prueba %lu", (unsigned long *)&count);
        }
        fclose(f);
    }
    count++;
    const int n = snprintf(line, sizeof(line), "Flex OS Watch Ultra prueba %lu\n", (unsigned long)count);
    int ok = write_atomic(TEST_FILE, TEST_TMP, line, (size_t)n) == 0 ? 1 : -1;
    if (ok == 1) {
        char back[96] = {0};
        f = fopen(TEST_FILE, "rb");
        ok = (f && fgets(back, sizeof(back), f) && strcmp(back, line) == 0) ? 1 : -1;
        if (f) {
            fclose(f);
        }
    }
    refresh_usage();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.file_test = ok;
    s_info.file_count = ok == 1 ? count : s_info.file_count;
    s_info.state = STG_MOUNTED;
    s_info.detail = ok == 1 ? "Archivo escrito y releído" : "Falló el archivo de prueba";
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "archivo de prueba: %s (contador %lu)", ok == 1 ? "bien" : "MAL", (unsigned long)count);
}

static void storage_task(void *arg)
{
    (void)arg;
    if (s_info.gate == STG_GATE_OK) {
        mount();
    }
    for (;;) {
        uint32_t req = 0;
        xTaskNotifyWait(0, UINT32_MAX, &req, portMAX_DELAY);
        if (s_info.gate != STG_GATE_OK) {
            continue;
        }
        set_info(STG_BUSY, ESP_OK, "Trabajando…");
        if (req == STG_REQ_SECTOR_TEST) {
            sector_test();
        } else if (req == STG_REQ_FORMAT) {
            format();
        } else if (req == STG_REQ_FILE_TEST) {
            file_test();
        } else {
            set_info(idle_state(), ESP_OK, NULL);
        }
    }
}

esp_err_t watch_storage_start(const stg_flash_t *flash)
{
    if (s_started) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_flash = *flash;
    s_info.gate = stg_gate(&s_flash);
    s_info.state = STG_LOCKED;
    s_info.detail = stg_gate_text(s_info.gate);
    s_started = true;
    if (s_info.gate != STG_GATE_OK) {
        ESP_LOGW(TAG, "storage bloqueado: %s (no se monta ni se escribe)", stg_gate_text(s_info.gate));
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(storage_task, "watch_storage", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void watch_storage_info(watch_storage_info_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        out->detail = "Sin arrancar";
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}

esp_err_t watch_storage_request(stg_req_t req)
{
    if (!s_task || s_info.gate != STG_GATE_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotify(s_task, (uint32_t)req, eSetValueWithOverwrite);
    return ESP_OK;
}
