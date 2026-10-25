/*
 * watch_theme — ver include/watch_theme.h
 */
#include "watch_theme.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_blur.h"
#include "flex_glass_math.h"
#include "flex_jpeg.h"
#include "flex_safe.h"
#include "flex_wallpaper.h"
#include "misc/cache/lv_cache.h" /* lv_image_cache_drop (no está en lvgl.h) */

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

#define HDR_CHEV 26 /* chevron de "volver" */

/* Fondos en PSRAM (402 KB cada uno), con una clave que dice qué contienen:
 *  - s_bg: el de la esfera activa (un fondo de pantalla o un fondo propio de
 *    esfera fotográfica). Se reescribe al cambiar y se libera con el negro;
 *  - s_prev: el de las esferas temporales del selector (vistas previas), solo
 *    mientras dura la generación (watch_theme_preview_begin/end);
 *  - s_blur: s_bg desenfocado para el cajón (watch_theme_blur_*). */
#define BG_BYTES  ((size_t)FLEX_SCREEN_W * FLEX_SCREEN_H * sizeof(uint16_t))
#define KEY_NONE  0u
#define KEY_IMAGE 0x100u /* | flex_bg_t; un fondo de pantalla es su flex_wall_t */
typedef struct {
    uint16_t *px;
    uint16_t key;
    lv_image_dsc_t dsc;
} bgbuf_t;
static bgbuf_t s_bg, s_prev;
static bool s_preview;
static flex_wall_t s_wall = FLEX_WALL_BLACK;

static struct {
    bgbuf_t out;
    uint16_t src_key; /* clave de s_bg que se está desenfocando o ya desenfocada */
    bool ready;
    flex_blur_t job;
    uint8_t *small, *tmp;
    int holds;          /* cajones vivos que muestran s_blur.out: no se libera mientras tanto */
    uint16_t failed_key; /* fondo para el que no hubo PSRAM: no se reintenta en bucle */
} s_blur;
#define BLUR_DIM 165 /* brillo del fondo desenfocado: los iconos y sus bordes se leen encima */

/* flex_wall_t (valor guardado en los ajustes) -> fondo de la referencia */
static const flex_wp_t kWallToWp[FLEX_WALL_COUNT_] = {
    [FLEX_WALL_ONYX] = FLEX_WP_ONYX,         [FLEX_WALL_FLEX_ORIGINAL] = FLEX_WP_FLEX_ORIGINAL,
    [FLEX_WALL_AURORA] = FLEX_WP_AURORA,     [FLEX_WALL_NOCTURNO] = FLEX_WP_NOCTURNO,
    [FLEX_WALL_HALO] = FLEX_WP_HALO,         [FLEX_WALL_OCEANO] = FLEX_WP_OCEANO,
    [FLEX_WALL_VIOLETA] = FLEX_WP_VIOLETA,   [FLEX_WALL_NATURALEZA] = FLEX_WP_NATURALEZA,
};
static lv_opa_t s_flat_mix; /* tinte del vidrio sobre el negro de la página */
static void lg_init(void);

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
    lv_style_set_pad_bottom(&s_list, 12);
    lv_style_set_pad_row(&s_list, FLEX_ROW_GAP);
    lv_style_set_layout(&s_list, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_list, LV_FLEX_FLOW_COLUMN);

    s_inited = true;
    lg_init(); /* estilos del rediseño: también al arrancar (sus propiedades viven en el montón) */
}

static void add_states(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &s_checked, LV_STATE_CHECKED);
    lv_obj_add_style(obj, &s_disabled, LV_STATE_DISABLED);
}

void watch_theme_set_wallpaper(flex_wall_t w)
{
    s_wall = ((unsigned)w < FLEX_WALL_COUNT_) ? w : FLEX_WALL_BLACK;
}

flex_wall_t watch_theme_get_wallpaper(void)
{
    return s_wall;
}

const char *watch_theme_wallpaper_name(flex_wall_t w)
{
    if (w == FLEX_WALL_BLACK || (unsigned)w >= FLEX_WALL_COUNT_) {
        return "Negro";
    }
    return flex_wallpaper_name(kWallToWp[w]);
}

void watch_theme_wallpaper_thumb(flex_wall_t w, uint16_t *buf, int tw, int th)
{
    if (w == FLEX_WALL_BLACK || (unsigned)w >= FLEX_WALL_COUNT_) {
        memset(buf, 0, (size_t)tw * (size_t)th * sizeof(uint16_t));
        return;
    }
    flex_wallpaper_render(kWallToWp[w], buf, tw, th);
}

