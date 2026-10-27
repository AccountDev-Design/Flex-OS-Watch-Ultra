/*
 * wm_sd — la microSD en el reloj (ESP-IDF 5.5.5). Ver include/watch_media.h
 * y include/wm_sd_logic.h.
 *
 * Hardware (esquema de la placa y BSP de Waveshare, docs/MULTIMEDIA.md §1):
 * SDMMC en modo de 1 bit, CLK GPIO2, CMD GPIO1, D0 GPIO3. GPIO17 es SDCS/DAT3:
 * se deja en entrada con pull-up para que la tarjeta arranque en modo SD (no
 * SPI). No hay pin de detección: la presencia se sabe sondeando (wm_sd_logic).
 *
 * El montaje se hace paso a paso con la API pública de FATFS/VFS en vez de
 * esp_vfs_fat_sdmmc_mount(): así una tarjeta que no se monta sigue
 * inicializada (para decir por qué y, si la persona lo pide, formatearla) y
 * no se formatea nunca a escondidas (format_if_mount_failed no existe aquí).
 *
 * Una sola tarea ("wm_sd") hace TODO lo que toca el controlador SDMMC fuera
 * de las lecturas y escrituras normales de FATFS (que serializa FatFs con su
 * propio cerrojo por volumen).
 */
#include "watch_media.h"

#include <stdio.h>
#include <string.h>

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

/* Nombres largos en UTF-8: "Grabación 27-10 18.04.wav" no cabe en 8.3. Un
 * sdkconfig generado antes de este cambio no los tiene: se avisa al compilar
 * en vez de truncar nombres en la tarjeta de la persona. */
#if !CONFIG_FATFS_LFN_HEAP && !CONFIG_FATFS_LFN_STACK
#error "watch_media necesita nombres largos en FATFS (CONFIG_FATFS_LFN_HEAP). Tu sdkconfig es anterior a sdkconfig.defaults: bórralo (o ejecuta idf.py fullclean) y vuelve a compilar. Ver docs/MULTIMEDIA.md §3."
#endif
#if !CONFIG_FATFS_API_ENCODING_UTF_8
#error "watch_media necesita CONFIG_FATFS_API_ENCODING_UTF_8 (nombres con tildes y eñes). Ver docs/MULTIMEDIA.md §3."
#endif

static const char *TAG = "wm_sd";

#define PIN_CLK GPIO_NUM_2
#define PIN_CMD GPIO_NUM_1
#define PIN_D0  GPIO_NUM_3
#define PIN_D3  GPIO_NUM_17 /* SDCS/DAT3 */

#define TASK_STACK  6144
#define TASK_PRIO   3       /* por debajo de LVGL (4): nunca le quita la CPU */
#define DMA_BUF     4096    /* 8 sectores: lecturas y escrituras a búferes en PSRAM */
#define FMT_WORK    (32 * 1024)
#define TICKET_MS   60000u
#define LISTENERS   6

static SemaphoreHandle_t s_mx;
static TaskHandle_t s_task;
static wsd_t s_sd;

/* Solo los toca la tarea (salvo s_info, bajo s_mx). */
static sdmmc_card_t *s_card;
static void *s_dmabuf;
static bool s_slot_up;
static BYTE s_pdrv = 0xFF;
static wm_sd_info_t s_info;
static uint32_t s_ticket;
static uint32_t s_ticket_ms;

static struct {
    wm_sd_listener_t cb;
    void *ctx;
} s_lis[LISTENERS];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mx);
}

static void wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

/* ---------------- registros ---------------- */

/* Sin tarjeta, cada sondeo haría que el controlador de IDF escribiera
 * "send_op_cond returned 0x107" por la UART. Durante el SONDEO se callan sus
 * etiquetas; el resultado lo resume este módulo cuando cambia. */
static const char *const kQuiet[] = {"sdmmc_common", "sdmmc_sd", "sdmmc_io", "sdmmc_init", "sdmmc_cmd",
                                     "sdmmc_periph", "sdmmc_req"};

static void quiet(bool on)
{
    for (size_t i = 0; i < sizeof(kQuiet) / sizeof(kQuiet[0]); i++) {
        esp_log_level_set(kQuiet[i], on ? ESP_LOG_NONE : ESP_LOG_WARN);
    }
}

