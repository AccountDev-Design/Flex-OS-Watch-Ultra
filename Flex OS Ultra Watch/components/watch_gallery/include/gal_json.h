/*
 * gal_json — lector JSON de la Galería: no reserva memoria ni construye un
 * árbol; lee en sitio sobre el búfer de la respuesta.
 *
 * A diferencia del escáner del Clima (wx_j_find busca la clave en cualquier
 * profundidad), aquí una clave solo casa en el PRIMER nivel del objeto que se
 * le pasa: el "id" de un archivo no se confunde con el "id" de su carpeta ni
 * el "state" de la cuota con el de una subida. Lo que viene de la red no se da
 * por bueno: una cadena sin cerrar, un escape inválido, un número que no cabe
 * o un objeto truncado hacen fallar la lectura (nunca se lee fuera de [p, e)).
 *
 * C puro: lo usan el reloj, tests/host y la vista previa.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Salta espacios. */
const char *gal_j_ws(const char *p, const char *e);
/** Salta un valor completo (objeto, lista, cadena, número, literal). NULL si está mal formado. */
const char *gal_j_skip(const char *p, const char *e);
/** ¿[p, e) es un único valor JSON completo (con espacios alrededor)? */
bool gal_j_valid(const char *p, const char *e);
/**
 * Valor de la clave key en el PRIMER nivel del objeto que empieza en obj
 * ('{'). Devuelve el principio del valor (sin espacios) o NULL. *ve recibe
 * su final (puede ser NULL).
 */
const char *gal_j_get(const char *obj, const char *e, const char *key, const char **ve);
/** Igual, pero el valor tiene que ser un objeto o una lista: [*os, *oe). */
bool gal_j_obj(const char *obj, const char *e, const char *key, const char **os, const char **oe);
/** Primer elemento de la lista que empieza en arr ('['). NULL si está vacía o mal formada. */
const char *gal_j_first(const char *arr, const char *e);
/** Elemento siguiente a it (que empieza en it). NULL al final o si está mal formada. */
const char *gal_j_next(const char *it, const char *e);
/**
 * Cadena en p (con las comillas) a UTF-8, con los escapes resueltos
 * (\uXXXX y los pares sustitutos incluidos). Recorta sin partir un carácter
 * si no cabe y devuelve false si no es una cadena válida. out siempre acaba en 0.
 */
bool gal_j_str(const char *p, const char *e, char *out, size_t n);
/** Atajos sobre una clave del objeto obj. */
bool gal_j_get_str(const char *obj, const char *e, const char *key, char *out, size_t n);
bool gal_j_get_u64(const char *obj, const char *e, const char *key, uint64_t *out);
bool gal_j_get_i64(const char *obj, const char *e, const char *key, int64_t *out);
bool gal_j_get_bool(const char *obj, const char *e, const char *key, bool *out);
/** Número entero sin signo en p (sin fracción ni exponente). */
bool gal_j_u64(const char *p, const char *e, uint64_t *out);
bool gal_j_i64(const char *p, const char *e, int64_t *out);
/** true/false, o 0/1 (el servidor del P4 usa números como booleanos). */
bool gal_j_bool(const char *p, const char *e, bool *out);
bool gal_j_is_null(const char *p, const char *e);

/** Escribe s como cadena JSON (con comillas) en out. false si no cabe. */
bool gal_j_quote(const char *s, char *out, size_t n);

#ifdef __cplusplus
}
#endif
