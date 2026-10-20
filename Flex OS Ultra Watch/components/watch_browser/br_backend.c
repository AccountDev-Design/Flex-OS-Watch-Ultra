/*
 * br_backend — ver include/br_backend.h
 */
#include "br_backend.h"

#include <stdio.h>
#include <string.h>

/* Los mismos valores que FLP_RELAY_* (flex_phone.h): aquí no se incluye
 * flexlink para que el navegador siga sin depender del enlace. */
enum { RL_OFF = 0, RL_STARTING, RL_UP, RL_ERROR, RL_SUSPENDED };

static bool phone_up(const br_phone_view_t *ph)
{
    return ph && ph->link_ready && ph->relay_state == RL_UP && ph->port &&
           (ph->ip[0] | ph->ip[1] | ph->ip[2] | ph->ip[3]) && ph->token[0];
}

static void phone_why(const br_phone_view_t *ph, char *out, size_t n)
{
    if (!ph || !ph->token[0]) {
        snprintf(out, n, "Empareja Flex Phone para usar su conexión");
    } else if (!ph->link_ready) {
        snprintf(out, n, "Flex Phone no está conectado por Bluetooth");
    } else if (!ph->relay_supported) {
        snprintf(out, n, "La app Flex Phone no ofrece el navegador");
    } else {
        switch (ph->relay_state) {
        case RL_STARTING:
            snprintf(out, n, "Pidiendo al teléfono su servidor del navegador…");
            break;
        case RL_ERROR:
            snprintf(out, n, "Flex Phone: %s", ph->relay_err[0] ? ph->relay_err : "el servidor no pudo arrancar");
            break;
        case RL_SUSPENDED:
            snprintf(out, n, "Android suspendió el servidor de Flex Phone");
            break;
        case RL_UP:
            snprintf(out, n, "Flex Phone no dio una dirección válida");
            break;
        default:
            snprintf(out, n, "%s", ph->relay_by_user ? "Servidor del navegador detenido en el teléfono"
                                                     : "El servidor del navegador del teléfono está parado");
            break;
        }
    }
}

void br_backend_resolve(const BrSettings *st, const br_phone_view_t *ph, br_backend_t *out)
{
    memset(out, 0, sizeof(*out));
    out->src = BRSRC_NONE;
    if (!st) {
        snprintf(out->why, sizeof(out->why), "Sin ajustes de navegador");
        return;
    }
    const bool up = phone_up(ph);
    const bool cloud = st->cloudServer[0] != 0;
    const bool manual = st->server[0] != 0;
    out->src = flexBrSourceResolve(st, up, cloud, manual);
    /* Se pide el relay si el teléfono puede ser la fuente y aún no lo es,
     * salvo que la persona lo parase (eso no se deshace solo). */
    const bool phone_wanted = st->source == BRSRC_AUTO || st->source == BRSRC_PHONE;
    out->want_relay = phone_wanted && ph && ph->link_ready && ph->relay_supported && ph->token[0] &&
                      !ph->relay_by_user && (ph->relay_state == RL_OFF || ph->relay_state == RL_ERROR);
    switch (out->src) {
    case BRSRC_PHONE:
        snprintf(out->url, sizeof(out->url), "%s://%u.%u.%u.%u:%u/v1/session", ph->tls ? "wss" : "ws", ph->ip[0],
                 ph->ip[1], ph->ip[2], ph->ip[3], (unsigned)ph->port);
        snprintf(out->token, sizeof(out->token), "%s", ph->token);
        out->insecure_ok = true; /* red local; la credencial va en el HELLO */
        break;
    case BRSRC_CLOUD:
        snprintf(out->url, sizeof(out->url), "%s", st->cloudServer);
        snprintf(out->token, sizeof(out->token), "%s", st->token);
        break;
    case BRSRC_MANUAL:
        snprintf(out->url, sizeof(out->url), "%s", st->server);
        snprintf(out->token, sizeof(out->token), "%s", st->token);
        break;
    default:
        if (st->source == BRSRC_PHONE || (st->source == BRSRC_AUTO && ph && ph->token[0])) {
            phone_why(ph, out->why, sizeof(out->why));
        } else if (st->source == BRSRC_AUTO) {
            snprintf(out->why, sizeof(out->why), "Empareja Flex Phone o configura un servidor en Ajustes");
        } else {
            snprintf(out->why, sizeof(out->why), "%s", flexBrSourceWhyNone(st, false, cloud, manual));
        }
        break;
    }
}

bool br_backend_differs(const br_backend_t *a, const br_backend_t *b)
{
    return a->src != b->src || a->insecure_ok != b->insecure_ok || strcmp(a->url, b->url) != 0 ||
           strcmp(a->token, b->token) != 0 || strcmp(a->why, b->why) != 0;
}
