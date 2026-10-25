/*
 * stg_usage — cuánto ocupa cada categoría del almacenamiento del usuario, con
 * POSIX (opendir/stat) sobre la raíz real de LittleFS (en el reloj
 * WATCH_STORAGE_BASE; en el PC, una carpeta temporal: tests/host).
 *
 * Categorías de Ajustes > Almacenamiento (diseño del usuario):
 *  - Documentos: lo que crea el usuario (/Documentos, /Notas, /Paint, /Imagenes);
 *  - Papelera: /Papelera;
 *  - Aplicaciones: el resto de lo ocupado en LittleFS (datos de las apps, como
 *    las miniaturas de la Galería en /System, y los metadatos del sistema de
 *    archivos). Se calcula con el "usado" real de LittleFS, no se inventa.
 * Se recorre una sola vez a petición (al abrir la página), en la tarea del
 * almacenamiento, con un tope de archivos para no bloquear: si se alcanza,
 * truncated lo dice.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STG_DU_MAX_FILES 4000
#define STG_DU_MAX_DEPTH 8

typedef struct {
    uint64_t docs;     /**< bytes de los archivos del usuario */
    uint64_t trash;    /**< bytes de la papelera */
    uint32_t files;    /**< archivos contados */
    bool truncated;    /**< se llegó al tope de archivos o de profundidad */
} stg_usage_t;

/** Recorre root (la raíz real de flexfs) y rellena out. */
void stg_usage_scan(const char *root, stg_usage_t *out);

/** Bytes que ocupa dir (recursivo). files y truncated se acumulan. */
uint64_t stg_du(const char *dir, uint32_t *files, bool *truncated);

#ifdef __cplusplus
}
#endif
