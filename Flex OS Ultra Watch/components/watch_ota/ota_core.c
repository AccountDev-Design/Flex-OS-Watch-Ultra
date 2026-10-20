/*
 * ota_core — ver include/ota_core.h.
 */
#include "ota_core.h"

#include <stdio.h>
#include <string.h>
#include "ota_sig.h"

/* ======================= Versiones ======================= */

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alnum_dash(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool ota_ver_parse(const char *s, ota_ver_t *out)
{
    if (!s || strlen(s) >= OTA_VER_MAX) {
        return false;
    }
    ota_ver_t v;
    memset(&v, 0, sizeof(v));
    const char *p = s;
    for (int i = 0; i < 3; i++) {
        if (!is_digit(*p)) {
            return false; /* vacío, signo, espacio, "v"... */
        }
        uint32_t n = 0;
        while (is_digit(*p)) {
            n = n * 10 + (uint32_t)(*p - '0');
            if (n > 65535) {
                return false;
            }
            p++;
        }
        v.v[i] = (uint16_t)n;
        if (i < 2) {
            if (*p != '.') {
                return false;
            }
            p++;
        }
    }
    if (*p == '-') { /* RELOJ: preliberación SemVer */
        p++;
        const char *pre = p;
        bool empty_id = true;
        for (; *p; p++) {
            if (*p == '.') {
                if (empty_id) {
                    return false;
                }
                empty_id = true;
            } else if (is_alnum_dash(*p)) {
                empty_id = false;
            } else {
                return false;
            }
        }
        if (empty_id) {
            return false;
        }
        memcpy(v.pre, pre, (size_t)(p - pre) + 1);
    } else if (*p) {
        return false;
    }
    if (out) {
        *out = v;
    }
    return true;
}

/* Un identificador de preliberación: [s, s+n). */
static bool id_numeric(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!is_digit(s[i])) {
            return false;
        }
    }
    return true;
}

static int cmp_id(const char *a, size_t an, const char *b, size_t bn)
{
    const bool na = id_numeric(a, an), nb = id_numeric(b, bn);
    if (na && nb) {
        while (an > 1 && *a == '0') {
            a++, an--;
        }
        while (bn > 1 && *b == '0') {
            b++, bn--;
        }
        if (an != bn) {
            return an < bn ? -1 : 1;
        }
        const int c = memcmp(a, b, an);
        return c < 0 ? -1 : c > 0;
    }
    if (na != nb) {
        return na ? -1 : 1; /* numérico < alfanumérico */
    }
    const size_t m = an < bn ? an : bn;
    const int c = memcmp(a, b, m);
    if (c) {
        return c < 0 ? -1 : 1;
    }
    return an == bn ? 0 : (an < bn ? -1 : 1);
}

int ota_ver_cmp(const ota_ver_t *a, const ota_ver_t *b)
{
    for (int i = 0; i < 3; i++) {
        if (a->v[i] != b->v[i]) {
            return a->v[i] < b->v[i] ? -1 : 1;
        }
    }
    if (!a->pre[0] || !b->pre[0]) {
        return (!a->pre[0] && !b->pre[0]) ? 0 : (!a->pre[0] ? 1 : -1);
    }
    const char *pa = a->pre, *pb = b->pre;
    for (;;) {
        const size_t an = strcspn(pa, "."), bn = strcspn(pb, ".");
        const int c = cmp_id(pa, an, pb, bn);
        if (c) {
            return c;
        }
        pa += an;
        pb += bn;
        if (!*pa || !*pb) {
            return (!*pa && !*pb) ? 0 : (!*pa ? -1 : 1);
        }
        pa++;
        pb++;
    }
}

/* ======================= Utilidades ======================= */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool ota_md5_usable(const char *s)
{
    if (!s || strlen(s) != 32) {
        return false;
    }
    bool nonzero = false;
    for (int i = 0; i < 32; i++) {
        if (hexval(s[i]) < 0) {
            return false;
        }
        if (s[i] != '0') {
            nonzero = true;
        }
    }
    return nonzero;
}

void ota_fmt_bytes(uint32_t b, char *out, size_t n)
{
    if (b < 1024u) {
        snprintf(out, n, "%u B", (unsigned)b);
    } else if (b < 1048576u) {
        snprintf(out, n, "%u KB", (unsigned)(b / 1024u));
    } else {
        snprintf(out, n, "%u,%01u MB", (unsigned)(b / 1048576u), (unsigned)((uint64_t)(b % 1048576u) * 10u / 1048576u));
    }
}

