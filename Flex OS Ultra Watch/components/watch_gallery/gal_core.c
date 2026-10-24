/*
 * gal_core — ver include/gal_core.h.
 */
#include "gal_core.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "files_logic.h"
#include "gal_http.h"
#include "gal_json.h"

void gal_copyz(char *d, size_t n, const char *s)
{
    if (!n) {
        return;
    }
    size_t l = 0;
    while (s && l + 1 < n && s[l]) {
        l++;
    }
    while (l && s[l] && ((unsigned char)s[l] & 0xC0) == 0x80) { /* sin cortar un carácter UTF-8 */
        l--;
    }
    if (l) {
        memcpy(d, s, l);
    }
    d[l] = 0;
}

/* ======================= formatos ======================= */

static const struct {
    const char *name;
    int kind;
    const char *why;
} kFmt[GAL_F_N] = {
    [GAL_F_UNKNOWN] = {"Desconocido", GAL_K_PHOTO, "Formato desconocido"},
    [GAL_F_JPEG] = {"JPEG", GAL_K_PHOTO, NULL},
    [GAL_F_JPEG_PROG] = {"JPEG progresivo", GAL_K_PHOTO, "JPEG progresivo: el reloj solo muestra JPEG baseline"},
    [GAL_F_PNG] = {"PNG", GAL_K_PHOTO, "PNG: el reloj solo muestra JPEG"},
    [GAL_F_GIF] = {"GIF", GAL_K_PHOTO, "GIF: el reloj solo muestra JPEG"},
    [GAL_F_BMP] = {"BMP", GAL_K_PHOTO, "BMP: el reloj solo muestra JPEG"},
    [GAL_F_WEBP] = {"WebP", GAL_K_PHOTO, "WebP: el reloj solo muestra JPEG"},
    [GAL_F_HEIC] = {"HEIC", GAL_K_PHOTO, "HEIC: el reloj solo muestra JPEG"},
    [GAL_F_AVIF] = {"AVIF", GAL_K_PHOTO, "AVIF: el reloj solo muestra JPEG"},
    [GAL_F_VIDEO] = {"Vídeo", GAL_K_VIDEO, "Vídeo: el reloj no reproduce vídeo; ábrelo en Flex OS Ultra o en la web"},
    [GAL_F_FXP] = {"Dibujo", GAL_K_DRAW, NULL},
};

gal_fmt_t gal_fmt_from_name(const char *name)
{
    static const struct {
        const char *ext;
        gal_fmt_t f;
    } X[] = {
        {".jpg", GAL_F_JPEG},  {".jpeg", GAL_F_JPEG},  {".png", GAL_F_PNG},   {".gif", GAL_F_GIF},
        {".bmp", GAL_F_BMP},   {".webp", GAL_F_WEBP},  {".heic", GAL_F_HEIC}, {".heif", GAL_F_HEIC},
        {".avif", GAL_F_AVIF}, {".avi", GAL_F_VIDEO},  {".mp4", GAL_F_VIDEO}, {".m4v", GAL_F_VIDEO},
        {".mov", GAL_F_VIDEO}, {".webm", GAL_F_VIDEO}, {".mkv", GAL_F_VIDEO}, {".3gp", GAL_F_VIDEO},
        {".fxp", GAL_F_FXP},
    };
    const char *d = name ? strrchr(name, '.') : NULL;
    if (!d || d == name) {
        return GAL_F_UNKNOWN;
    }
    for (size_t i = 0; i < sizeof(X) / sizeof(X[0]); i++) {
        if (!strcasecmp(d, X[i].ext)) {
            return X[i].f;
        }
    }
    return GAL_F_UNKNOWN;
}

gal_fmt_t gal_fmt_from_mime(const char *mime)
{
    if (!mime || !mime[0]) {
        return GAL_F_UNKNOWN;
    }
    if (!strncasecmp(mime, "video/", 6)) {
        return GAL_F_VIDEO;
    }
    static const struct {
        const char *m;
        gal_fmt_t f;
    } X[] = {
        {"image/jpeg", GAL_F_JPEG}, {"image/jpg", GAL_F_JPEG},   {"image/png", GAL_F_PNG},
        {"image/gif", GAL_F_GIF},   {"image/bmp", GAL_F_BMP},    {"image/webp", GAL_F_WEBP},
        {"image/heic", GAL_F_HEIC}, {"image/heif", GAL_F_HEIC},  {"image/avif", GAL_F_AVIF},
    };
    for (size_t i = 0; i < sizeof(X) / sizeof(X[0]); i++) {
        const size_t l = strlen(X[i].m);
        if (!strncasecmp(mime, X[i].m, l) && (mime[l] == 0 || mime[l] == ';')) {
            return X[i].f;
        }
    }
    return GAL_F_UNKNOWN;
}

