/*
 * wm_sd_logic — lógica PURA de la microSD (se prueba en el PC): en qué estado
 * está la tarjeta y qué hay que hacer a continuación. No toca el hardware: el
 * controlador (wm_sd.c) ejecuta la acción que devuelve wsd_next() y le cuenta
 * el resultado con wsd_on_*().
 *
 * La placa NO tiene pin de detección (CD): GPIO17 es SDCS/DAT3 y no lleva
 * interruptor. La presencia se sabe así:
 *  - sin tarjeta: cada cierto tiempo se intenta inicializar (CMD0, CMD8,
 *    ACMD41...). Sin tarjeta falla en milisegundos y no escribe nada;
 *  - con la tarjeta montada: CMD13 (estado) cada cierto tiempo y tras
 *    cualquier error de E/S que avise una app. Dos fallos seguidos = retirada.
 *
 * REGLA DE ORO: el sistema de archivos NUNCA se desmonta con un archivo
 * abierto. Quien abre algo en la microSD toma antes un "arrendamiento"
 * (wsd_lease) y lo suelta al cerrar. Al retirarla, expulsarla o formatearla
 * la tarjeta pasa a REMOVING: no se dan arrendamientos nuevos, los que hay
 * reciben el aviso (cambia gen) y el desmontaje espera a que el contador
 * llegue a 0. Si una app no suelta, la tarjeta se queda en REMOVING con su
 * motivo; nunca se desmonta debajo de ella.
 *
 * Nunca se formatea sola: solo con wsd_request_format(), que pide la
 * interfaz tras una doble confirmación.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WSD_ABSENT = 0, /**< no hay tarjeta (o se expulsó y sigue puesta) */
    WSD_DETECTING,  /**< inicializando la tarjeta */
    WSD_MOUNTING,   /**< montando el sistema de archivos */
    WSD_READY,      /**< montada: se puede leer y escribir */
    WSD_BUSY,       /**< formateando (nadie más la usa) */
    WSD_ERROR,      /**< hay tarjeta pero no se puede usar (ver err) */
    WSD_REMOVING,   /**< desmontando: espera a que se suelten los arrendamientos */
    WSD_STATE_N
} wsd_state_t;

typedef enum {
    WSD_E_NONE = 0,
    WSD_E_CARD,        /**< la tarjeta contesta a medias: dañada o mal insertada */
    WSD_E_UNSUPPORTED, /**< no es una SD (MMC/SDIO) o es SDUC (más de 2 TB) */
    WSD_E_NOFS,        /**< sin formato (vacía o sin partición reconocible) */
    WSD_E_EXFAT,       /**< exFAT: ESP-IDF 5.5.5 no lo incluye (se puede formatear en FAT32) */
    WSD_E_NTFS,
    WSD_E_GPT,         /**< tabla GPT: FatFs sin LBA de 64 bits no la lee */
    WSD_E_FS_DAMAGED,  /**< FAT con la estructura dañada */
    WSD_E_IO,          /**< errores de lectura al montar */
    WSD_E_NOMEM,       /**< sin memoria para montar */
    WSD_E_FORMAT,      /**< el formateo falló */
    WSD_E_HELD,        /**< una app no suelta la tarjeta: no se desmonta debajo de ella */
    WSD_E_N
} wsd_err_t;

/** Resultado de inicializar la tarjeta. */
typedef enum {
    WSD_PROBE_NOCARD = 0, /**< nadie contesta: no hay tarjeta */
    WSD_PROBE_OK,         /**< inicializada (el controlador la deja encendida) */
    WSD_PROBE_BAD,        /**< contesta pero la inicialización falla (WSD_E_CARD) */
    WSD_PROBE_UNSUPPORTED /**< MMC, SDIO o SDUC (WSD_E_UNSUPPORTED) */
} wsd_probe_t;

/** Lo que el controlador tiene que hacer ahora. */
typedef enum {
    WSD_DO_WAIT = 0, /**< nada hasta wsd_t.next_ms (o un aviso) */
    WSD_DO_PROBE,    /**< inicializar la tarjeta -> wsd_on_probe() */
    WSD_DO_MOUNT,    /**< montar el sistema de archivos -> wsd_on_mount() */
    WSD_DO_STATUS,   /**< CMD13 -> wsd_on_status() */
    WSD_DO_UNMOUNT,  /**< desmontar (sin apagar la tarjeta) -> wsd_on_unmounted() */
    WSD_DO_FORMAT,   /**< formatear en FAT -> wsd_on_format() */
    WSD_DO_RELEASE,  /**< apagar la tarjeta y el controlador SDMMC -> wsd_on_released() */
} wsd_action_t;