bool ota_hex_decode(const char *hex, uint8_t *out, size_t n)
{
    if (!hex || strlen(hex) != 2 * n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const int h = hexval(hex[2 * i]), l = hexval(hex[2 * i + 1]);
        if (h < 0 || l < 0) {
            return false;
        }
        out[i] = (uint8_t)(h << 4 | l);
    }
    return true;
}

void ota_hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

/* otaErrText(), RELOJ: con tildes. */
static const char *const kErr[OTA_ERR_COUNT] = {
    [OTA_ERR_NONE] = "Error desconocido",
    [OTA_ERR_NO_WIFI] = "Sin conexión a Internet",
    [OTA_ERR_LOWMEM] = "Memoria insuficiente",
    [OTA_ERR_HTTPS] = "No se pudo conectar (HTTPS)",
    [OTA_ERR_TLS] = "Certificado no válido",
    [OTA_ERR_HTTP_STATUS] = "Archivo no encontrado",
    [OTA_ERR_TIMEOUT] = "Se agotó el tiempo de espera",
    [OTA_ERR_JSON] = "Manifiesto corrupto",
    [OTA_ERR_NO_BLOCK] = "Sin versión para este equipo",
    [OTA_ERR_BAD_VERSION] = "Versión no válida",
    [OTA_ERR_BAD_URL] = "Dirección de descarga no válida",
    [OTA_ERR_BAD_SIZE] = "Tamaño de firmware no válido",
    [OTA_ERR_NO_PARTITION] = "Partición sin soporte OTA",
    [OTA_ERR_BEGIN] = "No hay espacio para la imagen",
    [OTA_ERR_WRITE] = "Error de escritura",
    [OTA_ERR_TRUNCATED] = "Descarga incompleta",
    [OTA_ERR_CORRUPT] = "Firmware corrupto",
    [OTA_ERR_CANCELLED] = "Actualización cancelada",
    [OTA_ERR_NO_CA] = "Falta el certificado del servidor",
    [OTA_ERR_NO_SIG] = "La actualización no viene firmada",
    [OTA_ERR_BAD_SIG] = "La firma de la actualización no es válida",
    [OTA_ERR_ROLLBACK] = "Esa versión es anterior a la instalada",
    [OTA_ERR_NO_KEY] = "Este reloj no tiene clave para verificar actualizaciones",
    [OTA_ERR_WRONG_IMAGE] = "La imagen no es para este reloj",
    [OTA_ERR_BATTERY] = "Batería baja: pon el reloj a cargar",
};

const char *ota_err_text(int err)
{
    if (err <= OTA_ERR_NONE || err >= OTA_ERR_COUNT) {
        return kErr[OTA_ERR_NONE];
    }
    return kErr[err];
}

const char *ota_phase_text(int st)
{
    switch (st) {
    case OTA_CONNECTING: return "Conectando...";
    case OTA_DOWNLOADING: return "Descargando...";
    case OTA_VERIFYING: return "Verificando...";
    case OTA_INSTALLING: return "Instalando...";
    case OTA_FINALIZING: return "Finalizando...";
    case OTA_REBOOTING: return "Reiniciando...";
    default: return "Preparando...";
    }
}

bool ota_busy_state(int st)
{
    return st == OTA_CHECKING || (st >= OTA_CONNECTING && st <= OTA_REBOOTING);
}

/* ======================= JSON ======================= */
/* RELOJ: lector estricto (RFC 8259) sin reservas de memoria. La referencia
 * buscaba "clave": en cualquier nivel y daba por bueno un fichero roto. */

#define JS_DEPTH 16

typedef struct {
    const char *p, *end;
    int depth;
} js_t;