gal_fmt_t gal_fmt_from_ultra(const char *fmt)
{
    /* kFmt[].name de FlexOS_MediaLib.cpp */
    static const struct {
        const char *n;
        gal_fmt_t f;
    } X[] = {
        {"JPEG", GAL_F_JPEG}, {"JPEG progresivo", GAL_F_JPEG_PROG}, {"PNG", GAL_F_PNG},     {"GIF", GAL_F_GIF},
        {"BMP", GAL_F_BMP},   {"WebP", GAL_F_WEBP},                 {"HEIC", GAL_F_HEIC},   {"AVIF", GAL_F_AVIF},
        {"AVI MJPEG", GAL_F_VIDEO}, {"AVI", GAL_F_VIDEO},           {"MP4", GAL_F_VIDEO},   {"MOV", GAL_F_VIDEO},
        {"WebM", GAL_F_VIDEO}, {"MKV", GAL_F_VIDEO},                {"Dibujo", GAL_F_FXP},
    };
    for (size_t i = 0; fmt && i < sizeof(X) / sizeof(X[0]); i++) {
        if (!strcmp(fmt, X[i].n)) {
            return X[i].f;
        }
    }
    return GAL_F_UNKNOWN;
}

bool gal_fmt_viewable(gal_fmt_t f)
{
    return f == GAL_F_JPEG || f == GAL_F_FXP;
}

const char *gal_fmt_why(gal_fmt_t f)
{
    return f >= 0 && f < GAL_F_N ? kFmt[f].why : kFmt[0].why;
}

const char *gal_fmt_name(gal_fmt_t f)
{
    return f >= 0 && f < GAL_F_N ? kFmt[f].name : kFmt[0].name;
}

int gal_kind_of(gal_fmt_t f)
{
    return f >= 0 && f < GAL_F_N ? kFmt[f].kind : GAL_K_PHOTO;
}

/* ======================= estado y Automático ======================= */

uint32_t gal_backoff_ms(uint8_t fails)
{
    if (fails == 0) {
        return 0;
    }
    if (fails >= 6) {
        return 60000;
    }
    return 2000u << (fails - 1); /* 2, 4, 8, 16, 32 s */
}

void gal_health_ok(gal_health_t *h, uint32_t now, uint32_t recheck_ms)
{
    if (h->state != GAL_ST_READY) {
        h->ready_ms = now;
    }
    h->state = GAL_ST_READY;
    h->fails = 0;
    h->next_ms = now + recheck_ms;
    h->detail[0] = 0;
}

void gal_health_fail(gal_health_t *h, uint8_t state, const char *why, uint32_t now)
{
    if (h->fails < 255) {
        h->fails++;
    }
    h->state = state;
    h->ready_ms = 0;
    h->next_ms = now + gal_backoff_ms(h->fails);
    gal_copyz(h->detail, sizeof(h->detail), why);
}

const char *gal_src_name(int src)
{
    switch (src) {
    case GAL_SRC_LOCAL: return "Reloj";
    case GAL_SRC_ULTRA: return "Flex OS Ultra";
    case GAL_SRC_CLOUD: return "Flex Cloud";
    case GAL_SRC_PHONE: return "Flex Phone";
    default: return "Ninguna";
    }
}

const char *gal_pick_name(gal_pick_t p)
{
    switch (p) {
    case GAL_PICK_AUTO: return "Automático";
    case GAL_PICK_ULTRA: return "Flex OS Ultra";
    case GAL_PICK_CLOUD: return "Flex Cloud";
    case GAL_PICK_PHONE: return "Flex Phone";
    default: return "?";
    }
}

const char *gal_state_name(uint8_t st)
{
    switch (st) {
    case GAL_ST_UNSET: return "Sin configurar";
    case GAL_ST_NOWIFI: return "Sin Wi-Fi";
    case GAL_ST_CHECKING: return "Comprobando";
    case GAL_ST_READY: return "Conectado";
    case GAL_ST_PAIR: return "Hay que volver a conectar";
    case GAL_ST_DOWN: return "No responde";
    default: return "?";
    }
}

static const char *unset_why(int src)
{
    switch (src) {
    case GAL_SRC_ULTRA: return "Conecta con Flex OS Ultra en Origen (código de su Flex Web Server)";
    case GAL_SRC_CLOUD: return "Vincula este reloj con tu Flex Account en Origen";
    case GAL_SRC_PHONE: return "Flex OS Ultra no tiene un teléfono listo en Flex Storage";
    default: return "";
    }
}

