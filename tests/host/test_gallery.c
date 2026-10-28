/*
 * Pruebas de PC de la Galería (watch_gallery): lector JSON, cliente HTTP con
 * respuestas troceadas, cortadas y conexiones viejas, reglas del modo
 * Automático, lectores de los contratos, errores y reintentos, decodificación
 * JPEG por flujo con memoria acotada, Flex Account con sobres firmados de
 * verdad (ECDSA P-256 con una clave de pruebas) y el MOTOR entero contra
 * Flex OS Ultra y Flex Cloud en memoria (gal_fake_eco): emparejar, rechazos,
 * caídas y vuelta, listas, miniaturas, visor, descargas cortadas que
 * reanudan con Range, bytes corruptos, subidas por partes reanudables,
 * duplicados, papelera y memoria liberada.
 *
 * Binario aparte (necesita el TJpgDec de LVGL): make -C tests/host gallery LVGL_DIR=...
 */
#define _DEFAULT_SOURCE /* mkdtemp */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gal_wm.h"

#include "flexfs.h"
#include "gal_acct.h"
#include "gal_core.h"
#include "gal_engine.h"
#include "gal_fake.h"
#include "gal_fake_eco.h"
#include "gal_http.h"
#include "gal_jpeg.h"
#include "gal_json.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "ota_sig.h"

int g_checks, g_fail;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); g_checks++; if (_a != _b) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s = %ld, esperado %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); g_checks++; if (strcmp(_a, _b)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s = \"%s\", esperado \"%s\"\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static const char *FIX = "fixtures";

/* ======================= memoria contada ======================= */

static size_t s_cur, s_peak, s_allocs;
static size_t s_limit; /* > 0: falla lo que pase de aquí (sin memoria) */

static void *m_alloc(size_t n)
{
    if (s_limit && s_cur + n > s_limit) {
        return NULL;
    }
    size_t *p = malloc(n + 16);
    if (!p) {
        return NULL;
    }
    p[0] = n;
    s_cur += n;
    s_allocs++;
    if (s_cur > s_peak) {
        s_peak = s_cur;
    }
    return (uint8_t *)p + 16;
}

static void m_free(void *q)
{
    if (!q) {
        return;
    }
    size_t *p = (size_t *)((uint8_t *)q - 16);
    s_cur -= p[0];
    free(p);
}

/* ======================= JSON ======================= */

static void test_json(void)
{
    const char *j = "{\"id\":\"fil_1\",\"folder\":{\"id\":\"fld_9\"},\"n\":\"Caf\\u00e9 \\ud83d\\ude00\",\"s\":12,"
                    "\"neg\":-3,\"b\":true,\"z\":0,\"x\":null,\"arr\":[1,{\"id\":2},\"tres\"]}";
    const char *e = j + strlen(j);
    char s[64];
    uint64_t u;
    int64_t i;
    bool b;
    CHECK(gal_j_valid(j, e));
    CHECK(gal_j_get_str(j, e, "id", s, sizeof(s)));
    CHECK_STR(s, "fil_1"); /* el de primer nivel, no el de "folder" */
    CHECK(gal_j_get_str(j, e, "n", s, sizeof(s)));
    CHECK_STR(s, "Café \xF0\x9F\x98\x80");
    CHECK(gal_j_get_u64(j, e, "s", &u) && u == 12);
    CHECK(!gal_j_get_u64(j, e, "neg", &u));
    CHECK(gal_j_get_i64(j, e, "neg", &i) && i == -3);
    CHECK(gal_j_get_bool(j, e, "b", &b) && b);
    CHECK(gal_j_get_bool(j, e, "z", &b) && !b); /* el P4 usa 0/1 */
    CHECK(gal_j_is_null(gal_j_get(j, e, "x", NULL), e));
    CHECK(!gal_j_get(j, e, "nope", NULL));
    const char *as, *ae;
    CHECK(gal_j_obj(j, e, "arr", &as, &ae));
    int n = 0;
    for (const char *it = gal_j_first(as, ae); it; it = gal_j_next(it, ae)) {
        n++;
    }
    CHECK_EQ(n, 3);
    /* hostil: truncado, sin cerrar, escapes malos, control sin escapar, profundidad */
    const char *bad[] = {"{\"a\":1", "{\"a\":\"x}", "{\"a\":\"\\q\"}", "{\"a\":\"\x01\"}", "{\"a\":01x}",
                         "{\"a\":\"\\ud800\"}", "{\"a\":\"\\u0000\"}", "[1,2,", "{\"a\" 1}", ""};
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        const char *be = bad[k] + strlen(bad[k]);
        CHECK(!gal_j_valid(bad[k], be) || !gal_j_get_str(bad[k], be, "a", s, sizeof(s)));
    }
    char deep[200];
    memset(deep, '[', 100);
    memset(deep + 100, ']', 99);
    deep[199] = 0;
    CHECK(!gal_j_valid(deep, deep + strlen(deep))); /* tope de profundidad: no hay recursión sin fin */
    /* recorte sin partir un carácter */
    CHECK(gal_j_get_str(j, e, "n", s, 6));
    CHECK_STR(s, "Café"); /* "Café" son 5 bytes; el emoji no cabe entero */
    CHECK(gal_j_quote("a\"b\n\\c", s, sizeof(s)));
    CHECK_STR(s, "\"a\\\"b\\n\\\\c\"");
    char big[8];
    CHECK(!gal_j_quote("demasiado largo", big, sizeof(big)));
}

/* ======================= HTTP ======================= */

typedef struct {
    int mode;
    int hits;
} h_ud_t;

static void h_handler(void *ud, const gf_req_t *rq, gf_resp_t *rs)
{
    h_ud_t *u = ud;
    u->hits++;
    switch (u->mode) {
    case 0: gf_json(rs, 200, "{\"path\":\"%s\"}", rq->path); break;
    case 1: gf_json(rs, 200, "{\"chunked\":\"%s\",\"pad\":\"0123456789abcdef\"}", rq->method); rs->chunked = true; break;
    case 2: gf_json(rs, 200, "{\"cortada\":true,\"relleno\":\"aaaaaaaaaaaaaaaaaaaa\"}"); rs->cut_after = u->hits & 1 ? 40 : 100; break;
    case 3: rs->stall = true; rs->status = 200; break;
    case 4: gf_json(rs, 200, "{\"hasta\":\"el cierre\"}"); rs->no_length = true; break;
    case 5: gf_bytes(rs, 200, "text/plain", "ok", 2); snprintf(rs->extra, sizeof(rs->extra), "Content-Length: 3\r\n"); break;
    case 6: gf_bytes(rs, 206, "image/jpeg", "abcdef", 6);
        snprintf(rs->extra, sizeof(rs->extra), "Content-Range: bytes 10-15/16\r\nETag: \"abc123\"\r\nSet-Cookie: fxs=0123456789abcdef0123456789abcdef; Path=/; HttpOnly\r\nRetry-After: 7\r\n");
        break;
    default: gf_json(rs, 500, "{}");
    }
}

static void test_http(void)
{
    uint32_t clock = 1000;
    h_ud_t u = {0};
    gf_conn_t fc;
    gf_init(&fc, h_handler, &u, &clock);
    const gal_io_t io = gf_io(&fc);
    gal_conn_t *c = malloc(sizeof(*c));
    gal_conn_init(c, &io);
    gal_req_t rq = {.method = "GET", .host = "192.168.1.20", .port = 8080, .path = "/api/hello", .range_from = -1,
                    .agent = GAL_AGENT, .cookie = "fxs=abc", .xflex = false};
    gal_resp_t rs;
    char buf[512];
    size_t n;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    CHECK_EQ(gal_http_read_all(c, buf, sizeof(buf), &n), 0);
    gal_http_end(c);
    CHECK_STR(buf, "{\"path\":\"/api/hello\"}");
    CHECK_STR(fc.last.host, "192.168.1.20:8080"); /* el Host que exige el P4 */
    CHECK_STR(fc.last.cookie, "fxs=abc");
    CHECK(strstr(fc.last.agent, "ESP32-S3") != NULL); /* el reloj se distingue del P4 */
    /* keep-alive: la segunda por la misma conexión */
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    gal_http_read_all(c, buf, sizeof(buf), &n);
    gal_http_end(c);
    CHECK_EQ(fc.connects, 1);
    /* ociosa más de GAL_HTTP_IDLE_MS: se abre otra */
    clock += GAL_HTTP_IDLE_MS + 10;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    gal_http_read_all(c, buf, sizeof(buf), &n);
    gal_http_end(c);
    CHECK_EQ(fc.connects, 2);
    /* el servidor cerró la conexión sin decirlo: se repite UNA vez por una nueva */
    fc.server_closes_idle = true;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    gal_http_read_all(c, buf, sizeof(buf), &n);
    gal_http_end(c);
    const int before = u.hits;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    gal_http_read_all(c, buf, sizeof(buf), &n);
    gal_http_end(c);
    CHECK_EQ(u.hits, before + 1); /* el servidor la ve una vez: no se duplica */
    fc.server_closes_idle = false;
    /* chunked con extensiones y remolque */
    u.mode = 1;
    rq.method = "POST";
    rq.body = (const uint8_t *)"x";
    rq.body_len = 1;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    CHECK(rs.chunked);
    CHECK_EQ(gal_http_read_all(c, buf, sizeof(buf), &n), 0);
    gal_http_end(c);
    CHECK_STR(buf, "{\"chunked\":\"POST\",\"pad\":\"0123456789abcdef\"}");
    rq.method = "GET";
    rq.body = NULL;
    rq.body_len = 0;
    /* cortada en las cabeceras y a mitad del cuerpo: incompleta, nunca "bien" */
    u.mode = 2;
    u.hits = 0;
    gal_conn_close(c); /* por una conexión nueva: en una reutilizada se repetiría (no se distingue de una caducada) */
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_CLOSED);
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    CHECK_EQ(gal_http_read_all(c, buf, sizeof(buf), &n), GAL_HTTP_E_CLOSED);
    gal_http_end(c);
    CHECK(!c->open);
    /* sin respuesta: plazo */
    u.mode = 3;
    rq.timeout_ms = 2000;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_TIMEOUT);
    rq.timeout_ms = 0;
    /* hasta el cierre */
    u.mode = 4;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    CHECK_EQ(gal_http_read_all(c, buf, sizeof(buf), &n), 0);
    gal_http_end(c);
    CHECK_STR(buf, "{\"hasta\":\"el cierre\"}");
    /* dos Content-Length distintos: no se adivina */
    u.mode = 5;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_PROTO);
    /* cabeceras: Content-Range, ETag sin comillas, cookie de sesión, Retry-After */
    u.mode = 6;
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 206);
    CHECK(rs.has_range && rs.range_start == 10 && rs.range_end == 15 && rs.range_total == 16);
    CHECK_STR(rs.etag, "abc123");
    CHECK(rs.cookie_set);
    CHECK_STR(rs.cookie, "0123456789abcdef0123456789abcdef");
    CHECK_EQ(rs.retry_after_s, 7);
    gal_http_end(c);
    /* lo que no cabe en el búfer de JSON */
    u.mode = 0;
    rq.path = "/0123456789012345678901234567890123456789";
    CHECK_EQ(gal_http_begin(c, &rq, &rs), 200);
    CHECK_EQ(gal_http_read_all(c, buf, 16, &n), GAL_HTTP_E_TOOBIG);
    gal_http_end(c);
    /* inyección de cabeceras: no sale nada */
    rq.path = "/api/hello";
    rq.cookie = "fxs=a\r\nX-Evil: 1";
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_ARG);
    rq.cookie = NULL;
    rq.path = "/a b";
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_ARG);
    rq.path = "/api/hello";
    /* conexión rechazada y DNS */
    fc.fail_connect = 1;
    fc.fail_code = GAL_IO_E_DNS;
    gal_conn_close(c);
    CHECK_EQ(gal_http_begin(c, &rq, &rs), GAL_HTTP_E_DNS);
    CHECK_STR(gal_http_err_text(GAL_HTTP_E_DNS), "No se encuentra el servidor (DNS)");
    char enc[64];
    CHECK(gal_url_encode("Foto ñ/1.jpg", enc, sizeof(enc)));
    CHECK_STR(enc, "Foto%20%C3%B1%2F1.jpg");
    gal_conn_close(c);
    free(c);
    gf_free(&fc);
}

/* ======================= modelo y Automático ======================= */

