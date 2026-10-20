/*
 * ota_fake — ver ota_fake.h.
 */
#include "ota_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "ota_sig.h"

void ota_fake_init(ota_fake_t *f)
{
    memset(f, 0, sizeof(*f));
    f->online = true;
    f->mem_ok = true;
    f->batt_pct = 80;
    f->slot = 0x600000;
    f->fail_write_at = -1;
    f->data_ms = 10;
    f->idle_ms = 100;
    f->cur = -1;
}

void ota_fake_free(ota_fake_t *f)
{
    free(f->flash);
    f->flash = NULL;
}

ota_fake_route_t *ota_fake_route(ota_fake_t *f, const char *url, const void *body, size_t len)
{
    for (int i = 0; i < f->nr; i++) {
        if (!strcmp(f->r[i].url, url)) {
            f->r[i].body = body;
            f->r[i].len = len;
            f->r[i].clen = (int64_t)len;
            return &f->r[i];
        }
    }
    if (f->nr >= OTA_FAKE_ROUTES) {
        return NULL;
    }
    ota_fake_route_t *r = &f->r[f->nr++];
    memset(r, 0, sizeof(*r));
    snprintf(r->url, sizeof(r->url), "%s", url);
    r->body = body;
    r->len = len;
    r->clen = (int64_t)len;
    r->cut_at = OTA_FAKE_NOCUT;
    r->stall_at = OTA_FAKE_NOCUT;
    return r;
}

static uint32_t f_now(void *ctx)
{
    ota_fake_t *f = ctx;
    return f->ext_clock ? f->ext_clock() : f->now;
}

static void f_sleep(void *ctx, uint32_t ms)
{
    ota_fake_t *f = ctx;
    f->now += ms;
    f->sleeps_ms += (int)ms;
}

static bool f_online(void *ctx)
{
    return ((ota_fake_t *)ctx)->online;
}

static bool f_mem_ok(void *ctx)
{
    return ((ota_fake_t *)ctx)->mem_ok;
}

static int f_open(void *ctx, const char *url, int64_t *len)
{
    ota_fake_t *f = ctx;
    snprintf(f->last_url, sizeof(f->last_url), "%s", url);
    for (int i = 0; i < f->nr; i++) {
        if (!strcmp(f->r[i].url, url)) {
            if (f->r[i].open_err) {
                return f->r[i].open_err;
            }
            f->opens++; /* solo las que abren: un fallo al abrir no se cierra */
            f->cur = i;
            f->pos = 0;
            f->is_open = true;
            *len = f->r[i].clen;
            return 0;
        }
    }
    return OTA_ERR_HTTP_STATUS; /* 404 */
}

static int f_read(void *ctx, uint8_t *buf, int n)
{
    ota_fake_t *f = ctx;
    if (!f->is_open || f->cur < 0) {
        return -1;
    }
    const ota_fake_route_t *r = &f->r[f->cur];
    int ret;
    if (f->pos >= r->cut_at || f->pos >= r->len) {
        ret = -1;
    } else if (f->pos >= r->stall_at) {
        f->now += f->idle_ms;
        ret = 0;
    } else {
        size_t lim = r->len;
        if (r->cut_at < lim) {
            lim = r->cut_at;
        }
        if (r->stall_at < lim) {
            lim = r->stall_at;
        }
        size_t k = lim - f->pos;
        if (k > (size_t)n) {
            k = (size_t)n;
        }
        if (r->chunk > 0 && k > (size_t)r->chunk) {
            k = (size_t)r->chunk;
        }
        memcpy(buf, r->body + f->pos, k);
        f->pos += k;
        f->now += f->data_ms;
        ret = (int)k;
    }
    if (f->on_read) {
        f->on_read(f, f->ud);
    }
    return ret;
}

static void f_close(void *ctx)
{
    ota_fake_t *f = ctx;
    f->closes++;
    f->is_open = false;
    f->cur = -1;
}

static uint32_t f_slot(void *ctx)
{
    return ((ota_fake_t *)ctx)->slot;
}

static int f_begin(void *ctx, uint32_t size)
{
    ota_fake_t *f = ctx;
    f->begins++;
    if (f->fail_begin) {
        return f->fail_begin;
    }
    free(f->flash);
    f->flash = calloc(1, size);
    if (!f->flash) {
        return OTA_ERR_BEGIN;
    }
    f->fl_size = size;
    f->fl_written = 0;
    f->fl_begun = true;
    f->fl_finished = f->fl_aborted = false;
    return 0;
}

static int f_write(void *ctx, const uint8_t *d, int n)
{
    ota_fake_t *f = ctx;
    if (!f->fl_begun || f->fl_written + (uint32_t)n > f->fl_size) {
        return OTA_ERR_WRITE;
    }
    if (f->fail_write_at >= 0 && (long)(f->fl_written + (uint32_t)n) > f->fail_write_at) {
        return OTA_ERR_WRITE;
    }
    memcpy(f->flash + f->fl_written, d, (size_t)n);
    f->fl_written += (uint32_t)n;
    return 0;
}

static int f_finish(void *ctx)
{
    ota_fake_t *f = ctx;
    f->fl_begun = false;
    if (f->fail_finish) {
        return f->fail_finish;
    }
    f->fl_finished = true;
    return 0;
}

