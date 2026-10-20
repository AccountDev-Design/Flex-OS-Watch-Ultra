/*
 * ui_face_editor — selector de esferas.
 *
 * Se abre manteniendo pulsada la esfera o desde Ajustes > Pantalla y fondo
 * de pantalla > Esfera (el mismo selector). Las vistas previas son las
 * esferas REALES: cada una se construye con ui_face_create_style (hora,
 * fecha, fondo y complicaciones de ahora mismo), se renderiza una vez con
 * lv_snapshot y se reduce a la mitad (205x251, RGB565 en PSRAM). Se generan
 * de una en una con un temporizador para no bloquear la interfaz; después,
 * desplazarse en horizontal (una esfera por página) solo mueve imágenes.
 *
 * Tocar una esfera la aplica: se guarda en los ajustes (watch_settings, NVS:
 * se recupera tras reiniciar) y la raíz se rehace. Desde la esfera se vuelve
 * a ella; desde Ajustes, también directamente a la esfera.
 */
#include "ui_internal.h"

#include <string.h>
#include "watch_settings.h"
#include "watch_theme.h"

#define TH_W        (FLEX_SCREEN_W / 2)   /* 205 */
#define TH_H        (FLEX_SCREEN_H / 2)   /* 251 */
#define ITEM_W      FLEX_SCREEN_W   /* una esfera por página: nada mordido por las esquinas */
#define STRIP_Y     (FLEX_HDR_H + 8)
#define STRIP_H     (TH_H + 60)
#define GEN_MS      30

typedef struct {
    lv_obj_t *strip;
    lv_obj_t *card[UI_FACE_COUNT_];
    lv_obj_t *img[UI_FACE_COUNT_];
    lv_image_dsc_t dsc[UI_FACE_COUNT_];
    uint16_t *px[UI_FACE_COUNT_];
    int next; /* próxima vista previa que generar */
    lv_timer_t *gen;
} ed_t;

static bool s_from_settings;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* 2x2 -> 1 con promedio de canales (RGB565). */
static void downscale(const uint16_t *src, int sw, int sh, uint32_t stride_px, uint16_t *dst)
{
    for (int y = 0; y < sh / 2; y++) {
        const uint16_t *r0 = src + (size_t)(2 * y) * stride_px;
        const uint16_t *r1 = r0 + stride_px;
        for (int x = 0; x < sw / 2; x++) {
            const uint16_t p[4] = {r0[2 * x], r0[2 * x + 1], r1[2 * x], r1[2 * x + 1]};
            uint32_t r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                r += p[k] >> 11;
                g += (p[k] >> 5) & 0x3F;
                b += p[k] & 0x1F;
            }
            dst[(size_t)y * (sw / 2) + x] = (uint16_t)(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
        }
    }
}

static bool make_preview(ed_t *e, lv_obj_t *page, int style)
{
    lv_obj_t *face = ui_face_create_style(page, style, false);
    if (!face) {
        return false;
    }
    lv_obj_add_flag(face, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(face);
    bool ok = false;
    lv_draw_buf_t *snap = lv_snapshot_take(face, LV_COLOR_FORMAT_RGB565);
    if (snap) {
        e->px[style] = lv_malloc((size_t)TH_W * TH_H * 2);
        if (e->px[style]) {
            downscale((const uint16_t *)snap->data, FLEX_SCREEN_W, FLEX_SCREEN_H, snap->header.stride / 2,
                      e->px[style]);
            lv_image_dsc_t *d = &e->dsc[style];
            memset(d, 0, sizeof(*d));
            d->header.magic = LV_IMAGE_HEADER_MAGIC;
            d->header.cf = LV_COLOR_FORMAT_RGB565;
            d->header.w = TH_W;
            d->header.h = TH_H;
            d->header.stride = TH_W * 2;
            d->data_size = (uint32_t)TH_W * TH_H * 2;
            d->data = (const uint8_t *)e->px[style];
            lv_image_set_src(e->img[style], d);
            ok = true;
        }
        lv_draw_buf_destroy(snap);
    }
    lv_obj_delete(face);
    return ok;
}

static void gen_cb(lv_timer_t *t)
{
    ed_t *e = lv_timer_get_user_data(t);
    lv_obj_t *page = lv_obj_get_parent(e->strip);
    while (e->next < UI_FACE_COUNT_ && e->px[e->next]) {
        e->next++; /* ya hecha (la elegida, al abrir) */
    }
    if (e->next < UI_FACE_COUNT_) {
        make_preview(e, page, e->next);
        e->next++;
    }
    if (e->next >= UI_FACE_COUNT_) {
        lv_timer_delete(t);
        e->gen = NULL;
    }
}

static void pick_cb(lv_event_t *ev)
{
    const int style = (int)(intptr_t)lv_event_get_user_data(ev);
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.face != style) {
        ws.face = (uint8_t)style;
        watch_settings_update(&ws);
        ui_nav_replace_root(ui_face_create);
    }
    if (s_from_settings) {
        ui_nav_home(); /* al terminar desde Ajustes, a la esfera */
    } else {
        ui_nav_back();
    }
}

