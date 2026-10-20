/*
 * br_logic — PORT a C11 del núcleo portable del navegador de Flex OS Ultra
 * (FlexOS_Ultra/FlexOS_Browser.h/.cpp, build 6): omnibox con su política
 * anti-SSRF, códec FBP/1, SHA-1/Base64/WebSocket y troceo de URL ws(s).
 * Sin red, sin dibujo y sin memoria dinámica. Cambios del reloj marcados
 * "RELOJ:"; el resto es la referencia. La parte de la app (flexBrowser*,
 * brHost*) no se porta aquí: la hacen watch_browser y ui_browser.
 */
// #############################################################
// ##  FlexOS_Browser.h  ·  NAVEGADOR DE FLEX OS ULTRA
// ##  Nucleo comun a las tres placas: ESP32-P4 (Ultra), ESP32-S3
// ##  (Ultra S3) y ESP32 clasico (Pro).
// #############################################################
//
//  QUE ES ESTO
//  -----------
//  La app "Navegador" del escritorio era hasta ahora una maqueta:
//  navEnter() pintaba una barra de direcciones falsa, tres pestanas
//  decorativas y un globo. Este modulo la convierte en un navegador
//  de verdad.
//
//  POR QUE UNA ARQUITECTURA HIBRIDA (y no "un navegador dentro del chip")
//  ---------------------------------------------------------------------
//  Un motor web moderno (HTML5 + CSS3 + JavaScript + TLS + fuentes +
//  vídeo) son cientos de MB de codigo y necesita cientos de MB de RAM.
//  El ESP32-P4 tiene 32 MB de PSRAM en el mejor caso y el ESP32 clasico
//  ~300 KB de SRAM. No es una cuestion de optimizar: no cabe, y decir
//  lo contrario seria mentir. Asi que el reparto es:
//
//    · EL DISPOSITIVO pone lo que sabe hacer bien y con baja latencia:
//      la interfaz nativa, el tactil, el teclado, las pestanas, el
//      historial, los favoritos, la cache, las paginas internas y el
//      reproductor multimedia.
//    · UN SERVICIO REMOTO aislado (Node.js + Playwright, en server/)
//      ejecuta Chromium de verdad y devuelve la pagina YA RASTERIZADA
//      como JPEG del tamano exacto de la pantalla.
//    · El transporte es HTTPS para el control y WebSocket (wss) para
//      la sesion interactiva.
//
//  El usuario ve texto e imagenes reales de sitios modernos, pulsa
//  enlaces, se desplaza y escribe. Lo que viaja son pixeles hacia
//  abajo y eventos hacia arriba.
//
//  ESTE FICHERO NO DIBUJA NADA. Igual que FlexOS_OTA.cpp, es una
//  unidad de traduccion aparte que no ve las primitivas `static` de
//  los .ino: el dibujo entra por FlexOS_Browser_Bridge.h.
//
//  REPARTO EN FICHEROS
//  -------------------
//    FlexOS_Browser.h          este: API, tipos, protocolo, capacidades
//    FlexOS_Browser.cpp        nucleo PURO y portable (se prueba en PC):
//                              omnibox, validacion de URL/SSRF, paginas
//                              internas, historial, favoritos, ajustes,
//                              codec del protocolo, telemetria
//    FlexOS_BrowserApp.cpp     lado DISPOSITIVO (solo ARDUINO): memoria y
//                              capacidades medidas, persistencia, cliente
//                              WebSocket sobre TLS en su propia tarea,
//                              decodificacion de frames, interfaz y gestos
//    FlexOS_Browser_Bridge.h   puente con las primitivas del .ino y
//                              teclado del omnibox (solo ARDUINO)
//
//  El nucleo se compila TAMBIEN en el PC (tests/host) para poder
//  probar de verdad el analizador del omnibox y el protocolo.

#ifndef BR_LOGIC_H
#define BR_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// sdkconfig.h ANTES que nada, por el mismo motivo que en FlexOS_OTA.h:
// de ahi salen las macros CONFIG_IDF_TARGET_* con las que se decide la
// plataforma. En el PC no existe, y entonces se compila el perfil de
// pruebas de host.
/* PORT (reloj): el mismo perfil (S3) en el reloj, en tests/host y en la
 * vista previa, para que las pruebas midan lo que corre en el reloj. */
