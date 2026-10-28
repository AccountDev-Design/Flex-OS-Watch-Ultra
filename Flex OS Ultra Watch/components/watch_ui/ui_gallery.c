/*
 * ui_gallery — app Galería del reloj (Flex Gallery de Flex OS Ultra, IC_GALERIA).
 *
 * La interfaz solo pide y lee instantáneas: todo lo que bloquea (red,
 * LittleFS, decodificar) lo hace el motor (watch_gallery / gal_engine) en su
 * tarea. Aquí, en la tarea de LVGL:
 *  - rejilla de 3 columnas con miniaturas de GAL_THUMB_SIDE px. Es VIRTUAL:
 *    un juego fijo de POOL celdas se recoloca al desplazar, así los objetos de
 *    LVGL y las miniaturas fijadas no crecen con el número de fotos;
 *  - pestañas Todas / Reloj / la fuente remota en uso y, encima de la
 *    rejilla, de dónde salen las fotos y por qué (Automático lo dice);
 *  - cada foto lleva su origen (Reloj, Ultra, Cloud, Phone) y la marca de
 *    "ya está en el reloj";
 *  - visor a pantalla completa: deslizar para la siguiente o la anterior,
 *    doble toque para el 2x y arrastrar para moverse por él;
 *  - información y acciones de una foto: SOLO las que funcionan ahora
 *    (gal_can), con su motivo si no;
 *  - Origen: Automático, Flex OS Ultra, Flex Cloud y Flex Phone (este, solo
 *    si Flex OS Ultra tiene un teléfono listo); emparejar con el P4 y
 *    vincular la Flex Account (la misma de Flex OS Ultra y Flex Cloud Web);
 *  - transferencias con su progreso, cancelar y reintentar.
 * Al suspenderse la app se sueltan miniaturas y visor (watch_gallery_close);
 * las transferencias siguen. Los dibujos de /Paint se abren en Paint.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "flex_safe.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "FlexOS_MediaLib.h"
#include "watch_gallery.h"
#include "watch_media.h"
#include "watch_theme.h"
#include "watch_time.h"
#include "wt_time.h"

#define TICK_MS      150
#define RETRY_MS     3000
#define CONFIRM_MS   3000
#define CELL         GAL_THUMB_SIDE /* 112 */
#define GAP          8
#define PITCH        (CELL + GAP)
#define COLS         3
#define POOL_ROWS    5 /* filas que pueden asomar a la vez en el desplazamiento (+1) */
#define POOL         (COLS * POOL_ROWS)
#define GRID_W       (COLS * CELL + (COLS - 1) * GAP)
#define GRID_X       ((FLEX_SCREEN_W - GRID_W) / 2)
#define TABS_Y       (FLEX_HDR_H + 2)
#define TABS_H       44
#define SC_Y         (TABS_Y + TABS_H + 6)
#define SC_H         (FLEX_SCREEN_H - FLEX_LIST_BOTTOM - SC_Y)
#define CHIP_H       60
#define GRID_Y       (CHIP_H + 8) /* primera fila, dentro del desplazamiento */
#define XP_W         120 /* indicador de transferencias en la cabecera */
#define XP_H         40
#define SWIPE_PX     70
#define TAP_PX       14
#define DTAP_MS      350
#define ITEMS_STEP   64

/* Pestañas: Fotos (fotos y dibujos) y Vídeos; "+" abre lo de añadir y las fuentes. */
enum { KT_PHOTOS = 0, KT_VIDEOS, KT_N };

typedef struct {
    lv_obj_t *box, *img, *cv, *name, *pill, *pill_l, *mark, *play;
    lv_image_dsc_t dsc;
    uint16_t *draw; /* píxeles del dibujo (PSRAM), solo mientras la celda enseña uno */
    gal_item_t key; /* lo que enseña (copia: la lista del motor cambia sola) */
    int idx;        /* elemento, -1 = libre */
    int slot;       /* miniatura fijada, -1 = ninguna */
    int row, col;
    bool want;      /* tiene miniatura posible y aún no está */
} cell_t;

typedef struct {
    gal_engine_t *e;
    lv_obj_t *page, *tabs[KT_N], *plus, *scroll, *chip, *chip_dot, *chip_l;
    lv_obj_t *card, *card_t, *card_s, *card_b, *card_bl, *spacer, *xp, *xp_ok, *xp_err, *xp_l;
    cell_t cell[POOL];
    gal_item_t *items; /* la pestaña, en PSRAM */
    int n, cap;
    gal_tab_t tab;      /* la vista del motor: siempre "Todas" (reloj, microSD y la fuente remota) */
    int ktab;           /* KT_PHOTOS o KT_VIDEOS */
    gal_status_t st;
    uint32_t lgen, rgen, retry_ms, more_ms;
    int card_act; /* qué hace el botón de la tarjeta */
    char chip_in[200];
    bool suspended, dirty, nomem;
    gal_item_t sel; /* el de la página de información o el visor */
} gal_ui_t;

static gal_ui_t *G;

enum { CARD_NONE = 0, CARD_RETRY, CARD_ORIGIN, CARD_XFER };

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* ======================= utilidades ======================= */

/* Texto que cabe en `lines` líneas de w px con la fuente f (recortado con "…"
 * si no) y sin glifos que la fuente no tenga. */
