/*
 * wa_logic — ver include/wa_logic.h
 */
#include "wa_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void wa_wav_header(uint8_t out[WA_WAV_HDR], uint32_t rate, uint16_t channels, uint32_t data_bytes)
{
    const uint16_t align = (uint16_t)(channels * 2u);
    memcpy(out, "RIFF", 4);
    le32(out + 4, data_bytes == WA_WAV_STREAMING ? WA_WAV_STREAMING
                  : data_bytes > WA_WAV_MAX_DATA  ? 0xFFFFFFFFu
                                                  : data_bytes + 36u);
    memcpy(out + 8, "WAVEfmt ", 8);
    le32(out + 16, 16);
    le16(out + 20, 1); /* PCM */
    le16(out + 22, channels);
    le32(out + 24, rate);
    le32(out + 28, rate * align);
    le16(out + 32, align);
    le16(out + 34, 16);
    memcpy(out + 36, "data", 4);
    le32(out + 40, data_bytes);
}

bool wa_wav_parse44(const uint8_t h[WA_WAV_HDR], uint32_t *rate, uint16_t *channels, uint32_t *data_bytes)
{
    if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) || rd32(h + 16) != 16 || rd16(h + 20) != 1 ||
        rd16(h + 34) != 16 || memcmp(h + 36, "data", 4)) {
        return false;
    }
    const uint16_t ch = rd16(h + 22);
    const uint32_t r = rd32(h + 24);
    if (ch < 1 || ch > 2 || r < 4000 || r > 192000 || rd16(h + 32) != ch * 2u || rd32(h + 28) != r * ch * 2u) {
        return false;
    }
    if (rate) {
        *rate = r;
    }
    if (channels) {
        *channels = ch;
    }
    if (data_bytes) {
        *data_bytes = rd32(h + 40);
    }
    return true;
}

size_t wa_tdm_extract(const int16_t *tdm, size_t frames, int16_t *out, bool stereo)
{
    for (size_t f = 0; f < frames; f++) {
        const int16_t l = tdm[f * WA_TDM_SLOTS + WA_TDM_SLOT_MIC1];
        const int16_t r = tdm[f * WA_TDM_SLOTS + WA_TDM_SLOT_MIC2];
        if (stereo) {
            out[2 * f] = l;
            out[2 * f + 1] = r;
        } else {
            out[f] = (int16_t)(((int32_t)l + (int32_t)r) / 2); /* sin desbordar */
        }
    }
    return stereo ? frames * 2 : frames;
}

void wa_meter_init(wa_meter_t *m)
{
    memset(m, 0, sizeof(*m));
    m->peak_db = WA_METER_FLOOR_DB;
    m->rms_db = WA_METER_FLOOR_DB;
}

float wa_meter_map(float db)
{
    if (db <= WA_METER_FLOOR_DB) {
        return 0.0f;
    }
    if (db >= 0.0f) {
        return 1.0f;
    }
    return 1.0f - db / WA_METER_FLOOR_DB;
}

static float to_db(double v)
{
    if (v < 1.0) {
        return WA_METER_FLOOR_DB;
    }
    const float db = (float)(20.0 * log10(v / 32768.0));
    return db < WA_METER_FLOOR_DB ? WA_METER_FLOOR_DB : db;
}

void wa_meter_feed(wa_meter_t *m, const int16_t *s, size_t n, uint32_t now_ms)
{
    int32_t peak = 0;
    double sum = 0.0;
    bool clip = false;
    for (size_t i = 0; i < n; i++) {
        const int32_t v = s[i] < 0 ? -(int32_t)s[i] : s[i];
        if (v > peak) {
            peak = v;
        }
        if (v >= 32767) {
            clip = true;
        }
        sum += (double)s[i] * (double)s[i];
    }
    m->peak_db = to_db(peak);
    m->rms_db = n ? to_db(sqrt(sum / (double)n)) : WA_METER_FLOOR_DB;
    if (clip) {
        m->clip_ms = now_ms;
        m->has_clip = true;
    }
    /* Se pinta el RMS (lo que se oye) con un poco del pico para que las
     * consonantes se vean. */
    const float target = wa_meter_map(m->rms_db * 0.7f + m->peak_db * 0.3f);
    if (!m->started) {
        m->level = target;
        m->started = true;
    } else if (target >= m->level) {
        m->level = target; /* subida inmediata */
    } else {
        const uint32_t dt = now_ms - m->last_ms;
        const float k = expf(-(float)dt / (float)WA_METER_RELEASE_MS);
        m->level = target + (m->level - target) * k;
    }
    m->last_ms = now_ms;
}

bool wa_meter_clipping(const wa_meter_t *m, uint32_t now_ms)
{
    return m->has_clip && (uint32_t)(now_ms - m->clip_ms) < WA_METER_CLIP_HOLD_MS;
}

uint32_t wa_bytes_per_s(uint32_t rate, uint16_t channels)
{
    return rate * channels * 2u;
}

uint32_t wa_rec_seconds_left(uint64_t free_bytes, uint64_t reserve, uint64_t written, uint32_t bps,
                             uint64_t max_file)
{
    if (bps == 0) {
        return 0;
    }
    uint64_t room = free_bytes > reserve ? free_bytes - reserve : 0;
    uint64_t cap = max_file < (uint64_t)WA_WAV_MAX_DATA + WA_WAV_HDR ? max_file : (uint64_t)WA_WAV_MAX_DATA + WA_WAV_HDR;
    cap = cap > written + WA_WAV_HDR ? cap - written - WA_WAV_HDR : 0;
    if (room > cap) {
        room = cap;
    }
    const uint64_t s = room / bps;
    return s > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)s;
}

bool wa_rec_must_stop(uint64_t free_bytes, uint64_t reserve, uint64_t written, uint32_t block, uint64_t max_file)
{
    if (free_bytes < reserve + block) {
        return true;
    }
    const uint64_t cap = max_file < (uint64_t)WA_WAV_MAX_DATA + WA_WAV_HDR ? max_file : (uint64_t)WA_WAV_MAX_DATA + WA_WAV_HDR;
    return written + WA_WAV_HDR + block > cap;
}

void wa_fmt_time(uint32_t ms, char *out, size_t n)
{
    const uint32_t s = ms / 1000u;
    if (s >= 3600u) {
        snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u), (unsigned)(s % 60u));
    } else {
        snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
    }
}