#define FLEXBR_ON_DEVICE 1

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================
//  0) GUARDIA DE VERSION ENTRE FICHEROS
//  -------------------------------------------------------------
//  El navegador son CUATRO ficheros que se copian a mano a la carpeta
//  del sketch (FlexOS_Browser.h/.cpp, FlexOS_BrowserApp.cpp y
//  FlexOS_Browser_Bridge.h). Si se actualiza solo alguno, el proyecto
//  COMPILA IGUAL y el fallo aparece en la placa como un
//  comportamiento raro y silencioso -- que es lo peor posible de
//  diagnosticar sin depurador.
//
//  Aqui se cierra esa puerta de dos formas:
//   · cada fichero comprueba en COMPILACION que ve este mismo numero;
//   · el puente llama a una funcion cuyo NOMBRE lleva el numero
//     dentro, asi que un FlexOS_BrowserApp.cpp viejo da un error de
//     ENLACE que dice literalmente lo que hay que hacer.
//
//  Sube este numero cuando cambie algo que obligue a recopiar todo.
// =============================================================
#define FLEXBR_BUILD 6

// =============================================================
//  1) INTERRUPTORES MAESTROS
//  -------------------------------------------------------------
//  Mismo patron que GLASS_*_ON / KIOSK_ON / FLEXOS_OTA_ON: cada
//  bloque se puede apagar solo, sin tocar los demas, y a 0 el codigo
//  compila a casi nada. Sirven para aislar un problema en la placa
//  sin desmontar la app entera.
// =============================================================
#ifndef FLEXBR_ON
#define FLEXBR_ON            1   // interruptor general de la app
#endif
#ifndef FLEXBR_REMOTE_ON
#define FLEXBR_REMOTE_ON     1   // sesion remota (WebSocket + frames)
#endif
#ifndef FLEXBR_TILES_ON
#define FLEXBR_TILES_ON      1   // regiones sucias ademas de frames completos
#endif
#ifndef FLEXBR_MEDIA_ON
#define FLEXBR_MEDIA_ON      1   // deteccion multimedia + reproductor propio
#endif
#ifndef FLEXBR_TELEMETRY_ON
#define FLEXBR_TELEMETRY_ON  1   // panel local de depuracion (flex://about)
#endif
// TLS. A 1 el transporte exige wss://. Se puede bajar a 0 SOLO para
// pruebas en una red de laboratorio; el codigo avisa en la interfaz.
#ifndef FLEXBR_REQUIRE_TLS
#define FLEXBR_REQUIRE_TLS   1
#endif

// =============================================================
//  2) PLATAFORMA Y PRESUPUESTOS REALES
//  -------------------------------------------------------------
//  No hay una lista de modelos escrita a mano: la plataforma sale de
//  CONFIG_IDF_TARGET_*, igual que en el modulo OTA. Los presupuestos
//  SI son por placa, porque la memoria disponible es radicalmente
//  distinta y fingir lo contrario acaba en un reinicio por falta de
//  heap a mitad de una pagina.
// =============================================================
#define FLEXBR_PLAT_ULTRA_S3  1
#define FLEXBR_PLAT_NAME      "Flex OS Watch Ultra"

#ifndef FLEXBR_PLAT_ULTRA
#define FLEXBR_PLAT_ULTRA    0
#endif
#ifndef FLEXBR_PLAT_ULTRA_S3
#define FLEXBR_PLAT_ULTRA_S3 0
#endif
#ifndef FLEXBR_PLAT_PRO
#define FLEXBR_PLAT_PRO      0
#endif
#ifndef FLEXBR_PLAT_HOST
#define FLEXBR_PLAT_HOST     0
#endif

// -------------------------------------------------------------
//  Presupuesto de memoria por placa.
//
//  DECISION CLAVE: la "cache de pagina" es el JPEG COMPRIMIDO, no el
//  mapa de bits descomprimido. Un frame de 480x800 en RGB565 son
//  768 KB; el mismo frame en JPEG de calidad util ronda 20-60 KB. Al
//  guardar el comprimido y re-decodificar cuando hay que repintar
//  (cerrar un menu, volver del reproductor) se ahorran esos 768 KB
//  enteros, que en el Pro es la diferencia entre funcionar y no
//  arrancar. El coste es CPU en un repintado ocasional, no por frame.
// -------------------------------------------------------------
#if FLEXBR_PLAT_PRO
  #define FLEXBR_KEYFRAME_MAX   28672    // 28 KB  (RAM interna: no hay PSRAM)
  #define FLEXBR_TILE_MAX       0        // sin regiones sucias en esta placa
  #define FLEXBR_MAX_TABS       1
  #define FLEXBR_HIST_MAX       24
  #define FLEXBR_BM_MAX         12
  #define FLEXBR_NET_STACK      6144
  // Sin PSRAM TODO sale del heap interno: el buffer de recepcion, el
  // historial y los favoritos. Hay que contarlo para no prometer una
  // sesion remota que despues no cabe.
  #define FLEXBR_OWN_INTERNAL   (32u * 1024u + 15u * 1024u + 7u * 1024u)
