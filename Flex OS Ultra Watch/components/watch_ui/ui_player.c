/*
 * ui_player — app Música del reloj (docs/MULTIMEDIA.md §6 y §9): la
 * biblioteca local (memoria del reloj y microSD) y "Reproduciendo".
 *
 * Toda la reproducción la hace el servicio wa_player (sigue sonando al salir
 * de la app); estas páginas le mandan órdenes y leen su estado y su portada.
 * "En el teléfono" (ui_music.c) sigue siendo el mando de lo que suena en el
 * teléfono.
 *
 * "Reproduciendo" (composición del reproductor del Apple Watch con el vidrio
 * de Flex OS): fondo desenfocado DESDE LA PORTADA (flex_glass_backdrop: se
 * calcula una vez por canción, nunca desde la pantalla, así que no deja
 * rastros), portada de 200 px, título y artista, barra de progreso que se
 * puede tocar para saltar, los tres controles y el volumen.
 */
#include "ui_internal.h"

#include "misc/cache/lv_cache.h" /* lv_image_cache_drop (no está en lvgl.h) */

#include <stdio.h>
#include <string.h>
#include "wa_logic.h"
#include "wa_player.h"
#include "watch_audio.h"
#include "watch_media.h"
#include "watch_theme.h"
#include "wm_store.h"

#define LIB_MAX     WAP_LIST_MAX
#define NP_TICK_MS  250
#define HOME_TICK_MS 500
#define COVER_Y     44
#define TITLE_Y     252
#define BAR_Y       334
#define BAR_W       300
#define CTRL_Y      366
#define CTRL_D      64
#define PLAY_D      84
#define VOL_Y       458
#define VOL_W       200

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

static bool is_song(const FlexMlRec *r)
{
    return r->kind == FML_K_AUDIO && (r->flags & FML_R_PLAYABLE) && !(r->flags & FML_R_LOCKED) &&
           strncmp(r->path, FML_DIR_REC "/", sizeof(FML_DIR_REC)) != 0;
}

static void track_of(const FlexMlRec *r, uint8_t vol, wap_track_t *t)
{
    memset(t, 0, sizeof(*t));
    t->vol = vol;
    t->id = r->id;
    snprintf(t->path, sizeof(t->path), "%s", r->path);
    ui_media_display_name(r, t->name, sizeof(t->name));
    ui_copy_utf8(t->title, sizeof(t->title), r->title[0] ? r->title : t->name);
    ui_copy_utf8(t->artist, sizeof(t->artist), r->artist);
    ui_copy_utf8(t->album, sizeof(t->album), r->album);
    t->dur_ms = r->durMs;
}

/* ======================= Reproduciendo ======================= */

typedef struct {
    lv_obj_t *backdrop, *cover, *cover_ph;
    lv_obj_t *title, *artist, *info;
    lv_obj_t *bar, *pos, *dur;
    lv_obj_t *prev, *play, *play_icon, *next;
    lv_obj_t *vol;
    lv_image_dsc_t dsc;
    uint16_t *px;            /* portada: WAP_COVER_SIDE^2 RGB565 */
    uint32_t gen, cover_gen;
    bool cover_seen;
} np_page_t;

static void np_cover(np_page_t *p)
{
    uint32_t g = 0;
    if (p->px && wap_cover_copy(p->px, &g)) {
        lv_image_cache_drop(&p->dsc);
        lv_obj_invalidate(p->cover);
        watch_theme_set_visible(p->cover, true);
        watch_theme_set_visible(p->cover_ph, false);
        flex_glass_backdrop_set(p->backdrop, p->px, WAP_COVER_SIDE, WAP_COVER_SIDE, WAP_COVER_SIDE, 110);
    } else {
        watch_theme_set_visible(p->cover, false);
        watch_theme_set_visible(p->cover_ph, true);
        flex_glass_backdrop_clear(p->backdrop);
    }
    p->cover_gen = g;
    p->cover_seen = true;
}

