/*
 * gal_acct — ver include/gal_acct.h. Las citas son de FlexOS_Account.cpp.
 */
#include "gal_acct.h"

#include <stdio.h>
#include <string.h>

#include "gal_core.h"
#include "gal_json.h"
#include "ota_sig.h"

#define ENVELOPE_MAX 8192 /* ENVELOPE_MAX */
#define REJECT_RATE_MS 60000u

/* Misma clave pública anclada que Flex Store y el P4 (ACCOUNT_PUBLIC_KEY / ACCOUNT_KEY_ID).
 * La privada solo existe en Flex Developer Studio. */
static const uint8_t ACCOUNT_PUBLIC_KEY[65] = {
    0x04, 0x70, 0x9d, 0xf4, 0x80, 0xde, 0x8d, 0x66, 0x05, 0x38, 0x6b, 0x01, 0xb3, 0xf8, 0x9f, 0x20, 0xf7,
    0x29, 0x08, 0x7f, 0x76, 0xff, 0x76, 0xfd, 0x43, 0x9d, 0x50, 0xa8, 0x30, 0x9e, 0xac, 0xc7, 0x60, 0xf5,
    0x4d, 0x60, 0xbb, 0x85, 0xde, 0xa2, 0xd3, 0x79, 0x8e, 0x15, 0xab, 0xad, 0x89, 0xd5, 0x97, 0xa4, 0xc0,
    0x7c, 0xfa, 0x66, 0xc1, 0x70, 0x6d, 0xe6, 0x48, 0xb3, 0x8e, 0x40, 0x28, 0x2c, 0x05,
};
static const char ACCOUNT_KEY_ID[] = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";

enum { AUTHST_OK = 0, AUTHST_REQUIRED = 1, AUTHST_EXPIRED = 2 };

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

bool gal_acct_valid_address(const char *a)
{
    const size_t l = a ? strlen(a) : 0;
    return l >= 6 && !strcmp(a + l - 5, "@flex");
}

const char *gal_link_text(uint8_t link)
{
    switch (link) {
    case GAL_LINK_UNLINKED: return "Sin cuenta vinculada";
    case GAL_LINK_LINKED: return "Conectada";
    case GAL_LINK_OFFLINE: return "Vinculada · sin conexión";
    case GAL_LINK_UNAVAILABLE: return "Vinculada · servicio no disponible";
    case GAL_LINK_AUTH_REQUIRED: return "Vuelve a vincular tu cuenta";
    case GAL_LINK_TOKEN_EXPIRED: return "La sesión caducó: vuelve a vincular";
    default: return "Cuenta guardada incompleta";
    }
}

gal_sess_t gal_acct_classify(int status, const char *code)
{
    if (status >= 200 && status < 300) {
        return GAL_SESS_OK;
    }
    if (status == 401 || status == 403) {
        if (code && !strcmp(code, "token_expired")) {
            return GAL_SESS_EXPIRED;
        }
        if (code && (!strcmp(code, "device_revoked") || !strcmp(code, "auth_required") ||
                     !strcmp(code, "invalid_token") || !strcmp(code, "device_unknown"))) {
            return GAL_SESS_AUTH;
        }
    }
    return GAL_SESS_UNAVAILABLE; /* también un 401 sin código: puede ser la página de un proxy */
}

uint32_t gal_acct_backoff_ms(uint8_t fails)
{
    if (!fails) {
        return 0;
    }
    uint32_t ms = 30000u;
    for (uint8_t i = 1; i < fails && ms < 15u * 60u * 1000u; i++) {
        ms *= 2u;
    }
    return ms > 15u * 60u * 1000u ? 15u * 60u * 1000u : ms;
}

static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

