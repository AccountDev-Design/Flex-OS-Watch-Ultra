/*
 * battery_alerts — decisión PURA de los avisos de batería baja y crítica (sin
 * ESP-IDF; se prueba en el PC). Sigue el criterio anti-repetición de Flex OS
 * Ultra (docs/MULTITAREA-MEMORIA.md de la referencia): aviso solo en la
 * transición, histéresis en el nivel y enfriamiento por clase.
 *
 *  - Un aviso por "episodio" y clase: bajo al entrar en LOW, crítico al entrar
 *    en CRITICAL. El episodio termina cuando el nivel vuelve a NORMAL (con la
 *    histéresis de battery_thresholds.h, >= 18 %), lo que lo rearma.
 *  - Solo descargando: con USB (cargando, completa o externa) no se avisa.
 *    Si se desenchufa en nivel bajo sin haber avisado aún, se avisa entonces.
 *  - Enfriamiento: la misma clase no se repite antes de BATT_ALERT_COOLDOWN_MS
 *    aunque el episodio se rearme; si se suprime, se da por avisado.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "battery_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BATT_ALERT_COOLDOWN_MS (10u * 60u * 1000u)

typedef enum {
    BATT_ALERT_NONE = 0,
    BATT_ALERT_LOW,
    BATT_ALERT_CRITICAL,
} batt_alert_t;

typedef struct {
    batt_level_t alerted;  /**< nivel más grave ya avisado en este episodio */
    bool fired[3];         /**< la clase ha sonado alguna vez */
    uint32_t last_ms[3];   /**< momento del último aviso de cada clase */
} batt_alert_state_t;

void batt_alert_init(batt_alert_state_t *a);

/** Devuelve el aviso que hay que mostrar AHORA (o NONE). now_ms: reloj monótono. */
batt_alert_t batt_alert_update(batt_alert_state_t *a, batt_state_t state, batt_level_t level, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
