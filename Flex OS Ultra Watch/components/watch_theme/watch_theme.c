/*
 * watch_theme — ver include/watch_theme.h
 */
#include "watch_theme.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "flex_glass_math.h"
#include "flex_wallpaper.h"

static const char *TAG = "watch_theme";

static lv_style_t s_card;
static lv_style_t s_pressed;
static lv_style_t s_checked;
static lv_style_t s_disabled;
static lv_style_t s_row;
static lv_style_t s_tile;
static lv_style_t s_info;
static lv_style_t s_list;
static bool s_inited;

/* Hijos de una fila, en este orden (watch_theme_list_row) */
enum { ROW_ICON = 0, ROW_TITLE, ROW_VALUE };

static uint16_t *s_wall_px;
static lv_image_dsc_t s_wall_dsc;
static flex_wall_t s_wall = FLEX_WALL_BLACK;
static lv_opa_t s_flat_mix; /* tinte del vidrio sobre el negro de la página */

static lv_color_t mix(uint32_t fg, uint32_t bg, uint8_t a)
{
    return lv_color_hex(flex_mix_rgb(fg, bg, a));
}

static uint16_t rgb_to_565(uint32_t rgb)
{
    return flex_rgb565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void watch_theme_init(void)
{
    if (s_inited) {
        return;
    }

    /* Vidrio sobre negro AMOLED. El fondo de página es negro puro y
     * conocido, así que el tinte adaptativo (glassTintMix de la referencia)
     * se precalcula una vez: las superficies son OPACAS y LVGL no mezcla
     * píxel a píxel en cada cuadro (las pantallas de lista y el panel rápido
     * se redibujan enteros al desplazarse o al seguir el dedo). Los bordes de
     * luz arriba/izquierda también se precalculan sobre la superficie. La capa
     * de brillo con degradado (glassShadeRow) desaparece: sobre negro apenas
     * se distinguía y era la parte más cara de cada tarjeta. */
    s_flat_mix =
        flex_glass_tint_mix((uint32_t)flex_glass_luma565(rgb_to_565(FLEX_C_PAGE)), 1, rgb_to_565(FLEX_C_GLASS));
    const uint32_t surf = flex_mix_rgb(FLEX_C_GLASS, FLEX_C_PAGE, s_flat_mix);
    const lv_color_t edge_weak = mix(FLEX_GLASS_EDGE_HI, surf, FLEX_GLASS_EDGE_WEAK);
    const lv_color_t edge_strong = mix(FLEX_GLASS_EDGE_HI, surf, FLEX_GLASS_EDGE_STRONG);

    /* Tarjeta: radio de tarjeta y reflejo claro arriba/izquierda con el peso
     * fuerte del material (luz desde arriba a la izquierda). */
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, lv_color_hex(surf));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, FLEX_RADIUS_CARD);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_side(&s_card, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_card, edge_strong);
    lv_style_set_border_opa(&s_card, LV_OPA_COVER);
    lv_style_set_pad_all(&s_card, 18);
    lv_style_set_pad_row(&s_card, 4);

    /* Estados de los controles (ver FLEX_GLASS_* en flex_tokens.h), también
     * precalculados sobre negro. */
    lv_style_init(&s_pressed);
    lv_style_set_bg_color(&s_pressed, mix(FLEX_C_GLASS_PRESSED, FLEX_C_PAGE, FLEX_GLASS_PRESSED_MIX));
    lv_style_set_bg_opa(&s_pressed, LV_OPA_COVER);
    lv_style_init(&s_checked);
    lv_style_set_bg_color(&s_checked, mix(FLEX_C_ACCENT, FLEX_C_PAGE, FLEX_GLASS_ACTIVE_MIX));
    lv_style_set_bg_opa(&s_checked, LV_OPA_COVER);
    lv_style_init(&s_disabled);
    const uint32_t dis = flex_mix_rgb(FLEX_C_GLASS, FLEX_C_PAGE, FLEX_GLASS_DISABLED_MIX);
    lv_style_set_bg_color(&s_disabled, lv_color_hex(dis));
    lv_style_set_bg_opa(&s_disabled, LV_OPA_COVER);
    lv_style_set_border_color(&s_disabled, mix(FLEX_GLASS_EDGE_HI, dis, FLEX_GLASS_EDGE_WEAK / 2));

    /* Fila: vidrio plano sobre el fondo de página (drawGlassCardFlat de la
     * referencia), sin capa de brillo: una lista con desplazamiento redibuja
     * las filas en cada cuadro y así cada una es un solo rectángulo. */
    lv_style_init(&s_row);
    lv_style_set_bg_color(&s_row, lv_color_hex(surf));
    lv_style_set_bg_opa(&s_row, LV_OPA_COVER);
    lv_style_set_radius(&s_row, FLEX_RADIUS_ROW);
    lv_style_set_border_width(&s_row, 1);
    lv_style_set_border_side(&s_row, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_row, edge_weak);
    lv_style_set_border_opa(&s_row, LV_OPA_COVER);
    lv_style_set_pad_left(&s_row, 16);
    lv_style_set_pad_right(&s_row, 14);
    lv_style_set_pad_column(&s_row, 14);
    lv_style_set_layout(&s_row, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&s_row, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&s_row, LV_FLEX_ALIGN_CENTER);

    /* Mosaico del panel rápido: el mismo vidrio plano que las filas, con el
     * radio de las tarjetas grandes del panel (QuickPanel.h:105). */
    lv_style_init(&s_tile);
    lv_style_set_bg_color(&s_tile, lv_color_hex(surf));
    lv_style_set_bg_opa(&s_tile, LV_OPA_COVER);
    lv_style_set_radius(&s_tile, FLEX_RADIUS_CARD);
    lv_style_set_border_width(&s_tile, 1);
    lv_style_set_border_side(&s_tile, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_tile, edge_weak);
    lv_style_set_border_opa(&s_tile, LV_OPA_COVER);
    lv_style_set_pad_all(&s_tile, 16);

    /* Fila de información: el mismo vidrio que las filas, más baja. */
    lv_style_init(&s_info);
    lv_style_set_bg_color(&s_info, lv_color_hex(surf));
    lv_style_set_bg_opa(&s_info, LV_OPA_COVER);
    lv_style_set_radius(&s_info, FLEX_RADIUS_ROW);
    lv_style_set_pad_left(&s_info, 16);
    lv_style_set_pad_right(&s_info, 16);
    lv_style_set_pad_column(&s_info, 12);
    lv_style_set_layout(&s_info, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_info, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&s_info, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&s_info, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&s_list);
    lv_style_set_pad_left(&s_list, FLEX_LIST_X);
    lv_style_set_pad_right(&s_list, FLEX_LIST_X);
    lv_style_set_pad_top(&s_list, 4);
    lv_style_set_pad_bottom(&s_list, 28);
    lv_style_set_pad_row(&s_list, FLEX_ROW_GAP);
    lv_style_set_layout(&s_list, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_list, LV_FLEX_FLOW_COLUMN);

    s_inited = true;
}

