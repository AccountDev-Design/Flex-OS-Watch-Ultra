/*
 * ui_keyboard — teclado de pantalla compartido (contraseña Wi-Fi, Notas,
 * renombrar).
 *
 * La referencia tiene su teclado de 4 capas (FlexOS_Ultra_Keyboard.h). Aquí
 * es una matriz de botones con teclas de TEXTO ("DEL", "OK", "Intro"): los
 * símbolos de LVGL no existen en la fuente Outfit. Dos modos:
 *  - UI_KB_ASCII (contraseñas): minúsculas, mayúsculas, números y símbolos;
 *    todo el ASCII imprimible. "OK" confirma.
 *  - UI_KB_TEXT (notas, nombres): además ñ/Ñ y una página de tildes y
 *    ¿¡; "OK" confirma o, con UI_KB_NEWLINE, "Intro" escribe un salto.
 * Mayúscula para una sola letra, como en los teclados de móvil.
 */
#include "ui_internal.h"

#include <string.h>
#include "watch_theme.h"

enum { KB_LOWER, KB_UPPER, KB_NUM, KB_SYM, KB_ACC, KB_PAGES };

typedef struct {
    lv_obj_t *kb, *ta;
    int page;
    int flags;
    ui_kb_cb_t ok;
    void *ud;
} kb_t;

/* ASCII (contraseñas) */
static const char *const kAscLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                        "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                        "ABC", "z", "x", "c", "v", "b", "n", "m", "DEL", "\n",
                                        "123", "Espacio", "OK", ""};
static const char *const kAscUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                        "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                        "abc", "Z", "X", "C", "V", "B", "N", "M", "DEL", "\n",
                                        "123", "Espacio", "OK", ""};
static const char *const kAscNum[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                      "-", "_", ".", ",", "@", "#", "$", "%", "&", "*", "\n",
                                      "#+=", "!", "?", "(", ")", "/", ":", ";", "DEL", "\n",
                                      "abc", "Espacio", "OK", ""};
static const char *const kAscSym[] = {"[", "]", "{", "}", "<", ">", "^", "~", "`", "|", "\n",
                                      "\\", "'", "\"", "+", "=", "\n",
                                      "123", "DEL", "\n",
                                      "abc", "Espacio", "OK", ""};

/* Texto en español (la última tecla la pone el modo: "OK" o "Intro") */
#define LAST_OK "OK"
static const char *kTxtLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                  "a", "s", "d", "f", "g", "h", "j", "k", "l", "ñ", "\n",
                                  "ABC", "z", "x", "c", "v", "b", "n", "m", "DEL", "\n",
                                  "123", "áé", "Espacio", LAST_OK, ""};
static const char *kTxtUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                  "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ñ", "\n",
                                  "abc", "Z", "X", "C", "V", "B", "N", "M", "DEL", "\n",
                                  "123", "áé", "Espacio", LAST_OK, ""};
static const char *kTxtNum[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                "-", "_", ".", ",", "@", "#", "$", "%", "&", "*", "\n",
                                "#+=", "!", "?", "(", ")", "/", ":", ";", "DEL", "\n",
                                "abc", "áé", "Espacio", LAST_OK, ""};
static const char *kTxtSym[] = {"[", "]", "{", "}", "<", ">", "^", "~", "|", "\n",
                                "\\", "'", "\"", "+", "=", "º", "ª", "\n",
                                "123", "DEL", "\n",
                                "abc", "áé", "Espacio", LAST_OK, ""};
static const char *kTxtAcc[] = {"á", "é", "í", "ó", "ú", "ü", "\n",
                                "Á", "É", "Í", "Ó", "Ú", "Ü", "\n",
                                "¿", "¡", "ç", "·", "DEL", "\n",
                                "abc", "123", "Espacio", LAST_OK, ""};

/* Dos juegos de mapas de texto: con "OK" y con "Intro" (copias en RAM) */
#define TXT_N 40
static const char *s_nl[KB_PAGES][TXT_N];
static bool s_nl_ready;

static const char *const *map_of(const kb_t *k, int page)
{
    if (!(k->flags & UI_KB_TEXT)) {
        static const char *const *const asc[KB_PAGES] = {kAscLower, kAscUpper, kAscNum, kAscSym, kAscLower};
        return asc[page];
    }
    static const char *const *const txt[KB_PAGES] = {kTxtLower, kTxtUpper, kTxtNum, kTxtSym, kTxtAcc};
    if (!(k->flags & UI_KB_NEWLINE)) {
        return txt[page];
    }
    if (!s_nl_ready) {
        for (int p = 0; p < KB_PAGES; p++) {
            for (int i = 0; i < TXT_N; i++) {
                const char *t = txt[p][i];
                s_nl[p][i] = (t[0] && !strcmp(t, LAST_OK)) ? "Intro" : t;
                if (!t[0]) {
                    break;
                }
            }
        }
        s_nl_ready = true;
    }
    return s_nl[page];
}

