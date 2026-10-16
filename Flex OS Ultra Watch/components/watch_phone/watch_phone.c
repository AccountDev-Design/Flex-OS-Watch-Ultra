/*
 * watch_phone — ver include/watch_phone.h
 *
 * Una sola tarea es dueña del enlace (FlexPhoneLink) y del modelo
 * (FlexPhoneModel): los dos viven en PSRAM (~30 KB) y solo se tocan con
 * s_lock tomado. La interfaz pide cosas por la cola y lee instantáneas.
 * El transporte BLE despierta la tarea en cuanto llega una trama.
 *
 * NVS: espacio "flexphone" con las MISMAS claves que la referencia
 * (BR.h:110-114, flexlink-v2 §3.4): bondkey, bondpeer, bondname, selfid y
 * autolink. La clave del vínculo no sale nunca de aquí ni va a los registros.
 */
#include "watch_phone.h"

#include <stdio.h>
#include <string.h>
#include "ble_link.h"
#include "phone_core.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "watch_phone";

#define NVS_NS      "flexphone"
#define TICK_MS     100  /* plazos del enlace en ms; la RX despierta antes */
#define RSSI_MS     5000
#define TASK_STACK  6144

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_q;
static TaskHandle_t s_task;
static FlexPhoneLink *s_L;
static FlexPhoneModel *s_M;
static phone_core_t s_core;   /* órdenes, instantánea y lista: phone_core.c */
static FlexPhoneBond s_saved; /* lo que hay en NVS: solo se escribe lo que cambia */
static uint32_t s_rssi_at;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static uint32_t now_ms(void)
{
    const uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    return t ? t : 1;
}

static uint32_t rnd(void)
{
    return esp_random(); /* aleatorio de verdad con la radio encendida */
}

static void wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

static void post(const phone_msg_t *m)
{
    if (!s_q || xQueueSend(s_q, m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cola llena: orden %d descartada", m->type);
        return;
    }
    wake();
}

static void post_simple(phone_msg_type_t t, uint32_t id, uint8_t arg)
{
    phone_msg_t m = {.type = (uint8_t)t, .id = id, .arg = arg};
    post(&m);
}

/* ---------- NVS ---------- */

static void save_bond(const FlexPhoneBond *b)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS no disponible: el vínculo no se guarda");
        return;
    }
    esp_err_t e = ESP_OK;
    if (b->valid) {
        e = nvs_set_blob(h, "bondkey", b->key, sizeof(b->key));
        if (e == ESP_OK) {
            e = nvs_set_str(h, "bondpeer", b->peerId);
        }
        if (e == ESP_OK) {
            e = nvs_set_str(h, "bondname", b->peerName);
        }
    } else {
        nvs_erase_key(h, "bondkey");
        nvs_erase_key(h, "bondpeer");
        nvs_erase_key(h, "bondname");
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "no se pudo guardar el vínculo: %s", esp_err_to_name(e));
    }
}

static void save_enabled(bool on)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "autolink", on ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Carga identidad, vínculo y preferencia. Devuelve si estaba encendido. */
static bool load(char self_id[FLXA_ID_MAX], FlexPhoneBond *b)
{
    memset(b, 0, sizeof(*b));
    self_id[0] = 0;
    bool on = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    size_t n = FLXA_ID_MAX;
    if (nvs_get_str(h, "selfid", self_id, &n) != ESP_OK || !self_id[0]) {
        /* Igual que la referencia (BR.h:137-153): se genera UNA vez. Entra
         * en la derivación de la clave: cambiarlo invalida los vínculos. */
        snprintf(self_id, FLXA_ID_MAX, "flexos-%08lx%08lx", (unsigned long)esp_random(),
                 (unsigned long)esp_random());
        nvs_set_str(h, "selfid", self_id);
        nvs_commit(h);
    }
    uint8_t u8 = 0;
    if (nvs_get_u8(h, "autolink", &u8) == ESP_OK) {
        on = u8 != 0;
    }
    size_t kn = sizeof(b->key), pn = sizeof(b->peerId), nn = sizeof(b->peerName);
    if (nvs_get_blob(h, "bondkey", b->key, &kn) == ESP_OK && kn == sizeof(b->key) &&
        nvs_get_str(h, "bondpeer", b->peerId, &pn) == ESP_OK && b->peerId[0]) {
        if (nvs_get_str(h, "bondname", b->peerName, &nn) != ESP_OK) {
            b->peerName[0] = 0;
        }
        b->valid = true;
    } else {
        memset(b, 0, sizeof(*b)); /* sin clave de 32 B o sin id: no hay vínculo (BR.h:173-196) */
    }
    nvs_close(h);
    return on;
}

/* ---------- tarea ---------- */

static bool bond_changed(const FlexPhoneBond *a, const FlexPhoneBond *b)
{
    return a->valid != b->valid || memcmp(a->key, b->key, sizeof(a->key)) != 0 ||
           strcmp(a->peerId, b->peerId) != 0 || strcmp(a->peerName, b->peerName) != 0;
}

