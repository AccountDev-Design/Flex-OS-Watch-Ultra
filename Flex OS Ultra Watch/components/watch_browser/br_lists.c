/*
 * br_lists — ver include/br_lists.h.
 */
#include "br_lists.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void br_lists_init(br_lists_t *l)
{
    memset(l, 0, sizeof(*l));
}

static void copy(char *d, size_t n, const char *s)
{
    snprintf(d, n, "%s", s ? s : "");
    for (char *p = d; *p; p++) {
        if ((unsigned char)*p < 0x20) {
            *p = ' ';
        }
    }
}

void br_hist_add(br_lists_t *l, const char *url, const char *title, uint32_t now_ms)
{
    if (!url || !url[0] || flexBrInternalPage(url) != BRP_NONE || strlen(url) >= FLEXBR_URL_MAX) {
        return;
    }
    BrHistEntry e;
    int i = 0;
    for (; i < l->hn && strcmp(l->h[i].url, url) != 0; i++) {
    }
    if (i < l->hn) { /* ya estaba: sube arriba con una visita más */
        e = l->h[i];
        e.visits = (uint16_t)(e.visits < 0xFFFF ? e.visits + 1 : e.visits);
        memmove(&l->h[1], &l->h[0], sizeof(BrHistEntry) * (size_t)i);
    } else {
        memset(&e, 0, sizeof(e));
        copy(e.url, sizeof(e.url), url);
        e.visits = 1;
        const int n = l->hn < FLEXBR_HIST_MAX ? l->hn : FLEXBR_HIST_MAX - 1;
        memmove(&l->h[1], &l->h[0], sizeof(BrHistEntry) * (size_t)n);
        l->hn = n + 1;
    }
    if (title && title[0]) {
        copy(e.title, sizeof(e.title), title);
    }
    e.lastMs = now_ms;
    l->h[0] = e;
    l->gen++;
}

void br_hist_title(br_lists_t *l, const char *url, const char *title)
{
    for (int i = 0; url && title && title[0] && i < l->hn; i++) {
        if (strcmp(l->h[i].url, url) == 0) {
            if (strncmp(l->h[i].title, title, FLEXBR_TITLE_MAX - 1) != 0) {
                copy(l->h[i].title, sizeof(l->h[i].title), title);
                l->gen++;
            }
            return;
        }
    }
}

void br_hist_clear(br_lists_t *l)
{
    l->hn = 0;
    l->gen++;
}

int br_bm_find(const br_lists_t *l, const char *url)
{
    for (int i = 0; url && i < l->bn; i++) {
        if (strcmp(l->b[i].url, url) == 0) {
            return i;
        }
    }
    return -1;
}

bool br_bm_toggle(br_lists_t *l, const char *url, const char *title, bool *added)
{
    *added = false;
    if (!url || !url[0] || flexBrInternalPage(url) != BRP_NONE) {
        return false;
    }
    const int i = br_bm_find(l, url);
    if (i >= 0) {
        memmove(&l->b[i], &l->b[i + 1], sizeof(BrBookmark) * (size_t)(l->bn - i - 1));
        l->bn--;
        l->gen++;
        return true;
    }
    if (l->bn >= FLEXBR_BM_MAX) {
        return false;
    }
    copy(l->b[l->bn].url, FLEXBR_URL_MAX, url);
    copy(l->b[l->bn].title, FLEXBR_TITLE_MAX, title && title[0] ? title : url);
    l->bn++;
    l->gen++;
    *added = true;
    return true;
}

void br_bm_clear(br_lists_t *l)
{
    l->bn = 0;
    l->gen++;
}

/* ---- fichero: cabecera {u32 magia, u16 versión, u16 cuenta} + registros ---- */

static bool put_u(FILE *f, uint32_t v, int bytes)
{
    uint8_t b[4];
    for (int i = 0; i < bytes; i++) {
        b[i] = (uint8_t)(v >> (8 * i));
    }
    return fwrite(b, 1, (size_t)bytes, f) == (size_t)bytes;
}

static bool get_u(FILE *f, uint32_t *v, int bytes)
{
    uint8_t b[4];
    if (fread(b, 1, (size_t)bytes, f) != (size_t)bytes) {
        return false;
    }
    *v = 0;
    for (int i = 0; i < bytes; i++) {
        *v |= (uint32_t)b[i] << (8 * i);
    }
    return true;
}

static bool put_s(FILE *f, const char *s)
{
    const size_t n = strlen(s);
    return put_u(f, (uint32_t)n, 2) && fwrite(s, 1, n, f) == n;
}

static bool get_s(FILE *f, char *out, size_t cap)
{
    uint32_t n;
    if (!get_u(f, &n, 2) || n >= cap || fread(out, 1, n, f) != n) {
        return false;
    }
    out[n] = 0;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)out[i] < 0x20) {
            return false;
        }
    }
    return true;
}

bool br_lists_save(const br_lists_t *l, bool hist, const char *path)
{
    char tmp[300];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return false;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    const int n = hist ? l->hn : l->bn;
    bool ok = put_u(f, BR_FILE_MAGIC, 4) && put_u(f, BR_FILE_VER, 2) && put_u(f, (uint32_t)n, 2);
    for (int i = 0; ok && i < n; i++) {
        if (hist) {
            ok = put_s(f, l->h[i].url) && put_s(f, l->h[i].title) && put_u(f, l->h[i].lastMs, 4) &&
                 put_u(f, l->h[i].visits, 2);
        } else {
            ok = put_s(f, l->b[i].url) && put_s(f, l->b[i].title);
        }
    }
    if (fclose(f) != 0 || !ok) {
        unlink(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        unlink(path);
        if (rename(tmp, path) != 0) {
            unlink(tmp);
            return false;
        }
    }
    return true;
}

bool br_lists_load(br_lists_t *l, bool hist, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint32_t magic, ver, n;
    bool ok = get_u(f, &magic, 4) && get_u(f, &ver, 2) && get_u(f, &n, 2) && magic == BR_FILE_MAGIC &&
              ver == BR_FILE_VER && n <= (uint32_t)(hist ? FLEXBR_HIST_MAX : FLEXBR_BM_MAX);
    int got = 0;
    for (uint32_t i = 0; ok && i < n; i++) {
        if (hist) {
            BrHistEntry *e = &l->h[i];
            uint32_t ms = 0, vis = 0;
            ok = get_s(f, e->url, sizeof(e->url)) && get_s(f, e->title, sizeof(e->title)) && get_u(f, &ms, 4) &&
                 get_u(f, &vis, 2) && flexBrInternalPage(e->url) == BRP_NONE && e->url[0];
            e->lastMs = ms;
            e->visits = (uint16_t)vis;
        } else {
            BrBookmark *b = &l->b[i];
            ok = get_s(f, b->url, sizeof(b->url)) && get_s(f, b->title, sizeof(b->title)) && b->url[0];
        }
        got += ok;
    }
    fclose(f);
    if (!ok) { /* un fichero roto no deja entradas a medias: lista vacía */
        if (hist) {
            l->hn = 0;
        } else {
            l->bn = 0;
        }
        l->gen++;
        return false;
    }
    if (hist) {
        l->hn = got;
    } else {
        l->bn = got;
    }
    l->gen++;
    return true;
}
