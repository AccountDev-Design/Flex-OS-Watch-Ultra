/*
 * net_text — textos del estado del Wi-Fi (compartidos con la vista previa).
 */
#include "watch_net.h"

const char *watch_net_state_text(const watch_net_status_t *s)
{
    switch (s->state) {
    case NET_OFF:
        return "Apagado";
    case NET_IDLE:
        return "Sin conexión";
    case NET_SCANNING:
        return "Buscando redes";
    case NET_CONNECTING:
        return "Conectando";
    case NET_CONNECTED:
        return "Conectado";
    case NET_FAILED:
        return s->reason ? net_reason_text(s->reason) : "Sin conexión";
    default:
        return "";
    }
}