static void js_ws(js_t *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

/* Salida de una cadena: cap bytes con su terminador; *over si no cupo. */
typedef struct {
    char *out;
    size_t cap, len;
    bool over;
} js_out_t;

static void out_byte(js_out_t *o, uint8_t c)
{
    if (!o->out) {
        return;
    }
    if (o->len + 1 < o->cap) {
        o->out[o->len++] = (char)c;
    } else {
        o->over = true;
    }
}

static void out_cp(js_out_t *o, uint32_t cp)
{
    if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) {
        out_byte(o, '?'); /* sin NUL dentro ni medios pares sueltos */
    } else if (cp < 0x80) {
        out_byte(o, (uint8_t)cp);
    } else if (cp < 0x800) {
        out_byte(o, (uint8_t)(0xC0 | cp >> 6));
        out_byte(o, (uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        out_byte(o, (uint8_t)(0xE0 | cp >> 12));
        out_byte(o, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        out_byte(o, (uint8_t)(0x80 | (cp & 0x3F)));
    }
}

/* Cierra la salida sin dejar un carácter UTF-8 a medias si se cortó. */
static void out_end(js_out_t *o)
{
    if (!o->out) {
        return;
    }
    if (o->over) {
        size_t i = o->len;
        while (i > 0 && ((uint8_t)o->out[i - 1] & 0xC0) == 0x80) {
            i--;
        }
        if (i > 0 && (uint8_t)o->out[i - 1] >= 0xC0) {
            const uint8_t lead = (uint8_t)o->out[i - 1];
            const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (o->len - (i - 1) < need) {
                o->len = i - 1;
            }
        }
    }
    o->out[o->len] = 0;
}

/* Cadena en j->p ('"'). 0 = bien, -1 = sintaxis. Escapes como jStr: \t es
 * un espacio, \r se descarta; RELOJ: \uXXXX se decodifica. */
static int js_str(js_t *j, js_out_t *o)
{
    if (j->p >= j->end || *j->p != '"') {
        return -1;
    }
    j->p++;
    while (j->p < j->end) {
        const uint8_t c = (uint8_t)*j->p++;
        if (c == '"') {
            out_end(o);
            return 0;
        }
        if (c < 0x20) {
            return -1;
        }
        if (c != '\\') {
            out_byte(o, c);
            continue;
        }
        if (j->p >= j->end) {
            return -1;
        }
        const char e = *j->p++;
        switch (e) {
        case '"': case '\\': case '/': out_byte(o, (uint8_t)e); break;
        case 'n': out_byte(o, '\n'); break;
        case 't': out_byte(o, ' '); break;
        case 'r': case 'b': case 'f': break;
        case 'u': {
            if (j->end - j->p < 4) {
                return -1;
            }
            uint32_t cp = 0;
            for (int i = 0; i < 4; i++) {
                const int h = hexval(j->p[i]);
                if (h < 0) {
                    return -1;
                }
                cp = cp << 4 | (uint32_t)h;
            }
            j->p += 4;
            out_cp(o, cp);
            break;
        }
        default:
            return -1;
        }
    }
    return -1; /* sin cerrar */
}

/* Número JSON. *u32 = valor si es un entero sin signo que cabe en 32 bits. */
static int js_num(js_t *j, uint32_t *u32, bool *is_u32)
{
    const char *p = j->p;
    bool ok = true;
    uint64_t v = 0;
    if (p < j->end && *p == '-') {
        ok = false;
        p++;
    }
    if (p >= j->end || !is_digit(*p)) {
        return -1;
    }
    if (*p == '0') {
        p++;
    } else {
        while (p < j->end && is_digit(*p)) {
            v = v * 10 + (uint64_t)(*p - '0');
            if (v > 0xFFFFFFFFull) {
                ok = false;
                v = 0;
            }
            p++;
        }
    }
    if (p < j->end && *p == '.') {
        ok = false;
        p++;
        if (p >= j->end || !is_digit(*p)) {
            return -1;
        }
        while (p < j->end && is_digit(*p)) {
            p++;
        }
    }
    if (p < j->end && (*p == 'e' || *p == 'E')) {
        ok = false;
        p++;
        if (p < j->end && (*p == '+' || *p == '-')) {
            p++;
        }
        if (p >= j->end || !is_digit(*p)) {
            return -1;
        }
        while (p < j->end && is_digit(*p)) {
            p++;
        }
    }
    j->p = p;
    if (u32) {
        *u32 = ok ? (uint32_t)v : 0;
        *is_u32 = ok;
    }
    return 0;
}

static int js_value(js_t *j);

typedef int (*js_member_cb)(void *ud, const char *key, js_t *j);

/* Objeto en j->p ('{'). Cada miembro llama a cb con j->p en el valor; cb
 * tiene que consumirlo (o devolver 1 para que se salte). */
static int js_object(js_t *j, js_member_cb cb, void *ud)
{
    if (j->p >= j->end || *j->p != '{' || j->depth >= JS_DEPTH) {
        return -1;
    }
    j->depth++;
    j->p++;
    js_ws(j);
    if (j->p < j->end && *j->p == '}') {
        j->p++;
        j->depth--;
        return 0;
    }
    for (;;) {
        char key[40];
        js_out_t ko = {key, sizeof(key), 0, false};
        js_ws(j);
        if (js_str(j, &ko) != 0) {
            return -1;
        }
        js_ws(j);
        if (j->p >= j->end || *j->p != ':') {
            return -1;
        }
        j->p++;
        js_ws(j);
        int r = 1;
        if (cb && !ko.over) {
            r = cb(ud, key, j);
            if (r < 0) {
                return -1;
            }
        }
        if (r == 1 && js_value(j) != 0) {
            return -1;
        }
        js_ws(j);
        if (j->p >= j->end) {
            return -1;
        }
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == '}') {
            j->p++;
            j->depth--;
            return 0;
        }
        return -1;
    }
}

static int js_array(js_t *j)
{
    if (j->depth >= JS_DEPTH) {
        return -1;
    }
    j->depth++;
    j->p++;
    js_ws(j);
    if (j->p < j->end && *j->p == ']') {
        j->p++;
        j->depth--;
        return 0;
    }
    for (;;) {
        js_ws(j);
        if (js_value(j) != 0) {
            return -1;
        }
        js_ws(j);
        if (j->p >= j->end) {
            return -1;
        }
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == ']') {
            j->p++;
            j->depth--;
            return 0;
        }
        return -1;
    }
}

static int js_lit(js_t *j, const char *w)
{
    const size_t n = strlen(w);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, w, n) != 0) {
        return -1;
    }
    j->p += n;
    return 0;
}

