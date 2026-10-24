/*
 * gal_fake_eco — ver gal_fake_eco.h. Las citas son del código real que imita.
 */
#include "gal_fake_eco.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gal_core.h"
#include "gal_json.h"
#include "ota_sig.h"

void eco_sha_hex(const void *d, size_t n, char out[65])
{
    uint8_t dg[32];
    ota_sha256(d, n, dg);
    gal_hex(dg, 32, out);
}

/* ======================= Flex OS Ultra ======================= */

static bool host_ok(eco_t *e, const gf_req_t *rq)
{
    char a[40], b[48];
    snprintf(a, sizeof(a), "%s", e->p4_ip);
    snprintf(b, sizeof(b), "%s:%u", e->p4_ip, e->p4_port);
    return !strcmp(rq->host, a) || !strcmp(rq->host, b); /* flexHttpHostOk */
}

static bool has_session(eco_t *e, const gf_req_t *rq)
{
    char want[48];
    snprintf(want, sizeof(want), "fxs=%s", e->session);
    return e->session[0] && !strcmp(rq->cookie, want);
}

eco_file_t *eco_find_p4(eco_t *e, uint32_t id)
{
    for (int i = 0; i < ECO_MAX; i++) {
        if (e->p4[i].used && !e->p4[i].trashed && e->p4[i].id == id) {
            return &e->p4[i];
        }
    }
    return NULL;
}

uint32_t eco_p4_add(eco_t *e, const char *name, const char *kind, const char *fmt, const void *data, size_t n,
                    const void *thumb, size_t tn, bool locked)
{
    for (int i = 0; i < ECO_MAX; i++) {
        eco_file_t *f = &e->p4[i];
        if (f->used) {
            continue;
        }
        memset(f, 0, sizeof(*f));
        f->used = true;
        f->id = ++e->p4_next;
        snprintf(f->name, sizeof(f->name), "%s", name);
        snprintf(f->kind, sizeof(f->kind), "%s", kind);
        snprintf(f->fmt, sizeof(f->fmt), "%s", fmt);
        f->data = malloc(n ? n : 1);
        memcpy(f->data, data, n);
        f->size = n;
        if (thumb && tn) {
            f->thumb = malloc(tn);
            memcpy(f->thumb, thumb, tn);
            f->thumb_len = tn;
        }
        f->locked = locked;
        f->when = 1790000000u + f->id * 60u;
        e->rev++;
        return f->id;
    }
    return 0;
}

static void p4_err(gf_resp_t *rs, int st, const char *msg, const char *more)
{
    gf_json(rs, st, "{\"error\":\"%s\"%s}", msg, more ? more : "");
}

static void body_str(const gf_req_t *rq, char *out, size_t n)
{
    const size_t k = rq->body_len < n - 1 ? rq->body_len : n - 1;
    memcpy(out, rq->body, k);
    out[k] = 0;
}

static void serve_range(gf_resp_t *rs, const gf_req_t *rq, const uint8_t *d, size_t n, const char *ctype, const char *etag,
                        int *ranges)
{
    size_t a = 0;
    bool range = false;
    if (!strncmp(rq->range, "bytes=", 6) && (!etag || !rq->if_range[0] || strstr(rq->if_range, etag))) {
        a = (size_t)strtoull(rq->range + 6, NULL, 10);
        range = true;
        if (ranges) {
            (*ranges)++;
        }
    }
    if (range && a >= n) {
        rs->status = 416;
        snprintf(rs->extra, sizeof(rs->extra), "Content-Range: bytes */%zu\r\n", n);
        return;
    }
    gf_bytes(rs, range ? 206 : 200, ctype, d + a, n - a);
    size_t w = (size_t)snprintf(rs->extra, sizeof(rs->extra), "Accept-Ranges: bytes\r\n");
    if (etag) {
        w += (size_t)snprintf(rs->extra + w, sizeof(rs->extra) - w, "ETag: \"%s\"\r\n", etag);
    }
    if (range) {
        snprintf(rs->extra + w, sizeof(rs->extra) - w, "Content-Range: bytes %zu-%zu/%zu\r\n", a, n - 1, n);
    }
}

static void cloud_handle(eco_t *e, eco_file_t *tab, bool phone, const gf_req_t *rq, const char *p, gf_resp_t *rs);