/* Tiempos (ms). Con "interés" (una app de medios o Ajustes > Almacenamiento
 * abierta) se mira más a menudo. */
#define WSD_PROBE_MS_ACTIVE   1500u
#define WSD_PROBE_MS_IDLE     10000u
#define WSD_STATUS_MS_ACTIVE  2000u
#define WSD_STATUS_MS_IDLE    10000u
#define WSD_STATUS_RETRY_MS   250u   /* tras un primer fallo de CMD13 */
#define WSD_STATUS_FAILS      2      /* fallos seguidos que significan "retirada" */
#define WSD_HELD_MS           10000u /* tras esto en REMOVING, motivo WSD_E_HELD */

typedef struct {
    wsd_state_t st;
    wsd_state_t prev_st;   /**< el estado visible antes de un sondeo (DETECTING) */
    wsd_err_t err;
    bool card_up;          /**< la tarjeta está inicializada (hay que liberarla) */
    bool fs_up;            /**< el sistema de archivos está montado */
    bool ejected;          /**< se expulsó y sigue puesta: no se vuelve a montar */
    uint32_t eject_serial; /**< número de serie (CID) de la expulsada */
    uint32_t serial;       /**< el de la tarjeta inicializada */
    bool want_eject;       /**< REMOVING porque lo pidió la persona */
    bool want_format;      /**< REMOVING para formatear después */
    bool surprise;         /**< REMOVING porque dejó de contestar */
    uint8_t status_fails;
    uint16_t interest;     /**< apps que quieren saber al momento */
    uint32_t leases;       /**< archivos/operaciones abiertos en la tarjeta */
    uint32_t next_ms;      /**< cuándo toca la próxima comprobación */
    uint32_t since_ms;     /**< desde cuándo está en el estado actual */
    uint32_t gen;          /**< sube con cada cambio visible (estado, error, expulsión) */
    bool now_pending;      /**< hay que mirar ya (aviso de E/S, reintento) */
} wsd_t;

void wsd_init(wsd_t *s, uint32_t now);

/** La acción que toca ahora (y el estado que implica: DETECTING al sondear...). */
wsd_action_t wsd_next(wsd_t *s, uint32_t now);

void wsd_on_probe(wsd_t *s, wsd_probe_t r, uint32_t serial, uint32_t now);
/** err = WSD_E_NONE si montó. */
void wsd_on_mount(wsd_t *s, wsd_err_t err, uint32_t now);
void wsd_on_status(wsd_t *s, bool ok, uint32_t now);
void wsd_on_unmounted(wsd_t *s, uint32_t now);
void wsd_on_format(wsd_t *s, bool ok, uint32_t now);
void wsd_on_released(wsd_t *s, uint32_t now);

/** Arrendamiento: true solo en READY. Cada true pide su wsd_unlease(). */
bool wsd_lease(wsd_t *s);
void wsd_unlease(wsd_t *s);

/** Expulsar (READY). false si no hay nada que expulsar. */
bool wsd_request_eject(wsd_t *s, uint32_t now);
/** Formatear (READY o ERROR con la tarjeta inicializada). */
bool wsd_request_format(wsd_t *s, uint32_t now);
bool wsd_can_format(const wsd_t *s);
/** Volver a intentarlo (ERROR o expulsada): suelta la tarjeta y la sondea ya. */
bool wsd_request_retry(wsd_t *s, uint32_t now);
/** Una app tuvo un error de E/S en la tarjeta: se comprueba ya. */
void wsd_io_error(wsd_t *s, uint32_t now);
void wsd_interest(wsd_t *s, bool on, uint32_t now);

/** El estado que se enseña: un sondeo rutinario sin tarjeta no "parpadea"
 * a DETECTING (dura milisegundos). */
wsd_state_t wsd_visible(const wsd_t *s);
const char *wsd_state_name(wsd_state_t st);
/** Texto para la persona ("Formato exFAT: el reloj lee FAT32..."). */
const char *wsd_err_text(wsd_err_t e);

#ifdef __cplusplus
}
#endif
