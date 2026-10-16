/*
 * ui_wallpaper — Ajustes > Pantalla > Fondo: selector de fondos.
 *
 * Adaptación de la vista "Fondo de pantalla" de Flex OS Ultra
 * (FlexOS_Ultra_HomeCfg.h:557-575): rejilla de miniaturas con el nombre
 * debajo; la elegida lleva doble borde del acento y la insignia con la marca
 * de verificación arriba a la derecha.
 * Cambia: 3 columnas con miniaturas en la proporción del reloj (104x127),
 * la opción "Negro" (AMOLED: píxeles apagados) delante de los 8 fondos de la
 * referencia, y se aplica al tocar (el reloj solo tiene la esfera: no hay
 * "Inicio / Bloqueo / Ambos").
 *
 * Memoria: las 9 miniaturas se generan al abrir la página en un solo bloque
 * de PSRAM (232 KB) y se liberan al cerrarla.
 */
#include "ui_internal.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "misc/cache/lv_cache.h" /* lv_image_cache_drop (no está en lvgl.h) */
#include "watch_settings.h"
#include "watch_theme.h"

_Static_assert(FLEX_WALL_COUNT_ == WS_WALLPAPER_COUNT, "watch_settings y watch_theme no cuentan los mismos fondos");

#define COLS     3
#define CELL_W   128
#define THUMB_W  104
#define THUMB_H  127 /* 104 * 502 / 410 */
#define THUMB_R  12  /* radio de HC_PV (HomeCfg.h:566) */
#define NAME_GAP 6
#define ROW_STEP (THUMB_H + NAME_GAP + 23 + 14)
#define GRID_X0  ((FLEX_SCREEN_W - ((COLS - 1) * CELL_W + THUMB_W)) / 2 - (CELL_W - THUMB_W) / 2)
#define BADGE_D  22

typedef struct {
    uint16_t *px; /* FLEX_WALL_COUNT_ miniaturas seguidas (PSRAM) */
    lv_image_dsc_t dsc[FLEX_WALL_COUNT_];
    lv_obj_t *frame[FLEX_WALL_COUNT_];
    lv_obj_t *badge[FLEX_WALL_COUNT_];
} wp_page_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* Esquinas redondeadas pintadas en la miniatura (fuera del arco, negro: el
 * color de la página). Así LVGL no tiene que recortar con una máscara. */
static void round_corners(uint16_t *px)
{
    for (int y = 0; y < THUMB_R; y++) {
        const int dy = THUMB_R - y;
        for (int x = 0; x < THUMB_R; x++) {
            const int dx = THUMB_R - x;
            if (dx * dx + dy * dy > THUMB_R * THUMB_R) {
                px[y * THUMB_W + x] = 0;
                px[y * THUMB_W + (THUMB_W - 1 - x)] = 0;
                px[(THUMB_H - 1 - y) * THUMB_W + x] = 0;
                px[(THUMB_H - 1 - y) * THUMB_W + (THUMB_W - 1 - x)] = 0;
            }
        }
    }
}