static void np_refresh(np_page_t *p)
{
    wap_status_t s;
    wap_status(&s);
    if (!p->cover_seen || s.cover_gen != p->cover_gen) {
        np_cover(p);
    }
    if (s.gen != p->gen) {
        p->gen = s.gen;
        const bool have = s.index >= 0 && s.state != WAP_IDLE;
        set_text(p->title, have ? (s.track.title[0] ? s.track.title : s.track.name) : "Nada sonando");
        char a[96];
        if (s.track.artist[0] && s.track.album[0]) {
            snprintf(a, sizeof(a), "%s · %s", s.track.artist, s.track.album);
        } else {
            snprintf(a, sizeof(a), "%s", s.track.artist[0] ? s.track.artist : s.track.album);
        }
        set_text(p->artist, have ? a : "");
        if (s.state == WAP_ERROR || (s.state == WAP_PAUSED && s.error[0])) {
            set_text(p->info, s.error);
            watch_theme_set_text_color(p->info, FLEX_C_WARN);
        } else if (s.preempted) {
            set_text(p->info, "En pausa: la grabadora está usando el audio");
            watch_theme_set_text_color(p->info, FLEX_C_TXT2);
        } else if (s.state == WAP_LOADING) {
            set_text(p->info, "Cargando…");
            watch_theme_set_text_color(p->info, FLEX_C_TXT2);
        } else {
            char f[64];
            if (s.count > 1) {
                snprintf(f, sizeof(f), "%s · %d de %d", s.format, s.index + 1, s.count);
            } else {
                snprintf(f, sizeof(f), "%s", s.format);
            }
            set_text(p->info, f);
            watch_theme_set_text_color(p->info, FLEX_C_MUTE);
        }
        flex_icon_set(p->play_icon, s.state == WAP_PLAYING || s.state == WAP_LOADING ? FLEX_ICON_PAUSE : FLEX_ICON_PLAY);
        enable(p->play, have);
        enable(p->prev, have);
        enable(p->next, have && s.index + 1 < s.count);
    }
    char t[16];
    wa_fmt_time(s.pos_ms, t, sizeof(t));
    set_text(p->pos, t);
    if (s.dur_ms) {
        wa_fmt_time(s.dur_ms, t, sizeof(t));
        set_text(p->dur, t);
        const int32_t v = (int32_t)((uint64_t)(s.pos_ms > s.dur_ms ? s.dur_ms : s.pos_ms) * 1000u / s.dur_ms);
        if (lv_bar_get_value(p->bar) != v) {
            lv_bar_set_value(p->bar, v, LV_ANIM_OFF);
        }
    } else {
        set_text(p->dur, "--:--");
    }
}

static void np_tick_cb(lv_timer_t *t)
{
    np_refresh(lv_timer_get_user_data(t));
}

static void np_play_cb(lv_event_t *e)
{
    wap_toggle();
    np_refresh(lv_event_get_user_data(e));
}

static void np_prev_cb(lv_event_t *e)
{
    (void)e;
    wap_prev();
}

static void np_next_cb(lv_event_t *e)
{
    (void)e;
    wap_next();
}

static void np_seek_cb(lv_event_t *e)
{
    np_page_t *p = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    lv_area_t a;
    lv_obj_get_coords(p->bar, &a);
    const int32_t w = lv_area_get_width(&a);
    int32_t x = pt.x - a.x1;
    x = x < 0 ? 0 : x > w ? w : x;
    wap_status_t s;
    wap_status(&s);
    if (s.dur_ms && w > 0) {
        wap_seek((uint32_t)((uint64_t)s.dur_ms * (uint32_t)x / (uint32_t)w));
    }
}

static void np_vol_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_current_target_obj(e);
    wa_set_volume((uint8_t)lv_slider_get_value(sl)); /* lo aplica la tarea de audio */
}

static void np_free_cb(lv_event_t *e)
{
    np_page_t *p = lv_event_get_user_data(e);
    lv_image_cache_drop(&p->dsc);
    wm_free(p->px);
    lv_free(p);
}

static lv_obj_t *ctl(lv_obj_t *page, int32_t x, int32_t y, int32_t d, flex_icon_t ic, int32_t isz, lv_event_cb_t cb,
                     void *ud, lv_obj_t **icon)
{
    lv_obj_t *b = flex_glass_icon_button(page, ic, isz, d, FLEX_GLASS_TINT_NONE);
    lv_obj_set_pos(b, x, y);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    if (icon) {
        *icon = lv_obj_get_child(b, 0);
    }
    return b;
}