#elif FLEXBR_PLAT_ULTRA_S3
  #define FLEXBR_KEYFRAME_MAX   131072   // 128 KB en PSRAM
  #define FLEXBR_TILE_MAX       49152    // 48 KB
  #define FLEXBR_MAX_TABS       4
  #define FLEXBR_HIST_MAX       80
  #define FLEXBR_BM_MAX         40
  #define FLEXBR_NET_STACK      8192
  #define FLEXBR_OWN_INTERNAL   0u        // los buffers grandes van a PSRAM
#else  /* ULTRA (P4) y HOST */
  #define FLEXBR_KEYFRAME_MAX   196608   // 192 KB en PSRAM
  #define FLEXBR_TILE_MAX       65536    // 64 KB
  #define FLEXBR_MAX_TABS       6
  #define FLEXBR_HIST_MAX       120
  #define FLEXBR_BM_MAX         60
  #define FLEXBR_NET_STACK      10240
  #define FLEXBR_OWN_INTERNAL   0u        // los buffers grandes van a PSRAM
#endif

// -------------------------------------------------------------
//  DOS UMBRALES DE HEAP, Y NO UNO, PORQUE SE MIRAN EN MOMENTOS
//  DISTINTOS:
//
//    FLEXBR_TLS_HEAP      heap INTERNO libre que hace falta EN EL
//                         MOMENTO DE CONECTAR. Es lo que reserva
//                         mbedTLS para sus buffers de registro mas el
//                         socket. Se comprueba en la tarea de red,
//                         cuando los buffers del navegador YA estan
//                         reservados.
//    FLEXBR_MIN_FREE_HEAP el anterior MAS lo que el propio navegador va
//                         a reservar del heap interno (0 en las placas
//                         con PSRAM). Se comprueba AL ABRIR LA APP,
//                         antes de reservar nada, para decidir si la
//                         sesion remota es viable. Si no lo es, la
//                         capacidad se apaga y la interfaz explica por
//                         que, con el numero real de KB.
//
//  Un solo umbral daba un falso positivo en Flex OS Pro: pasaba la
//  comprobacion al abrir, se reservaban 54 KB y despues TLS ya no cabia.
// -------------------------------------------------------------
#ifndef FLEXBR_TLS_HEAP
#define FLEXBR_TLS_HEAP       46000u
#endif
#define FLEXBR_MIN_FREE_HEAP  (FLEXBR_TLS_HEAP + FLEXBR_OWN_INTERNAL)

// -------------------------------------------------------------
//  Capacidades. Se resuelven EN EJECUCION (memoria medida, PSRAM
//  presente, ajustes del usuario) y la interfaz explica por que algo
//  esta desactivado en vez de limitarse a no funcionar.
// -------------------------------------------------------------
enum {
  FLEXBR_CAP_REMOTE   = 1u << 0,   // sesion remota con frames
  FLEXBR_CAP_TILES    = 1u << 1,   // regiones sucias
  FLEXBR_CAP_MEDIA    = 1u << 2,   // reproductor multimedia propio
  FLEXBR_CAP_TABS     = 1u << 3,   // mas de una pestana
  FLEXBR_CAP_TLS      = 1u << 4,   // TLS disponible
  FLEXBR_CAP_PSRAM    = 1u << 5,   // hay PSRAM utilizable
  FLEXBR_CAP_FS       = 1u << 6    // LittleFS montado (historial/favoritos)
};

// =============================================================
//  3) LIMITES DE TEXTO
//  -------------------------------------------------------------
//  Todo son buffers fijos a proposito: ni un String dinamico en las
//  rutas que se ejecutan por frame o por evento tactil. Es la misma
//  regla que sigue FlexOS_FS.h y por el mismo motivo (fragmentacion
//  del heap = tirones en pantalla).
// =============================================================
#define FLEXBR_URL_MAX     512    // URL resuelta
#define FLEXBR_INPUT_MAX   256    // lo que el usuario puede teclear
#define FLEXBR_TITLE_MAX   80
#define FLEXBR_HOST_MAX    128
#define FLEXBR_ERRMSG_MAX  120
#define FLEXBR_TOKEN_MAX   96     // credencial del dispositivo
#define FLEXBR_SERVER_MAX  160    // wss://host:puerto/ruta