static void test_pick(void)
{
    gal_health_t h[GAL_SRC_N];
    gal_choice_t ch;
    memset(h, 0, sizeof(h));
    /* sin Wi-Fi: nada remoto */
    gal_pick_resolve(GAL_PICK_AUTO, h, false, GAL_SRC_NONE, 0, &ch);
    CHECK_EQ(ch.src, GAL_SRC_NONE);
    CHECK(strstr(ch.why, "Sin Wi-Fi") != NULL);
    /* nada configurado: qué hacer */
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_NONE, 0, &ch);
    CHECK_EQ(ch.src, GAL_SRC_NONE);
    CHECK(strstr(ch.why, "Origen") != NULL);
    /* explícita sin configurar: esa, con su motivo; nunca otra */
    h[GAL_SRC_CLOUD].state = GAL_ST_READY;
    gal_pick_resolve(GAL_PICK_ULTRA, h, true, GAL_SRC_NONE, 0, &ch);
    CHECK_EQ(ch.src, GAL_SRC_ULTRA);
    CHECK(strstr(ch.why, "código") != NULL);
    gal_pick_resolve(GAL_PICK_PHONE, h, true, GAL_SRC_NONE, 0, &ch);
    CHECK_EQ(ch.src, GAL_SRC_PHONE);
    /* Automático: el P4 lista primero (red local) */
    gal_health_ok(&h[GAL_SRC_ULTRA], 1000, 30000);
    gal_health_ok(&h[GAL_SRC_CLOUD], 1000, 30000);
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_NONE, 20000, &ch);
    CHECK_EQ(ch.src, GAL_SRC_ULTRA);
    CHECK(ch.automatic);
    CHECK_STR(ch.why, "Automático: Flex OS Ultra");
    /* se cae el P4: Flex Cloud */
    gal_health_fail(&h[GAL_SRC_ULTRA], GAL_ST_DOWN, "no responde", 21000);
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_ULTRA, 21000, &ch);
    CHECK_EQ(ch.src, GAL_SRC_CLOUD);
    /* vuelve el P4: la nube se conserva hasta que el P4 aguante 10 s */
    gal_health_ok(&h[GAL_SRC_ULTRA], 30000, 30000);
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_CLOUD, 32000, &ch);
    CHECK_EQ(ch.src, GAL_SRC_CLOUD);
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_CLOUD, 30000 + GAL_AUTO_STABLE_MS, &ch);
    CHECK_EQ(ch.src, GAL_SRC_ULTRA);
    /* ninguna lista: la que se está comprobando, luego la configurada con su motivo */
    gal_health_fail(&h[GAL_SRC_ULTRA], GAL_ST_DOWN, "Flex OS Ultra no responde", 50000);
    h[GAL_SRC_CLOUD].state = GAL_ST_CHECKING;
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_ULTRA, 50000, &ch);
    CHECK_EQ(ch.src, GAL_SRC_CLOUD);
    CHECK(strstr(ch.why, "Conectando") != NULL);
    h[GAL_SRC_CLOUD].state = GAL_ST_UNSET;
    gal_pick_resolve(GAL_PICK_AUTO, h, true, GAL_SRC_ULTRA, 50000, &ch);
    CHECK_EQ(ch.src, GAL_SRC_ULTRA);
    CHECK(strstr(ch.why, "no responde") != NULL);
    /* espera creciente y limitada */
    CHECK_EQ(gal_backoff_ms(1), 2000);
    CHECK_EQ(gal_backoff_ms(2), 4000);
    CHECK_EQ(gal_backoff_ms(5), 32000);
    CHECK_EQ(gal_backoff_ms(6), 60000);
    CHECK_EQ(gal_backoff_ms(200), 60000);
    /* errores */
    CHECK_EQ(gal_err_from_http(401, "device_revoked"), GAL_E_AUTH);
    CHECK_EQ(gal_err_from_http(401, ""), GAL_E_SERVER); /* página de un proxy: no es una sesión perdida */
    CHECK_EQ(gal_err_from_http(401, NULL), GAL_E_AUTH);  /* el P4: sin sesión */
    CHECK_EQ(gal_err_from_http(507, "quota_exceeded"), GAL_E_QUOTA);
    CHECK_EQ(gal_err_from_http(422, "checksum_mismatch"), GAL_E_INTEGRITY);
    CHECK_EQ(gal_err_from_http(503, NULL), GAL_E_BUSY);
    CHECK_EQ(gal_err_from_http(GAL_HTTP_E_TIMEOUT, NULL), GAL_E_NET);
    CHECK_EQ(gal_err_from_http(GAL_HTTP_E_TLS, NULL), GAL_E_TLS);
    CHECK(gal_err_retry(GAL_E_NET) && gal_err_retry(GAL_E_BUSY) && !gal_err_retry(GAL_E_QUOTA) && !gal_err_retry(GAL_E_AUTH));
    /* formatos */
    CHECK_EQ(gal_fmt_from_name("IMG_1.JPG"), GAL_F_JPEG);
    CHECK_EQ(gal_fmt_from_mime("image/heic"), GAL_F_HEIC);
    CHECK_EQ(gal_fmt_from_ultra("JPEG progresivo"), GAL_F_JPEG_PROG);
    CHECK(!gal_fmt_viewable(GAL_F_PNG) && gal_fmt_viewable(GAL_F_JPEG));
    CHECK(strstr(gal_fmt_why(GAL_F_VIDEO), "AVI MJPEG") != NULL); /* el reloj reproduce AVI MJPEG: lo dice */
    /* nombres seguros para LittleFS */
    char nm[64];
    gal_safe_name("../../etc/pass@wd.jpg", nm, sizeof(nm));
    CHECK(!strchr(nm, '/') && !strchr(nm, '@') && nm[0] != '.');
    gal_safe_name("", nm, sizeof(nm));
    CHECK_STR(nm, "Foto.jpg");
    gal_safe_name("una foto con un nombre larguísimo que no cabe en LittleFS del reloj.jpeg", nm, sizeof(nm));
    CHECK(strlen(nm) < 48 && !strcmp(fl_ext(nm), ".jpeg"));
    gal_name_variant("playa.jpg", 2, nm, sizeof(nm));
    CHECK_STR(nm, "playa (2).jpg");
    char cp[8];
    gal_copyz(cp, 7, "Galería"); /* el corte caería en medio de la "í": se corta antes */
    CHECK_STR(cp, "Galer");
    gal_copyz(cp, 3, "añ");
    CHECK_STR(cp, "a");
    gal_copyz(cp, 5, "añoñ"); /* "a" + "ñ" + "o" caben justos */
    CHECK_STR(cp, "año");
    gal_copyz(cp, sizeof(cp), NULL);
    CHECK_STR(cp, "");
    char sz[24];
    gal_size_text(3565158, sz, sizeof(sz));
    CHECK_STR(sz, "3,4 MB");
    gal_size_text(5ull * 1024 * 1024 * 1024, sz, sizeof(sz));
    CHECK_STR(sz, "5,0 GB");
    CHECK_EQ(gal_crc32(0, "123456789", 9), 0xCBF43926u); /* el vector de zlib */
    CHECK_EQ(gal_chunk_for(1745424, 65536, 67108864), 262144);
    CHECK(gal_chunk_for(200ull * 1024 * 1024, 65536, 67108864) == 524288); /* no pasa de 512 partes */
    CHECK_EQ(gal_chunk_for(10ull * 1024 * 1024 * 1024, 65536, 16u * 1024 * 1024), 0); /* no cabe en el teléfono */
}

static void test_parsers(void)
{
    gal_item_t it[8];
    uint32_t rev = 0;
    bool same = false;
    /* exactamente lo que escribe flexMlJsonItem */
    const char *lib = "{\"rev\":7,\"owner\":0,\"lockType\":1,\"free\":1,\"total\":2,\"reserve\":3,\"up\":1,"
                      "\"lim\":{\"photo\":6291456,\"video\":8388608,\"audio\":8388608},\"items\":["
                      "{\"id\":3,\"k\":\"photo\",\"s\":1000,\"c\":1790000000,\"a\":1790000100,\"m\":1790000100,\"w\":640,"
                      "\"h\":480,\"d\":0,\"st\":\"ready\",\"e\":0,\"p\":1,\"lock\":0,\"t\":2,\"fmt\":\"JPEG\",\"n\":\"Playa\","
                      "\"fn\":\"playa.jpg\"},{\"id\":4,\"lock\":1},"
                      "{\"id\":5,\"k\":\"audio\",\"s\":9,\"st\":\"ready\",\"fmt\":\"MP3\",\"n\":\"x\",\"fn\":\"x.mp3\"},"
                      "{\"id\":6,\"k\":\"video\",\"s\":5,\"c\":0,\"a\":1790000200,\"st\":\"proc\",\"fmt\":\"MP4\",\"n\":\"v\",\"fn\":\"v.mp4\"}]}";
    int n = gal_parse_ultra_library(lib, strlen(lib), it, 8, &rev, &same);
    CHECK_EQ(n, 3);
    CHECK_EQ(rev, 7);
    CHECK_STR(it[0].id, "3");
    CHECK_STR(it[0].name, "Playa");
    CHECK(it[0].fmt == GAL_F_JPEG && (it[0].flags & GAL_I_THUMB) && it[0].tver == 2 && it[0].w == 640);
    CHECK_EQ(it[0].when, 1790000000LL * 1000);
    CHECK(it[1].flags & GAL_I_LOCKED);
    CHECK_STR(it[1].name, "Protegido"); /* ni nombre ni clase de lo protegido */
    CHECK(it[2].kind == GAL_K_VIDEO && it[2].fmt == GAL_F_VIDEO && (it[2].flags & GAL_I_BUSY));
    const char *sm = "{\"rev\":7,\"same\":1,\"owner\":0}";
    CHECK_EQ(gal_parse_ultra_library(sm, strlen(sm), it, 8, &rev, &same), 0);
    CHECK(same);
    CHECK(gal_parse_ultra_library("{\"rev\":1,\"items\":[", 18, it, 8, &rev, &same) < 0);
    gal_hello_t h;
    const char *hj = "{\"paired\":1,\"owner\":0,\"lockType\":2,\"up\":1,\"dev\":\"Flex OS Ultra\"}";
    CHECK(gal_parse_hello(hj, strlen(hj), &h) && gal_hello_is_ultra(&h) && h.paired && h.lock_type == 2);
    const char *other = "{\"paired\":1,\"dev\":\"Router\"}";
    CHECK(gal_parse_hello(other, strlen(other), &h) && !gal_hello_is_ultra(&h));
    /* exactamente lo que escribe fileView de Flex Cloud */
    const char *cl = "{\"ok\":true,\"items\":[{\"type\":\"folder\",\"id\":\"fld_aaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"F\"},"
                     "{\"type\":\"file\",\"id\":\"fil_abcdefghijklmnopqrstuvwx\",\"name\":\"Ni\\u00f1a.jpg\",\"parentId\":null,"
                     "\"size\":2048,\"mime\":\"image/jpeg\",\"kind\":\"photo\",\"sha256\":"
                     "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"version\":1,\"status\":\"ready\","
                     "\"storageLocation\":\"r2\",\"source\":\"device\",\"hasThumbnail\":true,\"metadata\":{\"width\":4000,"
                     "\"height\":3000},\"createdAt\":1790000000000,\"updatedAt\":1790000500000},"
                     "{\"type\":\"file\",\"id\":\"fil_x\\\"/../\",\"name\":\"mal\",\"kind\":\"photo\"},"
                     "{\"type\":\"file\",\"id\":\"fil_doc\",\"name\":\"d.pdf\",\"kind\":\"document\"}],\"nextCursor\":\"60\"}";
    char cur[16];
    n = gal_parse_cloud_list(cl, strlen(cl), GAL_SRC_CLOUD, it, 8, cur, sizeof(cur));
    CHECK_EQ(n, 1); /* ni carpetas, ni documentos, ni un id que se saldría de la ruta */
    CHECK_STR(it[0].name, "Niña.jpg");
    CHECK(it[0].w == 4000 && it[0].size == 2048 && (it[0].flags & GAL_I_THUMB) && strlen(it[0].sha256) == 64);
    CHECK_STR(cur, "60");
    gal_me_t me;
    const char *mj = "{\"ok\":true,\"account\":{\"id\":\"acc\",\"flexAddress\":\"ana@flex\",\"displayName\":\"Ana\"},"
                     "\"device\":{\"id\":\"d\",\"label\":\"x\"},\"quota\":{\"totalBytes\":5368709120,\"usedBytes\":10,"
                     "\"availableBytes\":5368709110,\"state\":\"ok\"},\"limits\":{\"maxFileBytes\":1,\"chunkDefault\":8388608,"
                     "\"chunkMin\":65536,\"chunkMax\":67108864}}";
    CHECK(gal_parse_cloud_me(mj, strlen(mj), &me) && me.q_total == 5368709120ull && me.chunk_min == 65536);
    CHECK_STR(me.address, "ana@flex");
    char code[32], msg[64];
    const char *ej = "{\"ok\":false,\"error\":{\"code\":\"quota_exceeded\",\"message\":\"Sin espacio\",\"details\":{}}}";
    CHECK(gal_parse_cloud_error(ej, strlen(ej), code, sizeof(code), msg, sizeof(msg)));
    CHECK_STR(code, "quota_exceeded");
    CHECK(!gal_parse_cloud_error("<html>", 6, code, sizeof(code), msg, sizeof(msg)));
    gal_upload_t up;
    const char *uj = "{\"ok\":true,\"upload\":{\"uploadId\":\"upl_abcdefghijklmnopqrstuvwx\",\"chunkSize\":262144,"
                     "\"totalParts\":7,\"receivedParts\":[1,3,3,9],\"state\":\"active\",\"resumed\":true}}";
    CHECK(gal_parse_cloud_upload(uj, strlen(uj), &up));
    CHECK(up.parts == 7 && up.got_n == 2 && gal_upload_has(&up, 1) && !gal_upload_has(&up, 2) && gal_upload_has(&up, 3));
    CHECK(up.resumed);
    gal_phone_t ph;
    const char *oj = "{\"local\":{},\"phone\":{\"state\":\"ready\",\"name\":\"Pixel\",\"model\":\"Pixel 8\"},\"cloud\":null}";
    CHECK(gal_parse_overview(oj, strlen(oj), &ph));
    CHECK_STR(ph.state, "ready");
    /* vista: una copia en el reloj sale una vez en "Todas" */
    gal_item_t loc[2], rem[2];
    memset(loc, 0, sizeof(loc));
    memset(rem, 0, sizeof(rem));
    snprintf(loc[0].path, sizeof(loc[0].path), "/Imagenes/a.jpg");
    loc[0].size = 10;
    loc[0].when = 500;
    snprintf(loc[1].path, sizeof(loc[1].path), "/Imagenes/b.jpg");
    loc[1].size = 20;
    loc[1].when = 100;
    rem[0].src = GAL_SRC_CLOUD;
    snprintf(rem[0].id, sizeof(rem[0].id), "fil_a");
    rem[0].when = 900;
    rem[1].src = GAL_SRC_CLOUD;
    snprintf(rem[1].id, sizeof(rem[1].id), "fil_b");
    rem[1].when = 300;
    gal_origin_t tab[1] = {{.src = GAL_SRC_CLOUD, .id = "fil_a", .name = "a.jpg", .size = 10}};
    gal_origin_apply(loc, 2, tab, 1);
    CHECK((loc[0].flags & GAL_I_REMOTE) && !(loc[1].flags & GAL_I_REMOTE));
    gal_ref_t refs[8];
    n = gal_view_build(loc, 2, rem, 2, GAL_TAB_ALL, refs, 8);
    CHECK_EQ(n, 3);
    CHECK(rem[0].flags & GAL_I_ONWATCH);
    CHECK(refs[0].list == 0 && refs[0].idx == 0); /* la copia (500) antes que fil_b (300) y que b (100) */
    CHECK(refs[1].list == 1 && refs[1].idx == 1);
    CHECK_EQ(gal_view_build(loc, 2, rem, 2, GAL_TAB_REMOTE, refs, 8), 2);
    CHECK_EQ(gal_view_build(loc, 2, rem, 2, GAL_TAB_WATCH, refs, 8), 2);
    char line[256];
    gal_origin_t o;
    CHECK(gal_origin_format(&tab[0], line, sizeof(line)));
    CHECK(gal_origin_parse(line, &o) && o.src == GAL_SRC_CLOUD && o.size == 10);
    CHECK_STR(o.name, "a.jpg");
    CHECK(!gal_origin_parse("9\tx\t1\ta.jpg", &o));
    CHECK(!gal_origin_parse("2\tx\t1\t../a.jpg/b", &o));
}

