/*
 * watch_browser — el navegador remoto (F6) en el reloj: la sesión FBP/1
 * (br_net) en una tarea propia sobre esp_transport (TCP o TLS VERIFICADO con
 * el paquete de CA de ESP-IDF), los ajustes en NVS (espacio "flexos_br", las
 * claves de la referencia) y el historial y los favoritos en LittleFS
 * (/.sistema, oculto en el explorador).
 *
 * La web NO se ejecuta en el reloj: la renderiza un servicio (server/ de
 * Flex OS Ultra en un PC o un servidor) y llega como JPEG. Sin servicio no
 * hay web; las páginas internas (flex://) sí funcionan sin él.
 *
 * El servicio sale de una de tres fuentes (br_backend.h): el Browser Relay
 * de Flex Phone (pedido por BLE, servido por el Wi-Fi local), la nube o el
 * PC de Ajustes. Con "Automático", el teléfono primero.
 *
 * La sesión solo vive con la app abierta (watch_browser_open/close): al
 * salir se cierra el socket y se suelta el búfer de 135 KB de PSRAM. Solo
 * usa el Wi-Fi si ya está conectado; del BLE solo lee el estado del
 * teléfono y le pide el relay (watch_phone).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "br_lists.h"
#include "br_net.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Abre la sesión (reserva el búfer y arranca la tarea). */
esp_err_t watch_browser_open(void);
/** ¿Hay memoria para abrir la sesión con margen para el resto del sistema?
 * Si no, why (opcional) dice qué falta y cuánto hay. No reserva nada. */
bool watch_browser_mem_check(char *why, size_t n);
/** Tras un ESP_ERR_NO_MEM de watch_browser_open: qué faltó, con cifras. */
const char *watch_browser_mem_why(void);
/** Cierra la sesión y libera el búfer (los ajustes y las listas quedan). */
void watch_browser_close(void);
/** La sesión abierta, o NULL. Leer con br_net_lock/unlock. */
br_net_t *watch_browser_net(void);
/** Historial y favoritos (siempre válidos; se cargan en la primera apertura). */
br_lists_t *watch_browser_lists(void);
/** Guarda las listas si cambiaron (como mucho una vez cada 20 s salvo force). */
void watch_browser_persist(bool force);

void watch_browser_get_settings(BrSettings *out);
/** Guarda en NVS y reconfigura la sesión abierta. */
void watch_browser_set_settings(const BrSettings *st);
/** Identificador del reloj que viaja en el HELLO ("flexwatch-xxxxxx"). */
const char *watch_browser_device(void);
/** Capacidades del reloj (FLEXBR_CAP_*). */
uint32_t watch_browser_caps(void);
/** ¿Hay Wi-Fi con IP? */
bool watch_browser_online(void);
/** Última página abierta (NVS brlast). */
void watch_browser_get_last(char *url, size_t n);
void watch_browser_set_last(const char *url);

/** De dónde sale la web ahora mismo (para la interfaz). */
typedef struct {
    int src;              /**< BRSRC_PHONE/CLOUD/MANUAL o BRSRC_NONE */
    uint8_t pref;         /**< BRSRC_* que eligió la persona */
    char why[96];         /**< sin fuente: por qué; con el teléfono arrancando: qué se espera */
    uint8_t relay_state;  /**< FLP_RELAY_* del teléfono */
    uint8_t relay_ip[4];
    uint16_t relay_port;
    bool phone_possible;  /**< hay teléfono vinculado y la preferencia lo admite: merece la pena esperar */
} watch_browser_source_t;
/**
 * Fuente en uso. Con la app abierta la recalcula la tarea cada 0,5 s (y pide
 * el relay al teléfono si hace falta); con la app cerrada, esta llamada.
 */
void watch_browser_source(watch_browser_source_t *out);

/** "Probar el servidor": GET /v1/health de la fuente en uso, en segundo plano. */
void watch_browser_probe_start(void);
/** 0 = en curso o sin empezar, 1 = terminado (msg con el resultado). */
int watch_browser_probe_result(char *msg, size_t n);

#ifdef __cplusplus
}
#endif
