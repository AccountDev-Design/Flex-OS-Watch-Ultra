/*
 * wm_vol — los dos volúmenes y el destino elegido. Ver include/watch_media.h.
 */
#include "watch_media.h"

#include <stdio.h>
#include <string.h>

#include "esp_vfs_fat.h"
#include "watch_settings.h"
#include "watch_storage.h"

static bool watch_ready(watch_storage_info_t *si)
{
    watch_storage_info(si);
    /* BUSY es una operación de mantenimiento a petición (prueba, recuento):
     * mientras dura, no se empieza nada nuevo. */
    return si->state == STG_MOUNTED;
}

bool wm_vol_info(wm_vol_t v, wm_vol_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (v == WM_VOL_WATCH) {
        watch_storage_info_t si;
        out->ready = watch_ready(&si);
        if (out->ready && si.total >= si.used) {
            out->total_bytes = si.total;
            out->free_bytes = (uint64_t)(si.total - si.used);
        }
        return out->ready;
    }
    if (v != WM_VOL_SD || !wm_sd_lease()) {
        return false;
    }
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(WM_SD_BASE, &total, &free_b) == ESP_OK) {
        out->total_bytes = total;
        out->free_bytes = free_b;
    }
    out->ready = true;
    wm_sd_unlease();
    return true;
}

wm_dest_status_t wm_dest_get(wm_vol_t *vol)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    const wm_vol_t v = ws.media_dest == WS_DEST_SD ? WM_VOL_SD : WM_VOL_WATCH;
    if (vol) {
        *vol = v;
    }
    if (v == WM_VOL_WATCH) {
        watch_storage_info_t si;
        return watch_ready(&si) ? WM_DEST_OK : WM_DEST_WATCH_NOT_READY;
    }
    wm_sd_info_t i;
    wm_sd_info(&i);
    if (i.state == WSD_READY) {
        return WM_DEST_OK;
    }
    return i.state == WSD_ABSENT ? WM_DEST_SD_ABSENT : WM_DEST_SD_NOT_READY;
}

void wm_dest_set(wm_vol_t vol)
{
    watch_settings_t ws;
    watch_settings_get(&ws);
    const uint8_t d = vol == WM_VOL_SD ? WS_DEST_SD : WS_DEST_WATCH;
    if (ws.media_dest != d) {
        ws.media_dest = d;
        watch_settings_update(&ws);
    }
}
