/*
 * br_phone_view — lo que el navegador lee de watch_phone para elegir fuente.
 * Solo lo incluyen watch_browser.c y la vista previa (sim_br.c): así el
 * resolvedor (br_backend) sigue sin depender del enlace.
 */
#pragma once

#include <string.h>
#include "br_backend.h"
#include "watch_phone.h"

/** Rellena v con el estado REAL del enlace y del relay del teléfono. ps: espacio de trabajo. */
static inline void br_phone_view_get(br_phone_view_t *v, watch_phone_status_t *ps)
{
    memset(v, 0, sizeof(*v));
    watch_phone_get(ps);
    v->link_ready = ps->link == FLP_LS_READY;
    v->relay_supported = v->link_ready && (!ps->caps.valid || (ps->caps.supported & FLP_CAP_RELAY));
    v->relay_state = ps->relay.state;
    v->relay_by_user = ps->relay.byUser;
    memcpy(v->ip, ps->relay.ip, sizeof(v->ip));
    v->port = ps->relay.port;
    v->tls = ps->relay.tls;
    memcpy(v->relay_err, ps->relay.err, sizeof(v->relay_err));
    v->relay_err[sizeof(v->relay_err) - 1] = 0;
    if (ps->bonded) {
        watch_phone_relay_token(v->token, sizeof(v->token));
    }
}