static int js_value(js_t *j)
{
    if (j->p >= j->end) {
        return -1;
    }
    switch (*j->p) {
    case '"': {
        js_out_t o = {NULL, 0, 0, false};
        return js_str(j, &o);
    }
    case '{': return js_object(j, NULL, NULL);
    case '[': return js_array(j);
    case 't': return js_lit(j, "true");
    case 'f': return js_lit(j, "false");
    case 'n': return js_lit(j, "null");
    default: return js_num(j, NULL, NULL);
    }
}

/* Campos del bloque: gana la primera aparición, como jFind. */
typedef struct {
    bool has_ver, ver_str, ver_over;
    bool has_url, url_str, url_over;
    bool has_size, size_ok;
    bool has_md5, md5_str, md5_over;
    bool has_log;
    bool has_sig, sig_str, sig_over;
    bool has_alg, alg_ok;
    uint32_t size;
    ota_info_t *info;
} blk_t;

/* Lee una cadena a buf si el valor lo es (*is_str); si no, lo salta. */
static int take_str(js_t *j, char *buf, size_t cap, bool *is_str, bool *over)
{
    if (j->p < j->end && *j->p == '"') {
        js_out_t o = {buf, cap, 0, false};
        if (js_str(j, &o) != 0) {
            return -1;
        }
        *is_str = true;
        *over = o.over;
        return 0;
    }
    *is_str = false;
    return js_value(j);
}

static int blk_member(void *ud, const char *key, js_t *j)
{
    blk_t *b = ud;
    ota_info_t *in = b->info;
    bool dummy;
    int r = 1;
    if (!strcmp(key, "version") && !b->has_ver) {
        b->has_ver = true;
        r = take_str(j, in->ver, sizeof(in->ver), &b->ver_str, &b->ver_over);
    } else if (!strcmp(key, "url") && !b->has_url) {
        b->has_url = true;
        r = take_str(j, in->url, sizeof(in->url), &b->url_str, &b->url_over);
    } else if (!strcmp(key, "size") && !b->has_size) {
        b->has_size = true;
        if (j->p < j->end && *j->p != '"' && *j->p != '{' && *j->p != '[' && *j->p != 't' && *j->p != 'f' &&
            *j->p != 'n') {
            r = js_num(j, &b->size, &b->size_ok);
        }
    } else if (!strcmp(key, "md5") && !b->has_md5) {
        b->has_md5 = true;
        r = take_str(j, in->md5, sizeof(in->md5), &b->md5_str, &b->md5_over);
    } else if (!strcmp(key, "changelog") && !b->has_log) {
        bool is_str = false;
        r = take_str(j, in->changelog, sizeof(in->changelog), &is_str, &dummy);
        b->has_log = is_str;
        if (!is_str) {
            in->changelog[0] = 0;
        }
    } else if (!strcmp(key, "sig") && !b->has_sig) {
        b->has_sig = true;
        r = take_str(j, in->sig, sizeof(in->sig), &b->sig_str, &b->sig_over);
    } else if (!strcmp(key, "sigalg") && !b->has_alg) {
        char alg[24];
        bool is_str = false, over = false;
        b->has_alg = true;
        r = take_str(j, alg, sizeof(alg), &is_str, &over);
        b->alg_ok = is_str && !over && !strcmp(alg, OTA_SIGALG);
    }
    return r;
}

typedef struct {
    const char *key;
    bool found, is_obj;
    blk_t *blk;
} root_t;

static int root_member(void *ud, const char *key, js_t *j)
{
    root_t *r = ud;
    if (r->found || strcmp(key, r->key) != 0) {
        return 1;
    }
    r->found = true;
    if (j->p < j->end && *j->p == '{') {
        r->is_obj = true;
        return js_object(j, blk_member, r->blk);
    }
    return 1;
}