/* ======================= JPEG ======================= */

typedef struct {
    const uint8_t *d;
    size_t n, off;
    size_t max_read;
} mem_src_t;

static int mem_read(void *ud, uint8_t *b, size_t n)
{
    mem_src_t *s = ud;
    if (n > s->max_read && s->max_read) {
        n = s->max_read;
    }
    const size_t k = s->n - s->off < n ? s->n - s->off : n;
    memcpy(b, s->d + s->off, k);
    s->off += k;
    return (int)k;
}

static uint16_t s_canvas[GAL_VIEW_W * GAL_VIEW_H];

static uint16_t *fixed_dst(void *ud, int sw, int sh, int *dw, int *dh)
{
    const int *wh = ud;
    (void)sw;
    (void)sh;
    *dw = wh[0];
    *dh = wh[1];
    return s_canvas;
}

static int decode_file(const char *name, gal_fit_t fit, int w, int h, gal_jpeg_info_t *info, size_t *peak)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", FIX, name);
    size_t n;
    uint8_t *d = gf_read_file(p, &n);
    if (!d) {
        fprintf(stderr, "falta la fija %s\n", p);
        return -99;
    }
    mem_src_t ms = {.d = d, .n = n, .max_read = 333}; /* a trozos raros, como llega por la red */
    const int wh[2] = {w, h};
    const size_t base = s_cur;
    s_peak = s_cur;
    gal_jpeg_src_t src = {.read = mem_read, .ud = &ms, .alloc = m_alloc, .free = m_free};
    const int r = gal_jpeg_decode(&src, fit, fixed_dst, (void *)wh, 0x1234, info);
    if (peak) {
        *peak = s_peak - base;
    }
    free(d);
    return r;
}

static uint16_t px_at(int w, int x, int y)
{
    return s_canvas[y * w + x];
}

static bool near(uint16_t p, int r, int g, int b)
{
    const int pr = (p >> 11) << 3, pg = ((p >> 5) & 63) << 2, pb = (p & 31) << 3;
    return abs(pr - r) < 40 && abs(pg - g) < 40 && abs(pb - b) < 40;
}

static void test_jpeg(void)
{
    gal_jpeg_info_t in;
    size_t peak;
    const int T = GAL_THUMB_SIDE;
    /* miniatura cuadrada (recorte centrado) de los cuatro cuadrantes */
    CHECK_EQ(decode_file("gal_quad_640x480.jpg", GAL_FIT_COVER, T, T, &in, &peak), GAL_JPG_OK);
    CHECK(in.src_w == 640 && in.src_h == 480);
    CHECK(near(px_at(T, 10, 10), 255, 0, 0));        /* rojo arriba a la izquierda: los canales en su sitio */
    CHECK(near(px_at(T, T - 10, 10), 0, 255, 0));    /* verde */
    CHECK(near(px_at(T, 10, T - 10), 0, 0, 255));    /* azul */
    CHECK(near(px_at(T, T - 10, T - 10), 255, 255, 255));
    CHECK(peak < GAL_JPEG_WORK + 64 * 1024); /* ni la foto ni sus píxeles enteros: banda + trabajo */
    /* visor: encaje sin ampliar, centrado, con bandas del color de fondo */
    CHECK_EQ(decode_file("gal_tall_300x900.jpg", GAL_FIT_CONTAIN, GAL_VIEW_W, GAL_VIEW_H, &in, &peak), GAL_JPG_OK);
    CHECK(in.h == GAL_VIEW_H && in.w == 167 && in.x == (GAL_VIEW_W - 167) / 2);
    CHECK_EQ(px_at(GAL_VIEW_W, 2, 250), 0x1234);
    /* más pequeña que el lienzo: ampliación sin huecos */
    CHECK_EQ(decode_file("gal_tiny_40x30.jpg", GAL_FIT_COVER, T, T, &in, NULL), GAL_JPG_OK);
    CHECK(near(px_at(T, 5, 5), 255, 0, 0) && near(px_at(T, T - 5, T - 5), 255, 255, 255));
    /* progresivo: rechazado con su motivo, como el P4 */
    CHECK_EQ(decode_file("gal_prog_200x150.jpg", GAL_FIT_COVER, T, T, &in, NULL), GAL_JPG_E_PROGRESSIVE);
    CHECK_STR(gal_jpeg_err_text(GAL_JPG_E_PROGRESSIVE), "JPEG progresivo: el reloj solo muestra JPEG baseline");
    /* truncado y basura */
    char p[256];
    snprintf(p, sizeof(p), "%s/gal_quad_640x480.jpg", FIX);
    size_t n;
    uint8_t *d = gf_read_file(p, &n);
    mem_src_t ms = {.d = d, .n = n / 2};
    const int wh[2] = {T, T};
    gal_jpeg_src_t src = {.read = mem_read, .ud = &ms, .alloc = m_alloc, .free = m_free};
    const size_t before = s_cur;
    CHECK(gal_jpeg_decode(&src, GAL_FIT_COVER, fixed_dst, (void *)wh, 0, &in) != GAL_JPG_OK);
    CHECK_EQ(s_cur, before); /* ni un byte perdido al fallar */
    uint8_t junk[300];
    for (size_t i = 0; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 7 + 3);
    }
    ms = (mem_src_t){.d = junk, .n = sizeof(junk)};
    const int jr = gal_jpeg_decode(&src, GAL_FIT_COVER, fixed_dst, (void *)wh, 0, &in);
    CHECK(jr == GAL_JPG_E_FORMAT || jr == GAL_JPG_E_INPUT); /* sin SOI: TJpgDec busca hasta el final */
    /* sin memoria: lo dice, no se rompe */
    ms = (mem_src_t){.d = d, .n = n};
    s_limit = s_cur + 1024;
    CHECK_EQ(gal_jpeg_decode(&src, GAL_FIT_COVER, fixed_dst, (void *)wh, 0, &in), GAL_JPG_E_NOMEM);
    s_limit = 0;
    CHECK_EQ(s_cur, before);
    free(d);
    /* reducir un lienzo por áreas (el "Ajustar" del visor a partir del 2x) */
    uint16_t a[6] = {0xF800, 0xF800, 0x001F, 0xF800, 0xF800, 0x001F}, o[2];
    gal_rgb565_shrink(a, 3, 2, o, 2, 1);
    CHECK(near(o[0], 255, 0, 0)); /* la mitad izquierda es roja */
    CHECK(((o[1] >> 11) & 31) > 8 && (o[1] & 31) > 8); /* la derecha mezcla rojo y azul */
    int fw, fh;
    gal_jpeg_fit(4000, 3000, GAL_VIEW_MAX_W, GAL_VIEW_MAX_H, &fw, &fh);
    CHECK(fw == 820 && fh == 615);
    gal_jpeg_fit(100, 50, 410, 502, &fw, &fh);
    CHECK(fw == 100 && fh == 50); /* nunca se amplía en el visor */
}

/* ======================= Flex Account ======================= */

static mbedtls_ecdsa_context s_key;
static uint8_t s_pub[65];
static const char *KID = "kid-de-pruebas";

static int rng(void *ctx, unsigned char *out, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; i++) {
        out[i] = (unsigned char)rand();
    }
    return 0;
}

static void keygen(void)
{
    mbedtls_ecdsa_init(&s_key);
    mbedtls_ecdsa_genkey(&s_key, MBEDTLS_ECP_DP_SECP256R1, rng, NULL);
    size_t ol = 0;
    mbedtls_ecp_point_write_binary(&s_key.MBEDTLS_PRIVATE(grp), &s_key.MBEDTLS_PRIVATE(Q), MBEDTLS_ECP_PF_UNCOMPRESSED,
                                   &ol, s_pub, sizeof(s_pub));
}

static void b64url(const uint8_t *d, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t acc = 0;
    int bits = 0;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        acc = acc << 8 | d[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out[w++] = T[(acc >> bits) & 63];
        }
    }
    if (bits) {
        out[w++] = T[(acc << (6 - bits)) & 63];
    }
    out[w] = 0;
}

/* Sobre firmado como el de Flex Developer Studio (test_account.cpp de la referencia). */
static void envelope(const char *payload, char *out, size_t cap, bool tamper)
{
    uint8_t dg[32], sig[64];
    ota_sha256((const uint8_t *)payload, strlen(payload), dg);
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecdsa_sign(&s_key.MBEDTLS_PRIVATE(grp), &r, &s, &s_key.MBEDTLS_PRIVATE(d), dg, 32, rng, NULL);
    mbedtls_mpi_write_binary(&r, sig, 32);
    mbedtls_mpi_write_binary(&s, sig + 32, 32);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    if (tamper) {
        sig[5] ^= 1;
    }
    char pb[2048], sb[128];
    b64url((const uint8_t *)payload, strlen(payload), pb);
    b64url(sig, 64, sb);
    snprintf(out, cap, "{\"schema\":2,\"algorithm\":\"ES256\",\"keyId\":\"%s\",\"payload\":\"%s\",\"signature\":\"%s\"}", KID,
             pb, sb);
}

typedef struct {
    char hash[65];
    char label[64];
    char model[16];
    char hwid[64];
    int polls_left;
    bool tamper, expire;
    bool registered;
    int me_status;     /* 0 = válida */
    const char *me_code;
    int code_posts;
} acct_srv_t;

