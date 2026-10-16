/*
 * ble_link — ver ble_link.h
 *
 * Montaje del canal (flexphone-messages.md §7.3, GattServer.kt):
 *   1. Escaneo pasivo buscando el UUID de servicio f1580001-… en el anuncio
 *      (el teléfono anuncia solo ese UUID). 30 s rápido y luego de fondo
 *      con un 3,5 % de ciclo (itvl 1,28 s, ventana 45 ms).
 *   2. Conexión y, a la vez, intercambio de MTU (se pide 247) y cifrado:
 *      las características del teléfono son *_ENCRYPTED, así que hace falta
 *      emparejar a nivel BLE (Just Works; el teléfono pregunta una vez).
 *   3. Con el enlace cifrado: servicio -> características RX/TX -> CCCD de TX
 *      -> notificaciones activadas ANTES del HELLO (el servidor no mira el
 *      CCCD y enviaría igual, §7.3.22).
 *   4. MTU >= 247 obligatorio: los dos reensambladores exigen fragmentos
 *      intermedios de 226 B (trama de 244 B + 3 de ATT). Con menos, el
 *      canal se declara inservible y se dice, en vez de perder mensajes.
 *
 * Una trama = una escritura ATT (Write Request) en RX, de una en una; una
 * notificación de TX = una trama. NimBLE es seguro entre tareas: la tarea
 * del enlace escribe y la de NimBLE recibe; los anillos van con mutex.
 */
#include "ble_link.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void); /* NimBLE no lo declara en ninguna cabecera pública */

static const char *TAG = "ble_link";

/* GattServer.kt:77-82. BLE_UUID128_INIT va en orden de bytes inverso. */
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0x4b, 0x4c, 0x58, 0xf1, 0x78, 0x65, 0x6c, 0x8f, 0x53, 0x4f,
                                                       0x65, 0x4c, 0x01, 0x00, 0x58, 0xf1);
static const ble_uuid128_t UUID_RX = BLE_UUID128_INIT(0x4b, 0x4c, 0x58, 0xf1, 0x78, 0x65, 0x6c, 0x8f, 0x53, 0x4f,
                                                      0x65, 0x4c, 0x02, 0x00, 0x58, 0xf1);
static const ble_uuid128_t UUID_TX = BLE_UUID128_INIT(0x4b, 0x4c, 0x58, 0xf1, 0x78, 0x65, 0x6c, 0x8f, 0x53, 0x4f,
                                                      0x65, 0x4c, 0x03, 0x00, 0x58, 0xf1);

#define MTU_NEEDED      (FLNK_MAX_FRAME + 3) /* 247 */
#define FAST_SCAN_MS    30000
#define CONNECT_TIMEOUT 15000

typedef enum {
    PH_IDLE,      /* apagado */
    PH_SCAN,      /* buscando el anuncio */
    PH_CONNECT,   /* conectando */
    PH_SECURE,    /* cifrando / emparejando BLE */
    PH_DISCOVER,  /* servicio, características, CCCD */
    PH_SUBSCRIBE, /* activando notificaciones */
    PH_OPEN,      /* canal listo: se mueven tramas */
    PH_WAIT,      /* esperando para volver a buscar */
} phase_t;

static struct {
    SemaphoreHandle_t mtx;
    void (*wake)(void);
    bool inited, synced, want;
    phase_t ph;
    bool failed;       /* PH_WAIT tras un fallo (canal FAILED) y no tras una caída normal */
    bool mtu_done, sub_done;
    uint8_t own_addr_type;
    uint16_t conn;
    uint16_t svc_start, svc_end, rx_val, tx_val, tx_cccd;
    int8_t rssi;
    uint8_t fails;     /* fallos seguidos montando el canal */
    char why[96];
    char peer[24];
    FlexFrameRing *rx, *tx;
    bool tx_busy;
    esp_timer_handle_t timer;
} B = {.conn = BLE_HS_CONN_HANDLE_NONE};

static int gap_cb(struct ble_gap_event *ev, void *arg);

