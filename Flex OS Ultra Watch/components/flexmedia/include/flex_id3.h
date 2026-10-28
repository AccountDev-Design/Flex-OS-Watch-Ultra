/*
 * flex_id3 — RELOJ: etiquetas ID3v2 de un MP3 (lógica pura, se prueba en el
 * PC con datos dañados a propósito). Lo usan la biblioteca (título, artista y
 * álbum en el catálogo) y el reproductor (la portada).
 *
 * Solo LEE lo que hace falta y nunca confía en los tamaños declarados:
 *  - ID3v2.2, 2.3 y 2.4 (tamaños sincroseguros en 2.4, normales en 2.3,
 *    de 3 bytes en 2.2); cada marco se acota a la etiqueta y la etiqueta al
 *    archivo;
 *  - texto ISO-8859-1, UTF-16 con BOM, UTF-16BE y UTF-8 -> UTF-8 bien
 *    formado, recortado sin partir caracteres;
 *  - portada (APIC/PIC): dónde está y cuánto mide, SIN leerla; el
 *    reproductor la decodifica después por trozos si es JPEG;
 *  - la desincronización de toda la etiqueta (v2.3) no se deshace: si la
 *    lleva, se leen los textos que se puedan y la portada se ignora.
 * No reserva memoria. Lee como mucho FLEXID3_SCAN_MAX bytes de marcos.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_Media.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEXID3_SCAN_MAX (512u * 1024u)
#define FLEXID3_TEXT_MAX 64

enum { FLEXID3_OK = 0, FLEXID3_NONE = 1, FLEXID3_ERR_IO = -1, FLEXID3_ERR_FORMAT = -2 };
enum { FLEXID3_PIC_NONE = 0, FLEXID3_PIC_JPEG, FLEXID3_PIC_PNG, FLEXID3_PIC_OTHER };

typedef struct {
    uint8_t version;                 /**< 2, 3 o 4 */
    uint32_t tagBytes;               /**< tamaño total con cabecera y pie */
    char title[FLEXID3_TEXT_MAX];
    char artist[FLEXID3_TEXT_MAX];
    char album[FLEXID3_TEXT_MAX];
    uint8_t picKind;                 /**< FLEXID3_PIC_* */
    uint32_t picOffset;              /**< primer byte de la imagen en el archivo */
    uint32_t picBytes;
} FlexId3Info;

/** FLEXID3_OK, FLEXID3_NONE (no hay etiqueta) o un error. */
int flexId3Read(const FlexMediaIO *io, FlexId3Info *out);

/** Texto de un marco (codificación en enc) -> UTF-8. Expuesto para las pruebas. */
size_t flexId3Text(uint8_t enc, const uint8_t *s, size_t n, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
