// FlexOS_JPEG.h DEL RELOJ: el contrato de decodificación que usan
// FlexOS_MediaThumb (miniaturas que VALIDAN la foto entera) y FlexOS_JPEGEnc
// (tipos de reserva), con el MISMO nombre y los mismos códigos que el de
// Flex OS Ultra (FlexOS_Ultra/FlexOS_JPEG.h @ 5dbc949).
//
// RELOJ: el decodificador del P4 (FlexOS_JPEG.cpp) NO se porta: su IDCT
// parece derivada de la del IJG (jidctint.c) sin atribución
// (docs/AUDITORIA-FASE0.md §4). Aquí lo hace TJpgDec, el mismo que ya usa la
// Galería del reloj:
//  - en el reloj, el de la ROM del ESP32-S3 (R0.01, escala 1/2, 1/4 y 1/8);
//  - en el PC (pruebas y vista previa), el de LVGL (R0.03, sin escala: se
//    reduce después con una media de caja, como hace la ROM a 1/8 con el
//    coeficiente DC).
// Solo se declara lo que se usa: el modo flujo con filas RGB888.
#ifndef FLEXOS_JPEG_H
#define FLEXOS_JPEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FLEXJPG_OK = 0,
  FLEXJPG_ERR_ARG = -1,          // parametros invalidos
  FLEXJPG_ERR_TRUNCATED = -2,    // se acabaron los bytes antes de tiempo
  FLEXJPG_ERR_BADMARKER = -3,    // no es un JPEG o la cabecera esta corrupta
  FLEXJPG_ERR_UNSUPPORTED = -4,  // progresivo, 12 bits, CMYK, muestreo que TJpgDec no admite
  FLEXJPG_ERR_MEMORY = -5,       // no hay RAM para los buffers de trabajo
  FLEXJPG_ERR_TOOBIG = -6,       // supera los limites de dimensiones pedidos
  FLEXJPG_ERR_HUFFMAN = -7,      // datos corruptos
  FLEXJPG_ERR_ABORTED = -8,      // el callback pidio parar
  FLEXJPG_ERR_IO = -9            // la funcion de lectura fallo
};

// Limite duro: una cabecera manipulada no reserva cientos de MB.
// RELOJ: 8192 (fotos de movil de 48 Mpx caben a 1/8 en ~1000 px de lado).
#define FLEXJPG_MAX_DIM        8192

typedef struct {
  int width, height;      // dimensiones REALES de la imagen (SOF)
  int outWidth, outHeight;// dimensiones que se entregan (ya escaladas)
  int components;         // 1 o 3 (TJpgDec entrega siempre RGB)
  int scaleDenom;         // 1, 2, 4 u 8
  bool progressive;       // true si era progresivo (y no se decodifico)
} FlexJpegInfo;

typedef void* (*FlexJpegAlloc)(size_t n);
typedef void  (*FlexJpegFree)(void* p);
// Lee hasta n bytes: >0 leidos, 0 fin del archivo, <0 error.
typedef int (*FlexJpegReadFn)(void* ctx, uint8_t* buf, size_t n);
// Divisor (1, 2, 4 u 8) con las dimensiones REALES. <= 0 cancela sin decodificar.
typedef int (*FlexJpegScaleFn)(void* user, int width, int height);
// Una fila RGB888 (R, G, B) de la imagen escalada, de arriba a abajo. El
// puntero solo vale durante la llamada. false = parar.
typedef bool (*FlexJpegRow888Cb)(void* user, int y, int w, const uint8_t* rgb);

// Decodifica leyendo por trozos (la foto nunca entera en RAM) y entrega filas
// RGB888. maxW/maxH/maxPixels: con pick NULL, el menor divisor con el que la
// salida cabe. Memoria de trabajo: 8 KB de TJpgDec + una banda de 16 filas de
// la anchura de salida (en el PC, de la de entrada).
int flexJpegDecode888Stream(FlexJpegReadFn rd, void* rdCtx,
                            int maxW, int maxH, uint32_t maxPixels, FlexJpegScaleFn pick,
                            FlexJpegInfo* info,
                            FlexJpegRow888Cb cb, void* user,
                            FlexJpegAlloc af, FlexJpegFree ff);

// Lee SOLO la cabecera (SOI..SOF): dimensiones y si es progresivo. No
// decodifica ni reserva memoria.
int flexJpegProbe(const uint8_t* data, size_t len, FlexJpegInfo* info);

// Filas RGB565 (uint16_t nativo: R en los 5 bits altos) de un JPEG en memoria.
typedef bool (*FlexJpegRowCb)(void* user, int y, int w, const uint16_t* rgb);
int flexJpegDecode(const uint8_t* data, size_t len,
                   int maxW, int maxH, uint32_t maxPixels,
                   FlexJpegInfo* info,
                   FlexJpegRowCb cb, void* user,
                   FlexJpegAlloc af, FlexJpegFree ff);

// Lo mismo con filas RGB888 (R, G, B), sin perder precision.
int flexJpegDecode888(const uint8_t* data, size_t len,
                      int maxW, int maxH, uint32_t maxPixels,
                      FlexJpegInfo* info,
                      FlexJpegRow888Cb cb, void* user,
                      FlexJpegAlloc af, FlexJpegFree ff);

const char* flexJpegErrStr(int err);

#ifdef __cplusplus
}
#endif

#endif // FLEXOS_JPEG_H