static void p4_handle(void *ud, const gf_req_t *rq, gf_resp_t *rs)
{
    eco_t *e = ud;
    const char *p = rq->path;
    const bool get = !strcmp(rq->method, "GET");
    if (e->p4_down) { /* apagado: no contesta y el cliente agota su plazo */
        rs->stall = true;
        rs->close = true;
        return;
    }
    if (!host_ok(e, rq)) {
        p4_err(rs, 403, "Host no válido", NULL);
        rs->close = true;
        return;
    }
    if (!get && !rq->xflex) { /* defensa CSRF (route) */
        p4_err(rs, 403, "Petición no permitida", NULL);
        rs->close = true;
        return;
    }
    if (e->p4_busy) {
        p4_err(rs, 503, "Sistema ocupado", NULL);
        return;
    }
    if (get && !strcmp(p, "/api/hello")) {
        gf_json(rs, 200, "{\"paired\":%d,\"owner\":0,\"lockType\":1,\"up\":1,\"dev\":\"Flex OS Ultra\"}", has_session(e, rq));
        return;
    }
    if (!get && !strcmp(p, "/api/pair")) {
        char body[64], code[16] = "";
        body_str(rq, body, sizeof(body));
        if (!strncmp(body, "code=", 5)) { /* application/x-www-form-urlencoded (handlePair) */
            snprintf(code, sizeof(code), "%.15s", body + 5);
        }
        if (e->pair_fails >= 5) {
            p4_err(rs, 429, "Demasiados intentos. Espera antes de volver a probar", ",\"wait\":30");
            return;
        }
        if (strcmp(code, e->code)) {
            e->pair_fails++;
            char more[24];
            snprintf(more, sizeof(more), ",\"left\":%d", 5 - e->pair_fails);
            p4_err(rs, 403, "Código incorrecto", more);
            return;
        }
        e->pair_fails = 0;
        for (int i = 0; i < 32; i++) {
            e->session[i] = "0123456789abcdef"[(unsigned)rand() & 15];
        }
        e->session[32] = 0;
        gf_json(rs, 200, "{\"ok\":1}");
        snprintf(rs->extra, sizeof(rs->extra), "Set-Cookie: fxs=%s; Path=/; HttpOnly; SameSite=Strict\r\n", e->session);
        return;
    }
    if (!has_session(e, rq)) {
        p4_err(rs, 401, "Conecta primero con el código que aparece en Flex OS", ",\"pair\":1");
        return;
    }
    uint32_t id = 0;
    if (get && !strncmp(p, "/api/library", 12)) {
        char since[16];
        gf_query(p, "since", since, sizeof(since));
        if (since[0] && (uint32_t)strtoul(since, NULL, 10) == e->rev) {
            gf_json(rs, 200, "{\"rev\":%u,\"same\":1,\"owner\":0}", e->rev);
            return;
        }
        size_t cap = 256 + ECO_MAX * 400, w = 0;
        char *b = malloc(cap);
        w += (size_t)snprintf(b + w, cap - w,
                              "{\"rev\":%u,\"owner\":0,\"lockType\":1,\"free\":1000000,\"total\":11000000,\"reserve\":524288,"
                              "\"up\":1,\"lim\":{\"photo\":6291456,\"video\":8388608,\"audio\":8388608},\"items\":[",
                              e->rev);
        bool first = true;
        for (int i = 0; i < ECO_MAX; i++) {
            const eco_file_t *f = &e->p4[i];
            if (!f->used || f->trashed) {
                continue;
            }
            if (f->locked) {
                w += (size_t)snprintf(b + w, cap - w, "%s{\"id\":%u,\"lock\":1}", first ? "" : ",", f->id);
            } else {
                w += (size_t)snprintf(b + w, cap - w,
                                      "%s{\"id\":%u,\"k\":\"%s\",\"s\":%zu,\"c\":%llu,\"a\":%llu,\"m\":%llu,\"w\":0,\"h\":0,"
                                      "\"d\":0,\"st\":\"ready\",\"e\":0,\"p\":1,\"lock\":0%s,\"fmt\":\"%s\",\"n\":\"%s\","
                                      "\"fn\":\"%s\"}",
                                      first ? "" : ",", f->id, f->kind, f->size, (unsigned long long)f->when,
                                      (unsigned long long)f->when, (unsigned long long)f->when, f->thumb ? ",\"t\":1" : "",
                                      f->fmt, f->name, f->name);
            }
            first = false;
        }
        w += (size_t)snprintf(b + w, cap - w, "]}");
        rs->body = (uint8_t *)b;
        rs->len = w;
        rs->status = 200;
        rs->chunked = true; /* el real manda la biblioteca por trozos (handleLibrary) */
        snprintf(rs->ctype, sizeof(rs->ctype), "application/json; charset=utf-8");
        return;
    }
    if (get && sscanf(p, "/api/thumb/%u", &id) == 1) {
        eco_file_t *f = eco_find_p4(e, id);
        if (!f) {
            p4_err(rs, 404, "No existe", NULL);
        } else if (f->locked) {
            p4_err(rs, 403, "Contenido protegido", ",\"lock\":1");
        } else if (!f->thumb) {
            p4_err(rs, 404, "Sin miniatura", NULL);
        } else {
            gf_bytes(rs, 200, "image/jpeg", f->thumb, f->thumb_len);
        }
        return;
    }
    if (get && sscanf(p, "/api/file/%u", &id) == 1) {
        eco_file_t *f = eco_find_p4(e, id);
        if (!f) {
            p4_err(rs, 404, "No existe", NULL);
        } else if (f->locked) {
            p4_err(rs, 403, "Contenido protegido", ",\"lock\":1");
        } else {
            serve_range(rs, rq, f->data, f->size, "image/jpeg", NULL, &e->range_requests);
        }
        return;
    }
    if (!get && !strncmp(p, "/api/upload?", 12)) {
        char kind[16], name[64], size[24], crc[24];
        gf_query(p, "kind", kind, sizeof(kind));
        gf_query(p, "name", name, sizeof(name));
        gf_query(p, "size", size, sizeof(size));
        gf_query(p, "crc", crc, sizeof(crc));
        if (strtoull(size, NULL, 10) != rq->body_len) {
            p4_err(rs, 400, "El tamaño declarado no coincide con lo que se envía", NULL);
            return;
        }
        if ((uint32_t)strtoul(crc, NULL, 10) != gal_crc32(0, rq->body, rq->body_len)) {
            p4_err(rs, 422, "La suma de comprobación no coincide", NULL);
            return;
        }
        for (int i = 0; i < ECO_MAX; i++) {
            const eco_file_t *f = &e->p4[i];
            if (f->used && !f->trashed && f->size == rq->body_len && !memcmp(f->data, rq->body, f->size)) {
                gf_json(rs, 200, "{\"id\":%u,\"dup\":1}", f->id);
                return;
            }
        }
        const uint32_t nid = eco_p4_add(e, name, "photo", "JPEG", rq->body, rq->body_len, rq->body, rq->body_len, false);
        e->p4_uploads++;
        gf_json(rs, 201, "{\"id\":%u,\"p\":1,\"thumb\":1}", nid);
        return;
    }
    if (!get && !strcmp(p, "/api/local/delete")) {
        char body[64];
        body_str(rq, body, sizeof(body));
        id = (uint32_t)strtoul(body + 4, NULL, 10);
        eco_file_t *f = eco_find_p4(e, id);
        if (!f) {
            p4_err(rs, 404, "No existe", NULL);
        } else if (f->locked) {
            p4_err(rs, 403, "Contenido protegido", ",\"lock\":1");
        } else {
            f->trashed = true;
            e->rev++;
            e->p4_deletes++;
            gf_json(rs, 200, "{\"ok\":1,\"failed\":[]}");
        }
        return;
    }
    if (get && !strcmp(p, "/api/fs/overview")) {
        gf_json(rs, 200,
                "{\"local\":{\"total\":11000000,\"free\":1000000},\"phone\":{\"state\":\"%s\",\"name\":\"Pixel de Ana\","
                "\"model\":\"Pixel 8\",\"ip\":\"192.168.1.30\",\"port\":47830,\"reachable\":1,\"lastOkS\":3,\"pairWaiting\":0},"
                "\"cloud\":null}",
                e->phone_state);
        return;
    }
    if (!strncmp(p, "/api/cloud/", 11)) {
        if (strcmp(e->phone_state, "ready")) { /* cloudOpen: "no_phone" */
            gf_json(rs, 503, "{\"ok\":false,\"error\":{\"code\":\"no_phone\",\"message\":\"Sin teléfono\"}}");
            return;
        }
        cloud_handle(e, e->ph, true, rq, p + 10, rs);
        return;
    }
    p4_err(rs, 404, "Aquí no hay nada", NULL);
}

