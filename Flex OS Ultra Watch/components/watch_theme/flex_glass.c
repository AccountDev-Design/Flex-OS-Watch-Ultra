/*
 * flex_glass — Flex Liquid Glass Engine. Ver include/flex_glass.h y
 * docs/LIQUID-GLASS.md.
 *
 * Cada material (función x estado) es UN lv_style_t compartido por todos los
 * objetos que lo usan, construido a partir de su receta pura
 * (flex_glass_recipe, flex_glass_math.c). Los tintes también son estilos
 * compartidos, uno por color usado (tabla pequeña). Cambiar de perfil
 * reescribe esos estilos en su sitio y avisa a LVGL una sola vez: ni se crean
 * ni se borran objetos, y no hay estilos locales que se queden con el aspecto
 * anterior.
 */
#include "flex_glass.h"

#include <string.h>
#include "core/lv_obj_private.h"       /* obj->styles: qué material o tinte lleva un objeto */
#include "core/lv_obj_style_private.h"
#include "misc/cache/lv_cache.h" /* lv_image_cache_drop (no está en lvgl.h) */
#include "esp_log.h"
#include "flex_tokens.h"
#include "flex_wallpaper.h"
#include "watch_theme.h"

static const char *TAG = "flex_glass";

/* Paleta de tintes del sistema: todos se crean al arrancar (memoria fija,
 * ~4 KB de estilos) y un color fuera de la paleta toma el más parecido. El
 * tinte es para lo activo y la acción principal: pocos colores, siempre los
 * mismos en todo el sistema. */
static const uint32_t kPalette[] = {
    FLEX_C_PRIMARY,   FLEX_C_DANGER,   FLEX_C_OK,      FLEX_C_ACCENT,    FLEX_TINT_BLUE,   FLEX_TINT_INDIGO,
    FLEX_TINT_AMBER,  FLEX_TINT_GREEN, FLEX_TINT_RED,  FLEX_TINT_PURPLE, FLEX_TINT_YELLOW, FLEX_TINT_LIME,
};
#define TINT_MAX ((int)(sizeof(kPalette) / sizeof(kPalette[0])))

typedef struct {
    lv_style_t st[FLEX_GLASS_ST_COUNT];
} mat_t;

typedef struct {
    uint32_t rgb;
    mat_t m;
} tint_t;

static mat_t s_role[FLEX_GLASS_ROLE_COUNT];
static tint_t s_tint[TINT_MAX];
static int s_tint_n;
static lv_style_t s_lay_ctl, s_lay_tile, s_sw_ind, s_sw_knob, s_sl_ind, s_sl_knob;
static lv_style_transition_dsc_t s_trans;
static flex_glass_profile_t s_profile = FLEX_GLASS_PREMIUM;
static bool s_inited;

/* Propiedades que se animan al cambiar de estado (el material "se ilumina") */
static const lv_style_prop_t kTransProps[] = {
    LV_STYLE_BG_COLOR,     LV_STYLE_BG_GRAD_COLOR, LV_STYLE_BG_OPA,      LV_STYLE_BG_MAIN_OPA,
    LV_STYLE_BG_GRAD_OPA,  LV_STYLE_BORDER_COLOR,  LV_STYLE_BORDER_OPA,  LV_STYLE_PROP_INV,
};

/* Funciones con borde de luz (el ancho no cambia entre estados: un borde que
 * aparece al pulsar movería 1 px el contenido) */
static bool has_rim(flex_glass_role_t role)
{
    return role != FLEX_GLASS_SCRIM && role != FLEX_GLASS_AMBIENT;
}

/* Degradado (reflejo) en todos los materiales Premium y en el ambiente
 * (opaco) de Estándar; el resto, plano. Dentro de un perfil todos los estados
 * de un material usan el mismo modo, para que la transición sea continua. */
static bool grad_mode(flex_glass_role_t role)
{
    return s_profile == FLEX_GLASS_PREMIUM || (s_profile == FLEX_GLASS_STANDARD && role == FLEX_GLASS_AMBIENT);
}