static bool url_ok(const char *u)
{
    if (strncmp(u, "https://", 8) != 0 || !u[8] || u[8] == '/') {
        return false;
    }
    for (const char *p = u; *p; p++) {
        if ((uint8_t)*p <= ' ' || (uint8_t)*p >= 0x7F) {
            return false;
        }
    }
    return true;
}

static bool hex_ok(const char *s, size_t n)
{
    if (strlen(s) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (hexval(s[i]) < 0) {
            return false;
        }
    }
    return true;
}

int ota_parse_manifest(const char *buf, size_t len, const char *key, const char *local_ver,
                       const ota_opts_t *opts, ota_info_t *out, bool *available)
{
    *available = false;
    if (len == 0) {
        return OTA_ERR_JSON;
    }
    memset(out, 0, sizeof(*out));
    blk_t b;
    memset(&b, 0, sizeof(b));
    b.info = out;
    root_t r = {key, false, false, &b};
    js_t j = {buf, buf + len, 0};
    js_ws(&j);
    if (js_object(&j, root_member, &r) != 0) {
        return OTA_ERR_NO_BLOCK;
    }
    js_ws(&j);
    if (j.p != j.end || !r.found || !r.is_obj) {
        return OTA_ERR_NO_BLOCK;
    }
    if (!b.has_ver || !b.ver_str) {
        return OTA_ERR_JSON;
    }
    ota_ver_t rv, lv;
    if (b.ver_over || !ota_ver_parse(out->ver, &rv) || !ota_ver_parse(local_ver, &lv)) {
        return OTA_ERR_BAD_VERSION;
    }
    if (ota_ver_cmp(&rv, &lv) <= 0) {
        if (opts->antirollback && ota_ver_cmp(&rv, &lv) < 0) {
            return OTA_ERR_ROLLBACK;
        }
        return OTA_ERR_NONE; /* al día; url no se exige */
    }
    if (!b.has_url || !b.url_str) {
        return OTA_ERR_JSON;
    }
    if (b.url_over || !url_ok(out->url)) {
        return OTA_ERR_BAD_URL;
    }
    out->size = (b.has_size && b.size_ok) ? b.size : 0;
    if (!b.md5_str || b.md5_over) {
        if (b.md5_over) {
            out->md5[0] = 0;
        }
        out->md5_ok = false;
    } else {
        out->md5_ok = ota_md5_usable(out->md5);
    }
    const bool sig_valid = b.sig_str && !b.sig_over && hex_ok(out->sig, OTA_SIG_HEX);
    if (opts->require_sig) {
        if (!b.has_sig || !b.sig_str || !out->sig[0]) {
            return OTA_ERR_NO_SIG;
        }
        if (!sig_valid || (b.has_alg && !b.alg_ok)) {
            return OTA_ERR_BAD_SIG;
        }
    }
    if (!sig_valid) {
        out->sig[0] = 0;
    }
    *available = true;
    return OTA_ERR_NONE;
}

size_t ota_sign_message(const char *product, const char *ver, uint32_t size, const uint8_t sha[32], char *out,
                        size_t n)
{
    char hex[65];
    ota_hex_encode(sha, 32, hex);
    const int r = snprintf(out, n, "flexos-ota-v1\n%s\n%s\n%lu\n%s", product, ver, (unsigned long)size, hex);
    return (r < 0 || (size_t)r >= n) ? 0 : (size_t)r;
}

/* ======================= Imagen y arranque ======================= */

#define IMG_MAGIC        0xE9
#define IMG_CHIP_OFF     12
#define CHIP_ID_ESP32S3  0x0009
#define DESC_OFF         32          /* esp_image_header_t (24) + esp_image_segment_header_t (8) */
#define DESC_MAGIC       0xABCD5432u
#define DESC_VER_OFF     (DESC_OFF + 16)
#define DESC_PROJ_OFF    (DESC_OFF + 48)

static bool field_eq(const uint8_t *f, const char *s)
{
    const size_t n = strlen(s);
    return n < 32 && memcmp(f, s, n) == 0 && f[n] == 0;
}

int ota_image_check(const uint8_t *head, size_t n, const char *project, const char *ver)
{
    if (n < OTA_HEAD_LEN || head[0] != IMG_MAGIC) {
        return OTA_ERR_WRONG_IMAGE;
    }
    const unsigned chip = (unsigned)head[IMG_CHIP_OFF] | (unsigned)head[IMG_CHIP_OFF + 1] << 8;
    const uint32_t magic = (uint32_t)head[DESC_OFF] | (uint32_t)head[DESC_OFF + 1] << 8 |
                           (uint32_t)head[DESC_OFF + 2] << 16 | (uint32_t)head[DESC_OFF + 3] << 24;
    if (chip != CHIP_ID_ESP32S3 || magic != DESC_MAGIC || !field_eq(head + DESC_PROJ_OFF, project) ||
        !field_eq(head + DESC_VER_OFF, ver)) {
        return OTA_ERR_WRONG_IMAGE;
    }
    return OTA_ERR_NONE;
}