static void acct_handler(void *ud, const gf_req_t *rq, gf_resp_t *rs)
{
    acct_srv_t *s = ud;
    char env[4096], pl[1024];
    if (!strcmp(rq->method, "POST") && !strcmp(rq->path, GAL_ACCT_CODE_PATH)) {
        s->code_posts++;
        char js[600];
        const size_t k = rq->body_len < sizeof(js) - 1 ? rq->body_len : sizeof(js) - 1;
        memcpy(js, rq->body, k);
        js[k] = 0;
        gal_j_get_str(js, js + k, "tokenHash", s->hash, sizeof(s->hash));
        gal_j_get_str(js, js + k, "label", s->label, sizeof(s->label));
        gal_j_get_str(js, js + k, "model", s->model, sizeof(s->model));
        gal_j_get_str(js, js + k, "hardwareId", s->hwid, sizeof(s->hwid));
        snprintf(pl, sizeof(pl),
                 "{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"K7QP2M\",\"tokenHash\":\"%s\",\"activationUrl\":"
                 "\"" GAL_ACCT_ACTIVATE "K7QP2M\"}",
                 s->hash);
        envelope(pl, env, sizeof(env), s->tamper);
        gf_json(rs, 201, "%s", env);
        return;
    }
    if (!strcmp(rq->method, "GET") && !strncmp(rq->path, GAL_ACCT_CODE_PATH "?", strlen(GAL_ACCT_CODE_PATH) + 1)) {
        char th[80];
        gf_query(rq->path, "tokenHash", th, sizeof(th));
        if (strcmp(th, s->hash)) {
            rs->status = 404;
            return;
        }
        const char *st = s->expire ? "expired" : s->polls_left-- > 0 ? "pending" : "approved";
        if (!strcmp(st, "approved")) {
            s->registered = true;
        }
        snprintf(pl, sizeof(pl), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"%s\"%s}", st,
                 !strcmp(st, "approved") ? ",\"flexAddress\":\"ana@flex\",\"displayName\":\"Ana\"" : "");
        envelope(pl, env, sizeof(env), false);
        gf_json(rs, 200, "%s", env);
        return;
    }
    if (!strcmp(rq->path, GAL_ACCT_ME_PATH)) {
        if (s->me_status) {
            if (s->me_code) {
                gf_json(rs, s->me_status, "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"x\"}}", s->me_code);
            } else {
                gf_bytes(rs, s->me_status, "text/html", "<html>proxy</html>", 18);
            }
            return;
        }
        gf_json(rs, 200, "{\"ok\":true,\"account\":{\"id\":\"a\",\"flexAddress\":\"ana@flex\",\"displayName\":\"Ana R\"}}");
        return;
    }
    rs->status = 404;
}

typedef struct {
    char k[16][24];
    uint8_t v[16][128];
    size_t n[16];
    int used;
    int writes;
} mstore_t;

static bool ms_get(void *ctx, const char *key, void *buf, size_t *len)
{
    mstore_t *s = ctx;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], key) && s->n[i] != (size_t)-1) {
            if (s->n[i] > *len) {
                return false;
            }
            memcpy(buf, s->v[i], s->n[i]);
            *len = s->n[i];
            return true;
        }
    }
    return false;
}

static bool ms_set(void *ctx, const char *key, const void *buf, size_t len)
{
    mstore_t *s = ctx;
    s->writes++;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], key)) {
            memcpy(s->v[i], buf, len);
            s->n[i] = len;
            return true;
        }
    }
    snprintf(s->k[s->used], sizeof(s->k[0]), "%s", key);
    memcpy(s->v[s->used], buf, len);
    s->n[s->used++] = len;
    return true;
}

static bool ms_erase(void *ctx, const char *key)
{
    mstore_t *s = ctx;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], key)) {
            s->n[i] = (size_t)-1;
        }
    }
    return true;
}

static void ms_rand(void *ctx, uint8_t *out, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; i++) {
        out[i] = (uint8_t)rand();
    }
}

static bool ms_has(mstore_t *s, const char *key)
{
    uint8_t b[128];
    size_t l = sizeof(b);
    return ms_get(s, key, b, &l);
}