static void fill(lv_style_t *s, flex_glass_role_t role, const flex_glass_recipe_t *r)
{
    lv_style_reset(s);
    lv_style_init(s);
    if (grad_mode(role)) {
        /* Cuerpo invisible (fantasma en reposo): bg_opa 0, no "superficie" */
        lv_style_set_bg_opa(s, r->top_opa == 0 && r->bot_opa == 0 ? LV_OPA_TRANSP : LV_OPA_COVER);
        lv_style_set_bg_color(s, lv_color_hex(r->top_rgb));
        lv_style_set_bg_grad_color(s, lv_color_hex(r->bot_rgb));
        lv_style_set_bg_grad_dir(s, LV_GRAD_DIR_VER);
        lv_style_set_bg_main_opa(s, r->top_opa);
        lv_style_set_bg_grad_opa(s, r->bot_opa);
        lv_style_set_bg_main_stop(s, 0);
        lv_style_set_bg_grad_stop(s, r->grad_end);
    } else {
        lv_style_set_bg_grad_dir(s, LV_GRAD_DIR_NONE);
        lv_style_set_bg_color(s, lv_color_hex(r->top_rgb));
        lv_style_set_bg_opa(s, r->top_opa);
    }
    if (has_rim(role)) {
        lv_style_set_border_width(s, FLEX_GLASS_RIM_W);
        lv_style_set_border_side(s, LV_BORDER_SIDE_FULL);
        lv_style_set_border_color(s, lv_color_hex(r->rim_rgb));
        lv_style_set_border_opa(s, r->rim_w ? r->rim_opa : LV_OPA_TRANSP);
    }
    lv_style_set_text_color(s, lv_color_hex(r->text_rgb));
    if (r->trans_ms) {
        lv_style_set_transition(s, &s_trans);
    }
}

static void build_mat(mat_t *m, flex_glass_role_t role, uint32_t tint)
{
    for (int st = 0; st < FLEX_GLASS_ST_COUNT; st++) {
        flex_glass_recipe_t r;
        flex_glass_recipe(role, s_profile, (flex_glass_state_t)st, tint, &r);
        fill(&m->st[st], role, &r);
    }
}

static void build_all(void)
{
    const uint32_t ms = s_profile == FLEX_GLASS_PREMIUM ? FLEX_GLASS_TRANS_PREMIUM
                        : s_profile == FLEX_GLASS_STANDARD ? FLEX_GLASS_TRANS_STD
                                                          : 0;
    lv_style_transition_dsc_init(&s_trans, kTransProps, lv_anim_path_ease_out, ms, 0, NULL);
    for (int r = 0; r < FLEX_GLASS_ROLE_COUNT; r++) {
        build_mat(&s_role[r], (flex_glass_role_t)r, FLEX_GLASS_TINT_NONE);
    }
    for (int i = 0; i < s_tint_n; i++) {
        build_mat(&s_tint[i].m, FLEX_GLASS_CONTROL, s_tint[i].rgb);
    }
}

static void face_init(void);

void flex_glass_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    for (int r = 0; r < FLEX_GLASS_ROLE_COUNT; r++) {
        for (int st = 0; st < FLEX_GLASS_ST_COUNT; st++) {
            lv_style_init(&s_role[r].st[st]);
        }
    }
    for (int t = 0; t < TINT_MAX; t++) {
        s_tint[t].rgb = kPalette[t];
        for (int st = 0; st < FLEX_GLASS_ST_COUNT; st++) {
            lv_style_init(&s_tint[t].m.st[st]);
        }
    }
    s_tint_n = TINT_MAX;
    build_all();
    face_init();

    /* Disposición compartida de los componentes (no depende del perfil) */
    lv_style_init(&s_lay_ctl);
    lv_style_set_radius(&s_lay_ctl, FLEX_RADIUS_CARD);
    lv_style_set_pad_all(&s_lay_ctl, 18);
    lv_style_set_pad_row(&s_lay_ctl, 4);
    lv_style_init(&s_lay_tile);
    lv_style_set_radius(&s_lay_tile, FLEX_RADIUS_CARD);
    lv_style_set_pad_all(&s_lay_tile, 16);
    /* Interruptor y deslizador */
    lv_style_init(&s_sw_ind);
    lv_style_set_radius(&s_sw_ind, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_sw_ind, lv_color_hex(FLEX_TINT_GREEN));
    lv_style_set_bg_opa(&s_sw_ind, LV_OPA_COVER);
    lv_style_init(&s_sw_knob);
    lv_style_set_radius(&s_sw_knob, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_sw_knob, lv_color_hex(FLEX_C_WHITE));
    lv_style_set_bg_opa(&s_sw_knob, LV_OPA_COVER);
    lv_style_init(&s_sl_ind);
    lv_style_set_radius(&s_sl_ind, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&s_sl_ind, 230);
    lv_style_init(&s_sl_knob); /* sin botón visible: se arrastra la barra entera */
    lv_style_set_bg_opa(&s_sl_knob, LV_OPA_TRANSP);
}

