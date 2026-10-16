/*
 * files_logic — nombres y rutas de flexfs, sin tocar el disco (se prueba
 * entero en tests/host). Mismas reglas que FlexOS_FS.cpp de la referencia.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_PATH_MAX 96 /* ruta virtual completa ("/Notas/Lista 1.txt") */
#define FL_NAME_MAX 48 /* nombre suelto */

/** Último componente de la ruta. */
const char *fl_base(const char *path);
/** Extensión con el punto (".txt") o "" (un nombre que empieza por punto no la tiene). */
const char *fl_ext(const char *name);
/** Nombre sin extensión ni carpeta. */
void fl_stem(const char *name, char *out, size_t n);
/** Carpeta de la ruta ("/Notas/a.txt" -> "/Notas", "/a" -> "/"). */
void fl_parent(const char *path, char *out, size_t n);
/** dir + "/" + name sin barras dobles. false si no cabe. */
bool fl_join(const char *dir, const char *name, char *out, size_t n);
/**
 * ¿Sirve como nombre? No vacío, sin '/', '\\' ni '@' (la papelera usa '@'
 * para codificar la ruta: RELOJ, la referencia no lo impedía y restaurar
 * un nombre con '@' lo mandaba a una carpeta que no existe), sin espacios
 * al principio o al final, no "." ni "..", y más corto que FL_NAME_MAX.
 */
bool fl_name_ok(const char *name);
/** Nombre en la papelera para una ruta ('/' -> '@'). false si no cabe. */
bool fl_trash_encode(const char *path, char *out, size_t n);
/** Ruta original de un nombre de la papelera ('@' -> '/'); quita el "@@N" de una colisión. false si no es válido. */
bool fl_trash_origin(const char *trash_name, char *out, size_t n);
/** "512 B", "12 KB", "3,4 MB" (coma decimal). */
void fl_fmt_size(uint32_t bytes, char *out, size_t n);
/** Quita una secuencia UTF-8 incompleta al final (lectura cortada por tamaño). */
void fl_utf8_trim(char *s);
/** Orden de lista: carpetas primero, luego por nombre sin distinguir mayúsculas (ASCII). */
int fl_cmp(bool dir_a, const char *a, bool dir_b, const char *b);

#ifdef __cplusplus
}
#endif
