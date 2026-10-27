/*
 * Pruebas de PC de los ajustes persistentes (ws_logic, F4.1).
 */
#include <string.h>

#include "test_util.h"
#include "ws_logic.h"

static void test_defaults(void)
{
    ws_settings_t s;
    memset(&s, 0xAB, sizeof(s));
    ws_defaults(&s);
    CHECK_EQ(s.brightness, 70);
    CHECK_EQ(s.screen_to_s, 15);
    CHECK(s.h24);
    CHECK_EQ(s.tz_min, -300);
    CHECK_EQ(s.wallpaper, 0);
    CHECK_EQ(s.face, 0);
    /* Los valores por defecto ya son válidos. */
    CHECK(!ws_normalize(&s));
}

static void test_normalize(void)
{
    ws_settings_t s;
    ws_defaults(&s);
    s.brightness = 0;
    CHECK(ws_normalize(&s));
    CHECK_EQ(s.brightness, 10);
    s.brightness = 255;
    ws_normalize(&s);
    CHECK_EQ(s.brightness, 100);

    /* Apagado de pantalla: al valor más cercano de la lista. */
    static const struct { uint8_t in, out; } to[] = {
        {0, 5}, {5, 5}, {7, 5}, {8, 10}, {12, 10}, {13, 15}, {22, 15}, {23, 30}, {44, 30}, {45, 60}, {255, 60},
    };
    for (size_t i = 0; i < sizeof(to) / sizeof(to[0]); i++) {
        s.screen_to_s = to[i].in;
        ws_normalize(&s);
        CHECK_EQ(s.screen_to_s, to[i].out);
    }

    /* Zona horaria: dentro de UTC-12..UTC+14 y en múltiplos de 15 min. */
    static const struct { int in, out; } tz[] = {
        {-300, -300}, {330, 330}, {345, 345}, {-1000, -720}, {900, 840}, {7, 0}, {8, 15}, {-7, 0}, {-8, -15},
        {839, 840}, {-719, -720}, {0, 0},
    };
    for (size_t i = 0; i < sizeof(tz) / sizeof(tz[0]); i++) {
        s.tz_min = (int16_t)tz[i].in;
        ws_normalize(&s);
        CHECK_EQ(s.tz_min, tz[i].out);
    }
    s.wallpaper = 9;
    s.face = WS_FACE_COUNT;
    CHECK(ws_normalize(&s));
    CHECK_EQ(s.wallpaper, 0);
    CHECK_EQ(s.face, 0);

    ws_settings_t a, b;
    ws_defaults(&a);
    b = a;
    CHECK(ws_equal(&a, &b));
    b.h24 = false;
    CHECK(!ws_equal(&a, &b));
}

static void test_deadline(void)
{
    /* Un cambio suelto: 1,5 s después. */
    CHECK_EQ(ws_commit_deadline(1000, 1000), 2500);
    /* Cambios seguidos (un deslizador): espera a que pare... */
    CHECK_EQ(ws_commit_deadline(1000, 9000), 10500);
    /* ...pero nunca más de 30 s desde el primero. */
    CHECK_EQ(ws_commit_deadline(1000, 40000), 31000);
    /* Simulación: un cambio cada 200 ms durante 60 s. */
    int64_t first = -1, last = 0;
    int commits = 0;
    for (int64_t t = 0; t <= 62000; t += 100) {
        if (t <= 60000 && t % 200 == 0) {
            if (first < 0) {
                first = t;
            }
            last = t;
        }
        if (first >= 0 && t >= ws_commit_deadline(first, last)) {
            commits++;
            first = -1;
        }
    }
    CHECK_EQ(commits, 2); /* a los 30 s y a los 60,2 s (que ya incluye el último cambio) */
}

