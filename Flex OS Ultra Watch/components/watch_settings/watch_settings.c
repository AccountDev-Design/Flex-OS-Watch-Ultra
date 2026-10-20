/*
 * watch_settings — ver include/watch_settings.h
 */
#include "watch_settings.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "watch_settings";

#define NS          "flexws"
#define TASK_STACK  4096
#define TASK_PRIO   2           /* por debajo de LVGL y de los servicios de I2C */
#define RETRY_MS    10000       /* tras un error de NVS */
#define NOTE_CHANGE (1u << 0)
#define NOTE_FLUSH  (1u << 1)

static const char *const s_blob_key[WS_BLOB_COUNT] = {"alarms"};

typedef struct {
    uint8_t data[WS_BLOB_HDR + WS_BLOB_MAX];
    size_t len; /* bytes empaquetados (0 = sin bloque) */
} blob_t;

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_flushed;
static TaskHandle_t s_task;
static watch_settings_t s_cur;   /* lo que ve el sistema */
static watch_settings_t s_saved; /* lo que hay en NVS */
static bool s_ver_saved;
static blob_t s_blob[WS_BLOB_COUNT];
static bool s_blob_dirty[WS_BLOB_COUNT];
static bool s_dirty;
static int64_t s_first_ms, s_last_ms;
static uint32_t s_gen;
static watch_settings_stats_t s_stats;
static bool s_started;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void mark_dirty_locked(void)
{
    const int64_t t = now_ms();
    if (!s_dirty) {
        s_dirty = true;
        s_first_ms = t;
    }
    s_last_ms = t;
    s_stats.pending = true;
}

/* ---- Lectura inicial ---- */

static void load(void)
{
    ws_defaults(&s_cur);
    s_saved = s_cur;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "primer arranque: ajustes por defecto");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no se pudo abrir \"%s\": %s", NS, esp_err_to_name(err));
        s_stats.last_err = err;
        return;
    }
    uint8_t u8;
    int16_t i16;
    uint8_t ver = 0;
    s_ver_saved = nvs_get_u8(h, "ver", &ver) == ESP_OK;
    if (nvs_get_u8(h, "bright", &u8) == ESP_OK) {
        s_cur.brightness = u8;
    }
    if (nvs_get_u8(h, "scr_to", &u8) == ESP_OK) {
        s_cur.screen_to_s = u8;
    }
    if (nvs_get_u8(h, "h24", &u8) == ESP_OK) {
        s_cur.h24 = u8 != 0;
    }
    if (nvs_get_i16(h, "tz_min", &i16) == ESP_OK) {
        s_cur.tz_min = i16;
    }
    if (nvs_get_u8(h, "wall", &u8) == ESP_OK) {
        s_cur.wallpaper = u8;
    }
    if (nvs_get_u8(h, "face", &u8) == ESP_OK) {
        s_cur.face = u8;
    }
    /* Claves añadidas después (AOD, protección AMOLED, ahorro): si faltan,
     * quedan los valores por defecto; el esquema no cambia. */
    if (nvs_get_u8(h, "aod", &u8) == ESP_OK) {
        s_cur.aod = u8 != 0;
    }
    if (nvs_get_u8(h, "pshift", &u8) == ESP_OK) {
        s_cur.pshift = u8 != 0;
    }
    if (nvs_get_u8(h, "dim", &u8) == ESP_OK) {
        s_cur.dim = u8;
    }
    if (nvs_get_u8(h, "psave", &u8) == ESP_OK) {
        s_cur.psave = u8 != 0;
    }
    if (nvs_get_u8(h, "ps_radio", &u8) == ESP_OK) {
        s_cur.psave_radio = u8;
    }
    if (nvs_get_u8(h, "ps_bright", &u8) == ESP_OK) {
        s_cur.ps_bright = u8;
    }
    if (nvs_get_u8(h, "ps_flags", &u8) == ESP_OK) {
        s_cur.ps_flags = u8;
    }
    s_saved = s_cur; /* lo que hay guardado, antes de normalizar */
    if (ws_normalize(&s_cur)) {
        ESP_LOGW(TAG, "valores guardados fuera de rango: corregidos");
    }
    for (int i = 0; i < WS_BLOB_COUNT; i++) {
        /* Se valida (magia, longitud, CRC) al leerlo con watch_settings_blob_get. */
        size_t n = sizeof(s_blob[i].data);
        if (nvs_get_blob(h, s_blob_key[i], s_blob[i].data, &n) == ESP_OK) {
            s_blob[i].len = n;
        }
    }
    s_stats.loaded = true;
    nvs_close(h);
    ESP_LOGI(TAG, "ajustes leidos (esquema %u): brillo %u %%, pantalla %u s, %s, zona %+d min, fondo %u, esfera %u",
             (unsigned)ver, s_cur.brightness, s_cur.screen_to_s, s_cur.h24 ? "24 h" : "12 h", s_cur.tz_min,
             s_cur.wallpaper, s_cur.face);
}