static void test_acct(void)
{
    keygen();
    uint32_t clock = 5000;
    acct_srv_t srv = {.polls_left = 2};
    gf_conn_t fc;
    gf_init(&fc, acct_handler, &srv, &clock);
    const gal_io_t io = gf_io(&fc);
    gal_conn_t *c = malloc(sizeof(*c));
    gal_conn_init(c, &io);
    mstore_t st = {0};
    const gal_store_t store = {.ctx = &st, .get = ms_get, .set = ms_set, .erase = ms_erase, .random = ms_rand};
    char *scratch = malloc(GAL_ACCT_SCRATCH);
    gal_acct_t *a = malloc(sizeof(*a));
    gal_acct_init(a, &store, "A1B2C3D4E5F6", "0.9.0-gal");
    gal_acct_set_service(a, "cloud.test", 443, true, s_pub, KID);
    CHECK_EQ(a->link, GAL_LINK_UNLINKED);
    CHECK(!gal_acct_usable(a));
    /* sobre con la firma cambiada: no se acepta el código */
    srv.tamper = true;
    gal_acct_request_link(a);
    gal_acct_step(a, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(a->flow, GAL_ACCT_ERROR);
    CHECK(strstr(a->error, "ES256") != NULL);
    CHECK(!a->have);
    /* el bueno */
    srv.tamper = false;
    gal_acct_request_link(a);
    gal_acct_step(a, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(a->flow, GAL_ACCT_CODE);
    CHECK_STR(a->code, "K7QP2M");
    CHECK_STR(srv.model, "ESP32-S3");           /* la cuenta distingue el reloj del P4 */
    CHECK_STR(srv.label, "Flex OS Watch Ultra");
    CHECK(!strncmp(srv.hwid, "S3-A1B2C3D4E5F6-", 16));
    CHECK_EQ(strlen(srv.hash), 64);
    for (int i = 0; i < 10 && a->flow == GAL_ACCT_CODE; i++) {
        clock += GAL_ACCT_POLL_MS;
        gal_acct_step(a, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    }
    CHECK_EQ(a->flow, GAL_ACCT_LINKED);
    CHECK(a->have && gal_acct_usable(a));
    CHECK_STR(a->address, "ana@flex");
    char bearer[48];
    CHECK(gal_acct_bearer(a, bearer, sizeof(bearer)) && strlen(bearer) == 43);
    char hh[65];
    uint8_t dg[32];
    ota_sha256((const uint8_t *)bearer, 43, dg);
    gal_hex(dg, 32, hh);
    CHECK_STR(hh, srv.hash); /* solo viajó la huella */
    CHECK(strstr(fc.last.path, bearer) == NULL);
    /* reinicio: la cuenta se recupera de lo guardado */
    gal_acct_t *b = malloc(sizeof(*b));
    gal_acct_init(b, &store, "A1B2C3D4E5F6", "0.9.0-gal");
    gal_acct_set_service(b, "cloud.test", 443, true, s_pub, KID);
    CHECK(b->have && b->link == GAL_LINK_OFFLINE);
    /* con Wi-Fi valida: un 401 de un proxy (sin código) NO desvincula */
    srv.me_status = 401;
    srv.me_code = NULL;
    gal_acct_step(b, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    clock += GAL_ACCT_SETTLE_MS;
    gal_acct_step(b, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(b->link, GAL_LINK_UNAVAILABLE);
    CHECK(b->have);
    CHECK_EQ(b->wait_ms, 30000); /* y espera 30 s, no insiste */
    /* el servicio sí dice que se revocó */
    srv.me_code = "device_revoked";
    clock += 30000;
    gal_acct_step(b, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(b->link, GAL_LINK_AUTH_REQUIRED);
    CHECK(!gal_acct_usable(b));
    CHECK(b->have); /* la credencial no se borra sola */
    /* vuelve a ser válida: tras un 401 de Flex Cloud se revalida (como mucho cada minuto) */
    srv.me_status = 0;
    gal_acct_report_rejected(b, clock);
    gal_acct_step(b, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(b->link, GAL_LINK_LINKED);
    CHECK_STR(b->display, "Ana R"); /* el perfil que dice el servidor */
    /* desvincular: borra la credencial guardada */
    gal_acct_forget(b, &store);
    CHECK(!b->have && !ms_has(&st, "token") && !ms_has(&st, "linked"));
    gal_acct_t *z = malloc(sizeof(*z));
    gal_acct_init(z, &store, "A1B2C3D4E5F6", "0.9.0-gal");
    CHECK(!z->have && z->link == GAL_LINK_UNLINKED);
    /* el código caduca */
    srv.expire = true;
    gal_acct_set_service(z, "cloud.test", 443, true, s_pub, KID);
    gal_acct_request_link(z);
    gal_acct_step(z, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    clock += GAL_ACCT_POLL_MS;
    gal_acct_step(z, c, &store, true, clock, scratch, GAL_ACCT_SCRATCH);
    CHECK_EQ(z->flow, GAL_ACCT_EXPIRED);
    /* un registro a medias (corte durante el guardado) se dice como tal */
    mstore_t half = {0};
    const gal_store_t hs = {.ctx = &half, .get = ms_get, .set = ms_set, .erase = ms_erase, .random = ms_rand};
    ms_set(&half, "token", "abc", 3);
    gal_acct_init(z, &hs, "A1", "v");
    CHECK_EQ(z->link, GAL_LINK_ERROR);
    CHECK_EQ(gal_acct_classify(401, "token_expired"), GAL_SESS_EXPIRED);
    CHECK_EQ(gal_acct_classify(403, NULL), GAL_SESS_UNAVAILABLE);
    CHECK_EQ(gal_acct_backoff_ms(6), 15u * 60u * 1000u);
    CHECK(gal_acct_valid_address("a@flex") && !gal_acct_valid_address("a@gmail.com"));
    free(a);
    free(b);
    free(z);
    free(scratch);
    gal_conn_close(c);
    free(c);
    gf_free(&fc);
    mbedtls_ecdsa_free(&s_key);
}

/* ======================= el motor contra el ecosistema en memoria ======================= */

typedef struct {
    eco_t eco;
    gal_io_t lan, cl;
    gal_env_t env;
    gal_engine_t *e;
    mstore_t st;
    gal_store_t store;
    char root[256];
    uint64_t fs_free;
} rig_t;

static uint32_t r_now(void *ctx)
{
    return ((rig_t *)ctx)->eco.clock;
}

static uint64_t r_fs_free(void *ctx)
{
    return ((rig_t *)ctx)->fs_free;
}

static bool r_abort_lan(void *ctx);

static size_t s_deny_n; /* mem_ok dice que no a reservas de este tamaño (fragmentación) */

static bool r_mem_ok(void *ctx, size_t n)
{
    (void)ctx;
    return (!s_limit || s_cur + n <= s_limit) && n != s_deny_n;
}

static void copy_fixture(const char *root, const char *name, const char *as)
{
    char src[256], dst[512];
    snprintf(src, sizeof(src), "%s/%s", FIX, name);
    snprintf(dst, sizeof(dst), "%s/Imagenes/%s", root, as);
    size_t n;
    uint8_t *d = gf_read_file(src, &n);
    FILE *f = fopen(dst, "wb");
    if (f && d) {
        fwrite(d, 1, n, f);
    }
    if (f) {
        fclose(f);
    }
    free(d);
}

static gal_engine_t *s_cur_engine;
static const gal_local_io_t *s_rig_local; /* E/S local de la plataforma (NULL = LittleFS) */

static bool r_abort_lan(void *ctx)
{
    (void)ctx;
    return s_cur_engine && gal_engine_should_abort(s_cur_engine);
}

static void rig_init(rig_t *r, bool linked)
{
    memset(r, 0, sizeof(*r));
    eco_init(&r->eco);
    r->eco.clock = 100000;
    r->lan = gf_io(&r->eco.lan);
    r->cl = gf_io(&r->eco.cloud);
    r->lan.cancelled = r_abort_lan;
    r->cl.cancelled = r_abort_lan;
    snprintf(r->root, sizeof(r->root), "/tmp/galtest_XXXXXX");
    if (!mkdtemp(r->root)) {
        perror("mkdtemp");
    }
    ffs_set_root(r->root);
    ffs_init_dirs();
    ffs_mkdir("/Imagenes");
    r->fs_free = 8u * 1024 * 1024;
    r->store = (gal_store_t){.ctx = &r->st, .get = ms_get, .set = ms_set, .erase = ms_erase, .random = ms_rand};
    if (linked) {
        const char *tok = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ";
        const uint8_t one = 1;
        ms_set(&r->st, "token", tok, 43);
        ms_set(&r->st, "address", "ana@flex", 8);
        ms_set(&r->st, "display", "Ana", 3);
        ms_set(&r->st, "linked", &one, 1);
        snprintf(r->eco.bearer, sizeof(r->eco.bearer), "%s", tok);
    }
    r->env = (gal_env_t){.io_lan = &r->lan, .io_cloud = &r->cl, .store = &r->store, .alloc = m_alloc, .free = m_free,
                         .ctx = r, .now = r_now, .fs_free = r_fs_free, .mem_ok = r_mem_ok, .mac_hex = "A1B2C3D4E5F6", .version = "t",
                         .local = s_rig_local};
    r->e = gal_engine_new(&r->env);
    s_cur_engine = r->e;
    gal_engine_set_cloud(r->e, "cloud.test", 443, true, NULL, NULL);
}

static void rig_free(rig_t *r)
{
    gal_engine_close(r->e);
    for (int i = 0; i < 50; i++) {
        r->eco.clock += gal_engine_step(r->e);
    }
    gal_engine_free(r->e);
    s_cur_engine = NULL;
    eco_free(&r->eco);
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", r->root);
    if (system(cmd) != 0) {
        fprintf(stderr, "no se pudo borrar %s\n", r->root);
    }
}

#define SPIN(r, cond) do { for (int _i = 0; _i < 3000 && !(cond); _i++) { (r)->eco.clock += gal_engine_step((r)->e); } } while (0)

static gal_status_t S(rig_t *r)
{
    gal_status_t s;
    gal_status(r->e, &s);
    return s;
}

static const gal_item_t *find_item(rig_t *r, int which, const char *name)
{
    int n;
    gal_engine_lock(r->e);
    const gal_item_t *l = gal_list(r->e, which, &n);
    const gal_item_t *f = NULL;
    for (int i = 0; i < n && !f; i++) {
        if (!strcmp(l[i].name, name)) {
            f = &l[i];
        }
    }
    gal_engine_unlock(r->e);
    return f;
}

static int xstate(rig_t *r, int id, char *msg)
{
    gal_xfer_info_t x[GAL_XFERS];
    const int n = gal_xfers(r->e, x, GAL_XFERS);
    for (int i = 0; i < n; i++) {
        if (x[i].id == id) {
            if (msg) {
                strcpy(msg, x[i].msg);
            }
            return x[i].state;
        }
    }
    return -1;
}

static uint8_t *fix_bytes(const char *name, size_t *n)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", FIX, name);
    return gf_read_file(p, n);
}

static void test_engine_ultra(void)
{
    rig_t *r = calloc(1, sizeof(*r));
    const size_t base = s_cur; /* antes del motor: al final no puede quedar nada */
    rig_init(r, false);
    copy_fixture(r->root, "gal_quad_640x480.jpg", "cuadros.jpg");
    gal_set_wifi(r->e, true);
    CHECK(gal_engine_open(r->e));
    SPIN(r, S(r).nl == 1);
    CHECK_EQ(S(r).nl, 1);
    /* sin nada configurado: solo el reloj, y dice qué hacer */
    SPIN(r, S(r).choice.why[0]);
    CHECK_EQ(S(r).choice.src, GAL_SRC_NONE);
    /* la dirección del P4: hace falta su código */
    CHECK(!gal_set_ultra(r->e, "192.168.1.20/evil", 8080));
    CHECK(gal_set_ultra(r->e, "192.168.1.20", 8080));
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_PAIR);
    CHECK_EQ(S(r).health[GAL_SRC_ULTRA].state, GAL_ST_PAIR);
    CHECK(strstr(S(r).health[GAL_SRC_ULTRA].detail, "código") != NULL);
    /* código equivocado: se dice cuántos quedan */
    CHECK(!gal_ultra_pair(r->e, "12a456"));
    CHECK(gal_ultra_pair(r->e, "000000"));
    SPIN(r, !S(r).ultra_pairing);
    CHECK_STR(S(r).ultra_pair_msg, "Código incorrecto. Quedan 4 intentos");
    CHECK(!S(r).ultra_paired);
    /* el bueno */
    const uint8_t *pic;
    size_t pn;
    uint8_t *thumbsrc = fix_bytes("gal_tall_300x900.jpg", &pn);
    eco_p4_add(&r->eco, "Atardecer.jpg", "photo", "JPEG", thumbsrc, pn, thumbsrc, pn, false);
    eco_p4_add(&r->eco, "Secreto.jpg", "photo", "JPEG", thumbsrc, pn, NULL, 0, true);
    eco_p4_add(&r->eco, "Clip.mp4", "video", "MP4", "vid", 3, NULL, 0, false);
    (void)pic;
    CHECK(gal_ultra_pair(r->e, "123456"));
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_READY && S(r).nr == 3);
    gal_status_t s = S(r);
    CHECK(s.ultra_paired);
    CHECK_EQ(s.choice.src, GAL_SRC_ULTRA);
    CHECK_EQ(s.nr, 3);
    CHECK(strstr(r->eco.lan.last.agent, "ESP32-S3") != NULL);
    CHECK(r->eco.cloud.connects == 0); /* sin cuenta no se toca Flex Cloud */
    const gal_item_t *at = find_item(r, 1, "Atardecer.jpg");
    CHECK(at != NULL);
    const gal_item_t *lk = find_item(r, 1, "Protegido");
    CHECK(lk && (lk->flags & GAL_I_LOCKED));
    /* miniaturas: la del P4 (su JPEG de 132 px) */
    gal_item_t atc = *at;
    gal_thumb_request(r->e, &atc);
    const uint16_t *px = NULL;
    int ts = 0, slot = -1;
    SPIN(r, (slot = gal_thumb_acquire(r->e, &atc, &px, &ts)) >= 0);
    CHECK(slot >= 0 && px != NULL);
    gal_thumb_release(r->e, slot);
    /* lo protegido no se pide ni se abre ni se descarga */
    gal_item_t lkc = *lk;
    gal_thumb_request(r->e, &lkc);
    CHECK(!gal_view_open(r->e, &lkc));
    char why[96];
    CHECK_EQ(gal_download(r->e, &lkc, why, sizeof(why)), 0);
    CHECK(strstr(why, "protegido") != NULL);
    /* visor por flujo */
    CHECK(gal_view_open(r->e, &atc));
    gal_view_t v;
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING));
    CHECK_EQ(v.state, GAL_V_READY);
    CHECK(v.fit && v.fit_w == 167 && v.fit_h == GAL_VIEW_H && v.zoom && v.zoom_w == 300 && v.zoom_h == 900);
    CHECK(v.src_w == 300 && v.src_h == 900);
    gal_view_close(r->e);
    /* descarga del P4 al reloj, con su origen apuntado */
    const int xid = gal_download(r->e, &atc, why, sizeof(why));
    CHECK(xid > 0);
    CHECK_EQ(gal_download(r->e, &atc, why, sizeof(why)), 0); /* la misma dos veces: no */
    CHECK(strstr(why, "cola") != NULL);
    SPIN(r, xstate(r, xid, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, xid, NULL), GAL_XS_DONE);
    SPIN(r, S(r).nl == 2);
    CHECK(find_item(r, 0, "Atardecer.jpg") && (find_item(r, 0, "Atardecer.jpg")->flags & GAL_I_REMOTE));
    char lp[512];
    snprintf(lp, sizeof(lp), "%s/Imagenes/Atardecer.jpg", r->root);
    size_t ln;
    uint8_t *ld = gf_read_file(lp, &ln);
    CHECK(ld && ln == pn && !memcmp(ld, thumbsrc, pn)); /* byte a byte el original */
    free(ld);
    /* ya en el reloj: no se ofrece otra vez y "Todas" no la duplica */
    SPIN(r, find_item(r, 1, "Atardecer.jpg") && (find_item(r, 1, "Atardecer.jpg")->flags & GAL_I_ONWATCH));
    gal_engine_lock(r->e);
    gal_ref_t refs[16];
    const int nall = gal_build_view(r->e, GAL_TAB_ALL, refs, 16);
    gal_engine_unlock(r->e);
    CHECK_EQ(nall, 4); /* 2 del reloj + protegido + vídeo (la copia no se repite) */
    at = find_item(r, 1, "Atardecer.jpg");
    atc = *at;
    CHECK(!gal_can_download(r->e, &atc));
    /* subir una foto del reloj al P4 (CRC-32 de extremo a extremo) */
    const gal_item_t *cu = find_item(r, 0, "cuadros.jpg");
    gal_item_t cuc = *cu;
    CHECK(gal_can_upload(r->e, &cuc, GAL_SRC_ULTRA));
    const int uid = gal_upload(r->e, &cuc, GAL_SRC_ULTRA, why, sizeof(why));
    SPIN(r, xstate(r, uid, NULL) == GAL_XS_DONE);
    CHECK_EQ(r->eco.p4_uploads, 1);
    SPIN(r, find_item(r, 1, "cuadros.jpg") != NULL);
    CHECK(find_item(r, 1, "cuadros.jpg") != NULL);
    /* papelera del P4 */
    gal_item_t vc = *find_item(r, 1, "Clip.mp4");
    CHECK(gal_can_trash(r->e, &vc));
    const int tid = gal_trash(r->e, &vc, why, sizeof(why));
    SPIN(r, xstate(r, tid, NULL) == GAL_XS_DONE);
    CHECK_EQ(r->eco.p4_deletes, 1);
    SPIN(r, !find_item(r, 1, "Clip.mp4"));
    CHECK(!find_item(r, 1, "Clip.mp4"));
    /* el P4 se apaga: se dice y se reintenta con espera creciente (sin bucles) */
    r->eco.lan.fail_connect = 1000;
    gal_refresh(r->e);
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_DOWN);
    CHECK_EQ(S(r).health[GAL_SRC_ULTRA].state, GAL_ST_DOWN);
    CHECK(strstr(S(r).health[GAL_SRC_ULTRA].detail, "Flex Web Server") != NULL);
    const int conn0 = r->eco.lan.requests;
    const uint32_t t0 = r->eco.clock;
    SPIN(r, r->eco.clock - t0 > 120000);
    const uint8_t fails = S(r).health[GAL_SRC_ULTRA].fails;
    CHECK(fails >= 3 && fails <= 8); /* 2+4+8+16+32+60 s en dos minutos: unas 6, no cientos */
    CHECK_EQ(r->eco.lan.requests, conn0);
    /* vuelve */
    r->eco.lan.fail_connect = 0;
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_READY);
    CHECK_EQ(S(r).health[GAL_SRC_ULTRA].state, GAL_ST_READY);
    /* el P4 se reinicia (sesión perdida): pide el código otra vez, sin fingir que sigue conectado */
    memset(r->eco.session, 0, sizeof(r->eco.session));
    gal_refresh(r->e);
    r->eco.clock += GAL_RECHECK_MS;
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_PAIR);
    CHECK_EQ(S(r).health[GAL_SRC_ULTRA].state, GAL_ST_PAIR);
    CHECK(strstr(S(r).health[GAL_SRC_ULTRA].detail, "caducó") != NULL);
    CHECK(!S(r).ultra_paired);
    /* nada de credenciales cruzadas: al P4 nunca va un Bearer */
    CHECK(r->eco.lan.last.auth[0] == 0);
    free(thumbsrc);
    gal_engine_close(r->e);
    SPIN(r, false);
    gal_mem_t m;
    gal_engine_mem(r->e, &m);
    CHECK_EQ(m.thumb_bytes, 0);
    CHECK_EQ(m.view_bytes, 0);
    CHECK_EQ(m.json_bytes, 0); /* cerrada y sin transferencias: suelta lo grande */
    gal_engine_free(r->e);
    s_cur_engine = NULL;
    CHECK_EQ(s_cur, base); /* ni un byte perdido */
    r->e = gal_engine_new(&r->env);
    s_cur_engine = r->e;
    rig_free(r);
    free(r);
}

