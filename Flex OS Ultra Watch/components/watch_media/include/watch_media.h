/*
 * watch_media — servicio central de almacenamiento multimedia del reloj
 * (docs/MULTIMEDIA.md §2-§4). Lo usan la grabadora, la música, el vídeo, el
 * visor y la Galería; ninguna app monta, formatea ni decide destinos por su
 * cuenta.
 *
 * Dos volúmenes:
 *  - WM_VOL_WATCH: el LittleFS interno (/storage, watch_storage). No se toca
 *    su montaje: si la puerta de 32 MB no lo permite, no está listo;
 *  - WM_VOL_SD: la microSD (/sdcard), con su propia tarea y la máquina de
 *    estados de wm_sd_logic.h.
 *
 * Reglas:
 *  - quien abre un archivo en la microSD toma antes wm_sd_lease() y lo suelta
 *    al cerrarlo. Sin arrendamiento, la tarjeta puede desmontarse;
 *  - el destino lo elige la persona (Ajustes > media_dest). wm_dest_get()
 *    dice si está listo; si no, la operación se detiene: nunca se guarda en
 *    el otro volumen sin preguntar;
 *  - nada se formatea ni se borra solo. Formatear exige un ticket que solo da
 *    el diálogo de doble confirmación (wm_sd_format_ticket).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "wm_fsclass.h"
#include "wm_sd_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WM_SD_BASE      "/sdcard"
#define WM_SD_MAX_FILES 8   /* archivos abiertos a la vez en la microSD */
#define WM_PATH_MAX     128 /* ruta real ("/sdcard/Musica/x.mp3") */

typedef enum { WM_VOL_WATCH = 0, WM_VOL_SD, WM_VOL_N } wm_vol_t;

/* ---------------- microSD ---------------- */

typedef enum { WM_CARD_NONE = 0, WM_CARD_SDSC, WM_CARD_SDHC, WM_CARD_SDXC } wm_card_kind_t;

typedef struct {
    wsd_state_t state;      /**< el visible (wsd_visible) */
    wsd_err_t err;
    bool ejected;           /**< expulsada y todavía puesta */
    bool can_format;
    uint32_t gen;           /**< cambia con cada cambio visible */
    uint32_t leases;
    /* tarjeta (si card_valid) */
    bool card_valid;
    wm_card_kind_t kind;
    char name[8];           /**< nombre de producto del CID */
    uint8_t mfg_id;
    uint64_t card_bytes;    /**< capacidad física (64 bits) */
    uint32_t freq_khz;
    /* sistema de archivos (si space_valid) */
    uint8_t fat_bits;
    bool space_valid;
    uint64_t total_bytes;
    uint64_t free_bytes;
} wm_sd_info_t;

/** Arranca la tarea de la microSD (la primera comprobación es inmediata). */
esp_err_t watch_media_start(void);

void wm_sd_info(wm_sd_info_t *out);
uint32_t wm_sd_gen(void);
/** true solo si está montada (READY); cada true pide su wm_sd_unlease(). */
bool wm_sd_lease(void);
void wm_sd_unlease(void);
/** Una app tuvo un error de E/S en /sdcard: se comprueba la tarjeta ya. */
void wm_sd_io_error(void);
/** Una pantalla que enseña el estado está abierta: se mira más a menudo. */
void wm_sd_interest(bool on);
bool wm_sd_eject(void);
bool wm_sd_retry(void);
/** Ticket de un solo uso (60 s) para formatear: lo pide el diálogo tras la
 * SEGUNDA confirmación. 0 si no se puede formatear ahora. */
uint32_t wm_sd_format_ticket(void);
/** Formatea en FAT32 (o FAT16 si es muy pequeña). BORRA TODO. */
bool wm_sd_format(uint32_t ticket);
/** Aviso desde la tarea de la microSD cuando cambia gen. Debe volver enseguida
 * (sin E/S ni bloqueos): suele solo marcar una bandera. Hasta 6. */
typedef void (*wm_sd_listener_t)(uint32_t gen, void *ctx);
bool wm_sd_listen(wm_sd_listener_t cb, void *ctx);

/** "Lista · 29,7 GB libres de 31,9 GB", "Sin tarjeta", el motivo del error... */
void wm_sd_describe(const wm_sd_info_t *i, char *out, size_t n);

/* ---------------- volúmenes y destino ---------------- */

typedef struct {
    bool ready;
    uint64_t total_bytes;   /**< 0 = no se sabe */
    uint64_t free_bytes;
} wm_vol_info_t;

const char *wm_vol_root(wm_vol_t v);  /**< "/storage", "/sdcard" */
const char *wm_vol_name(wm_vol_t v);  /**< "Reloj", "microSD" */
/** Estado y espacio (en la microSD toma y suelta un arrendamiento). */
bool wm_vol_info(wm_vol_t v, wm_vol_info_t *out);
/** Ruta real de una relativa al volumen ("/Musica/x.mp3"); false si no es segura. */
bool wm_vol_path(wm_vol_t v, const char *rel, char *out, size_t n);

typedef enum {
    WM_DEST_OK = 0,
    WM_DEST_SD_ABSENT,     /**< eligió microSD y no hay tarjeta */
    WM_DEST_SD_NOT_READY,  /**< hay tarjeta pero no se puede usar (formato, error, desmontando) */
    WM_DEST_WATCH_NOT_READY/**< la memoria interna no está montada */
} wm_dest_status_t;

/** El volumen elegido en Ajustes y si se puede escribir en él ahora. */
wm_dest_status_t wm_dest_get(wm_vol_t *vol);
const char *wm_dest_text(wm_dest_status_t st);
/** Lo cambia la persona (Ajustes o el selector de las apps). */
void wm_dest_set(wm_vol_t vol);

#ifdef __cplusplus
}
#endif