void gal_pick_resolve(gal_pick_t pick, const gal_health_t h[GAL_SRC_N], bool wifi, int prev, uint32_t now,
                      gal_choice_t *out)
{
    static const int8_t ORDER[] = {GAL_SRC_ULTRA, GAL_SRC_CLOUD, GAL_SRC_PHONE};
    memset(out, 0, sizeof(*out));
    out->src = GAL_SRC_NONE;
    out->automatic = pick == GAL_PICK_AUTO;
    if (!wifi) {
        gal_copyz(out->why, sizeof(out->why), "Sin Wi-Fi: solo lo que hay en el reloj");
        return;
    }
    if (pick != GAL_PICK_AUTO) {
        const int s = pick == GAL_PICK_ULTRA ? GAL_SRC_ULTRA : pick == GAL_PICK_CLOUD ? GAL_SRC_CLOUD : GAL_SRC_PHONE;
        out->src = (int8_t)s;
        const gal_health_t *x = &h[s];
        if (x->state == GAL_ST_UNSET) {
            gal_copyz(out->why, sizeof(out->why), x->detail[0] ? x->detail : unset_why(s));
        } else if (x->state != GAL_ST_READY) {
            snprintf(out->why, sizeof(out->why), "%s: %.80s", gal_state_name(x->state), x->detail);
        }
        return;
    }
    /* La que está en uso, mientras siga lista, salvo que una preferida lleve lista el tiempo de estabilidad. */
    int best = GAL_SRC_NONE;
    for (size_t i = 0; i < sizeof(ORDER); i++) {
        const int s = ORDER[i];
        if (h[s].state != GAL_ST_READY) {
            continue;
        }
        if (s == prev) {
            best = s;
            break;
        }
        const bool stable = now - h[s].ready_ms >= GAL_AUTO_STABLE_MS;
        if (prev < 0 || h[prev].state != GAL_ST_READY || stable) {
            best = s;
            break;
        }
    }
    if (best >= 0) {
        out->src = (int8_t)best;
        snprintf(out->why, sizeof(out->why), "Automático: %s", gal_src_name(best));
        return;
    }
    for (size_t i = 0; i < sizeof(ORDER); i++) {
        if (h[ORDER[i]].state == GAL_ST_CHECKING) {
            out->src = ORDER[i];
            snprintf(out->why, sizeof(out->why), "Conectando con %s", gal_src_name(ORDER[i]));
            return;
        }
    }
    for (size_t i = 0; i < sizeof(ORDER); i++) {
        const gal_health_t *x = &h[ORDER[i]];
        if (x->state != GAL_ST_UNSET) {
            out->src = ORDER[i];
            snprintf(out->why, sizeof(out->why), "%s: %s", gal_src_name(ORDER[i]),
                     x->detail[0] ? x->detail : gal_state_name(x->state));
            return;
        }
    }
    gal_copyz(out->why, sizeof(out->why), "Conecta Flex OS Ultra o vincula Flex Account en Origen");
}

/* ======================= errores ======================= */

gal_err_t gal_err_from_http(int status, const char *code)
{
    if (status < 0) {
        switch (status) {
        case GAL_HTTP_E_TLS: return GAL_E_TLS;
        case GAL_HTTP_E_PROTO:
        case GAL_HTTP_E_TOOBIG: return GAL_E_PROTO;
        case GAL_HTTP_E_CANCEL: return GAL_E_CANCEL;
        case GAL_HTTP_E_BODY: return GAL_E_FS;
        case GAL_HTTP_E_ARG: return GAL_E_ARG;
        default: return GAL_E_NET;
        }
    }
    if (status >= 200 && status < 300) {
        return GAL_E_OK;
    }
    if (code && code[0]) {
        static const struct {
            const char *c;
            gal_err_t e;
        } C[] = {
            {"auth_required", GAL_E_AUTH},      {"token_expired", GAL_E_AUTH},     {"device_revoked", GAL_E_AUTH},
            {"csrf_failed", GAL_E_AUTH},        {"account_unavailable", GAL_E_SERVER},
            {"not_found", GAL_E_NOTFOUND},      {"upload_not_found", GAL_E_NOTFOUND},
            {"quota_exceeded", GAL_E_QUOTA},    {"file_too_large", GAL_E_TOOBIG},  {"payload_too_large", GAL_E_TOOBIG},
            {"rate_limited", GAL_E_RATE},       {"checksum_mismatch", GAL_E_INTEGRITY},
            {"upload_expired", GAL_E_EXPIRED},  {"server_busy", GAL_E_BUSY},       {"internal_error", GAL_E_SERVER},
        };
        for (size_t i = 0; i < sizeof(C) / sizeof(C[0]); i++) {
            if (!strcmp(code, C[i].c)) {
                return C[i].e;
            }
        }
    }
    if (code && (status == 401 || status == 403)) {
        /* Flex Cloud sin un código suyo: la página de un proxy o del alojamiento.
         * No es una sesión perdida (flexAccountClassify): se reintenta. */
        return GAL_E_SERVER;
    }
    switch (status) {
    case 401:
    case 403: return GAL_E_AUTH;
    case 404:
    case 410: return GAL_E_NOTFOUND;
    case 408: return GAL_E_NET;
    case 413: return GAL_E_TOOBIG;
    case 422: return GAL_E_INTEGRITY;
    case 429: return GAL_E_RATE;
    case 503: return GAL_E_BUSY;
    case 507: return GAL_E_QUOTA;
    default: return status >= 500 ? GAL_E_SERVER : GAL_E_PROTO;
    }
}

bool gal_err_retry(gal_err_t e)
{
    switch (e) {
    case GAL_E_NET:
    case GAL_E_TLS:
    case GAL_E_PROTO:
    case GAL_E_RATE:
    case GAL_E_BUSY:
    case GAL_E_SERVER:
    case GAL_E_INTEGRITY: return true;
    default: return false;
    }
}

