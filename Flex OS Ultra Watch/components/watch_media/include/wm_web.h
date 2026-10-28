/*
 * wm_web — el Flex Web Server en el reloj: recibir fotos, vídeos y música
 * desde el navegador del móvil (docs/MULTIMEDIA.md §9).
 *
 * Es EL MISMO servidor que Flex OS Ultra (FlexOS_MediaWeb, portado en
 * components/flexmedia y probado entero en tests/host): la misma página, el
 * mismo emparejamiento con código de 6 cifras (que va dentro del QR), las
 * mismas sesiones, CSRF, límites y validación de lo recibido. Aquí solo se
 * pone lo del reloj:
 *  - la tarea "wm_web" (pila de 16 KB en PSRAM: flexWebServeConn usa ~5 KB),
 *    que nace al abrir "Recibir desde el móvil" y muere sola al salir de esa
 *    pantalla (cuando termina lo que esté en marcha) o tras
 *    WWEB_IDLE_STOP_MS sin ninguna petición;
 *  - sockets de lwIP en el puerto 8080, una conexión a la vez;
 *  - el disco y el catálogo del DESTINO ELEGIDO (wm_dest_get) por conexión:
 *    con la microSD, el catálogo adquirido la mantiene montada solo mientras
 *    dura la conexión, y si la tarjeta se expulsa o se saca, la subida en
 *    curso se corta (sus temporales los limpia el catálogo);
 *  - la identidad del reloj ("S3-<MAC>", sacada de la MAC como el hardwareId
 *    de Flex Account) que la página devuelve en X-Flex-Dev: si la IP la tiene
 *    ahora otro aparato Flex, no se escribe nada;
 *  - el código caduca a los WWEB_CODE_TTL_MS y cambia con cada IP nueva.
 *
 * Nunca se registra el código ni las sesiones (ESP_LOG*): solo contadores.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "watch_media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WWEB_PORT          8080
#define WWEB_CODE_TTL_MS   (10u * 60u * 1000u) /* vida del código del QR */
#define WWEB_IDLE_STOP_MS  (15u * 60u * 1000u) /* sin peticiones: se apaga solo */
#define WWEB_KEEP_MS       15000u              /* la pantalla dejó de pedirlo hace esto: se apaga */
#define WWEB_STACK         16384
#define WWEB_DEV_NAME      "Flex OS Watch Ultra"
#define WWEB_DEV_TYPE      "watch"

typedef enum {
    WWEB_OFF = 0,  /**< apagado */
    WWEB_STARTING, /**< arrancando (memoria, tarea) */
    WWEB_NO_WIFI,  /**< esperando a la Wi-Fi */
    WWEB_READY,    /**< escuchando: QR válido */
    WWEB_ERROR,    /**< no pudo escuchar (motivo en err); se reintenta */
} wweb_state_t;

/* Lo último que pasó con una transferencia (para la tarjeta de la pantalla) */
typedef enum { WWEB_X_NONE = 0, WWEB_X_RUN, WWEB_X_CHECK, WWEB_X_DONE, WWEB_X_FAIL } wweb_xstate_t;

typedef struct {
    wweb_state_t state;
    char ip[16];
    uint16_t port;
    char code[8];           /**< "" mientras no hay QR válido */
    uint32_t code_left_s;   /**< 0 = caducado: hace falta uno nuevo */
    wm_vol_t dest;
    bool dest_ok;           /**< el destino elegido está listo */
    int sessions;           /**< móviles conectados */
    bool busy;              /**< hay una conexión atendiéndose */
    uint8_t x_state;        /**< wweb_xstate_t */
    uint32_t x_done, x_total;
    char x_name[64];
    char x_msg[96];
    uint32_t uploads, downloads, rejected;
    char err[96];
    uint32_t gen;           /**< cambia con cualquier cambio de lo anterior */
} wm_web_status_t;

/** Arranca (o mantiene) el servidor. Lo llama la pantalla mientras está abierta. */
void wm_web_keep(void);
/** La pantalla se cierra: se apaga en cuanto no haya nada en marcha. */
void wm_web_release(void);
/** Código nuevo (y QR nuevo): las sesiones abiertas siguen. */
void wm_web_new_code(void);
/** Desconecta todos los móviles (y cambia el código). */
void wm_web_drop_all(void);
void wm_web_status(wm_web_status_t *out);

/* ---- lógica sin hardware (wm_web_logic.c; tests/host) ---- */

/** "http://192.168.1.50:8080/?k=123456": escanear es emparejar. false si no cabe. */
bool wweb_url(char *out, size_t n, const char *ip, uint16_t port, const char *code);
/** Lo que /api/hello dice del destino (JSON sin llaves). */
bool wweb_dest_json(char *out, size_t n, wm_vol_t v, bool ready, uint64_t free_b);
/** Perfil de medios del reloj para la página (JSON sin llaves): a qué tamaño convertir. */
const char *wweb_media_json(void);
/** Segundos que le quedan al código (redondeo hacia arriba; 0 = caducado). */
uint32_t wweb_code_left_s(uint32_t now_ms, uint32_t born_ms, uint32_t ttl_ms);

typedef struct {
    bool wanted;        /**< la pantalla lo pide (wm_web_keep reciente) */
    uint32_t since_keep;/**< ms desde el último wm_web_keep */
    uint32_t idle_ms;   /**< ms sin ninguna petición */
    bool busy;          /**< conexión en curso */
} wweb_life_t;
/** ¿Hay que apagarlo ya? Nunca con una conexión en curso. */
bool wweb_should_stop(const wweb_life_t *l);
/** "Esperando a la Wi-Fi", "Listo para recibir"... */
const char *wweb_state_text(wweb_state_t s);
/** "S3-A1B2C3D4E5F6" desde la MAC de la estación. */
void wweb_dev_id(const uint8_t mac[6], char *out, size_t n);

#ifdef __cplusplus
}
#endif