// =============================================================
//  4) PROTOCOLO  ·  FBP/1  (Flex Browser Protocol, version 1)
//  -------------------------------------------------------------
//  Binario, pequeno y versionado. Todos los enteros van en
//  LITTLE-ENDIAN (es el orden nativo del ESP32: cero conversiones en
//  el lado con menos CPU). Las cadenas van SIEMPRE con longitud por
//  delante (uint16) y NO llevan terminador: no hay forma de que una
//  cadena sin cerrar arrastre la lectura del resto del mensaje.
//
//  Cabecera de 12 bytes:
//     0  uint8   'F'
//     1  uint8   'B'
//     2  uint8   version (1)
//     3  uint8   tipo
//     4  uint8   flags
//     5  uint8   canal   (0 = sesion, 1..N = pestana)
//     6  uint16  seq
//     8  uint32  longitud de la carga
//    12  carga
//
//  SEPARACION CONTROL / IMAGEN: son tipos distintos sobre la misma
//  conexion, y el receptor puede descartar un FRAME viejo sin perder
//  ni un mensaje de control. El numero de secuencia es lo que permite
//  tirar frames atrasados en vez de dibujarlos tarde.
// =============================================================
#define FBP_MAGIC0        'F'
#define FBP_MAGIC1        'B'
#define FBP_VERSION       1
#define FBP_HDR_SIZE      12
// Tope duro de carga util. Un servidor comprometido no puede pedir al
// dispositivo que reserve mas que esto: se cierra la sesion.
#define FBP_PAYLOAD_MAX   (FLEXBR_KEYFRAME_MAX + 4096)

// ---- Dispositivo -> servicio ----
enum {
  FBP_C_HELLO      = 0x01,
  FBP_C_PING       = 0x02,
  FBP_C_ACK        = 0x03,   // uint16 lastSeq, uint8 pendientes
  FBP_C_NAVIGATE   = 0x10,   // str url
  FBP_C_BACK       = 0x11,
  FBP_C_FORWARD    = 0x12,
  FBP_C_RELOAD     = 0x13,
  FBP_C_STOP       = 0x14,
  FBP_C_POINTER    = 0x20,   // u8 accion, u16 x, u16 y, u8 boton
  FBP_C_SCROLL     = 0x21,   // i16 dx, i16 dy
  FBP_C_KEY        = 0x22,   // u8 accion, u8 codigo, u8 mods, str texto
  FBP_C_VIEWPORT   = 0x23,   // u16 w, u16 h, u8 calidad, u8 perfil, u8 escala%
  FBP_C_TAB_NEW    = 0x30,
  FBP_C_TAB_CLOSE  = 0x31,   // u8 id
  FBP_C_TAB_SELECT = 0x32,   // u8 id
  FBP_C_MEDIA      = 0x40,   // u8 orden, u32 argumento
  FBP_C_REQ_FRAME  = 0x41    // pide un frame COMPLETO (tras un fallo de decodificacion)
};

// ---- Servicio -> dispositivo ----
enum {
  FBP_S_WELCOME    = 0x81,
  FBP_S_STATE      = 0x82,
  FBP_S_FRAME      = 0x83,
  FBP_S_MEDIA      = 0x84,
  FBP_S_ERROR      = 0x85,
  FBP_S_PONG       = 0x86,
  FBP_S_NAVIGATED  = 0x87,
  FBP_S_TABS       = 0x88,
  FBP_S_DOWNLOAD   = 0x89
};

// Acciones de puntero
enum { FBP_PTR_DOWN = 0, FBP_PTR_UP = 1, FBP_PTR_MOVE = 2, FBP_PTR_TAP = 3, FBP_PTR_CANCEL = 4 };
// Acciones de teclado
enum { FBP_KEY_TEXT = 0, FBP_KEY_DOWN = 1, FBP_KEY_UP = 2 };
// Teclas especiales (codigo != 0 con FBP_KEY_DOWN)
enum { FBP_VK_NONE = 0, FBP_VK_ENTER = 1, FBP_VK_BACKSPACE = 2, FBP_VK_TAB = 3,
       FBP_VK_ESC = 4, FBP_VK_UP = 5, FBP_VK_DOWN = 6, FBP_VK_LEFT = 7, FBP_VK_RIGHT = 8 };
// Ordenes al reproductor
enum { FBP_MED_PLAY = 0, FBP_MED_PAUSE = 1, FBP_MED_SEEK = 2, FBP_MED_STOP = 3,
       FBP_MED_VOLUME = 4, FBP_MED_OPEN = 5 };

