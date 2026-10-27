/*
 * wm_sd_logic — ver include/wm_sd_logic.h
 */
#include "wm_sd_logic.h"

#include <string.h>

static bool due(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

static void set_state(wsd_t *s, wsd_state_t st, wsd_err_t err, uint32_t now)
{
    if (s->st != st || s->err != err) {
        s->st = st;
        s->err = err;
        s->since_ms = now;
        s->gen++;
    }
}

/* Resultado de un sondeo: gen solo cambia si el resultado es distinto de lo
 * que se enseñaba antes de sondear. */
static void settle(wsd_t *s, wsd_state_t st, wsd_err_t err, uint32_t now)
{
    if (s->prev_st != st || s->err != err) {
        s->since_ms = now;
        s->gen++;
    }
    s->st = st;
    s->err = err;
}

static void begin_probe(wsd_t *s)
{
    s->prev_st = s->st;
    s->st = WSD_DETECTING;
}

static uint32_t probe_ms(const wsd_t *s)
{
    return s->interest ? WSD_PROBE_MS_ACTIVE : WSD_PROBE_MS_IDLE;
}

static uint32_t status_ms(const wsd_t *s)
{
    return s->interest ? WSD_STATUS_MS_ACTIVE : WSD_STATUS_MS_IDLE;
}

static bool fs_error(wsd_err_t e)
{
    return e == WSD_E_NOFS || e == WSD_E_EXFAT || e == WSD_E_NTFS || e == WSD_E_GPT || e == WSD_E_FS_DAMAGED ||
           e == WSD_E_FORMAT;
}

void wsd_init(wsd_t *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->st = WSD_ABSENT;
    s->since_ms = now;
    s->next_ms = now; /* primera comprobación al arrancar */
    s->now_pending = true;
    s->gen = 1;
}

wsd_action_t wsd_next(wsd_t *s, uint32_t now)
{
    const bool time = s->now_pending || due(now, s->next_ms);
    switch (s->st) {
    case WSD_ABSENT:
        if (s->card_up) {
            return WSD_DO_RELEASE; /* la expulsada que sigue puesta: se apaga */
        }
        if (!time) {
            return WSD_DO_WAIT;
        }
        s->now_pending = false;
        /* Sondeo rutinario: el estado no "parpadea" (gen no cambia) si no
         * aparece nada. */
        begin_probe(s);
        return WSD_DO_PROBE;
    case WSD_DETECTING:
        return WSD_DO_PROBE;
    case WSD_MOUNTING:
        return WSD_DO_MOUNT;
    case WSD_READY:
        if (!time) {
            return WSD_DO_WAIT;
        }
        s->now_pending = false;
        return WSD_DO_STATUS;
    case WSD_BUSY:
        return WSD_DO_FORMAT;
    case WSD_REMOVING:
        if (s->fs_up) {
            if (s->leases == 0) {
                return WSD_DO_UNMOUNT;
            }
            if (s->err != WSD_E_HELD && due(now, s->since_ms + WSD_HELD_MS)) {
                s->err = WSD_E_HELD; /* sigue en REMOVING: solo cambia el motivo */
                s->gen++;
            }
            s->now_pending = false;
            return WSD_DO_WAIT;
        }
        if (s->want_format && !s->surprise && s->card_up) {
            s->want_format = false;
            set_state(s, WSD_BUSY, WSD_E_NONE, now);
            return WSD_DO_FORMAT;
        }
        if (s->card_up) {
            return WSD_DO_RELEASE;
        }
        wsd_on_released(s, now); /* no había nada encendido */
        return WSD_DO_WAIT;
    case WSD_ERROR:
        if (!time) {
            return WSD_DO_WAIT;
        }
        s->now_pending = false;
        if (s->card_up) {
            return WSD_DO_STATUS; /* ¿sigue puesta? */
        }
        begin_probe(s);           /* sin inicializar: se vuelve a sondear */
        return WSD_DO_PROBE;
    default:
        return WSD_DO_WAIT;
    }
}

void wsd_on_probe(wsd_t *s, wsd_probe_t r, uint32_t serial, uint32_t now)
{
    s->next_ms = now + probe_ms(s);
    switch (r) {
    case WSD_PROBE_OK:
        s->card_up = true;
        s->serial = serial;
        s->status_fails = 0;
        if (s->ejected && serial == s->eject_serial) {
            settle(s, WSD_ABSENT, WSD_E_NONE, now); /* la misma que se expulsó: fuera hasta retirarla */
            return;                                  /* wsd_next() la apaga */
        }
        if (s->ejected) {
            s->ejected = false; /* otra tarjeta */
        }
        settle(s, WSD_MOUNTING, WSD_E_NONE, now);
        break;
    case WSD_PROBE_BAD:
    case WSD_PROBE_UNSUPPORTED:
        s->card_up = false;
        s->ejected = false;
        settle(s, WSD_ERROR, r == WSD_PROBE_BAD ? WSD_E_CARD : WSD_E_UNSUPPORTED, now);
        break;
    case WSD_PROBE_NOCARD:
    default:
        s->card_up = false;
        if (s->ejected) {
            s->ejected = false; /* ya la han sacado */
            s->gen++;
        }
        settle(s, WSD_ABSENT, WSD_E_NONE, now);
        break;
    }
}

void wsd_on_mount(wsd_t *s, wsd_err_t err, uint32_t now)
{
    s->status_fails = 0;
    s->next_ms = now + status_ms(s);
    if (err == WSD_E_NONE) {
        s->fs_up = true;
        set_state(s, WSD_READY, WSD_E_NONE, now);
    } else {
        s->fs_up = false;
        set_state(s, WSD_ERROR, err, now);
    }
}

void wsd_on_status(wsd_t *s, bool ok, uint32_t now)
{
    if (ok) {
        s->status_fails = 0;
        s->next_ms = now + status_ms(s);
        return;
    }
    if (++s->status_fails < WSD_STATUS_FAILS) {
        s->next_ms = now + WSD_STATUS_RETRY_MS;
        return;
    }
    /* Retirada: se desmonta en cuanto nadie la use. */
    s->status_fails = 0;
    s->surprise = true;
    s->want_eject = false;
    s->want_format = false;
    set_state(s, WSD_REMOVING, WSD_E_NONE, now);
    s->now_pending = true;
}

void wsd_on_unmounted(wsd_t *s, uint32_t now)
{
    (void)now;
    s->fs_up = false;
    s->now_pending = true;
}

void wsd_on_format(wsd_t *s, bool ok, uint32_t now)
{
    s->want_format = false;
    if (ok) {
        set_state(s, WSD_MOUNTING, WSD_E_NONE, now);
    } else {
        s->next_ms = now + status_ms(s);
        set_state(s, WSD_ERROR, WSD_E_FORMAT, now);
    }
}

void wsd_on_released(wsd_t *s, uint32_t now)
{
    const bool removing = s->st == WSD_REMOVING;
    s->card_up = false;
    s->fs_up = false;
    s->status_fails = 0;
    s->next_ms = now + probe_ms(s);
    if (!removing) {
        return; /* la expulsada que seguía puesta, o un reintento: sin cambio visible */
    }
    if (s->want_eject && !s->surprise) {
        s->ejected = true;
        s->eject_serial = s->serial;
    }
    const bool retry = !s->want_eject && !s->surprise; /* soltada para reintentar */
    s->want_eject = false;
    s->want_format = false;
    s->surprise = false;
    set_state(s, WSD_ABSENT, WSD_E_NONE, now);
    s->gen++; /* "expulsada" / "retirada" también es un cambio visible */
    if (retry) {
        s->now_pending = true;
    }
}

bool wsd_lease(wsd_t *s)
{
    if (s->st != WSD_READY) {
        return false;
    }
    s->leases++;
    return true;
}

void wsd_unlease(wsd_t *s)
{
    if (s->leases > 0) {
        s->leases--;
    }
    if (s->st == WSD_REMOVING && s->leases == 0) {
        s->now_pending = true;
    }
}

bool wsd_request_eject(wsd_t *s, uint32_t now)
{
    if (s->st == WSD_READY || (s->st == WSD_ERROR && s->card_up)) {
        s->want_eject = true;
        s->want_format = false;
        s->surprise = false;
        set_state(s, WSD_REMOVING, WSD_E_NONE, now);
        s->now_pending = true;
        return true;
    }
    return false;
}

bool wsd_can_format(const wsd_t *s)
{
    return s->st == WSD_READY || (s->st == WSD_ERROR && s->card_up && fs_error(s->err));
}

bool wsd_request_format(wsd_t *s, uint32_t now)
{
    if (!wsd_can_format(s)) {
        return false;
    }
    if (s->st == WSD_READY) {
        s->want_format = true;
        s->want_eject = false;
        s->surprise = false;
        set_state(s, WSD_REMOVING, WSD_E_NONE, now);
    } else {
        set_state(s, WSD_BUSY, WSD_E_NONE, now);
    }
    s->now_pending = true;
    return true;
}

bool wsd_request_retry(wsd_t *s, uint32_t now)
{
    if (s->st == WSD_ERROR) {
        if (s->card_up) {
            s->want_eject = false;
            s->want_format = false;
            s->surprise = false;
            set_state(s, WSD_REMOVING, WSD_E_NONE, now); /* se suelta y se vuelve a sondear */
        } else {
            set_state(s, WSD_ABSENT, WSD_E_NONE, now);
        }
        s->now_pending = true;
        return true;
    }
    if (s->st == WSD_ABSENT && s->ejected) {
        s->ejected = false;
        s->gen++;
        s->now_pending = true;
        return true;
    }
    return false;
}

void wsd_io_error(wsd_t *s, uint32_t now)
{
    (void)now;
    if (s->st == WSD_READY) {
        s->now_pending = true;
    }
}

void wsd_interest(wsd_t *s, bool on, uint32_t now)
{
    if (on) {
        s->interest++;
        /* Quien abre una pantalla de medios quiere el estado real ya. */
        if (s->st == WSD_ABSENT || s->st == WSD_READY || s->st == WSD_ERROR) {
            s->now_pending = true;
        }
    } else if (s->interest > 0) {
        s->interest--;
    }
    (void)now;
}

wsd_state_t wsd_visible(const wsd_t *s)
{
    return s->st == WSD_DETECTING && s->prev_st == WSD_ABSENT ? WSD_ABSENT : s->st;
}

const char *wsd_state_name(wsd_state_t st)
{
    switch (st) {
    case WSD_ABSENT:    return "Sin tarjeta";
    case WSD_DETECTING: return "Detectando";
    case WSD_MOUNTING:  return "Montando";
    case WSD_READY:     return "Lista";
    case WSD_BUSY:      return "Formateando";
    case WSD_ERROR:     return "Error";
    case WSD_REMOVING:  return "Desmontando";
    default:            return "?";
    }
}

const char *wsd_err_text(wsd_err_t e)
{
    switch (e) {
    case WSD_E_NONE:        return "";
    case WSD_E_CARD:        return "La tarjeta no responde bien. Sácala, límpiala e insértala de nuevo.";
    case WSD_E_UNSUPPORTED: return "Tarjeta no compatible: el reloj usa microSD, SDHC y SDXC de hasta 2 TB.";
    case WSD_E_NOFS:        return "La tarjeta no tiene un formato reconocible.";
    case WSD_E_EXFAT:       return "Formato exFAT: el reloj lee FAT32. Puedes formatearla en FAT32 (borra todo).";
    case WSD_E_NTFS:        return "Formato NTFS: el reloj lee FAT32. Puedes formatearla en FAT32 (borra todo).";
    case WSD_E_GPT:         return "Tabla de particiones GPT: el reloj necesita MBR con FAT32.";
    case WSD_E_FS_DAMAGED:  return "El sistema de archivos está dañado. Repáralo en un ordenador o formatéala.";
    case WSD_E_IO:          return "Error de lectura al montar la tarjeta.";
    case WSD_E_NOMEM:       return "No hay memoria para montar la tarjeta ahora.";
    case WSD_E_FORMAT:      return "El formateo no terminó. La tarjeta puede estar dañada.";
    case WSD_E_HELD:        return "Una app sigue usando la tarjeta. Ciérrala para terminar.";
    default:                return "?";
    }
}
