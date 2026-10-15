/*
 * watch_theme — ver include/watch_theme.h
 */
#include "watch_theme.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "flex_glass_math.h"
#include "flex_wallpaper.h"

static const char *TAG = "watch_theme";

static lv_style_t s_card;
static lv_style_t s_sheen;
static lv_grad_dsc_t s_sheen_grad;
static bool s_inited;

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

    s_inited = true;
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

lv_obj_t *watch_theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, flex_color(rgb), 0);
    lv_label_set_text(l, text);
    return l;
}