const char *gal_err_text(gal_err_t e)
{
    switch (e) {
    case GAL_E_OK: return "";
    case GAL_E_NOWIFI: return "Sin Wi-Fi";
    case GAL_E_NET: return "No hay conexión con el servidor; se reintentará";
    case GAL_E_TLS: return "Conexión segura no verificada";
    case GAL_E_PROTO: return "Respuesta incompleta o no válida del servidor";
    case GAL_E_AUTH: return "La sesión ya no vale: vuelve a conectar en Origen";
    case GAL_E_LOCKED: return "Contenido protegido: ábrelo en Flex OS Ultra";
    case GAL_E_NOTFOUND: return "Ya no está en el servidor";
    case GAL_E_QUOTA: return "No queda espacio en el destino";
    case GAL_E_TOOBIG: return "El archivo es demasiado grande para el destino";
    case GAL_E_RATE: return "Demasiados intentos; espera un poco";
    case GAL_E_BUSY: return "El servidor está ocupado; se reintentará";
    case GAL_E_SERVER: return "El servidor tuvo un fallo; se reintentará";
    case GAL_E_NOSPACE: return "No queda espacio en el reloj";
    case GAL_E_NOMEM: return "No hay memoria libre ahora";
    case GAL_E_INTEGRITY: return "El archivo llegó distinto (huella o tamaño); se repite";
    case GAL_E_CANCEL: return "Cancelado";
    case GAL_E_FORMAT: return "El reloj no abre este formato";
    case GAL_E_FS: return "No se pudo leer o escribir en el reloj";
    case GAL_E_ARG: return "Petición no válida";
    case GAL_E_EXPIRED: return "La subida caducó en el servidor; se empieza de nuevo";
    default: return "Error";
    }
}

/* ======================= lectores ======================= */

static const char *root(const char *j, size_t n, const char **e)
{
    if (!j || !n) {
        return NULL;
    }
    *e = j + n;
    const char *p = gal_j_ws(j, *e);
    return p < *e && *p == '{' && gal_j_valid(p, *e) ? p : NULL;
}

bool gal_parse_hello(const char *j, size_t n, gal_hello_t *out)
{
    const char *e, *r = root(j, n, &e);
    memset(out, 0, sizeof(*out));
    if (!r || !gal_j_get_str(r, e, "dev", out->dev, sizeof(out->dev))) {
        return false;
    }
    int64_t lt = 0;
    gal_j_get_bool(r, e, "paired", &out->paired);
    gal_j_get_bool(r, e, "owner", &out->owner);
    gal_j_get_bool(r, e, "up", &out->up);
    if (gal_j_get_i64(r, e, "lockType", &lt)) {
        out->lock_type = (int)lt;
    }
    return true;
}

bool gal_hello_is_ultra(const gal_hello_t *h)
{
    return h && !strcmp(h->dev, "Flex OS Ultra"); /* handleHello: "dev":"Flex OS Ultra" */
}

int gal_parse_ultra_library(const char *j, size_t n, gal_item_t *out, int cap, uint32_t *rev, bool *same)
{
    const char *e, *r = root(j, n, &e);
    if (same) {
        *same = false;
    }
    if (!r) {
        return -1;
    }
    uint64_t rv = 0;
    if (!gal_j_get_u64(r, e, "rev", &rv)) {
        return -1;
    }
    if (rev) {
        *rev = (uint32_t)rv;
    }
    bool sm = false;
    if (gal_j_get_bool(r, e, "same", &sm) && sm) {
        if (same) {
            *same = true;
        }
        return 0;
    }
    const char *as, *ae;
    if (!gal_j_obj(r, e, "items", &as, &ae) || *as != '[') {
        return -1;
    }
    int k = 0;
    for (const char *it = gal_j_first(as, ae); it && k < cap; it = gal_j_next(it, ae)) {
        uint64_t id = 0;
        if (*it != '{' || !gal_j_get_u64(it, ae, "id", &id) || !id) {
            continue;
        }
        gal_item_t *o = &out[k];
        memset(o, 0, sizeof(*o));
        o->src = GAL_SRC_ULTRA;
        o->origin_src = GAL_SRC_NONE;
        snprintf(o->id, sizeof(o->id), "%llu", (unsigned long long)id);
        bool lock = false;
        gal_j_get_bool(it, ae, "lock", &lock);
        char kind[12] = "";
        if (!gal_j_get_str(it, ae, "k", kind, sizeof(kind))) {
            if (!lock) {
                continue; /* {"id":N} sin más: el P4 no pudo describirlo */
            }
            o->flags = GAL_I_LOCKED; /* lo único que se sabe: que existe */
            o->kind = GAL_K_PHOTO;
            gal_copyz(o->name, sizeof(o->name), "Protegido");
            k++;
            continue;
        }
        if (!strcmp(kind, "photo")) {
            o->kind = GAL_K_PHOTO;
        } else if (!strcmp(kind, "video")) {
            o->kind = GAL_K_VIDEO;
        } else {
            continue; /* audio: es de Música */
        }
        uint64_t v;
        char fmt[24] = "", st[8] = "";
        o->size = gal_j_get_u64(it, ae, "s", &v) ? v : 0;
        uint64_t c = 0, a = 0;
        gal_j_get_u64(it, ae, "c", &c);
        gal_j_get_u64(it, ae, "a", &a);
        o->when = (int64_t)(c ? c : a) * 1000;
        o->w = gal_j_get_u64(it, ae, "w", &v) && v < 65536 ? (uint16_t)v : 0;
        o->h = gal_j_get_u64(it, ae, "h", &v) && v < 65536 ? (uint16_t)v : 0;
        o->dur_ms = gal_j_get_u64(it, ae, "d", &v) && v < UINT32_MAX ? (uint32_t)v : 0;
        if (gal_j_get_u64(it, ae, "t", &v)) {
            o->flags |= GAL_I_THUMB;
            o->tver = (uint32_t)v;
        }
        if (lock) {
            o->flags |= GAL_I_LOCKED; /* con nivel de propietario llega entero */
        }
        if (gal_j_get_str(it, ae, "st", st, sizeof(st))) {
            o->flags |= !strcmp(st, "proc") ? GAL_I_BUSY : !strcmp(st, "err") ? GAL_I_DAMAGED : 0;
        }
        gal_j_get_str(it, ae, "fmt", fmt, sizeof(fmt));
        o->fmt = (uint8_t)gal_fmt_from_ultra(fmt);
        if (o->fmt == GAL_F_UNKNOWN && o->kind == GAL_K_VIDEO) {
            o->fmt = GAL_F_VIDEO;
        }
        if (!gal_j_get_str(it, ae, "n", o->name, sizeof(o->name)) || !o->name[0]) {
            gal_j_get_str(it, ae, "fn", o->name, sizeof(o->name));
        }
        k++;
    }
    return k;
}