int ota_boot_notice(const char *pend, const char *running, bool has_invalid)
{
    if (!pend || !pend[0]) {
        return OTA_BOOT_NONE;
    }
    if (running && !strcmp(pend, running)) {
        return OTA_BOOT_UPDATED;
    }
    return has_invalid ? OTA_BOOT_REVERTED : OTA_BOOT_NONE;
}

bool ota_auto_due(ota_auto_t *a, uint32_t now, bool online, int state)
{
    if (!online) {
        a->was_online = false;
        return false;
    }
    if (!a->was_online) {
        a->was_online = true;
        a->online_since = now;
    }
    if (state != OTA_IDLE && state != OTA_UP_TO_DATE && state != OTA_ERROR) {
        return false;
    }
    if (!a->checked) {
        if (now - a->online_since < OTA_AUTO_FIRST_MS) {
            return false;
        }
        a->checked = true;
        a->last = now;
        return true;
    }
    if (now - a->last >= OTA_AUTO_EVERY_MS) {
        a->last = now;
        return true;
    }
    return false;
}

/* ======================= Motor ======================= */

void ota_init(ota_t *o, const char *key, const char *local_ver, const char *manifest_url, const uint8_t *pubkey)
{
    memset(o, 0, sizeof(*o));
    snprintf(o->key, sizeof(o->key), "%s", key);
    snprintf(o->local, sizeof(o->local), "%s", local_ver);
    snprintf(o->url, sizeof(o->url), "%s", manifest_url);
    o->opts.require_sig = true;
    if (pubkey) {
        memcpy(o->pubkey, pubkey, sizeof(o->pubkey));
        o->have_key = pubkey[0] == 0x04;
    }
    o->state = OTA_IDLE;
}

bool ota_req_check(ota_t *o, bool online)
{
    if (ota_busy_state(o->state)) {
        return false;
    }
    o->gen++;
    if (!online) {
        o->err = OTA_ERR_NO_WIFI;
        o->state = OTA_ERROR;
        return false;
    }
    o->err = OTA_ERR_NONE;
    o->state = OTA_CHECKING;
    return true;
}

bool ota_req_install(ota_t *o, bool online)
{
    if (ota_busy_state(o->state) || !o->has_upd) {
        return false;
    }
    o->gen++;
    if (!online) {
        o->err = OTA_ERR_NO_WIFI;
        o->state = OTA_ERROR;
        return false;
    }
    o->cancel = false; /* RELOJ: aquí y no en la tarea (un "Cancelar" temprano no se pierde) */
    o->err = OTA_ERR_NONE;
    o->done = o->bps = 0;
    o->pct = 0;
    o->state = OTA_CONNECTING;
    return true;
}

void ota_req_cancel(ota_t *o)
{
    if (o->state == OTA_CONNECTING || o->state == OTA_DOWNLOADING) {
        o->cancel = true;
    }
}

static void lk(const ota_io_t *io)
{
    if (io->lock) {
        io->lock(io->ctx);
    }
}

static void ulk(const ota_io_t *io)
{
    if (io->unlock) {
        io->unlock(io->ctx);
    }
}

static void set_state(ota_t *o, const ota_io_t *io, int st, int err)
{
    lk(io);
    o->state = (uint8_t)st;
    o->err = (uint8_t)err;
    o->gen++;
    ulk(io);
}