/* ---- Búferes de fondo ---- */

static void bg_set_dsc(bgbuf_t *b)
{
    b->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    b->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    b->dsc.header.w = FLEX_SCREEN_W;
    b->dsc.header.h = FLEX_SCREEN_H;
    b->dsc.header.stride = FLEX_SCREEN_W * sizeof(uint16_t);
    b->dsc.data_size = BG_BYTES;
    b->dsc.data = (const uint8_t *)b->px;
}

/* Libera un fondo. El de la esfera solo se suelta mientras se construye la
 * esfera nueva: la vieja, que aún apunta al búfer, se borra en la misma
 * llamada sin dibujarse (ui_nav_replace_root), así que nadie lo lee después. */
static void bg_drop(bgbuf_t *b)
{
    if (b->px) {
        lv_image_cache_drop(&b->dsc);
        heap_caps_free(b->px);
        b->px = NULL;
        b->key = KEY_NONE;
    }
}

extern const uint8_t _binary_face_montana_jpg_start[], _binary_face_montana_jpg_end[];
extern const uint8_t _binary_face_analogica_jpg_start[], _binary_face_analogica_jpg_end[];
extern const uint8_t _binary_face_gato_jpg_start[], _binary_face_gato_jpg_end[];
extern const uint8_t _binary_face_tierra_jpg_start[], _binary_face_tierra_jpg_end[];
extern const uint8_t _binary_face_flores_jpg_start[], _binary_face_flores_jpg_end[];
extern const uint8_t _binary_face_taj_jpg_start[], _binary_face_taj_jpg_end[];

static const struct {
    const char *name;
    const uint8_t *start, *end;
} kImages[FLEX_BG_COUNT_] = {
    [FLEX_BG_MONTANA] = {"montana", _binary_face_montana_jpg_start, _binary_face_montana_jpg_end},
    [FLEX_BG_ANALOGICA] = {"analogica", _binary_face_analogica_jpg_start, _binary_face_analogica_jpg_end},
    [FLEX_BG_GATO] = {"gato", _binary_face_gato_jpg_start, _binary_face_gato_jpg_end},
    [FLEX_BG_TIERRA] = {"tierra", _binary_face_tierra_jpg_start, _binary_face_tierra_jpg_end},
    [FLEX_BG_FLORES] = {"flores", _binary_face_flores_jpg_start, _binary_face_flores_jpg_end},
    [FLEX_BG_TAJ] = {"taj", _binary_face_taj_jpg_start, _binary_face_taj_jpg_end},
};

const uint8_t *watch_theme_image_jpeg(flex_bg_t bg, uint32_t *len)
{
    if (bg <= FLEX_BG_NONE || bg >= FLEX_BG_COUNT_) {
        *len = 0;
        return NULL;
    }
    *len = (uint32_t)(kImages[bg].end - kImages[bg].start);
    return kImages[bg].start;
}

/* Deja en b el fondo key (generado o decodificado). false con KEY_NONE, sin
 * PSRAM o si el JPEG no se puede decodificar (queda negro y se registra). */
static bool bg_ensure(bgbuf_t *b, uint16_t key)
{
    if (key == KEY_NONE) {
        bg_drop(b);
        return false;
    }
    if (!b->px) {
        b->px = heap_caps_malloc(BG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!b->px) {
            ESP_LOGW(TAG, "sin PSRAM para el fondo (%u B): fondo negro", (unsigned)BG_BYTES);
            return false;
        }
        b->key = KEY_NONE;
    }
    if (b->key != key) {
        const int64_t t0 = esp_timer_get_time();
        lv_image_cache_drop(&b->dsc); /* mismo descriptor, contenido nuevo */
        if (key & KEY_IMAGE) {
            const flex_bg_t id = (flex_bg_t)(key & 0xFF);
            uint32_t n;
            const uint8_t *jpg = watch_theme_image_jpeg(id, &n);
            const int r = flex_jpeg_draw(jpg, n, b->px, FLEX_SCREEN_W, FLEX_SCREEN_H, 0, 0, FLEX_SCREEN_W,
                                         FLEX_SCREEN_H);
            if (r != 0) {
                ESP_LOGW(TAG, "fondo \"%s\": JPEG no válido (%d): fondo negro", kImages[id].name, r);
                bg_drop(b);
                return false;
            }
            ESP_LOGI(TAG, "fondo \"%s\" decodificado en PSRAM (%u B de JPEG) en %d ms", kImages[id].name,
                     (unsigned)n, (int)((esp_timer_get_time() - t0) / 1000));
        } else {
            const flex_wall_t w = (flex_wall_t)key;
            flex_wallpaper_render(kWallToWp[w], b->px, FLEX_SCREEN_W, FLEX_SCREEN_H);
            ESP_LOGI(TAG, "fondo \"%s\" generado en PSRAM (%u B) en %d ms", watch_theme_wallpaper_name(w),
                     (unsigned)BG_BYTES, (int)((esp_timer_get_time() - t0) / 1000));
        }
        b->key = key;
    }
    bg_set_dsc(b);
    return true;
}

