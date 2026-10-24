/*
 * gal_e2e — el motor de la Galería del reloj (el MISMO código que va al S3)
 * contra los servidores REALES, con sockets de verdad:
 *  - Flex Web Server de Flex OS Ultra: FlexOS_MediaWeb.cpp compilado en el PC
 *    (flexweb_host de Flex-OS-Ecosistema, sin tocar);
 *  - Flex Cloud: el servidor Node de cloud/ de Flex Developer Studio en modo
 *    desarrollo, con fotos subidas como lo hace Flex Cloud Web.
 * Lo lanza tests/host/gallery_e2e.sh (que arranca los servidores).
 *
 *   gal_e2e <raíz> <fotos> <puerto_p4> <código_p4> <puerto_nube> <credencial> <fil_web>
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "flexfs.h"
#include "gal_engine.h"
#include "gal_fake.h"
#include "gal_json.h"
#include "gal_posix_io.h"
#include "ota_sig.h"

static int g_checks, g_fail;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static size_t s_cur;
static void *m_alloc(size_t n)
{
    size_t *p = malloc(n + 16);
    if (!p) {
        return NULL;
    }
    p[0] = n;
    s_cur += n;
    return (uint8_t *)p + 16;
}
static void m_free(void *q)
{
    if (q) {
        size_t *p = (size_t *)((uint8_t *)q - 16);
        s_cur -= p[0];
        free(p);
    }
}

/* almacén en memoria (NVS) */
typedef struct {
    char k[16][24];
    uint8_t v[16][128];
    size_t n[16];
    int used;
} mstore_t;
static bool ms_get(void *c, const char *k, void *b, size_t *l)
{
    mstore_t *s = c;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], k) && s->n[i] != (size_t)-1 && s->n[i] <= *l) {
            memcpy(b, s->v[i], s->n[i]);
            *l = s->n[i];
            return true;
        }
    }
    return false;
}
static bool ms_set(void *c, const char *k, const void *b, size_t l)
{
    mstore_t *s = c;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], k)) {
            memcpy(s->v[i], b, l);
            s->n[i] = l;
            return true;
        }
    }
    snprintf(s->k[s->used], 24, "%s", k);
    memcpy(s->v[s->used], b, l);
    s->n[s->used++] = l;
    return true;
}
static bool ms_erase(void *c, const char *k)
{
    mstore_t *s = c;
    for (int i = 0; i < s->used; i++) {
        if (!strcmp(s->k[i], k)) {
            s->n[i] = (size_t)-1;
        }
    }
    return true;
}
static void ms_rand(void *c, uint8_t *o, size_t n)
{
    (void)c;
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(o, 1, n, f) != n) {
        for (size_t i = 0; i < n; i++) {
            o[i] = (uint8_t)rand();
        }
    }
    if (f) {
        fclose(f);
    }
}

static char g_root[256];
static uint32_t e_now(void *c)
{
    (void)c;
    return pio_now_ms();
}
static uint64_t e_fs_free(void *c)
{
    (void)c;
    struct statvfs v;
    return statvfs(g_root, &v) == 0 ? (uint64_t)v.f_bavail * v.f_frsize : 0;
}

static gal_engine_t *g_e;
static bool e_abort(void *ud)
{
    (void)ud;
    return g_e && gal_engine_should_abort(g_e);
}

#define SPIN(secs, cond) do { const uint32_t _t0 = pio_now_ms(); \
    while (!(cond) && pio_now_ms() - _t0 < (secs) * 1000u) { const uint32_t _w = gal_engine_step(g_e); \
    usleep((_w > 20 ? 20 : _w) * 1000); } } while (0)

static gal_status_t S(void)
{
    gal_status_t s;
    gal_status(g_e, &s);
    return s;
}

static const gal_item_t *find(int which, const char *name)
{
    int n;
    gal_engine_lock(g_e);
    const gal_item_t *l = gal_list(g_e, which, &n), *f = NULL;
    for (int i = 0; i < n && !f; i++) {
        if (!strcmp(l[i].name, name)) {
            f = &l[i];
        }
    }
    gal_engine_unlock(g_e);
    return f;
}