static bool is_ctrl(const char *t)
{
    static const char *const kCtrl[] = {"DEL", "OK", "Intro", "ABC", "abc", "123", "#+=", "áé"};
    for (size_t i = 0; i < sizeof(kCtrl) / sizeof(kCtrl[0]); i++) {
        if (!strcmp(t, kCtrl[i])) {
            return true;
        }
    }
    return false;
}

/* Anchos relativos: las teclas de control, más anchas */
static void set_page(kb_t *k, int page)
{
    k->page = page;
    const char *const *map = map_of(k, page);
    lv_buttonmatrix_set_map(k->kb, map);
    for (uint32_t i = 0, b = 0; map[i][0]; i++) {
        if (strcmp(map[i], "\n") == 0) {
            continue;
        }
        const uint32_t w = !strcmp(map[i], "Espacio") ? ((k->flags & UI_KB_TEXT) ? 6 : 8) : is_ctrl(map[i]) ? 3 : 2;
        lv_buttonmatrix_set_button_width(k->kb, b++, w);
    }
}

static void kb_cb(lv_event_t *e)
{
    kb_t *k = lv_event_get_user_data(e);
    const uint32_t id = lv_buttonmatrix_get_selected_button(k->kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char *t = lv_buttonmatrix_get_button_text(k->kb, id);
    if (!t) {
        return;
    }
    if (!strcmp(t, "DEL")) {
        lv_textarea_delete_char(k->ta);
    } else if (!strcmp(t, "ABC")) {
        set_page(k, KB_UPPER);
    } else if (!strcmp(t, "abc")) {
        set_page(k, KB_LOWER);
    } else if (!strcmp(t, "123")) {
        set_page(k, KB_NUM);
    } else if (!strcmp(t, "#+=")) {
        set_page(k, KB_SYM);
    } else if (!strcmp(t, "áé")) {
        set_page(k, KB_ACC);
    } else if (!strcmp(t, "Espacio")) {
        lv_textarea_add_char(k->ta, ' ');
    } else if (!strcmp(t, "Intro")) {
        lv_textarea_add_char(k->ta, '\n');
    } else if (!strcmp(t, "OK")) {
        if (k->ok) {
            k->ok(k->ud);
        }
        return; /* puede haber borrado la página */
    } else {
        lv_textarea_add_text(k->ta, t);
        if (k->page == KB_UPPER) {
            set_page(k, KB_LOWER); /* mayúscula para una letra */
        }
    }
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_keyboard_create(lv_obj_t *parent, lv_obj_t *ta, int flags, ui_kb_cb_t ok, void *ud)
{
    kb_t *k = lv_malloc_zeroed(sizeof(*k));
    LV_ASSERT_MALLOC(k);
    k->ta = ta;
    k->flags = flags;
    k->ok = ok;
    k->ud = ud;
    k->kb = lv_buttonmatrix_create(parent);
    lv_obj_t *kb = k->kb;
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 0, 0);
    lv_obj_set_style_pad_gap(kb, 5, 0);
    /* Teclas: controles de vidrio del motor (se iluminan al pulsarlas) */
    flex_glass_apply_states(kb, FLEX_GLASS_CONTROL, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, FLEX_FONT_SMALL, LV_PART_ITEMS);
    set_page(k, KB_LOWER);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_VALUE_CHANGED, k);
    lv_obj_add_event_cb(kb, free_cb, LV_EVENT_DELETE, k);
    return kb;
}

void ui_textarea_style(lv_obj_t *ta)
{
    /* Campo de texto: material de contenido del motor */
    flex_glass_apply(ta, FLEX_GLASS_ROW, 0);
    lv_obj_set_style_radius(ta, 16, 0);
    lv_obj_set_style_text_font(ta, FLEX_FONT_BODY, 0);
    lv_obj_set_style_text_color(ta, flex_color(FLEX_C_TXT), 0);
    lv_obj_set_style_text_color(ta, flex_color(FLEX_C_MUTE), LV_PART_TEXTAREA_PLACEHOLDER);
    /* 1 px menos de relleno por el borde de luz del material: la línea de
     * texto sigue cabiendo entera (si no, el campo se desplaza para enseñar el
     * cursor y vuelve, sin fin) */
    lv_obj_set_style_pad_hor(ta, 14 - FLEX_GLASS_RIM_W, 0);
    lv_obj_set_style_pad_ver(ta, 14 - FLEX_GLASS_RIM_W, 0);
    lv_obj_set_style_bg_color(ta, flex_color(FLEX_C_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, flex_color(FLEX_C_ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
    /* Cursor fijo, sin parpadeo: una animación infinita redibuja la pantalla
     * sin parar (batería) y nunca deja la interfaz en reposo. */
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR | LV_STATE_FOCUSED); /* el tema lo pone ahí */
    lv_obj_add_state(ta, LV_STATE_FOCUSED); /* cursor visible */
}