static int b64_span(const char *s, size_t n, uint8_t *out, size_t cap)
{
    while (n && s[n - 1] == '=') {
        n--;
    }
    if ((n & 3u) == 1u) {
        return -1;
    }
    const size_t expect = n / 4u * 3u + ((n & 3u) ? (n & 3u) - 1u : 0u);
    if (expect > cap) {
        return -1;
    }
    uint32_t acc = 0;
    int bits = 0;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        const int v = b64v(s[i]);
        if (v < 0) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[w++] = (uint8_t)(acc >> bits);
        }
    }
    return w == expect ? (int)w : -1;
}

int gal_b64url_decode(const char *in, uint8_t *out, size_t cap)
{
    return in ? b64_span(in, strlen(in), out, cap) : -1;
}

/* Contenido crudo de una cadena JSON sin escapes (base64): [*s, *s + *n). */
static bool raw_str(const char *obj, const char *e, const char *key, const char **s, size_t *n)
{
    const char *ve = NULL, *v = gal_j_get(obj, e, key, &ve);
    if (!v || *v != '"' || ve - v < 2 || memchr(v, '\\', (size_t)(ve - v))) {
        return false;
    }
    *s = v + 1;
    *n = (size_t)(ve - v - 2);
    return true;
}

int gal_acct_open_envelope(const char *env, size_t n, const uint8_t pub[65], const char *key_id, char *payload,
                           size_t cap)
{
    const char *e = env + n, *r = gal_j_ws(env, e);
    if (!r || r >= e || *r != '{' || !gal_j_valid(r, e) || cap < 2) {
        return -1;
    }
    int64_t schema = 0;
    char alg[8], kid[72];
    const char *ps, *ss;
    size_t pn, sn;
    if (!gal_j_get_i64(r, e, "schema", &schema) || schema != 2 || !gal_j_get_str(r, e, "algorithm", alg, sizeof(alg)) ||
        strcmp(alg, "ES256") || !gal_j_get_str(r, e, "keyId", kid, sizeof(kid)) ||
        strcmp(kid, key_id ? key_id : ACCOUNT_KEY_ID) || !raw_str(r, e, "payload", &ps, &pn) ||
        !raw_str(r, e, "signature", &ss, &sn)) {
        return -1;
    }
    uint8_t sig[64];
    const int pl = b64_span(ps, pn, (uint8_t *)payload, cap - 1);
    if (pl <= 0 || b64_span(ss, sn, sig, sizeof(sig)) != 64 ||
        !ota_p256_verify(pub ? pub : ACCOUNT_PUBLIC_KEY, (const uint8_t *)payload, (size_t)pl, sig)) {
        payload[0] = 0;
        return -1;
    }
    payload[pl] = 0;
    return pl;
}

/* ======================= persistencia ======================= */

static bool get_str(const gal_store_t *st, const char *k, char *out, size_t n)
{
    size_t len = n - 1;
    if (!st->get || !st->get(st->ctx, k, out, &len) || len >= n) {
        out[0] = 0;
        return false;
    }
    out[len] = 0;
    return strlen(out) == len; /* sin ceros por medio */
}

static bool put_str(const gal_store_t *st, const char *k, const char *v)
{
    return st->set && st->set(st->ctx, k, v, strlen(v));
}

static void set_link(gal_acct_t *a, uint8_t link, const char *detail)
{
    a->link = link;
    gal_copyz(a->link_detail, sizeof(a->link_detail), detail ? detail : gal_link_text(link));
}