bool gal_parse_overview(const char *j, size_t n, gal_phone_t *out)
{
    const char *e, *r = root(j, n, &e), *ps, *pe;
    memset(out, 0, sizeof(*out));
    if (!r || !gal_j_obj(r, e, "phone", &ps, &pe) || *ps != '{') {
        return false;
    }
    gal_j_get_str(ps, pe, "name", out->name, sizeof(out->name));
    gal_j_get_str(ps, pe, "model", out->model, sizeof(out->model));
    return gal_j_get_str(ps, pe, "state", out->state, sizeof(out->state));
}

static bool ok_true(const char *r, const char *e)
{
    bool ok = false;
    return gal_j_get_bool(r, e, "ok", &ok) && ok;
}

int gal_parse_cloud_list(const char *j, size_t n, uint8_t src, gal_item_t *out, int cap, char *cursor, size_t ccap)
{
    const char *e, *r = root(j, n, &e), *as, *ae;
    if (cursor && ccap) {
        cursor[0] = 0;
    }
    if (!r || !ok_true(r, e) || !gal_j_obj(r, e, "items", &as, &ae) || *as != '[') {
        return -1;
    }
    if (cursor && ccap) {
        const char *cv = gal_j_get(r, e, "nextCursor", NULL);
        if (cv && !gal_j_is_null(cv, e)) {
            gal_j_str(cv, e, cursor, ccap);
        }
    }
    int k = 0;
    for (const char *it = gal_j_first(as, ae); it && k < cap; it = gal_j_next(it, ae)) {
        char type[8] = "", kind[12] = "", mime[48] = "";
        if (*it != '{' || !gal_j_get_str(it, ae, "type", type, sizeof(type)) || strcmp(type, "file") ||
            !gal_j_get_str(it, ae, "kind", kind, sizeof(kind)) || (strcmp(kind, "photo") && strcmp(kind, "video"))) {
            continue;
        }
        gal_item_t *o = &out[k];
        memset(o, 0, sizeof(*o));
        o->origin_src = GAL_SRC_NONE;
        if (!gal_j_get_str(it, ae, "id", o->id, sizeof(o->id)) || strncmp(o->id, "fil_", 4) ||
            !gal_j_get_str(it, ae, "name", o->name, sizeof(o->name))) {
            continue;
        }
        bool safe = true; /* el id va en la ruta de la petición: solo [a-z0-9_] */
        for (const char *p = o->id; *p; p++) {
            safe = safe && ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_');
        }
        if (!safe) {
            continue;
        }
        o->src = src;
        o->kind = !strcmp(kind, "video") ? GAL_K_VIDEO : GAL_K_PHOTO;
        uint64_t v;
        int64_t t;
        o->size = gal_j_get_u64(it, ae, "size", &v) ? v : 0;
        gal_j_get_str(it, ae, "sha256", o->sha256, sizeof(o->sha256));
        if (strlen(o->sha256) != 64) {
            o->sha256[0] = 0;
        }
        bool th = false;
        if (gal_j_get_bool(it, ae, "hasThumbnail", &th) && th) {
            o->flags |= GAL_I_THUMB;
        }
        if (gal_j_get_i64(it, ae, "updatedAt", &t) || gal_j_get_i64(it, ae, "createdAt", &t)) {
            o->when = t;
            o->tver = (uint32_t)(t / 1000);
        }
        gal_j_get_str(it, ae, "mime", mime, sizeof(mime));
        o->fmt = (uint8_t)gal_fmt_from_mime(mime);
        if (o->fmt == GAL_F_UNKNOWN) {
            o->fmt = (uint8_t)gal_fmt_from_name(o->name);
        }
        if (o->kind == GAL_K_VIDEO) {
            o->fmt = GAL_F_VIDEO;
        }
        const char *ms, *me;
        if (gal_j_obj(it, ae, "metadata", &ms, &me) && *ms == '{') {
            o->w = gal_j_get_u64(ms, me, "width", &v) && v < 65536 ? (uint16_t)v : 0;
            o->h = gal_j_get_u64(ms, me, "height", &v) && v < 65536 ? (uint16_t)v : 0;
        }
        k++;
    }
    return k;
}