static void fit_text(const char *in, char *out, size_t n, const lv_font_t *f, int32_t w, int lines)
{
    char tmp[256];
    ui_phone_text(in ? in : "", tmp, sizeof(tmp), f);
    const int32_t lh = lv_font_get_line_height(f);
    lv_point_t sz;
    size_t len = strlen(tmp);
    gal_copyz(out, n, tmp);
    lv_text_get_size(&sz, out, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    while (sz.y > lh * lines && len > 0) {
        do {
            len--;
        } while (len > 0 && ((unsigned char)tmp[len] & 0xC0) == 0x80);
        while (len > 0 && tmp[len - 1] == ' ') {
            len--;
        }
        snprintf(out, n, "%.*s…", (int)len, tmp);
        lv_text_get_size(&sz, out, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    }
}

static lv_obj_t *wrap_label(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, int32_t w)
{
    lv_obj_t *l = watch_theme_label(parent, f, rgb, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, w);
    return l;
}

/* Texto suelto dentro de una lista (watch_theme_list): con el aire de las filas */
#define LIST_TW (FLEX_SCREEN_W - 2 * FLEX_LIST_X - 16)
static lv_obj_t *list_label(lv_obj_t *list, const lv_font_t *f, uint32_t rgb)
{
    lv_obj_t *l = watch_theme_label(list, f, rgb, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_hor(l, 8, 0);
    return l;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static uint32_t src_rgb(int src)
{
    switch (src) {
    case GAL_SRC_LOCAL: return 0x2F8F5B;
    case GAL_SRC_ULTRA: return 0x4A55C8;
    case GAL_SRC_CLOUD: return 0x2C74C9;
    case GAL_SRC_PHONE: return 0xB8772C;
    default: return FLEX_C_SURF2;
    }
}

/* Nombre corto para la marca de origen de una celda */
static const char *src_short(int src)
{
    switch (src) {
    case GAL_SRC_LOCAL: return "Reloj";
    case GAL_SRC_ULTRA: return "Ultra";
    case GAL_SRC_CLOUD: return "Cloud";
    case GAL_SRC_PHONE: return "Phone";
    default: return "";
    }
}

static uint32_t state_rgb(uint8_t st)
{
    switch (st) {
    case GAL_ST_READY: return FLEX_C_OK;
    case GAL_ST_CHECKING: return FLEX_C_WARN;
    case GAL_ST_PAIR:
    case GAL_ST_DOWN: return FLEX_C_ERR;
    default: return FLEX_C_MUTE;
    }
}

static lv_obj_t *dot(lv_obj_t *parent, int32_t d)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static bool same_item(const gal_item_t *a, const gal_item_t *b)
{
    return a->src == b->src && a->vol == b->vol && a->tver == b->tver && !strcmp(a->id, b->id) &&
           !strcmp(a->path, b->path);
}

static bool thumbable(const gal_item_t *it)
{
    if (it->flags & GAL_I_LOCKED) {
        return false;
    }
    if (it->src == GAL_SRC_LOCAL) {
        /* la miniatura de la biblioteca (también de vídeos y dibujos) o un JPEG del reloj */
        return (it->flags & GAL_I_THUMB) || (it->kind == GAL_K_PHOTO && it->fmt == GAL_F_JPEG);
    }
    return it->kind != GAL_K_DRAW && (it->flags & GAL_I_THUMB) != 0;
}

static bool viewable(const gal_item_t *it)
{
    return !(it->flags & GAL_I_LOCKED) && it->kind != GAL_K_DRAW && gal_fmt_viewable((gal_fmt_t)it->fmt);
}

/* Un vídeo del reloj o de la microSD que se puede reproducir (AVI MJPEG). */
static bool playable_video(const gal_item_t *it)
{
    return it->src == GAL_SRC_LOCAL && it->kind == GAL_K_VIDEO && it->lfmt == FML_F_AVI_MJPEG &&
           !(it->flags & GAL_I_LOCKED);
}

static void fmt_when(int64_t ms, char *b, size_t n)
{
    static const char *const kMon[] = {"ene", "feb", "mar", "abr", "may", "jun",
                                       "jul", "ago", "sep", "oct", "nov", "dic"};
    if (ms <= 0) {
        gal_copyz(b, n, "No se sabe");
        return;
    }
    wt_datetime_t dt;
    wt_utc_to_local(ms / 1000, watch_time_get_tz(), &dt);
    snprintf(b, n, "%d %s %d, %02d:%02d", dt.day, kMon[(dt.month + 11) % 12], dt.year, dt.hour, dt.minute);
}

static void img_dsc(lv_image_dsc_t *d, const uint16_t *px, int w, int h)
{
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)w * 2;
    d->data_size = (uint32_t)w * (uint32_t)h * 2;
    d->data = (const uint8_t *)px;
}

/* La fuente remota que enseña la tercera pestaña */
static int remote_src(void)
{
    const gal_status_t *s = &G->st;
    if (s->choice.src >= 0) {
        return s->choice.src;
    }
    return s->pick == GAL_PICK_ULTRA ? GAL_SRC_ULTRA
           : s->pick == GAL_PICK_CLOUD ? GAL_SRC_CLOUD
           : s->pick == GAL_PICK_PHONE ? GAL_SRC_PHONE
                                       : GAL_SRC_NONE;
}

/* ======================= celdas ======================= */

/* Sin miniatura: el motivo (si hay) y el nombre, en `lines` líneas como mucho */
static void cell_show_name(cell_t *c, const char *why, int lines)
{
    char nm[96], t[160];
    const bool two = why[0] && strcmp(why, c->key.name) != 0;
    fit_text(c->key.name, nm, sizeof(nm), FLEX_FONT_SMALL, CELL - 12, two ? lines - 1 : lines);
    if (two) {
        snprintf(t, sizeof(t), "%s\n%s", why, nm);
    } else {
        gal_copyz(t, sizeof(t), why[0] ? why : nm);
    }
    set_text(c->name, t);
    watch_theme_set_visible(c->name, true);
}

static void cell_clear(cell_t *c)
{
    if (c->slot >= 0 || lv_image_get_src(c->img)) {
        lv_image_set_src(c->img, NULL);
        lv_image_cache_drop(&c->dsc);
    }
    if (c->slot >= 0 && G->e) {
        gal_thumb_release(G->e, c->slot);
    }
    c->slot = -1;
    c->want = false;
    if (c->draw) {
        lv_obj_add_flag(c->cv, LV_OBJ_FLAG_HIDDEN);
        lv_image_cache_drop(lv_canvas_get_image(c->cv));
        heap_caps_free(c->draw);
        c->draw = NULL;
    }
    watch_theme_set_visible(c->img, false);
    watch_theme_set_visible(c->name, false);
}

/* Marcas: origen (en Todas), ya en el reloj y vídeo */
static void cell_deco(cell_t *c)
{
    const gal_item_t *it = &c->key;
    /* De dónde es: nada si está en el reloj, "SD" si está en la microSD y la
     * fuente si es remota. */
    const bool sd = it->src == GAL_SRC_LOCAL && it->vol;
    const bool pill = it->src != GAL_SRC_LOCAL || sd;
    if (pill) {
        set_text(c->pill_l, sd ? "SD" : src_short(it->src));
        watch_theme_set_bg_color(c->pill, sd ? 0xC8862A : src_rgb(it->src));
    }
    watch_theme_set_visible(c->pill, pill);
    watch_theme_set_visible(c->mark, it->src != GAL_SRC_LOCAL && (it->flags & GAL_I_ONWATCH));
    watch_theme_set_visible(c->play, it->kind == GAL_K_VIDEO);
}

/* Miniatura del dibujo desde su archivo (como la galería de Paint) */
static void cell_draw(cell_t *c)
{
    c->draw = heap_caps_malloc((size_t)CELL * CELL * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c->draw) {
        cell_show_name(c, "Dibujo", 3);
        return;
    }
    lv_canvas_set_buffer(c->cv, c->draw, CELL, CELL, LV_COLOR_FORMAT_RGB565);
    lv_obj_remove_flag(c->cv, LV_OBJ_FLAG_HIDDEN);
    if (!ui_paint_thumb(c->cv, CELL, CELL, c->key.path)) {
        cell_show_name(c, "Dibujo dañado", 3);
    }
}

static void cell_try(cell_t *c)
{
    if (!c->want || c->slot >= 0 || !G->e) {
        return;
    }
    const uint16_t *px = NULL;
    int st = GAL_TH_EMPTY;
    const int slot = gal_thumb_acquire(G->e, &c->key, &px, &st);
    if (slot >= 0 && px) {
        c->slot = slot;
        c->want = false;
        img_dsc(&c->dsc, px, CELL, CELL);
        lv_image_set_src(c->img, &c->dsc);
        watch_theme_set_visible(c->img, true);
        watch_theme_set_visible(c->name, false);
    } else if (st == GAL_TH_FAILED) {
        c->want = false;
        cell_show_name(c, "Sin miniatura", 3);
    } else if (st == GAL_TH_EMPTY) {
        gal_thumb_request(G->e, &c->key); /* se volvió a pedir menos de lo que había: otra vez */
    }
}

static void cell_bind(cell_t *c, int idx, int row, int col)
{
    const gal_item_t *it = idx >= 0 && idx < G->n ? &G->items[idx] : NULL;
    if (!it) {
        if (c->idx >= 0) {
            cell_clear(c);
            c->idx = -1;
        }
        watch_theme_set_visible(c->box, false);
        return;
    }
    if (c->row != row || c->col != col) {
        lv_obj_set_pos(c->box, GRID_X + col * PITCH, GRID_Y + row * PITCH);
        c->row = row;
        c->col = col;
    }
    const bool same = c->idx >= 0 && same_item(&c->key, it);
    c->idx = idx;
    watch_theme_set_visible(c->box, true);
    if (same) {
        c->key.flags = it->flags; /* p. ej. ya descargada */
        cell_deco(c);
        return;
    }
    cell_clear(c);
    c->key = *it;
    cell_deco(c);
    if (it->kind == GAL_K_DRAW && !(it->flags & GAL_I_THUMB) && it->src == GAL_SRC_LOCAL && !it->vol) {
        cell_draw(c); /* sin miniatura de la biblioteca aún: se pinta desde el archivo */
    } else if (thumbable(it)) {
        c->want = true;
        gal_thumb_request(G->e, it);
        cell_try(c);
    } else if (it->kind == GAL_K_VIDEO) {
        cell_show_name(c, "", 1); /* el triángulo ya dice que es un vídeo */
    } else {
        cell_show_name(c, (it->flags & GAL_I_LOCKED) ? "Protegido" : gal_fmt_name((gal_fmt_t)it->fmt), 3);
    }
}

static void unbind_all(void)
{
    for (int k = 0; k < POOL; k++) {
        cell_clear(&G->cell[k]);
        G->cell[k].idx = -1;
        watch_theme_set_visible(G->cell[k].box, false);
    }
}

/* Celda k = fila (r % POOL_ROWS) y columna (k % COLS): al bajar una fila solo
 * se recoloca una fila de celdas, las demás conservan su miniatura. */
static void bind_cells(void)
{
    if (!G->e || G->suspended) {
        return;
    }
    int first = (int)((lv_obj_get_scroll_y(G->scroll) - GRID_Y) / PITCH);
    if (first < 0) {
        first = 0;
    }
    for (int k = 0; k < POOL; k++) {
        const int sr = k / COLS, col = k % COLS;
        const int r = first + ((sr - first % POOL_ROWS) + POOL_ROWS) % POOL_ROWS;
        cell_bind(&G->cell[k], r * COLS + col, r, col);
    }
}

static void layout(void)
{
    const int rows = (G->n + COLS - 1) / COLS;
    watch_theme_set_visible(G->card, G->n == 0);
    const int32_t bottom = G->n ? GRID_Y + rows * PITCH : GRID_Y + 8;
    if (lv_obj_get_y(G->spacer) != bottom) {
        lv_obj_set_y(G->spacer, bottom);
    }
    bind_cells();
}

/* La pestaña desde las listas del motor (copia en PSRAM) */
static void rebuild(void)
{
    gal_engine_t *e = G->e;
    G->dirty = false;
    G->lgen = G->st.local_gen;
    G->rgen = G->st.remote_gen;
    gal_engine_lock(e);
    int nl = 0, nr = 0;
    gal_list(e, 0, &nl);
    gal_list(e, 1, &nr);
    const int need = nl + nr;
    if (need > G->cap) {
        heap_caps_free(G->items);
        G->cap = (need + ITEMS_STEP - 1) / ITEMS_STEP * ITEMS_STEP;
        G->items = heap_caps_malloc((size_t)G->cap * sizeof(gal_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!G->items) {
            G->cap = 0;
        }
    }
    gal_ref_t *refs = need && G->items ? lv_malloc(sizeof(gal_ref_t) * (size_t)need) : NULL;
    const int nv = refs ? gal_build_view(e, G->tab, refs, need) : 0;
    int n = 0;
    for (int i = 0; i < nv; i++) {
        const gal_item_t *it = gal_ref_item(e, refs[i]);
        if (!it) {
            break;
        }
        const bool video = it->kind == GAL_K_VIDEO;
        if (video == (G->ktab == KT_VIDEOS)) {
            G->items[n++] = *it;
        }
    }
    gal_engine_unlock(e);
    lv_free(refs);
    G->nomem = need > 0 && !G->items;
    G->n = n;
    layout();
}

static void scroll_cb(lv_event_t *e)
{
    (void)e;
    if (G && G->e) {
        bind_cells();
    }
}

static void open_item(int idx, bool info);

static void cell_cb(lv_event_t *e)
{
    const cell_t *c = lv_event_get_user_data(e);
    if (G && c->idx >= 0 && c->idx < G->n) {
        open_item(c->idx, lv_event_get_code(e) == LV_EVENT_LONG_PRESSED);
    }
}

static void cell_create(cell_t *c)
{
    c->idx = -1;
    c->slot = -1;
    c->row = c->col = -1;
    c->box = lv_obj_create(G->scroll);
    lv_obj_remove_style_all(c->box);
    lv_obj_set_size(c->box, CELL, CELL);
    lv_obj_set_style_bg_color(c->box, flex_color(FLEX_C_SURF), 0);
    lv_obj_set_style_bg_opa(c->box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c->box, flex_color(FLEX_C_ACCENT_LIGHT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(c->box, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_post(c->box, true, 0);
    lv_obj_remove_flag(c->box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c->box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(c->box, cell_cb, LV_EVENT_SHORT_CLICKED, c);
    lv_obj_add_event_cb(c->box, cell_cb, LV_EVENT_LONG_PRESSED, c);
    c->img = lv_image_create(c->box);
    lv_obj_set_pos(c->img, 0, 0);
    lv_obj_add_flag(c->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->img, LV_OBJ_FLAG_CLICKABLE);
    c->cv = lv_canvas_create(c->box);
    lv_obj_set_pos(c->cv, 0, 0);
    lv_obj_add_flag(c->cv, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c->cv, LV_OBJ_FLAG_CLICKABLE);
    c->name = wrap_label(c->box, FLEX_FONT_SMALL, FLEX_C_TXT2, CELL - 12);
    lv_obj_set_style_text_align(c->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(c->name, 6, 8);
    lv_obj_add_flag(c->name, LV_OBJ_FLAG_HIDDEN);
    const int32_t lh = lv_font_get_line_height(FLEX_FONT_SMALL);
    c->pill = lv_obj_create(c->box);
    lv_obj_remove_style_all(c->pill);
    lv_obj_set_size(c->pill, LV_SIZE_CONTENT, lh + 2);
    lv_obj_set_style_pad_hor(c->pill, 8, 0);
    lv_obj_set_style_radius(c->pill, (lh + 2) / 2, 0);
    lv_obj_set_style_bg_opa(c->pill, LV_OPA_COVER, 0);
    lv_obj_align(c->pill, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_remove_flag(c->pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c->pill, LV_OBJ_FLAG_HIDDEN);
    c->pill_l = watch_theme_label(c->pill, FLEX_FONT_SMALL, FLEX_C_WHITE, "");
    lv_obj_align(c->pill_l, LV_ALIGN_LEFT_MID, 0, 0);
    c->mark = dot(c->box, 26);
    lv_obj_set_style_bg_color(c->mark, flex_color(0x2F8F5B), 0);
    lv_obj_align(c->mark, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_flag(c->mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(flex_icon_create(c->mark, FLEX_ICON_CHECK, 16, FLEX_C_WHITE));
    c->play = dot(c->box, 44);
    lv_obj_set_style_bg_color(c->play, flex_color(FLEX_C_BLACK), 0);
    lv_obj_center(c->play);
    lv_obj_add_flag(c->play, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(flex_icon_create(c->play, FLEX_ICON_PLAY, 22, FLEX_C_WHITE));
}

/* ======================= estado: pestañas, origen, tarjeta, transferencias ======================= */

static void tab_cb(lv_event_t *e)
{
    const int t = (int)(uintptr_t)lv_event_get_user_data(e);
    if (!G || t == G->ktab) {
        return;
    }
    G->ktab = t;
    for (int i = 0; i < KT_N; i++) {
        if (i == t) {
            lv_obj_add_state(G->tabs[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(G->tabs[i], LV_STATE_CHECKED);
        }
    }
    lv_obj_scroll_to_y(G->scroll, 0, LV_ANIM_OFF);
    unbind_all();
    G->dirty = true;
}

static void update_chip(void)
{
    const gal_status_t *s = &G->st;
    const int rs = remote_src();
    char t[200];
    uint32_t rgb = FLEX_C_MUTE;
    if (rs >= 0) {
        rgb = state_rgb(s->health[rs].state);
    }
    if (s->choice.why[0]) {
        gal_copyz(t, sizeof(t), s->choice.why);
    } else if (rs >= 0) {
        snprintf(t, sizeof(t), "%s · %s", gal_src_name(rs), gal_state_name(s->health[rs].state));
    } else {
        gal_copyz(t, sizeof(t), "Solo las fotos del reloj");
    }
    if (rs >= 0 && s->remote_src == rs && s->remote_state == GAL_L_LOADING && s->health[rs].state == GAL_ST_READY) {
        snprintf(t + strlen(t), sizeof(t) - strlen(t), " · cargando…");
    } else if (s->remote_state == GAL_L_ERROR && s->remote_err[0]) {
        snprintf(t, sizeof(t), "%s", s->remote_err);
        rgb = FLEX_C_ERR;
    }
    if (strcmp(t, G->chip_in) != 0) {
        gal_copyz(G->chip_in, sizeof(G->chip_in), t);
        char f[200];
        fit_text(t, f, sizeof(f), FLEX_FONT_SMALL, lv_obj_get_style_width(G->chip_l, LV_PART_MAIN), 2);
        set_text(G->chip_l, f);
    }
    watch_theme_set_bg_color(G->chip_dot, rgb);
}

static void card_set(const char *title, const char *sub, const char *btn, int act)
{
    char f[240];
    fit_text(title, f, sizeof(f), FLEX_FONT_BODY, GRID_W - 32, 2);
    set_text(G->card_t, f);
    fit_text(sub ? sub : "", f, sizeof(f), FLEX_FONT_SMALL, GRID_W - 32, 4);
    set_text(G->card_s, f);
    watch_theme_set_visible(G->card_s, sub && sub[0]);
    G->card_act = btn ? act : CARD_NONE;
    if (btn) {
        set_text(G->card_bl, btn);
    }
    watch_theme_set_visible(G->card_b, btn != NULL);
}

static void update_card(void)
{
    if (G->n) {
        return;
    }
    const gal_status_t *s = &G->st;
    const bool videos = G->ktab == KT_VIDEOS;
    const char *fs_why = NULL;
    if (G->nomem) {
        card_set("Sin memoria para la lista", "Cierra otra app y vuelve a abrir la Galería.", NULL, CARD_NONE);
    } else if (s->remote_state == GAL_L_ERROR && s->remote_err[0] && remote_src() >= 0 && s->nl == 0) {
        card_set("No se pudo cargar la lista", s->remote_err, "Reintentar", CARD_RETRY);
    } else if (!ui_fs_ready(&fs_why) && s->nl == 0) {
        card_set("Galería sin almacenamiento", fs_why, NULL, CARD_NONE);
    } else if (s->scanning && s->nl == 0) {
        card_set(videos ? "Buscando vídeos…" : "Buscando fotos…", NULL, NULL, CARD_NONE);
    } else if (videos) {
        card_set("No hay vídeos",
                 "Recíbelos desde el móvil: la página los convierte a AVI MJPEG, el vídeo que el reloj reproduce.",
                 "Recibir desde el móvil", CARD_XFER);
    } else {
        card_set("No hay fotos",
                 "Recíbelas desde el móvil, descárgalas de Flex OS Ultra o Flex Cloud, o haz un dibujo en Paint.",
                 "Recibir desde el móvil", CARD_XFER);
    }
}

static lv_obj_t *src_page_create(lv_obj_t *parent);
static lv_obj_t *xf_page_create(lv_obj_t *parent);

static void card_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!G || !G->e) {
        return;
    }
    if (G->card_act == CARD_RETRY) {
        gal_refresh(G->e);
    } else if (G->card_act == CARD_ORIGIN) {
        ui_nav_push(src_page_create);
    } else if (G->card_act == CARD_XFER) {
        ui_nav_push(ui_xfer_create);
    }
}

static void chip_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(src_page_create);
}

static void xp_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(xf_page_create);
}

/* Indicador de la cabecera: transferencias en marcha (progreso) o fallidas */
static void update_xp(void)
{
    gal_xfer_info_t x[GAL_XFERS];
    const int n = gal_xfers(G->e, x, GAL_XFERS);
    uint64_t done = 0, total = 0;
    int live = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        if (x[i].state == GAL_XS_QUEUED || x[i].state == GAL_XS_RUN || x[i].state == GAL_XS_WAIT) {
            live++;
            done += x[i].done;
            total += x[i].total;
        } else if (x[i].state == GAL_XS_FAIL) {
            failed++;
        }
    }
    char t[24];
    if (live) {
        snprintf(t, sizeof(t), "%d · %d %%", live, total ? (int)(done * 100 / total) : 0);
    } else {
        snprintf(t, sizeof(t), "%d", failed);
    }
    set_text(G->xp_l, t);
    watch_theme_set_visible(G->xp_ok, live > 0);
    watch_theme_set_visible(G->xp_err, !live && failed > 0);
    watch_theme_set_visible(G->xp, live > 0 || failed > 0);
}

static void maybe_more(void)
{
    const gal_status_t *s = &G->st;
    if (!s->remote_more || s->remote_state != GAL_L_READY) {
        return;
    }
    const int32_t left = lv_obj_get_scroll_bottom(G->scroll);
    const uint32_t now = lv_tick_get();
    if (left < 2 * PITCH && now - G->more_ms > 1500) {
        G->more_ms = now;
        gal_more(G->e);
    }
}

/* ======================= visor ======================= */

enum { VM_NONE = 0, VM_RETRY, VM_INFO, VM_PAINT };

typedef struct {
    lv_obj_t *page, *touch, *img, *top, *title, *bottom, *bot_l, *load_l, *msg, *msg_l, *msg_b;
    lv_image_dsc_t dsc;
    gal_item_t it;
    uint32_t gen;
    int32_t iw, ih, px, py, px0, py0;
    lv_point_t p0;
    uint32_t tap_ms;
    int msg_act;
    bool zoomed, overlay, shown, moved;
} vw_t;

static vw_t *V;

static lv_obj_t *info_page_create(lv_obj_t *parent);

static void vw_hide_img(void)
{
    if (V && V->shown) {
        lv_image_set_src(V->img, NULL);
        lv_image_cache_drop(&V->dsc);
        V->shown = false;
    }
}

static void vw_place(void)
{
    int32_t x = (FLEX_SCREEN_W - V->iw) / 2, y = (FLEX_SCREEN_H - V->ih) / 2;
    if (V->zoomed) { /* el 2x se mueve sin dejar ver el fondo por los lados que sobran */
        const int32_t mx = V->iw > FLEX_SCREEN_W ? (V->iw - FLEX_SCREEN_W) / 2 : 0;
        const int32_t my = V->ih > FLEX_SCREEN_H ? (V->ih - FLEX_SCREEN_H) / 2 : 0;
        V->px = LV_CLAMP(-mx, V->px, mx);
        V->py = LV_CLAMP(-my, V->py, my);
        x += V->px;
        y += V->py;
    }
    lv_obj_set_pos(V->img, x, y);
}

static void vw_texts(int src_w, int src_h);

static void vw_show(const gal_view_t *v)
{
    const bool z = V->zoomed && v->zoom;
    vw_hide_img();
    img_dsc(&V->dsc, z ? v->zoom : v->fit, z ? v->zoom_w : v->fit_w, z ? v->zoom_h : v->fit_h);
    V->iw = (int32_t)V->dsc.header.w;
    V->ih = (int32_t)V->dsc.header.h;
    lv_image_set_src(V->img, &V->dsc);
    V->shown = true;
    V->gen = v->gen;
    vw_texts(v->src_w, v->src_h);
    vw_place();
    watch_theme_set_visible(V->img, true);
    watch_theme_set_visible(V->load_l, false);
    watch_theme_set_visible(V->msg, false);
}

static void vw_msg(const char *text, const char *btn, int act)
{
    char f[200];
    fit_text(text, f, sizeof(f), FLEX_FONT_BODY, FLEX_SCREEN_W - 2 * 48, 5);
    set_text(V->msg_l, f);
    V->msg_act = btn ? act : VM_NONE;
    if (btn) {
        flex_glass_button_set(V->msg_b, btn, 0);
    }
    watch_theme_set_visible(V->msg_b, btn != NULL);
    watch_theme_set_visible(V->msg, true);
    watch_theme_set_visible(V->load_l, false);
}

static void vw_texts(int src_w, int src_h)
{
    const gal_item_t *it = &V->it;
    char t[160], f[160];
    fit_text(it->name, f, sizeof(f), FLEX_FONT_BODY, lv_obj_get_style_width(V->title, LV_PART_MAIN), 1);
    set_text(V->title, f);
    if (src_w > 0 && src_h > 0) { /* las medidas reales, leídas de la foto */
        snprintf(t, sizeof(t), "%s · %d × %d", gal_src_name(it->src), src_w, src_h);
    } else if (it->w && it->h) {
        snprintf(t, sizeof(t), "%s · %u × %u", gal_src_name(it->src), it->w, it->h);
    } else {
        char sz[24];
        gal_size_text(it->size, sz, sizeof(sz));
        snprintf(t, sizeof(t), "%s · %s", gal_src_name(it->src), sz);
    }
    fit_text(t, f, sizeof(f), FLEX_FONT_SMALL, lv_obj_get_style_width(V->bot_l, LV_PART_MAIN), 1);
    set_text(V->bot_l, f);
}

static void vw_start(void)
{
    vw_hide_img();
    V->zoomed = false;
    V->px = V->py = 0;
    V->gen = 0;
    watch_theme_set_visible(V->img, false);
    vw_texts(0, 0);
    if (!G || !G->e) {
        return;
    }
    const gal_item_t *it = &V->it;
    if (it->kind == GAL_K_DRAW) {
        vw_msg("Dibujo de Paint", "Abrir en Paint", VM_PAINT);
    } else if (it->flags & GAL_I_LOCKED) {
        vw_msg("Contenido protegido: ábrelo en Flex OS Ultra", "Información", VM_INFO);
    } else if (!viewable(it)) {
        vw_msg(gal_fmt_why((gal_fmt_t)it->fmt), "Información", VM_INFO);
    } else {
        gal_view_open(G->e, it);
        set_text(V->load_l, "Cargando…");
        watch_theme_set_visible(V->load_l, true);
        watch_theme_set_visible(V->msg, false);
    }
}

static void vw_overlay(bool on)
{
    V->overlay = on;
    watch_theme_set_visible(V->top, on);
    watch_theme_set_visible(V->bottom, on);
}

/* Siguiente (+1) o anterior (-1) de la pestaña */
static void vw_step(int d)
{
    for (int i = 0; G && i < G->n; i++) {
        if (same_item(&G->items[i], &V->it)) {
            const int j = i + d;
            if (j >= 0 && j < G->n) {
                V->it = G->items[j];
                G->sel = V->it;
                vw_start();
            }
            return;
        }
    }
}

static void vw_zoom(void)
{
    if (!G || !G->e) {
        return;
    }
    gal_view_t v;
    gal_view_get(G->e, &v);
    if (v.state == GAL_V_READY && v.zoom) {
        V->zoomed = !V->zoomed;
        V->px = V->py = 0;
        vw_show(&v);
    }
}

static void vw_touch_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    lv_point_t p = {0, 0};
    if (!V || !in) {
        return;
    }
    lv_indev_get_point(in, &p);
    const int32_t dx = p.x - V->p0.x, dy = p.y - V->p0.y;
    if (code == LV_EVENT_PRESSED) {
        V->p0 = p;
        V->px0 = V->px;
        V->py0 = V->py;
        V->moved = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (LV_ABS(dx) > TAP_PX || LV_ABS(dy) > TAP_PX) {
            V->moved = true;
        }
        if (V->zoomed && V->shown) {
            V->px = V->px0 + dx;
            V->py = V->py0 + dy;
            vw_place();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!V->moved) {
            const uint32_t now = lv_tick_get();
            if (V->tap_ms && now - V->tap_ms < DTAP_MS) { /* doble toque: 2x (y el primer toque se deshace) */
                V->tap_ms = 0;
                vw_overlay(!V->overlay);
                vw_zoom();
            } else {
                V->tap_ms = now;
                vw_overlay(!V->overlay);
            }
        } else if (!V->zoomed && LV_ABS(dx) > SWIPE_PX && LV_ABS(dx) > 2 * LV_ABS(dy)) {
            vw_step(dx < 0 ? 1 : -1);
        }
    }
}

static void vw_info_cb(lv_event_t *e)
{
    (void)e;
    if (V && G) {
        G->sel = V->it;
        ui_nav_push(info_page_create);
    }
}

static void vw_msg_cb(lv_event_t *e)
{
    (void)e;
    if (!V || !G) {
        return;
    }
    if (V->msg_act == VM_RETRY) {
        vw_start();
    } else if (V->msg_act == VM_INFO) {
        vw_info_cb(e);
    } else if (V->msg_act == VM_PAINT) {
        ui_paint_open(V->it.path);
    }
}

static void vw_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!V || !G || !G->e || G->suspended || !viewable(&V->it)) {
        return;
    }
    gal_view_t v;
    gal_view_get(G->e, &v);
    if (v.state == GAL_V_LOADING) {
        char b[48];
        if (v.total) {
            snprintf(b, sizeof(b), "Cargando… %d %%", (int)(v.done * 100 / v.total));
        } else {
            snprintf(b, sizeof(b), "Cargando…");
        }
        set_text(V->load_l, b);
        if (!V->shown) {
            watch_theme_set_visible(V->load_l, true);
        }
    } else if (v.state == GAL_V_READY && (v.gen != V->gen || !V->shown)) {
        vw_show(&v);
    } else if (v.state == GAL_V_ERROR && lv_obj_has_flag(V->msg, LV_OBJ_FLAG_HIDDEN)) {
        vw_msg(v.err[0] ? v.err : "No se pudo abrir la foto", "Reintentar", VM_RETRY);
    }
}

static void vw_del_cb(lv_event_t *e)
{
    (void)e;
    vw_hide_img();
    if (G && G->e) {
        gal_view_close(G->e);
    }
    lv_free(V);
    V = NULL;
}

static lv_obj_t *vw_page_create(lv_obj_t *parent)
{
    if (V || !G) {
        return NULL;
    }
    V = lv_malloc_zeroed(sizeof(*V));
    LV_ASSERT_MALLOC(V);
    V->it = G->sel;
    lv_obj_t *page = ui_page_new(parent);
    lv_obj_set_style_bg_color(page, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    V->page = page;
    V->touch = lv_obj_create(page);
    lv_obj_remove_style_all(V->touch);
    lv_obj_set_size(V->touch, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_remove_flag(V->touch, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(V->touch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(V->touch, vw_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(V->touch, vw_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(V->touch, vw_touch_cb, LV_EVENT_RELEASED, NULL);
    V->img = lv_image_create(V->touch);
    lv_obj_remove_flag(V->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(V->img, LV_OBJ_FLAG_HIDDEN);
    V->load_l = watch_theme_label(V->touch, FLEX_FONT_BODY, FLEX_C_TXT2, "");
    lv_obj_center(V->load_l);
    V->msg = lv_obj_create(V->touch);
    lv_obj_remove_style_all(V->msg);
    lv_obj_set_size(V->msg, FLEX_SCREEN_W - 2 * 48, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(V->msg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(V->msg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(V->msg, 14, 0);
    lv_obj_remove_flag(V->msg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(V->msg);
    V->msg_l = wrap_label(V->msg, FLEX_FONT_BODY, FLEX_C_TXT, FLEX_SCREEN_W - 2 * 48);
    lv_obj_set_style_text_align(V->msg_l, LV_TEXT_ALIGN_CENTER, 0);
    V->msg_b = flex_glass_button(V->msg, "", FLEX_GLASS_TINT_NONE, 220, 56);
    lv_obj_add_event_cb(V->msg_b, vw_msg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(V->msg, LV_OBJ_FLAG_HIDDEN);

    /* capa de arriba (volver y nombre) y de abajo (origen, medidas e información): opacas */
    V->top = lv_obj_create(page);
    lv_obj_remove_style_all(V->top);
    lv_obj_set_size(V->top, FLEX_SCREEN_W, FLEX_HDR_H);
    lv_obj_set_style_bg_color(V->top, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(V->top, LV_OPA_COVER, 0);
    lv_obj_remove_flag(V->top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(V->top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hdr = watch_theme_header(V->top, NULL, back_cb, NULL);
    lv_obj_update_layout(hdr);
    lv_obj_t *zone = lv_obj_get_child(hdr, 0);
    const int32_t th = lv_font_get_line_height(FLEX_FONT_BODY);
    const int32_t ty = FLEX_SAFE_TOP + (FLEX_HDR_ZONE - th) / 2;
    const int32_t tx = lv_obj_get_x(zone) + lv_obj_get_width(zone) + FLEX_HDR_GAP;
    V->title = watch_theme_label(V->top, FLEX_FONT_BODY, FLEX_C_TXT, "");
    lv_obj_set_pos(V->title, tx, ty);
    lv_obj_set_width(V->title, flex_safe_right(ty, ty + th - 1, FLEX_SAFE_MARGIN) + 1 - tx);
    const int32_t bh = FLEX_LIST_BOTTOM + 8;
    V->bottom = lv_obj_create(page);
    lv_obj_remove_style_all(V->bottom);
    lv_obj_set_size(V->bottom, FLEX_SCREEN_W, bh);
    lv_obj_set_pos(V->bottom, 0, FLEX_SCREEN_H - bh);
    lv_obj_set_style_bg_color(V->bottom, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(V->bottom, LV_OPA_COVER, 0);
    lv_obj_remove_flag(V->bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(V->bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(V->bottom, vw_info_cb, LV_EVENT_CLICKED, NULL);
    const int32_t sh = lv_font_get_line_height(FLEX_FONT_SMALL);
    const int32_t by = FLEX_SCREEN_H - bh + 8;
    const int32_t bx = flex_safe_left(by, by + sh - 1, FLEX_SAFE_MARGIN + 6);
    V->bot_l = watch_theme_label(V->bottom, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_width(V->bot_l, FLEX_SCREEN_W - 2 * bx);
    lv_obj_set_style_text_align(V->bot_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(V->bot_l, bx, 8);
    V->overlay = true;
    vw_start();
    lv_obj_add_event_cb(page, vw_del_cb, LV_EVENT_DELETE, NULL);
    ui_page_timer(page, vw_tick_cb, TICK_MS, NULL);
    return page;
}

/* ======================= información y acciones ======================= */

enum { IA_VIEW = 1, IA_PAINT, IA_DOWNLOAD, IA_UP_ULTRA, IA_UP_CLOUD, IA_UP_PHONE, IA_TRASH, IA_PLAY };

typedef struct {
    gal_item_t it;
    lv_obj_t *page, *list, *status, *trash_b;
    uint32_t trash_at, sig;
    bool trashed;
} if_t;

static void info_build(if_t *p);

/* Busca el elemento en la lista actual (las marcas cambian: ya descargado...) */
static bool find_item(const gal_item_t *key, gal_item_t *out)
{
    for (int i = 0; G && i < G->n; i++) {
        if (same_item(&G->items[i], key)) {
            *out = G->items[i];
            return true;
        }
    }
    /* No está en la pestaña (p. ej. un remoto ya descargado: la pestaña enseña
     * la copia): en las listas del motor, con sus marcas al día. */
    if (!G || !G->e) {
        return false;
    }
    bool found = false;
    gal_engine_lock(G->e);
    for (int which = 0; which < 2 && !found; which++) {
        int n = 0;
        const gal_item_t *l = gal_list(G->e, which, &n);
        for (int i = 0; i < n && !found; i++) {
            if (same_item(&l[i], key)) {
                *out = l[i];
                found = true;
            }
        }
    }
    gal_engine_unlock(G->e);
    return found;
}

static void if_act_cb(lv_event_t *e)
{
    if_t *p = lv_event_get_user_data(e);
    const int act = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    char why[96] = "";
    int id = 0;
    if (!G || !G->e) {
        return;
    }
    switch (act) {
    case IA_VIEW:
        G->sel = p->it;
        ui_nav_push(vw_page_create);
        return;
    case IA_PAINT:
        ui_paint_open(p->it.path);
        return;
    case IA_PLAY:
        ui_video_open(p->it.vol, p->it.path, p->it.name);
        return;
    case IA_DOWNLOAD:
        id = gal_download(G->e, &p->it, why, sizeof(why));
        break;
    case IA_UP_ULTRA:
    case IA_UP_CLOUD:
    case IA_UP_PHONE:
        id = gal_upload(G->e, &p->it, act == IA_UP_ULTRA ? GAL_SRC_ULTRA : act == IA_UP_CLOUD ? GAL_SRC_CLOUD : GAL_SRC_PHONE,
                        why, sizeof(why));
        break;
    case IA_TRASH: {
        const uint32_t now = lv_tick_get();
        if (!p->trash_at || now - p->trash_at >= CONFIRM_MS) {
            p->trash_at = now;
            flex_glass_button_set(p->trash_b,
                                  p->it.src == GAL_SRC_LOCAL && p->it.vol ? "Toca otra vez para borrarla"
                                                                          : "Toca otra vez para tirarla",
                                  FLEX_C_DANGER);
            return;
        }
        p->trash_at = 0;
        id = gal_trash(G->e, &p->it, why, sizeof(why));
        p->trashed = id > 0;
        break;
    }
    default:
        return;
    }
    set_text(p->status, id > 0 ? "En la cola de transferencias" : (why[0] ? why : "No se puede ahora"));
    watch_theme_set_text_color(p->status, id > 0 ? FLEX_C_OK : FLEX_C_WARN);
}

static void if_button(if_t *p, const char *t, uint32_t rgb, int act)
{
    lv_obj_t *b = flex_glass_button(p->list, t, rgb, LV_PCT(100), 60);
    lv_obj_set_user_data(b, (void *)(intptr_t)act);
    lv_obj_add_event_cb(b, if_act_cb, LV_EVENT_CLICKED, p);
    if (act == IA_TRASH) {
        p->trash_b = b;
    }
}

/* La microSD no tiene papelera: allí se borra (con el mismo doble toque). */
static const char *trash_text(const gal_item_t *it)
{
    if (it->src != GAL_SRC_LOCAL) {
        return "Mover a su papelera";
    }
    return it->vol ? "Borrar de la microSD" : "Mover a la papelera";
}

static void where_text(const gal_item_t *it, char *b, size_t n)
{
    if (it->src == GAL_SRC_LOCAL) {
        const char *vn = it->vol ? "la microSD" : "el reloj";
        if ((it->flags & GAL_I_REMOTE) && it->origin_src > 0) {
            snprintf(b, n, "En %s · copia de %s", vn, gal_src_name(it->origin_src));
        } else {
            snprintf(b, n, "En %s (%s)", vn, it->path);
        }
    } else if (it->src == GAL_SRC_PHONE) {
        snprintf(b, n, "En el Flex Cloud del teléfono, a través de Flex OS Ultra%s",
                 (it->flags & GAL_I_ONWATCH) ? " · también en el reloj" : "");
    } else {
        snprintf(b, n, "En %s%s", gal_src_name(it->src), (it->flags & GAL_I_ONWATCH) ? " · también en el reloj" : "");
    }
}

static void info_build(if_t *p)
{
    const gal_item_t *it = &p->it;
    char t[200], f[200];
    lv_obj_clean(p->list);
    p->trash_b = NULL;
    lv_obj_t *c = flex_glass_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    const int32_t w = FLEX_SCREEN_W - 2 * FLEX_LIST_X - 32;
    fit_text(it->name, f, sizeof(f), FLEX_FONT_BODY, w, 3);
    set_text(wrap_label(c, FLEX_FONT_BODY, FLEX_C_TXT, w), f);
    where_text(it, t, sizeof(t));
    fit_text(t, f, sizeof(f), FLEX_FONT_SMALL, w, 3);
    set_text(wrap_label(c, FLEX_FONT_SMALL, FLEX_C_MUTE, w), f);
    if (it->size) {
        gal_size_text(it->size, t, sizeof(t));
        watch_theme_info_row(p->list, "Tamaño", t);
    }
    if (it->w && it->h) {
        snprintf(t, sizeof(t), "%u × %u", it->w, it->h);
        watch_theme_info_row(p->list, "Medidas", t);
    }
    fmt_when(it->when, t, sizeof(t));
    watch_theme_info_row(p->list, "Fecha", t);
    watch_theme_info_row(p->list, "Formato", gal_fmt_name((gal_fmt_t)it->fmt));
    if (it->dur_ms) {
        snprintf(t, sizeof(t), "%u:%02u", (unsigned)(it->dur_ms / 60000), (unsigned)(it->dur_ms / 1000 % 60));
        watch_theme_info_row(p->list, "Duración", t);
    }
    if (it->flags & (GAL_I_LOCKED | GAL_I_DAMAGED | GAL_I_BUSY)) {
        watch_theme_info_row(p->list, "Estado", (it->flags & GAL_I_LOCKED)    ? "Protegido"
                                                : (it->flags & GAL_I_DAMAGED) ? "Dañado en Flex OS Ultra"
                                                                              : "Comprobándose");
    }
    if (it->kind != GAL_K_DRAW && !viewable(it) && !playable_video(it)) {
        lv_obj_t *l = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2);
        set_text(l, (it->flags & GAL_I_LOCKED) ? "Contenido protegido: ábrelo en Flex OS Ultra"
                                               : gal_fmt_why((gal_fmt_t)it->fmt));
    }
    /* Solo lo que se puede hacer ahora */
    if (playable_video(it)) {
        if_button(p, "Reproducir", FLEX_C_PRIMARY, IA_PLAY);
    } else if (it->kind == GAL_K_DRAW) {
        if_button(p, "Abrir en Paint", FLEX_C_PRIMARY, IA_PAINT);
    } else if (viewable(it) && !V) {
        if_button(p, "Ver", FLEX_C_PRIMARY, IA_VIEW);
    }
    char why[96] = "";
    if (it->src != GAL_SRC_LOCAL) {
        if (gal_can(G->e, GAL_X_DOWNLOAD, it, it->src, why, sizeof(why))) {
            /* al destino elegido ("Guardar en"), y lo dice */
            wm_vol_t dv = WM_VOL_WATCH;
            wm_dest_get(&dv);
            if_button(p, dv == WM_VOL_SD ? "Descargar a la microSD" : "Descargar al reloj", FLEX_C_PRIMARY,
                      IA_DOWNLOAD);
        } else if (why[0]) {
            lv_obj_t *l = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2);
            set_text(l, why);
        }
    } else {
        static const struct {
            int dest, act;
            const char *text;
        } kUp[] = {{GAL_SRC_ULTRA, IA_UP_ULTRA, "Subir a Flex OS Ultra"},
                   {GAL_SRC_CLOUD, IA_UP_CLOUD, "Subir a Flex Cloud"},
                   {GAL_SRC_PHONE, IA_UP_PHONE, "Subir a Flex Phone"}};
        for (size_t i = 0; i < sizeof(kUp) / sizeof(kUp[0]); i++) {
            if (gal_can(G->e, GAL_X_UPLOAD, it, kUp[i].dest, NULL, 0)) {
                if_button(p, kUp[i].text, FLEX_GLASS_TINT_NONE, kUp[i].act);
            }
        }
    }
    if (gal_can(G->e, GAL_X_TRASH, it, it->src, NULL, 0)) {
        if_button(p, trash_text(it), FLEX_GLASS_TINT_NONE, IA_TRASH);
    }
    p->status = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_WARN);
}

static uint32_t if_sig(const gal_item_t *cur)
{
    const gal_status_t *s = &G->st;
    uint32_t sig = s->xfer_gen * 31u + s->remote_gen * 7u + s->local_gen;
    for (int i = 0; i < GAL_SRC_N; i++) {
        sig = sig * 5u + s->health[i].state;
    }
    return cur ? sig * 3u + cur->flags : sig;
}

static void if_tick_cb(lv_timer_t *t)
{
    if_t *p = lv_timer_get_user_data(t);
    if (!G || !G->e || G->suspended) {
        return;
    }
    gal_item_t cur;
    const bool there = find_item(&p->it, &cur);
    if (p->trashed && !there && ui_nav_top() == p->page) { /* ya no está: se cierra sola */
        p->trashed = false;
        ui_nav_back();
        return;
    }
    if (p->trash_at && lv_tick_get() - p->trash_at >= CONFIRM_MS) {
        p->trash_at = 0;
        flex_glass_button_set(p->trash_b, trash_text(&p->it), FLEX_GLASS_TINT_NONE);
    }
    /* Lo que se puede hacer depende de las fuentes y de la cola: se rehace si cambian */
    const uint32_t sig = if_sig(there ? &cur : NULL);
    if (sig != p->sig && !p->trash_at) {
        char keep[112];
        gal_copyz(keep, sizeof(keep), lv_label_get_text(p->status));
        const uint32_t rgb = lv_color_to_u32(lv_obj_get_style_text_color(p->status, 0)) & 0xFFFFFFu;
        p->sig = sig;
        if (there) {
            p->it = cur;
        }
        info_build(p);
        set_text(p->status, keep);
        watch_theme_set_text_color(p->status, rgb);
    }
}

static lv_obj_t *info_page_create(lv_obj_t *parent)
{
    if (!G) {
        return NULL;
    }
    if_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->it = G->sel;
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Información", back_cb, NULL);
    p->list = watch_theme_list(page);
    info_build(p);
    gal_item_t cur;
    p->sig = if_sig(find_item(&p->it, &cur) ? &cur : NULL);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, if_tick_cb, TICK_MS * 2, p);
    return page;
}

static void open_item(int idx, bool info)
{
    G->sel = G->items[idx];
    if (info) {
        ui_nav_push(info_page_create);
    } else if (G->sel.kind == GAL_K_VIDEO) {
        if (playable_video(&G->sel)) {
            ui_video_open(G->sel.vol, G->sel.path, G->sel.name);
        } else {
            ui_nav_push(info_page_create); /* dice por qué no y qué hacer */
        }
    } else if (G->sel.kind == GAL_K_DRAW) {
        ui_paint_open(G->sel.path);
    } else {
        ui_nav_push(vw_page_create);
    }
}

/* ======================= Origen ======================= */

static const char *short_state(uint8_t st)
{
    switch (st) {
    case GAL_ST_READY: return "Lista";
    case GAL_ST_CHECKING: return "Comprobando";
    case GAL_ST_PAIR: return "Reconectar";
    case GAL_ST_DOWN: return "No responde";
    case GAL_ST_NOWIFI: return "Sin Wi-Fi";
    default: return "Sin configurar";
    }
}

typedef struct {
    lv_obj_t *page, *list, *row[GAL_PICK_N], *why, *u_row, *forget, *a_row, *quota;
    uint32_t forget_at;
    int8_t pick;
    bool phone, uhost, linked;
} sp_t;

static lv_obj_t *ultra_page_create(lv_obj_t *parent);
static lv_obj_t *acct_page_create(lv_obj_t *parent);

static void sp_build(sp_t *p);

static void sp_pick_cb(lv_event_t *e)
{
    const gal_pick_t k = (gal_pick_t)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (G && G->e) {
        gal_set_pick(G->e, k);
        gal_status(G->e, &G->st);
        G->dirty = true; /* la pestaña remota puede ser otra */
    }
}

static void sp_ultra_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ultra_page_create);
}

static void sp_acct_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(acct_page_create);
}

static void sp_forget_cb(lv_event_t *e)
{
    sp_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (!G || !G->e) {
        return;
    }
    if (p->forget_at && now - p->forget_at < CONFIRM_MS) {
        p->forget_at = 0;
        gal_ultra_forget(G->e);
        return;
    }
    p->forget_at = now;
    lv_obj_t *t = lv_obj_get_child(p->forget, 1);
    set_text(t, "Toca otra vez");
}

static void sp_values(sp_t *p)
{
    const gal_status_t *s = &G->st;
    static const int kSrc[GAL_PICK_N] = {GAL_SRC_NONE, GAL_SRC_ULTRA, GAL_SRC_CLOUD, GAL_SRC_PHONE};
    for (int k = 0; k < GAL_PICK_N; k++) {
        if (!p->row[k]) {
            continue;
        }
        const char *v = k == GAL_PICK_AUTO ? (s->choice.automatic && s->choice.src >= 0 &&
                                                      s->health[s->choice.src].state == GAL_ST_READY
                                                  ? src_short(s->choice.src)
                                                  : "Ninguna lista")
                                           : short_state(s->wifi ? s->health[kSrc[k]].state : GAL_ST_NOWIFI);
        watch_theme_row_set_value(p->row[k], v);
    }
    char f[200];
    fit_text(s->choice.why[0] ? s->choice.why : "", f, sizeof(f), FLEX_FONT_SMALL, LIST_TW, 4);
    set_text(p->why, f);
    watch_theme_row_set_value(p->u_row, !s->ultra_host[0] ? "Sin configurar"
                                        : s->ultra_paired ? "Emparejado"
                                                          : "Sin emparejar");
    watch_theme_row_set_value(p->a_row, s->acct_link == GAL_LINK_UNLINKED ? "Sin vincular" : "Vinculada");
    if (p->quota) {
        char a[24], b[24], q[64];
        gal_size_text(s->q_used, a, sizeof(a));
        gal_size_text(s->q_total, b, sizeof(b));
        snprintf(q, sizeof(q), "%s de %s", a, b);
        watch_theme_info_set(p->quota, q);
    }
}

static void sp_build(sp_t *p)
{
    const gal_status_t *s = &G->st;
    lv_obj_clean(p->list);
    memset(p->row, 0, sizeof(p->row));
    p->forget = NULL;
    p->quota = NULL;
    p->forget_at = 0;
    p->pick = (int8_t)s->pick;
    p->phone = s->health[GAL_SRC_PHONE].state != GAL_ST_UNSET || s->pick == GAL_PICK_PHONE;
    p->uhost = s->ultra_host[0] != 0;
    p->linked = s->acct_link != GAL_LINK_UNLINKED;
    watch_theme_section(p->list, "De dónde salen las fotos");
    static const char *const kName[GAL_PICK_N] = {"Automático", "Flex OS Ultra", "Flex Cloud", "Flex Phone"};
    for (int k = 0; k < GAL_PICK_N; k++) {
        if (k == GAL_PICK_PHONE && !p->phone) {
            continue; /* sin teléfono listo en Flex OS Ultra no se ofrece */
        }
        const bool sel = s->pick == (gal_pick_t)k;
        p->row[k] = watch_theme_list_row(p->list, sel ? FLEX_ICON_CHECK : FLEX_ICON_IMAGE,
                                         sel ? FLEX_C_ACCENT : FLEX_C_MUTE, kName[k], "", false);
        lv_obj_set_user_data(p->row[k], (void *)(intptr_t)k);
        lv_obj_add_event_cb(p->row[k], sp_pick_cb, LV_EVENT_CLICKED, p);
    }
    p->why = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2);
    watch_theme_section(p->list, "Flex OS Ultra");
    p->u_row = watch_theme_list_row(p->list, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Conectar", "", true);
    lv_obj_add_event_cb(p->u_row, sp_ultra_cb, LV_EVENT_CLICKED, p);
    if (p->uhost) {
        p->forget = watch_theme_list_row(p->list, FLEX_ICON_CROSS, FLEX_C_DANGER, "Olvidar", NULL, false);
        lv_obj_add_event_cb(p->forget, sp_forget_cb, LV_EVENT_CLICKED, p);
    }
    watch_theme_section(p->list, "Flex Cloud");
    p->a_row = watch_theme_list_row(p->list, FLEX_ICON_GLOBE, FLEX_C_CAT_BLUE, "Flex Account", "", true);
    lv_obj_add_event_cb(p->a_row, sp_acct_cb, LV_EVENT_CLICKED, p);
    if (s->q_total) {
        p->quota = watch_theme_info_row(p->list, "Espacio usado", "");
    }
    lv_obj_t *h = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE);
    set_text(h, "Automático usa Flex OS Ultra si está en tu red; si no, Flex Cloud, y si no, el teléfono a través "
                "de Flex OS Ultra. Cada foto dice de dónde viene. Es la misma biblioteca y la misma cuenta que en "
                "Flex OS Ultra y Flex Cloud Web.");
    sp_values(p);
}

static void sp_tick_cb(lv_timer_t *t)
{
    sp_t *p = lv_timer_get_user_data(t);
    if (!G || !G->e || G->suspended) {
        return;
    }
    const gal_status_t *s = &G->st;
    const bool phone = s->health[GAL_SRC_PHONE].state != GAL_ST_UNSET || s->pick == GAL_PICK_PHONE;
    if (p->pick != (int8_t)s->pick || p->phone != phone || p->uhost != (s->ultra_host[0] != 0) ||
        (p->quota == NULL) != (s->q_total == 0)) {
        sp_build(p);
        return;
    }
    if (p->forget_at && lv_tick_get() - p->forget_at >= CONFIRM_MS) {
        p->forget_at = 0;
        set_text(lv_obj_get_child(p->forget, 1), "Olvidar");
    }
    sp_values(p);
}

static lv_obj_t *src_page_create(lv_obj_t *parent)
{
    if (!G || !G->e) {
        return NULL;
    }
    sp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Origen", back_cb, NULL);
    p->list = watch_theme_list(page);
    gal_status(G->e, &G->st);
    sp_build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, sp_tick_cb, TICK_MS * 2, p);
    return page;
}

/* ======================= Flex OS Ultra: dirección y código ======================= */

static const char *const kPad[] = {"1", "2", "3", "DEL", "\n", "4", "5", "6", ".", "\n",
                                   "7", "8", "9", ":",   "\n", "0", "OK", ""};

typedef struct {
    lv_obj_t *page, *ta[2], *status;
    int cur;
    bool sent;
    uint32_t ok_ms;
} up_t;

static void up_focus(up_t *p, int i)
{
    p->cur = i;
    for (int k = 0; k < 2; k++) {
        if (k == i) {
            lv_obj_add_state(p->ta[k], LV_STATE_FOCUSED);
        } else {
            lv_obj_remove_state(p->ta[k], LV_STATE_FOCUSED);
        }
    }
}

static void up_ta_cb(lv_event_t *e)
{
    up_t *p = lv_event_get_user_data(e);
    up_focus(p, lv_event_get_current_target_obj(e) == p->ta[1]);
}

/* "192.168.1.50" o "192.168.1.50:8080" */
static bool up_addr(const char *in, char *host, size_t hn, uint16_t *port)
{
    const char *c = strchr(in, ':');
    const size_t hl = c ? (size_t)(c - in) : strlen(in);
    if (!hl || hl >= hn) {
        return false;
    }
    memcpy(host, in, hl);
    host[hl] = 0;
    *port = GAL_ULTRA_PORT;
    if (c) {
        const long v = c[1] ? strtol(c + 1, NULL, 10) : 0;
        if (v <= 0 || v > 65535) {
            return false;
        }
        *port = (uint16_t)v;
    }
    return true;
}

static void up_status(up_t *p, const char *t, uint32_t rgb)
{
    char f[160];
    fit_text(t, f, sizeof(f), FLEX_FONT_SMALL, lv_obj_get_style_width(p->status, LV_PART_MAIN), 2);
    set_text(p->status, f);
    watch_theme_set_text_color(p->status, rgb);
}

static void up_pad_cb(lv_event_t *e)
{
    up_t *p = lv_event_get_user_data(e);
    lv_obj_t *kb = lv_event_get_current_target_obj(e);
    const char *k = lv_buttonmatrix_get_button_text(kb, lv_buttonmatrix_get_selected_button(kb));
    if (!k || !G || !G->e) {
        return;
    }
    lv_obj_t *ta = p->ta[p->cur];
    if (!strcmp(k, "DEL")) {
        lv_textarea_delete_char(ta);
        return;
    }
    if (strcmp(k, "OK") != 0) {
        lv_textarea_add_text(ta, k); /* los caracteres que no valen los filtra el campo */
        return;
    }
    char host[40];
    uint16_t port;
    if (!up_addr(lv_textarea_get_text(p->ta[0]), host, sizeof(host), &port)) {
        up_focus(p, 0);
        up_status(p, "Escribe la dirección que enseña Flex OS Ultra, p. ej. 192.168.1.50", FLEX_C_WARN);
        return;
    }
    const char *code = lv_textarea_get_text(p->ta[1]);
    if (strlen(code) != 6) {
        up_focus(p, 1);
        up_status(p, "Escribe el código de 6 cifras de Flex OS Ultra", p->cur == 1 ? FLEX_C_WARN : FLEX_C_TXT2);
        return;
    }
    if (!gal_set_ultra(G->e, host, port)) {
        up_status(p, "Esa dirección no es válida", FLEX_C_WARN);
        return;
    }
    gal_ultra_pair(G->e, code);
    p->sent = true;
    up_status(p, "Comprobando el código…", FLEX_C_TXT2);
}

static void up_tick_cb(lv_timer_t *t)
{
    up_t *p = lv_timer_get_user_data(t);
    if (!G || !G->e || !p->sent) {
        return;
    }
    const gal_status_t *s = &G->st;
    if (s->ultra_pairing) {
        up_status(p, "Comprobando el código…", FLEX_C_TXT2);
    } else if (s->ultra_paired) {
        up_status(p, "Conectado con Flex OS Ultra", FLEX_C_OK);
        if (!p->ok_ms) {
            p->ok_ms = lv_tick_get();
        } else if (lv_tick_get() - p->ok_ms > 1200 && ui_nav_top() == p->page) {
            p->sent = false;
            ui_nav_back();
        }
    } else if (s->ultra_pair_msg[0]) {
        up_status(p, s->ultra_pair_msg, FLEX_C_WARN);
    }
}

static lv_obj_t *ultra_page_create(lv_obj_t *parent)
{
    if (!G || !G->e) {
        return NULL;
    }
    up_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Flex OS Ultra", back_cb, NULL);
    const int32_t w = FLEX_SCREEN_W - 2 * FLEX_SAFE_X;
    lv_obj_t *h = wrap_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, w - 8);
    char f[200];
    fit_text("En Flex OS Ultra abre «Conectar con el móvil»: dirección y código de 6 cifras.", f, sizeof(f),
             FLEX_FONT_SMALL, w - 8, 2);
    set_text(h, f);
    lv_obj_set_pos(h, FLEX_SAFE_X + 4, FLEX_HDR_H - 2);
    const int32_t ty = FLEX_HDR_H + 50;
    for (int i = 0; i < 2; i++) {
        p->ta[i] = lv_textarea_create(page);
        lv_textarea_set_one_line(p->ta[i], true);
        ui_textarea_style(p->ta[i]);
        lv_obj_set_style_pad_ver(p->ta[i], 10, 0);
        lv_obj_add_event_cb(p->ta[i], up_ta_cb, LV_EVENT_CLICKED, p);
    }
    lv_textarea_set_accepted_chars(p->ta[0], "0123456789.:");
    lv_textarea_set_max_length(p->ta[0], 21);
    lv_textarea_set_placeholder_text(p->ta[0], "192.168.1.50");
    lv_obj_set_size(p->ta[0], 226, 50);
    lv_obj_set_pos(p->ta[0], FLEX_SAFE_X, ty);
    lv_textarea_set_accepted_chars(p->ta[1], "0123456789");
    lv_textarea_set_max_length(p->ta[1], 6);
    lv_textarea_set_placeholder_text(p->ta[1], "Código");
    lv_obj_set_size(p->ta[1], w - 226 - 8, 50);
    lv_obj_set_pos(p->ta[1], FLEX_SAFE_X + 226 + 8, ty);
    const gal_status_t *s = &G->st;
    if (s->ultra_host[0]) {
        char a[48];
        if (s->ultra_port && s->ultra_port != GAL_ULTRA_PORT) {
            snprintf(a, sizeof(a), "%s:%u", s->ultra_host, s->ultra_port);
        } else {
            gal_copyz(a, sizeof(a), s->ultra_host);
        }
        lv_textarea_set_text(p->ta[0], a);
    }
    up_focus(p, s->ultra_host[0] ? 1 : 0);
    p->status = wrap_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, w - 8);
    lv_obj_set_pos(p->status, FLEX_SAFE_X + 4, ty + 54);
    if (s->ultra_paired) {
        up_status(p, "Ya está emparejado. Escribe otro código para cambiarlo.", FLEX_C_OK);
    } else if (s->health[GAL_SRC_ULTRA].detail[0]) {
        up_status(p, s->health[GAL_SRC_ULTRA].detail, FLEX_C_TXT2);
    }
    lv_obj_t *kb = lv_buttonmatrix_create(page);
    lv_buttonmatrix_set_map(kb, kPad);
    lv_buttonmatrix_set_button_width(kb, 13, 3); /* "OK" */
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 0, 0);
    lv_obj_set_style_pad_gap(kb, 6, 0);
    flex_glass_apply_states(kb, FLEX_GLASS_CONTROL, LV_PART_ITEMS); /* teclas de vidrio del motor */
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, FLEX_FONT_BODY, LV_PART_ITEMS);
    const int32_t ky = ty + 54 + 2 * lv_font_get_line_height(FLEX_FONT_SMALL) + 8;
    lv_obj_set_size(kb, FLEX_SCREEN_W - 2 * 36, FLEX_SCREEN_H - FLEX_LIST_BOTTOM - 4 - ky);
    lv_obj_set_pos(kb, 36, ky);
    lv_obj_add_event_cb(kb, up_pad_cb, LV_EVENT_VALUE_CHANGED, p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, up_tick_cb, TICK_MS, p);
    return page;
}

/* ======================= Flex Account ======================= */

typedef struct {
    lv_obj_t *page, *list, *stage, *err, *un_b, *state, *quota;
    uint8_t flow, link;
    bool wifi;
    uint32_t un_at;
} ac_t;

static void ac_link_cb(lv_event_t *e)
{
    (void)e;
    if (G && G->e) {
        gal_acct_link(G->e);
    }
}

static void ac_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (G && G->e) {
        gal_acct_link_cancel(G->e);
    }
}

static void ac_unlink_cb(lv_event_t *e)
{
    ac_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (!G || !G->e) {
        return;
    }
    if (p->un_at && now - p->un_at < CONFIRM_MS) {
        p->un_at = 0;
        gal_acct_unlink(G->e);
        return;
    }
    p->un_at = now;
    flex_glass_button_set(p->un_b, "Toca otra vez para desvincular", FLEX_C_DANGER);
}

static bool ac_busy(uint8_t flow)
{
    return flow == GAL_ACCT_REQUESTING || flow == GAL_ACCT_CODE;
}

static void ac_texts(ac_t *p)
{
    const gal_status_t *s = &G->st;
    const int32_t w = FLEX_SCREEN_W - 2 * FLEX_LIST_X - 16;
    char f[200];
    if (p->stage) {
        fit_text(s->acct_stage, f, sizeof(f), FLEX_FONT_SMALL, w, 2);
        set_text(p->stage, f);
    }
    if (p->err) {
        fit_text(s->acct_error, f, sizeof(f), FLEX_FONT_SMALL, w, 4);
        set_text(p->err, f);
    }
    if (p->state) {
        watch_theme_info_set(p->state, gal_link_text(s->acct_link));
    }
    if (p->quota) {
        char a[24], b[24], q[64];
        gal_size_text(s->q_used, a, sizeof(a));
        gal_size_text(s->q_total, b, sizeof(b));
        snprintf(q, sizeof(q), "%s de %s", a, b);
        watch_theme_info_set(p->quota, q);
    }
}

static void ac_build(ac_t *p)
{
    const gal_status_t *s = &G->st;
    const int32_t w = FLEX_SCREEN_W - 2 * FLEX_LIST_X - 16;
    char f[240];
    lv_obj_clean(p->list);
    p->stage = p->err = p->un_b = p->state = p->quota = NULL;
    p->un_at = 0;
    p->flow = s->acct_flow;
    p->link = s->acct_link;
    p->wifi = s->wifi;
    if (s->acct_flow == GAL_ACCT_CODE) {
        lv_obj_t *c = flex_glass_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(c, 6, 0);
        watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT2, "Código de vinculación");
        watch_theme_label(c, FLEX_FONT_LARGE, FLEX_C_TXT, s->acct_code);
        lv_obj_t *l = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2);
        fit_text("En el móvil o el PC abre Flex Account (" GAL_ACCT_HOST "/activate), entra con tu cuenta y "
                 "escribe el código. Es la misma cuenta de Flex OS Ultra y Flex Cloud Web.",
                 f, sizeof(f), FLEX_FONT_SMALL, w, 7);
        set_text(l, f);
        p->stage = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE);
        lv_obj_t *b = flex_glass_button(p->list, "Cancelar", FLEX_GLASS_TINT_NONE, LV_PCT(100), 60);
        lv_obj_add_event_cb(b, ac_cancel_cb, LV_EVENT_CLICKED, p);
    } else if (s->acct_flow == GAL_ACCT_REQUESTING) {
        watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT, "Pidiendo un código…");
        p->stage = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE);
        lv_obj_t *b = flex_glass_button(p->list, "Cancelar", FLEX_GLASS_TINT_NONE, LV_PCT(100), 60);
        lv_obj_add_event_cb(b, ac_cancel_cb, LV_EVENT_CLICKED, p);
    } else if (s->acct_link != GAL_LINK_UNLINKED) {
        lv_obj_t *c = flex_glass_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 4, 0);
        fit_text(s->acct_address[0] ? s->acct_address : "Flex Account", f, sizeof(f), FLEX_FONT_BODY, w - 16, 2);
        set_text(wrap_label(c, FLEX_FONT_BODY, FLEX_C_TXT, w - 16), f);
        set_text(wrap_label(c, FLEX_FONT_SMALL, FLEX_C_MUTE, w - 16), "Este reloj usa tu biblioteca de Flex Cloud");
        p->state = watch_theme_info_row(p->list, "Estado", "");
        if (s->q_total) {
            p->quota = watch_theme_info_row(p->list, "Espacio usado", "");
        }
        p->err = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_WARN);
        if (s->acct_link == GAL_LINK_AUTH_REQUIRED || s->acct_link == GAL_LINK_TOKEN_EXPIRED) {
            lv_obj_t *b = flex_glass_button(p->list, "Vincular otra vez", FLEX_C_PRIMARY, LV_PCT(100), 60);
            lv_obj_add_event_cb(b, ac_link_cb, LV_EVENT_CLICKED, p);
            if (!s->wifi) {
                lv_obj_add_state(b, LV_STATE_DISABLED);
            }
        }
        p->un_b = flex_glass_button(p->list, "Desvincular este reloj", FLEX_GLASS_TINT_NONE, LV_PCT(100), 60);
        lv_obj_add_event_cb(p->un_b, ac_unlink_cb, LV_EVENT_CLICKED, p);
        set_text(list_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE),
                 "Desvincular borra la credencial del reloj; tus fotos siguen en Flex Cloud.");
    } else {
        lv_obj_t *l = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_TXT2);
        set_text(l, "Vincula el reloj con tu Flex Account para ver y subir tus fotos de Flex Cloud: la misma cuenta, "
                    "cuota y biblioteca que Flex OS Ultra y Flex Cloud Web. El reloj no guarda tu contraseña.");
        p->err = list_label(p->list, FLEX_FONT_SMALL, FLEX_C_WARN);
        lv_obj_t *b = flex_glass_button(p->list, "Vincular con Flex Account", FLEX_C_PRIMARY, LV_PCT(100), 60);
        lv_obj_add_event_cb(b, ac_link_cb, LV_EVENT_CLICKED, p);
        if (!s->wifi) {
            lv_obj_add_state(b, LV_STATE_DISABLED);
            set_text(list_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE), "Conecta el Wi-Fi para vincular.");
        }
    }
    ac_texts(p);
}

static void ac_tick_cb(lv_timer_t *t)
{
    ac_t *p = lv_timer_get_user_data(t);
    if (!G || !G->e || G->suspended) {
        return;
    }
    const gal_status_t *s = &G->st;
    const bool code = s->acct_flow == GAL_ACCT_CODE;
    if (s->acct_flow != p->flow && (ac_busy(s->acct_flow) || ac_busy(p->flow) || code)) {
        ac_build(p);
        return;
    }
    if ((s->acct_link == GAL_LINK_UNLINKED) != (p->link == GAL_LINK_UNLINKED) || s->wifi != p->wifi ||
        (p->quota == NULL) != (s->q_total == 0 || s->acct_link == GAL_LINK_UNLINKED)) {
        if (!ac_busy(s->acct_flow)) {
            ac_build(p);
            return;
        }
    }
    p->flow = s->acct_flow;
    if (p->un_at && lv_tick_get() - p->un_at >= CONFIRM_MS) {
        p->un_at = 0;
        flex_glass_button_set(p->un_b, "Desvincular este reloj", FLEX_GLASS_TINT_NONE);
    }
    ac_texts(p);
}

static lv_obj_t *acct_page_create(lv_obj_t *parent)
{
    if (!G || !G->e) {
        return NULL;
    }
    ac_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Flex Account", back_cb, NULL);
    p->list = watch_theme_list(page);
    gal_status(G->e, &G->st);
    ac_build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, ac_tick_cb, TICK_MS * 2, p);
    return page;
}

/* ======================= Transferencias ======================= */

typedef struct {
    lv_obj_t *page, *list, *line[GAL_XFERS];
    uint8_t ids[GAL_XFERS], states[GAL_XFERS];
    int n;
} xf_t;

static void xf_line(const gal_xfer_info_t *x, char *b, size_t n)
{
    const int pct = x->total ? (int)(x->done * 100 / x->total) : 0;
    const char *op = x->op == GAL_X_DOWNLOAD ? "Descargando" : x->op == GAL_X_UPLOAD ? "Subiendo" : "A la papelera";
    switch (x->state) {
    case GAL_XS_QUEUED: snprintf(b, n, "En cola · %s", op); break;
    case GAL_XS_RUN:
        if (x->op == GAL_X_TRASH) {
            snprintf(b, n, "Moviendo a la papelera");
        } else {
            snprintf(b, n, "%s · %d %%", op, pct);
        }
        break;
    case GAL_XS_WAIT: snprintf(b, n, "%s", x->msg[0] ? x->msg : "Esperando para reintentar"); break;
    case GAL_XS_DONE: snprintf(b, n, "%s", x->msg[0] ? x->msg : "Hecho"); break;
    case GAL_XS_FAIL: snprintf(b, n, "%s", x->msg[0] ? x->msg : "Falló"); break;
    default: snprintf(b, n, "Cancelada"); break;
    }
}

static void xf_btn_cb(lv_event_t *e)
{
    const intptr_t v = (intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (!G || !G->e) {
        return;
    }
    if (v < 0) {
        gal_xfer_retry(G->e, (int)-v);
    } else if (v > 0) {
        gal_xfer_cancel(G->e, (int)v);
    } else {
        gal_xfer_clear(G->e);
    }
}

static void xf_build(xf_t *p, const gal_xfer_info_t *x, int n)
{
    const int32_t w = FLEX_SCREEN_W - 2 * FLEX_LIST_X - 32;
    char t[160], f[160];
    lv_obj_clean(p->list);
    p->n = n;
    bool ended = false;
    if (!n) {
        watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2, "No hay transferencias");
    }
    for (int i = 0; i < n; i++) {
        p->ids[i] = x[i].id;
        p->states[i] = x[i].state;
        lv_obj_t *c = flex_glass_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 4, 0);
        fit_text(x[i].name, f, sizeof(f), FLEX_FONT_BODY, w, 1);
        set_text(wrap_label(c, FLEX_FONT_BODY, FLEX_C_TXT, w), f);
        if (x[i].op == GAL_X_UPLOAD) {
            snprintf(t, sizeof(t), "Al %s", gal_src_name(x[i].dest));
        } else {
            snprintf(t, sizeof(t), "De %s", gal_src_name(x[i].dest));
        }
        set_text(wrap_label(c, FLEX_FONT_SMALL, FLEX_C_MUTE, w), t);
        p->line[i] = wrap_label(c, FLEX_FONT_SMALL, x[i].state == GAL_XS_FAIL ? FLEX_C_WARN : FLEX_C_TXT2, w);
        xf_line(&x[i], t, sizeof(t));
        fit_text(t, f, sizeof(f), FLEX_FONT_SMALL, w, 3);
        set_text(p->line[i], f);
        const bool live = x[i].state == GAL_XS_QUEUED || x[i].state == GAL_XS_RUN || x[i].state == GAL_XS_WAIT;
        if (live || x[i].state == GAL_XS_FAIL) {
            lv_obj_t *b = flex_glass_button(c, live ? "Cancelar" : "Reintentar", live ? FLEX_GLASS_TINT_NONE : FLEX_C_PRIMARY,
                                             LV_PCT(100), 52);
            lv_obj_set_user_data(b, (void *)(intptr_t)(live ? x[i].id : -(int)x[i].id));
            lv_obj_add_event_cb(b, xf_btn_cb, LV_EVENT_CLICKED, p);
        } else {
            ended = true;
        }
    }
    if (ended) {
        lv_obj_t *b = flex_glass_button(p->list, "Quitar las terminadas", FLEX_GLASS_TINT_NONE, LV_PCT(100), 60);
        lv_obj_set_user_data(b, (void *)(intptr_t)0);
        lv_obj_add_event_cb(b, xf_btn_cb, LV_EVENT_CLICKED, p);
    }
}

static void xf_tick_cb(lv_timer_t *t)
{
    xf_t *p = lv_timer_get_user_data(t);
    if (!G || !G->e || G->suspended) {
        return;
    }
    gal_xfer_info_t x[GAL_XFERS];
    const int n = gal_xfers(G->e, x, GAL_XFERS);
    bool same = n == p->n;
    for (int i = 0; same && i < n; i++) {
        same = p->ids[i] == x[i].id && p->states[i] == x[i].state;
    }
    if (!same) {
        xf_build(p, x, n);
        return;
    }
    for (int i = 0; i < n; i++) {
        char b[160], f[160];
        xf_line(&x[i], b, sizeof(b));
        fit_text(b, f, sizeof(f), FLEX_FONT_SMALL, lv_obj_get_style_width(p->line[i], LV_PART_MAIN), 3);
        set_text(p->line[i], f);
    }
}

static lv_obj_t *xf_page_create(lv_obj_t *parent)
{
    if (!G || !G->e) {
        return NULL;
    }
    xf_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->n = -1;
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Transferencias", back_cb, NULL);
    p->list = watch_theme_list(page);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    xf_tick_cb(ui_page_timer(page, xf_tick_cb, TICK_MS * 2, p));
    return page;
}

/* ======================= + : añadir, fuentes y lo demás ======================= */

/* Lo que antes estaba en las pestañas y en "Más opciones": recibir desde el
 * móvil (QR o navegador, el Flex Web Server del reloj), las fuentes remotas de
 * siempre (Flex OS Ultra, Flex Cloud, Flex Phone y Origen), dónde se guarda,
 * las transferencias y Música. Nada se quita: cambia de sitio. */
typedef struct {
    lv_obj_t *ultra, *cloud, *phone, *dest;
    uint32_t sig;
} pl_t;

static void pl_open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static void pl_src_cb(lv_event_t *e)
{
    const int src = (int)(intptr_t)lv_event_get_user_data(e);
    if (!G || !G->e) {
        return;
    }
    const gal_status_t *s = &G->st;
    if (src == GAL_SRC_ULTRA && !s->ultra_paired) {
        ui_nav_push(ultra_page_create); /* primero, su dirección y su código */
        return;
    }
    if (src == GAL_SRC_CLOUD && s->health[GAL_SRC_CLOUD].state == GAL_ST_UNSET) {
        ui_nav_push(acct_page_create); /* primero, vincular Flex Account */
        return;
    }
    if (src == GAL_SRC_PHONE && s->health[GAL_SRC_PHONE].state == GAL_ST_UNSET) {
        ui_nav_push(src_page_create); /* allí se explica qué le falta */
        return;
    }
    gal_set_pick(G->e, src == GAL_SRC_ULTRA ? GAL_PICK_ULTRA : src == GAL_SRC_CLOUD ? GAL_PICK_CLOUD : GAL_PICK_PHONE);
    ui_nav_back(); /* a la rejilla, que ya enseña esa fuente */
}

static void pl_values(pl_t *p)
{
    if (!G) {
        return;
    }
    const gal_status_t *s = &G->st;
    watch_theme_row_set_value(p->ultra, s->ultra_paired ? gal_state_name(s->health[GAL_SRC_ULTRA].state) : "Conectar");
    watch_theme_row_set_value(p->cloud, s->health[GAL_SRC_CLOUD].state == GAL_ST_UNSET
                                            ? "Vincular"
                                            : gal_state_name(s->health[GAL_SRC_CLOUD].state));
    watch_theme_row_set_value(p->phone, s->health[GAL_SRC_PHONE].state == GAL_ST_UNSET
                                            ? "No disponible"
                                            : gal_state_name(s->health[GAL_SRC_PHONE].state));
    wm_vol_t v = WM_VOL_WATCH;
    wm_dest_get(&v);
    watch_theme_row_set_value(p->dest, wm_vol_name(v));
}

static void pl_tick_cb(lv_timer_t *t)
{
    pl_values(lv_timer_get_user_data(t));
}

static lv_obj_t *plus_page_create(lv_obj_t *parent)
{
    if (!G) {
        return NULL;
    }
    pl_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Añadir y fuentes", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    watch_theme_section(list, "Desde el móvil");
    lv_obj_t *r = watch_theme_list_row(list, FLEX_ICON_QR, FLEX_C_CAT_GREEN, "Recibir con QR", NULL, true);
    lv_obj_add_event_cb(r, pl_open_cb, LV_EVENT_CLICKED, (void *)ui_xfer_create);
    r = watch_theme_list_row(list, FLEX_ICON_GLOBE, FLEX_C_CAT_NET, "Desde el navegador", NULL, true);
    lv_obj_add_event_cb(r, pl_open_cb, LV_EVENT_CLICKED, (void *)ui_xfer_create);
    watch_theme_section(list, "Fuentes");
    p->ultra = watch_theme_list_row(list, FLEX_ICON_WIFI, src_rgb(GAL_SRC_ULTRA), "Flex OS Ultra", "", true);
    lv_obj_add_event_cb(p->ultra, pl_src_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAL_SRC_ULTRA);
    p->cloud = watch_theme_list_row(list, FLEX_ICON_GLOBE, src_rgb(GAL_SRC_CLOUD), "Flex Cloud", "", true);
    lv_obj_add_event_cb(p->cloud, pl_src_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAL_SRC_CLOUD);
    p->phone = watch_theme_list_row(list, FLEX_ICON_PHONE, src_rgb(GAL_SRC_PHONE), "Flex Phone", "", true);
    lv_obj_add_event_cb(p->phone, pl_src_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GAL_SRC_PHONE);
    r = watch_theme_list_row(list, FLEX_ICON_GEAR, FLEX_C_CAT_BLUE, "Origen", NULL, true);
    lv_obj_add_event_cb(r, pl_open_cb, LV_EVENT_CLICKED, (void *)src_page_create);
    watch_theme_section(list, "Más");
    p->dest = watch_theme_list_row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_SUN, "Guardar en", "", true);
    lv_obj_add_event_cb(p->dest, pl_open_cb, LV_EVENT_CLICKED, (void *)ui_dest_create);
    r = watch_theme_list_row(list, FLEX_ICON_UPDATE, FLEX_C_CAT_BLUE, "Transferencias", NULL, true);
    lv_obj_add_event_cb(r, pl_open_cb, LV_EVENT_CLICKED, (void *)xf_page_create);
    r = watch_theme_list_row(list, FLEX_ICON_NOTE, 0xEC4C6C, "Música", NULL, true);
    lv_obj_add_event_cb(r, pl_open_cb, LV_EVENT_CLICKED, (void *)ui_music_create);
    pl_values(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, pl_tick_cb, TICK_MS * 4, p);
    return page;
}