static void test_engine_cloud(void)
{
    rig_t *r = calloc(1, sizeof(*r));
    rig_init(r, true);
    copy_fixture(r->root, "gal_quad_640x480.jpg", "cuadros.jpg");
    copy_fixture(r->root, "gal_tall_300x900.jpg", "alta.jpg");
    size_t qn, tn;
    uint8_t *quad = fix_bytes("gal_quad_640x480.jpg", &qn), *tall = fix_bytes("gal_tall_300x900.jpg", &tn);
    for (int i = 0; i < 5; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "nube_%d.jpg", i);
        eco_cloud_add(&r->eco, r->eco.cl, nm, "image/jpeg", tall, tn, quad, qn, "image/jpeg");
    }
    eco_cloud_add(&r->eco, r->eco.cl, "web.png", "image/png", "png", 3, "png", 3, "image/png");
    r->eco.page = 4; /* para ver las páginas */
    gal_set_pick(r->e, GAL_PICK_CLOUD);
    gal_set_wifi(r->e, true);
    CHECK(gal_engine_open(r->e));
    SPIN(r, S(r).health[GAL_SRC_CLOUD].state == GAL_ST_READY && S(r).nr > 0);
    gal_status_t s = S(r);
    CHECK_EQ(s.health[GAL_SRC_CLOUD].state, GAL_ST_READY);
    CHECK_EQ(s.choice.src, GAL_SRC_CLOUD);
    CHECK(!s.choice.automatic);
    CHECK_EQ(s.nr, 4);
    CHECK(s.remote_more);
    CHECK(s.q_total == 5ull * 1024 * 1024 * 1024); /* la cuota la dice el servidor */
    CHECK(!strncmp(r->eco.cloud.last.auth, "Bearer ", 7));
    CHECK(r->eco.cloud.last.cookie[0] == 0);
    gal_more(r->e);
    SPIN(r, S(r).nr == 6);
    CHECK_EQ(S(r).nr, 6);
    CHECK(!S(r).remote_more);
    /* miniatura JPEG sí; PNG de la web no (el reloj solo decodifica JPEG), sin inventarla */
    gal_item_t n0 = *find_item(r, 1, "nube_0.jpg"), png = *find_item(r, 1, "web.png");
    gal_thumb_request(r->e, &n0);
    gal_thumb_request(r->e, &png);
    const uint16_t *px;
    int ts, sl = -1;
    SPIN(r, (sl = gal_thumb_acquire(r->e, &n0, &px, &ts)) >= 0);
    CHECK(sl >= 0);
    gal_thumb_release(r->e, sl);
    CHECK_EQ(gal_thumb_acquire(r->e, &png, &px, &ts), -1);
    CHECK(!gal_view_open(r->e, &png)); /* el visor no promete un PNG */
    /* descarga cortada a mitad: queda el .part y se reanuda con Range + If-Range */
    char why[96];
    r->eco.cut_download_at = 3000;
    const int d1 = gal_download(r->e, &n0, why, sizeof(why));
    CHECK(d1 > 0);
    SPIN(r, xstate(r, d1, NULL) == GAL_XS_WAIT || xstate(r, d1, NULL) == GAL_XS_DONE);
    char msg[96];
    CHECK_EQ(xstate(r, d1, msg), GAL_XS_WAIT);
    CHECK(strstr(msg, "Reintento") != NULL);
    SPIN(r, xstate(r, d1, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, d1, NULL), GAL_XS_DONE);
    CHECK_EQ(r->eco.range_requests, 1);
    CHECK(strstr(r->eco.cloud.last.if_range, n0.sha256) != NULL);
    char lp[512];
    snprintf(lp, sizeof(lp), "%s/Imagenes/nube_0.jpg", r->root);
    size_t ln;
    uint8_t *ld = gf_read_file(lp, &ln);
    CHECK(ld && ln == tn && !memcmp(ld, tall, tn));
    free(ld);
    /* bytes cambiados por el camino: la huella no cuadra, se descarta y se repite */
    gal_item_t n1 = *find_item(r, 1, "nube_1.jpg");
    r->eco.corrupt_download = 1;
    const int d2 = gal_download(r->e, &n1, why, sizeof(why));
    SPIN(r, xstate(r, d2, NULL) == GAL_XS_DONE || xstate(r, d2, NULL) == GAL_XS_FAIL);
    CHECK_EQ(xstate(r, d2, NULL), GAL_XS_DONE);
    snprintf(lp, sizeof(lp), "%s/Imagenes/nube_1.jpg", r->root);
    ld = gf_read_file(lp, &ln);
    CHECK(ld && ln == tn && !memcmp(ld, tall, tn));
    free(ld);
    /* sin sitio en el reloj: ni se intenta */
    gal_item_t n2 = *find_item(r, 1, "nube_2.jpg");
    r->fs_free = 100000;
    CHECK_EQ(gal_download(r->e, &n2, why, sizeof(why)), 0);
    CHECK_STR(why, "No queda espacio en el reloj");
    r->fs_free = 8u * 1024 * 1024;
    /* subida por partes con una parte cortada: se reanuda y solo se manda lo que falta */
    SPIN(r, find_item(r, 0, "cuadros.jpg") != NULL);
    gal_item_t cq = *find_item(r, 0, "cuadros.jpg");
    /* un archivo de 3 partes de 256 KB (la quad es pequeña: se hace uno grande) */
    char bigp[512];
    snprintf(bigp, sizeof(bigp), "%s/Imagenes/grande.jpg", r->root);
    FILE *bf = fopen(bigp, "wb");
    fwrite(quad, 1, qn, bf);
    for (int i = 0; i < 600 * 1024; i++) {
        fputc(i * 31 & 0xFF, bf);
    }
    fclose(bf);
    gal_refresh(r->e);
    SPIN(r, find_item(r, 0, "grande.jpg") != NULL);
    gal_item_t gq = *find_item(r, 0, "grande.jpg");
    r->eco.cut_part = 2;
    const int u1 = gal_upload(r->e, &gq, GAL_SRC_CLOUD, why, sizeof(why));
    CHECK(u1 > 0);
    SPIN(r, xstate(r, u1, NULL) == GAL_XS_DONE || xstate(r, u1, NULL) == GAL_XS_FAIL);
    CHECK_EQ(xstate(r, u1, msg), GAL_XS_DONE);
    CHECK_EQ(r->eco.completes, 1);
    CHECK_EQ(r->eco.part_puts, 4); /* 3 partes + la cortada que se repite (no las 6 de empezar de cero) */
    SPIN(r, find_item(r, 1, "grande.jpg") != NULL);
    const gal_item_t *gr = find_item(r, 1, "grande.jpg");
    CHECK(gr != NULL);
    if (gr) {
        char sha[65];
        size_t gn;
        uint8_t *gd = gf_read_file(bigp, &gn);
        eco_sha_hex(gd, gn, sha);
        CHECK_STR(gr->sha256, sha); /* la nube tiene el mismo archivo */
        free(gd);
        CHECK(gr->flags & GAL_I_ONWATCH);
    }
    (void)cq;
    /* la cuota llena: falla sin reintentos y con su motivo */
    r->eco.quota_used = r->eco.quota_total - 10;
    gal_item_t aq = *find_item(r, 0, "alta.jpg");
    const int u2 = gal_upload(r->e, &aq, GAL_SRC_CLOUD, why, sizeof(why));
    SPIN(r, xstate(r, u2, NULL) == GAL_XS_FAIL || xstate(r, u2, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, u2, msg), GAL_XS_FAIL);
    CHECK_STR(msg, "No queda espacio en el destino");
    gal_xfer_retry(r->e, u2);
    r->eco.quota_used = 0;
    SPIN(r, xstate(r, u2, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, u2, NULL), GAL_XS_DONE);
    /* papelera de Flex Cloud */
    gal_item_t n3 = *find_item(r, 1, "nube_3.jpg");
    const int t1 = gal_trash(r->e, &n3, why, sizeof(why));
    SPIN(r, xstate(r, t1, NULL) == GAL_XS_DONE);
    CHECK(eco_find_cl(&r->eco, r->eco.cl, n3.id) == NULL);
    /* la credencial deja de valer: se revalida y se pide volver a vincular; la lista lo dice */
    r->eco.revoked = true;
    gal_refresh(r->e);
    r->eco.clock += GAL_RECHECK_MS;
    SPIN(r, S(r).health[GAL_SRC_CLOUD].state == GAL_ST_PAIR);
    CHECK_EQ(S(r).health[GAL_SRC_CLOUD].state, GAL_ST_PAIR);
    CHECK_EQ(S(r).acct_link, GAL_LINK_AUTH_REQUIRED);
    /* Flex Cloud con una página de error del alojamiento: sigue vinculada, se reintenta */
    r->eco.revoked = false;
    gal_acct_link(r->e); /* (no hace falta: probamos que el estado de antes se respeta) */
    gal_acct_link_cancel(r->e);
    SPIN(r, S(r).acct_flow != GAL_ACCT_REQUESTING);
    free(quad);
    free(tall);
    rig_free(r);
    free(r);
}

static void test_engine_auto_phone(void)
{
    rig_t *r = calloc(1, sizeof(*r));
    rig_init(r, true);
    size_t tn;
    uint8_t *tall = fix_bytes("gal_tall_300x900.jpg", &tn);
    eco_p4_add(&r->eco, "del_p4.jpg", "photo", "JPEG", tall, tn, tall, tn, false);
    eco_cloud_add(&r->eco, r->eco.cl, "de_la_nube.jpg", "image/jpeg", tall, tn, NULL, 0, NULL);
    eco_cloud_add(&r->eco, r->eco.ph, "del_telefono.jpg", "image/jpeg", tall, tn, tall, tn, "image/jpeg");
    snprintf(r->eco.session, sizeof(r->eco.session), "0123456789abcdef0123456789abcdef");
    gal_set_ultra(r->e, "192.168.1.20", 8080);
    gal_set_wifi(r->e, true);
    gal_engine_open(r->e);
    /* sin sesión todavía: Automático usa Flex Cloud mientras tanto */
    SPIN(r, S(r).choice.src == GAL_SRC_CLOUD && S(r).nr == 1);
    CHECK_EQ(S(r).choice.src, GAL_SRC_CLOUD);
    CHECK(find_item(r, 1, "de_la_nube.jpg") != NULL);
    gal_ultra_pair(r->e, "123456");
    /* con el P4 listo y estable, Automático vuelve a la red local (y su lista, no la de antes) */
    r->eco.clock += GAL_AUTO_STABLE_MS;
    SPIN(r, S(r).choice.src == GAL_SRC_ULTRA && S(r).nr == 1 && find_item(r, 1, "del_p4.jpg"));
    CHECK_EQ(S(r).choice.src, GAL_SRC_ULTRA);
    CHECK(find_item(r, 1, "del_p4.jpg") != NULL);
    CHECK(!find_item(r, 1, "de_la_nube.jpg"));
    /* el P4 no tiene teléfono: Flex Phone no existe (la interfaz no lo ofrece) */
    CHECK_EQ(S(r).health[GAL_SRC_PHONE].state, GAL_ST_UNSET);
    /* el P4 tiene su teléfono listo: Flex Phone por su pasarela */
    snprintf(r->eco.phone_state, sizeof(r->eco.phone_state), "ready");
    r->eco.clock += GAL_RECHECK_MS;
    SPIN(r, S(r).health[GAL_SRC_PHONE].state == GAL_ST_READY);
    CHECK_EQ(S(r).health[GAL_SRC_PHONE].state, GAL_ST_READY);
    CHECK_STR(S(r).phone.name, "Pixel de Ana");
    gal_set_pick(r->e, GAL_PICK_PHONE);
    SPIN(r, S(r).choice.src == GAL_SRC_PHONE && find_item(r, 1, "del_telefono.jpg"));
    CHECK(find_item(r, 1, "del_telefono.jpg") != NULL);
    CHECK(!strncmp(r->eco.lan.last.path, "/api/cloud/files", 16)); /* por el P4, con su sesión */
    CHECK(r->eco.lan.last.auth[0] == 0);
    gal_item_t ph = *find_item(r, 1, "del_telefono.jpg");
    char why[96];
    const int d = gal_download(r->e, &ph, why, sizeof(why));
    SPIN(r, xstate(r, d, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, d, NULL), GAL_XS_DONE);
    /* el teléfono se va: Flex Phone sin servicio y con motivo; nunca cae a otra fuente */
    snprintf(r->eco.phone_state, sizeof(r->eco.phone_state), "off");
    r->eco.clock += GAL_RECHECK_MS;
    SPIN(r, S(r).health[GAL_SRC_PHONE].state == GAL_ST_UNSET && strstr(S(r).choice.why, "teléfono"));
    gal_status_t s = S(r);
    CHECK_EQ(s.choice.src, GAL_SRC_PHONE);
    CHECK(strstr(s.choice.why, "teléfono") != NULL);
    CHECK(s.remote_state == GAL_L_ERROR || s.nr == 1);
    /* sin Wi-Fi: solo el reloj, sin peticiones */
    gal_set_wifi(r->e, false);
    const int reqs = r->eco.lan.requests + r->eco.cloud.requests;
    SPIN(r, false);
    CHECK_EQ(r->eco.lan.requests + r->eco.cloud.requests, reqs);
    CHECK_EQ(S(r).choice.src, GAL_SRC_NONE);
    free(tall);
    rig_free(r);
    free(r);
}

/* Estado de la miniatura sin dejarla fijada (acquire fija si está lista). */
static int th_state(rig_t *r, const gal_item_t *it)
{
    const uint16_t *px;
    int ts = GAL_TH_EMPTY;
    const int sl = gal_thumb_acquire(r->e, it, &px, &ts);
    if (sl >= 0) {
        gal_thumb_release(r->e, sl);
    }
    return ts;
}