bool gal_parse_cloud_me(const char *j, size_t n, gal_me_t *out)
{
    const char *e, *r = root(j, n, &e), *as, *ae;
    memset(out, 0, sizeof(*out));
    if (!r || !ok_true(r, e) || !gal_j_obj(r, e, "account", &as, &ae) || *as != '{') {
        return false;
    }
    gal_j_get_str(as, ae, "id", out->account_id, sizeof(out->account_id));
    gal_j_get_str(as, ae, "flexAddress", out->address, sizeof(out->address));
    gal_j_get_str(as, ae, "displayName", out->display, sizeof(out->display));
    const char *ds, *de;
    if (gal_j_obj(r, e, "device", &ds, &de) && *ds == '{') {
        gal_j_get_str(ds, de, "kind", out->device_kind, sizeof(out->device_kind));
    }
    const char *qs, *qe;
    if (gal_j_obj(r, e, "quota", &qs, &qe) && *qs == '{') {
        gal_j_get_u64(qs, qe, "totalBytes", &out->q_total);
        gal_j_get_u64(qs, qe, "usedBytes", &out->q_used);
        gal_j_get_u64(qs, qe, "availableBytes", &out->q_avail);
        gal_j_get_str(qs, qe, "state", out->q_state, sizeof(out->q_state));
        gal_j_get_bool(qs, qe, "limitedByDevice", &out->limited_by_device);
    }
    const char *ls, *le;
    if (gal_j_obj(r, e, "limits", &ls, &le) && *ls == '{') {
        gal_j_get_u64(ls, le, "chunkMin", &out->chunk_min);
        gal_j_get_u64(ls, le, "chunkMax", &out->chunk_max);
        gal_j_get_u64(ls, le, "chunkDefault", &out->chunk_def);
        gal_j_get_u64(ls, le, "maxFileBytes", &out->max_file);
    }
    return true;
}

bool gal_parse_cloud_error(const char *j, size_t n, char *code, size_t cap, char *msg, size_t mcap)
{
    const char *e, *r = root(j, n, &e), *es, *ee;
    if (code && cap) {
        code[0] = 0;
    }
    if (msg && mcap) {
        msg[0] = 0;
    }
    bool ok = true;
    if (!r || !gal_j_get_bool(r, e, "ok", &ok) || ok || !gal_j_obj(r, e, "error", &es, &ee) || *es != '{') {
        return false;
    }
    if (msg) {
        gal_j_get_str(es, ee, "message", msg, mcap);
    }
    return gal_j_get_str(es, ee, "code", code, cap);
}

bool gal_parse_cloud_upload(const char *j, size_t n, gal_upload_t *out)
{
    const char *e, *r = root(j, n, &e), *us, *ue;
    memset(out, 0, sizeof(*out));
    if (!r || !ok_true(r, e) || !gal_j_obj(r, e, "upload", &us, &ue) || *us != '{') {
        return false;
    }
    uint64_t v;
    if (!gal_j_get_str(us, ue, "uploadId", out->id, sizeof(out->id)) || strncmp(out->id, "upl_", 4) ||
        !gal_j_get_u64(us, ue, "chunkSize", &out->chunk) || !out->chunk || !gal_j_get_u64(us, ue, "totalParts", &v) ||
        v > GAL_UP_PARTS_MAX) {
        return false;
    }
    for (const char *p = out->id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
            return false;
        }
    }
    out->parts = (uint32_t)v;
    gal_j_get_str(us, ue, "state", out->state, sizeof(out->state));
    gal_j_get_bool(us, ue, "resumed", &out->resumed);
    gal_j_get_str(us, ue, "fileId", out->file_id, sizeof(out->file_id));
    gal_j_get_str(us, ue, "sha256", out->sha256, sizeof(out->sha256));
    const char *as, *ae;
    if (gal_j_obj(us, ue, "receivedParts", &as, &ae) && *as == '[') {
        for (const char *it = gal_j_first(as, ae); it; it = gal_j_next(it, ae)) {
            uint64_t p;
            if (gal_j_u64(it, ae, &p) && p >= 1 && p <= out->parts && !gal_upload_has(out, (uint32_t)p)) {
                out->got[(p - 1) / 8] |= (uint8_t)(1u << ((p - 1) % 8));
                out->got_n++;
            }
        }
    }
    return true;
}