/* ======================= Flex Cloud ======================= */

static void cl_err(gf_resp_t *rs, int st, const char *code)
{
    gf_json(rs, st, "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}", code, code);
}

eco_file_t *eco_find_cl(eco_t *e, eco_file_t *tab, const char *fid)
{
    (void)e;
    for (int i = 0; i < ECO_MAX; i++) {
        char id[40];
        snprintf(id, sizeof(id), "fil_%024u", tab[i].id);
        if (tab[i].used && !tab[i].trashed && !strcmp(id, fid)) {
            return &tab[i];
        }
    }
    return NULL;
}

int eco_cloud_add(eco_t *e, eco_file_t *tab, const char *name, const char *mime, const void *data, size_t n,
                  const void *thumb, size_t tn, const char *thumb_mime)
{
    for (int i = 0; i < ECO_MAX; i++) {
        eco_file_t *f = &tab[i];
        if (f->used) {
            continue;
        }
        memset(f, 0, sizeof(*f));
        f->used = true;
        f->id = (uint32_t)++e->cl_next;
        snprintf(f->name, sizeof(f->name), "%s", name);
        snprintf(f->mime, sizeof(f->mime), "%s", mime);
        snprintf(f->kind, sizeof(f->kind), "%s", !strncmp(mime, "video/", 6) ? "video" : "photo");
        f->data = malloc(n ? n : 1);
        memcpy(f->data, data, n);
        f->size = n;
        eco_sha_hex(data, n, f->sha);
        if (thumb && tn) {
            f->thumb = malloc(tn);
            memcpy(f->thumb, thumb, tn);
            f->thumb_len = tn;
            snprintf(f->thumb_mime, sizeof(f->thumb_mime), "%s", thumb_mime ? thumb_mime : "image/jpeg");
        }
        f->when = 1790000000000ull + (uint64_t)f->id * 60000u;
        e->quota_used += n;
        return (int)f->id;
    }
    return 0;
}

