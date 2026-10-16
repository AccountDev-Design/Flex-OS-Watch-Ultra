/*
 * Ajustes persistentes en la vista previa: la misma API que watch_settings,
 * en RAM (sin NVS). Cuenta las actualizaciones reales para comprobar que la
 * interfaz no escribe en cada paso de un deslizador.
 */
#include <string.h>

#include "sim.h"
#include "watch_settings.h"

static watch_settings_t s_cur;
static bool s_init;
static uint32_t s_gen;
static uint8_t s_blob[WS_BLOB_COUNT][WS_BLOB_HDR + WS_BLOB_MAX];
static size_t s_blob_len[WS_BLOB_COUNT];
int sim_ws_updates;

static void ensure(void)
{
    if (!s_init) {
        ws_defaults(&s_cur);
        s_init = true;
    }
}

void sim_ws_reset(const watch_settings_t *s)
{
    ensure();
    if (s) {
        s_cur = *s;
        ws_normalize(&s_cur);
    } else {
        ws_defaults(&s_cur);
    }
    memset(s_blob_len, 0, sizeof(s_blob_len));
    sim_ws_updates = 0;
    s_gen++;
}

esp_err_t watch_settings_start(void)
{
    ensure();
    return ESP_OK;
}

void watch_settings_get(watch_settings_t *out)
{
    ensure();
    *out = s_cur;
}

void watch_settings_update(const watch_settings_t *s)
{
    ensure();
    watch_settings_t n = *s;
    ws_normalize(&n);
    if (!ws_equal(&n, &s_cur)) {
        s_cur = n;
        s_gen++;
        sim_ws_updates++;
    }
}

uint32_t watch_settings_generation(void)
{
    return s_gen;
}

esp_err_t watch_settings_flush(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return ESP_OK;
}

bool watch_settings_persistent(void)
{
    return true;
}

void watch_settings_stats(watch_settings_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->nvs_ok = true;
    out->commits = (uint32_t)sim_ws_updates;
}

esp_err_t watch_settings_blob_get(ws_blob_id_t id, uint8_t *ver, void *buf, size_t *len)
{
    if (id >= WS_BLOB_COUNT || !len) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_blob_len[id] && ws_blob_unpack(s_blob[id], s_blob_len[id], ver, buf, *len, len) ? ESP_OK
                                                                                            : ESP_ERR_NOT_FOUND;
}

esp_err_t watch_settings_blob_set(ws_blob_id_t id, uint8_t ver, const void *buf, size_t len)
{
    if (id >= WS_BLOB_COUNT || len > WS_BLOB_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_blob_len[id] = ws_blob_pack(ver, buf, len, s_blob[id], sizeof(s_blob[id]));
    s_gen++;
    sim_ws_updates++;
    return ESP_OK;
}