static void load(gal_acct_t *a, const gal_store_t *st)
{
    a->have = false;
    a->bearer[0] = a->address[0] = a->display[0] = 0;
    a->flow = GAL_ACCT_UNLINKED;
    set_link(a, GAL_LINK_UNLINKED, NULL);
    gal_copyz(a->stage, sizeof(a->stage), "Sin cuenta vinculada");
    uint32_t id = 0;
    size_t len = sizeof(id);
    if (!st->get || !st->get(st->ctx, "installId", &id, &len) || len != sizeof(id) || !id) {
        do {
            st->random(st->ctx, (uint8_t *)&id, sizeof(id));
        } while (!id);
        if (st->set) {
            st->set(st->ctx, "installId", &id, sizeof(id));
        }
    }
    a->install_id = id;
    uint8_t linked = 0, authst = AUTHST_OK;
    len = 1;
    const bool has_linked = st->get && st->get(st->ctx, "linked", &linked, &len) && len == 1;
    len = 1;
    if (!st->get || !st->get(st->ctx, "authst", &authst, &len) || len != 1) {
        authst = AUTHST_OK;
    }
    const bool tok = get_str(st, "token", a->bearer, sizeof(a->bearer));
    const bool adr = get_str(st, "address", a->address, sizeof(a->address));
    get_str(st, "display", a->display, sizeof(a->display));
    a->authst = authst;
    if (has_linked && linked == 1 && tok && strlen(a->bearer) == 43 && adr && gal_acct_valid_address(a->address)) {
        a->have = true;
        a->flow = GAL_ACCT_LINKED;
        gal_copyz(a->stage, sizeof(a->stage), "Cuenta vinculada");
        if (authst == AUTHST_EXPIRED) {
            set_link(a, GAL_LINK_TOKEN_EXPIRED, NULL);
        } else if (authst == AUTHST_REQUIRED) {
            set_link(a, GAL_LINK_AUTH_REQUIRED, NULL);
        } else {
            set_link(a, GAL_LINK_OFFLINE, "Vinculada; se comprobará al conectar el Wi-Fi");
        }
        return;
    }
    wipe(a->bearer, sizeof(a->bearer));
    a->address[0] = a->display[0] = 0;
    if (has_linked || tok || adr) {
        /* había algo, pero incompleto (corte a mitad de guardar): se dice, no se finge "sin cuenta" */
        set_link(a, GAL_LINK_ERROR, "La cuenta guardada está incompleta: vuelve a vincular");
    }
}

static bool persist_linked(const gal_store_t *st, const char *bearer, const char *address, const char *display)
{
    const uint8_t zero = 0, one = 1, ok = AUTHST_OK;
    /* "linked" va la ÚLTIMA: un corte a mitad deja un registro incompleto, no uno que parece válido */
    return st->set && st->set(st->ctx, "linked", &zero, 1) && put_str(st, "token", bearer) &&
           put_str(st, "address", address) && put_str(st, "display", display ? display : "") &&
           st->set(st->ctx, "authst", &ok, 1) && st->set(st->ctx, "linked", &one, 1);
}

static void persist_authst(gal_acct_t *a, const gal_store_t *st, uint8_t v)
{
    if (a->authst != v && st->set) {
        st->set(st->ctx, "authst", &v, 1);
    }
    a->authst = v;
}

void gal_acct_init(gal_acct_t *a, const gal_store_t *st, const char *mac_hex, const char *version)
{
    memset(a, 0, sizeof(*a));
    gal_copyz(a->host, sizeof(a->host), GAL_ACCT_HOST);
    a->port = 443;
    a->tls = true;
    gal_copyz(a->hw_mac, sizeof(a->hw_mac), mac_hex);
    gal_copyz(a->version, sizeof(a->version), version);
    load(a, st);
}

void gal_acct_set_service(gal_acct_t *a, const char *host, uint16_t port, bool tls, const uint8_t pub[65],
                          const char *key_id)
{
    gal_copyz(a->host, sizeof(a->host), host);
    a->port = port;
    a->tls = tls;
    a->pubkey = pub;
    a->key_id = key_id;
}

void gal_acct_request_link(gal_acct_t *a)
{
    if (a->flow == GAL_ACCT_REQUESTING || a->flow == GAL_ACCT_CODE) {
        return;
    }
    a->want_link = true;
    a->cancel = false;
    a->flow = GAL_ACCT_REQUESTING;
    a->error[0] = 0;
    gal_copyz(a->stage, sizeof(a->stage), "Creando enlace seguro");
}