static void plus_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(plus_page_create);
}

/* ======================= la app ======================= */

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!G || G->suspended) {
        return;
    }
    if (!G->e) { /* sin memoria al abrir: se reintenta sola, sin forzar nada */
        const uint32_t now = lv_tick_get();
        if (now - G->retry_ms >= RETRY_MS) {
            G->retry_ms = now;
            G->e = watch_gallery_open();
            G->dirty = G->e != NULL;
            if (G->e && V) {
                vw_start(); /* el visor esperaba al motor (retomada sin memoria) */
            }
        }
        if (!G->e) {
            card_set("Sin memoria para la Galería", "Se vuelve a intentar sola; cerrar otra app libera memoria.", NULL,
                     CARD_NONE);
            watch_theme_set_visible(G->card, true);
            return;
        }
    }
    gal_status(G->e, &G->st);
    if (G->dirty || G->st.local_gen != G->lgen || G->st.remote_gen != G->rgen) {
        rebuild();
    }
    update_chip();
    update_card();
    update_xp();
    for (int k = 0; k < POOL; k++) {
        cell_try(&G->cell[k]);
    }
    maybe_more();
}

/* Suspendida (a la esfera sin cerrar): fuera miniaturas, visor y JSON; las transferencias siguen */
static void suspend_cb(lv_event_t *e)
{
    (void)e;
    if (!G || G->suspended) {
        return;
    }
    G->suspended = true;
    if (V) {
        vw_hide_img();
        if (G->e) {
            gal_view_close(G->e);
        }
    }
    unbind_all();
    if (G->e) {
        watch_gallery_close();
        G->e = NULL;
    }
}

