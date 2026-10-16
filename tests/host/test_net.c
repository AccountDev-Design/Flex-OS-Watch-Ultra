/* Pruebas de net_logic: redes guardadas, reintentos, motivos y escaneo. */
#include <string.h>

#include "net_logic.h"
#include "test_util.h"
#include "ws_logic.h"

static void test_creds(void)
{
    net_creds_t c;
    net_creds_init(&c);
    CHECK(net_creds_put(&c, "Casa", "clave1234"));
    CHECK(net_creds_put(&c, "Oficina", "otra"));
    CHECK(net_creds_put(&c, "Cafe", ""));
    CHECK_EQ(c.n, 3);
    CHECK(strcmp(c.c[0].ssid, "Cafe") == 0); /* la más reciente primero */
    CHECK(net_creds_put(&c, "Casa", "nueva"));  /* actualizar la sube */
    CHECK_EQ(c.n, 3);
    CHECK(strcmp(c.c[0].ssid, "Casa") == 0 && strcmp(c.c[0].pass, "nueva") == 0);
    CHECK(net_creds_put(&c, "D", "1") && net_creds_put(&c, "E", "2")); /* lleno: sale la más antigua */
    CHECK_EQ(c.n, NET_MAX_SAVED);
    CHECK_EQ(net_creds_find(&c, "Oficina"), -1);
    CHECK(net_creds_find(&c, "Casa") >= 0);
    CHECK(!net_creds_put(&c, "", "x"));
    CHECK(!net_creds_put(&c, "123456789012345678901234567890123", "x")); /* 33 > 32 */
    CHECK(net_creds_remove(&c, "Casa") && !net_creds_remove(&c, "Casa"));
    CHECK_EQ(c.n, NET_MAX_SAVED - 1);

    /* Ida y vuelta, dentro del bloque con CRC de watch_settings */
    uint8_t raw[512], blob[600], back[600];
    const size_t n = net_creds_encode(&c, raw, sizeof(raw));
    CHECK(n > 1);
    const size_t bn = ws_blob_pack(1, raw, n, blob, sizeof(blob));
    CHECK(bn > n);
    uint8_t ver = 0;
    size_t rn = 0;
    CHECK(ws_blob_unpack(blob, bn, &ver, back, sizeof(back), &rn) && rn == n);
    net_creds_t d;
    CHECK(net_creds_decode(&d, back, rn));
    CHECK_EQ(d.n, c.n);
    CHECK(memcmp(&d, &c, sizeof(c)) == 0);
    /* Dañados: no se usan */
    blob[bn / 2] ^= 0x40;
    CHECK(!ws_blob_unpack(blob, bn, &ver, back, sizeof(back), &rn));
    CHECK(!net_creds_decode(&d, raw, n - 1) && d.n == 0); /* truncado */
    raw[0] = NET_MAX_SAVED + 1;
    CHECK(!net_creds_decode(&d, raw, n));
    const uint8_t zero_ssid[] = {1, 0, 0};
    CHECK(!net_creds_decode(&d, zero_ssid, sizeof(zero_ssid)));
    const uint8_t nul_in[] = {1, 2, 'a', 0, 0};
    CHECK(!net_creds_decode(&d, nul_in, sizeof(nul_in)));
    const uint8_t empty[] = {0};
    CHECK(net_creds_decode(&d, empty, 1) && d.n == 0);
    /* No cabe en el destino */
    CHECK_EQ(net_creds_encode(&c, raw, 4), 0);
}

static void test_retry_reason(void)
{
    CHECK_EQ(net_backoff_ms(0), 1000);
    CHECK_EQ(net_backoff_ms(1), 2000);
    CHECK_EQ(net_backoff_ms(5), 32000);
    CHECK_EQ(net_backoff_ms(6), 60000);
    CHECK_EQ(net_backoff_ms(100), 60000);
    CHECK_EQ(net_backoff_ms(-3), 1000);
    /* wifi_err_reason_t de ESP-IDF 5.5.5 */
    CHECK(net_reason_is_auth(202) && net_reason_is_auth(15) && net_reason_is_auth(204) && !net_reason_is_auth(201));
    CHECK(strcmp(net_reason_text(202), "Contraseña incorrecta") == 0);
    CHECK(strcmp(net_reason_text(201), "Red no encontrada") == 0);
    CHECK(strcmp(net_reason_text(200), "Señal perdida") == 0);
    CHECK(strcmp(net_reason_text(210), "Seguridad de la red no compatible") == 0);
    CHECK(strcmp(net_reason_text(0), "") == 0);
    CHECK(strcmp(net_reason_text(99), "Error de conexión") == 0);
}

static void test_scan(void)
{
    CHECK_EQ(net_rssi_bars(-40), 4);
    CHECK_EQ(net_rssi_bars(-60), 3);
    CHECK_EQ(net_rssi_bars(-70), 2);
    CHECK_EQ(net_rssi_bars(-80), 1);
    CHECK_EQ(net_rssi_bars(-95), 0);
    net_creds_t c;
    net_creds_init(&c);
    net_creds_put(&c, "Casa", "x");
    net_ap_t a[6] = {
        {"Vecino", -50, true, false}, {"", -40, true, false},     {"Casa", -80, true, false},
        {"Cafe", -60, false, false},  {"Vecino", -45, true, false}, {"Casa", -70, true, false},
    };
    const int n = net_scan_sort(a, 6, &c);
    CHECK_EQ(n, 3); /* sin oculta ni repetidas */
    CHECK(strcmp(a[0].ssid, "Casa") == 0 && a[0].saved && a[0].rssi == -70); /* guardada primero, la más fuerte */
    CHECK(strcmp(a[1].ssid, "Vecino") == 0 && a[1].rssi == -45);
    CHECK(strcmp(a[2].ssid, "Cafe") == 0 && !a[2].secure);
    CHECK_EQ(net_pick_saved(a, n), 0);
    net_creds_init(&c);
    net_ap_t b[1] = {{"Otra", -40, true, false}};
    CHECK_EQ(net_scan_sort(b, 1, &c), 1);
    CHECK_EQ(net_pick_saved(b, 1), -1);
}

void test_net_all(void)
{
    test_creds();
    test_retry_reason();
    test_scan();
}