void gal_acct_cancel(gal_acct_t *a)
{
    a->cancel = true;
}

void gal_acct_forget(gal_acct_t *a, const gal_store_t *st)
{
    const bool any = a->have || a->link == GAL_LINK_ERROR;
    if (any && st->erase) {
        st->erase(st->ctx, "linked");
        st->erase(st->ctx, "token");
        st->erase(st->ctx, "address");
        st->erase(st->ctx, "display");
        st->erase(st->ctx, "authst");
    }
    wipe(a->bearer, sizeof(a->bearer));
    wipe(a->pend_bearer, sizeof(a->pend_bearer));
    a->have = false;
    a->address[0] = a->display[0] = a->code[0] = 0;
    a->authst = AUTHST_OK;
    a->flow = GAL_ACCT_UNLINKED;
    a->want_link = a->want_validate = a->verified = false;
    a->fails = 0;
    a->epoch++;
    gal_copyz(a->stage, sizeof(a->stage), "Sin cuenta vinculada");
    set_link(a, GAL_LINK_UNLINKED, NULL);
}

bool gal_acct_usable(const gal_acct_t *a)
{
    return a->have && a->link != GAL_LINK_AUTH_REQUIRED && a->link != GAL_LINK_TOKEN_EXPIRED &&
           a->link != GAL_LINK_ERROR;
}

bool gal_acct_bearer(const gal_acct_t *a, char *out, size_t n)
{
    if (!a->have || !a->bearer[0] || n < sizeof(a->bearer)) {
        if (n) {
            out[0] = 0;
        }
        return false;
    }
    gal_copyz(out, n, a->bearer);
    return true;
}

void gal_acct_report_rejected(gal_acct_t *a, uint32_t now)
{
    if (!a->have || (a->last_reject && now - a->last_reject < REJECT_RATE_MS)) {
        return;
    }
    a->last_reject = now | 1u;
    a->want_validate = true;
    a->wait_from = now;
    a->wait_ms = 0;
}

/* ======================= red ======================= */

static int http_json(gal_acct_t *a, gal_conn_t *c, const char *method, const char *path, const char *bearer,
                     const char *body, char *out, size_t cap, size_t *len)
{
    gal_req_t rq = {
        .method = method, .host = a->host, .port = a->port, .tls = a->tls, .path = path, .bearer = bearer,
        .agent = "FlexOS-Watch-Ultra/1.0 ESP32-S3", .range_from = -1,
        .ctype = body ? "application/json" : NULL, .body = (const uint8_t *)body, .body_len = body ? strlen(body) : 0,
        .extra = "Accept: application/json\r\n",
    };
    gal_resp_t rs;
    const int st = gal_http_begin(c, &rq, &rs);
    *len = 0;
    out[0] = 0;
    if (st < 0) {
        return st;
    }
    if (rs.length > (int64_t)cap - 1) {
        gal_http_end(c);
        gal_conn_close(c);
        return GAL_HTTP_E_TOOBIG;
    }
    const int r = gal_http_read_all(c, out, cap, len);
    gal_http_end(c);
    return r < 0 ? r : st;
}

static void make_bearer(const gal_store_t *st, char out[48], char hash[65])
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint8_t rnd[32], dg[32];
    st->random(st->ctx, rnd, sizeof(rnd));
    uint32_t acc = 0;
    int bits = 0;
    size_t w = 0;
    for (size_t i = 0; i < sizeof(rnd); i++) {
        acc = acc << 8 | rnd[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out[w++] = T[(acc >> bits) & 63u];
        }
    }
    if (bits) {
        out[w++] = T[(acc << (6 - bits)) & 63u];
    }
    out[w] = 0;
    ota_sha256((const uint8_t *)out, w, dg);
    gal_hex(dg, sizeof(dg), hash);
    wipe(rnd, sizeof(rnd));
    wipe(dg, sizeof(dg));
}

