/*
 * flex_mp3 — RELOJ: comprobación de un MP3 SIN decodificarlo (lógica pura,
 * se prueba en el PC). La usan el servidor web (lo que sube el móvil no se
 * publica como "reproducible" si no tiene audio MPEG de verdad) y la
 * biblioteca (duración de lo que ya estaba en el disco).
 *
 *  - Salta la etiqueta ID3v2 (con su pie, si lo tiene) por su tamaño
 *    declarado, acotado al archivo.
 *  - Busca la primera trama MPEG-1/2/2.5 capa III y la da por buena solo si
 *    las DOS siguientes también lo son y concuerdan (versión, frecuencia):
 *    los bytes 0xFFE que aparecen al azar dentro de una portada no engañan.
 *    Excepción: un clip de exactamente dos tramas vale; una sola, no.
 *  - Duración: con la cabecera Xing/Info o VBRI (VBR) por número de tramas;
 *    sin ella, por la tasa de la primera trama y los bytes de audio.
 * No reserva memoria. Lee como mucho FLEXMP3_SCAN_MAX bytes buscando la sincronía.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "FlexOS_Media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXMP3_SCAN_MAX (128u * 1024u)

enum { FLEXMP3_OK = 0, FLEXMP3_ERR_IO = -1, FLEXMP3_ERR_FORMAT = -2 };

typedef struct {
    uint32_t sampleRate;   /**< Hz */
    uint8_t channels;      /**< 1 o 2 */
    uint8_t version;       /**< 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5 */
    uint16_t kbps;         /**< tasa de la primera trama */
    bool vbr;              /**< había cabecera Xing/VBRI con número de tramas */
    uint32_t frames;       /**< tramas (0 = no se sabe) */
    uint32_t dataStart;    /**< primera trama */
    uint32_t audioStart;   /**< primera trama CON audio: si la primera es la cabecera
                                Xing/Info/VBRI (silencio), la siguiente */
    uint32_t durMs;        /**< duración (estimada sin cabecera VBR) */
} FlexMp3Info;

/** Datos de una cabecera de trama (4 bytes). false si no es MPEG capa III válida. */
typedef struct {
    uint8_t version;     /* 1, 2 o 25 */
    uint16_t kbps;
    uint32_t sampleRate;
    uint8_t channels;
    uint16_t frameBytes; /* incluido el relleno */
    uint16_t samples;    /* por trama: 1152 (MPEG-1) o 576 */
} FlexMp3Hdr;
bool flexMp3ParseHeader(const uint8_t h[4], FlexMp3Hdr *out);

int flexMp3Probe(const FlexMediaIO *io, FlexMp3Info *out);

#ifdef __cplusplus
}
#endif