/* ---------------- tarjeta ---------------- */

static void release_card(void)
{
    if (s_slot_up) {
        sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_1); /* apaga el host cuando no queda ninguna ranura */
        s_slot_up = false;
    }
    heap_caps_free(s_card);
    s_card = NULL;
    heap_caps_free(s_dmabuf);
    s_dmabuf = NULL;
}

static wsd_probe_t do_probe(uint32_t *serial)
{
    *serial = 0;
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "controlador SDMMC: %s", esp_err_to_name(err));
        return WSD_PROBE_NOCARD;
    }
    sdmmc_slot_config_t slot = {
        .clk = PIN_CLK,
        .cmd = PIN_CMD,
        .d0 = PIN_D0,
        .d1 = GPIO_NUM_NC,
        .d2 = GPIO_NUM_NC,
        .d3 = GPIO_NUM_NC,
        .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC,
        .d6 = GPIO_NUM_NC,
        .d7 = GPIO_NUM_NC,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 1,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };
    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ranura SDMMC: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        return WSD_PROBE_NOCARD;
    }
    s_slot_up = true;
    s_card = heap_caps_calloc(1, sizeof(*s_card), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_dmabuf = heap_caps_malloc(DMA_BUF, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_card || !s_dmabuf) {
        ESP_LOGW(TAG, "sin RAM interna para la tarjeta");
        release_card();
        return WSD_PROBE_NOCARD; /* se reintenta en el próximo sondeo */
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; /* 20 MHz, como el BSP de Waveshare */
    host.dma_aligned_buffer = s_dmabuf;     /* rebote para búferes en PSRAM, sin reservas por llamada */
    host.unaligned_multi_block_rw_max_chunk_size = DMA_BUF / 512;
    quiet(true);
    err = sdmmc_card_init(&host, s_card);
    quiet(false);
    if (err != ESP_OK) {
        release_card();
        if (err == ESP_ERR_TIMEOUT) {
            return WSD_PROBE_NOCARD; /* nadie contesta */
        }
        if (err == ESP_ERR_NOT_SUPPORTED) {
            return WSD_PROBE_UNSUPPORTED; /* SDUC (CSD v3) u otra cosa */
        }
        ESP_LOGW(TAG, "la tarjeta contesta pero no se inicializa: %s", esp_err_to_name(err));
        return WSD_PROBE_BAD;
    }
    if (!s_card->is_mem || s_card->is_mmc) {
        ESP_LOGW(TAG, "no es una tarjeta SD de memoria (%s)", s_card->is_mmc ? "MMC" : "SDIO");
        release_card();
        return WSD_PROBE_UNSUPPORTED;
    }
    *serial = (uint32_t)s_card->cid.serial ^ ((uint32_t)s_card->cid.mfg_id << 24) ^
              ((uint32_t)s_card->cid.date << 8);
    return WSD_PROBE_OK;
}

static void card_info_locked(void)
{
    s_info.card_valid = s_card != NULL;
    if (!s_card) {
        s_info.kind = WM_CARD_NONE;
        s_info.card_bytes = 0;
        s_info.name[0] = 0;
        return;
    }
    s_info.card_bytes = wm_card_bytes(s_card->csd.capacity, s_card->csd.sector_size);
    if (!(s_card->ocr & SD_OCR_SDHC_CAP)) {
        s_info.kind = WM_CARD_SDSC;
    } else {
        s_info.kind = s_info.card_bytes > 32ull * 1024 * 1024 * 1024 ? WM_CARD_SDXC : WM_CARD_SDHC;
    }
    memcpy(s_info.name, s_card->cid.name, sizeof(s_info.name) - 1);
    s_info.name[sizeof(s_info.name) - 1] = 0;
    for (int i = 0; s_info.name[i]; i++) {
        if ((unsigned char)s_info.name[i] < 0x20 || (unsigned char)s_info.name[i] > 0x7E) {
            s_info.name[i] = '?';
        }
    }
    s_info.mfg_id = (uint8_t)s_card->cid.mfg_id;
    s_info.freq_khz = (uint32_t)s_card->real_freq_khz;
}

/* ---------------- montaje ---------------- */

static bool read_sector(uint32_t lba, uint8_t *buf)
{
    return sdmmc_read_sectors(s_card, buf, lba, 1) == ESP_OK;
}

/* ¿Qué hay en una tarjeta que FatFs no reconoce? Solo lee. */
static wsd_err_t classify(uint8_t *fat_bits)
{
    uint8_t *sec = s_dmabuf; /* DMA interno: lectura directa */
    if (!read_sector(0, sec)) {
        return WSD_E_IO;
    }
    wfs_class_t c = wfs_classify(sec, false);
    if (c.kind == WFS_PARTITION) {
        if (c.lba >= (uint32_t)s_card->csd.capacity && s_card->csd.capacity > 0) {
            return WSD_E_FS_DAMAGED; /* la partición empieza fuera de la tarjeta */
        }
        if (!read_sector(c.lba, sec)) {
            return WSD_E_IO;
        }
        c = wfs_classify(sec, true);
    }
    *fat_bits = c.fat_bits;
    switch (c.kind) {
    case WFS_EXFAT: return WSD_E_EXFAT;
    case WFS_NTFS:  return WSD_E_NTFS;
    case WFS_GPT:   return WSD_E_GPT;
    case WFS_FAT:   return WSD_E_FS_DAMAGED; /* parece FAT pero FatFs no la monta */
    default:        return WSD_E_NOFS;
    }
}

static void drive_name(BYTE pdrv, char drv[3])
{
    drv[0] = (char)('0' + pdrv);
    drv[1] = ':';
    drv[2] = 0;
}

static void space_update(void)
{
    uint64_t total = 0, free_b = 0;
    /* f_getfree: con FSInfo válido es inmediato; si no, recorre la FAT una
     * vez (en una SDXC grande, segundos). Se hace aquí, en MOUNTING. */
    const bool ok = esp_vfs_fat_info(WM_SD_BASE, &total, &free_b) == ESP_OK;
    lock();
    s_info.space_valid = ok;
    s_info.total_bytes = ok ? total : 0;
    s_info.free_bytes = ok ? free_b : 0;
    unlock();
}

static wsd_err_t do_mount(void)
{
    BYTE pdrv = 0xFF;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == 0xFF) {
        return WSD_E_NOMEM;
    }
    ff_diskio_register_sdmmc(pdrv, s_card);
    ff_sdmmc_set_disk_status_check(pdrv, false);
    char drv[3];
    drive_name(pdrv, drv);
    const esp_vfs_fat_conf_t conf = {.base_path = WM_SD_BASE, .fat_drive = drv, .max_files = WM_SD_MAX_FILES};
    FATFS *fs = NULL;
    if (esp_vfs_fat_register_cfg(&conf, &fs) != ESP_OK || !fs) {
        ff_diskio_unregister(pdrv);
        return WSD_E_NOMEM;
    }
    const FRESULT fr = f_mount(fs, drv, 1);
    if (fr != FR_OK) {
        f_mount(NULL, drv, 0);
        esp_vfs_fat_unregister_path(WM_SD_BASE);
        ff_diskio_unregister(pdrv);
        uint8_t bits = 0;
        wsd_err_t e;
        switch (fr) {
        case FR_NO_FILESYSTEM:   e = classify(&bits); break;
        case FR_NOT_ENOUGH_CORE: e = WSD_E_NOMEM; break;
        case FR_DISK_ERR:
        case FR_NOT_READY:       e = WSD_E_IO; break;
        default:                 e = WSD_E_FS_DAMAGED; break;
        }
        ESP_LOGW(TAG, "no se monta (FatFs %d): %s", (int)fr, wsd_err_text(e));
        return e;
    }
    s_pdrv = pdrv;
    lock();
    s_info.fat_bits = fs->fs_type == FS_FAT32 ? 32 : fs->fs_type == FS_FAT16 ? 16 : 12;
    unlock();
    space_update();
    return WSD_E_NONE;
}