static int file_json(char *b, size_t cap, const eco_file_t *f)
{
    return snprintf(b, cap,
                    "{\"type\":\"file\",\"id\":\"fil_%024u\",\"name\":\"%s\",\"parentId\":null,\"size\":%zu,\"mime\":\"%s\","
                    "\"kind\":\"%s\",\"sha256\":\"%s\",\"version\":1,\"status\":\"ready\",\"storageLocation\":\"cloud\","
                    "\"source\":\"web\",\"hasThumbnail\":%s,\"metadata\":null,\"createdAt\":%llu,\"updatedAt\":%llu}",
                    f->id, f->name, f->size, f->mime, f->kind, f->sha, f->thumb ? "true" : "false",
                    (unsigned long long)f->when, (unsigned long long)f->when);
}

static eco_upload_t *find_up(eco_t *e, const char *id)
{
    for (int i = 0; i < 8; i++) {
        if (e->up[i].used && !strcmp(e->up[i].id, id)) {
            return &e->up[i];
        }
    }
    return NULL;
}

static int up_json(char *b, size_t cap, const eco_upload_t *u, bool resumed)
{
    int w = snprintf(b, cap,
                     "{\"ok\":true,\"upload\":{\"uploadId\":\"%s\",\"name\":\"%s\",\"size\":%llu,\"chunkSize\":%llu,"
                     "\"totalParts\":%u,\"receivedParts\":[",
                     u->id, u->name, (unsigned long long)u->size, (unsigned long long)u->chunk, u->parts);
    bool first = true;
    for (uint32_t p = 1; p <= u->parts; p++) {
        if (u->got[(p - 1) / 8] >> ((p - 1) % 8) & 1) {
            w += snprintf(b + w, cap - (size_t)w, "%s%u", first ? "" : ",", p);
            first = false;
        }
    }
    w += snprintf(b + w, cap - (size_t)w, "],\"state\":\"%s\",\"resumed\":%s,\"sha256\":\"%s\"}}", u->state,
                  resumed ? "true" : "false", u->sha);
    return w;
}