static void free_cb(lv_event_t *ev)
{
    ed_t *e = lv_event_get_user_data(ev);
    if (e->gen) {
        lv_timer_delete(e->gen);
    }
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        lv_free(e->px[i]);
    }
    lv_free(e);
}

lv_obj_t *ui_face_editor_create(lv_obj_t *parent)
{
    ed_t *e = lv_malloc_zeroed(sizeof(*e));
    LV_ASSERT_MALLOC(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Esferas", back_cb, NULL);

    lv_obj_t *strip = lv_obj_create(page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 0, STRIP_Y);
    lv_obj_set_size(strip, FLEX_SCREEN_W, STRIP_H);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(strip, (FLEX_SCREEN_W - ITEM_W) / 2, 0);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_SCROLL_ONE);
    e->strip = strip;

    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        lv_obj_t *item = lv_obj_create(strip);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, ITEM_W, STRIP_H);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(item, pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *card = lv_obj_create(item);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, TH_W, TH_H);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(card, 40, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_set_style_bg_color(card, flex_color(FLEX_C_SURF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        e->card[i] = card;
        e->img[i] = lv_image_create(card);
        lv_obj_set_pos(e->img[i], 0, 0);
        lv_obj_remove_flag(e->img[i], LV_OBJ_FLAG_CLICKABLE);
        /* Aro: la elegida, en el acento */
        lv_obj_t *ring = lv_obj_create(item);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, TH_W + 8, TH_H + 8);
        lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, -4);
        lv_obj_set_style_radius(ring, 44, 0);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_set_style_border_color(ring, flex_color(i == ws.face ? FLEX_C_ACCENT : FLEX_C_BORDER), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = watch_theme_label(item, FLEX_FONT_BODY, i == ws.face ? FLEX_C_ACCENT : FLEX_C_TXT,
                                           ui_face_name(i));
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    lv_obj_t *hint = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       ui_psave_active() ? "En ahorro de energía se usa la esfera mínima."
                                                         : "Desliza y toca una esfera para aplicarla.");
    lv_obj_set_width(hint, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 16);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, STRIP_Y + STRIP_H + 4);

    lv_obj_update_layout(strip);
    lv_obj_t *cur = lv_obj_get_child(strip, ws.face < UI_FACE_COUNT_ ? ws.face : 0);
    lv_obj_scroll_to_view(cur, LV_ANIM_OFF);

    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, e);
    /* La elegida primero: es la que se ve al abrir. */
    e->next = 0;
    make_preview(e, page, ws.face < UI_FACE_COUNT_ ? ws.face : 0);
    e->gen = lv_timer_create(gen_cb, GEN_MS, e);
    return page;
}

void ui_face_editor_open(bool from_settings)
{
    s_from_settings = from_settings;
    ui_nav_push(ui_face_editor_create);
}