static void do_unmount(void)
{
    if (s_pdrv == 0xFF) {
        return;
    }
    char drv[3];
    drive_name(s_pdrv, drv);
    f_mount(NULL, drv, 0);
    esp_vfs_fat_unregister_path(WM_SD_BASE);
    ff_diskio_unregister(s_pdrv);
    s_pdrv = 0xFF;
    lock();
    s_info.space_valid = false;
    s_info.total_bytes = s_info.free_bytes = 0;
    s_info.fat_bits = 0;
    unlock();
}

/* MBR con una partición que ocupa toda la tarjeta y FAT32 (FAT16 si es
 * pequeña), como hace ESP-IDF al formatear, con el clúster de wm_format_cluster. */
static bool do_format(void)
{
    BYTE pdrv = 0xFF;
    if (!s_card || ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == 0xFF) {
        return false;
    }
    size_t wsz = FMT_WORK;
    void *work = NULL;
    while (!work && wsz >= 4096) {
        work = heap_caps_malloc(wsz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!work) {
            wsz /= 2;
        }
    }
    if (!work) {
        wsz = FMT_WORK;
        work = heap_caps_malloc(wsz, MALLOC_CAP_SPIRAM); /* más lento (rebote), pero funciona */
    }
    if (!work) {
        return false;
    }
    ff_diskio_register_sdmmc(pdrv, s_card);
    char drv[3];
    drive_name(pdrv, drv);
    const uint64_t bytes = wm_card_bytes(s_card->csd.capacity, s_card->csd.sector_size);
    const uint32_t au = wm_format_cluster(bytes);
    ESP_LOGW(TAG, "formateando la microSD a petición de la persona (clúster %u KB)", (unsigned)(au / 1024));
    LBA_t plist[] = {100, 0, 0, 0};
    FRESULT fr = f_fdisk(pdrv, plist, work);
    if (fr == FR_OK) {
        const MKFS_PARM opt = {(BYTE)FM_ANY, 2, 0, 0, au};
        fr = f_mkfs(drv, &opt, work, (UINT)wsz);
    }
    ff_diskio_unregister(pdrv);
    heap_caps_free(work);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "formateo: FatFs %d", (int)fr);
    }
    return fr == FR_OK;
}