// Flags de STATE
enum {
  FBP_ST_LOADING  = 1u << 0,
  FBP_ST_CAN_BACK = 1u << 1,
  FBP_ST_CAN_FWD  = 1u << 2,
  FBP_ST_SECURE   = 1u << 3,   // https con certificado valido
  FBP_ST_INSECURE = 1u << 4,   // http plano o certificado con avisos
  FBP_ST_MEDIA    = 1u << 5,   // la pagina tiene multimedia reproducible
  FBP_ST_EDITING  = 1u << 6    // la pagina tiene el foco en un campo de texto
};
// Canal reservado para el flujo del reproductor. Los canales 1..N son
// pestanas; este va aparte para que el video no se confunda nunca con
// un frame de pagina ni compita por el mismo hueco.
#define FBP_CH_MEDIA  200
// Flags de FRAME
enum {
  FBP_FR_KEYFRAME = 1u << 0,   // frame COMPLETO (region = viewport entero)
  FBP_FR_LAST     = 1u << 1    // ultima region de esta actualizacion
};
// Formatos de imagen que el dispositivo declara saber decodificar.
enum { FBP_IMG_JPEG = 1 };

// Codigos de error del servicio (FBP_S_ERROR). Se traducen a texto en
// el dispositivo para que el mensaje siga el idioma del sistema.
enum {
  FBP_ERR_NONE = 0,
  FBP_ERR_AUTH = 1,          // credencial del dispositivo rechazada
  FBP_ERR_BLOCKED = 2,       // destino prohibido (SSRF, esquema, lista negra)
  FBP_ERR_NAV = 3,           // la navegacion fallo (DNS, conexion, 5xx)
  FBP_ERR_TIMEOUT = 4,
  FBP_ERR_LIMIT = 5,         // limite de sesiones/pestanas/ancho de banda
  FBP_ERR_INTERNAL = 6,
  FBP_ERR_CERT = 7,          // certificado del sitio invalido
  FBP_ERR_PROTOCOL = 8,      // mensaje mal formado
  FBP_ERR_EXPIRED = 9        // sesion caducada por inactividad
};

// =============================================================
//  5) ESTADO DE LA APP
// =============================================================
// Vistas del navegador.
enum {
  BRV_PAGE = 0,      // pagina remota (o el hueco de error / offline)
  BRV_INTERNAL,      // pagina interna flex://
  BRV_OMNIBOX,       // omnibox en edicion, con teclado
  BRV_MENU,          // menu propio de Flex OS
  BRV_TABS,          // selector de pestanas
  BRV_MEDIA          // reproductor a pantalla completa
};

// Paginas internas. El orden importa: es el que usa flexBrPageName().
enum {
  BRP_NONE = -1,
  BRP_NEWTAB = 0,
  BRP_HISTORY,
  BRP_BOOKMARKS,
  BRP_SETTINGS,
  BRP_DOWNLOADS,
  BRP_OFFLINE,
  BRP_ABOUT,
  BRP_N
};

// Resultado de resolver lo que el usuario escribio en el omnibox.
enum {
  BRI_EMPTY = 0,     // no habia nada: no se navega
  BRI_INTERNAL,      // pagina interna, NO se envia al servidor
  BRI_URL,           // http(s) validado
  BRI_SEARCH,        // consulta al buscador configurado
  BRI_BLOCKED        // bloqueado por seguridad
};

// Motivos de bloqueo (para poder explicarlo, no solo negarlo).
enum {
  BRB_NONE = 0,
  BRB_SCHEME,        // file://, javascript:, data:, ftp:, ...
  BRB_LOCALHOST,     // localhost, *.local, .internal
  BRB_PRIVATE_IP,    // 10/8, 192.168/16, 127/8, 169.254/16, fc00::/7, ...
  BRB_USERINFO,      // http://usuario:clave@sitio  (riesgo de suplantacion)
  BRB_TOOLONG,       // supera FLEXBR_URL_MAX
  BRB_BADCHARS,      // caracteres de control o codificacion invalida
  BRB_NOHOST         // no hay host reconocible
};

typedef struct {
  int  kind;                       // BRI_*
  int  page;                       // BRP_* si kind == BRI_INTERNAL
  int  block;                      // BRB_* si kind == BRI_BLOCKED
  char url[FLEXBR_URL_MAX];        // resultado final, ya codificado
} BrResolved;

// Perfiles de rendimiento. NO prometen una tasa de frames concreta:
// fijan calidad, escala y cuantos frames se dejan en vuelo. Lo que se
// mide de verdad esta en la telemetria (flex://about).
enum { BRQ_LOWLAT = 0, BRQ_BALANCED = 1, BRQ_DATASAVER = 2, BRQ_N };

