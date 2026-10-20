/*
 * ws_logic — ver include/ws_logic.h
 */
#include "ws_logic.h"

#include <string.h>

const uint8_t ws_screen_timeouts[] = {5, 10, 15, 30, 60};
const size_t ws_screen_timeout_count = sizeof(ws_screen_timeouts) / sizeof(ws_screen_timeouts[0]);

void ws_defaults(ws_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->brightness = WS_BRIGHT_DEF;
    s->screen_to_s = WS_SCREEN_TO_DEF;
    s->h24 = true;
    s->tz_min = WS_TZ_DEF;
    s->wallpaper = 0;
    s->face = 0;
    s->aod = false;
    s->pshift = true;
    s->dim = WS_DIM_MED; /* un tercio, como hasta ahora */
    s->psave = false;
    s->psave_radio = WS_PS_RADIO_WIFI;
    s->ps_bright = 0;
    s->ps_flags = 0;
}

int ws_dim_level(uint8_t dim, int user_percent)
{
    if (user_percent < 0) {
        user_percent = WS_BRIGHT_DEF;
    }
    int v;
    switch (dim) {
    case WS_DIM_SOFT:
        v = user_percent / 2;
        break;
    case WS_DIM_MED:
        v = user_percent / 3;
        break;
    case WS_DIM_STRONG:
        v = user_percent * 15 / 100;
        break;
    default:
        return user_percent;
    }
    if (v < 5) {
        v = 5;
    }
    return v > user_percent ? user_percent : v;
}

bool ws_normalize(ws_settings_t *s)
{
    const ws_settings_t before = *s;
    if (s->brightness < WS_BRIGHT_MIN) {
        s->brightness = WS_BRIGHT_MIN;
    } else if (s->brightness > WS_BRIGHT_MAX) {
        s->brightness = WS_BRIGHT_MAX;
    }
    /* el apagado más cercano de la lista (en empate, el más largo) */
    uint8_t best = ws_screen_timeouts[0];
    for (size_t i = 0; i < ws_screen_timeout_count; i++) {
        const int d = ws_screen_timeouts[i] - s->screen_to_s;
        const int bd = best - s->screen_to_s;
        if ((d < 0 ? -d : d) <= (bd < 0 ? -bd : bd)) {
            best = ws_screen_timeouts[i];
        }
    }
    s->screen_to_s = best;
    s->h24 = s->h24 ? true : false;
    int tz = s->tz_min;
    if (tz < WS_TZ_MIN) {
        tz = WS_TZ_MIN;
    } else if (tz > WS_TZ_MAX) {
        tz = WS_TZ_MAX;
    }
    /* a múltiplos de 15 min, redondeando al más cercano */
    const int r = ((tz % WS_TZ_STEP) + WS_TZ_STEP) % WS_TZ_STEP;
    tz = r * 2 >= WS_TZ_STEP ? tz + (WS_TZ_STEP - r) : tz - r;
    if (tz > WS_TZ_MAX) {
        tz -= WS_TZ_STEP;
    }
    s->tz_min = (int16_t)tz;
    if (s->wallpaper >= WS_WALLPAPER_COUNT) {
        s->wallpaper = 0;
    }
    if (s->face >= WS_FACE_COUNT) {
        s->face = 0;
    }
    s->aod = s->aod ? true : false;
    s->pshift = s->pshift ? true : false;
    s->psave = s->psave ? true : false;
    if (s->dim >= WS_DIM_COUNT) {
        s->dim = WS_DIM_MED;
    }
    if (s->psave_radio >= WS_PS_RADIO_COUNT) {
        s->psave_radio = WS_PS_RADIO_WIFI;
    }
    if (s->ps_bright > WS_BRIGHT_MAX || (s->ps_bright && s->ps_bright < WS_BRIGHT_MIN)) {
        s->ps_bright = 0;
    }
    s->ps_flags &= (uint8_t)(WS_PS_PREV_VALID | WS_PS_PREV_AOD | WS_PS_PREV_WIFI | WS_PS_PREV_BT);
    return !ws_equal(&before, s);
}

bool ws_equal(const ws_settings_t *a, const ws_settings_t *b)
{
    return a->brightness == b->brightness && a->screen_to_s == b->screen_to_s && a->h24 == b->h24 &&
           a->tz_min == b->tz_min && a->wallpaper == b->wallpaper && a->face == b->face && a->aod == b->aod &&
           a->pshift == b->pshift && a->dim == b->dim && a->psave == b->psave &&
           a->psave_radio == b->psave_radio && a->ps_bright == b->ps_bright && a->ps_flags == b->ps_flags;
}

int64_t ws_commit_deadline(int64_t first_dirty_ms, int64_t last_change_ms)
{
    const int64_t quiet = last_change_ms + WS_QUIET_MS;
    const int64_t cap = first_dirty_ms + WS_MAX_MS;
    return quiet < cap ? quiet : cap;
}

uint32_t ws_crc32(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

size_t ws_blob_pack(uint8_t ver, const void *payload, size_t len, uint8_t *out, size_t cap)
{
    if (len > 0xFFFFu || cap < WS_BLOB_HDR + len || (len && !payload)) {
        return 0;
    }
    const uint32_t crc = ws_crc32(payload, len);
    out[0] = (uint8_t)(WS_BLOB_MAGIC & 0xFF);
    out[1] = (uint8_t)(WS_BLOB_MAGIC >> 8);
    out[2] = ver;
    out[3] = (uint8_t)(len & 0xFF);
    out[4] = (uint8_t)(len >> 8);
    for (int i = 0; i < 4; i++) {
        out[5 + i] = (uint8_t)(crc >> (8 * i));
    }
    if (len) {
        memcpy(out + WS_BLOB_HDR, payload, len);
    }
    return WS_BLOB_HDR + len;
}

bool ws_blob_unpack(const uint8_t *in, size_t n, uint8_t *ver, void *payload, size_t cap, size_t *len)
{
    if (!in || n < WS_BLOB_HDR) {
        return false;
    }
    if ((uint16_t)(in[0] | (in[1] << 8)) != WS_BLOB_MAGIC) {
        return false;
    }
    const size_t l = (size_t)(in[3] | (in[4] << 8));
    if (n != WS_BLOB_HDR + l || l > cap) {
        return false;
    }
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++) {
        crc |= (uint32_t)in[5 + i] << (8 * i);
    }
    if (ws_crc32(in + WS_BLOB_HDR, l) != crc) {
        return false;
    }
    if (l) {
        memcpy(payload, in + WS_BLOB_HDR, l);
    }
    if (ver) {
        *ver = in[2];
    }
    if (len) {
        *len = l;
    }
    return true;
}