static void add_states(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &s_checked, LV_STATE_CHECKED);
    lv_obj_add_style(obj, &s_disabled, LV_STATE_DISABLED);
}

void watch_theme_set_wallpaper(flex_wall_t w)
{
    s_wall = w;
}

flex_wall_t watch_theme_get_wallpaper(void)
{
    return s_wall;
}

static bool ensure_onyx(void)
{
    if (s_wall_px) {
        return true;
    }
    const size_t bytes = (size_t)FLEX_SCREEN_W * FLEX_SCREEN_H * sizeof(uint16_t);
    s_wall_px = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_wall_px) {
        ESP_LOGW(TAG, "sin PSRAM para el fondo Onyx (%u B): fondo negro", (unsigned)bytes);
        return false;
    }
    flex_wallpaper_onyx(s_wall_px, FLEX_SCREEN_W, FLEX_SCREEN_H);
    s_wall_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_wall_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_wall_dsc.header.w = FLEX_SCREEN_W;
    s_wall_dsc.header.h = FLEX_SCREEN_H;
    s_wall_dsc.header.stride = FLEX_SCREEN_W * sizeof(uint16_t);
    s_wall_dsc.data_size = bytes;
    s_wall_dsc.data = (const uint8_t *)s_wall_px;
    ESP_LOGI(TAG, "fondo Onyx generado en PSRAM (%u B)", (unsigned)bytes);
    return true;
}