void flex_glass_set_profile(flex_glass_profile_t p)
{
    if ((unsigned)p >= FLEX_GLASS_PROFILE_COUNT) {
        p = FLEX_GLASS_PREMIUM;
    }
    if (s_inited && p == s_profile) {
        return;
    }
    s_profile = p;
    if (!s_inited) {
        flex_glass_init();
        return;
    }
    build_all();
    /* Un solo aviso para todos los estilos: LVGL recalcula y redibuja una vez
     * los objetos que los usan. */
    lv_obj_report_style_change(NULL);
    ESP_LOGI(TAG, "perfil %s", flex_glass_profile_name(p));
}

flex_glass_profile_t flex_glass_profile(void)
{
    return s_profile;
}

const char *flex_glass_profile_name(flex_glass_profile_t p)
{
    switch (p) {
    case FLEX_GLASS_PREMIUM:
        return "Premium";
    case FLEX_GLASS_STANDARD:
        return "Estándar";
    default:
        return "Ligero";
    }
}

uint32_t flex_glass_style_count(void)
{
    return (uint32_t)(FLEX_GLASS_ROLE_COUNT + s_tint_n) * FLEX_GLASS_ST_COUNT;
}

/* ---- Materiales en los objetos ---- */

/* Material del objeto (su estilo normal del motor) o -1. */
static int role_of(const lv_obj_t *obj)
{
    for (uint32_t i = 0; i < obj->style_cnt; i++) {
        for (int r = 0; r < FLEX_GLASS_ROLE_COUNT; r++) {
            if (obj->styles[i].style == &s_role[r].st[FLEX_GLASS_ST_NORMAL]) {
                return r;
            }
        }
    }
    return -1;
}

/* Tinte puesto en el objeto (índice en s_tint) o -1. */
static int tint_of(const lv_obj_t *obj)
{
    for (uint32_t i = 0; i < obj->style_cnt; i++) {
        for (int t = 0; t < s_tint_n; t++) {
            if (obj->styles[i].style == &s_tint[t].m.st[FLEX_GLASS_ST_NORMAL]) {
                return t;
            }
        }
    }
    return -1;
}

static int tint_slot(uint32_t rgb)
{
    int best = 0;
    uint32_t best_d = UINT32_MAX;
    for (int t = 0; t < s_tint_n; t++) {
        uint32_t d = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            const int a = (int)((rgb >> sh) & 0xFF) - (int)((s_tint[t].rgb >> sh) & 0xFF);
            d += (uint32_t)(a * a);
        }
        if (d < best_d) {
            best_d = d;
            best = t;
        }
    }
    return best;
}

lv_obj_t *flex_glass_create(lv_obj_t *parent, flex_glass_role_t role)
{
    flex_glass_init();
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &s_role[role].st[FLEX_GLASS_ST_NORMAL], 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *flex_glass_panel(lv_obj_t *parent, flex_glass_role_t role, int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r)
{
    lv_obj_t *o = flex_glass_create(parent, role);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    return o;
}

lv_obj_t *flex_glass_pressable(lv_obj_t *parent, flex_glass_role_t role, int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t r)
{
    lv_obj_t *o = flex_glass_panel(parent, role, x, y, w, h, r);
    flex_glass_interactive(o);
    return o;
}

void flex_glass_apply(lv_obj_t *obj, flex_glass_role_t role, lv_style_selector_t part)
{
    flex_glass_init();
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_NORMAL], part);
}

void flex_glass_apply_states(lv_obj_t *obj, flex_glass_role_t role, lv_style_selector_t part)
{
    flex_glass_init();
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_NORMAL], part);
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_PRESSED], part | LV_STATE_PRESSED);
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_DISABLED], part | LV_STATE_DISABLED);
}