/* ---- Escritura (solo desde la tarea) ---- */

static esp_err_t write_changed(nvs_handle_t h, const watch_settings_t *n, const watch_settings_t *o, bool force)
{
    esp_err_t err = ESP_OK;
#define SET(cond, call)                                                                                          \
    do {                                                                                                         \
        if (err == ESP_OK && (force || (cond))) {                                                                \
            err = (call);                                                                                        \
        }                                                                                                        \
    } while (0)
    SET(n->brightness != o->brightness, nvs_set_u8(h, "bright", n->brightness));
    SET(n->screen_to_s != o->screen_to_s, nvs_set_u8(h, "scr_to", n->screen_to_s));
    SET(n->h24 != o->h24, nvs_set_u8(h, "h24", n->h24 ? 1 : 0));
    SET(n->tz_min != o->tz_min, nvs_set_i16(h, "tz_min", n->tz_min));
    SET(n->wallpaper != o->wallpaper, nvs_set_u8(h, "wall", n->wallpaper));
    SET(n->face != o->face, nvs_set_u8(h, "face", n->face));
    SET(n->aod != o->aod, nvs_set_u8(h, "aod", n->aod ? 1 : 0));
    SET(n->pshift != o->pshift, nvs_set_u8(h, "pshift", n->pshift ? 1 : 0));
    SET(n->dim != o->dim, nvs_set_u8(h, "dim", n->dim));
    SET(n->psave != o->psave, nvs_set_u8(h, "psave", n->psave ? 1 : 0));
    SET(n->psave_radio != o->psave_radio, nvs_set_u8(h, "ps_radio", n->psave_radio));
    SET(n->ps_bright != o->ps_bright, nvs_set_u8(h, "ps_bright", n->ps_bright));
    SET(n->ps_flags != o->ps_flags, nvs_set_u8(h, "ps_flags", n->ps_flags));
#undef SET
    return err;
}

static void commit(void)
{
    watch_settings_t cur;
    static blob_t blobs[WS_BLOB_COUNT];
    bool blob_dirty[WS_BLOB_COUNT];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cur = s_cur;
    for (int i = 0; i < WS_BLOB_COUNT; i++) {
        blob_dirty[i] = s_blob_dirty[i];
        if (blob_dirty[i]) {
            blobs[i] = s_blob[i];
            s_blob_dirty[i] = false;
        }
    }
    s_dirty = false;
    xSemaphoreGive(s_lock);

    const int64_t t0 = esp_timer_get_time();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (!s_ver_saved) {
            err = nvs_set_u8(h, "ver", WS_SCHEMA_VER);
        }
        if (err == ESP_OK) {
            err = write_changed(h, &cur, &s_saved, false);
        }
        for (int i = 0; i < WS_BLOB_COUNT && err == ESP_OK; i++) {
            if (blob_dirty[i]) {
                err = blobs[i].len ? nvs_set_blob(h, s_blob_key[i], blobs[i].data, blobs[i].len)
                                   : nvs_erase_key(h, s_blob_key[i]);
                if (err == ESP_ERR_NVS_NOT_FOUND) {
                    err = ESP_OK;
                }
            }
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    const int dt_ms = (int)((esp_timer_get_time() - t0) / 1000);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (err == ESP_OK) {
        s_saved = cur;
        s_ver_saved = true;
        s_stats.commits++;
    } else {
        /* Se reintenta más tarde: lo no guardado vuelve a quedar pendiente. */
        s_stats.errors++;
        s_stats.last_err = err;
        for (int i = 0; i < WS_BLOB_COUNT; i++) {
            s_blob_dirty[i] |= blob_dirty[i];
        }
        s_dirty = true;
        s_first_ms = now_ms();
        s_last_ms = s_first_ms + RETRY_MS - WS_QUIET_MS; /* siguiente intento en RETRY_MS */
    }
    s_stats.pending = s_dirty;
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ajustes guardados en %d ms", dt_ms);
    } else {
        ESP_LOGE(TAG, "no se pudieron guardar los ajustes: %s (se reintenta)", esp_err_to_name(err));
    }
}

