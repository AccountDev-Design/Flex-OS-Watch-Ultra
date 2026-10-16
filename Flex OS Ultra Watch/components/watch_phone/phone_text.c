/*
 * phone_text — textos del estado de watch_phone (sin ESP-IDF: el simulador
 * de PC los usa tal cual).
 */
#include "watch_phone.h"

const char *watch_phone_state_text(const watch_phone_status_t *s)
{
    if (!s) {
        return "";
    }
    switch (s->link) {
    case FLP_LS_UNAVAILABLE:
        return "No disponible";
    case FLP_LS_OFF:
        return "Apagado";
    case FLP_LS_SEARCHING:
        /* El canal sabe más que el enlace mientras busca */
        return s->chan == FLP_TC_OPENING ? "Conectando por Bluetooth" : "Buscando el teléfono";
    case FLP_LS_CONNECTING:
        return "Conectado sin sesión";
    case FLP_LS_PAIRING:
        return "Emparejando";
    case FLP_LS_AUTH:
        return "Autenticando";
    case FLP_LS_READY:
        return "Conectado";
    default:
        return "Error";
    }
}