// ---- DE DONDE SALE EL NAVEGADOR --------------------------------
// El P4 pinta la interfaz, las pestanas, el historial y el tactil;
// quien EJECUTA la pagina esta al otro lado del protocolo FBP/1.
// Ese "otro lado" puede ser ahora de tres sitios distintos, y el
// usuario elige cual:
//
//   BRSRC_AUTO   -> primero el telefono si esta conectado; si no,
//                   la nube configurada; y por ultimo el servidor
//                   manual. Es el valor de fabrica.
//   BRSRC_PHONE  -> SOLO Flex Phone (Browser Relay por Wi-Fi).
//   BRSRC_CLOUD  -> SOLO el servidor en la nube del usuario.
//   BRSRC_MANUAL -> SOLO el servidor Ubuntu/PC de siempre.
//
// El modo manual es EXACTAMENTE el comportamiento anterior: quien
// ya tenia su servidor no nota ningun cambio.
enum { BRSRC_AUTO = 0, BRSRC_PHONE, BRSRC_CLOUD, BRSRC_MANUAL, BRSRC_N };
#define BRSRC_NONE (-1)   // ninguna fuente utilizable ahora mismo

typedef struct {
  char     server[FLEXBR_SERVER_MAX];   // wss://host:puerto/v1/session
  char     token[FLEXBR_TOKEN_MAX];     // credencial del dispositivo
  uint8_t  searchEngine;                // 0 DuckDuckGo · 1 Google · 2 Bing · 3 personalizado
  char     searchCustom[FLEXBR_SERVER_MAX];  // plantilla con %s
  uint8_t  profile;                     // BRQ_*
  uint8_t  quality;                     // 20..90 (calidad JPEG que se pide)
  uint8_t  scalePct;                    // 50..100 (escala del viewport remoto)
  bool     allowLocal;                  // permitir IP/localhost (SOLO desarrollo)
  bool     allowInsecure;               // permitir ws:// sin TLS (SOLO desarrollo)
  bool     saveHistory;
  bool     telemetry;
  bool     mediaNative;                 // usar el reproductor propio al detectar video
  uint8_t  source;                      // BRSRC_*: de donde sale el navegador
  char     cloudServer[FLEXBR_SERVER_MAX];  // wss://... del servidor en la nube
} BrSettings;

// Una entrada de historial. `visits` y `lastMs` permiten ordenar sin
// guardar un fichero de indice aparte.
typedef struct {
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
  uint32_t lastMs;
  uint16_t visits;
} BrHistEntry;

typedef struct {
  char url[FLEXBR_URL_MAX];
  char title[FLEXBR_TITLE_MAX];
} BrBookmark;

// Estado de UNA pestana en el dispositivo. El contenido real vive en el
// servicio; aqui solo se guarda lo que la interfaz necesita pintar.
typedef struct {
  bool     used;
  uint8_t  remoteId;                 // id de la sesion remota (canal FBP)
  int      view;                     // BRV_PAGE / BRV_INTERNAL
  int      page;                     // BRP_* cuando view == BRV_INTERNAL
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
  uint8_t  stateFlags;               // FBP_ST_*
  uint8_t  progress;                 // 0..100
  bool     hasFrame;                 // hay un keyframe valido para repintar
  uint32_t lastUseMs;                // para suspender la mas antigua
} BrTab;

// Telemetria local. Se puede apagar entera (FLEXBR_TELEMETRY_ON = 0 o
// el ajuste del usuario) y no se envia a ningun sitio: solo se muestra
// en flex://about.
typedef struct {
  uint32_t framesOk, framesDropped, framesBad;
  uint32_t bytesIn, bytesOut;
  uint32_t reconnects, decodeMsLast, decodeMsMax;
  uint32_t rttMs;
  uint32_t freeHeap, freePsram, minFreeHeap;
  uint32_t navCount, errCount;
  // Viewport que el servicio tiene pedido AHORA (el ultimo HELLO o VIEWPORT
  // enviado) y cuantas veces se ha renegociado. Es lo que permite ver en
  // flex://about -- y comprobar en las pruebas -- que redimensionar la ventana
  // llega de verdad al servicio, en vez de suponerlo.
  uint16_t viewW, viewH;
  uint32_t viewportMsgs;
  // Quien ejecuta las paginas, segun el id de sesion de su WELCOME (ver
  // flexBrRelayBuild): relayKind BRRELAY_* y, si es el telefono, que build del
  // Browser Relay corre. Es lo que permite DECIR "tu Flex Phone es antiguo" en vez
  // de que el usuario vea una web ampliada y no sepa por que.
  int16_t  relayBuild;
  uint8_t  relayKind;
} BrStats;