static bool onyx_active(void)
{
    return s_wall == FLEX_WALL_ONYX && s_wall_px;
}

bool watch_theme_apply_background(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    if (s_wall != FLEX_WALL_ONYX || !ensure_onyx()) {
        return false;
    }
    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, &s_wall_dsc);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

lv_obj_t *watch_theme_glass_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    watch_theme_init();
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_card, 0);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    if (onyx_active()) {
        /* Sobre Onyx: vidrio translúcido con el tinte adaptativo según la
         * luma real del fondo que queda debajo (glassTintMix). */
        uint32_t sum = 0;
        int n = 0;
        flex_glass_region_luma(s_wall_px, FLEX_SCREEN_W, FLEX_SCREEN_H, x, y, w, h, &sum, &n);
        lv_obj_set_style_bg_color(card, flex_color(FLEX_C_GLASS), 0);
        lv_obj_set_style_bg_opa(card, flex_glass_tint_mix(sum, n, rgb_to_565(FLEX_C_GLASS)), 0);
        lv_obj_set_style_border_color(card, flex_color(FLEX_GLASS_EDGE_HI), 0);
        lv_obj_set_style_border_opa(card, FLEX_GLASS_EDGE_STRONG, 0);
    }
    return card;
}

void watch_theme_glass_set_min_tint(lv_obj_t *card, lv_opa_t min_mix)
{
    if (lv_obj_get_style_bg_opa(card, 0) < LV_OPA_COVER) {
        /* translúcida (Onyx): sube el alfa */
        if (lv_obj_get_style_bg_opa(card, 0) < min_mix) {
            lv_obj_set_style_bg_opa(card, min_mix, 0);
        }
        return;
    }
    /* opaca (negro): el mismo vidrio con un tinte mayor, como superficie elevada */
    if (min_mix > s_flat_mix) {
        lv_obj_set_style_bg_color(card, mix(FLEX_C_GLASS, FLEX_C_PAGE, min_mix), 0);
    }
}

lv_obj_t *watch_theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, flex_color(rgb), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *watch_theme_glass_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    watch_theme_init();
    lv_obj_t *b = watch_theme_glass_card(parent, x, y, w, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    add_states(b);
    return b;
}

void watch_theme_page(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, flex_color(FLEX_C_PAGE), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

lv_obj_t *watch_theme_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb, void *user_data)
{
    watch_theme_init();
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, FLEX_SCREEN_W, FLEX_HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    int32_t tx = FLEX_SAFE_X;
    if (back_cb) {
        /* Zona táctil completa con el glifo centrado (AppFramework.h:394):
         * el dedo acierta aunque no toque el chevron. */
        lv_obj_t *zone = lv_obj_create(hdr);
        lv_obj_remove_style_all(zone);
        lv_obj_set_size(zone, FLEX_HDR_ZONE, FLEX_HDR_ZONE);
        lv_obj_set_pos(zone, FLEX_HDR_X, FLEX_SAFE_TOP);
        lv_obj_set_style_radius(zone, FLEX_HDR_ZONE / 2, 0);
        lv_obj_add_style(zone, &s_pressed, LV_STATE_PRESSED);
        lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(zone, back_cb, LV_EVENT_CLICKED, user_data);
        lv_obj_t *chev = flex_icon_create(zone, FLEX_ICON_CHEVRON_LEFT, 26, FLEX_C_TXT);
        lv_obj_center(chev);
        tx = FLEX_HDR_X + FLEX_HDR_ZONE + FLEX_HDR_GAP;
    }
    if (title) {
        lv_obj_t *l = watch_theme_label(hdr, FLEX_FONT_TITLE, FLEX_C_TXT, title);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, FLEX_SCREEN_W - tx - FLEX_SAFE_X);
        lv_obj_set_pos(l, tx, FLEX_SAFE_TOP + (FLEX_HDR_ZONE - lv_font_get_line_height(FLEX_FONT_TITLE)) / 2);
    }
    return hdr;
}