static void settings_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_dirty && s_stats.nvs_ok) {
            const int64_t left = ws_commit_deadline(s_first_ms, s_last_ms) - now_ms();
            wait = left > 0 ? pdMS_TO_TICKS(left) + 1 : 0;
        }
        xSemaphoreGive(s_lock);
        uint32_t note = 0;
        xTaskNotifyWait(0, UINT32_MAX, &note, wait);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool due = s_dirty && ((note & NOTE_FLUSH) || now_ms() >= ws_commit_deadline(s_first_ms, s_last_ms));
        xSemaphoreGive(s_lock);
        if (due && s_stats.nvs_ok) {
            commit();
        }
        if (note & NOTE_FLUSH) {
            xSemaphoreGive(s_flushed);
        }
    }
}

/* ---- API ---- */

esp_err_t watch_settings_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_flushed = xSemaphoreCreateBinary();
    if (!s_lock || !s_flushed) {
        return ESP_ERR_NO_MEM;
    }
    ws_defaults(&s_cur);
    s_saved = s_cur;

    /* Único sitio que inicia NVS (también la usará el Wi-Fi en F5). Si la
     * partición está llena o es de otra versión de NVS, se borra SOLO la
     * partición nvs (recuperación estándar de ESP-IDF) y se avisa en el
     * registro: no toca otras particiones. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "particion nvs ilegible (%s): se borra y se reinicia (solo nvs)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    s_stats.nvs_ok = (err == ESP_OK);
    if (!s_stats.nvs_ok) {
        s_stats.last_err = err;
        ESP_LOGE(TAG, "NVS no disponible (%s): los ajustes no se guardaran", esp_err_to_name(err));
    } else {
        load();
    }
    BaseType_t ok = xTaskCreate(settings_task, "watch_settings", TASK_STACK, NULL, TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return s_stats.nvs_ok ? ESP_OK : err;
}

void watch_settings_get(watch_settings_t *out)
{
    if (!s_lock) {
        ws_defaults(out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cur;
    xSemaphoreGive(s_lock);
}

void watch_settings_update(const watch_settings_t *s)
{
    if (!s_lock) {
        return;
    }
    watch_settings_t n = *s;
    ws_normalize(&n);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool changed = !ws_equal(&n, &s_cur);
    if (changed) {
        s_cur = n;
        mark_dirty_locked();
        __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
    }
    xSemaphoreGive(s_lock);
    if (changed && s_task) {
        xTaskNotify(s_task, NOTE_CHANGE, eSetBits);
    }
}

uint32_t watch_settings_generation(void)
{
    return __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
}

esp_err_t watch_settings_flush(uint32_t timeout_ms)
{
    if (!s_task || !s_stats.nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskGetCurrentTaskHandle() == s_task) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_flushed, 0); /* descarta un aviso viejo */
    xTaskNotify(s_task, NOTE_FLUSH, eSetBits);
    if (xSemaphoreTake(s_flushed, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    watch_settings_stats_t st;
    watch_settings_stats(&st);
    return st.pending ? st.last_err : ESP_OK;
}

bool watch_settings_persistent(void)
{
    return s_stats.nvs_ok;
}

void watch_settings_stats(watch_settings_stats_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}

esp_err_t watch_settings_blob_get(ws_blob_id_t id, uint8_t *ver, void *buf, size_t *len)
{
    if (id >= WS_BLOB_COUNT || !len || !s_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_blob[id].len && ws_blob_unpack(s_blob[id].data, s_blob[id].len, ver, buf, *len, len);
    xSemaphoreGive(s_lock);
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t watch_settings_blob_set(ws_blob_id_t id, uint8_t ver, const void *buf, size_t len)
{
    if (id >= WS_BLOB_COUNT || len > WS_BLOB_MAX || !s_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    blob_t b;
    b.len = len || buf ? ws_blob_pack(ver, buf, len, b.data, sizeof(b.data)) : 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool changed = b.len != s_blob[id].len || memcmp(b.data, s_blob[id].data, b.len) != 0;
    if (changed) {
        s_blob[id] = b;
        s_blob_dirty[id] = true;
        mark_dirty_locked();
        __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
    }
    xSemaphoreGive(s_lock);
    if (changed && s_task) {
        xTaskNotify(s_task, NOTE_CHANGE, eSetBits);
    }
    return ESP_OK;
}