void ota_run_check(ota_t *o, const ota_io_t *io)
{
    if (!io->online(io->ctx)) {
        set_state(o, io, OTA_ERROR, OTA_ERR_NO_WIFI);
        return;
    }
    if (io->mem_ok && !io->mem_ok(io->ctx)) {
        set_state(o, io, OTA_ERROR, OTA_ERR_LOWMEM);
        return;
    }
    int64_t clen = -1;
    int e = io->open(io->ctx, o->url, &clen);
    if (e) {
        set_state(o, io, OTA_ERROR, e);
        return;
    }
    size_t n = 0;
    bool too_long = false;
    uint32_t t_last = io->now(io->ctx);
    for (;;) {
        uint8_t *dst = n < OTA_MANIFEST_MAX - 1 ? (uint8_t *)o->manifest + n : o->chunk;
        const int want = n < OTA_MANIFEST_MAX - 1 ? (int)(OTA_MANIFEST_MAX - 1 - n) : 1;
        const int r = io->read(io->ctx, dst, want);
        if (r > 0) {
            if (dst == o->chunk) {
                too_long = true; /* RELOJ: un manifiesto que no cabe no se recorta */
                break;
            }
            n += (size_t)r;
            t_last = io->now(io->ctx);
        } else if (r == 0) {
            if (io->now(io->ctx) - t_last > OTA_STALL_MS) {
                io->close(io->ctx);
                set_state(o, io, OTA_ERROR, OTA_ERR_TIMEOUT);
                return;
            }
        } else {
            break;
        }
    }
    io->close(io->ctx);
    o->manifest[n] = 0;
    if (too_long) {
        set_state(o, io, OTA_ERROR, OTA_ERR_JSON);
        return;
    }
    if (clen >= 0 && (int64_t)n < clen) {
        set_state(o, io, OTA_ERROR, OTA_ERR_TRUNCATED);
        return;
    }
    bool avail = false;
    e = ota_parse_manifest(o->manifest, n, o->key, o->local, &o->opts, &o->scratch, &avail);
    lk(io);
    if (e) {
        o->state = OTA_ERROR;
        o->err = (uint8_t)e;
    } else if (avail) {
        o->info = o->scratch;
        o->total = o->info.size;
        o->done = o->bps = 0;
        o->pct = 0;
        o->has_upd = true;
        o->err = OTA_ERR_NONE;
        o->state = OTA_AVAILABLE;
    } else {
        memset(&o->info, 0, sizeof(o->info));
        o->has_upd = false;
        o->total = 0;
        o->err = OTA_ERR_NONE;
        o->state = OTA_UP_TO_DATE;
    }
    o->gen++;
    ulk(io);
}

static bool batt_low(const ota_io_t *io, int min_pct, bool strict)
{
    if (!io->battery) {
        return false;
    }
    int pct = -1;
    bool chg = false;
    io->battery(io->ctx, &pct, &chg);
    if (pct < 0 || chg) {
        return false; /* sin dato del AXP2101: no bloquea */
    }
    return strict ? pct <= min_pct : pct < min_pct;
}

