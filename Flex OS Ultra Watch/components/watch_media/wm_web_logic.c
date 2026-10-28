/*
 * wm_web_logic — lo de wm_web que no toca sockets ni tareas: la dirección
 * del QR, lo que /api/hello cuenta del reloj y cuándo apagar el servidor.
 * Ver include/wm_web.h; se prueba en tests/host (test_media.c).
 */
#include "wm_web.h"

#include <stdio.h>
#include <string.h>

bool wweb_url(char *out, size_t n, const char *ip, uint16_t port, const char *code)
{
    if (!out || !n || !ip || !ip[0] || !code || strlen(code) != 6) {
        if (out && n) {
            out[0] = 0;
        }
        return false;
    }
    const int k = snprintf(out, n, "http://%s:%u/?k=%s", ip, (unsigned)port, code);
    if (k < 0 || (size_t)k >= n) {
        out[0] = 0;
        return false;
    }
    return true;
}

bool wweb_dest_json(char *out, size_t n, wm_vol_t v, bool ready, uint64_t free_b)
{
    const int k = snprintf(out, n, "\"vol\":\"%s\",\"name\":\"%s\",\"ready\":%d,\"free\":%llu",
                           v == WM_VOL_SD ? "sd" : "watch", wm_vol_name(v), ready ? 1 : 0,
                           (unsigned long long)(ready ? free_b : 0));
    if (k < 0 || (size_t)k >= n) {
        if (n) {
            out[0] = 0;
        }
        return false;
    }
    return true;
}

const char *wweb_media_json(void)
{
    /* La pantalla es de 410 x 502: fotos con margen para el zoom x2 del
     * visor, y vídeo MJPEG que cabe a 1:1 (lado largo <= 400 px: sin escalar
     * en cada fotograma) y que el reloj decodifica a tiempo (TJpgDec). */
    return "\"photo\":{\"light\":{\"max\":1024},\"rec\":{\"max\":1280},\"high\":{\"max\":1600}},"
           "\"video\":{\"light\":{\"long\":320,\"fps\":10},\"rec\":{\"long\":360,\"fps\":12},"
           "\"high\":{\"long\":400,\"fps\":15}}";
}

uint32_t wweb_code_left_s(uint32_t now_ms, uint32_t born_ms, uint32_t ttl_ms)
{
    const uint32_t age = now_ms - born_ms;
    if (!ttl_ms) {
        return UINT32_MAX;
    }
    return age >= ttl_ms ? 0 : (ttl_ms - age + 999u) / 1000u;
}

bool wweb_should_stop(const wweb_life_t *l)
{
    if (l->busy) {
        return false; /* lo que está en marcha termina */
    }
    if (!l->wanted || l->since_keep >= WWEB_KEEP_MS) {
        return true;
    }
    return l->idle_ms >= WWEB_IDLE_STOP_MS;
}

const char *wweb_state_text(wweb_state_t s)
{
    switch (s) {
    case WWEB_OFF:      return "Apagado";
    case WWEB_STARTING: return "Preparando…";
    case WWEB_NO_WIFI:  return "Esperando a la Wi-Fi";
    case WWEB_READY:    return "Listo para recibir";
    case WWEB_ERROR:    return "No se pudo abrir el servidor";
    default:            return "?";
    }
}

void wweb_dev_id(const uint8_t mac[6], char *out, size_t n)
{
    snprintf(out, n, "S3-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