static void finish_failed(gal_acct_t *a, uint8_t flow, const char *stage, const char *error)
{
    wipe(a->pend_bearer, sizeof(a->pend_bearer));
    a->pend_hash[0] = 0;
    a->code[0] = 0;
    if (a->have) { /* un enlace nuevo que no termina bien no toca la cuenta guardada */
        a->flow = GAL_ACCT_LINKED;
        gal_copyz(a->stage, sizeof(a->stage), "Cuenta vinculada");
    } else {
        a->flow = flow;
        gal_copyz(a->stage, sizeof(a->stage), stage);
    }
    gal_copyz(a->error, sizeof(a->error), error);
}

static bool code_payload(const char *p, size_t n, const char *hash, char code[9], char url[192])
{
    const char *e = p + n, *r = gal_j_ws(p, e);
    int64_t schema = 0;
    char purpose[24], th[66];
    if (!r || *r != '{' || !gal_j_get_i64(r, e, "schema", &schema) || schema != 1 ||
        !gal_j_get_str(r, e, "purpose", purpose, sizeof(purpose)) || strcmp(purpose, "flex-device-code") ||
        !gal_j_get_str(r, e, "code", code, 9) || !gal_j_get_str(r, e, "tokenHash", th, sizeof(th)) ||
        !gal_j_get_str(r, e, "activationUrl", url, 192) || strcmp(th, hash) || strlen(code) < 5 ||
        strncmp(url, GAL_ACCT_ACTIVATE, strlen(GAL_ACCT_ACTIVATE))) {
        return false;
    }
    for (const char *c = code; *c; c++) {
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= '2' && *c <= '9'))) {
            return false;
        }
    }
    return true;
}

enum { POLL_PENDING = 0, POLL_APPROVED, POLL_EXPIRED, POLL_INVALID };

static int status_payload(const char *p, size_t n, char addr[48], char disp[64])
{
    const char *e = p + n, *r = gal_j_ws(p, e);
    int64_t schema = 0;
    char purpose[24], state[12];
    if (!r || *r != '{' || !gal_j_get_i64(r, e, "schema", &schema) || schema != 1 ||
        !gal_j_get_str(r, e, "purpose", purpose, sizeof(purpose)) || strcmp(purpose, "flex-device-status") ||
        !gal_j_get_str(r, e, "state", state, sizeof(state))) {
        return POLL_INVALID;
    }
    if (!strcmp(state, "pending")) {
        return POLL_PENDING;
    }
    if (!strcmp(state, "expired")) {
        return POLL_EXPIRED;
    }
    if (!strcmp(state, "approved") && gal_j_get_str(r, e, "flexAddress", addr, 48) && gal_acct_valid_address(addr)) {
        const char *dv = gal_j_get(r, e, "displayName", NULL);
        if (!dv || gal_j_is_null(dv, e) || !gal_j_str(dv, e, disp, 64)) {
            disp[0] = 0;
        }
        return POLL_APPROVED;
    }
    return POLL_INVALID;
}

