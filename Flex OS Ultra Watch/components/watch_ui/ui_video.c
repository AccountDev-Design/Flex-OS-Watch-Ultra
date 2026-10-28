/*
 * ui_video — la pantalla de un vídeo (docs/MULTIMEDIA.md §8): el fotograma
 * que decodifica wm_video, centrado sobre negro, y una capa de vidrio con
 * reproducir/pausa, la barra (tocar = ir ahí) y el tiempo. Tocar el vídeo
 * enseña u oculta la capa; reproduciendo, se oculta sola a los 3 s.
 *
 *  - La interfaz solo recoge el último fotograma (wv_frame_acquire): nunca
 *    decodifica ni lee el archivo.
 *  - Dos descriptores de imagen, uno por lienzo: cada fotograma nuevo cambia
 *    de descriptor, y el del lienzo que se reutiliza se suelta de la caché.
 *  - Al salir (o al ir a la esfera) primero deja de enseñar el lienzo y
 *    después cierra el vídeo, que suelta su memoria y la microSD.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "misc/cache/lv_cache.h"
#include "ui_logic.h"
#include "wa_logic.h"
#include "watch_theme.h"
#include "wm_video.h"

#define FRAME_MS   16     /* se mira si hay fotograma nuevo a 60 Hz */
#define HIDE_MS    3000
#define BAR_W      300
#define CTRL_D     72

typedef struct {
    lv_obj_t *page, *img, *msg, *hdr, *ctrl, *play, *play_icon, *bar, *pos, *dur;
    lv_image_dsc_t dsc[2];
    int cur;               /* descriptor en uso */
    const uint16_t *shown; /* lienzo a la vista */
    uint32_t frame_gen, gen;
    uint32_t touch_ms;
    bool overlay, opened;
    char why[96];
} vp_t;

static wm_vol_t s_vol;
static char s_rel[128];

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void overlay(vp_t *p, bool on)
{
    p->overlay = on;
    p->touch_ms = lv_tick_get();
    watch_theme_set_visible(p->hdr, on);
    watch_theme_set_visible(p->ctrl, on);
}

static void show_frame(vp_t *p)
{
    int w = 0, h = 0;
    uint32_t g = 0;
    const uint16_t *px = wv_frame_acquire(&w, &h, &g);
    if (!px || g == p->frame_gen) {
        return;
    }
    p->frame_gen = g;
    if (px != p->shown) {
        /* otro lienzo: el otro descriptor (el que lo señalaba antes se suelta) */
        p->cur ^= 1;
        lv_image_dsc_t *d = &p->dsc[p->cur];
        lv_image_cache_drop(d);
        memset(d, 0, sizeof(*d));
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = LV_COLOR_FORMAT_RGB565;
        d->header.w = (uint32_t)w;
        d->header.h = (uint32_t)h;
        d->header.stride = (uint32_t)w * 2;
        d->data_size = (uint32_t)w * (uint32_t)h * 2;
        d->data = (const uint8_t *)px;
        lv_image_set_src(p->img, d);
        lv_obj_center(p->img);
        p->shown = px;
    } else {
        lv_image_cache_drop(&p->dsc[p->cur]);
        lv_obj_invalidate(p->img); /* el mismo lienzo con otro fotograma */
    }
    watch_theme_set_visible(p->img, true);
}