/* El búfer en el que se construye ahora (la esfera activa o una vista previa). */
static bgbuf_t *bg_target(void)
{
    return s_preview ? &s_prev : &s_bg;
}

static bool wall_active(void)
{
    const bgbuf_t *b = bg_target();
    return b->px && b->key != KEY_NONE;
}

bool watch_theme_wall_is_light(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!wall_active()) {
        return false;
    }
    return flex_wallpaper_region_luma(bg_target()->px, FLEX_SCREEN_W, FLEX_SCREEN_H, x, y, w, h) >
           FLEX_WALL_LIGHT_LUMA;
}

static bool apply_key(lv_obj_t *screen, uint16_t key)
{
    lv_obj_set_style_bg_color(screen, flex_color(FLEX_C_BLACK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    bgbuf_t *b = bg_target();
    if (!bg_ensure(b, key)) {
        return false;
    }
    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, &b->dsc);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

bool watch_theme_apply_background(lv_obj_t *screen)
{
    return apply_key(screen, s_wall == FLEX_WALL_BLACK ? KEY_NONE : (uint16_t)s_wall);
}

bool watch_theme_apply_image(lv_obj_t *screen, flex_bg_t bg)
{
    if (bg <= FLEX_BG_NONE || bg >= FLEX_BG_COUNT_) {
        return apply_key(screen, KEY_NONE);
    }
    return apply_key(screen, (uint16_t)(KEY_IMAGE | bg));
}

void watch_theme_preview_begin(void)
{
    s_preview = true;
}

void watch_theme_preview_end(void)
{
    s_preview = false;
}

void watch_theme_preview_release(void)
{
    s_preview = false;
    bg_drop(&s_prev);
}

/* ---- Desenfoque para el cajón ---- */

static void blur_free_work(void)
{
    heap_caps_free(s_blur.small);
    heap_caps_free(s_blur.tmp);
    s_blur.small = NULL;
    s_blur.tmp = NULL;
}

/* Suelta el desenfoque si ningún cajón lo está mostrando (si no, solo su
 * memoria de trabajo: el cajón oculto apunta a la imagen). */
static bool blur_drop(void)
{
    blur_free_work();
    if (s_blur.holds > 0) {
        return false;
    }
    bg_drop(&s_blur.out);
    s_blur.ready = false;
    s_blur.src_key = KEY_NONE;
    return true;
}

void watch_theme_blur_hold(bool on)
{
    s_blur.holds += on ? 1 : -1;
    if (s_blur.holds < 0) {
        s_blur.holds = 0;
    }
}

bool watch_theme_has_background(void)
{
    return s_bg.px && s_bg.key != KEY_NONE;
}

const lv_image_dsc_t *watch_theme_blur_get(void)
{
    if (!watch_theme_has_background() || !s_blur.ready || s_blur.src_key != s_bg.key) {
        return NULL;
    }
    return &s_blur.out.dsc;
}

bool watch_theme_blur_step(int rows)
{
    if (!watch_theme_has_background()) {
        if (s_blur.out.px) {
            blur_drop(); /* esfera en negro: no hay nada que desenfocar */
        }
        return true;
    }
    if ((s_blur.ready && s_blur.src_key == s_bg.key) || s_blur.failed_key == s_bg.key) {
        return true;
    }
    if (s_blur.src_key != s_bg.key || !s_blur.small) {
        /* fondo nuevo (o primera vez): se empieza de cero */
        s_blur.ready = false;
        if (!s_blur.out.px) {
            s_blur.out.px = heap_caps_malloc(BG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_blur.small) {
            s_blur.small = heap_caps_malloc(flex_blur_small_bytes(FLEX_SCREEN_W, FLEX_SCREEN_H),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_blur.tmp) {
            s_blur.tmp = heap_caps_malloc(flex_blur_tmp_bytes(FLEX_SCREEN_W, FLEX_SCREEN_H),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_blur.out.px || !s_blur.small || !s_blur.tmp) {
            ESP_LOGW(TAG, "sin PSRAM para el fondo desenfocado: el cajón irá sobre negro");
            blur_drop();
            s_blur.failed_key = s_bg.key; /* no se insiste con este fondo: el cajón funciona igual */
            return true;
        }
        lv_image_cache_drop(&s_blur.out.dsc);
        flex_blur_begin(&s_blur.job, s_bg.px, s_blur.out.px, FLEX_SCREEN_W, FLEX_SCREEN_H, s_blur.small,
                        s_blur.tmp, BLUR_DIM);
        s_blur.src_key = s_bg.key;
        s_blur.failed_key = KEY_NONE;
    }
    if (flex_blur_step(&s_blur.job, rows)) {
        blur_free_work();
        bg_set_dsc(&s_blur.out);
        s_blur.ready = true;
        ESP_LOGI(TAG, "fondo desenfocado listo (%u B en PSRAM)", (unsigned)BG_BYTES);
        return true;
    }
    return false;
}

size_t watch_theme_release_caches(void)
{
    size_t n = 0;
    if (s_blur.small) {
        n += flex_blur_small_bytes(FLEX_SCREEN_W, FLEX_SCREEN_H) + flex_blur_tmp_bytes(FLEX_SCREEN_W, FLEX_SCREEN_H);
    }
    if (s_prev.px && !s_preview) {
        bg_drop(&s_prev); /* fondo de la última vista previa del selector */
        n += BG_BYTES;
    }
    const bool had = s_blur.out.px != NULL;
    s_blur.failed_key = KEY_NONE; /* con la memoria liberada se puede volver a intentar */
    if (blur_drop() && had) {
        n += BG_BYTES;
    }
    lv_image_cache_drop(NULL);
    return n;
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

    if (wall_active()) {
        /* Sobre un fondo: vidrio translúcido con el tinte adaptativo según la
         * luma real del fondo que queda debajo (glassTintMix). */
        uint32_t sum = 0;
        int n = 0;
        flex_glass_region_luma(bg_target()->px, FLEX_SCREEN_W, FLEX_SCREEN_H, x, y, w, h, &sum, &n);
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

lv_obj_t *watch_theme_button(lv_obj_t *parent, const char *text, uint32_t bg_rgb, int32_t w, int32_t h)
{
    watch_theme_init();
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, flex_color(bg_rgb), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* pulsado: un poco más oscuro; no disponible: atenuado */
    lv_obj_set_style_bg_color(b, lv_color_hex(flex_mix_rgb(bg_rgb, FLEX_C_BLACK, 190)), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(flex_mix_rgb(bg_rgb, FLEX_C_BLACK, 90)), LV_STATE_DISABLED);
    /* el texto hereda color y fuente del botón (así se atenúa con él) */
    lv_obj_set_style_text_font(b, FLEX_FONT_BODY, 0);
    lv_obj_set_style_text_color(b, flex_color(FLEX_C_WHITE), 0);
    lv_obj_set_style_text_color(b, flex_color(FLEX_C_MUTE), LV_STATE_DISABLED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

void watch_theme_button_set(lv_obj_t *btn, const char *text, uint32_t bg_rgb)
{
    if (text) {
        lv_obj_t *l = lv_obj_get_child(btn, 0);
        if (strcmp(lv_label_get_text(l), text) != 0) {
            lv_label_set_text(l, text);
        }
    }
    if (bg_rgb) {
        lv_obj_set_style_bg_color(btn, flex_color(bg_rgb), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(flex_mix_rgb(bg_rgb, FLEX_C_BLACK, 190)), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(flex_mix_rgb(bg_rgb, FLEX_C_BLACK, 90)), LV_STATE_DISABLED);
    }
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

    const int32_t th = lv_font_get_line_height(FLEX_FONT_TITLE);
    const int32_t ty = FLEX_SAFE_TOP + (FLEX_HDR_ZONE - th) / 2;
    int32_t tx = flex_safe_left(ty, ty + th - 1, FLEX_SAFE_MARGIN);
    if (back_cb) {
        /* Zona táctil completa con el glifo centrado (AppFramework.h:394):
         * el dedo acierta aunque no toque el chevron. La zona se aparta de la
         * esquina redondeada hasta que el chevron se ve entero. */
        const int32_t cy = FLEX_SAFE_TOP + (FLEX_HDR_ZONE - HDR_CHEV) / 2;
        int32_t zx = flex_safe_left(cy, cy + HDR_CHEV - 1, FLEX_SAFE_MARGIN) - (FLEX_HDR_ZONE - HDR_CHEV) / 2;
        if (zx < FLEX_HDR_X) {
            zx = FLEX_HDR_X;
        }
        lv_obj_t *zone = lv_obj_create(hdr);
        lv_obj_remove_style_all(zone);
        lv_obj_set_size(zone, FLEX_HDR_ZONE, FLEX_HDR_ZONE);
        lv_obj_set_pos(zone, zx, FLEX_SAFE_TOP);
        lv_obj_set_style_radius(zone, FLEX_HDR_ZONE / 2, 0);
        lv_obj_add_style(zone, &s_pressed, LV_STATE_PRESSED);
        lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(zone, back_cb, LV_EVENT_CLICKED, user_data);
        lv_obj_t *chev = flex_icon_create(zone, FLEX_ICON_CHEVRON_LEFT, HDR_CHEV, FLEX_C_TXT);
        lv_obj_center(chev);
        tx = zx + FLEX_HDR_ZONE + FLEX_HDR_GAP;
    }
    if (title) {
        lv_obj_t *l = watch_theme_label(hdr, FLEX_FONT_TITLE, FLEX_C_TXT, title);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, th);
        lv_obj_set_width(l, flex_safe_right(ty, ty + th - 1, FLEX_SAFE_MARGIN) + 1 - tx);
        lv_obj_set_pos(l, tx, ty);
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
    lv_obj_set_size(list, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H - FLEX_LIST_BOTTOM);
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
    lv_obj_set_style_pad_left(l, 16, 0); /* alineado con el contenido de las filas */
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
    lv_obj_set_style_max_width(v, 180, 0); /* deja ~130 px al título; un valor largo pasa a 2 líneas */
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

void watch_theme_set_text_color(lv_obj_t *obj, uint32_t rgb)
{
    const lv_color_t c = flex_color(rgb);
    if (obj && !lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_text_color(obj, c, 0);
    }
}

void watch_theme_set_bg_color(lv_obj_t *obj, uint32_t rgb)
{
    const lv_color_t c = flex_color(rgb);
    if (obj && !lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_bg_color(obj, c, 0);
    }
}

void watch_theme_set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj || visible == !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Liquid Glass de los diseños del usuario ---- */

static lv_style_t s_lg_dark, s_lg_dark_pr, s_lg_light, s_lg_light_pr, s_lg_light_chk, s_lg_light_dis, s_glow;
static lv_grad_dsc_t s_glow_grad;
static bool s_lg_inited;

static void lg_init(void)
{
    if (s_lg_inited) {
        return;
    }
    /* Oscuro: degradado vertical del cristal, borde de luz azulado. */
    lv_style_init(&s_lg_dark);
    lv_style_set_bg_color(&s_lg_dark, flex_color(FLEX_LG_DARK_TOP));
    lv_style_set_bg_grad_color(&s_lg_dark, flex_color(FLEX_LG_DARK_BOT));
    lv_style_set_bg_grad_dir(&s_lg_dark, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&s_lg_dark, LV_OPA_COVER);
    lv_style_set_border_width(&s_lg_dark, 2);
    lv_style_set_border_color(&s_lg_dark, flex_color(FLEX_LG_DARK_EDGE));
    lv_style_set_border_opa(&s_lg_dark, FLEX_LG_DARK_EDGE_OPA);
    lv_style_init(&s_lg_dark_pr);
    lv_style_set_bg_color(&s_lg_dark_pr, mix(FLEX_C_WHITE, FLEX_LG_DARK_TOP, 40));
    lv_style_set_bg_grad_color(&s_lg_dark_pr, mix(FLEX_C_WHITE, FLEX_LG_DARK_BOT, 40));
    /* Claro: blanco translúcido sobre el degradado del panel rápido. */
    lv_style_init(&s_lg_light);
    lv_style_set_bg_color(&s_lg_light, flex_color(FLEX_C_WHITE));
    lv_style_set_bg_opa(&s_lg_light, FLEX_LG_LIGHT_FILL_OPA);
    lv_style_set_border_width(&s_lg_light, 2);
    lv_style_set_border_color(&s_lg_light, flex_color(FLEX_C_WHITE));
    lv_style_set_border_opa(&s_lg_light, FLEX_LG_LIGHT_EDGE_OPA);
    lv_style_init(&s_lg_light_pr);
    lv_style_set_bg_opa(&s_lg_light_pr, FLEX_LG_LIGHT_FILL_OPA + 50);
    lv_style_init(&s_lg_light_dis); /* no disponible: más transparente (no el gris del vidrio oscuro) */
    lv_style_set_bg_opa(&s_lg_light_dis, FLEX_LG_LIGHT_FILL_OPA / 2);
    lv_style_set_border_opa(&s_lg_light_dis, FLEX_LG_LIGHT_EDGE_OPA / 2);
    lv_style_init(&s_lg_light_chk);
    lv_style_set_bg_color(&s_lg_light_chk, flex_color(FLEX_QP_ACTIVE));
    lv_style_set_bg_opa(&s_lg_light_chk, 225);
    lv_style_set_border_opa(&s_lg_light_chk, 190);
    /* Reflejo: línea fina blanca que se desvanece hacia los lados. */
    static const lv_color_t gc[3] = {LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
                                     LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)};
    static const lv_opa_t go[3] = {LV_OPA_TRANSP, FLEX_LG_GLOW_OPA, LV_OPA_TRANSP};
    static const uint8_t gf[3] = {0, 128, 255};
    lv_grad_init_stops(&s_glow_grad, gc, go, gf, 3);
    lv_grad_horizontal_init(&s_glow_grad);
    lv_style_init(&s_glow);
    lv_style_set_bg_grad(&s_glow, &s_glow_grad);
    lv_style_set_bg_opa(&s_glow, LV_OPA_COVER);
    lv_style_set_radius(&s_glow, 1);
    s_lg_inited = true;
}

lv_obj_t *watch_theme_lg(lv_obj_t *parent, flex_lg_t kind, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t radius, flex_glow_t glow)
{
    lg_init();
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, kind == FLEX_LG_LIGHT ? &s_lg_light : &s_lg_dark, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (glow != FLEX_GLOW_NONE) {
        /* Dentro del borde y lejos de las esquinas redondeadas. */
        lv_obj_t *g = lv_obj_create(o);
        lv_obj_remove_style_all(g);
        lv_obj_add_style(g, &s_glow, 0);
        lv_obj_set_size(g, w * 56 / 100, 2);
        lv_obj_add_flag(g, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(g, glow == FLEX_GLOW_TOP ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0,
                     glow == FLEX_GLOW_TOP ? 1 : -1);
    }
    return o;
}

lv_obj_t *watch_theme_lg_button(lv_obj_t *parent, flex_lg_t kind, int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t radius, flex_glow_t glow)
{
    lv_obj_t *o = watch_theme_lg(parent, kind, x, y, w, h, radius, glow);
    if (kind == FLEX_LG_LIGHT) {
        lv_obj_add_style(o, &s_lg_light_pr, LV_STATE_PRESSED);
        lv_obj_add_style(o, &s_lg_light_chk, LV_STATE_CHECKED);
        lv_obj_add_style(o, &s_lg_light_dis, LV_STATE_DISABLED);
    } else {
        lv_obj_add_style(o, &s_lg_dark_pr, LV_STATE_PRESSED);
        lv_obj_add_style(o, &s_disabled, LV_STATE_DISABLED);
    }
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *watch_theme_switch(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, w, h);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_RD_SWITCH_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_RD_SWITCH_ON), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, flex_color(FLEX_C_WHITE), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -(h / 10), LV_PART_KNOB); /* el botón casi llena la pista, como en el diseño */
    lv_obj_set_ext_click_area(sw, 12);
    return sw;
}

lv_obj_t *watch_theme_bar(lv_obj_t *parent, int32_t w, int32_t h, uint32_t fill_rgb, uint32_t track_rgb)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_radius(b, h / 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, flex_color(track_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, flex_color(fill_rgb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}