static void show_selected(wp_page_t *p, flex_wall_t sel)
{
    for (int i = 0; i < FLEX_WALL_COUNT_; i++) {
        const bool on = (i == (int)sel);
        lv_obj_set_style_border_color(p->frame[i], flex_color(on ? FLEX_C_ACCENT : FLEX_C_BORDER), 0);
        lv_obj_set_style_border_width(p->frame[i], on ? 3 : 1, 0);
        if (on) {
            lv_obj_remove_flag(p->badge[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p->badge[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void pick_cb(lv_event_t *e)
{
    wp_page_t *p = lv_event_get_user_data(e);
    const flex_wall_t w = (flex_wall_t)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.wallpaper != (uint8_t)w) {
        ws.wallpaper = (uint8_t)w;
        watch_settings_update(&ws);
        watch_theme_set_wallpaper(w);
        ui_nav_replace_root(ui_face_create); /* la esfera, debajo, con el fondo nuevo */
    }
    show_selected(p, w);
}

static void free_cb(lv_event_t *e)
{
    wp_page_t *p = lv_event_get_user_data(e);
    for (int i = 0; i < FLEX_WALL_COUNT_; i++) {
        lv_image_cache_drop(&p->dsc[i]);
    }
    heap_caps_free(p->px);
    lv_free(p);
}

static lv_obj_t *cell(lv_obj_t *grid, wp_page_t *p, int i)
{
    const int32_t line = lv_font_get_line_height(FLEX_FONT_SMALL);
    lv_obj_t *c = lv_obj_create(grid);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, CELL_W, THUMB_H + NAME_GAP + line);
    lv_obj_set_pos(c, GRID_X0 + (i % COLS) * CELL_W, 6 + (i / COLS) * ROW_STEP);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(c, (void *)(intptr_t)i);
    lv_obj_add_event_cb(c, pick_cb, LV_EVENT_CLICKED, p);

    /* Marco: la miniatura con su borde (normal o elegido). */
    lv_obj_t *f = lv_obj_create(c);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, THUMB_W, THUMB_H);
    lv_obj_align(f, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(f, THUMB_R, 0);
    lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_border_post(f, true, 0); /* el borde, encima de la miniatura */
    lv_obj_set_style_bg_color(f, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (p->px) {
        uint16_t *px = p->px + (size_t)i * THUMB_W * THUMB_H;
        lv_image_dsc_t *d = &p->dsc[i];
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = LV_COLOR_FORMAT_RGB565;
        d->header.w = THUMB_W;
        d->header.h = THUMB_H;
        d->header.stride = THUMB_W * 2;
        d->data_size = THUMB_W * THUMB_H * 2;
        d->data = (const uint8_t *)px;
        lv_obj_t *img = lv_image_create(f);
        lv_image_set_src(img, d);
        lv_obj_set_pos(img, 0, 0);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }
    p->frame[i] = f;

    /* Insignia de "elegido" (HomeCfg.h:567-570): círculo del acento con la marca. */
    lv_obj_t *b = lv_obj_create(f);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BADGE_D, BADGE_D);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *chk = flex_icon_create(b, FLEX_ICON_CHECK, 14, FLEX_C_WHITE);
    lv_obj_center(chk);
    p->badge[i] = b;

    lv_obj_t *n = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT, watch_theme_wallpaper_name((flex_wall_t)i));
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(n, CELL_W, line);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, THUMB_H + NAME_GAP);
    return c;
}

lv_obj_t *ui_wallpaper_create(lv_obj_t *parent)
{
    wp_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->px = heap_caps_malloc((size_t)FLEX_WALL_COUNT_ * THUMB_W * THUMB_H * sizeof(uint16_t),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p->px) {
        for (int i = 0; i < FLEX_WALL_COUNT_; i++) {
            uint16_t *px = p->px + (size_t)i * THUMB_W * THUMB_H;
            watch_theme_wallpaper_thumb((flex_wall_t)i, px, THUMB_W, THUMB_H);
            round_corners(px);
        }
    }

    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Fondo de pantalla", back_cb, NULL);
    lv_obj_t *grid = watch_theme_list(page);
    lv_obj_set_layout(grid, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(grid, 0, 0);
    for (int i = 0; i < FLEX_WALL_COUNT_; i++) {
        cell(grid, p, i);
    }
    const int rows = (FLEX_WALL_COUNT_ + COLS - 1) / COLS;
    lv_obj_t *note = watch_theme_label(grid, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       p->px ? "Negro: los píxeles se apagan y gastan menos."
                                             : "Sin memoria para las miniaturas: el fondo se aplica igual.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(note, FLEX_SCREEN_W - 2 * FLEX_LIST_X - 32);
    lv_obj_set_pos(note, FLEX_LIST_X + 16, 6 + rows * ROW_STEP);

    show_selected(p, watch_theme_get_wallpaper());
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    return page;
}