static void refresh(vp_t *p)
{
    if (!p->opened) {
        return; /* el motivo de no haberlo abierto se queda a la vista */
    }
    wv_status_t s;
    wv_status(&s);
    if (s.gen == p->gen && s.state != WV_PLAYING) {
        return;
    }
    p->gen = s.gen;
    char t[48];
    switch (s.state) {
    case WV_OPENING:
        set_text(p->msg, "Abriendo…");
        break;
    case WV_ERROR:
        set_text(p->msg, s.err[0] ? s.err : "No se pudo reproducir");
        break;
    default:
        set_text(p->msg, "");
        break;
    }
    watch_theme_set_visible(p->msg, lv_label_get_text(p->msg)[0] != 0);
    flex_icon_set(p->play_icon, s.state == WV_PLAYING ? FLEX_ICON_PAUSE : FLEX_ICON_PLAY);
    wa_fmt_time(s.pos_ms, t, sizeof(t));
    set_text(p->pos, t);
    wa_fmt_time(s.dur_ms, t, sizeof(t));
    set_text(p->dur, s.dur_ms ? t : "--:--");
    const int32_t v = s.dur_ms ? (int32_t)((uint64_t)(s.pos_ms > s.dur_ms ? s.dur_ms : s.pos_ms) * 1000u / s.dur_ms) : 0;
    if (lv_bar_get_value(p->bar) != v) {
        lv_bar_set_value(p->bar, v, LV_ANIM_OFF);
    }
    if (s.state == WV_ERROR || s.state == WV_ENDED || s.state == WV_PAUSED) {
        if (!p->overlay) {
            overlay(p, true); /* parado: los mandos a la vista */
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    vp_t *p = lv_timer_get_user_data(t);
    show_frame(p);
    refresh(p);
    wv_status_t s;
    wv_status(&s);
    if (p->overlay && s.state == WV_PLAYING && lv_tick_elaps(p->touch_ms) > HIDE_MS) {
        overlay(p, false);
    }
}

static void tap_cb(lv_event_t *e)
{
    vp_t *p = lv_event_get_user_data(e);
    overlay(p, !p->overlay);
}

static void play_cb(lv_event_t *e)
{
    vp_t *p = lv_event_get_user_data(e);
    wv_toggle();
    p->touch_ms = lv_tick_get();
}

static void seek_cb(lv_event_t *e)
{
    vp_t *p = lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_t *in = lv_indev_active();
    if (!in) {
        return;
    }
    lv_indev_get_point(in, &pt);
    lv_area_t a;
    lv_obj_get_coords(p->bar, &a);
    const int32_t w = lv_area_get_width(&a);
    int32_t x = pt.x - a.x1;
    x = x < 0 ? 0 : x > w ? w : x;
    wv_status_t s;
    wv_status(&s);
    if (s.dur_ms && w > 0) {
        wv_seek_ms((uint32_t)((uint64_t)s.dur_ms * (uint32_t)x / (uint32_t)w));
    }
    p->touch_ms = lv_tick_get();
}

static void suspend_cb(lv_event_t *e)
{
    (void)e;
    wv_status_t s;
    wv_status(&s);
    if (s.state == WV_PLAYING) {
        wv_toggle(); /* a la esfera: en pausa, no sigue gastando batería */
    }
}

static void del_cb(lv_event_t *e)
{
    vp_t *p = lv_event_get_user_data(e);
    lv_image_set_src(p->img, NULL); /* primero deja de enseñar el lienzo... */
    lv_image_cache_drop(&p->dsc[0]);
    lv_image_cache_drop(&p->dsc[1]);
    if (p->opened) {
        wv_close(); /* ...y después se suelta */
    }
    lv_free(p);
}

void ui_video_open(int vol, const char *rel, const char *name)
{
    s_vol = vol ? WM_VOL_SD : WM_VOL_WATCH;
    (void)name; /* la cabecera dice "Vídeo": un nombre largo se recortaría */
    ui_copy_utf8(s_rel, sizeof(s_rel), rel ? rel : "");
    ui_nav_push(ui_video_create);
}

lv_obj_t *ui_video_create(lv_obj_t *parent)
{
    vp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, tap_cb, LV_EVENT_SHORT_CLICKED, p);

    p->img = lv_image_create(page);
    lv_obj_remove_flag(p->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p->img, LV_OBJ_FLAG_HIDDEN);
    p->msg = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->msg, 320);
    lv_obj_set_style_text_align(p->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(p->msg);

    p->hdr = watch_theme_header(page, "Vídeo", back_cb, NULL);

    /* Capa de mandos: vidrio abajo, lejos de la esquina */
    p->ctrl = flex_glass_panel(page, FLEX_GLASS_CARD, (FLEX_SCREEN_W - 340) / 2, FLEX_SCREEN_H - 168, 340, 120, 30);
    lv_obj_remove_flag(p->ctrl, LV_OBJ_FLAG_CLICKABLE);
    p->play = flex_glass_icon_button(p->ctrl, FLEX_ICON_PAUSE, 28, CTRL_D - 16, FLEX_GLASS_TINT_NONE);
    lv_obj_align(p->play, LV_ALIGN_TOP_MID, 0, 6);
    p->play_icon = lv_obj_get_child(p->play, 0);
    lv_obj_add_event_cb(p->play, play_cb, LV_EVENT_CLICKED, p);
    p->bar = watch_theme_bar(p->ctrl, BAR_W - 40, 6, FLEX_GLASS_TXT, 0x4A4E58);
    lv_obj_align(p->bar, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_flag(p->bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(p->bar, 14);
    lv_obj_add_event_cb(p->bar, seek_cb, LV_EVENT_CLICKED, p);
    p->pos = watch_theme_label(p->ctrl, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT2, "0:00");
    lv_obj_align(p->pos, LV_ALIGN_TOP_LEFT, 20, 82);
    p->dur = watch_theme_label(p->ctrl, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT2, "--:--");
    lv_obj_align(p->dur, LV_ALIGN_TOP_RIGHT, -20, 82);

    p->opened = wv_open(s_vol, s_rel, p->why, sizeof(p->why));
    if (!p->opened) {
        set_text(p->msg, p->why);
        watch_theme_set_visible(p->msg, true);
    }
    p->gen = 0xFFFFFFFFu;
    overlay(p, true);
    lv_obj_add_event_cb(page, del_cb, LV_EVENT_DELETE, p);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), NULL);
    ui_page_timer(page, tick_cb, FRAME_MS, p);
    return page;
}
