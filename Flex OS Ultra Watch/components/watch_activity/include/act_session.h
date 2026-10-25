/*
 * act_session — máquina de estados de un entrenamiento (lógica PURA; se
 * prueba en el PC). Vive en el servicio de actividad: no depende de que la
 * pantalla esté abierta.
 *
 *            start            pausa (manual o automática)
 *   IDLE ───────────► ACTIVE ◄──────────────────────────► PAUSED
 *                        │      reanudar (manual, o automática si           │
 *                        │      la pausa fue automática)                    │
 *                        │ end (manual)                    end (manual, o   │
 *                        ▼                                 automática tras  │
 *                    COMPLETED ◄───────────────────────────  end_after_s) ──┘
 *
 * Reglas:
 *  - Cada transición se comprueba contra el estado actual: un mismo evento
 *    no pausa, reanuda ni finaliza dos veces (las funciones devuelven false y
 *    no cambian nada).
 *  - Pausa automática (tipos con ACT_F_AUTO): sin evidencia de actividad
 *    (ningún paso) durante pause_after_s. La pausa empieza en la ÚLTIMA
 *    evidencia: los segundos de espera no cuentan como ejercicio.
 *  - Reanudación automática: solo de una pausa automática (una pausa manual
 *    la levanta el usuario) y solo con movimiento sostenido (un tren de pasos
 *    confirmado que dura al menos resume_ms). Empieza en el primer paso de
 *    ese tren.
 *  - Finalización automática: pausada (por cualquier motivo) end_after_s
 *    seguidos desde que se vio la pausa. La pausa final no cuenta: la sesión
 *    termina donde empezó.
 *  - Tiempo total = activo + pausado; solo el activo es "ejercicio".
 * Tiempos en ms de un reloj monótono (int64: sin vuelta).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ACT_SES_IDLE = 0, ACT_SES_ACTIVE, ACT_SES_PAUSED, ACT_SES_COMPLETED } act_ses_state_t;
typedef enum { ACT_PAUSE_NONE = 0, ACT_PAUSE_MANUAL, ACT_PAUSE_AUTO, ACT_PAUSE_RESTORED } act_pause_t;
typedef enum { ACT_END_NONE = 0, ACT_END_MANUAL, ACT_END_AUTO, ACT_END_RESTORED } act_end_t;

#define ACT_RESUME_MS 3000 /* movimiento sostenido para reanudar solo */

typedef struct {
    bool auto_pause;
    bool auto_resume;
    bool auto_end;
    uint16_t pause_after_s; /**< 20 por defecto */
    uint16_t end_after_s;   /**< 300 por defecto */
} act_auto_cfg_t;

typedef struct {
    act_ses_state_t state;
    uint8_t type;           /**< id de act_catalog */
    bool auto_ok;           /**< el tipo admite las funciones automáticas */
    uint32_t id;            /**< único: evita duplicados al guardar y restaurar */
    int64_t start_ms;
    int64_t seg_ms;         /**< inicio del tramo actual (activo o pausado) */
    int64_t end_ms;
    uint32_t active_ms;     /**< tramos activos cerrados */
    uint32_t paused_ms;     /**< tramos pausados cerrados */
    act_pause_t pause_kind;
    act_end_t end_reason;
    int64_t evidence_ms;    /**< última evidencia de actividad */
    int64_t pause_ms;       /**< cuándo se detectó la pausa: la finalización automática cuenta desde aquí */
    uint8_t pauses, auto_pauses, auto_resumes;
} act_session_t;

/** Lo que pasó en un act_session_tick. */
typedef enum { ACT_SEV_NONE = 0, ACT_SEV_AUTO_PAUSE, ACT_SEV_AUTO_RESUME, ACT_SEV_AUTO_END } act_sev_t;

void act_auto_cfg_default(act_auto_cfg_t *c);
void act_session_reset(act_session_t *s);
/** IDLE o COMPLETED -> ACTIVE. */
bool act_session_start(act_session_t *s, uint32_t id, uint8_t type, bool auto_ok, int64_t now_ms);
bool act_session_pause(act_session_t *s, int64_t now_ms);
bool act_session_resume(act_session_t *s, int64_t now_ms);
bool act_session_end(act_session_t *s, int64_t now_ms);
/** Hay actividad ahora (un paso contado): retrasa la pausa automática. */
void act_session_evidence(act_session_t *s, int64_t t_ms);
/**
 * Evalúa las funciones automáticas. walking: hay un tren de pasos
 * confirmado; bout_start_ms: su primer paso.
 */
act_sev_t act_session_tick(act_session_t *s, const act_auto_cfg_t *c, int64_t now_ms, bool walking,
                           int64_t bout_start_ms);
/** Tiempo activo / pausado / total hasta now (o hasta el final si COMPLETED). */
uint32_t act_session_active_ms(const act_session_t *s, int64_t now_ms);
uint32_t act_session_paused_ms(const act_session_t *s, int64_t now_ms);
uint32_t act_session_total_ms(const act_session_t *s, int64_t now_ms);
/** En marcha (ACTIVE o PAUSED). */
bool act_session_running(const act_session_t *s);
/**
 * Tras reiniciar el reloj: la sesión guardada vuelve PAUSADA en el instante
 * now_ms con sus tiempos acumulados (no se sabe qué pasó mientras estuvo
 * apagado). gap_ms: tiempo que pasó apagado si se conoce (UTC válido), o 0;
 * cuenta como pausa y puede finalizarla al momento.
 */
void act_session_restore(act_session_t *s, int64_t now_ms, uint32_t gap_ms);

#ifdef __cplusplus
}
#endif