static bool request_code(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, char *buf, size_t cap, int *http)
{
    char label[64], body[420];
    gal_j_quote(GAL_ACCT_LABEL, label, sizeof(label));
    const int w = snprintf(body, sizeof(body),
                           "{\"hardwareId\":\"S3-%s-%08lX\",\"label\":%s,\"model\":\"" GAL_ACCT_MODEL
                           "\",\"flexVersion\":\"%s\",\"tokenHash\":\"%s\"}",
                           a->hw_mac, (unsigned long)a->install_id, label, a->version, a->pend_hash);
    (void)st;
    if (w <= 0 || (size_t)w >= sizeof(body)) {
        gal_copyz(a->error, sizeof(a->error), "Solicitud de cuenta demasiado grande");
        return false;
    }
    char *env = buf, *pay = buf + ENVELOPE_MAX;
    size_t n = 0;
    *http = http_json(a, c, "POST", GAL_ACCT_CODE_PATH, NULL, body, env, ENVELOPE_MAX, &n);
    if (*http != 200 && *http != 201) {
        if (*http == 409) {
            gal_copyz(a->error, sizeof(a->error), "Este registro del dispositivo ya estaba vinculado");
        } else if (*http == 429) {
            gal_copyz(a->error, sizeof(a->error), "Demasiados intentos; espera un minuto");
        } else if (*http > 0) {
            snprintf(a->error, sizeof(a->error), "Flex Account respondió HTTP %d", *http);
        } else {
            snprintf(a->error, sizeof(a->error), "%s", gal_http_err_text(*http));
        }
        return false;
    }
    const int pl = gal_acct_open_envelope(env, n, a->pubkey, a->key_id, pay, cap - ENVELOPE_MAX);
    if (pl < 0) {
        gal_copyz(a->error, sizeof(a->error), "Firma ES256 de Flex Account no válida");
        return false;
    }
    if (!code_payload(pay, (size_t)pl, a->pend_hash, a->code, a->activation)) {
        gal_copyz(a->error, sizeof(a->error), "Datos de vinculación incompatibles");
        return false;
    }
    return true;
}

static void link_start(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, bool wifi, uint32_t now, char *buf,
                       size_t cap)
{
    a->want_link = false;
    if (!wifi) {
        finish_failed(a, GAL_ACCT_ERROR, "Sin Wi-Fi", "Conecta el reloj a una Wi-Fi y vuelve a intentarlo");
        return;
    }
    make_bearer(st, a->pend_bearer, a->pend_hash);
    gal_copyz(a->stage, sizeof(a->stage), "Contactando Flex Account");
    int http = 0;
    bool ok = request_code(a, c, st, buf, cap, &http);
    if (!ok && http == 409) {
        /* registrado antes de que la credencial llegara a guardarse: identidad de instalación nueva */
        uint32_t id = 0;
        do {
            st->random(st->ctx, (uint8_t *)&id, sizeof(id));
        } while (!id || id == a->install_id);
        if (st->set && st->set(st->ctx, "installId", &id, sizeof(id))) {
            a->install_id = id;
            ok = request_code(a, c, st, buf, cap, &http);
        }
    }
    if (!ok) {
        char why[144];
        gal_copyz(why, sizeof(why), a->error[0] ? a->error : "No se recibió una respuesta auténtica de Flex Account");
        finish_failed(a, a->cancel ? GAL_ACCT_CANCELLED : GAL_ACCT_ERROR, a->cancel ? "Cancelado" : "Enlace no disponible",
                      a->cancel ? NULL : why);
        return;
    }
    a->flow = GAL_ACCT_CODE;
    a->flow_ms = now;
    a->poll_ms = now;
    a->error[0] = 0;
    gal_copyz(a->stage, sizeof(a->stage), "Escribe el código en la web de Flex Account");
}

