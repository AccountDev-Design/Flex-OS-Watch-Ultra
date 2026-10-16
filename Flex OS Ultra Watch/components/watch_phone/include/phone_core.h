/*
 * phone_core — la parte de watch_phone que no depende de ESP-IDF: órdenes
 * de la interfaz, instantánea del estado y lista ordenada de notificaciones
 * sobre el enlace y el modelo de flexlink. La usan el firmware
 * (watch_phone.c, con FreeRTOS, NVS y BLE) y el simulador de PC (con un
 * teléfono simulado por un transporte en memoria), así que lo que se ve en
 * la vista previa sale de la misma lógica que corre en el reloj.
 *
 * Nada de aquí es seguro entre tareas por sí solo: quien lo usa lo protege.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "watch_phone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_ENABLE, PM_DISABLE, PM_PAIR, PM_CONFIRM, PM_CANCEL, PM_FORGET,
    PM_DISMISS, PM_CLEAR, PM_REPLY, PM_ACTION, PM_FIND, PM_MEDIA,
} phone_msg_type_t;

typedef struct {
    uint8_t type;   /**< phone_msg_type_t */
    uint8_t arg;    /**< índice de acción, start/stop, orden multimedia */
    uint32_t id;    /**< notificación */
    char text[FLP_REPLY_MAX];
} phone_msg_t;

typedef struct {
    FlexPhoneLink *L;
    FlexPhoneModel *M;
    watch_phone_status_t st;
    FlexAuthRandFn rnd;
    /** Qué hace el canal, en palabras (opcional). */
    void (*chan_text)(char *out, size_t n);
    /** Borra los emparejamientos del transporte al olvidar (opcional). */
    void (*forget_transport)(void);
    uint32_t sig, local;
} phone_core_t;

/** Prepara el núcleo sobre L y M ya iniciados (identidad, vínculo, transporte). */
void phone_core_init(phone_core_t *c, FlexPhoneLink *L, FlexPhoneModel *M, FlexAuthRandFn rnd, bool enabled);
/** Aplica una orden de la interfaz. */
void phone_core_handle(phone_core_t *c, const phone_msg_t *m, uint32_t now_ms);
/** Un paso: enlace (con `rounds` vueltas para vaciar ráfagas) e instantánea. */
void phone_core_tick(phone_core_t *c, uint32_t now_ms, int rounds, bool (*rx_pending)(void));
/** Rehace la instantánea (c->st) sin mover el enlace. */
void phone_core_snapshot(phone_core_t *c, uint32_t now_ms);
/** Notificaciones, la más reciente primero (hora del teléfono, luego llegada). */
int phone_core_notifs(const phone_core_t *c, FlexPhoneNotif *out, int max);
/** Lo que haría falta para responder: FLNK_E_NONE = se puede. */
uint8_t phone_core_reply_check(const phone_core_t *c, uint32_t id, const char *text);
/** ¿Hay sesión y el teléfono concede esa capacidad? */
bool phone_core_can(const phone_core_t *c, uint16_t cap);

#ifdef __cplusplus
}
#endif