// Estado del transporte.
enum {
  BRN_OFF = 0,        // no configurado / app cerrada
  BRN_NOWIFI,         // sin Wi-Fi
  BRN_CONNECTING,
  BRN_HANDSHAKE,
  BRN_READY,
  BRN_ERROR
};

// =============================================================
//  6) API DEL NUCLEO PURO  (portable, se prueba en el PC)
// =============================================================

// Deja `st` con los valores por defecto (servidor vacio, DuckDuckGo,
// perfil equilibrado, sin permisos de desarrollo).
void  flexBrSettingsDefaults(BrSettings* st);

// Nombre legible de una fuente (para Ajustes y para la app Flex Phone).
const char* flexBrSourceName(int src);
// Que fuente se va a USAR de verdad, dadas las disponibilidades REALES.
// Devuelve BRSRC_PHONE / BRSRC_CLOUD / BRSRC_MANUAL, o BRSRC_NONE si
// nada esta disponible. NUNCA devuelve BRSRC_AUTO: "automatico" es una
// preferencia, no un destino.
//   phoneUp   -> hay Browser Relay ARRIBA en el telefono (ip y puerto reales)
//   cloudCfg  -> hay servidor de nube configurado
//   manualCfg -> hay servidor manual configurado
int flexBrSourceResolve(const BrSettings* st, bool phoneUp, bool cloudCfg, bool manualCfg);
// Por que no hay fuente utilizable. Cadena estatica, nunca NULL.
const char* flexBrSourceWhyNone(const BrSettings* st, bool phoneUp, bool cloudCfg, bool manualCfg);

// Resuelve lo que el usuario confirmo en el omnibox. Es la funcion mas
// sensible del modulo -- ver el orden exacto en FlexOS_Browser.cpp.
// Nunca concatena texto sin codificar y nunca devuelve una URL mas
// larga de FLEXBR_URL_MAX.
int   flexBrResolveInput(const char* text, const BrSettings* st, BrResolved* out);

// Comprueba una URL ya formada (por ejemplo, la que anuncia el servidor
// tras una redireccion). Devuelve BRB_NONE si es aceptable.
int   flexBrCheckUrl(const char* url, const BrSettings* st);

// Reconoce `flex://<pagina>` y devuelve BRP_* (o BRP_NONE).
int   flexBrInternalPage(const char* url);
const char* flexBrPageName(int page);      // "newtab", "history", ...
const char* flexBrPageTitle(int page);     // titulo legible en espanol

// Codificacion porcentual de un componente de consulta (RFC 3986).
// Devuelve los bytes escritos sin contar el 0 final, o -1 si no cabe.
int   flexBrPercentEncode(const char* in, char* out, size_t outN);

// Extrae el host de una URL http(s) a `out`. false si no hay host.
bool  flexBrUrlHost(const char* url, char* out, size_t outN);

// Etiqueta corta para la barra: host sin "www." y sin esquema.
void  flexBrUrlLabel(const char* url, char* out, size_t outN);

// Texto del motivo de bloqueo, en espanol, listo para pintar.
const char* flexBrBlockReason(int block);
// Texto de un error del servicio.
const char* flexBrErrorText(int code);

// ---- Codec del protocolo (sin red: solo bytes) ----
typedef struct {
  uint8_t  type, flags, channel;
  uint16_t seq;
  uint32_t len;
} FbpHeader;

// Escribe una cabecera en `buf` (>= FBP_HDR_SIZE). Devuelve FBP_HDR_SIZE.
int  fbpWriteHeader(uint8_t* buf, uint8_t type, uint8_t flags, uint8_t channel,
                    uint16_t seq, uint32_t len);
// Lee una cabecera. false si la magia o la version no cuadran, o si la
// longitud declarada supera FBP_PAYLOAD_MAX.
bool fbpReadHeader(const uint8_t* buf, size_t n, FbpHeader* out);

// Constructores de mensajes del cliente. Todos devuelven el total de
// bytes escritos (cabecera incluida) o -1 si no caben en `buf`.
int fbpBuildHello(uint8_t* buf, size_t n, uint16_t seq, const char* token,
                  const char* deviceId, uint16_t w, uint16_t h,
                  uint8_t quality, uint8_t profile, uint32_t caps);