static void test_engine_memory(void)
{
    rig_t *r = calloc(1, sizeof(*r));
    rig_init(r, false);
    copy_fixture(r->root, "gal_quad_640x480.jpg", "a.jpg");
    copy_fixture(r->root, "gal_prog_200x150.jpg", "prog.jpg");
    const size_t base = s_cur;
    gal_set_wifi(r->e, true);
    /* Sin memoria al abrir: abre igual, sin JSON ni miniaturas, y la lista
     * se reintenta sola (antes no abría o se quedaba vacía). */
    s_limit = s_cur + 1000;
    CHECK(gal_engine_open(r->e));
    SPIN(r, false);
    gal_mem_t m;
    gal_engine_mem(r->e, &m);
    CHECK(m.json_bytes == 0 && m.thumb_bytes == 0);
    CHECK_EQ(S(r).nl, 0);
    CHECK(S(r).scanning); /* sigue pendiente, no rendida */
    s_limit = s_cur + 80u * 1024u; /* la lista (66 KB) cabe; el JSON (160 KB), no */
    r->eco.clock += 3000; /* GAL_SCAN_NOMEM_MS */
    SPIN(r, S(r).nl == 2);
    CHECK_EQ(S(r).nl, 2);
    gal_item_t a = *find_item(r, 0, "a.jpg"), p = *find_item(r, 0, "prog.jpg");
    /* sin 25 KB para una miniatura: la celda se queda sin imagen, sin bucle... */
    s_limit = s_cur + 1000;
    gal_thumb_request(r->e, &a);
    const uint16_t *px;
    int ts = GAL_TH_EMPTY, sl = -1;
    SPIN(r, (ts = th_state(r, &a)) == GAL_TH_FAILED);
    CHECK_EQ(ts, GAL_TH_FAILED);
    gal_engine_mem(r->e, &m);
    CHECK(m.thumb_bytes == 0 && m.json_bytes == 0);
    /* ...y vuelve la memoria: el JSON se pide solo y la miniatura, a los 5 s */
    s_limit = 0;
    r->eco.clock += 5001;
    gal_thumb_request(r->e, &a);
    SPIN(r, (sl = gal_thumb_acquire(r->e, &a, &px, &ts)) >= 0);
    CHECK(sl >= 0);
    gal_thumb_release(r->e, sl);
    gal_engine_mem(r->e, &m);
    CHECK_EQ(m.json_bytes, GAL_JSON_MAX);
    CHECK_EQ(m.thumb_bytes, (size_t)GAL_THUMB_SIDE * GAL_THUMB_SIDE * 2); /* una a una, no 30 de golpe */
    /* Sin un bloque para otra miniatura: la nueva toma el lienzo de la más
     * vieja que no se pinta (no reserva más)... */
    s_deny_n = (size_t)GAL_THUMB_SIDE * GAL_THUMB_SIDE * 2;
    gal_item_t b1 = a, b2 = a;
    b1.tver = 7001;
    b2.tver = 7002;
    gal_thumb_request(r->e, &b1);
    SPIN(r, (ts = th_state(r, &b1)) == GAL_TH_READY || ts == GAL_TH_FAILED);
    CHECK_EQ(ts, GAL_TH_READY);
    gal_engine_mem(r->e, &m);
    CHECK_EQ(m.thumb_bytes, (size_t)GAL_THUMB_SIDE * GAL_THUMB_SIDE * 2);
    CHECK_EQ(th_state(r, &a), GAL_TH_EMPTY); /* "a" cedió su lienzo */
    /* ...pero nunca la que se está pintando: entonces la nueva se queda sin imagen */
    sl = gal_thumb_acquire(r->e, &b1, &px, &ts);
    CHECK(sl >= 0);
    gal_thumb_request(r->e, &b2);
    SPIN(r, (ts = th_state(r, &b2)) == GAL_TH_READY || ts == GAL_TH_FAILED);
    CHECK_EQ(ts, GAL_TH_FAILED);
    gal_thumb_release(r->e, sl);
    s_deny_n = 0;
    s_limit = 0;
    /* miniatura del reloj: se decodifica una vez y se guarda */
    gal_thumb_request(r->e, &a);
    SPIN(r, (sl = gal_thumb_acquire(r->e, &a, &px, &ts)) >= 0);
    CHECK(sl >= 0 && near(px[10 * GAL_THUMB_SIDE + 10], 255, 0, 0));
    /* fijada: no se reutiliza mientras se pinta, aunque se pidan muchas */
    for (int i = 0; i < GAL_THUMBS * 2; i++) {
        gal_item_t f = a;
        f.tver = 1000 + (uint32_t)i;
        gal_thumb_request(r->e, &f);
    }
    CHECK(near(px[10 * GAL_THUMB_SIDE + 10], 255, 0, 0));
    gal_thumb_release(r->e, sl);
    char th[512];
    snprintf(th, sizeof(th), "%s/System/Galeria/th", r->root);
    DIR *d = opendir(th);
    int files = 0;
    struct dirent *de;
    while (d && (de = readdir(d))) {
        files += de->d_name[0] != '.';
    }
    if (d) {
        closedir(d);
    }
    CHECK_EQ(files, 2); /* la de "a" y la de b1 (otra versión): cada una, una vez */
    SPIN(r, false); /* las pedidas arriba se hacen (o fallan) */
    /* el progresivo: la celda lo dirá; el visor lo rechaza con motivo */
    CHECK(gal_view_open(r->e, &p));
    gal_view_t v;
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING));
    CHECK_EQ(v.state, GAL_V_ERROR);
    CHECK(strstr(v.err, "progresivo") != NULL);
    /* 640x480: el 2x es la foto a 1:1 (no se amplía nada) y "Ajustar" sale de él */
    CHECK(gal_view_open(r->e, &a));
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING));
    CHECK(v.state == GAL_V_READY && v.zoom && v.zoom_w == 640 && v.zoom_h == 480 && v.fit_w == 410 && v.fit_h == 308);
    CHECK(near(v.fit[10 * v.fit_w + 10], 255, 0, 0) && near(v.zoom[(v.zoom_h - 10) * v.zoom_w + 10], 0, 0, 255));
    gal_view_close(r->e);
    gal_engine_step(r->e);
    gal_engine_mem(r->e, &m);
    CHECK_EQ(m.view_bytes, 0); /* cerrado el visor, sus lienzos se sueltan ya */
    /* con poca memoria: sin el 2x, solo encajada */
    s_limit = s_cur + 450u * 1024u; /* el 2x (614 KB) no cabe; encajada (252 KB + banda 131 KB) sí */
    CHECK(gal_view_open(r->e, &a));
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING));
    s_limit = 0;
    CHECK(v.state == GAL_V_READY && v.zoom == NULL && v.fit_w == 410 && v.fit_h == 308);
    gal_engine_mem(r->e, &m);
    CHECK(m.view_bytes <= (size_t)GAL_VIEW_MAX_W * GAL_VIEW_MAX_H * 2 + (size_t)GAL_VIEW_W * GAL_VIEW_H * 2);
    CHECK(m.thumb_bytes > 0 && m.thumb_bytes <= (size_t)GAL_THUMBS * GAL_THUMB_SIDE * GAL_THUMB_SIDE * 2);
    printf("   galería: motor %zu B, JSON %zu B, miniaturas %zu B, visor %zu B\n", m.engine_bytes, m.json_bytes,
           m.thumb_bytes, m.view_bytes);
    gal_view_close(r->e);
    gal_engine_close(r->e);
    SPIN(r, false);
    gal_engine_mem(r->e, &m);
    CHECK(m.view_bytes == 0 && m.thumb_bytes == 0 && m.json_bytes == 0);
    CHECK_EQ(s_cur - base, 0);
    rig_free(r);
    free(r);
}

/* ======================= biblioteca del reloj -> Galería (gal_wm_map) ======================= */

static void test_wm_map(void)
{
    FlexMlRec r;
    memset(&r, 0, sizeof(r));
    r.id = 7;
    r.kind = FML_K_PHOTO;
    r.fmt = FML_F_JPEG;
    r.size = 123456;
    r.created = 1700000000u;
    r.added = 1760000000u;
    r.w = 640;
    r.h = 480;
    r.flags = FML_R_THUMB | FML_R_PLAYABLE;
    r.thumbVer = 3;
    snprintf(r.path, sizeof(r.path), "/Imagenes/Playa n 1.jpg");
    snprintf(r.name, sizeof(r.name), "Playa \xC3\xB1 1.jpg");
    gal_item_t it;
    CHECK(gal_wm_item(&r, 1, &it));
    CHECK(it.src == GAL_SRC_LOCAL && it.vol == 1 && it.mid == 7 && it.kind == GAL_K_PHOTO && it.fmt == GAL_F_JPEG);
    CHECK_STR(it.path, "/Imagenes/Playa n 1.jpg");
    CHECK_STR(it.name, "Playa \xC3\xB1 1.jpg"); /* el nombre con el que llegó */
    CHECK(it.when == 1700000000000LL && it.size == 123456 && it.w == 640 && it.h == 480);
    CHECK((it.flags & GAL_I_THUMB) && !(it.flags & GAL_I_LOCKED) && it.origin_src == GAL_SRC_NONE);
    const uint32_t tv = it.tver;
    r.thumbVer = 4;
    CHECK(gal_wm_item(&r, 1, &it) && it.tver != tv); /* miniatura nueva: otra clave */
    /* vídeos: solo AVI con MJPEG se reproduce */
    r.kind = FML_K_VIDEO;
    r.fmt = FML_F_AVI_MJPEG;
    r.durMs = 4200;
    CHECK(gal_wm_item(&r, 0, &it) && it.kind == GAL_K_VIDEO && it.fmt == GAL_F_VIDEO && it.dur_ms == 4200);
    CHECK(gal_wm_playable_video(&it));
    r.fmt = FML_F_MP4;
    CHECK(gal_wm_item(&r, 0, &it) && !gal_wm_playable_video(&it));
    /* protegido, dibujos, sin fecha de origen, música fuera */
    r.fmt = FML_F_AVI_MJPEG;
    r.flags = FML_R_LOCKED;
    CHECK(gal_wm_item(&r, 0, &it) && (it.flags & GAL_I_LOCKED) && !gal_wm_playable_video(&it));
    r.kind = FML_K_DRAW;
    r.fmt = FML_F_FXP;
    r.flags = 0;
    r.created = 0;
    CHECK(gal_wm_item(&r, 0, &it) && it.kind == GAL_K_DRAW && it.fmt == GAL_F_FXP && it.when == 1760000000000LL);
    r.kind = FML_K_AUDIO;
    CHECK(!gal_wm_item(&r, 0, &it));
    /* sin formato comprobado aún: la extensión como pista; sin nombre: el del disco */
    r.kind = FML_K_PHOTO;
    r.fmt = FML_F_UNKNOWN;
    r.name[0] = 0;
    snprintf(r.path, sizeof(r.path), "/DCIM/IMG_0001.png");
    CHECK(gal_wm_item(&r, 1, &it) && it.fmt == GAL_F_PNG);
    CHECK_STR(it.name, "IMG_0001.png");
    CHECK(gal_wm_fmt(FML_F_HEIC) == GAL_F_HEIC && gal_wm_fmt(FML_F_WAV_PCM) == GAL_F_UNKNOWN);
    /* orden: más reciente primero y estable */
    gal_item_t v[4];
    memset(v, 0, sizeof(v));
    v[0].when = 10; v[0].mid = 1;
    v[1].when = 30; v[1].mid = 2;
    v[2].when = 10; v[2].mid = 3;
    v[3].when = 20; v[3].mid = 4;
    gal_wm_sort(v, 4);
    CHECK(v[0].mid == 2 && v[1].mid == 4 && v[2].mid == 1 && v[3].mid == 3);
}

/* ======================= la biblioteca de la plataforma (reloj + microSD) ======================= */

/* Una biblioteca de mentira con dos volúmenes en carpetas: vol0 (reloj) y
 * vol1 (microSD). Comprueba que el motor SIEMPRE suelta lo que arrienda y que
 * nunca guarda en otro sitio que el destino elegido. */
typedef struct {
    rig_t *r;
    gal_item_t items[8];
    int n;
    uint32_t rev;
    int leases, max_leases, acquires, thumbs, imports;
    bool sd_in, dest_ok;
    char trashed[GAL_PATH_MAX];
} lib_t;
static lib_t s_lib;

static void lib_root(int vol, char *out, size_t n)
{
    snprintf(out, n, "%s/vol%d", s_lib.r->root, vol);
}

static int lio_list(void *ctx, gal_item_t *out, int cap)
{
    (void)ctx;
    int k = 0;
    for (int i = 0; i < s_lib.n && k < cap; i++) {
        if (s_lib.items[i].vol == 1 && !s_lib.sd_in) {
            continue; /* sin tarjeta, su catálogo no está */
        }
        out[k++] = s_lib.items[i];
    }
    return k;
}

static uint32_t lio_rev(void *ctx)
{
    (void)ctx;
    return s_lib.rev;
}

static bool lio_acquire(void *ctx, const gal_item_t *it, char *real, size_t n)
{
    (void)ctx;
    if (it->vol == 1 && !s_lib.sd_in) {
        return false;
    }
    char root[300];
    lib_root(it->vol, root, sizeof(root));
    snprintf(real, n, "%s%s", root, it->path);
    s_lib.acquires++;
    if (it->vol == 1 && ++s_lib.leases > s_lib.max_leases) {
        s_lib.max_leases = s_lib.leases;
    }
    return true;
}