static void cloud_handle(eco_t *e, eco_file_t *tab, bool phone, const gf_req_t *rq, const char *p, gf_resp_t *rs)
{
    const bool get = !strcmp(rq->method, "GET");
    char fid[48], uid[48];
    unsigned pn;
    if (get && !strcmp(p, "/health")) {
        gf_json(rs, 200, "{\"ok\":true,\"service\":\"flex-cloud\",\"version\":1,\"time\":1}");
        return;
    }
    if (!phone) {
        char want[64];
        snprintf(want, sizeof(want), "Bearer %s", e->bearer);
        if (e->revoked) {
            cl_err(rs, 401, "device_revoked");
            return;
        }
        if (!e->bearer[0] || strcmp(rq->auth, want)) {
            cl_err(rs, 401, "auth_required");
            return;
        }
    }
    if (get && !strcmp(p, "/me")) {
        gf_json(rs, 200,
                "{\"ok\":true,\"account\":{\"id\":\"%s\",\"flexAddress\":\"ana@flex\",\"displayName\":\"Ana\"},"
                "\"device\":{\"id\":\"dev_1\",\"label\":\"Flex OS Watch Ultra\"%s},\"quota\":{\"totalBytes\":%llu,"
                "\"usedBytes\":%llu,\"availableBytes\":%llu,\"state\":\"ok\",\"plan\":\"free\"},\"limits\":{\"maxFileBytes\":"
                "53687091200,\"chunkDefault\":8388608,\"chunkMin\":65536,\"chunkMax\":%s}}",
                phone ? "phone:p1" : "acc_1", phone ? ",\"kind\":\"phone\"" : "", (unsigned long long)e->quota_total,
                (unsigned long long)e->quota_used, (unsigned long long)(e->quota_total - e->quota_used),
                phone ? "16777216" : "67108864");
        return;
    }
    if (get && !strncmp(p, "/files?", 7)) {
        char view[16], kind[16], lim[8], cur[16];
        gf_query(p, "view", view, sizeof(view));
        gf_query(p, "kind", kind, sizeof(kind));
        gf_query(p, "limit", lim, sizeof(lim));
        gf_query(p, "cursor", cur, sizeof(cur));
        int limit = e->page ? e->page : (lim[0] ? atoi(lim) : 100);
        const int off = atoi(cur);
        /* recientes primero (ORDER BY updated_at DESC) */
        int order[ECO_MAX], n = 0;
        for (int i = 0; i < ECO_MAX; i++) {
            const eco_file_t *f = &tab[i];
            if (f->used && !f->trashed && (strcmp(kind, "media") || !strcmp(f->kind, "photo") || !strcmp(f->kind, "video"))) {
                order[n++] = i;
            }
        }
        for (int i = 1; i < n; i++) {
            const int x = order[i];
            int k = i - 1;
            while (k >= 0 && tab[order[k]].when < tab[x].when) {
                order[k + 1] = order[k];
                k--;
            }
            order[k + 1] = x;
        }
        const size_t cap = 256 + ECO_MAX * 700;
        char *b = malloc(cap);
        size_t w = (size_t)snprintf(b, cap, "{\"ok\":true,\"items\":[");
        int shown = 0;
        for (int i = off; i < n && shown < limit; i++, shown++) {
            w += (size_t)snprintf(b + w, cap - w, "%s", shown ? "," : "");
            w += (size_t)file_json(b + w, cap - w, &tab[order[i]]);
        }
        if (off + shown < n) {
            w += (size_t)snprintf(b + w, cap - w, "],\"nextCursor\":\"%d\",\"folder\":null}", off + shown);
        } else {
            w += (size_t)snprintf(b + w, cap - w, "],\"nextCursor\":null,\"folder\":null}");
        }
        rs->body = (uint8_t *)b;
        rs->len = w;
        rs->status = 200;
        snprintf(rs->ctype, sizeof(rs->ctype), "application/json; charset=utf-8");
        return;
    }
    if (get && sscanf(p, "/files/%40[^/]/thumbnail", fid) == 1) {
        eco_file_t *f = eco_find_cl(e, tab, fid);
        if (!f || !f->thumb) {
            cl_err(rs, 404, "not_found");
        } else {
            e->thumbs_served++;
            gf_bytes(rs, 200, f->thumb_mime, f->thumb, f->thumb_len);
        }
        return;
    }
    if (get && sscanf(p, "/download/%40s", fid) == 1) {
        eco_file_t *f = eco_find_cl(e, tab, fid);
        if (!f) {
            cl_err(rs, 404, "not_found");
            return;
        }
        serve_range(rs, rq, f->data, f->size, f->mime, f->sha, &e->range_requests);
        if (e->corrupt_download > 0 && rs->len) {
            e->corrupt_download--;
            rs->body[rs->len / 2] ^= 0x5A;
        }
        if (e->cut_download_at > 0) {
            rs->cut_after = 300 + e->cut_download_at; /* cabeceras + parte del cuerpo */
            e->cut_download_at = 0;
        }
        return;
    }
    if (!strcmp(rq->method, "DELETE") && sscanf(p, "/files/%40s", fid) == 1) {
        eco_file_t *f = eco_find_cl(e, tab, fid);
        if (!f) {
            cl_err(rs, 404, "not_found");
        } else {
            f->trashed = true;
            gf_json(rs, 200, "{\"ok\":true,\"trashed\":true}");
        }
        return;
    }
    if (!strcmp(rq->method, "POST") && !strcmp(p, "/uploads")) {
        char js[1024];
        body_str(rq, js, sizeof(js));
        const char *je = js + strlen(js);
        char name[64], key[128], sha[65];
        uint64_t size = 0, chunk = 0;
        if (!gal_j_get_str(js, je, "name", name, sizeof(name)) || !gal_j_get_u64(js, je, "size", &size) ||
            !gal_j_get_u64(js, je, "chunkSize", &chunk) || chunk < 65536) {
            cl_err(rs, 400, "invalid_request");
            return;
        }
        gal_j_get_str(js, je, "clientKey", key, sizeof(key));
        gal_j_get_str(js, je, "sha256", sha, sizeof(sha));
        for (int i = 0; i < 8; i++) {
            eco_upload_t *u = &e->up[i];
            if (u->used && !strcmp(u->state, "active") && !strcmp(u->key, key) && u->size == size) {
                char b[2048];
                gf_json(rs, 200, "%.*s", up_json(b, sizeof(b), u, true), b);
                return;
            }
        }
        if (e->quota_used + size > e->quota_total) {
            cl_err(rs, 507, "quota_exceeded");
            return;
        }
        eco_upload_t *u = NULL;
        for (int i = 0; i < 8 && !u; i++) {
            if (!e->up[i].used || strcmp(e->up[i].state, "active")) {
                u = &e->up[i];
            }
        }
        free(u->data);
        memset(u, 0, sizeof(*u));
        u->used = true;
        snprintf(u->id, sizeof(u->id), "upl_%024d", ++e->up_next);
        snprintf(u->name, sizeof(u->name), "%s", name);
        snprintf(u->key, sizeof(u->key), "%s", key);
        snprintf(u->sha, sizeof(u->sha), "%s", sha);
        snprintf(u->state, sizeof(u->state), "active");
        u->size = size;
        u->chunk = chunk;
        u->parts = (uint32_t)((size + chunk - 1) / chunk);
        u->data = malloc(size ? size : 1);
        char b[2048];
        gf_json(rs, 201, "%.*s", up_json(b, sizeof(b), u, false), b);
        return;
    }
    if (!strcmp(rq->method, "PUT") && sscanf(p, "/uploads/%40[^/]/parts/%u", uid, &pn) == 2) {
        eco_upload_t *u = find_up(e, uid);
        if (!u) {
            cl_err(rs, 404, "upload_not_found");
            return;
        }
        if (pn < 1 || pn > u->parts) {
            cl_err(rs, 400, "part_out_of_range");
            return;
        }
        const uint64_t off = (uint64_t)(pn - 1) * u->chunk, len = u->size - off < u->chunk ? u->size - off : u->chunk;
        char sha[65];
        eco_sha_hex(rq->body, rq->body_len, sha);
        if (rq->body_len != len) {
            cl_err(rs, 400, "part_size_mismatch");
            return;
        }
        if (strcmp(sha, rq->part_sha)) {
            cl_err(rs, 422, "checksum_mismatch");
            return;
        }
        e->part_puts++;
        if (e->cut_part == (int)pn) {
            e->cut_part = 0;
            rs->status = 200;
            rs->cut_after = 0; /* la conexión se corta antes de la respuesta: la parte NO se guarda */
            return;
        }
        memcpy(u->data + off, rq->body, len);
        u->got[(pn - 1) / 8] |= (uint8_t)(1u << ((pn - 1) % 8));
        gf_json(rs, 200, "{\"ok\":true,\"alreadyReceived\":false}");
        return;
    }
    if (get && sscanf(p, "/uploads/%40s", uid) == 1) {
        eco_upload_t *u = find_up(e, uid);
        if (!u) {
            cl_err(rs, 404, "upload_not_found");
            return;
        }
        char b[2048];
        gf_json(rs, 200, "%.*s", up_json(b, sizeof(b), u, false), b);
        return;
    }
    if (!strcmp(rq->method, "POST") && sscanf(p, "/uploads/%40[^/]/complete", uid) == 1) {
        eco_upload_t *u = find_up(e, uid);
        if (!u) {
            cl_err(rs, 404, "upload_not_found");
            return;
        }
        for (uint32_t k = 1; k <= u->parts; k++) {
            if (!(u->got[(k - 1) / 8] >> ((k - 1) % 8) & 1)) {
                gf_json(rs, 409, "{\"ok\":false,\"error\":{\"code\":\"incomplete_upload\",\"message\":\"x\",\"details\":{\"missing\":[%u]}}}", k);
                return;
            }
        }
        char sha[65];
        eco_sha_hex(u->data, u->size, sha);
        if (u->sha[0] && strcmp(sha, u->sha)) {
            cl_err(rs, 422, "checksum_mismatch");
            return;
        }
        e->completes++;
        snprintf(u->state, sizeof(u->state), "completed");
        const int id = eco_cloud_add(e, tab, u->name, "image/jpeg", u->data, u->size, NULL, 0, NULL);
        eco_file_t *f = NULL;
        for (int i = 0; i < ECO_MAX; i++) {
            if (tab[i].used && (int)tab[i].id == id) {
                f = &tab[i];
            }
        }
        char b[1024];
        file_json(b, sizeof(b), f);
        gf_json(rs, 200, "{\"ok\":true,\"file\":%s,\"quota\":{}}", b);
        return;
    }
    if (!strcmp(rq->method, "DELETE") && sscanf(p, "/uploads/%40s", uid) == 1) {
        eco_upload_t *u = find_up(e, uid);
        if (u) {
            snprintf(u->state, sizeof(u->state), "aborted");
        }
        gf_json(rs, 200, "{\"ok\":true}");
        return;
    }
    cl_err(rs, 404, "not_found");
}