bool gal_upload_has(const gal_upload_t *u, uint32_t part)
{
    return part >= 1 && part <= u->parts && part <= GAL_UP_PARTS_MAX && (u->got[(part - 1) / 8] >> ((part - 1) % 8) & 1);
}

bool gal_parse_cloud_complete(const char *j, size_t n, char *file_id, size_t fcap, char *sha, size_t scap)
{
    const char *e, *r = root(j, n, &e), *fs, *fe;
    if (!r || !ok_true(r, e) || !gal_j_obj(r, e, "file", &fs, &fe) || *fs != '{') {
        return false;
    }
    return gal_j_get_str(fs, fe, "id", file_id, fcap) && gal_j_get_str(fs, fe, "sha256", sha, scap);
}

uint64_t gal_chunk_for(uint64_t size, uint64_t lo, uint64_t hi)
{
    uint64_t c = 256u * 1024u; /* el P4 usa 256 KB */
    if (!lo) {
        lo = 64u * 1024u;
    }
    if (!hi) {
        hi = 64u * 1024u * 1024u;
    }
    while (size / c + (size % c ? 1 : 0) > GAL_UP_PARTS_MAX && c < hi) {
        c *= 2;
    }
    if (c < lo) {
        c = lo;
    }
    if (c > hi) {
        c = hi;
    }
    return size / c + (size % c ? 1 : 0) <= GAL_UP_PARTS_MAX ? c : 0;
}

/* ======================= vista ======================= */

static bool same_origin(const gal_item_t *l, const gal_item_t *r)
{
    return (l->flags & GAL_I_REMOTE) && l->origin_src == (int8_t)r->src && !strcmp(l->origin_id, r->id);
}

int gal_view_build(const gal_item_t *loc, int nl, gal_item_t *rem, int nr, gal_tab_t tab, gal_ref_t *out, int cap)
{
    for (int i = 0; i < nr; i++) {
        rem[i].flags &= (uint8_t)~GAL_I_ONWATCH;
        for (int k = 0; k < nl; k++) {
            if (same_origin(&loc[k], &rem[i])) {
                rem[i].flags |= GAL_I_ONWATCH;
                break;
            }
        }
    }
    int n = 0;
    if (tab != GAL_TAB_REMOTE) {
        for (int i = 0; i < nl && n < cap; i++) {
            out[n++] = (gal_ref_t){0, (uint16_t)i};
        }
    }
    if (tab != GAL_TAB_WATCH) {
        for (int i = 0; i < nr && n < cap; i++) {
            if (tab == GAL_TAB_ALL && (rem[i].flags & GAL_I_ONWATCH)) {
                continue; /* ya está su copia en el reloj */
            }
            out[n++] = (gal_ref_t){1, (uint16_t)i};
        }
    }
    /* del más reciente al más antiguo; a igual fecha, el orden de llegada (estable) */
    for (int i = 1; i < n; i++) {
        const gal_ref_t x = out[i];
        const int64_t wx = x.list ? rem[x.idx].when : loc[x.idx].when;
        int k = i - 1;
        while (k >= 0) {
            const int64_t wk = out[k].list ? rem[out[k].idx].when : loc[out[k].idx].when;
            if (wk >= wx) {
                break;
            }
            out[k + 1] = out[k];
            k--;
        }
        out[k + 1] = x;
    }
    return n;
}

/* ======================= origen.txt ======================= */

bool gal_origin_parse(const char *line, gal_origin_t *o)
{
    memset(o, 0, sizeof(*o));
    const char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL, *t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
    if (!t3 || t1 - line != 1 || line[0] < '1' || line[0] > '3') {
        return false;
    }
    o->src = (int8_t)(line[0] - '0');
    const size_t il = (size_t)(t2 - t1 - 1);
    if (!il || il >= sizeof(o->id)) {
        return false;
    }
    memcpy(o->id, t1 + 1, il);
    o->id[il] = 0;
    uint64_t sz = 0;
    for (const char *p = t2 + 1; p < t3; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        sz = sz * 10 + (uint64_t)(*p - '0');
    }
    o->size = sz;
    size_t nl = strcspn(t3 + 1, "\r\n");
    if (!nl || nl >= sizeof(o->name)) {
        return false;
    }
    memcpy(o->name, t3 + 1, nl);
    o->name[nl] = 0;
    return fl_name_ok(o->name);
}

bool gal_origin_format(const gal_origin_t *o, char *out, size_t n)
{
    if (o->src < GAL_SRC_ULTRA || o->src > GAL_SRC_PHONE || strpbrk(o->id, "\t\r\n") || strpbrk(o->name, "\t\r\n")) {
        return false;
    }
    const int k = snprintf(out, n, "%d\t%s\t%llu\t%s\n", o->src, o->id, (unsigned long long)o->size, o->name);
    return k > 0 && (size_t)k < n;
}

