/*
 * Pruebas de watch_audio (lógica pura): cabecera WAV, ranuras TDM del ES7210,
 * medidor de nivel y espacio de la grabadora.
 */
#include <math.h>
#include <string.h>

#include "test_util.h"
#include "wa_logic.h"

static void test_wav(void)
{
    uint8_t h[WA_WAV_HDR];
    wa_wav_header(h, 24000, 1, 48000);
    CHECK(!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVEfmt ", 8) && !memcmp(h + 36, "data", 4));
    uint32_t rate = 0, data = 0;
    uint16_t ch = 0;
    CHECK(wa_wav_parse44(h, &rate, &ch, &data));
    CHECK_EQ(rate, 24000);
    CHECK_EQ(ch, 1);
    CHECK_EQ(data, 48000);
    CHECK_EQ(h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24, 48036); /* RIFF = datos + 36 */
    CHECK_EQ(h[28] | h[29] << 8 | h[30] << 16, 48000);                       /* bytes por segundo */
    CHECK_EQ(h[32], 2);                                                      /* alineación */
    wa_wav_header(h, 24000, 2, 0);
    CHECK(wa_wav_parse44(h, &rate, &ch, &data));
    CHECK_EQ(ch, 2);
    CHECK_EQ(data, 0);
    CHECK_EQ(h[28] | h[29] << 8 | h[30] << 16, 96000);
    /* "En flujo": los dos tamaños a 0xFFFFFFFF (los lectores los recortan al archivo). */
    wa_wav_header(h, 24000, 1, WA_WAV_STREAMING);
    CHECK(wa_wav_parse44(h, NULL, NULL, &data));
    CHECK(data == WA_WAV_STREAMING);
    CHECK(h[4] == 0xFF && h[5] == 0xFF && h[6] == 0xFF && h[7] == 0xFF);
    /* El máximo que cabe: RIFF = data + 36 sin desbordar. */
    wa_wav_header(h, 48000, 2, WA_WAV_MAX_DATA);
    CHECK((h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24) == WA_WAV_MAX_DATA + 36u);
    /* Cabeceras que no son las nuestras. */
    wa_wav_header(h, 24000, 1, 10);
    h[20] = 3; /* float */
    CHECK(!wa_wav_parse44(h, NULL, NULL, NULL));
    wa_wav_header(h, 24000, 3, 10);
    CHECK(!wa_wav_parse44(h, NULL, NULL, NULL));
    wa_wav_header(h, 100, 1, 10);
    CHECK(!wa_wav_parse44(h, NULL, NULL, NULL));
    wa_wav_header(h, 24000, 1, 10);
    h[28] ^= 1; /* bytes por segundo incoherentes */
    CHECK(!wa_wav_parse44(h, NULL, NULL, NULL));
}

static void test_tdm(void)
{
    /* 3 muestras: ranuras MIC1, MIC3 (eco), MIC2, MIC4. */
    const int16_t tdm[] = {100, 7777, 300, -1, -32768, 5, -32768, 5, 32767, 0, 32767, 0};
    int16_t out[6];
    CHECK_EQ(wa_tdm_extract(tdm, 3, out, true), 6);
    CHECK_EQ(out[0], 100);
    CHECK_EQ(out[1], 300); /* MIC2 viene de la ranura 2, no de la 1 (eco) */
    CHECK_EQ(out[2], -32768);
    CHECK_EQ(out[5], 32767);
    CHECK_EQ(wa_tdm_extract(tdm, 3, out, false), 3);
    CHECK_EQ(out[0], 200);
    CHECK_EQ(out[1], -32768); /* media sin desbordar */
    CHECK_EQ(out[2], 32767);
}

static void test_meter(void)
{
    wa_meter_t m;
    wa_meter_init(&m);
    int16_t silence[480] = {0};
    wa_meter_feed(&m, silence, 480, 1000);
    CHECK(m.level == 0.0f);
    CHECK(m.rms_db <= WA_METER_FLOOR_DB);
    /* Un seno a -6 dBFS: RMS ~ -9 dB, pico ~ -6 dB. */
    int16_t sine[480];
    for (int i = 0; i < 480; i++) {
        sine[i] = (int16_t)(16384.0 * sin(2.0 * 3.14159265 * 1000.0 * i / 24000.0));
    }
    wa_meter_feed(&m, sine, 480, 1020);
    CHECK(fabsf(m.peak_db - (-6.0f)) < 0.2f);
    CHECK(fabsf(m.rms_db - (-9.03f)) < 0.3f);
    const float loud = m.level;
    CHECK(loud > 0.8f && loud < 0.9f);
    /* Sube al momento y baja suave: tras 300 ms de silencio queda ~37 %. */
    wa_meter_feed(&m, silence, 480, 1320);
    CHECK(m.level > 0.0f && m.level < loud);
    CHECK(fabsf(m.level - loud * expf(-1.0f)) < 0.02f);
    wa_meter_feed(&m, silence, 480, 3320);
    CHECK(m.level < 0.01f);
    /* Recorte: se marca y se mantiene un rato. */
    CHECK(!wa_meter_clipping(&m, 3320));
    int16_t clip[4] = {0, 32767, -32768, 0};
    wa_meter_feed(&m, clip, 4, 4000);
    CHECK(wa_meter_clipping(&m, 4000));
    CHECK(wa_meter_clipping(&m, 4000 + WA_METER_CLIP_HOLD_MS - 1));
    CHECK(!wa_meter_clipping(&m, 4000 + WA_METER_CLIP_HOLD_MS));
    CHECK(wa_meter_map(-60.0f) == 0.0f && wa_meter_map(0.0f) == 1.0f && fabsf(wa_meter_map(-30.0f) - 0.5f) < 1e-6f);
    CHECK(wa_meter_map(-90.0f) == 0.0f && wa_meter_map(3.0f) == 1.0f);
    wa_meter_feed(&m, clip, 0, 5000); /* bloque vacío: sin dividir por 0 */
    CHECK(m.rms_db <= WA_METER_FLOOR_DB);
}

static void test_space(void)
{
    const uint32_t mono = wa_bytes_per_s(24000, 1);
    CHECK_EQ(mono, 48000);
    CHECK_EQ(wa_bytes_per_s(24000, 2), 96000);
    const uint64_t fat32 = 0xFFFFFFFFull; /* 4 GB - 1 por archivo */
    /* 10 MB libres, 512 KB de reserva: (10 MB - 512 KB) / 48000 */
    CHECK_EQ(wa_rec_seconds_left(10u * 1024 * 1024, 512u * 1024, 0, mono, fat32),
             (10u * 1024 * 1024 - 512u * 1024) / 48000u);
    CHECK_EQ(wa_rec_seconds_left(100, 512u * 1024, 0, mono, fat32), 0);   /* por debajo de la reserva */
    CHECK_EQ(wa_rec_seconds_left(1000, 0, 0, 0, fat32), 0);             /* sin tasa */
    /* Tarjeta de 2 TB vacía: el límite es el archivo (FAT32), no la tarjeta. */
    const uint32_t s = wa_rec_seconds_left(2000000000000ull, 4u << 20, 0, mono, fat32);
    CHECK_EQ(s, (uint32_t)((fat32 - WA_WAV_HDR) / mono));
    CHECK(s > 24u * 3600u); /* ~24,8 h en mono */
    /* Con lo ya escrito. */
    CHECK_EQ(wa_rec_seconds_left(2000000000000ull, 0, 4000000000ull, mono, fat32),
             (uint32_t)((fat32 - 4000000000ull - WA_WAV_HDR) / mono));
    CHECK_EQ(wa_rec_seconds_left(2000000000000ull, 0, fat32, mono, fat32), 0);
    /* Parar. */
    CHECK(!wa_rec_must_stop(10u << 20, 512u << 10, 0, 16384, fat32));
    CHECK(wa_rec_must_stop((512u << 10) + 100, 512u << 10, 0, 16384, fat32));
    CHECK(wa_rec_must_stop(1ull << 40, 0, fat32 - WA_WAV_HDR - 100, 16384, fat32));
    CHECK(!wa_rec_must_stop(1ull << 40, 0, fat32 - WA_WAV_HDR - 16384, 16384, fat32));
    char t[16];
    wa_fmt_time(0, t, sizeof(t));
    CHECK(!strcmp(t, "0:00"));
    wa_fmt_time(65000, t, sizeof(t));
    CHECK(!strcmp(t, "1:05"));
    wa_fmt_time(3723000, t, sizeof(t));
    CHECK(!strcmp(t, "1:02:03"));
}

void test_audio_all(void)
{
    test_wav();
    test_tdm();
    test_meter();
    test_space();
}
