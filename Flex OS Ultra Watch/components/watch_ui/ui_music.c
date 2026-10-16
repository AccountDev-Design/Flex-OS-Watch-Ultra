/*
 * ui_music — app Música (IC_MUSICA de Flex OS Ultra) en el reloj (F9.10).
 *
 * La referencia reproduce su propia biblioteca (AppMusic.h). El reloj no la
 * tiene (sin decisión sobre el altavoz, D13): aquí Música es el mando de lo
 * que suena en el TELÉFONO, con su pantalla "Reproduciendo"
 * (AppMusic.h:603-700): título o "Nada sonando", artista, "Sonando" / "En
 * pausa" y los controles. Los datos son los de T_MEDIA_STATE y las órdenes
 * van por T_MEDIA_CMD (MediaBridge.kt). Sin teléfono, sin reproductor activo
 * o sin permiso, los controles se apagan y se dice por qué.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flex_app_icons.h"
#include "flex_icons.h"
#include "watch_phone.h"
#include "watch_theme.h"

#define MUS_TICK_MS 300
#define BTN_D       84
#define PLAY_D      104

typedef struct {
    lv_obj_t *src, *title, *artist, *state, *why;
    lv_obj_t *prev, *play, *next, *play_icon;
    bool playing;
} mus_page_t;

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

static void cmd_cb(lv_event_t *e)
{
    mus_page_t *p = lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    lv_obj_t *b = lv_event_get_current_target_obj(e);
    uint8_t cmd = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (b == p->play) {
        cmd = p->playing ? FLP_MCMD_PAUSE : FLP_MCMD_PLAY;
    }
    if (!watch_phone_media(cmd)) {
        set_text(p->why, "Requiere el teléfono conectado");
    }
}

static lv_obj_t *round_btn(lv_obj_t *parent, mus_page_t *p, int32_t d, flex_icon_t icon, uint8_t cmd,
                           lv_obj_t **icon_out)
{
    lv_obj_t *b = watch_theme_glass_button(parent, 0, 0, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *ic = flex_icon_create(b, icon, d / 3, FLEX_C_TXT);
    lv_obj_center(ic);
    lv_obj_set_user_data(b, p);
    lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
    if (icon_out) {
        *icon_out = ic;
    }
    return b;
}

static void enable(lv_obj_t *b, bool on)
{
    if (on) {
        lv_obj_remove_state(b, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(b, LV_STATE_DISABLED);
    }
}

static void refresh(mus_page_t *p)
{
    watch_phone_status_t s;
    watch_phone_get(&s);
    char t[96];
    const bool ready = s.link == FLP_LS_READY;
    const bool granted = s.caps.valid && (s.caps.granted & FLP_CAP_MEDIA);
    const bool have = ready && s.media.valid;
    if (have && s.media.app[0]) {
        ui_phone_text(s.media.app, t, sizeof(t), FLEX_FONT_SMALL);
        char src[128];
        snprintf(src, sizeof(src), "En el teléfono · %s", t);
        set_text(p->src, src);
    } else {
        set_text(p->src, "En el teléfono");
    }
    ui_phone_text(have ? s.media.title : "Nada sonando", t, sizeof(t), FLEX_FONT_TITLE);
    set_text(p->title, t);
    ui_phone_text(have ? s.media.artist : "", t, sizeof(t), FLEX_FONT_BODY);
    set_text(p->artist, t);
    p->playing = have && s.media.status == FLP_MEDIA_PLAYING;
    set_text(p->state, !have ? "" : (p->playing ? "Sonando" : (s.media.status == FLP_MEDIA_PAUSED ? "En pausa"
                                                                                                    : "Detenido")));
    flex_icon_set(p->play_icon, p->playing ? FLEX_ICON_PAUSE : FLEX_ICON_PLAY);
    const char *why = NULL;
    if (!ready) {
        why = s.bonded ? "Requiere el teléfono conectado" : "Empareja el teléfono en Flex Phone";
    } else if (s.caps.valid && !granted) {
        why = flexPhoneCapWhyNot(s.caps.supported, s.caps.granted, FLP_CAP_MEDIA);
    } else if (!s.media.valid) {
        why = "Pon música en el teléfono y aparecerá aquí";
    }
    set_text(p->why, why ? why : "");
    const bool can = ready && granted;
    enable(p->prev, can);
    enable(p->play, can);
    enable(p->next, can);
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static lv_obj_t *centered(lv_obj_t *col, const lv_font_t *font, uint32_t rgb, lv_label_long_mode_t mode)
{
    lv_obj_t *l = watch_theme_label(col, font, rgb, "");
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, mode);
    return l;
}

lv_obj_t *ui_music_create(lv_obj_t *parent)
{
    mus_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Música", back_cb, NULL);
    lv_obj_t *col = watch_theme_list(page);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 4, 0);

    lv_obj_t *art = lv_image_create(col);
    lv_image_set_src(art, flex_app_icon(FLEX_APP_MUSICA));
    p->src = centered(col, FLEX_FONT_SMALL, FLEX_C_TXT2, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_pad_top(p->src, 6, 0);
    p->title = centered(col, FLEX_FONT_TITLE, FLEX_C_TXT, LV_LABEL_LONG_MODE_DOTS);
    p->artist = centered(col, FLEX_FONT_BODY, FLEX_C_TXT2, LV_LABEL_LONG_MODE_DOTS);
    p->state = centered(col, FLEX_FONT_SMALL, FLEX_C_ACCENT, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), PLAY_D + 8);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    p->prev = round_btn(row, p, BTN_D, FLEX_ICON_CHEVRON_LEFT, FLP_MCMD_PREV, NULL);
    p->play = round_btn(row, p, PLAY_D, FLEX_ICON_PLAY, FLP_MCMD_PLAY, &p->play_icon);
    p->next = round_btn(row, p, BTN_D, FLEX_ICON_CHEVRON_RIGHT, FLP_MCMD_NEXT, NULL);
    p->why = centered(col, FLEX_FONT_SMALL, FLEX_C_MUTE, LV_LABEL_LONG_MODE_WRAP);

    refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, MUS_TICK_MS, p);
    return page;
}