static void resume_cb(lv_event_t *e)
{
    (void)e;
    if (!G || !G->suspended) {
        return;
    }
    G->suspended = false;
    G->e = watch_gallery_open();
    G->retry_ms = lv_tick_get();
    G->dirty = true;
    if (G->e) {
        gal_status(G->e, &G->st);
        if (V) {
            vw_start();
        }
    }
}

static void del_cb(lv_event_t *e)
{
    (void)e;
    if (V) { /* el visor se borra después: que no enseñe lo que el motor va a soltar */
        vw_hide_img();
    }
    unbind_all();
    if (G->e) {
        if (V) {
            gal_view_close(G->e);
        }
        watch_gallery_close();
    }
    heap_caps_free(G->items);
    lv_free(G);
    G = NULL;
}

static lv_obj_t *tab_create(lv_obj_t *page, int t, int32_t x, int32_t w, const char *text)
{
    lv_obj_t *b = flex_glass_tile(page, x, TABS_Y, w, TABS_H);
    lv_obj_set_style_pad_all(b, 0, 0);
    flex_glass_set_tint(b, FLEX_C_PRIMARY, LV_STATE_CHECKED); /* la elegida, tintada */
    lv_obj_add_event_cb(b, tab_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
    lv_obj_t *l = watch_theme_label(b, FLEX_TYPE_LABEL, FLEX_C_TXT, text);
    lv_obj_center(l);
    if (t == G->ktab) {
        lv_obj_add_state(b, LV_STATE_CHECKED);
    }
    return b;
}

lv_obj_t *ui_gallery_create(lv_obj_t *parent)
{
    if (G) {
        return NULL; /* una sola: el motor y la sesión del P4 son únicos */
    }
    G = lv_malloc_zeroed(sizeof(*G));
    LV_ASSERT_MALLOC(G);
    G->tab = GAL_TAB_ALL;
    G->ktab = KT_PHOTOS;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    G->page = page;
    lv_obj_t *hdr = watch_theme_header(page, "Galería", back_cb, NULL);

    /* transferencias en marcha o fallidas: a la derecha de la cabecera */
    const int32_t xy = FLEX_SAFE_TOP + (FLEX_HDR_ZONE - XP_H) / 2;
    G->xp = flex_glass_tile(hdr, flex_safe_right(xy, xy + XP_H - 1, FLEX_SAFE_MARGIN) + 1 - XP_W, xy, XP_W, XP_H);
    lv_obj_set_style_pad_all(G->xp, 0, 0);
    lv_obj_add_event_cb(G->xp, xp_cb, LV_EVENT_CLICKED, NULL);
    G->xp_ok = flex_icon_create(G->xp, FLEX_ICON_UPDATE, 20, FLEX_C_ACCENT_LIGHT);
    lv_obj_align(G->xp_ok, LV_ALIGN_LEFT_MID, 12, 0);
    G->xp_err = flex_icon_create(G->xp, FLEX_ICON_CROSS, 20, FLEX_C_ERR);
    lv_obj_align(G->xp_err, LV_ALIGN_LEFT_MID, 12, 0);
    G->xp_l = watch_theme_label(G->xp, FLEX_FONT_SMALL, FLEX_C_TXT, "");
    lv_obj_align(G->xp_l, LV_ALIGN_LEFT_MID, 40, 0);
    lv_obj_add_flag(G->xp, LV_OBJ_FLAG_HIDDEN);

    /* Fotos · Vídeos · + (lo de añadir y las fuentes) */
    const int32_t span = GRID_W + 2 * (GRID_X - FLEX_SAFE_X), gap = 6;
    const int32_t w0 = (span - TABS_H - 2 * gap) / 2;
    G->tabs[KT_PHOTOS] = tab_create(page, KT_PHOTOS, FLEX_SAFE_X, w0, "Fotos");
    G->tabs[KT_VIDEOS] = tab_create(page, KT_VIDEOS, FLEX_SAFE_X + w0 + gap, w0, "Vídeos");
    G->plus = flex_glass_icon_button(page, FLEX_ICON_PLUS, 22, TABS_H, FLEX_GLASS_TINT_NONE);
    lv_obj_set_pos(G->plus, FLEX_SAFE_X + span - TABS_H, TABS_Y);
    lv_obj_add_event_cb(G->plus, plus_cb, LV_EVENT_CLICKED, NULL);

    /* desplazamiento: origen, tarjeta de estado y rejilla */
    G->scroll = lv_obj_create(page);
    lv_obj_remove_style_all(G->scroll);
    lv_obj_set_pos(G->scroll, 0, SC_Y);
    lv_obj_set_size(G->scroll, FLEX_SCREEN_W, SC_H);
    lv_obj_set_scroll_dir(G->scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(G->scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(G->scroll, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(G->scroll, scroll_cb, LV_EVENT_SCROLL, NULL);
    G->chip = flex_glass_tile(G->scroll, GRID_X, 0, GRID_W, CHIP_H);
    lv_obj_set_style_pad_all(G->chip, 0, 0);
    lv_obj_add_event_cb(G->chip, chip_cb, LV_EVENT_CLICKED, NULL);
    G->chip_dot = dot(G->chip, 12);
    lv_obj_align(G->chip_dot, LV_ALIGN_LEFT_MID, 16, 0);
    G->chip_l = wrap_label(G->chip, FLEX_FONT_SMALL, FLEX_C_TXT, GRID_W - 40 - 36);
    lv_obj_align(G->chip_l, LV_ALIGN_LEFT_MID, 40, 0);
    lv_obj_align(flex_icon_create(G->chip, FLEX_ICON_CHEVRON_RIGHT, 18, FLEX_C_MUTE), LV_ALIGN_RIGHT_MID, -14, 0);
    G->card = flex_glass_tile(G->scroll, GRID_X, GRID_Y, GRID_W, LV_SIZE_CONTENT);
    lv_obj_remove_flag(G->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(G->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(G->card, 8, 0);
    G->card_t = wrap_label(G->card, FLEX_FONT_BODY, FLEX_C_TXT, GRID_W - 32);
    G->card_s = wrap_label(G->card, FLEX_FONT_SMALL, FLEX_C_TXT2, GRID_W - 32);
    G->card_b = flex_glass_button(G->card, "", FLEX_GLASS_TINT_NONE, LV_PCT(100), 56);
    lv_obj_add_event_cb(G->card_b, card_btn_cb, LV_EVENT_CLICKED, NULL);
    G->card_bl = lv_obj_get_child(G->card_b, 0);
    G->spacer = lv_obj_create(G->scroll);
    lv_obj_remove_style_all(G->spacer);
    lv_obj_set_size(G->spacer, 1, 1);
    lv_obj_set_pos(G->spacer, 0, GRID_Y + 8);
    lv_obj_remove_flag(G->spacer, LV_OBJ_FLAG_CLICKABLE);
    for (int k = 0; k < POOL; k++) {
        cell_create(&G->cell[k]);
    }

    G->e = watch_gallery_open();
    G->retry_ms = lv_tick_get();
    G->dirty = true;
    lv_obj_add_event_cb(page, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), NULL);
    lv_obj_add_event_cb(page, resume_cb, (lv_event_code_t)ui_nav_event_resume(), NULL);
    tick_cb(ui_page_timer(page, tick_cb, TICK_MS, NULL));
    return page;
}