static void link_poll(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, bool wifi, uint32_t now, char *buf,
                      size_t cap)
{
    if (a->cancel) {
        finish_failed(a, GAL_ACCT_CANCELLED, "Cancelado", NULL);
        return;
    }
    if (now - a->flow_ms >= GAL_ACCT_CODE_TTL_MS) {
        finish_failed(a, GAL_ACCT_EXPIRED, "El código caducó", a->have ? "El código de vinculación caducó" : NULL);
        return;
    }
    if (now - a->poll_ms < GAL_ACCT_POLL_MS) {
        return;
    }
    a->poll_ms = now;
    if (!wifi) {
        gal_copyz(a->error, sizeof(a->error), "Sin Wi-Fi. Se reintenta al volver la conexión");
        return;
    }
    char path[200];
    snprintf(path, sizeof(path), GAL_ACCT_CODE_PATH "?code=%s&tokenHash=%s", a->code, a->pend_hash);
    char *env = buf, *pay = buf + ENVELOPE_MAX;
    size_t n = 0;
    const int http = http_json(a, c, "GET", path, NULL, NULL, env, ENVELOPE_MAX, &n);
    if (http == 404) {
        finish_failed(a, GAL_ACCT_ERROR, "Respuesta rechazada", "Flex Account ya no reconoce este código");
        return;
    }
    if (http != 200) {
        if (http < 0) {
            snprintf(a->error, sizeof(a->error), "Sin conexión (%s). Reintentando", gal_http_err_text(http));
        } else {
            snprintf(a->error, sizeof(a->error), "Flex Account respondió HTTP %d. Reintentando", http);
        }
        return;
    }
    const int pl = gal_acct_open_envelope(env, n, a->pubkey, a->key_id, pay, cap - ENVELOPE_MAX);
    char addr[48] = "", disp[64] = "";
    const int r = pl < 0 ? POLL_INVALID : status_payload(pay, (size_t)pl, addr, disp);
    if (r == POLL_PENDING) {
        a->error[0] = 0;
        return;
    }
    if (r == POLL_EXPIRED) {
        finish_failed(a, GAL_ACCT_EXPIRED, "El código caducó", a->have ? "El código de vinculación caducó" : NULL);
        return;
    }
    if (r == POLL_INVALID) {
        finish_failed(a, GAL_ACCT_ERROR, "Respuesta rechazada", "El reloj rechazó un estado sin firma válida");
        return;
    }
    gal_copyz(a->stage, sizeof(a->stage), "Guardando tu Flex Account");
    if (!persist_linked(st, a->pend_bearer, addr, disp)) {
        finish_failed(a, GAL_ACCT_ERROR, "No se pudo guardar", "El almacenamiento del reloj no respondió");
        return;
    }
    gal_copyz(a->bearer, sizeof(a->bearer), a->pend_bearer);
    wipe(a->pend_bearer, sizeof(a->pend_bearer));
    a->pend_hash[0] = 0;
    gal_copyz(a->address, sizeof(a->address), addr);
    gal_copyz(a->display, sizeof(a->display), disp);
    a->have = true;
    a->authst = AUTHST_OK;
    a->flow = GAL_ACCT_LINKED;
    a->code[0] = 0;
    a->error[0] = 0;
    a->verified = true; /* el servidor ACABA de aprobar esta credencial */
    a->last_ok = now;
    a->want_validate = false;
    a->fails = 0;
    a->online = true;
    gal_copyz(a->stage, sizeof(a->stage), "Cuenta vinculada");
    set_link(a, GAL_LINK_LINKED, "Conectada con Flex Account");
}