void flex_glass_interactive(lv_obj_t *obj)
{
    const int r = role_of(obj);
    const flex_glass_role_t role = r >= 0 ? (flex_glass_role_t)r : FLEX_GLASS_CONTROL;
    if (r < 0) {
        lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_NORMAL], 0);
    }
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_PRESSED], LV_STATE_PRESSED);
    lv_obj_add_style(obj, &s_role[role].st[FLEX_GLASS_ST_DISABLED], LV_STATE_DISABLED);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void flex_glass_set_tint(lv_obj_t *obj, uint32_t tint, lv_state_t when)
{
    flex_glass_init();
    const int cur = tint_of(obj);
    const int want = tint == FLEX_GLASS_TINT_NONE ? -1 : tint_slot(tint & 0xFFFFFF);
    if (cur == want) {
        return; /* el mismo: no se toca (aplicar un estilo redibuja) */
    }
    if (cur >= 0) {
        for (int st = 0; st < FLEX_GLASS_ST_COUNT; st++) {
            lv_obj_remove_style(obj, &s_tint[cur].m.st[st], LV_PART_ANY | LV_STATE_ANY);
        }
    }
    if (want < 0) {
        return;
    }
    mat_t *m = &s_tint[want].m;
    lv_obj_add_style(obj, &m->st[FLEX_GLASS_ST_NORMAL], when);
    lv_obj_add_style(obj, &m->st[FLEX_GLASS_ST_PRESSED], when | LV_STATE_PRESSED);
    lv_obj_add_style(obj, &m->st[FLEX_GLASS_ST_DISABLED], when | LV_STATE_DISABLED);
}

/* ---- Componentes ---- */

lv_obj_t *flex_glass_button(lv_obj_t *parent, const char *text, uint32_t tint, int32_t w, int32_t h)
{
    lv_obj_t *b = flex_glass_create(parent, FLEX_GLASS_CONTROL);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(b, LV_CLAMP(6, w / 12, 14), 0); /* w puede ser LV_PCT: entonces 14 */
    lv_obj_set_style_text_font(b, FLEX_TYPE_LABEL, 0); /* el texto hereda fuente y color (se apaga con él) */
    flex_glass_interactive(b);
    if (tint != FLEX_GLASS_TINT_NONE && tint != 0) {
        flex_glass_set_tint(b, tint, LV_STATE_DEFAULT);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text ? text : "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(l, LV_PCT(100), 0);
    lv_obj_center(l);
    return b;
}

void flex_glass_button_set(lv_obj_t *btn, const char *text, uint32_t tint)
{
    if (text) {
        lv_obj_t *l = lv_obj_get_child(btn, 0);
        if (l && strcmp(lv_label_get_text(l), text) != 0) {
            lv_label_set_text(l, text);
        }
    }
    if (tint) {
        flex_glass_set_tint(btn, tint, LV_STATE_DEFAULT);
    }
}

lv_obj_t *flex_glass_icon_button(lv_obj_t *parent, flex_icon_t icon, int32_t icon_size, int32_t d, uint32_t tint)
{
    lv_obj_t *b = flex_glass_create(parent, FLEX_GLASS_CONTROL);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    flex_glass_interactive(b);
    if (tint != FLEX_GLASS_TINT_NONE && tint != 0) {
        flex_glass_set_tint(b, tint, LV_STATE_DEFAULT);
    }
    lv_obj_t *ic = flex_icon_create(b, icon, icon_size, FLEX_GLASS_TXT);
    lv_obj_center(ic);
    return b;
}

lv_obj_t *flex_glass_control(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = flex_glass_create(parent, FLEX_GLASS_CONTROL);
    lv_obj_add_style(c, &s_lay_ctl, 0);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    flex_glass_interactive(c);
    return c;
}

lv_obj_t *flex_glass_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *t = flex_glass_create(parent, FLEX_GLASS_CARD);
    lv_obj_add_style(t, &s_lay_tile, 0);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_size(t, w, h);
    flex_glass_interactive(t);
    return t;
}

lv_obj_t *flex_glass_switch(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *sw = lv_switch_create(parent);
    flex_glass_switch_style(sw, w, h);
    lv_obj_set_ext_click_area(sw, 12);
    return sw;
}