static void phone_task(void *arg)
{
    (void)arg;
    for (;;) {
        lock();
        const bool active = s_L->state != FLP_LS_OFF && s_L->state != FLP_LS_UNAVAILABLE;
        const bool was_on = s_core.st.enabled;
        unlock();
        ulTaskNotifyTake(pdTRUE, active ? pdMS_TO_TICKS(TICK_MS) : portMAX_DELAY);

        phone_msg_t m;
        lock();
        const uint32_t now = now_ms();
        while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
            phone_core_handle(&s_core, &m, now);
        }
        phone_core_tick(&s_core, now, 8, ble_link_rx_pending);
        FlexPhoneBond bond = s_L->bond;
        const bool on = s_core.st.enabled;
        unlock();

        if (on != was_on) {
            save_enabled(on);
        }
        if (bond_changed(&bond, &s_saved)) {
            save_bond(&bond);
            s_saved = bond;
            if (bond.valid) {
                ESP_LOGI(TAG, "vínculo guardado (%s)", bond.peerId);
            } else {
                ESP_LOGI(TAG, "vínculo borrado");
            }
        }
        if (now - s_rssi_at >= RSSI_MS) {
            s_rssi_at = now;
            (void)ble_link_rssi(); /* refresca la medida para el diagnóstico */
        }
        memset(&bond, 0, sizeof(bond)); /* la copia de la clave no se queda en la pila */
    }
}

/* ---------- API ---------- */

esp_err_t watch_phone_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_L = heap_caps_calloc(1, sizeof(*s_L), MALLOC_CAP_SPIRAM);
    s_M = heap_caps_calloc(1, sizeof(*s_M), MALLOC_CAP_SPIRAM);
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(6, sizeof(phone_msg_t));
    if (!s_L || !s_M || !s_lock || !s_q) {
        ESP_LOGE(TAG, "sin memoria para Flex Phone");
        return ESP_ERR_NO_MEM;
    }
    char self_id[FLXA_ID_MAX];
    FlexPhoneBond bond;
    const bool on = load(self_id, &bond);
    flexPhoneLinkInit(s_L);
    flexPhoneModelInit(s_M);
    flexPhoneLinkSetIdentity(s_L, self_id);
    flexPhoneLinkSetRandom(s_L, rnd);
    flexPhoneLinkSetBond(s_L, &bond);
    flexPhoneLinkSetTransport(s_L, ble_link_transport(wake));
    s_saved = bond;
    memset(&bond, 0, sizeof(bond));
    phone_core_init(&s_core, s_L, s_M, rnd, on);
    s_core.chan_text = ble_link_text;
    s_core.forget_transport = ble_link_forget;
    if (xTaskCreate(phone_task, "watch_phone", TASK_STACK, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (on) {
        post_simple(PM_ENABLE, 0, 0);
    }
    ESP_LOGI(TAG, "Flex Phone %s; vínculo %s; enlace+modelo %u B en PSRAM", on ? "encendido" : "apagado",
             s_saved.valid ? "guardado" : "ninguno", (unsigned)(sizeof(*s_L) + sizeof(*s_M)));
    return ESP_OK;
}

void watch_phone_get(watch_phone_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        out->link = FLP_LS_OFF;
        out->rtt_ms = 0xFFFF;
        return;
    }
    lock();
    *out = s_core.st;
    unlock();
}

int watch_phone_notifs(FlexPhoneNotif *out, int max)
{
    if (!s_lock) {
        return 0;
    }
    lock();
    const int n = phone_core_notifs(&s_core, out, max);
    unlock();
    return n;
}

bool watch_phone_notif(uint32_t id, FlexPhoneNotif *out)
{
    if (!s_lock) {
        return false;
    }
    lock();
    const int i = flexPhoneNotifFind(s_M, id);
    if (i >= 0) {
        *out = s_M->notif[i];
    }
    unlock();
    return i >= 0;
}

bool watch_phone_notif_body(const FlexPhoneNotif *n, bool on_lock, char *out, size_t outN)
{
    if (!s_lock) {
        if (out && outN) {
            out[0] = 0;
        }
        return false;
    }
    lock();
    const bool ok = flexPhoneNotifBody(s_M, n, on_lock, out, outN);
    unlock();
    return ok;
}

void watch_phone_set_enabled(bool on)
{
    post_simple(on ? PM_ENABLE : PM_DISABLE, 0, 0);
}

void watch_phone_pair_begin(void)
{
    post_simple(PM_PAIR, 0, 0);
}

void watch_phone_pair_confirm(void)
{
    post_simple(PM_CONFIRM, 0, 0);
}

void watch_phone_pair_cancel(void)
{
    post_simple(PM_CANCEL, 0, 0);
}

void watch_phone_forget(void)
{
    post_simple(PM_FORGET, 0, 0);
}

void watch_phone_dismiss(uint32_t id)
{
    post_simple(PM_DISMISS, id, 0);
}

void watch_phone_clear_all(void)
{
    post_simple(PM_CLEAR, 0, 0);
}

uint8_t watch_phone_reply(uint32_t id, const char *text)
{
    if (!s_lock || !text) {
        return FLNK_E_INTERNAL;
    }
    lock();
    const uint8_t e = phone_core_reply_check(&s_core, id, text);
    unlock();
    if (e == FLNK_E_NONE) {
        phone_msg_t m = {.type = PM_REPLY, .id = id};
        snprintf(m.text, sizeof(m.text), "%s", text);
        post(&m);
    }
    return e;
}

static bool can_send(uint16_t cap)
{
    if (!s_lock) {
        return false;
    }
    lock();
    const bool ok = phone_core_can(&s_core, cap);
    unlock();
    return ok;
}

bool watch_phone_action(uint32_t id, uint8_t idx)
{
    if (!can_send(FLP_CAP_NOTIF)) {
        return false;
    }
    post_simple(PM_ACTION, id, idx);
    return true;
}

bool watch_phone_find(bool start)
{
    if (!can_send(FLP_CAP_FIND)) {
        return false;
    }
    post_simple(PM_FIND, 0, start ? 1 : 0);
    return true;
}

bool watch_phone_media(uint8_t cmd)
{
    if (cmd < FLP_MCMD_PLAY || cmd > FLP_MCMD_PREV || !can_send(FLP_CAP_MEDIA)) {
        return false;
    }
    post_simple(PM_MEDIA, 0, cmd);
    return true;
}
