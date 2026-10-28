/*
 * wa_rec — la grabadora (docs/MULTIMEDIA.md §6).
 *
 * Dos tareas mientras graba y ninguna después:
 *  - "wa_cap" (RAM interna, prioridad 7): lee el TDM del ES7210 en bloques de
 *    256 muestras (10,7 ms), saca MIC1/MIC2 (wa_logic), alimenta el medidor y
 *    deja el PCM en un búfer circular de 128 KB en PSRAM. Nunca toca el disco;
 *  - "wa_recw" (prioridad 3): escribe en bloques de 16 KB, comprueba el
 *    espacio cada segundo y, en la microSD, corrige la cabecera WAV cada 5 s
 *    (un corte deja un WAV válido hasta ese punto). En el LittleFS interno la
 *    cabecera queda "en flujo" (corregir el principio reescribiría el archivo
 *    entero) y los lectores la recortan al archivo.
 *
 * Se graba en FML_DIR_RECPART del destino elegido y, al terminar, se publica
 * en /Grabaciones con nombre único (wm_lib_add_file). Si se corta (batería,
 * tarjeta retirada), lo escrito se recupera al volver a montar el volumen.
 * Si el destino no está listo, NO se graba en otro sitio: wr_start lo dice.
 * Memoria interna + estéreo: la flash no borra lo bastante rápido para 96 KB/s
 * de forma sostenida, así que ahí se graba en mono y se avisa antes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_MediaLib.h"
#include "esp_err.h"
#include "watch_media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WR_IDLE = 0, WR_STARTING, WR_RECORDING, WR_PAUSED, WR_SAVING, WR_SAVED, WR_ERROR } wr_state_t;

typedef struct {
    wr_state_t state;
    wm_vol_t vol;            /**< dónde se graba */
    uint8_t channels;        /**< 1 o 2 */
    uint32_t rate;
    uint32_t elapsed_ms;     /**< grabado (sin las pausas) */
    uint64_t bytes;          /**< PCM escrito */
    uint32_t seconds_left;   /**< con el espacio libre del destino */
    float level;             /**< 0..1 medidor suavizado */
    bool clipping;
    uint32_t dropped_ms;     /**< audio perdido porque el disco no daba abasto */
    char message[112];       /**< error, o aviso al terminar ("memoria llena: guardada") */
    uint32_t saved_id;       /**< WR_SAVED: id en la biblioteca */
    char saved_name[FML_NAME_MAX];
    uint32_t gen;            /**< cambia con cada cambio de estado o mensaje */
} wr_status_t;

esp_err_t wr_init(void);

/** Lo que se va a hacer antes de empezar: destino, canales, tiempo que cabe
 * y la limitación que aplique. false si no se puede grabar (con el motivo). */
typedef struct {
    bool ok;
    wm_vol_t vol;
    uint8_t channels;
    uint32_t seconds_left;
    char note[112];          /**< "En el reloj se graba en mono..." o el motivo */
} wr_plan_t;
void wr_plan(wr_plan_t *out);

esp_err_t wr_start(void);
void wr_pause(void);
void wr_resume(void);
/** Termina y guarda. */
void wr_stop(void);
/** Termina y DESCARTA (la interfaz lo confirma antes). */
void wr_cancel(void);
/** Tras WR_SAVED o WR_ERROR, vuelve a WR_IDLE. */
void wr_ack(void);
void wr_status(wr_status_t *out);

#ifdef __cplusplus
}
#endif