static void test_blob(void)
{
    uint8_t buf[64], out[32];
    const char payload[] = "alarma 07:30";
    const size_t n = ws_blob_pack(3, payload, sizeof(payload), buf, sizeof(buf));
    CHECK_EQ(n, WS_BLOB_HDR + sizeof(payload));
    uint8_t ver = 0;
    size_t len = 0;
    CHECK(ws_blob_unpack(buf, n, &ver, out, sizeof(out), &len));
    CHECK_EQ(ver, 3);
    CHECK_EQ(len, sizeof(payload));
    CHECK(memcmp(out, payload, len) == 0);

    /* Cualquier byte dañado se detecta. */
    int detected = 0;
    for (size_t i = 0; i < n; i++) {
        for (int bit = 0; bit < 8; bit++) {
            buf[i] ^= (uint8_t)(1u << bit);
            if (!ws_blob_unpack(buf, n, &ver, out, sizeof(out), &len) || (i == 2)) {
                detected++; /* el byte 2 es la versión: cambia la versión, no los datos */
            }
            buf[i] ^= (uint8_t)(1u << bit);
        }
    }
    CHECK_EQ(detected, (int)(n * 8));
    /* Truncado, sin sitio, cabecera corta. */
    CHECK(!ws_blob_unpack(buf, n - 1, &ver, out, sizeof(out), &len));
    CHECK(!ws_blob_unpack(buf, n, &ver, out, 4, &len));
    CHECK(!ws_blob_unpack(buf, 5, &ver, out, sizeof(out), &len));
    CHECK(!ws_blob_unpack(NULL, n, &ver, out, sizeof(out), &len));
    /* No cabe al empaquetar. */
    CHECK_EQ(ws_blob_pack(1, payload, sizeof(payload), buf, WS_BLOB_HDR + 3), 0);
    /* Bloque vacío válido. */
    CHECK_EQ(ws_blob_pack(1, NULL, 0, buf, sizeof(buf)), WS_BLOB_HDR);
    CHECK(ws_blob_unpack(buf, WS_BLOB_HDR, &ver, out, sizeof(out), &len));
    CHECK_EQ(len, 0);
    /* CRC-32 estándar (IEEE): "123456789" -> CBF43926. */
    CHECK(ws_crc32("123456789", 9) == 0xCBF43926u);
}

/* Ajustes de pantalla, protección AMOLED y ahorro */
static void test_power_fields(void)
{
    ws_settings_t s;
    ws_defaults(&s);
    CHECK(!s.aod && s.pshift && s.dim == WS_DIM_MED && !s.psave && s.psave_radio == WS_PS_RADIO_WIFI);
    s.dim = 9;
    s.psave_radio = 7;
    s.ps_bright = 5;
    s.ps_flags = 0xFF;
    s.face = WS_FACE_COUNT;
    CHECK(ws_normalize(&s));
    CHECK_EQ(s.dim, WS_DIM_MED);
    CHECK_EQ(s.psave_radio, WS_PS_RADIO_WIFI);
    CHECK_EQ(s.ps_bright, 0);
    CHECK_EQ(s.ps_flags, WS_PS_PREV_VALID | WS_PS_PREV_AOD | WS_PS_PREV_WIFI | WS_PS_PREV_BT);
    CHECK_EQ(s.face, 0);
    ws_settings_t t = s;
    t.aod = true;
    CHECK(!ws_equal(&s, &t));
    /* Niveles de atenuación */
    CHECK_EQ(ws_dim_level(WS_DIM_MED, 70), 23);
    CHECK_EQ(ws_dim_level(WS_DIM_SOFT, 70), 35);
    CHECK_EQ(ws_dim_level(WS_DIM_STRONG, 70), 10);
    CHECK_EQ(ws_dim_level(WS_DIM_OFF, 70), 70);
    CHECK_EQ(ws_dim_level(WS_DIM_STRONG, 10), 5);
    CHECK_EQ(ws_dim_level(WS_DIM_MED, 3), 3);
}

/* Multimedia: destino, volumen y calidad de la grabadora */
static void test_media_fields(void)
{
    ws_settings_t s;
    ws_defaults(&s);
    CHECK_EQ(s.media_dest, WS_DEST_WATCH); /* lo seguro: la memoria del reloj */
    CHECK_EQ(s.media_vol, WS_MEDIA_VOL_DEF);
    CHECK_EQ(s.rec_quality, WS_REC_STANDARD);
    CHECK(!ws_normalize(&s));
    s.media_dest = WS_DEST_SD;
    s.media_vol = 0;
    s.rec_quality = WS_REC_VOICE;
    CHECK(!ws_normalize(&s)); /* valores válidos: no se tocan */
    CHECK_EQ(s.media_dest, WS_DEST_SD);
    s.media_dest = 9;
    s.media_vol = 101;
    s.rec_quality = 4;
    CHECK(ws_normalize(&s));
    CHECK_EQ(s.media_dest, WS_DEST_WATCH);
    CHECK_EQ(s.media_vol, 100);
    CHECK_EQ(s.rec_quality, WS_REC_STANDARD);
    ws_settings_t t = s;
    t.media_dest = WS_DEST_SD;
    CHECK(!ws_equal(&s, &t));
    t = s;
    t.media_vol = 1;
    CHECK(!ws_equal(&s, &t));
    t = s;
    t.rec_quality = WS_REC_VOICE;
    CHECK(!ws_equal(&s, &t));
}

void test_settings_all(void)
{
    test_media_fields();
    test_power_fields();
    test_defaults();
    test_normalize();
    test_deadline();
    test_blob();
}
