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
static lv_style_t s_sheen;
static lv_grad_dsc_t s_sheen_grad;
static lv_style_t s_pressed;
static lv_style_t s_checked;
static lv_style_t s_disabled;
static lv_style_t s_row;
static lv_style_t s_list;
static bool s_inited;

/* Hijos de una fila, en este orden (watch_theme_list_row) */
enum { ROW_ICON = 0, ROW_TITLE, ROW_VALUE };

static uint16_t *s_wall_px;
static lv_image_dsc_t s_wall_dsc;

static uint16_t rgb_to_565(uint32_t rgb)
{
    return flex_rgb565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void watch_theme_init(void)
{
    if (s_inited) {
        return;
    }

    /* Tarjeta: tinte "glass" (el alfa se calcula por tarjeta con la mezcla
     * adaptativa), radio de tarjeta y reflejo claro arriba/izquierda con el
     * peso fuerte del material (luz desde arriba a la izquierda). */
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, flex_color(FLEX_C_GLASS));
    lv_style_set_bg_opa(&s_card, FLEX_GLASS_TINT_BASE);
    lv_style_set_radius(&s_card, FLEX_RADIUS_CARD);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_side(&s_card, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_card, flex_color(FLEX_GLASS_EDGE_HI));
    lv_style_set_border_opa(&s_card, FLEX_GLASS_EDGE_STRONG);
    lv_style_set_pad_all(&s_card, 18);
    lv_style_set_pad_row(&s_card, 4);

    /* Capa de brillo y sombra por filas, como glassShadeRow(): blanco con
     * alfa SPECULAR en la fila 0 que cae a 0 al 45 %, y negro desde 0 al 45 %
     * hasta SHADE abajo. Más la sombra del borde abajo/derecha. */
    s_sheen_grad.dir = LV_GRAD_DIR_VER;
    s_sheen_grad.stops_count = 4;
    s_sheen_grad.stops[0].color = flex_color(FLEX_C_WHITE);
    s_sheen_grad.stops[0].opa = FLEX_GLASS_SPECULAR;
    s_sheen_grad.stops[0].frac = 0;
    s_sheen_grad.stops[1].color = flex_color(FLEX_C_WHITE);
    s_sheen_grad.stops[1].opa = LV_OPA_TRANSP;
    s_sheen_grad.stops[1].frac = 115; /* 45 % */
    s_sheen_grad.stops[2].color = flex_color(FLEX_C_BLACK);
    s_sheen_grad.stops[2].opa = LV_OPA_TRANSP;
    s_sheen_grad.stops[2].frac = 116;
    s_sheen_grad.stops[3].color = flex_color(FLEX_C_BLACK);
    s_sheen_grad.stops[3].opa = FLEX_GLASS_SHADE;
    s_sheen_grad.stops[3].frac = 255;

    lv_style_init(&s_sheen);
    lv_style_set_bg_opa(&s_sheen, LV_OPA_COVER);
    lv_style_set_bg_grad(&s_sheen, &s_sheen_grad);
    lv_style_set_radius(&s_sheen, FLEX_RADIUS_CARD);
    lv_style_set_border_width(&s_sheen, 1);
    lv_style_set_border_side(&s_sheen, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT);
    lv_style_set_border_color(&s_sheen, flex_color(FLEX_GLASS_EDGE_LO));
    lv_style_set_border_opa(&s_sheen, FLEX_GLASS_EDGE_STRONG);
    lv_style_set_pad_all(&s_sheen, 0);

    /* Estados de los controles (ver FLEX_GLASS_* en flex_tokens.h). */
    lv_style_init(&s_pressed);
    lv_style_set_bg_color(&s_pressed, flex_color(FLEX_C_GLASS_PRESSED));
    lv_style_set_bg_opa(&s_pressed, FLEX_GLASS_PRESSED_MIX);
    lv_style_init(&s_checked);
    lv_style_set_bg_color(&s_checked, flex_color(FLEX_C_ACCENT));
    lv_style_set_bg_opa(&s_checked, FLEX_GLASS_ACTIVE_MIX);
    lv_style_init(&s_disabled);
    lv_style_set_bg_opa(&s_disabled, FLEX_GLASS_DISABLED_MIX);
    lv_style_set_border_opa(&s_disabled, FLEX_GLASS_EDGE_WEAK / 2);

    /* Fila: vidrio plano sobre el fondo de página (drawGlassCardFlat de la
     * referencia), sin capa de brillo: una lista con desplazamiento redibuja
     * las filas en cada cuadro y así cada una es un solo rectángulo. */
    lv_style_init(&s_row);
    lv_style_set_bg_color(&s_row, flex_color(FLEX_C_GLASS));
    lv_style_set_bg_opa(&s_row, flex_glass_tint_mix((uint32_t)flex_glass_luma565(rgb_to_565(FLEX_C_PAGE)), 1,
                                                    rgb_to_565(FLEX_C_GLASS)));
    lv_style_set_radius(&s_row, FLEX_RADIUS_ROW);
    lv_style_set_border_width(&s_row, 1);
    lv_style_set_border_side(&s_row, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_row, flex_color(FLEX_GLASS_EDGE_HI));
    lv_style_set_border_opa(&s_row, FLEX_GLASS_EDGE_WEAK);
    lv_style_set_pad_left(&s_row, 16);
    lv_style_set_pad_right(&s_row, 14);
    lv_style_set_pad_column(&s_row, 14);
    lv_style_set_layout(&s_row, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&s_row, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&s_row, LV_FLEX_ALIGN_CENTER);

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

bool watch_theme_apply_background(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    if (!s_wall_px) {
        const size_t bytes = (size_t)FLEX_SCREEN_W * FLEX_SCREEN_H * sizeof(uint16_t);
        s_wall_px = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_wall_px) {
            ESP_LOGW(TAG, "sin PSRAM para el fondo (%u B): fondo negro liso", (unsigned)bytes);
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
    }

    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, &s_wall_dsc);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

lv_obj_t *watch_theme_glass_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_card, 0);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Tinte adaptativo según la luma real del fondo que queda debajo. */
    uint32_t sum = 0;
    int n = 0;
    if (s_wall_px) {
        flex_glass_region_luma(s_wall_px, FLEX_SCREEN_W, FLEX_SCREEN_H, x, y, w, h, &sum, &n);
    } else {
        n = 1; /* fondo negro liso: luma 0 */
    }
    const uint8_t mix = flex_glass_tint_mix(sum, n, rgb_to_565(FLEX_C_GLASS));
    lv_obj_set_style_bg_opa(card, mix, 0);

    /* La capa de brillo cubre la tarjeta entera, fuera del relleno. */
    lv_obj_t *sheen = lv_obj_create(card);
    lv_obj_remove_style_all(sheen);
    lv_obj_add_style(sheen, &s_sheen, 0);
    lv_obj_add_flag(sheen, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(sheen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sheen, w, h);
    lv_obj_set_pos(sheen, -lv_obj_get_style_pad_left(card, 0), -lv_obj_get_style_pad_top(card, 0));
    return card;
}

void watch_theme_glass_set_min_tint(lv_obj_t *card, lv_opa_t min_mix)
{
    if (lv_obj_get_style_bg_opa(card, 0) < min_mix) {
        lv_obj_set_style_bg_opa(card, min_mix, 0);
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
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list, &s_list, 0);
    lv_obj_set_pos(list, 0, FLEX_HDR_H);
    lv_obj_set_size(list, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_CLICKABLE);
    return list;
}

lv_obj_t *watch_theme_list_row(lv_obj_t *parent, flex_icon_t icon, uint32_t accent, const char *title,
                               const char *value, bool chevron)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_row, 0);
    add_states(row);
    lv_obj_set_size(row, LV_PCT(100), FLEX_ROW_H);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    flex_icon_create(row, icon, FLEX_ROW_ICON, accent); /* ROW_ICON */

    lv_obj_t *t = watch_theme_label(row, FLEX_FONT_BODY, FLEX_C_TXT, title ? title : ""); /* ROW_TITLE */
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(t, 1);

    lv_obj_t *v = watch_theme_label(row, FLEX_FONT_SMALL, FLEX_C_TXT2, value ? value : ""); /* ROW_VALUE */
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
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
