/*
 * wm_text — lo de watch_media que no toca el hardware: raíces y nombres de
 * los volúmenes, rutas, y los textos para la persona. Se compila igual en el
 * reloj y en la vista previa de PC (tools/sim), así que las pantallas dicen
 * exactamente lo mismo en los dos.
 */
#include "watch_media.h"

#include <stdio.h>
#include <string.h>

#include "FlexOS_MediaLib.h"
#include "watch_storage.h"
#include "wm_path.h"

const char *wm_vol_root(wm_vol_t v)
{
    return v == WM_VOL_SD ? WM_SD_BASE : WATCH_STORAGE_BASE;
}

const char *wm_vol_name(wm_vol_t v)
{
    return v == WM_VOL_SD ? "microSD" : "Reloj";
}

bool wm_vol_path(wm_vol_t v, const char *rel, char *out, size_t n)
{
    return v < WM_VOL_N && wm_path_join(wm_vol_root(v), rel, out, n);
}

const char *wm_dest_text(wm_dest_status_t st)
{
    switch (st) {
    case WM_DEST_OK:              return "";
    case WM_DEST_SD_ABSENT:       return "Has elegido guardar en la microSD y no hay tarjeta. Insértala o elige el reloj.";
    case WM_DEST_SD_NOT_READY:    return "La microSD no está lista. Revisa Ajustes > Almacenamiento o elige el reloj.";
    case WM_DEST_WATCH_NOT_READY: return "La memoria del reloj no está disponible (Ajustes > Almacenamiento).";
    default:                      return "?";
    }
}

void wm_sd_describe(const wm_sd_info_t *i, char *out, size_t n)
{
    char a[16], b[16];
    switch (i->state) {
    case WSD_READY:
        if (i->space_valid) {
            wm_fmt_bytes(i->free_bytes, a, sizeof(a));
            wm_fmt_bytes(i->total_bytes, b, sizeof(b));
            snprintf(out, n, "Lista · %s libres de %s", a, b);
        } else {
            snprintf(out, n, "Lista");
        }
        break;
    case WSD_ABSENT:
        snprintf(out, n, "%s", i->ejected ? "Expulsada: ya puedes sacarla" : "Sin tarjeta");
        break;
    case WSD_ERROR:
        snprintf(out, n, "%s", wsd_err_text(i->err));
        break;
    case WSD_REMOVING:
        snprintf(out, n, "%s", i->err == WSD_E_HELD ? wsd_err_text(i->err) : "Desmontando…");
        break;
    case WSD_MOUNTING:
        snprintf(out, n, "Montando… (calculando el espacio libre)");
        break;
    case WSD_BUSY:
        snprintf(out, n, "Formateando… no saques la tarjeta");
        break;
    default:
        snprintf(out, n, "%s…", wsd_state_name(i->state));
        break;
    }
}

int wm_big_add(wm_big_t *list, int n, int cap, const wm_big_t *it)
{
    if (!list || !it || cap <= 0) {
        return n;
    }
    int at = n;
    while (at > 0 && list[at - 1].size < it->size) {
        at--; /* los iguales se quedan delante: orden estable */
    }
    if (at >= cap) {
        return n; /* más pequeño que todos los que caben */
    }
    const int last = n < cap ? n : cap - 1; /* el último que se conserva tras mover */
    memmove(&list[at + 1], &list[at], (size_t)(last - at) * sizeof(*list));
    list[at] = *it;
    return n < cap ? n + 1 : cap;
}

const char *wm_big_kind_text(const wm_big_t *b)
{
    switch (b->kind) {
    case FML_K_AUDIO: return b->rec ? "Grabación" : "Música";
    case FML_K_VIDEO: return "Vídeo";
    case FML_K_PHOTO: return "Foto";
    case FML_K_DRAW:  return "Dibujo";
    default:          return "Archivo";
    }
}