/* ---------------- tarea ---------------- */

static void notify_listeners(uint32_t gen)
{
    wm_sd_listener_t cb[LISTENERS];
    void *ctx[LISTENERS];
    lock();
    for (int i = 0; i < LISTENERS; i++) {
        cb[i] = s_lis[i].cb;
        ctx[i] = s_lis[i].ctx;
    }
    unlock();
    for (int i = 0; i < LISTENERS; i++) {
        if (cb[i]) {
            cb[i](gen, ctx[i]);
        }
    }
}

static void log_change(void)
{
    char d[96];
    wm_sd_info_t i;
    wm_sd_info(&i);
    wm_sd_describe(&i, d, sizeof(d));
    ESP_LOGI(TAG, "microSD: %s", d);
}

static void sd_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ull << PIN_D3,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    uint32_t seen_gen = 0;
    for (;;) {
        lock();
        const wsd_action_t a = wsd_next(&s_sd, now_ms());
        uint32_t wait = 0;
        if (a == WSD_DO_WAIT) {
            const int32_t left = (int32_t)(s_sd.next_ms - now_ms());
            wait = left < 20 ? 20 : left > 1000 ? 1000 : (uint32_t)left;
        }
        unlock();
        switch (a) {
        case WSD_DO_WAIT:
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
            break;
        case WSD_DO_PROBE: {
            uint32_t serial = 0;
            const wsd_probe_t r = do_probe(&serial);
            lock();
            wsd_on_probe(&s_sd, r, serial, now_ms());
            card_info_locked();
            unlock();
            break;
        }
        case WSD_DO_MOUNT: {
            const wsd_err_t e = do_mount();
            lock();
            wsd_on_mount(&s_sd, e, now_ms());
            unlock();
            break;
        }
        case WSD_DO_STATUS: {
            const bool ok = s_card && sdmmc_get_status(s_card) == ESP_OK; /* CMD13 */
            lock();
            wsd_on_status(&s_sd, ok, now_ms());
            unlock();
            break;
        }
        case WSD_DO_UNMOUNT:
            do_unmount();
            lock();
            wsd_on_unmounted(&s_sd, now_ms());
            unlock();
            break;
        case WSD_DO_FORMAT: {
            const bool ok = do_format();
            lock();
            wsd_on_format(&s_sd, ok, now_ms());
            unlock();
            break;
        }
        case WSD_DO_RELEASE:
            release_card();
            lock();
            wsd_on_released(&s_sd, now_ms());
            card_info_locked();
            unlock();
            break;
        default:
            break;
        }
        lock();
        const uint32_t gen = s_sd.gen;
        unlock();
        if (gen != seen_gen) {
            seen_gen = gen;
            log_change();
            notify_listeners(gen);
        }
    }
}