static void session_tick(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, bool wifi, uint32_t now, char *buf,
                         size_t cap)
{
    if (!a->have) {
        return;
    }
    if (!wifi) {
        a->online = false;
        if (a->link == GAL_LINK_LINKED || a->link == GAL_LINK_UNAVAILABLE) {
            set_link(a, GAL_LINK_OFFLINE, "Sin Wi-Fi: se comprobará al volver la conexión");
        }
        return;
    }
    if (!a->online) { /* acaba de volver el Wi-Fi: se deja asentar la red */
        a->online = true;
        a->want_validate = true;
        uint32_t left = 0;
        if (a->fails) {
            const uint32_t el = now - a->wait_from;
            left = el < a->wait_ms ? a->wait_ms - el : 0;
        }
        a->wait_from = now;
        a->wait_ms = left < GAL_ACCT_SETTLE_MS ? GAL_ACCT_SETTLE_MS : left;
    }
    if (a->link == GAL_LINK_LINKED && a->verified && !a->want_validate && now - a->last_ok >= GAL_ACCT_REVALIDATE_MS) {
        a->want_validate = true;
        a->wait_from = now;
        a->wait_ms = 0;
    }
    if (!a->want_validate || now - a->wait_from < a->wait_ms) {
        return;
    }
    const uint32_t epoch = a->epoch;
    char bearer[48];
    gal_copyz(bearer, sizeof(bearer), a->bearer);
    size_t n = 0;
    const int http = http_json(a, c, "GET", GAL_ACCT_ME_PATH, bearer, NULL, buf, cap, &n);
    wipe(bearer, sizeof(bearer));
    if (epoch != a->epoch) {
        return; /* se desvinculó mientras tanto: el resultado ya no vale */
    }
    if (http == GAL_HTTP_E_CANCEL) { /* lo cortó el reloj: ni fallo ni espera larga */
        a->wait_from = now;
        a->wait_ms = 1000;
        return;
    }
    char code[40] = "";
    if (http > 0 && http != 200) {
        gal_parse_cloud_error(buf, n, code, sizeof(code), NULL, 0);
    }
    gal_sess_t v = gal_acct_classify(http, code);
    gal_me_t me;
    if (v == GAL_SESS_OK && (!gal_parse_cloud_me(buf, n, &me) || !gal_acct_valid_address(me.address))) {
        v = GAL_SESS_UNAVAILABLE; /* un 200 sin cuerpo válido no es una validación */
    }
    if (v == GAL_SESS_OK) {
        a->want_validate = false;
        a->fails = 0;
        a->last_ok = now;
        a->verified = true;
        persist_authst(a, st, AUTHST_OK);
        if (strcmp(me.address, a->address) || strcmp(me.display, a->display)) {
            gal_copyz(a->address, sizeof(a->address), me.address);
            gal_copyz(a->display, sizeof(a->display), me.display);
            put_str(st, "address", a->address);
            put_str(st, "display", a->display);
        }
        set_link(a, GAL_LINK_LINKED, "Conectada con Flex Account");
    } else if (v == GAL_SESS_AUTH || v == GAL_SESS_EXPIRED) {
        a->want_validate = false;
        a->fails = 0;
        persist_authst(a, st, v == GAL_SESS_EXPIRED ? AUTHST_EXPIRED : AUTHST_REQUIRED);
        set_link(a, v == GAL_SESS_EXPIRED ? GAL_LINK_TOKEN_EXPIRED : GAL_LINK_AUTH_REQUIRED,
                 v == GAL_SESS_EXPIRED ? "La sesión de este reloj caducó: vuelve a vincularlo"
                                       : "Flex Account ya no reconoce este reloj: vuelve a vincularlo");
    } else {
        if (a->fails < 250) {
            a->fails++;
        }
        a->wait_from = now;
        a->wait_ms = gal_acct_backoff_ms(a->fails);
        if (a->link != GAL_LINK_AUTH_REQUIRED && a->link != GAL_LINK_TOKEN_EXPIRED) {
            char d[96];
            if (http < 0) {
                snprintf(d, sizeof(d), "Sin respuesta segura (%s); se reintentará", gal_http_err_text(http));
            } else {
                snprintf(d, sizeof(d), "Flex Account respondió HTTP %d; se reintentará", http);
            }
            set_link(a, GAL_LINK_UNAVAILABLE, d);
        }
    }
}

uint32_t gal_acct_step(gal_acct_t *a, gal_conn_t *c, const gal_store_t *st, bool wifi, uint32_t now, char *scratch,
                       size_t scap)
{
    if (!scratch || scap < GAL_ACCT_SCRATCH) {
        return 1000;
    }
    if (a->want_link) {
        if (a->cancel) {
            a->want_link = false;
            finish_failed(a, GAL_ACCT_CANCELLED, "Cancelado", NULL);
            return 1000;
        }
        link_start(a, c, st, wifi, now, scratch, scap);
        return 100;
    }
    if (a->flow == GAL_ACCT_CODE) {
        link_poll(a, c, st, wifi, now, scratch, scap);
        return 250;
    }
    session_tick(a, c, st, wifi, now, scratch, scap);
    if (a->want_validate) {
        const uint32_t el = now - a->wait_from;
        return el < a->wait_ms ? a->wait_ms - el : 100;
    }
    return 5000;
}
