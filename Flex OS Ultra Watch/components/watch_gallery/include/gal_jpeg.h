/*
 * gal_jpeg — JPEG baseline POR FLUJO a un lienzo RGB565 reducido, sin cargar
 * nunca la imagen original en memoria.
 *
 * TJpgDec lee la entrada a trozos de 512 bytes (de un archivo de LittleFS o
 * del cuerpo de una respuesta HTTP mientras llega) y entrega bloques MCU de
 * 8 o 16 líneas. Cada bloque se reduce al vuelo: cada píxel de destino es la
 * MEDIA de los de origen que caen en él (filtro de caja), acumulada en una
 * banda de GAL_JPEG_BAND filas que se vacía cuando el decodificador pasa a la
 * fila de bloques siguiente. Memoria, aparte del lienzo: el área de trabajo
 * de TJpgDec (GAL_JPEG_WORK) + la banda (ancho x GAL_JPEG_BAND x 16 B) + una
 * tabla de columnas (2 B por columna de origen).
 *
 * DECODIFICADOR:
 *  - en el reloj, el TJpgDec de la ROM del ESP32-S3 (R0.01, con escala 1/2,
 *    1/4 y 1/8: a 1/8 solo usa el coeficiente DC y es varias veces más
 *    rápido). LVGL enlaza su propio TJpgDec (sin escala) con los mismos
 *    nombres, así que la ROM se llama por alias (gal_rom.ld, direcciones de
 *    esp32s3.rom.ld);
 *  - en el PC (pruebas y vista previa), el TJpgDec de LVGL; la escala se
 *    hace entera con el filtro de caja. Mismo resultado salvo redondeos.
 * El JPEG progresivo lo rechaza TJpgDec (GAL_JPG_E_PROGRESSIVE), como el P4.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAL_JPEG_WORK 8192          /* área de trabajo de TJpgDec */
#define GAL_JPEG_BAND 20            /* filas de destino en la banda (bloque de 16 + las que quedan a medias) */
#define GAL_JPEG_MAX_PIXELS (64u * 1024u * 1024u) /* 64 Mpx: lo que no es una foto se rechaza */

enum {
    GAL_JPG_OK = 0,
    GAL_JPG_E_INPUT = -1,       /**< la entrada falló o se acabó antes de tiempo */
    GAL_JPG_E_FORMAT = -2,      /**< no es un JPEG o está dañado */
    GAL_JPG_E_PROGRESSIVE = -3, /**< JPEG progresivo: no se decodifica */
    GAL_JPG_E_NOMEM = -4,
    GAL_JPG_E_TOOBIG = -5,
    GAL_JPG_E_CANCEL = -6,
    GAL_JPG_E_SIZE = -7,        /**< el destino rechazó el tamaño (o solo se quería saberlo) */
};

typedef enum {
    GAL_FIT_CONTAIN = 0, /**< la imagen entera, centrada, con bandas de color bg (visor) */
    GAL_FIT_COVER,       /**< recorte centrado que llena el lienzo (miniatura cuadrada, como el P4) */
} gal_fit_t;

typedef struct {
    /** Lee hasta n bytes: >0 leídos, 0 fin, <0 error. */
    int (*read)(void *ud, uint8_t *buf, size_t n);
    void *ud;
    /** ¿Abandonar? NULL = nunca. */
    bool (*cancel)(void *ud);
    /** Memoria de trabajo (PSRAM en el reloj). NULL = malloc/free. */
    void *(*alloc)(size_t n);
    void (*free)(void *p);
} gal_jpeg_src_t;

/**
 * Tras leer la cabecera: tamaño real de la imagen. Devuelve el lienzo de
 * *dw x *dh píxeles RGB565 donde pintar (lo fija quien llama), o NULL para no
 * decodificar (GAL_JPG_E_SIZE).
 */
typedef uint16_t *(*gal_jpeg_dst_t)(void *ud, int src_w, int src_h, int *dw, int *dh);

typedef struct {
    int src_w, src_h;   /**< tamaño real */
    int x, y, w, h;     /**< dónde quedó la imagen en el lienzo (CONTAIN) */
    uint8_t scale;      /**< escala de TJpgDec usada (0 = 1/1 ... 3 = 1/8) */
    int tjpgd;          /**< último código de TJpgDec (diagnóstico) */
} gal_jpeg_info_t;

int gal_jpeg_decode(const gal_jpeg_src_t *src, gal_fit_t fit, gal_jpeg_dst_t dst, void *dst_ud, uint16_t bg,
                    gal_jpeg_info_t *info);

/** Tamaño que tendrá la imagen al encajarla sin ampliar en max_w x max_h. */
void gal_jpeg_fit(int w, int h, int max_w, int max_h, int *ow, int *oh);
/**
 * Reduce un lienzo RGB565 (dw <= sw, dh <= sh) con la media de las áreas que
 * caen en cada píxel: el "Ajustar" del visor a partir del 2x, sin decodificar
 * otra vez.
 */
void gal_rgb565_shrink(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh);
/** ¿El backend escala en el decodificador? (la ROM sí; el de LVGL no) */
bool gal_jpeg_native_scale(void);
const char *gal_jpeg_err_text(int err);

#ifdef __cplusplus
}
#endif