void flex_glass_switch_style(lv_obj_t *sw, int32_t w, int32_t h)
{
    flex_glass_init();
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, w, h);
    lv_obj_add_style(sw, &s_role[FLEX_GLASS_CONTROL].st[FLEX_GLASS_ST_NORMAL], LV_PART_MAIN);
    lv_obj_add_style(sw, &s_role[FLEX_GLASS_CONTROL].st[FLEX_GLASS_ST_DISABLED], LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_style(sw, &s_sw_ind, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(sw, &s_sw_knob, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -(h / 10), LV_PART_KNOB); /* el botón casi llena la pista */
}

lv_obj_t *flex_glass_slider(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t fill_rgb)
{
    flex_glass_init();
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_remove_style_all(sl);
    lv_obj_set_pos(sl, x, y);
    lv_obj_set_size(sl, w, h);
    lv_obj_add_style(sl, &s_role[FLEX_GLASS_CONTROL].st[FLEX_GLASS_ST_NORMAL], LV_PART_MAIN);
    lv_obj_add_style(sl, &s_role[FLEX_GLASS_CONTROL].st[FLEX_GLASS_ST_DISABLED], LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_style(sl, &s_sl_ind, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(fill_rgb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_40, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_add_style(sl, &s_sl_knob, LV_PART_KNOB);
    return sl;
}

lv_obj_t *flex_glass_lens(lv_obj_t *bar, int32_t h)
{
    lv_obj_t *l = flex_glass_create(bar, FLEX_GLASS_LENS);
    lv_obj_set_height(l, h);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
    return l;
}

static void lens_x_cb(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void lens_w_cb(void *var, int32_t v)
{
    lv_obj_set_width(var, v);
}

void flex_glass_lens_move(lv_obj_t *lens, int32_t x, int32_t w, uint32_t ms)
{
    lv_anim_delete(lens, lens_x_cb);
    lv_anim_delete(lens, lens_w_cb);
    if (!ms || s_profile == FLEX_GLASS_LITE) {
        lv_obj_set_x(lens, x);
        lv_obj_set_width(lens, w);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lens);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, lens_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(lens), x);
    lv_anim_start(&a); /* ligadas a la lente: se borran con ella */
    lv_anim_set_exec_cb(&a, lens_w_cb);
    lv_anim_set_values(&a, lv_obj_get_width(lens), w);
    lv_anim_start(&a);
}

/* ---- Esferas: placa congelada ----
 * Mismos valores y la misma lógica que la tarjeta de vidrio original de las
 * esferas (no dependen del perfil). */

static lv_style_t s_face;
static lv_opa_t s_face_flat_mix;
static bool s_face_inited;

static uint16_t rgb_to_565(uint32_t rgb)
{
    return flex_rgb565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void face_init(void)
{
    if (s_face_inited) {
        return;
    }
    s_face_inited = true;
    s_face_flat_mix =
        flex_glass_tint_mix((uint32_t)flex_glass_luma565(rgb_to_565(FLEX_C_PAGE)), 1, rgb_to_565(FLEX_C_GLASS));
    const uint32_t surf = flex_mix_rgb(FLEX_C_GLASS, FLEX_C_PAGE, s_face_flat_mix);
    lv_style_init(&s_face);
    lv_style_set_bg_color(&s_face, lv_color_hex(surf));
    lv_style_set_bg_opa(&s_face, LV_OPA_COVER);
    lv_style_set_radius(&s_face, FLEX_RADIUS_CARD);
    lv_style_set_border_width(&s_face, 1);
    lv_style_set_border_side(&s_face, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&s_face, lv_color_hex(flex_mix_rgb(FLEX_GLASS_EDGE_HI, surf, FLEX_GLASS_EDGE_STRONG)));
    lv_style_set_border_opa(&s_face, LV_OPA_COVER);
    lv_style_set_pad_all(&s_face, 18);
    lv_style_set_pad_row(&s_face, 4);
}

lv_obj_t *flex_glass_face_plate(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    face_init();
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_face, 0);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    const uint16_t *bg = watch_theme_bg_pixels();
    if (bg) {
        /* Sobre un fondo: translúcida con el tinte adaptativo según la luma
         * real del fondo que queda debajo (glassTintMix). */
        uint32_t sum = 0;
        int n = 0;
        flex_glass_region_luma(bg, FLEX_SCREEN_W, FLEX_SCREEN_H, x, y, w, h, &sum, &n);
        lv_obj_set_style_bg_color(card, lv_color_hex(FLEX_C_GLASS), 0);
        lv_obj_set_style_bg_opa(card, flex_glass_tint_mix(sum, n, rgb_to_565(FLEX_C_GLASS)), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(FLEX_GLASS_EDGE_HI), 0);
        lv_obj_set_style_border_opa(card, FLEX_GLASS_EDGE_STRONG, 0);
    }
    return card;
}

void flex_glass_face_plate_min_tint(lv_obj_t *plate, lv_opa_t min_mix)
{
    face_init();
    if (lv_obj_get_style_bg_opa(plate, 0) < LV_OPA_COVER) {
        if (lv_obj_get_style_bg_opa(plate, 0) < min_mix) {
            lv_obj_set_style_bg_opa(plate, min_mix, 0);
        }
        return;
    }
    if (min_mix > s_face_flat_mix) {
        lv_obj_set_style_bg_color(plate, lv_color_hex(flex_mix_rgb(FLEX_C_GLASS, FLEX_C_PAGE, min_mix)), 0);
    }
}

/* ======================= Fondo desenfocado desde la fuente ======================= */

/* La imagen a 1/4 se dibuja a escala EXACTA x4 (pivote 0,0). Con una escala
 * no entera, LVGL calcula el paso del muestreo con el ancho de cada trozo que
 * redibuja, y un mismo píxel sale distinto según el trozo: rastros. A x4 el
 * paso es exacto en cualquier trozo. Además, LVGL acota el muestreo al último
 * píxel de la fuente (4*(BD_W-1)): la imagen sobresale BD_M px por cada lado
 * para que lo visible no llegue nunca a ese límite. */
#define BD_M   2
#define BD_W   ((BD_M + FLEX_SCREEN_W - 1 + 3) / 4 + 1)
#define BD_H   ((BD_M + FLEX_SCREEN_H - 1 + 3) / 4 + 1)
#define BD_R   3 /* radio de caja a 1/4: ~12 px a tamaño real */
#define BD_PASSES 3
#define BD_DESAT 70

typedef struct {
    lv_image_dsc_t dsc;
    uint16_t *px;
    bool has;
} backdrop_t;

size_t flex_glass_backdrop_bytes(void)
{
    return sizeof(backdrop_t) + (size_t)BD_W * BD_H * 2;
}

static void backdrop_free_cb(lv_event_t *e)
{
    backdrop_t *b = lv_event_get_user_data(e);
    if (b) {
        lv_image_cache_drop(&b->dsc);
        lv_free(b->px);
        lv_free(b);
    }
}

lv_obj_t *flex_glass_backdrop(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_style_all(img);
    lv_obj_set_pos(img, -BD_M, -BD_M);
    lv_obj_set_size(img, BD_W * 4, BD_H * 4);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_FLOATING); /* ni se desplaza ni cuenta para el desplazamiento */
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); /* vacío: la página negra */
    backdrop_t *b = lv_malloc_zeroed(sizeof(*b));
    if (!b) {
        return img;
    }
    b->px = lv_malloc((size_t)BD_W * BD_H * 2);
    if (!b->px) {
        lv_free(b);
        return img;
    }
    memset(b->px, 0, (size_t)BD_W * BD_H * 2);
    b->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    b->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    b->dsc.header.w = BD_W;
    b->dsc.header.h = BD_H;
    b->dsc.header.stride = BD_W * 2;
    b->dsc.data_size = (uint32_t)BD_W * BD_H * 2;
    b->dsc.data = (const uint8_t *)b->px;
    lv_obj_set_user_data(img, b);
    lv_obj_add_event_cb(img, backdrop_free_cb, LV_EVENT_DELETE, b);
    lv_image_set_src(img, &b->dsc);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(img, true);
    return img;
}

bool flex_glass_backdrop_set(lv_obj_t *bd, const uint16_t *src, int w, int h, int stride_px, uint8_t dim)
{
    backdrop_t *b = bd ? lv_obj_get_user_data(bd) : NULL;
    if (!b || !src || w <= 0 || h <= 0) {
        return false;
    }
    /* Trabajo temporal (solo durante esta llamada): la imagen en RGB888 y una línea. */
    const size_t n888 = (size_t)BD_W * BD_H * 3;
    uint8_t *work = lv_malloc(n888 + (size_t)flex_blur_work_bytes(BD_W, BD_H, BD_R));
    if (!work) {
        return false;
    }
    flex_blur_fit888(src, w, h, stride_px, work, BD_W, BD_H);
    flex_blur_box888(work, BD_W, BD_H, BD_R, BD_PASSES, work + n888);
    flex_blur_tone888(work, BD_W * BD_H, dim, BD_DESAT);
    flex_blur_pack565(work, BD_W, BD_H, b->px);
    lv_free(work);
    b->has = true;
    lv_image_cache_drop(&b->dsc);
    /* En Ligero no se dibuja: el escalado con filtro cuesta CPU en cada redibujo. */
    if (s_profile == FLEX_GLASS_LITE) {
        lv_obj_add_flag(bd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(bd, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(bd);
    return true;
}

void flex_glass_backdrop_clear(lv_obj_t *bd)
{
    backdrop_t *b = bd ? lv_obj_get_user_data(bd) : NULL;
    if (!b) {
        return;
    }
    if (b->has) {
        b->has = false;
        lv_obj_add_flag(bd, LV_OBJ_FLAG_HIDDEN);
    }
}