int fbpBuildNavigate(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, const char* url);
int fbpBuildSimple(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t type);
int fbpBuildPointer(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                    uint8_t action, uint16_t x, uint16_t y, uint8_t button);
int fbpBuildScroll(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, int16_t dx, int16_t dy);
int fbpBuildKey(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                uint8_t action, uint8_t code, uint8_t mods, const char* text);
int fbpBuildViewport(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                     uint16_t w, uint16_t h, uint8_t quality, uint8_t profile, uint8_t scalePct);
int fbpBuildAck(uint8_t* buf, size_t n, uint16_t seq, uint16_t lastSeq, uint8_t pending);
int fbpBuildMedia(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t cmd, uint32_t arg);

// Lectores de los mensajes del servidor. Devuelven false si la carga
// esta mal formada (y entonces NO se toca la salida).
typedef struct {
  uint8_t  protoVer;
  uint16_t viewW, viewH;
  uint32_t caps;
  uint16_t maxTabs;
  uint32_t maxFrameBytes;
  char     sessionId[40];
} FbpWelcome;
bool fbpParseWelcome(const uint8_t* p, uint32_t n, FbpWelcome* out);

// ---- BUILD DEL BROWSER RELAY DEL TELEFONO --------------------------
// El relay del telefono anuncia su build al final del id de sesion del WELCOME
// ("flexphone-18c3f2a.r6"; el formato lo fija Fbp.tagSessionId en Kotlin y se
// comprueba con los mismos ejemplos en las dos pruebas). Antes de r6 el relay
// maquetaba la pagina con la densidad del telefono (la web salia ampliada
// ~2,75x) y al desplazar movia la superficie entera: hay que reinstalar Flex Phone.
#define FLEXBR_RELAY_MIN_BUILD 6
enum { BRRELAY_UNKNOWN = 0, BRRELAY_PHONE = 1, BRRELAY_OTHER = 2 };
// Build anunciado en `sessionId`, o -1 si no lleva marca ".rN" (un relay anterior
// o un servicio de otro tipo). Nunca lee mas alla del terminador.
int  flexBrRelayBuild(const char* sessionId);
// BRRELAY_PHONE si el id es el de un Browser Relay de Flex Phone ("flexphone-..."),
// BRRELAY_OTHER si es de cualquier otro servicio (el Ubuntu/PC), BRRELAY_UNKNOWN si esta vacio.
int  flexBrRelayKind(const char* sessionId);
// ¿Es un relay del TELEFONO anterior al que arregla el viewport y el desplazamiento?
// Un servicio de otro tipo nunca es "antiguo" por esto.
bool flexBrRelayIsStale(const char* sessionId);

typedef struct {
  uint8_t  flags, progress;
  char     title[FLEXBR_TITLE_MAX];
  char     url[FLEXBR_URL_MAX];
} FbpState;
bool fbpParseState(const uint8_t* p, uint32_t n, FbpState* out);

typedef struct {
  uint16_t x, y, w, h;
  uint8_t  format, flags;
  uint32_t frameId;
  const uint8_t* img;      // apunta DENTRO de la carga: no se copia
  uint32_t imgLen;
} FbpFrame;
bool fbpParseFrame(const uint8_t* p, uint32_t n, FbpFrame* out);

typedef struct {
  uint8_t  kind;           // 0 ninguno · 1 video · 2 audio
  uint32_t durationMs;
  uint16_t w, h;
  char     mime[40];
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
} FbpMedia;
bool fbpParseMedia(const uint8_t* p, uint32_t n, FbpMedia* out);

typedef struct { uint16_t code; char msg[FLEXBR_ERRMSG_MAX]; } FbpError;
bool fbpParseError(const uint8_t* p, uint32_t n, FbpError* out);

// ---- Utilidades del apreton de manos WebSocket (RFC 6455) ----
//  Viven en el nucleo PORTABLE y no en el transporte para poder
//  probarlas contra los vectores del propio RFC: si el calculo de
//  Sec-WebSocket-Accept estuviera mal, el sintoma en la placa seria
//  "no conecta" sin mas pista, y eso es carisimo de depurar a ciegas.
void flexBrSha1(const uint8_t* data, size_t n, uint8_t out[20]);
int  flexBrBase64(const uint8_t* in, size_t n, char* out, size_t outN);
// Calcula el valor que el servidor DEBE devolver en Sec-WebSocket-Accept
// para la clave `clientKeyB64`. false si no cabe.
bool flexBrWsAccept(const char* clientKeyB64, char* out, size_t outN);
// Trocea "wss://host:puerto/ruta". `tls` sale a true con wss://.
bool flexBrParseWsUrl(const char* url, bool* tls, char* host, size_t hostN,
                      uint16_t* port, char* path, size_t pathN);


#ifdef __cplusplus
}
#endif

#endif