static void lock(void)
{
    xSemaphoreTake(B.mtx, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(B.mtx);
}

static void wake(void)
{
    if (B.wake) {
        B.wake();
    }
}

static void set_why(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_why(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(B.why, sizeof(B.why), fmt, ap);
    va_end(ap);
}

static void flush_locked(void)
{
    flexRingInit(B.rx);
    flexRingInit(B.tx);
    B.tx_busy = false;
}

/* ---------- espera y reintento ---------- */

static uint32_t backoff_ms(uint8_t fails)
{
    static const uint32_t steps[] = {1000, 2000, 4000, 8000, 16000, 30000};
    return fails < sizeof(steps) / sizeof(steps[0]) ? steps[fails] : 60000;
}

/* Deja el canal esperando y arma el reintento. failed = fue un fallo real. */
static void enter_wait(bool failed)
{
    uint32_t ms;
    lock();
    B.ph = PH_WAIT;
    B.failed = failed;
    if (failed && B.fails < 255) {
        B.fails++;
    }
    ms = failed ? backoff_ms(B.fails) : 1000;
    unlock();
    esp_timer_stop(B.timer);
    esp_timer_start_once(B.timer, (uint64_t)ms * 1000);
    wake();
}

static void scan_start(bool fast)
{
    lock();
    const bool ok = B.want && B.synced && B.conn == BLE_HS_CONN_HANDLE_NONE;
    if (ok) {
        B.ph = PH_SCAN;
        B.failed = false;
    }
    unlock();
    if (!ok) {
        return;
    }
    struct ble_gap_disc_params p = {
        .itvl = fast ? 96 : 2048,  /* 60 ms / 1,28 s */
        .window = fast ? 48 : 72,  /* 30 ms / 45 ms */
        .passive = 1,
        .filter_duplicates = 1,
    };
    const int rc = ble_gap_disc(B.own_addr_type, fast ? FAST_SCAN_MS : BLE_HS_FOREVER, &p, gap_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        set_why("No se pudo buscar por Bluetooth (código %d)", rc);
        enter_wait(true);
    }
    wake();
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (B.want && B.ph == PH_WAIT) {
        if (B.conn != BLE_HS_CONN_HANDLE_NONE) { /* aún no se ha cerrado la conexión anterior */
            esp_timer_start_once(B.timer, 500 * 1000);
            return;
        }
        scan_start(true);
    }
}

/* Falla el montaje: se dice por qué, se cierra y se reintenta con espera */
static void fail_conn(uint16_t conn)
{
    ESP_LOGW(TAG, "%s", B.why);
    enter_wait(true);
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

/* ---------- envío: una escritura cada vez ---------- */

static int on_write(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg);

static void tx_kick(void)
{
    uint8_t f[FLNK_MAX_FRAME];
    size_t n = 0;
    lock();
    const uint16_t conn = B.conn, h = B.rx_val;
    if (!B.tx_busy && B.ph == PH_OPEN) {
        n = flexRingPop(B.tx, f, sizeof(f));
        B.tx_busy = n > 0;
    }
    unlock();
    if (!n) {
        return;
    }
    const int rc = ble_gattc_write_flat(conn, h, f, (uint16_t)n, on_write, NULL);
    if (rc != 0) {
        lock();
        B.tx_busy = false;
        unlock();
        set_why("No se pudo escribir en el teléfono (código %d)", rc);
        fail_conn(conn);
    }
}

static int on_write(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    lock();
    B.tx_busy = false;
    unlock();
    if (err->status != 0) {
        set_why("El teléfono rechazó una escritura (código %d)", err->status);
        fail_conn(conn);
        return 0;
    }
    tx_kick();
    return 0;
}

/* ---------- descubrimiento ---------- */

static void finish_open(uint16_t conn)
{
    const uint16_t mtu = ble_att_mtu(conn);
    if (mtu < MTU_NEEDED) {
        set_why("MTU %u: el teléfono no admite tramas de 244 B (hace falta 247)", mtu);
        fail_conn(conn);
        return;
    }
    lock();
    B.ph = PH_OPEN;
    B.fails = 0;
    B.why[0] = 0;
    unlock();
    ESP_LOGI(TAG, "canal abierto con %s (MTU %u)", B.peer, mtu);
    wake();
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    (void)arg;
    (void)mtu;
    (void)err; /* si falla, ble_att_mtu() dirá 23 y finish_open lo explica */
    lock();
    B.mtu_done = true;
    const bool ready = B.sub_done && B.ph == PH_SUBSCRIBE;
    unlock();
    if (ready) {
        finish_open(conn);
    }
    return 0;
}

static int on_cccd(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (err->status != 0) {
        set_why("El teléfono no dejó activar los avisos (código %d)", err->status);
        fail_conn(conn);
        return 0;
    }
    lock();
    B.sub_done = true;
    const bool mtu = B.mtu_done;
    unlock();
    if (mtu) {
        finish_open(conn);
    }
    return 0;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val, const struct ble_gatt_dsc *d,
                  void *arg)
{
    (void)chr_val;
    (void)arg;
    if (err->status == 0 && d) {
        if (!B.tx_cccd && d->handle > B.tx_val && d->uuid.u.type == BLE_UUID_TYPE_16 &&
            ble_uuid_u16(&d->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            B.tx_cccd = d->handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !B.tx_cccd) {
        set_why("La característica de avisos del teléfono no tiene CCCD");
        fail_conn(conn);
        return 0;
    }
    lock();
    B.ph = PH_SUBSCRIBE;
    unlock();
    static const uint8_t on[2] = {0x01, 0x00};
    const int rc = ble_gattc_write_flat(conn, B.tx_cccd, on, sizeof(on), on_cccd, NULL);
    if (rc != 0) {
        set_why("No se pudieron activar los avisos (código %d)", rc);
        fail_conn(conn);
    }
    wake();
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *c, void *arg)
{
    (void)arg;
    if (err->status == 0 && c) {
        if (ble_uuid_cmp(&c->uuid.u, &UUID_RX.u) == 0) {
            B.rx_val = c->val_handle;
        } else if (ble_uuid_cmp(&c->uuid.u, &UUID_TX.u) == 0) {
            B.tx_val = c->val_handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !B.rx_val || !B.tx_val) {
        set_why("Al servicio Flex Phone le faltan sus características RX/TX");
        fail_conn(conn);
        return 0;
    }
    const int rc = ble_gattc_disc_all_dscs(conn, B.tx_val, B.svc_end, on_dsc, NULL);
    if (rc != 0) {
        set_why("Error al leer la característica de avisos (código %d)", rc);
        fail_conn(conn);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *s, void *arg)
{
    (void)arg;
    if (err->status == 0 && s) {
        B.svc_start = s->start_handle;
        B.svc_end = s->end_handle;
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !B.svc_end) {
        set_why("El teléfono no publica el servicio Flex Phone (¿app cerrada?)");
        fail_conn(conn);
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(conn, B.svc_start, B.svc_end, on_chr, NULL);
    if (rc != 0) {
        set_why("Error al buscar las características (código %d)", rc);
        fail_conn(conn);
    }
    return 0;
}

/* ---------- eventos GAP ---------- */

static bool adv_has_service(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) {
        return false;
    }
    for (int i = 0; i < f.num_uuids128; i++) {
        if (ble_uuid_cmp(&f.uuids128[i].u, &UUID_SVC.u) == 0) {
            return true;
        }
    }
    return false;
}

static void on_connected(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    lock();
    B.conn = conn;
    B.ph = PH_SECURE;
    B.mtu_done = B.sub_done = false;
    B.svc_start = B.svc_end = B.rx_val = B.tx_val = B.tx_cccd = 0;
    if (ble_gap_conn_find(conn, &d) == 0) {
        const uint8_t *a = d.peer_id_addr.val;
        snprintf(B.peer, sizeof(B.peer), "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    }
    unlock();
    ble_gattc_exchange_mtu(conn, on_mtu, NULL);
    const int rc = ble_gap_security_initiate(conn);
    if (rc != 0) {
        set_why("No se pudo cifrar el enlace (código %d)", rc);
        fail_conn(conn);
    }
}

static void on_enc(uint16_t conn, int status)
{
    if (status == 0) {
        lock();
        B.ph = PH_DISCOVER;
        unlock();
        const int rc = ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC.u, on_svc, NULL);
        if (rc != 0) {
            set_why("Error al buscar el servicio Flex Phone (código %d)", rc);
            fail_conn(conn);
        }
        wake();
        return;
    }
    if (status == BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING) {
        /* El teléfono borró el emparejamiento: se borra aquí también y se
         * empareja de cero en el siguiente intento. */
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(conn, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        set_why("El teléfono olvidó el emparejamiento Bluetooth: se vuelve a emparejar");
    } else {
        set_why("Falló el cifrado Bluetooth (código %d): acepta el emparejamiento en el teléfono", status);
    }
    fail_conn(conn);
}

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        if (B.ph != PH_SCAN || !adv_has_service(&ev->disc)) {
            return 0;
        }
        B.rssi = ev->disc.rssi;
        lock();
        B.ph = PH_CONNECT;
        unlock();
        ble_gap_disc_cancel();
        if (ble_gap_connect(B.own_addr_type, &ev->disc.addr, CONNECT_TIMEOUT, NULL, gap_cb, NULL) != 0) {
            set_why("No se pudo conectar con el teléfono");
            fail_conn(BLE_HS_CONN_HANDLE_NONE);
        }
        wake();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* Se acabó el escaneo rápido sin ver al teléfono: escaneo de fondo */
        if (B.want && B.ph == PH_SCAN) {
            scan_start(false);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            on_connected(ev->connect.conn_handle);
        } else {
            set_why("No se pudo conectar con el teléfono (código %d)", ev->connect.status);
            fail_conn(BLE_HS_CONN_HANDLE_NONE);
        }
        wake();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        lock();
        const bool was_open = B.ph == PH_OPEN;
        const bool waiting = B.ph == PH_WAIT;
        B.conn = BLE_HS_CONN_HANDLE_NONE;
        flush_locked();
        if (!B.want) {
            B.ph = PH_IDLE;
        }
        unlock();
        ESP_LOGI(TAG, "desconectado (motivo 0x%x)", ev->disconnect.reason);
        if (B.want && !waiting) {
            if (was_open) {
                set_why("Se perdió la conexión: buscando el teléfono");
                enter_wait(false);
            } else {
                set_why("Se cortó la conexión al prepararla (motivo 0x%x)", ev->disconnect.reason);
                enter_wait(true);
            }
        }
        wake();
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        on_enc(ev->enc_change.conn_handle, ev->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (ev->notify_rx.conn_handle != B.conn || ev->notify_rx.attr_handle != B.tx_val) {
            return 0;
        }
        const uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
        uint8_t f[FLNK_MAX_FRAME];
        if (n == 0 || n > sizeof(f) || os_mbuf_copydata(ev->notify_rx.om, 0, n, f) != 0) {
            return 0; /* una notificación nunca lleva más de una trama */
        }
        lock();
        flexRingPush(B.rx, f, n);
        unlock();
        wake();
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* El teléfono volvió a emparejar (borró el vínculo BLE): se acepta */
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

/* ---------- pila ---------- */

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &B.own_addr_type);
    B.synced = true;
    if (B.want) {
        scan_start(true);
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "la pila BLE se reinició (motivo %d)", reason);
    B.synced = false;
    lock();
    B.conn = BLE_HS_CONN_HANDLE_NONE;
    flush_locked();
    B.ph = B.want ? PH_WAIT : PH_IDLE;
    unlock();
    wake();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run(); /* vuelve solo con nimble_port_stop(), que no se usa */
    nimble_port_freertos_deinit();
}

static bool ensure_init(void)
{
    if (B.inited) {
        return true;
    }
    B.rx = heap_caps_calloc(1, sizeof(FlexFrameRing), MALLOC_CAP_SPIRAM);
    B.tx = heap_caps_calloc(1, sizeof(FlexFrameRing), MALLOC_CAP_SPIRAM);
    const esp_timer_create_args_t ta = {.callback = timer_cb, .name = "ble_link"};
    if (!B.rx || !B.tx || esp_timer_create(&ta, &B.timer) != ESP_OK) {
        set_why("Sin memoria para el Bluetooth");
        return false;
    }
    flexRingInit(B.rx);
    flexRingInit(B.tx);
    const int64_t t0 = esp_timer_get_time();
    const size_t i0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (nimble_port_init() != ESP_OK) {
        set_why("No se pudo iniciar el Bluetooth");
        return false;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* Just Works con vínculo: el reloj no tiene teclado numérico para BLE;
     * la autenticación de verdad la pone el código de 6 cifras de v2. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_att_set_preferred_mtu(MTU_NEEDED);
#if CONFIG_BT_NIMBLE_GATT_SERVER
    /* Servicio GAP: de aquí lee Android el nombre que enseña al emparejar */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Flex OS Watch");
#endif
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    B.inited = true;
    ESP_LOGI(TAG, "NimBLE iniciado en %lld ms; RAM interna usada: %u B",
             (long long)((esp_timer_get_time() - t0) / 1000),
             (unsigned)(i0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    return true;
}

/* ---------- interfaz FlexPhoneTransport ---------- */

static bool tr_start(FlexPhoneTransport *t)
{
    (void)t;
    if (!ensure_init()) {
        return false;
    }
    lock();
    B.want = true;
    B.fails = 0;
    const bool go = B.synced && (B.ph == PH_IDLE || B.ph == PH_WAIT);
    unlock();
    if (go) {
        scan_start(true);
    }
    return true;
}

static void tr_stop(FlexPhoneTransport *t)
{
    (void)t;
    if (!B.inited) {
        return;
    }
    lock();
    B.want = false;
    const phase_t was = B.ph;
    const uint16_t conn = B.conn;
    B.ph = PH_IDLE;
    flush_locked();
    unlock();
    esp_timer_stop(B.timer);
    if (was == PH_SCAN) {
        ble_gap_disc_cancel();
    } else if (was == PH_CONNECT) {
        ble_gap_conn_cancel();
    }
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static uint8_t tr_state(FlexPhoneTransport *t)
{
    (void)t;
    switch (B.ph) {
    case PH_IDLE:
        return FLP_TC_DOWN;
    case PH_SCAN:
        return FLP_TC_SEARCHING;
    case PH_OPEN:
        return FLP_TC_OPEN;
    case PH_WAIT:
        return B.failed ? FLP_TC_FAILED : FLP_TC_SEARCHING;
    default:
        return FLP_TC_OPENING;
    }
}

static uint16_t tr_mtu(FlexPhoneTransport *t)
{
    (void)t;
    return FLNK_MAX_FRAME; /* el canal solo se abre con MTU >= 247 */
}

static int tr_send(FlexPhoneTransport *t, const uint8_t *f, size_t n)
{
    (void)t;
    lock();
    if (B.ph != PH_OPEN) {
        unlock();
        return FLP_TR_ECLOSED;
    }
    if (flexRingFull(B.tx)) {
        unlock();
        return FLP_TR_EAGAIN;
    }
    flexRingPush(B.tx, f, n);
    unlock();
    tx_kick();
    return FLP_TR_OK;
}

static int tr_recv(FlexPhoneTransport *t, uint8_t *out, size_t cap)
{
    (void)t;
    lock();
    const size_t n = B.ph == PH_OPEN ? flexRingPop(B.rx, out, cap) : 0;
    unlock();
    return (int)n;
}

static int tr_room(FlexPhoneTransport *t)
{
    (void)t;
    lock();
    const int r = B.ph == PH_OPEN ? (int)flexRingFree(B.tx) : 0;
    unlock();
    return r;
}

static const char *tr_status(FlexPhoneTransport *t)
{
    (void)t;
    return B.why[0] ? B.why : "Bluetooth sin incidencias";
}

static void tr_peer(FlexPhoneTransport *t, char *out, size_t n)
{
    (void)t;
    lock();
    snprintf(out, n, "%s", B.peer);
    unlock();
}

static FlexPhoneTransport s_tr = {
    .kind = FLP_TR_BLE,
    .start = tr_start,
    .stop = tr_stop,
    .state = tr_state,
    .mtu = tr_mtu,
    .send = tr_send,
    .recv = tr_recv,
    .status = tr_status,
    .peer = tr_peer,
    .room = tr_room,
};

FlexPhoneTransport *ble_link_transport(void (*wake_fn)(void))
{
    if (!B.mtx) {
        B.mtx = xSemaphoreCreateMutex();
    }
    B.wake = wake_fn;
    return &s_tr;
}

bool ble_link_rx_pending(void)
{
    if (!B.inited) {
        return false;
    }
    lock();
    const bool p = !flexRingEmpty(B.rx);
    unlock();
    return p;
}

void ble_link_text(char *out, size_t n)
{
    static const char *const kPhase[] = {
        [PH_IDLE] = "Bluetooth apagado",
        [PH_SCAN] = "Buscando el teléfono",
        [PH_CONNECT] = "Conectando por Bluetooth",
        [PH_SECURE] = "Cifrando (acepta el emparejamiento en el teléfono)",
        [PH_DISCOVER] = "Buscando el servicio Flex Phone",
        [PH_SUBSCRIBE] = "Activando los avisos",
        [PH_OPEN] = "Conectado por Bluetooth",
        [PH_WAIT] = "Esperando para reintentar",
    };
    if (!B.mtx) {
        snprintf(out, n, "%s", kPhase[PH_IDLE]);
        return;
    }
    lock();
    if (B.ph == PH_WAIT && B.why[0]) {
        snprintf(out, n, "%s", B.why);
    } else {
        snprintf(out, n, "%s", kPhase[B.ph]);
    }
    unlock();
}

int8_t ble_link_rssi(void)
{
    if (B.inited && B.conn != BLE_HS_CONN_HANDLE_NONE) {
        int8_t r = 0;
        if (ble_gap_conn_rssi(B.conn, &r) == 0) {
            B.rssi = r;
        }
    }
    return B.rssi;
}

void ble_link_forget(void)
{
    /* Los vínculos BLE viven en NVS: aunque la pila no se haya iniciado
     * en este arranque, hay que poder borrarlos. */
    if (ensure_init()) {
        ble_store_clear();
    }
}