static int xst(int id, char *msg)
{
    gal_xfer_info_t x[GAL_XFERS];
    const int n = gal_xfers(g_e, x, GAL_XFERS);
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

static bool xfin(int id)
{
    const int s = xst(id, NULL);
    return s == GAL_XS_DONE || s == GAL_XS_FAIL || s == GAL_XS_CANCEL;
}

static void cp(const char *src, const char *dst_name)
{
    char d[512];
    snprintf(d, sizeof(d), "%s/Imagenes/%s", g_root, dst_name);
    size_t n;
    uint8_t *b = gf_read_file(src, &n);
    FILE *f = fopen(d, "wb");
    if (f && b) {
        fwrite(b, 1, n, f);
    }
    if (f) {
        fclose(f);
    }
    free(b);
}

static bool same_file(const char *a, const char *b)
{
    size_t na, nb;
    uint8_t *x = gf_read_file(a, &na), *y = gf_read_file(b, &nb);
    const bool ok = x && y && na == nb && !memcmp(x, y, na);
    free(x);
    free(y);
    return ok;
}

/* Petición suelta (otro cliente del P4: el móvil, la web). */
static int raw(gal_conn_t *c, const char *method, uint16_t port, const char *path, const char *host, const char *cookie,
               bool xflex, const void *body, size_t bl, const char *ctype, char *out, size_t cap, gal_resp_t *rsp)
{
    gal_req_t rq = {.method = method, .host = host ? host : "127.0.0.1", .port = port, .path = path, .cookie = cookie,
                    .xflex = xflex, .body = body, .body_len = bl, .ctype = ctype, .range_from = -1};
    gal_resp_t rs;
    const int st = gal_http_begin(c, &rq, &rs);
    size_t n = 0;
    if (st > 0) {
        gal_http_read_all(c, out, cap, &n);
        gal_http_end(c);
    }
    if (rsp) {
        *rsp = rs;
    }
    return st;
}

/* Una petición escrita a mano a 127.0.0.1 con otra cabecera Host. Devuelve el estado. */
static int raw_host(uint16_t port, const char *host, const char *cookie)
{
    pio_t p;
    pio_init(&p);
    const gal_io_t io = pio_io(&p);
    if (io.connect(io.ctx, "127.0.0.1", port, false, 3000) != GAL_IO_OK) {
        return -1;
    }
    char rq[256], rs[64] = "";
    const int n = snprintf(rq, sizeof(rq), "GET /api/library HTTP/1.1\r\nHost: %s:%u\r\nCookie: %s\r\n\r\n", host, port, cookie);
    io.write(io.ctx, (const uint8_t *)rq, (size_t)n, 3000);
    const int r = io.read(io.ctx, (uint8_t *)rs, sizeof(rs) - 1, 3000);
    io.close(io.ctx);
    return r > 12 && !strncmp(rs, "HTTP/1.", 7) ? atoi(rs + 9) : -1;
}

int main(int argc, char **argv)
{
    if (argc < 8) {
        fprintf(stderr, "uso: gal_e2e <raíz> <fotos> <puerto_p4> <código> <puerto_nube> <credencial> <fil_web>\n");
        return 2;
    }
    snprintf(g_root, sizeof(g_root), "%s", argv[1]);
    const char *fix = argv[2];
    const uint16_t p4 = (uint16_t)atoi(argv[3]), clp = (uint16_t)atoi(argv[5]);
    const char *code = argv[4], *token = argv[6], *web_id = argv[7];
    char big[300], mid[300], quad[300];
    snprintf(big, sizeof(big), "%s/gal_big_4000x3000.jpg", fix);
    snprintf(mid, sizeof(mid), "%s/gal_mid_1600x1200.jpg", fix);
    snprintf(quad, sizeof(quad), "%s/gal_quad_640x480.jpg", fix);
    setvbuf(stdout, NULL, _IONBF, 0);
    ffs_set_root(g_root);
    ffs_init_dirs();
    ffs_mkdir("/Imagenes");
    cp(quad, "cuadros.jpg");
    cp(big, "grande.jpg");

    pio_t lan, cl;
    pio_init(&lan);
    pio_init(&cl);
    lan.cancelled = cl.cancelled = e_abort;
    const gal_io_t io_lan = pio_io(&lan), io_cl = pio_io(&cl);
    mstore_t st = {0};
    const uint8_t one = 1;
    ms_set(&st, "token", token, strlen(token)); /* lo que deja una vinculación aprobada (gal_acct persist_linked) */
    ms_set(&st, "address", "ana@flex", 8);
    ms_set(&st, "display", "Ana", 3);
    ms_set(&st, "linked", &one, 1);
    const gal_store_t store = {.ctx = &st, .get = ms_get, .set = ms_set, .erase = ms_erase, .random = ms_rand};
    const gal_env_t env = {.io_lan = &io_lan, .io_cloud = &io_cl, .store = &store, .alloc = m_alloc, .free = m_free,
                           .now = e_now, .fs_free = e_fs_free, .mac_hex = "A1B2C3D4E5F6", .version = "e2e"};
    g_e = gal_engine_new(&env);
    gal_engine_set_cloud(g_e, "127.0.0.1", clp, false, NULL, NULL);
    char why[96], msg[96], buf[4096];

    /* ======================= Flex OS Ultra ======================= */
    printf("== Flex OS Ultra (FlexOS_MediaWeb real, puerto %u)\n", p4);
    gal_set_pick(g_e, GAL_PICK_ULTRA);
    gal_set_ultra(g_e, "127.0.0.1", p4);
    gal_set_wifi(g_e, true);
    CHECK(gal_engine_open(g_e));
    SPIN(5, S().nl == 2 && S().health[GAL_SRC_ULTRA].state == GAL_ST_PAIR);
    CHECK(S().health[GAL_SRC_ULTRA].state == GAL_ST_PAIR);
    printf("   sin sesión: \"%s\"\n", S().health[GAL_SRC_ULTRA].detail);
    gal_ultra_pair(g_e, "000000");
    SPIN(5, !S().ultra_pairing);
    printf("   código equivocado: \"%s\"\n", S().ultra_pair_msg);
    CHECK(strstr(S().ultra_pair_msg, "incorrecto") != NULL);
    gal_ultra_pair(g_e, code);
    SPIN(10, S().health[GAL_SRC_ULTRA].state == GAL_ST_READY && S().remote_state == GAL_L_READY);
    CHECK(S().ultra_paired && S().health[GAL_SRC_ULTRA].state == GAL_ST_READY);
    printf("   emparejado: %s, biblioteca: %d\n", S().ultra_pair_msg, S().nr);

    /* otro cliente (el móvil) sube una foto al P4 con su propia sesión */
    gal_conn_t *oc = malloc(sizeof(*oc));
    pio_t op;
    pio_init(&op);
    const gal_io_t oio = pio_io(&op);
    gal_conn_init(oc, &oio);
    char body[16], cookie[48];
    gal_resp_t rs;
    snprintf(body, sizeof(body), "code=%s", code);
    int stt = raw(oc, "POST", p4, "/api/pair", NULL, NULL, true, body, strlen(body), "application/x-www-form-urlencoded",
                  buf, sizeof(buf), &rs);
    CHECK(stt == 200 && rs.cookie_set);
    snprintf(cookie, sizeof(cookie), "fxs=%s", rs.cookie);
    size_t mn;
    uint8_t *md = gf_read_file(mid, &mn);
    char path[256];
    snprintf(path, sizeof(path), "/api/upload?kind=photo&name=desde_el_movil.jpg&size=%zu&crc=%lu", mn,
             (unsigned long)gal_crc32(0, md, mn));
    stt = raw(oc, "POST", p4, path, NULL, cookie, true, md, mn, "application/octet-stream", buf, sizeof(buf), NULL);
    printf("   el móvil sube una foto al P4: HTTP %d %s\n", stt, buf);
    CHECK(stt == 201);
    /* seguridad del servidor real: Host, CSRF y sesión */
    CHECK(raw_host(p4, "evil.example", cookie) == 403); /* DNS rebinding: el Host tiene que ser la IP del P4 */
    CHECK(raw(oc, "POST", p4, "/api/local/delete", NULL, cookie, false, "ids=1", 5,
              "application/x-www-form-urlencoded", buf, sizeof(buf), NULL) == 403);
    CHECK(raw(oc, "GET", p4, "/api/library", NULL, NULL, false, NULL, 0, NULL, buf, sizeof(buf), NULL) == 401);
    printf("   Host ajeno 403, sin X-Flex 403, sin sesión 401: el P4 rechaza lo que debe\n");

    gal_refresh(g_e);
    SPIN(10, find(1, "desde_el_movil.jpg") != NULL);
    const gal_item_t *mv = find(1, "desde_el_movil.jpg");
    CHECK(mv != NULL);
    if (mv) {
        gal_item_t m = *mv;
        CHECK(m.size == mn && m.fmt == GAL_F_JPEG);
        /* su miniatura (la hace la tarea de miniaturas del P4: "jobs") */
        for (int k = 0; k < 15 && !(find(1, "desde_el_movil.jpg")->flags & GAL_I_THUMB); k++) {
            gal_refresh(g_e); /* la miniatura la hace el P4 un poco después */
            SPIN(1, false);
        }
        m = *find(1, "desde_el_movil.jpg");
        CHECK(m.flags & GAL_I_THUMB);
        gal_thumb_request(g_e, &m);
        const uint16_t *px = NULL;
        int ts = 0, sl = -1;
        SPIN(10, (sl = gal_thumb_acquire(g_e, &m, &px, &ts)) >= 0 || ts == GAL_TH_FAILED);
        CHECK(sl >= 0);
        if (sl >= 0) {
            int nz = 0;
            for (int i = 0; i < GAL_THUMB_SIDE * GAL_THUMB_SIDE; i++) {
                nz += px[i] != 0;
            }
            CHECK(nz > GAL_THUMB_SIDE * GAL_THUMB_SIDE / 2);
            gal_thumb_release(g_e, sl);
            printf("   miniatura del P4 decodificada (%dx%d)\n", GAL_THUMB_SIDE, GAL_THUMB_SIDE);
        }
        /* visor por flujo: el original, sin descargarlo al reloj */
        CHECK(gal_view_open(g_e, &m));
        gal_view_t v;
        SPIN(20, (gal_view_get(g_e, &v), v.state != GAL_V_LOADING));
        CHECK(v.state == GAL_V_READY && v.src_w == 1600 && v.src_h == 1200 && v.fit_w == 410 && v.zoom_w == 820);
        printf("   visor: %dx%d -> encajada %dx%d, 2x %dx%d (%llu B leídos)\n", v.src_w, v.src_h, v.fit_w, v.fit_h,
               v.zoom_w, v.zoom_h, (unsigned long long)v.done);
        gal_view_close(g_e);
        /* descarga al reloj, cortada a mitad: se reanuda con Range y queda byte a byte */
        lan.cut_after = 120000;
        const int d = gal_download(g_e, &m, why, sizeof(why));
        CHECK(d > 0);
        SPIN(60, xfin(d));
        CHECK(xst(d, msg) == GAL_XS_DONE);
        printf("   descarga del P4 (cortada a los 120 KB y reanudada): %s; cortes %d\n", msg, lan.cuts);
        char lp[512];
        snprintf(lp, sizeof(lp), "%s/Imagenes/desde_el_movil.jpg", g_root);
        CHECK(same_file(lp, mid));
        CHECK(lan.cuts >= 1);
    }
    /* subida del reloj al P4 (CRC-32 de extremo a extremo; el P4 la valida decodificándola) */
    SPIN(5, find(0, "cuadros.jpg") != NULL);
    gal_item_t q = *find(0, "cuadros.jpg");
    const int u = gal_upload(g_e, &q, GAL_SRC_ULTRA, why, sizeof(why));
    CHECK(u > 0);
    SPIN(30, xfin(u));
    CHECK(xst(u, msg) == GAL_XS_DONE);
    printf("   subida del reloj al P4: %s\n", msg);
    SPIN(10, find(1, "cuadros.jpg") != NULL); /* la subida ya pidió la lista otra vez */
    CHECK(find(1, "cuadros.jpg") != NULL);
    /* y a la papelera del P4 */
    if (find(1, "cuadros.jpg")) {
        gal_item_t t = *find(1, "cuadros.jpg");
        const int tr = gal_trash(g_e, &t, why, sizeof(why));
        SPIN(20, xfin(tr));
        CHECK(xst(tr, msg) == GAL_XS_DONE);
        printf("   papelera del P4: %s\n", msg);
        SPIN(10, !find(1, "cuadros.jpg"));
        CHECK(!find(1, "cuadros.jpg"));
    }
    free(md);

    /* ======================= Flex Cloud ======================= */
    printf("== Flex Cloud (servidor Node real, puerto %u)\n", clp);
    gal_set_pick(g_e, GAL_PICK_CLOUD);
    gal_refresh(g_e);
    SPIN(20, S().health[GAL_SRC_CLOUD].state == GAL_ST_READY && S().remote_src == GAL_SRC_CLOUD &&
                 S().remote_state == GAL_L_READY && S().nr > 0);
    gal_status_t s = S();
    CHECK(s.health[GAL_SRC_CLOUD].state == GAL_ST_READY);
    CHECK(s.q_total == 5ull * 1024 * 1024 * 1024);
    printf("   cuenta ana@flex, cuota %llu B (plan free del servidor), %d fotos\n", (unsigned long long)s.q_total, s.nr);
    gal_engine_lock(g_e);
    int nr;
    const gal_item_t *rl = gal_list(g_e, 1, &nr);
    gal_item_t w = {0};
    for (int i = 0; i < nr; i++) {
        if (!strcmp(rl[i].id, web_id)) {
            w = rl[i];
        }
    }
    gal_engine_unlock(g_e);
    CHECK(w.id[0] && (w.flags & GAL_I_THUMB) && strlen(w.sha256) == 64);
    if (w.id[0]) {
        gal_thumb_request(g_e, &w);
        const uint16_t *px;
        int ts = 0, sl = -1;
        SPIN(10, (sl = gal_thumb_acquire(g_e, &w, &px, &ts)) >= 0 || ts == GAL_TH_FAILED);
        CHECK(sl >= 0);
        gal_thumb_release(g_e, sl);
        printf("   miniatura de Flex Cloud Web (JPEG 320 px) decodificada\n");
        /* descarga cortada: Range + If-Range con la huella; SHA-256 comprobado */
        cl.cut_after = 200000;
        const int d = gal_download(g_e, &w, why, sizeof(why));
        SPIN(60, xfin(d));
        CHECK(xst(d, msg) == GAL_XS_DONE);
        char lp[512];
        snprintf(lp, sizeof(lp), "%s/Imagenes/%s", g_root, w.name);
        CHECK(same_file(lp, big));
        printf("   descarga de Flex Cloud (cortada a los 200 KB, reanudada, SHA-256 bien): %s; cortes %d\n", msg, cl.cuts);
        CHECK(cl.total_rd < (uint64_t)w.size + w.size / 2); /* no se bajó dos veces */
    }
    /* subida reanudable desde el reloj: 7 partes de 256 KB, cortada en una parte */
    SPIN(5, find(0, "grande.jpg") != NULL);
    gal_item_t g = *find(0, "grande.jpg");
    cl.cut_write_after = 3 * 262144 + 5000; /* a mitad de la cuarta parte */
    const int up = gal_upload(g_e, &g, GAL_SRC_CLOUD, why, sizeof(why));
    CHECK(up > 0);
    SPIN(90, xfin(up));
    CHECK(xst(up, msg) == GAL_XS_DONE);
    printf("   subida del reloj a Flex Cloud (cortada en la parte 4, reanudada): %s\n", msg);
    gal_refresh(g_e);
    uint8_t dg[32];
    char sha[65];
    size_t bn;
    uint8_t *bd = gf_read_file(big, &bn);
    ota_sha256(bd, bn, dg);
    gal_hex(dg, 32, sha);
    free(bd);
    bool seen = false;
    for (int k = 0; k < 100 && !seen; k++) {
        SPIN(1, false);
        gal_engine_lock(g_e);
        rl = gal_list(g_e, 1, &nr);
        for (int i = 0; i < nr; i++) {
            seen = seen || (!strcmp(rl[i].sha256, sha) && strcmp(rl[i].id, web_id) && (rl[i].flags & GAL_I_ONWATCH));
        }
        gal_engine_unlock(g_e);
    }
    CHECK(seen); /* la nube tiene el mismo archivo, y la Galería sabe que es la copia del reloj */
    printf("   la nube lista la subida del reloj con la misma huella SHA-256\n");
    /* papelera de Flex Cloud: está en la papelera de la cuenta (la ve la web) */
    gal_item_t t = w;
    const int tr = gal_trash(g_e, &t, why, sizeof(why));
    SPIN(20, xfin(tr));
    CHECK(xst(tr, msg) == GAL_XS_DONE);
    gal_conn_t *wc = malloc(sizeof(*wc));
    pio_t wp;
    pio_init(&wp);
    const gal_io_t wio = pio_io(&wp);
    gal_conn_init(wc, &wio);
    char auth[64];
    snprintf(auth, sizeof(auth), "%s", token);
    gal_req_t rq = {.method = "GET", .host = "127.0.0.1", .port = clp, .path = "/api/cloud/files?view=trash",
                    .bearer = auth, .range_from = -1};
    char *tb = malloc(65536);
    size_t tn = 0;
    CHECK(gal_http_begin(wc, &rq, &rs) == 200 && gal_http_read_all(wc, tb, 65536, &tn) == 0);
    gal_http_end(wc);
    CHECK(strstr(tb, web_id) != NULL);
    printf("   papelera de Flex Cloud: %s (la foto está en la papelera de la cuenta)\n", msg);
    /* credencial que el servidor no conoce: se pide volver a vincular, nunca "conectado" */
    rq.path = "/api/cloud/me";
    rq.bearer = "credencial-que-no-existe-aaaaaaaaaaaaaaaaaa";
    const int sme = gal_http_begin(wc, &rq, &rs);
    tn = 0;
    if (sme > 0) {
        gal_http_read_all(wc, tb, 65536, &tn);
        gal_http_end(wc);
    }
    char ecode[32] = "";
    gal_parse_cloud_error(tb, tn, ecode, sizeof(ecode), NULL, 0);
    CHECK(sme == 401 && gal_err_from_http(sme, ecode) == GAL_E_AUTH);
    printf("   credencial desconocida: HTTP %d %s -> volver a vincular\n", sme, ecode);
    free(tb);
    gal_conn_close(wc);
    free(wc);
    gal_conn_close(oc);
    free(oc);

    gal_mem_t m;
    gal_engine_mem(g_e, &m);
    printf("   peticiones %u por %u conexiones (keep-alive), miniaturas %u\n", m.http_reqs, m.http_opens, m.thumbs_done);
    gal_engine_close(g_e);
    SPIN(2, false);
    gal_engine_free(g_e);
    g_e = NULL;
    CHECK(s_cur == 0);
    printf("Galería de extremo a extremo: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
