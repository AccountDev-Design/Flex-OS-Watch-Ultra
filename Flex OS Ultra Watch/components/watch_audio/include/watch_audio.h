/*
 * watch_audio — el audio del reloj (docs/MULTIMEDIA.md §5-§6): el ÚNICO dueño
 * del I2S, del ES8311 (altavoz), del ES7210 (micrófonos) y del amplificador.
 *
 * Hardware (esquema y firmware de fábrica de Waveshare, verificados):
 *  - I2S_NUM_1 en dúplex: MCLK GPIO16, BCLK GPIO41, LRCK GPIO45,
 *    DOUT GPIO40 (al ES8311), DIN GPIO42 (del ES7210). El TX es estándar
 *    estéreo de 16 bits y genera los relojes; el RX es TDM de 4 ranuras con
 *    los MISMOS relojes (bclk_div 8, MCLK = 256 fs).
 *  - ES8311 en 0x18 (0x30 en esp_codec_dev), ES7210 en 0x40 (0x80), en el
 *    I2C común (SDA GPIO15, SCL GPIO14).
 *  - Amplificador NS4150B: habilitación en GPIO46 (pull-down de 10 K: sin
 *    firmware, apagado). Lo enciende el controlador del ES8311 al abrir la
 *    salida y lo apaga al cerrarla.
 *
 * Una sola SESIÓN a la vez (reproductor, grabadora o vídeo). El hardware se
 * enciende al empezar la primera sesión y se apaga entero (amplificador,
 * códecs, I2S y sus búferes DMA) al soltarla: nada se queda gastando batería.
 * Pedir la sesión con `preempt` avisa al dueño actual (on_preempt) para que
 * pare, solo si el que pide tiene más prioridad (grabadora > vídeo > música:
 * nada corta una grabación); si no la suelta en 1,5 s, la petición falla y
 * nadie se queda colgado.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WA_OWNER_NONE = 0, WA_OWNER_PLAYER, WA_OWNER_RECORDER, WA_OWNER_VIDEO, WA_OWNER_N } wa_owner_t;

/** Guarda el bus I2C. No toca el hardware hasta la primera sesión. */
esp_err_t watch_audio_start(i2c_master_bus_handle_t bus);

/** Se pide al dueño actual que suelte la sesión (desde la tarea que la pide). */
typedef void (*wa_preempt_cb_t)(void *ctx);

/** Sesión exclusiva. on_preempt: lo que se llamará si otro la pide con preempt. */
esp_err_t wa_session_acquire(wa_owner_t who, bool preempt, wa_preempt_cb_t on_preempt, void *ctx);
/** Suelta la sesión y APAGA el hardware de audio. */
void wa_session_release(wa_owner_t who);
wa_owner_t wa_session_owner(void);
const char *wa_owner_name(wa_owner_t who);

/* ---------- salida (solo el dueño de la sesión) ---------- */

/** Abre el altavoz a esa frecuencia, en estéreo de 16 bits (el mono se duplica
 * antes de escribir). 8, 11,025, 16, 22,05, 24, 32, 44,1 o 48 kHz. */
esp_err_t wa_out_open(uint32_t rate);
/** Escribe PCM estéreo de 16 bits (bloquea mientras el DMA tiene sitio). */
esp_err_t wa_out_write(const void *pcm, size_t bytes);
void wa_out_close(void);
void wa_out_mute(bool mute);

/* ---------- entrada: el perfil de voz de fábrica ---------- */

#define WA_VOICE_RATE 24000u
/** Abre los micrófonos (24 kHz, 4 ranuras TDM, MIC1 y MIC2 a 24 dB). La salida
 * queda abierta (da los relojes) pero muda y con el amplificador apagado. */
esp_err_t wa_in_open_voice(void);
/** Lee muestras TDM (4 x int16 por muestra). bytes múltiplo de 8. */
esp_err_t wa_in_read(int16_t *tdm, size_t bytes);
void wa_in_close(void);

/* ---------- volumen (persistente en Ajustes) ---------- */

void wa_set_volume(uint8_t percent);
uint8_t wa_get_volume(void);

/* ---------- diagnóstico ---------- */

typedef struct {
    bool hw_up;
    bool out_open, in_open;
    wa_owner_t owner;
    uint32_t sessions;     /**< sesiones desde el arranque */
    uint32_t hw_errors;    /**< fallos de I2C/I2S/códec */
    uint32_t out_rate;
    esp_err_t last_err;
} wa_diag_t;
void wa_diag(wa_diag_t *out);

#ifdef __cplusplus
}
#endif