static void cloud_root(void *ud, const gf_req_t *rq, gf_resp_t *rs)
{
    eco_t *e = ud;
    if (e->cloud_500) {
        gf_bytes(rs, 503, "text/html", "<html>bad gateway</html>", 24);
        return;
    }
    if (strncmp(rq->path, "/api/cloud/", 11)) {
        cl_err(rs, 404, "not_found");
        return;
    }
    cloud_handle(e, e->cl, false, rq, rq->path + 10, rs);
}

void eco_init(eco_t *e)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->p4_ip, sizeof(e->p4_ip), "192.168.1.20");
    e->p4_port = 8080;
    snprintf(e->code, sizeof(e->code), "123456");
    snprintf(e->phone_state, sizeof(e->phone_state), "none");
    e->quota_total = 5ull * 1024 * 1024 * 1024; /* DEFAULT_PLANS.free */
    e->rev = 1;
    gf_init(&e->lan, p4_handle, e, &e->clock);
    gf_init(&e->cloud, cloud_root, e, &e->clock);
}

static void free_tab(eco_file_t *t)
{
    for (int i = 0; i < ECO_MAX; i++) {
        free(t[i].data);
        free(t[i].thumb);
    }
}

void eco_free(eco_t *e)
{
    free_tab(e->p4);
    free_tab(e->cl);
    free_tab(e->ph);
    for (int i = 0; i < 8; i++) {
        free(e->up[i].data);
    }
    gf_free(&e->lan);
    gf_free(&e->cloud);
}