lv_obj_t *ui_nowplaying_create(lv_obj_t *parent)
{
    np_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    p->backdrop = flex_glass_backdrop(page); /* primero: detrás de todo */
    watch_theme_header(page, "", back_cb, NULL);

    p->px = wm_alloc((size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2);
    const int32_t cx = (FLEX_SCREEN_W - WAP_COVER_SIDE) / 2;
    p->cover = lv_image_create(page);
    lv_obj_set_pos(p->cover, cx, COVER_Y);
    if (p->px) {
        memset(p->px, 0, (size_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2);
        p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        p->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        p->dsc.header.w = WAP_COVER_SIDE;
        p->dsc.header.h = WAP_COVER_SIDE;
        p->dsc.header.stride = WAP_COVER_SIDE * 2;
        p->dsc.data_size = (uint32_t)WAP_COVER_SIDE * WAP_COVER_SIDE * 2;
        p->dsc.data = (const uint8_t *)p->px;
        lv_image_set_src(p->cover, &p->dsc);
    }
    lv_obj_set_style_radius(p->cover, 22, 0);
    lv_obj_set_style_clip_corner(p->cover, true, 0);
    lv_obj_add_flag(p->cover, LV_OBJ_FLAG_HIDDEN);
    /* Sin portada: tarjeta de vidrio con una nota. */
    p->cover_ph = flex_glass_panel(page, FLEX_GLASS_CARD, cx, COVER_Y, WAP_COVER_SIDE, WAP_COVER_SIDE, 22);
    lv_obj_remove_flag(p->cover_ph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *note = flex_icon_create(p->cover_ph, FLEX_ICON_NOTE, 76, FLEX_GLASS_TXT2);
    lv_obj_center(note);

    p->title = watch_theme_label(page, FLEX_TYPE_HEADLINE, FLEX_GLASS_TXT, "");
    lv_label_set_long_mode(p->title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->title, 330, lv_font_get_line_height(FLEX_TYPE_HEADLINE));
    lv_obj_set_style_text_align(p->title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->title, (FLEX_SCREEN_W - 330) / 2, TITLE_Y);
    p->artist = watch_theme_label(page, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "");
    lv_label_set_long_mode(p->artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->artist, 330, lv_font_get_line_height(FLEX_TYPE_CAPTION));
    lv_obj_set_style_text_align(p->artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->artist, (FLEX_SCREEN_W - 330) / 2, TITLE_Y + 30);
    p->info = watch_theme_label(page, FLEX_TYPE_FOOTNOTE, FLEX_C_MUTE, "");
    lv_label_set_long_mode(p->info, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->info, 330, lv_font_get_line_height(FLEX_TYPE_FOOTNOTE));
    lv_obj_set_style_text_align(p->info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->info, (FLEX_SCREEN_W - 330) / 2, TITLE_Y + 54);

    p->bar = watch_theme_bar(page, BAR_W, 6, FLEX_GLASS_TXT, 0x4A4E58);
    lv_obj_set_pos(p->bar, (FLEX_SCREEN_W - BAR_W) / 2, BAR_Y);
    lv_obj_add_flag(p->bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(p->bar, 16);
    lv_obj_add_event_cb(p->bar, np_seek_cb, LV_EVENT_CLICKED, p);
    p->pos = watch_theme_label(page, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT2, "0:00");
    lv_obj_set_pos(p->pos, (FLEX_SCREEN_W - BAR_W) / 2, BAR_Y + 10);
    p->dur = watch_theme_label(page, FLEX_TYPE_FOOTNOTE, FLEX_GLASS_TXT2, "--:--");
    lv_obj_set_width(p->dur, 80);
    lv_obj_set_style_text_align(p->dur, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(p->dur, (FLEX_SCREEN_W + BAR_W) / 2 - 80, BAR_Y + 10);

    const int32_t mid = FLEX_SCREEN_W / 2;
    p->play = ctl(page, mid - PLAY_D / 2, CTRL_Y, PLAY_D, FLEX_ICON_PLAY, 36, np_play_cb, p, &p->play_icon);
    p->prev = ctl(page, mid - PLAY_D / 2 - 26 - CTRL_D, CTRL_Y + (PLAY_D - CTRL_D) / 2, CTRL_D, FLEX_ICON_PREV, 28,
                  np_prev_cb, p, NULL);
    p->next = ctl(page, mid + PLAY_D / 2 + 26, CTRL_Y + (PLAY_D - CTRL_D) / 2, CTRL_D, FLEX_ICON_NEXT, 28, np_next_cb,
                  p, NULL);

    lv_obj_t *vic = flex_icon_create(page, FLEX_ICON_VOLUME, 22, FLEX_GLASS_TXT2);
    lv_obj_set_pos(vic, (FLEX_SCREEN_W - VOL_W) / 2 - 34, VOL_Y + 3);
    p->vol = flex_glass_slider(page, (FLEX_SCREEN_W - VOL_W) / 2, VOL_Y, VOL_W, 28, FLEX_GLASS_TXT);
    lv_slider_set_range(p->vol, 0, 100);
    lv_slider_set_value(p->vol, wa_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(p->vol, np_vol_cb, LV_EVENT_VALUE_CHANGED, p);

    p->gen = 0xFFFFFFFFu;
    np_refresh(p);
    lv_obj_add_event_cb(page, np_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, np_tick_cb, NP_TICK_MS, p);
    return page;
}

/* ======================= Biblioteca ======================= */

typedef struct {
    uint8_t vol;
    uint32_t id;
} ref_t;

typedef struct {
    lv_obj_t *list;
    ref_t refs[LIB_MAX];
    int n;
    uint32_t rev[WM_VOL_N];
} lib_page_t;

static void lib_play_cb(lv_event_t *e)
{
    lib_page_t *p = lv_event_get_user_data(e);
    const int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    wap_track_t *list = wm_alloc(sizeof(wap_track_t) * (size_t)(p->n > 0 ? p->n : 1));
    if (!list) {
        ui_banner_show_info("Música", "No hay memoria para la lista");
        return;
    }
    int k = 0, start = 0;
    for (int i = 0; i < p->n; i++) {
        FlexMlRec r;
        if (!wm_lib_get((wm_vol_t)p->refs[i].vol, p->refs[i].id, &r)) {
            continue;
        }
        if (i == idx) {
            start = k;
        }
        track_of(&r, p->refs[i].vol, &list[k++]);
    }
    if (k > 0) {
        wap_play_list(list, k, start);
        ui_nav_push(ui_nowplaying_create);
    }
    wm_free(list);
}

static void lib_build(lib_page_t *p)
{
    lv_obj_clean(p->list);
    p->n = 0;
    FlexMlRec *tmp = wm_alloc(sizeof(FlexMlRec) * LIB_MAX);
    if (!tmp) {
        watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_WARN, "No hay memoria para listar la música.");
        return;
    }
    for (int v = 0; v < WM_VOL_N; v++) {
        const int n = wm_lib_snapshot((wm_vol_t)v, FML_MASK_AUDIO, tmp, LIB_MAX, &p->rev[v]);
        int shown = 0;
        for (int i = 0; i < n && p->n < LIB_MAX; i++) {
            if (!is_song(&tmp[i])) {
                continue;
            }
            if (!shown++) {
                watch_theme_section(p->list, v == WM_VOL_SD ? "microSD" : "Reloj");
            }
            char name[FML_NAME_MAX], val[FML_ARTIST_MAX + 24], d[16];
            ui_media_display_name(&tmp[i], name, sizeof(name));
            wa_fmt_time(tmp[i].durMs, d, sizeof(d));
            snprintf(val, sizeof(val), "%s%s%s", tmp[i].artist, tmp[i].artist[0] && tmp[i].durMs ? " · " : "",
                     tmp[i].durMs ? d : "");
            lv_obj_t *row = watch_theme_list_row(p->list, FLEX_ICON_NOTE, 0xEC4C6C,
                                                 tmp[i].title[0] ? tmp[i].title : name, val, false);
            lv_obj_set_user_data(row, (void *)(intptr_t)p->n);
            lv_obj_add_event_cb(row, lib_play_cb, LV_EVENT_CLICKED, p);
            p->refs[p->n++] = (ref_t){(uint8_t)v, tmp[i].id};
        }
    }
    wm_free(tmp);
    if (p->n == 0) {
        lv_obj_t *l = watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2,
                                        "No hay música en el reloj ni en la microSD. Pásala desde el móvil con "
                                        "Galería > + > Recibir desde el móvil (MP3 o WAV).");
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(l, LV_PCT(92));
    }
}

static void lib_tick_cb(lv_timer_t *t)
{
    lib_page_t *p = lv_timer_get_user_data(t);
    for (int v = 0; v < WM_VOL_N; v++) {
        if (wm_lib_rev((wm_vol_t)v) != p->rev[v]) {
            lib_build(p);
            return;
        }
    }
}

lv_obj_t *ui_music_library_create(lv_obj_t *parent)
{
    lib_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Canciones", back_cb, NULL);
    p->list = watch_theme_list(page);
    lib_build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, lib_tick_cb, 1000, p);
    return page;
}

/* ======================= Música (raíz de la app) ======================= */

typedef struct {
    lv_obj_t *now, *now_title, *now_sub, *now_play, *now_icon;
    lv_obj_t *songs;
    uint32_t gen;
    uint32_t rev[WM_VOL_N];
} home_page_t;

static int count_songs(uint32_t rev[WM_VOL_N])
{
    FlexMlRec *tmp = wm_alloc(sizeof(FlexMlRec) * LIB_MAX);
    if (!tmp) {
        return -1;
    }
    int c = 0;
    for (int v = 0; v < WM_VOL_N; v++) {
        const int n = wm_lib_snapshot((wm_vol_t)v, FML_MASK_AUDIO, tmp, LIB_MAX, &rev[v]);
        for (int i = 0; i < n; i++) {
            c += is_song(&tmp[i]);
        }
    }
    wm_free(tmp);
    return c;
}

static void home_refresh(home_page_t *p)
{
    wap_status_t s;
    wap_status(&s);
    if (s.gen != p->gen) {
        p->gen = s.gen;
        const bool have = s.index >= 0 && s.state != WAP_IDLE && s.state != WAP_ENDED;
        watch_theme_set_visible(p->now, have);
        if (have) {
            set_text(p->now_title, s.track.title[0] ? s.track.title : s.track.name);
            set_text(p->now_sub, s.state == WAP_ERROR ? s.error : (s.track.artist[0] ? s.track.artist : wap_state_name(s.state)));
            flex_icon_set(p->now_icon, s.state == WAP_PLAYING ? FLEX_ICON_PAUSE : FLEX_ICON_PLAY);
        }
    }
    bool changed = false;
    for (int v = 0; v < WM_VOL_N; v++) {
        changed = changed || wm_lib_rev((wm_vol_t)v) != p->rev[v];
    }
    if (changed) {
        const int c = count_songs(p->rev);
        char b[24];
        snprintf(b, sizeof(b), "%d", c < 0 ? 0 : c);
        watch_theme_row_set_value(p->songs, c < 0 ? "--" : b);
    }
}

static void home_tick_cb(lv_timer_t *t)
{
    home_refresh(lv_timer_get_user_data(t));
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

static void now_play_cb(lv_event_t *e)
{
    wap_toggle();
    lv_event_stop_bubbling(e);
    home_refresh(lv_event_get_user_data(e));
}

lv_obj_t *ui_music_create(lv_obj_t *parent)
{
    home_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Música", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    /* Lo que suena: tarjeta de vidrio que abre "Reproduciendo". */
    p->now = flex_glass_create(list, FLEX_GLASS_CARD);
    lv_obj_set_size(p->now, LV_PCT(100), 92);
    lv_obj_set_style_radius(p->now, FLEX_RADIUS_CARD, 0);
    flex_glass_interactive(p->now);
    lv_obj_add_event_cb(p->now, open_cb, LV_EVENT_CLICKED, (void *)ui_nowplaying_create);
    p->now_title = watch_theme_label(p->now, FLEX_TYPE_LABEL, FLEX_GLASS_TXT, "");
    lv_label_set_long_mode(p->now_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->now_title, 240, lv_font_get_line_height(FLEX_TYPE_LABEL));
    lv_obj_set_pos(p->now_title, 18, 16);
    p->now_sub = watch_theme_label(p->now, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "");
    lv_label_set_long_mode(p->now_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->now_sub, 240, lv_font_get_line_height(FLEX_TYPE_CAPTION));
    lv_obj_set_pos(p->now_sub, 18, 46);
    p->now_play = flex_glass_icon_button(p->now, FLEX_ICON_PAUSE, 22, 58, FLEX_GLASS_TINT_NONE);
    lv_obj_align(p->now_play, LV_ALIGN_RIGHT_MID, -14, 0);
    p->now_icon = lv_obj_get_child(p->now_play, 0);
    lv_obj_add_event_cb(p->now_play, now_play_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_flag(p->now, LV_OBJ_FLAG_HIDDEN);

    p->songs = watch_theme_list_row(list, FLEX_ICON_NOTE, 0xEC4C6C, "Canciones", "", true);
    lv_obj_add_event_cb(p->songs, open_cb, LV_EVENT_CLICKED, (void *)ui_music_library_create);
    lv_obj_t *r = watch_theme_list_row(list, FLEX_ICON_MIC, 0xFF453A, "Grabaciones", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_recordings_create);
    r = watch_theme_list_row(list, FLEX_ICON_PHONE, FLEX_C_CAT_BLUE, "En el teléfono", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_phone_music_create);
    r = watch_theme_list_row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_SUN, "Guardar en", NULL, true);
    lv_obj_add_event_cb(r, open_cb, LV_EVENT_CLICKED, (void *)ui_dest_create);

    p->gen = 0xFFFFFFFFu;
    for (int v = 0; v < WM_VOL_N; v++) {
        p->rev[v] = 0xFFFFFFFFu;
    }
    home_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, home_tick_cb, HOME_TICK_MS, p);
    return page;
}