static void f_abort(void *ctx)
{
    ota_fake_t *f = ctx;
    f->aborts++;
    f->fl_begun = false;
    f->fl_aborted = true;
}

static void f_battery(void *ctx, int *pct, bool *chg)
{
    ota_fake_t *f = ctx;
    *pct = f->batt_pct;
    *chg = f->charging;
}

static void f_pend(void *ctx, const char *ver)
{
    ota_fake_t *f = ctx;
    snprintf(f->pend, sizeof(f->pend), "%s", ver);
}

static void f_lock(void *ctx)
{
    ((ota_fake_t *)ctx)->locks++;
}

static void f_unlock(void *ctx)
{
    ((ota_fake_t *)ctx)->unlocks++;
}

ota_io_t ota_fake_io(ota_fake_t *f)
{
    const ota_io_t io = {
        .ctx = f,
        .now = f_now,
        .sleep = f_sleep,
        .online = f_online,
        .mem_ok = f_mem_ok,
        .open = f_open,
        .read = f_read,
        .close = f_close,
        .slot_size = f_slot,
        .fl_begin = f_begin,
        .fl_write = f_write,
        .fl_finish = f_finish,
        .fl_abort = f_abort,
        .battery = f_battery,
        .save_pend = f_pend,
        .lock = f_lock,
        .unlock = f_unlock,
    };
    return io;
}

void ota_fake_image(uint8_t *buf, size_t n, const char *project, const char *ver, uint16_t chip)
{
    uint32_t x = 0x12345678u;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (uint8_t)(x >> 16);
    }
    if (n < OTA_HEAD_LEN) {
        return;
    }
    memset(buf, 0, OTA_HEAD_LEN);
    buf[0] = 0xE9;
    buf[1] = 4;
    buf[12] = (uint8_t)chip;
    buf[13] = (uint8_t)(chip >> 8);
    const uint32_t magic = 0xABCD5432u;
    memcpy(buf + 32, &magic, 4); /* x86 y Xtensa: little-endian */
    strncpy((char *)buf + 48, ver, 31);
    strncpy((char *)buf + 80, project, 31);
}

/* Clave de prueba: se genera al ejecutar con un generador determinista y
 * nunca sale de la memoria del proceso. */
static mbedtls_ecp_group s_grp;
static mbedtls_mpi s_d;
static mbedtls_ecp_point s_q;
static bool s_have;

static int det_rng(void *p, unsigned char *out, size_t n)
{
    uint64_t *st = p;
    for (size_t i = 0; i < n; i++) {
        *st ^= *st << 13;
        *st ^= *st >> 7;
        *st ^= *st << 17;
        out[i] = (unsigned char)*st;
    }
    return 0;
}

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;

static void ensure_key(void)
{
    if (s_have) {
        return;
    }
    mbedtls_ecp_group_init(&s_grp);
    mbedtls_mpi_init(&s_d);
    mbedtls_ecp_point_init(&s_q);
    mbedtls_ecp_group_load(&s_grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_ecp_gen_keypair(&s_grp, &s_d, &s_q, det_rng, &s_rng);
    s_have = true;
}

void ota_fake_pubkey(uint8_t pub[65])
{
    ensure_key();
    size_t olen = 0;
    mbedtls_ecp_point_write_binary(&s_grp, &s_q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, 65);
}

void ota_fake_sign(const char *ver, const uint8_t *img, size_t n, char sighex[OTA_SIG_HEX + 1])
{
    ensure_key();
    uint8_t sha[32], hash[32], sig[64];
    ota_sha256(img, n, sha);
    char msg[160];
    const size_t mn = ota_sign_message(OTA_PRODUCT, ver, (uint32_t)n, sha, msg, sizeof(msg));
    ota_sha256((const uint8_t *)msg, mn, hash);
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecdsa_sign(&s_grp, &r, &s, &s_d, hash, sizeof(hash), det_rng, &s_rng);
    mbedtls_mpi_write_binary(&r, sig, 32);
    mbedtls_mpi_write_binary(&s, sig + 32, 32);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    ota_hex_encode(sig, sizeof(sig), sighex);
}

size_t ota_fake_manifest(char *out, size_t cap, const char *ver, const char *url, uint32_t size, const char *md5,
                         const char *sig, const char *changelog)
{
    int n = snprintf(out, cap, "{\n  \"%s\": {\n    \"version\": \"%s\",\n    \"url\": \"%s\"", OTA_PRODUCT, ver, url);
    if (size) {
        n += snprintf(out + n, cap - (size_t)n, ",\n    \"size\": %lu", (unsigned long)size);
    }
    if (md5) {
        n += snprintf(out + n, cap - (size_t)n, ",\n    \"md5\": \"%s\"", md5);
    }
    if (changelog) {
        n += snprintf(out + n, cap - (size_t)n, ",\n    \"changelog\": \"%s\"", changelog);
    }
    if (sig) {
        n += snprintf(out + n, cap - (size_t)n, ",\n    \"sig\": \"%s\",\n    \"sigalg\": \"%s\"", sig, OTA_SIGALG);
    }
    n += snprintf(out + n, cap - (size_t)n, "\n  }\n}\n");
    return (size_t)n;
}