/* ---------------- API ---------------- */

esp_err_t watch_media_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) {
        return ESP_ERR_NO_MEM;
    }
    wsd_init(&s_sd, now_ms());
    /* Pila en PSRAM: con XIP desde PSRAM la caché no se apaga al escribir en
     * la flash, así que una tarea con la pila ahí puede usar FATFS y SDMMC. */
    if (xTaskCreatePinnedToCoreWithCaps(sd_task, "wm_sd", TASK_STACK, NULL, TASK_PRIO, &s_task, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void wm_sd_info(wm_sd_info_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        out->state = WSD_ABSENT;
        return;
    }
    lock();
    *out = s_info;
    out->state = wsd_visible(&s_sd);
    out->err = s_sd.err;
    out->ejected = s_sd.ejected;
    out->can_format = wsd_can_format(&s_sd);
    out->gen = s_sd.gen;
    out->leases = s_sd.leases;
    if (out->state != WSD_READY && out->state != WSD_REMOVING) {
        out->space_valid = false;
    }
    unlock();
}

uint32_t wm_sd_gen(void)
{
    if (!s_mx) {
        return 0;
    }
    lock();
    const uint32_t g = s_sd.gen;
    unlock();
    return g;
}

bool wm_sd_lease(void)
{
    if (!s_mx) {
        return false;
    }
    lock();
    const bool ok = wsd_lease(&s_sd);
    unlock();
    return ok;
}

void wm_sd_unlease(void)
{
    if (!s_mx) {
        return;
    }
    lock();
    wsd_unlease(&s_sd);
    const bool w = s_sd.now_pending;
    unlock();
    if (w) {
        wake();
    }
}

void wm_sd_io_error(void)
{
    if (!s_mx) {
        return;
    }
    lock();
    wsd_io_error(&s_sd, now_ms());
    unlock();
    wake();
}

void wm_sd_interest(bool on)
{
    if (!s_mx) {
        return;
    }
    lock();
    wsd_interest(&s_sd, on, now_ms());
    unlock();
    wake();
}

bool wm_sd_eject(void)
{
    if (!s_mx) {
        return false;
    }
    lock();
    const bool ok = wsd_request_eject(&s_sd, now_ms());
    unlock();
    wake();
    return ok;
}

bool wm_sd_retry(void)
{
    if (!s_mx) {
        return false;
    }
    lock();
    const bool ok = wsd_request_retry(&s_sd, now_ms());
    unlock();
    wake();
    return ok;
}

uint32_t wm_sd_format_ticket(void)
{
    if (!s_mx) {
        return 0;
    }
    lock();
    uint32_t t = 0;
    if (wsd_can_format(&s_sd)) {
        do {
            t = esp_random();
        } while (t == 0);
        s_ticket = t;
        s_ticket_ms = now_ms();
    }
    unlock();
    return t;
}

bool wm_sd_format(uint32_t ticket)
{
    if (!s_mx || ticket == 0) {
        return false;
    }
    lock();
    bool ok = ticket == s_ticket && (uint32_t)(now_ms() - s_ticket_ms) < TICKET_MS;
    s_ticket = 0; /* de un solo uso, salga bien o mal */
    if (ok) {
        ok = wsd_request_format(&s_sd, now_ms());
    }
    unlock();
    wake();
    return ok;
}

bool wm_sd_listen(wm_sd_listener_t cb, void *ctx)
{
    if (!s_mx || !cb) {
        return false;
    }
    bool ok = false;
    lock();
    for (int i = 0; i < LISTENERS && !ok; i++) {
        if (!s_lis[i].cb) {
            s_lis[i].cb = cb;
            s_lis[i].ctx = ctx;
            ok = true;
        }
    }
    unlock();
    return ok;
}