void ota_run_install(ota_t *o, const ota_io_t *io)
{
    if (!o->has_upd || !o->info.url[0]) {
        set_state(o, io, OTA_ERROR, OTA_ERR_BAD_URL);
        return;
    }
    if (o->opts.require_sig && !o->have_key) {
        set_state(o, io, OTA_ERROR, OTA_ERR_NO_KEY);
        return;
    }
    if (batt_low(io, OTA_BATT_START_PCT, false)) {
        set_state(o, io, OTA_ERROR, OTA_ERR_BATTERY);
        return;
    }
    const uint32_t slot = io->slot_size(io->ctx);
    if (!slot) {
        set_state(o, io, OTA_ERROR, OTA_ERR_NO_PARTITION);
        return;
    }
    if (!io->online(io->ctx)) {
        set_state(o, io, OTA_ERROR, OTA_ERR_NO_WIFI);
        return;
    }
    if (io->mem_ok && !io->mem_ok(io->ctx)) {
        set_state(o, io, OTA_ERROR, OTA_ERR_LOWMEM);
        return;
    }
    if (o->cancel) {
        set_state(o, io, OTA_ERROR, OTA_ERR_CANCELLED);
        return;
    }
    int64_t clen = -1;
    int e = io->open(io->ctx, o->info.url, &clen);
    if (e) {
        set_state(o, io, OTA_ERROR, e);
        return;
    }
    if (clen < (int64_t)OTA_MIN_BIN || clen > (int64_t)slot || (o->info.size && clen != (int64_t)o->info.size)) {
        io->close(io->ctx);
        set_state(o, io, OTA_ERROR, OTA_ERR_BAD_SIZE);
        return;
    }
    const uint32_t total = (uint32_t)clen;
    e = io->fl_begin(io->ctx, total);
    if (e) {
        io->close(io->ctx);
        set_state(o, io, OTA_ERROR, e);
        return;
    }
    ota_hash_t *h = ota_hash_new(o->info.md5_ok);
    if (!h) {
        io->fl_abort(io->ctx);
        io->close(io->ctx);
        set_state(o, io, OTA_ERROR, OTA_ERR_LOWMEM);
        return;
    }
    lk(io);
    o->total = total;
    o->state = OTA_DOWNLOADING;
    o->gen++;
    ulk(io);

    uint32_t written = 0, rate_bytes = 0;
    uint32_t t_last = io->now(io->ctx), t_rate = t_last;
    bool first_rate = true;
    size_t head_n = 0;
    e = OTA_ERR_NONE;
    while (written < total) {
        if (o->cancel) {
            e = OTA_ERR_CANCELLED;
            break;
        }
        if (batt_low(io, OTA_BATT_ABORT_PCT, true)) {
            e = OTA_ERR_BATTERY;
            break;
        }
        const uint32_t left = total - written;
        const int want = (int)(left < OTA_CHUNK ? left : OTA_CHUNK);
        const int r = io->read(io->ctx, o->chunk, want);
        const uint32_t now = io->now(io->ctx);
        if (r > 0) {
            if (head_n < OTA_HEAD_LEN) {
                const size_t c = (size_t)r < OTA_HEAD_LEN - head_n ? (size_t)r : OTA_HEAD_LEN - head_n;
                memcpy(o->head + head_n, o->chunk, c);
                head_n += c;
                if (head_n == OTA_HEAD_LEN &&
                    (e = ota_image_check(o->head, head_n, o->key, o->info.ver)) != OTA_ERR_NONE) {
                    break; /* RELOJ: otra imagen no se escribe entera */
                }
            }
            if ((e = io->fl_write(io->ctx, o->chunk, r)) != OTA_ERR_NONE) {
                break;
            }
            ota_hash_update(h, o->chunk, (size_t)r);
            written += (uint32_t)r;
            rate_bytes += (uint32_t)r;
            t_last = now;
            lk(io);
            o->done = written;
            o->pct = (uint8_t)((uint64_t)written * 100u / total);
            if (now - t_rate >= 500) {
                const uint32_t inst = (uint32_t)((uint64_t)rate_bytes * 1000u / (now - t_rate));
                o->bps = first_rate ? inst : (3u * o->bps + inst) / 4u;
                first_rate = false;
                rate_bytes = 0;
                t_rate = now;
            }
            o->gen++;
            ulk(io);
        } else if (r == 0) {
            if (now - t_last > OTA_STALL_MS) {
                e = OTA_ERR_TIMEOUT;
                break;
            }
        } else {
            break; /* conexión cerrada */
        }
    }
    io->close(io->ctx);
    if (!e && written != total) {
        e = OTA_ERR_TRUNCATED;
    }
    if (e) {
        ota_hash_free(h);
        io->fl_abort(io->ctx);
        set_state(o, io, OTA_ERROR, e);
        return;
    }

    set_state(o, io, OTA_VERIFYING, OTA_ERR_NONE);
    uint8_t sha[32], md5[16];
    ota_hash_finish(h, sha, o->info.md5_ok ? md5 : NULL);
    uint8_t want_md5[16];
    if (o->info.md5_ok && (!ota_hex_decode(o->info.md5, want_md5, 16) || memcmp(md5, want_md5, 16) != 0)) {
        e = OTA_ERR_CORRUPT;
    }
    if (!e && (o->opts.require_sig || o->info.sig[0])) {
        char msg[160];
        uint8_t sig[64];
        const size_t mn = ota_sign_message(o->key, o->info.ver, total, sha, msg, sizeof(msg));
        if (!o->have_key) {
            e = OTA_ERR_NO_KEY;
        } else if (!mn || !ota_hex_decode(o->info.sig, sig, sizeof(sig)) ||
                   !ota_p256_verify(o->pubkey, (const uint8_t *)msg, mn, sig)) {
            e = OTA_ERR_BAD_SIG;
        }
    }
    if (e) {
        io->fl_abort(io->ctx);
        set_state(o, io, OTA_ERROR, e);
        return;
    }
    set_state(o, io, OTA_INSTALLING, OTA_ERR_NONE);
    if ((e = io->fl_finish(io->ctx)) != OTA_ERR_NONE) {
        set_state(o, io, OTA_ERROR, e);
        return;
    }
    set_state(o, io, OTA_FINALIZING, OTA_ERR_NONE);
    io->save_pend(io->ctx, o->info.ver);
    io->sleep(io->ctx, 400);
    set_state(o, io, OTA_REBOOTING, OTA_ERR_NONE);
    io->sleep(io->ctx, 1600);
}

void ota_status_text(const ota_t *o, char *out, size_t n)
{
    switch (o->state) {
    case OTA_CHECKING: snprintf(out, n, "Buscando actualizaciones..."); break;
    case OTA_UP_TO_DATE: snprintf(out, n, "El sistema está al día"); break;
    case OTA_AVAILABLE: snprintf(out, n, "%s disponible", o->info.ver); break;
    case OTA_ERROR: snprintf(out, n, "%s", ota_err_text(o->err)); break;
    case OTA_IDLE: snprintf(out, n, "Buscar actualización"); break;
    default: snprintf(out, n, "%s", ota_phase_text(o->state)); break;
    }
}