void gal_origin_apply(gal_item_t *loc, int nl, const gal_origin_t *tab, int nt)
{
    for (int i = 0; i < nl; i++) {
        loc[i].flags &= (uint8_t)~GAL_I_REMOTE;
        loc[i].origin_src = GAL_SRC_NONE;
        loc[i].origin_id[0] = 0;
        const char *base = fl_base(loc[i].path);
        for (int k = nt - 1; k >= 0; k--) { /* la última línea manda */
            if (!strcmp(tab[k].name, base) && tab[k].size == loc[i].size && !strncmp(loc[i].path, GAL_DIR_PHOTO "/", 10)) {
                loc[i].flags |= GAL_I_REMOTE;
                loc[i].origin_src = tab[k].src;
                gal_copyz(loc[i].origin_id, sizeof(loc[i].origin_id), tab[k].id);
                break;
            }
        }
    }
}

/* ======================= nombres ======================= */

static size_t utf8_cut(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n <= max) {
        return n;
    }
    n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
        n--; /* no partir un carácter */
    }
    return n;
}

void gal_safe_name(const char *in, char *out, size_t n)
{
    char tmp[GAL_NAME_MAX * 2];
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && w + 1 < sizeof(tmp); p++) {
        unsigned char c = *p;
        if (c < 0x20 || c == 0x7F || strchr("/\\@:*?\"<>|", c)) {
            c = '_';
        }
        tmp[w++] = (char)c;
    }
    tmp[w] = 0;
    char *s = tmp;
    while (*s == ' ' || *s == '.') {
        s++;
    }
    size_t l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '.')) {
        s[--l] = 0;
    }
    const char *ext = fl_ext(s);
    char stem[GAL_NAME_MAX * 2];
    /* fl_ext devuelve "" (otro literal) si no hay extensión: la raíz es entonces todo el nombre */
    const size_t sl = ext[0] ? (size_t)(ext - s) : strlen(s);
    memcpy(stem, s, sl);
    stem[sl] = 0;
    if (!stem[0]) {
        snprintf(stem, sizeof(stem), "Foto");
        if (!ext[0]) {
            ext = ".jpg";
        }
    }
    const size_t el = strlen(ext) < 12 ? strlen(ext) : 0; /* una extensión absurda no se conserva */
    const size_t max = FL_NAME_MAX - 1 - el;
    const size_t keep = utf8_cut(stem, max);
    stem[keep] = 0;
    while (keep && stem[strlen(stem) - 1] == ' ') {
        stem[strlen(stem) - 1] = 0;
    }
    snprintf(out, n, "%s%s", stem[0] ? stem : "Foto", el ? ext : "");
}

void gal_name_variant(const char *name, int k, char *out, size_t cap)
{
    const char *ext = fl_ext(name);
    char stem[GAL_NAME_MAX * 2], suf[16];
    const size_t nl = ext[0] ? (size_t)(ext - name) : strlen(name);
    const size_t sl = nl < sizeof(stem) - 1 ? nl : sizeof(stem) - 1;
    memcpy(stem, name, sl);
    stem[sl] = 0;
    snprintf(suf, sizeof(suf), " (%d)", k);
    const size_t max = FL_NAME_MAX - 1 - strlen(ext) - strlen(suf);
    stem[utf8_cut(stem, max)] = 0;
    snprintf(out, cap, "%s%s%s", stem, suf, ext);
}

/* ======================= textos ======================= */

void gal_count_text(int n, char *out, size_t cap)
{
    snprintf(out, cap, "%d elemento%s", n, n == 1 ? "" : "s");
}

void gal_size_text(uint64_t b, char *out, size_t cap)
{
    if (b < 1024u) {
        snprintf(out, cap, "%u B", (unsigned)b);
        return;
    }
    static const char *U[] = {"KB", "MB", "GB"};
    uint64_t div = 1024u;
    int u = 0;
    while (u < 2 && b >= div * 1024u) {
        div *= 1024u;
        u++;
    }
    const uint64_t t = (b * 10u + div / 2u) / div; /* décimas */
    if (u == 0 || t >= 100) {
        snprintf(out, cap, "%llu %s", (unsigned long long)((t + 5) / 10), U[u]);
    } else {
        snprintf(out, cap, "%llu,%llu %s", (unsigned long long)(t / 10), (unsigned long long)(t % 10), U[u]);
    }
}

void gal_hex(const uint8_t *b, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = H[b[i] >> 4];
        out[2 * i + 1] = H[b[i] & 15];
    }
    out[2 * n] = 0;
}

uint32_t gal_crc32(uint32_t crc, const void *data, size_t n)
{
    /* zlib (0xEDB88320) por medios bytes: 64 B de tabla en vez de 1 KB */
    static const uint32_t T[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    const uint8_t *p = data;
    crc = ~crc;
    for (size_t i = 0; p && i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}