lv_obj_t *watch_theme_list(lv_obj_t *parent)
{
    watch_theme_init();
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list, &s_list, 0);
    lv_obj_set_pos(list, 0, FLEX_HDR_H);
    lv_obj_set_size(list, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    /* Pulsable a propósito: LVGL solo desplaza desde un objeto pulsable, y
     * las filas de información y los títulos de sección no lo son. */
    lv_obj_add_flag(list, LV_OBJ_FLAG_CLICKABLE);
    return list;
}

lv_obj_t *watch_theme_list_row(lv_obj_t *parent, flex_icon_t icon, uint32_t accent, const char *title,
                               const char *value, bool chevron)
{
    watch_theme_init();
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_row, 0);
    add_states(row);
    lv_obj_set_size(row, LV_PCT(100), FLEX_ROW_H);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    flex_icon_create(row, icon, FLEX_ROW_ICON, accent); /* ROW_ICON */

    /* Una sola línea: en LVGL 9 los "..." solo actúan con la altura fija. */
    lv_obj_t *t = watch_theme_label(row, FLEX_FONT_BODY, FLEX_C_TXT, title ? title : ""); /* ROW_TITLE */
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t, lv_font_get_line_height(FLEX_FONT_BODY));
    lv_obj_set_flex_grow(t, 1);

    lv_obj_t *v = watch_theme_label(row, FLEX_FONT_SMALL, FLEX_C_TXT2, value ? value : ""); /* ROW_VALUE */
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(v, lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_obj_set_style_max_width(v, 170, 0);
    if (!value) {
        lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    }

    if (chevron) {
        flex_icon_create(row, FLEX_ICON_CHEVRON_RIGHT, 18, FLEX_C_MUTE);
    }
    return row;
}

void watch_theme_row_set_value(lv_obj_t *row, const char *value)
{
    lv_obj_t *v = lv_obj_get_child(row, ROW_VALUE);
    if (!v) {
        return;
    }
    if (!value) {
        lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strcmp(lv_label_get_text(v), value) != 0) {
        lv_label_set_text(v, value);
    }
    lv_obj_remove_flag(v, LV_OBJ_FLAG_HIDDEN);
}

void watch_theme_row_set_pending(lv_obj_t *row)
{
    lv_obj_add_state(row, LV_STATE_DISABLED);
    flex_icon_set_color(lv_obj_get_child(row, ROW_ICON), FLEX_C_MUTE);
    lv_obj_set_style_text_color(lv_obj_get_child(row, ROW_TITLE), flex_color(FLEX_C_MUTE), 0);
    watch_theme_row_set_value(row, "Pendiente");
}

lv_obj_t *watch_theme_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    watch_theme_init();
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_add_style(t, &s_tile, 0);
    add_states(t);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_size(t, w, h);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    return t;
}

lv_obj_t *watch_theme_section(lv_obj_t *list, const char *text)
{
    watch_theme_init();
    lv_obj_t *l = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_left(l, 6, 0);
    lv_obj_set_style_pad_top(l, 10, 0);
    return l;
}

lv_obj_t *watch_theme_info_row(lv_obj_t *list, const char *title, const char *value)
{
    watch_theme_init();
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_info, 0);
    lv_obj_set_size(row, LV_PCT(100), FLEX_INFO_ROW_H);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *t = watch_theme_label(row, FLEX_FONT_SMALL, FLEX_C_TXT2, title ? title : "");
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t, lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_obj_set_flex_grow(t, 1);
    /* El valor puede ocupar dos líneas (caben en FLEX_INFO_ROW_H); más, con "...". */
    lv_obj_t *v = watch_theme_label(row, FLEX_FONT_SMALL, FLEX_C_TXT, value ? value : "");
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(v, 230, 0);
    lv_obj_set_style_max_height(v, 2 * lv_font_get_line_height(FLEX_FONT_SMALL), 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    return row;
}

void watch_theme_info_set(lv_obj_t *row, const char *value)
{
    lv_obj_t *v = lv_obj_get_child(row, 1);
    if (v && strcmp(lv_label_get_text(v), value ? value : "") != 0) {
        lv_label_set_text(v, value ? value : "");
    }
}