static void lio_release(void *ctx, const gal_item_t *it)
{
    (void)ctx;
    if (it->vol == 1) {
        s_lib.leases--;
    }
}

static bool lio_thumb(void *ctx, const gal_item_t *it, uint16_t *px)
{
    (void)ctx;
    s_lib.thumbs++;
    for (int i = 0; i < GAL_THUMB_SIDE * GAL_THUMB_SIDE; i++) {
        px[i] = it->kind == GAL_K_VIDEO ? 0xF800 : 0x07E0;
    }
    return true;
}

static bool lio_trash(void *ctx, const gal_item_t *it, char *msg, size_t n)
{
    (void)ctx;
    snprintf(s_lib.trashed, sizeof(s_lib.trashed), "%s", it->path);
    snprintf(msg, n, "%s", it->vol == 1 ? "Borrada de la microSD" : "En la papelera del reloj");
    for (int i = 0; i < s_lib.n; i++) {
        if (s_lib.items[i].vol == it->vol && !strcmp(s_lib.items[i].path, it->path)) {
            s_lib.items[i] = s_lib.items[--s_lib.n];
            s_lib.rev++;
            break;
        }
    }
    return true;
}

static bool lio_dest_ready(void *ctx, char *why, size_t n)
{
    (void)ctx;
    if (!s_lib.dest_ok) {
        snprintf(why, n, "Has elegido guardar en la microSD y no hay tarjeta.");
    }
    return s_lib.dest_ok;
}

static bool lio_import(void *ctx, const char *tmp_real, const char *name, int kind, int src, char *final, size_t n,
                       char *why, size_t wn)
{
    (void)ctx;
    CHECK_EQ(src, GAL_SRC_ULTRA); /* de dónde viene, para el origen en la biblioteca */
    char dst[600], root[300];
    lib_root(1, root, sizeof(root));
    snprintf(final, n, "/Imagenes/%s", name);
    snprintf(dst, sizeof(dst), "%s%s", root, final);
    if (rename(tmp_real, dst) != 0) {
        snprintf(why, wn, "No se pudo guardar en la microSD");
        return false;
    }
    gal_item_t *o = &s_lib.items[s_lib.n++];
    memset(o, 0, sizeof(*o));
    o->src = GAL_SRC_LOCAL;
    o->vol = 1;
    o->mid = 100 + (uint32_t)s_lib.n;
    o->kind = (uint8_t)kind;
    o->fmt = GAL_F_JPEG;
    o->origin_src = GAL_SRC_NONE;
    snprintf(o->path, sizeof(o->path), "%s", final);
    snprintf(o->name, sizeof(o->name), "%s", name);
    s_lib.rev++;
    s_lib.imports++;
    snprintf(why, wn, "Guardada en microSD");
    return true;
}

static const gal_local_io_t kLio = {lio_list, lio_rev, lio_acquire, lio_release, lio_thumb,
                                    lio_trash, lio_dest_ready, lio_import};

static void lib_add(int vol, uint32_t mid, const char *path, int kind, int fmt, bool thumb, int64_t when)
{
    gal_item_t *o = &s_lib.items[s_lib.n++];
    memset(o, 0, sizeof(*o));
    o->src = GAL_SRC_LOCAL;
    o->vol = (uint8_t)vol;
    o->mid = mid;
    o->kind = (uint8_t)kind;
    o->fmt = (uint8_t)fmt;
    o->flags = thumb ? GAL_I_THUMB : 0;
    o->when = when;
    o->tver = mid;
    o->origin_src = GAL_SRC_NONE;
    snprintf(o->path, sizeof(o->path), "%s", path);
    const char *b = strrchr(path, '/');
    snprintf(o->name, sizeof(o->name), "%s", b ? b + 1 : path);
    s_lib.rev++;
}

static void put_file(int vol, const char *path, const char *fixture)
{
    char root[300], dst[700], cmd[1600];
    lib_root(vol, root, sizeof(root));
    snprintf(dst, sizeof(dst), "%s%s", root, path);
    snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\" && cp '%s/%s' '%s'", dst, FIX, fixture, dst);
    if (system(cmd) != 0) {
        fprintf(stderr, "no se pudo copiar %s\n", fixture);
    }
}

static int thumb_state(rig_t *r, const gal_item_t *it, uint16_t *first)
{
    gal_item_t c = *it;
    gal_thumb_request(r->e, &c);
    const uint16_t *px = NULL;
    int st = 0, slot = -1;
    SPIN(r, (slot = gal_thumb_acquire(r->e, &c, &px, &st)) >= 0 || st == GAL_TH_FAILED);
    if (slot >= 0) {
        *first = px[0];
        gal_thumb_release(r->e, slot);
        return GAL_TH_READY;
    }
    return st;
}

static void test_engine_local_io(void)
{
    rig_t *r = calloc(1, sizeof(*r));
    memset(&s_lib, 0, sizeof(s_lib));
    s_rig_local = &kLio;
    rig_init(r, false);
    s_rig_local = NULL;
    s_lib.r = r;
    s_lib.sd_in = true;
    s_lib.dest_ok = true;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/vol0/Imagenes' '%s/vol1/Imagenes' '%s/vol1/DCIM'", r->root, r->root, r->root);
    CHECK(system(cmd) == 0);
    lib_add(0, 1, "/Imagenes/cuadros.jpg", GAL_K_PHOTO, GAL_F_JPEG, false, 1000);
    lib_add(1, 2, "/DCIM/sd_foto.jpg", GAL_K_PHOTO, GAL_F_JPEG, true, 3000);
    lib_add(1, 3, "/Videos/clip.avi", GAL_K_VIDEO, GAL_F_VIDEO, true, 2000);
    lib_add(1, 4, "/DCIM/sin_miniatura.jpg", GAL_K_PHOTO, GAL_F_JPEG, false, 500);
    put_file(0, "/Imagenes/cuadros.jpg", "gal_quad_640x480.jpg");
    put_file(1, "/DCIM/sd_foto.jpg", "gal_tiny_40x30.jpg");
    CHECK(gal_engine_open(r->e));
    SPIN(r, S(r).nl == 4);
    CHECK_EQ(S(r).nl, 4);
    const gal_item_t *sf = find_item(r, 0, "sd_foto.jpg");
    CHECK(sf && sf->vol == 1 && sf->mid == 2 && (sf->flags & GAL_I_THUMB));
    /* miniaturas: la de la biblioteca (también del vídeo), la del JPEG del reloj
     * decodificada, y la de la microSD que aún no tiene: no se inventa */
    uint16_t c0 = 0;
    CHECK_EQ(thumb_state(r, sf, &c0), GAL_TH_READY);
    CHECK_EQ(c0, 0x07E0);
    CHECK_EQ(thumb_state(r, find_item(r, 0, "clip.avi"), &c0), GAL_TH_READY);
    CHECK_EQ(c0, 0xF800);
    CHECK_EQ(thumb_state(r, find_item(r, 0, "cuadros.jpg"), &c0), GAL_TH_READY);
    CHECK_EQ(thumb_state(r, find_item(r, 0, "sin_miniatura.jpg"), &c0), GAL_TH_FAILED);
    CHECK_EQ(s_lib.thumbs, 2);
    CHECK_EQ(s_lib.leases, 0);
    /* visor de una foto de la microSD: el arrendamiento dura lo que la lectura */
    gal_item_t sfc = *sf;
    CHECK(gal_view_open(r->e, &sfc));
    gal_view_t v;
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING && v.state != GAL_V_IDLE));
    CHECK_EQ(v.state, GAL_V_READY);
    CHECK(v.src_w == 40 && v.src_h == 30);
    gal_view_close(r->e);
    SPIN(r, false);
    CHECK_EQ(s_lib.leases, 0);
    CHECK(s_lib.max_leases >= 1);
    /* la microSD sale: su foto no se abre (sin colgarse) y su lista se va */
    s_lib.sd_in = false;
    s_lib.rev++;
    CHECK(gal_view_open(r->e, &sfc));
    SPIN(r, (gal_view_get(r->e, &v), v.state != GAL_V_LOADING && v.state != GAL_V_IDLE));
    CHECK_EQ(v.state, GAL_V_ERROR);
    gal_view_close(r->e);
    SPIN(r, S(r).nl == 1);
    CHECK_EQ(S(r).nl, 1);
    CHECK_EQ(s_lib.leases, 0);
    s_lib.sd_in = true;
    s_lib.rev++;
    SPIN(r, S(r).nl == 4);
    CHECK_EQ(S(r).nl, 4);
    /* borrar de la microSD: lo pide la persona y lo hace la biblioteca */
    char why[96];
    gal_item_t snc = *find_item(r, 0, "sin_miniatura.jpg");
    const int tid = gal_trash(r->e, &snc, why, sizeof(why));
    CHECK(tid > 0);
    SPIN(r, xstate(r, tid, NULL) == GAL_XS_DONE);
    char msg[96] = "";
    CHECK_EQ(xstate(r, tid, msg), GAL_XS_DONE);
    CHECK_STR(msg, "Borrada de la microSD");
    CHECK_STR(s_lib.trashed, "/DCIM/sin_miniatura.jpg");
    SPIN(r, S(r).nl == 3);
    CHECK_EQ(S(r).nl, 3);
    /* subir una foto de la microSD al P4: se lee con arrendamiento y se suelta */
    size_t pn;
    uint8_t *pic = fix_bytes("gal_tall_300x900.jpg", &pn);
    eco_p4_add(&r->eco, "Atardecer.jpg", "photo", "JPEG", pic, pn, pic, pn, false);
    gal_set_wifi(r->e, true);
    CHECK(gal_set_ultra(r->e, "192.168.1.20", 8080));
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_PAIR);
    CHECK(gal_ultra_pair(r->e, "123456"));
    SPIN(r, S(r).health[GAL_SRC_ULTRA].state == GAL_ST_READY && S(r).nr == 1);
    CHECK_EQ(S(r).nr, 1);
    sfc = *find_item(r, 0, "sd_foto.jpg");
    const int uid = gal_upload(r->e, &sfc, GAL_SRC_ULTRA, why, sizeof(why));
    CHECK(uid > 0);
    SPIN(r, xstate(r, uid, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, uid, NULL), GAL_XS_DONE);
    CHECK_EQ(r->eco.p4_uploads, 1);
    CHECK_EQ(s_lib.leases, 0);
    /* descargar con el destino elegido SIN tarjeta: no se baja ni se guarda en el reloj */
    s_lib.dest_ok = false;
    gal_item_t atc = *find_item(r, 1, "Atardecer.jpg");
    const int d1 = gal_download(r->e, &atc, why, sizeof(why));
    CHECK(d1 > 0);
    SPIN(r, xstate(r, d1, NULL) == GAL_XS_FAIL);
    CHECK_EQ(xstate(r, d1, msg), GAL_XS_FAIL);
    CHECK_STR(msg, "Has elegido guardar en la microSD y no hay tarjeta.");
    CHECK_EQ(s_lib.imports, 0);
    char lp[600];
    snprintf(lp, sizeof(lp), "%s/Imagenes/Atardecer.jpg", r->root);
    CHECK(access(lp, F_OK) != 0); /* nada en el LittleFS por detrás */
    /* con el destino listo: a la biblioteca de la microSD, byte a byte */
    s_lib.dest_ok = true;
    gal_xfer_clear(r->e);
    const int d2 = gal_download(r->e, &atc, why, sizeof(why));
    CHECK(d2 > 0);
    SPIN(r, xstate(r, d2, NULL) == GAL_XS_DONE);
    CHECK_EQ(xstate(r, d2, msg), GAL_XS_DONE);
    CHECK_STR(msg, "Guardada en microSD");
    CHECK_EQ(s_lib.imports, 1);
    snprintf(lp, sizeof(lp), "%s/vol1/Imagenes/Atardecer.jpg", r->root);
    size_t ln;
    uint8_t *ld = gf_read_file(lp, &ln);
    CHECK(ld && ln == pn && !memcmp(ld, pic, pn));
    free(ld);
    snprintf(lp, sizeof(lp), "%s/Imagenes/Atardecer.jpg", r->root);
    CHECK(access(lp, F_OK) != 0);
    SPIN(r, S(r).nl == 4);
    CHECK_EQ(S(r).nl, 4);
    CHECK_EQ(s_lib.leases, 0);
    free(pic);
    rig_free(r);
    free(r);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        FIX = argv[1];
    }
    srand(1234);
    test_json();
    test_http();
    test_pick();
    test_parsers();
    test_jpeg();
    test_acct();
    test_engine_ultra();
    test_engine_cloud();
    test_engine_auto_phone();
    test_engine_memory();
    test_engine_local_io();
    test_wm_map();
    printf("Galería: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
