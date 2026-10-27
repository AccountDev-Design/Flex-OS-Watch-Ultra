/*
 * wm_path — lógica PURA de rutas (se prueba en el PC): validar una ruta
 * relativa a un volumen y unirla a su raíz sin salirse de ella.
 *
 * Una ruta relativa válida:
 *  - empieza por '/', no termina en '/' (salvo "/"), sin "//";
 *  - ningún componente es "." ni "..";
 *  - sin '\\', ':', '*', '?', '"', '<', '>', '|' ni caracteres de control
 *    (lo que FAT no admite y lo que serviría para escaparse);
 *  - UTF-8 bien formado (los nombres con tildes y eñes valen);
 *  - cabe con la raíz en el búfer de salida.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wm_path_safe(const char *rel);
/** root + rel en out. false (y out vacío) si rel no es segura o no cabe. */
bool wm_path_join(const char *root, const char *rel, char *out, size_t n);

#ifdef __cplusplus
}
#endif
