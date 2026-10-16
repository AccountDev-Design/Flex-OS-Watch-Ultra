/*
 * flexfs — archivos del usuario en LittleFS, con la semántica de
 * FlexOS_FS de Flex OS Ultra (FlexOS_FS.h/.cpp):
 *  - carpetas fijas /Notas, /Documentos, /Paint y /Papelera;
 *  - la papelera guarda la ruta original en el propio nombre ('/' -> '@'),
 *    sin índice aparte que se pueda desincronizar (FS.cpp:451-469);
 *  - nombres nuevos "Sin título 1.txt", "Sin título 2.txt"...;
 *  - renombrar conserva la extensión y nunca pisa otro archivo;
 *  - el conteo de una carpeta se obtiene abriéndola, no de una tabla.
 *
 * Se apoya solo en POSIX (opendir/stat/rename/unlink...), que ESP-IDF da
 * sobre LittleFS en /storage (watch_storage) y Linux en el PC: el mismo
 * código se prueba en tests/host sobre una carpeta temporal y en la vista
 * previa. Las rutas que maneja la interfaz son "virtuales" ("/Notas/x.txt")
 * y se cuelgan de la raíz (ffs_set_root).
 *
 * No es seguro entre tareas por sí solo: lo usa la tarea de LVGL (las notas
 * son pequeñas; ver docs/F9-APPS.md sobre el coste de escribir en flash).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "files_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FFS_DIR_NOTAS "/Notas"
#define FFS_DIR_DOCS  "/Documentos"
#define FFS_DIR_PAINT "/Paint"
#define FFS_DIR_TRASH "/Papelera"
#define FFS_EXT_NOTE  ".txt"
#define FFS_TEXT_MAX  4096 /* tope de una nota (con el 0 final) */
#define FFS_LIST_MAX  32   /* elementos que se listan de una carpeta */

typedef struct {
    char name[FL_NAME_MAX];
    bool dir;
    uint32_t size;  /* bytes (carpeta: suma de lo que contiene, un nivel) */
    uint16_t items; /* elementos de una carpeta */
} ffs_entry_t;

/** Carpeta real donde vive "/" (por defecto WATCH_STORAGE_BASE). */
void ffs_set_root(const char *root);
const char *ffs_root(void);
/** ¿Está montado y se puede usar? */
bool ffs_ready(void);
/** Crea las carpetas fijas si faltan. */
void ffs_init_dirs(void);

/** Lista una carpeta: carpetas primero y luego por nombre. Devuelve cuántas (<0 = no existe). */
int ffs_list(const char *dir, ffs_entry_t *out, int max);
int ffs_count(const char *dir);
bool ffs_exists(const char *path);
bool ffs_is_dir(const char *path);
bool ffs_mkdir(const char *path);
/** Borra para siempre (una carpeta, con todo lo que tenga). */
bool ffs_delete(const char *path);
/** Renombra en su carpeta; sin extensión nueva conserva la vieja. new_path puede ser NULL. */
bool ffs_rename(const char *path, const char *new_name, char *new_path, size_t n);
/** A la papelera, con la ruta original codificada en el nombre. */
bool ffs_trash(const char *path);
/** Devuelve un elemento de la papelera a su sitio (numera si ya hay otro). */
bool ffs_restore(const char *trash_name);
bool ffs_empty_trash(void);
/** Primer nombre libre "base N ext" en dir. */
bool ffs_new_name(const char *dir, const char *base, const char *ext, char *out, size_t n);
/** Lee texto (siempre termina en 0). Devuelve bytes o -1. */
int ffs_read_text(const char *path, char *out, size_t n);
/** Escribe texto de forma atómica (temporal + renombrado): un corte no deja medio archivo. */
bool ffs_write_text(const char *path, const char *txt);

#ifdef __cplusplus
}
#endif
