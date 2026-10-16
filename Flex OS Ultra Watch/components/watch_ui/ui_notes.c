/*
 * ui_notes — app Notas (IC_NOTAS de Flex OS Ultra) en el reloj (F9.7).
 *
 * Igual que la referencia (FlexOS_Ultra_AppNotes.h):
 *  - cada tarjeta es un .txt de verdad en /Notas: el título sale del NOMBRE
 *    del archivo y la vista previa de sus primeros bytes, leídos del disco;
 *    una nota vacía se ve "(vacía)", no hay texto de relleno;
 *  - "Nueva nota" crea "Sin título N.txt" y la abre;
 *  - el editor está atado al archivo: se guarda 2 s después de la última
 *    tecla y al salir (un apagón no se lleva lo escrito); la escritura es
 *    atómica (flexfs);
 *  - mantener pulsada una nota abre sus opciones (renombrar, papelera,
 *    borrar), las mismas del explorador.
 * Teclado: el compartido en modo texto (ñ, tildes, ¿¡) con "Intro".
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flexfs.h"
#include "watch_theme.h"

#define NOTES_TICK_MS 500
#define AUTOSAVE_MS   2000 /* NOTE_AUTOSAVE_MS de la referencia */
#define PREVIEW_LEN   96   /* NOTE_PREV_LEN */

static char s_note_path[FL_PATH_MAX];

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

void ui_note_open(const char *path)
{
    snprintf(s_note_path, sizeof(s_note_path), "%s", path ? path : "");
    ui_nav_push(ui_note_edit_create);
}

/* ======================= Lista ======================= */

typedef struct {
    lv_obj_t *list;
    uint32_t gen;
    bool ready;
} nl_page_t;

static void card_cb(lv_event_t *e)
{
    const char *path = lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (!path) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        ui_file_options(path);
    } else {
        ui_note_open(path);
    }
}

static void new_cb(lv_event_t *e)
{
    (void)e;
    char p[FL_PATH_MAX];
    if (ffs_new_name(FFS_DIR_NOTAS, "Sin título", FFS_EXT_NOTE, p, sizeof(p)) && ffs_write_text(p, "")) {
        ui_files_changed();
        ui_note_open(p);
    }
}

static void str_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void build(nl_page_t *p)
{
    lv_obj_clean(p->list);
    const char *why = NULL;
    p->ready = ui_fs_ready(&why);
    if (!p->ready) {
        ui_fs_notice(p->list, "Notas", why);
        return;
    }
    lv_obj_t *nb = watch_theme_button(p->list, "Nueva nota", FLEX_C_PRIMARY, LV_PCT(100), 60);
    lv_obj_add_event_cb(nb, new_cb, LV_EVENT_CLICKED, NULL);
    ffs_entry_t *e = lv_malloc(sizeof(ffs_entry_t) * FFS_LIST_MAX);
    LV_ASSERT_MALLOC(e);
    const int n = ffs_list(FFS_DIR_NOTAS, e, FFS_LIST_MAX);
    int shown_n = 0;
    for (int i = 0; i < n; i++) {
        if (e[i].dir || strcmp(fl_ext(e[i].name), FFS_EXT_NOTE)) {
            continue; /* solo notas de texto */
        }
        char path[FL_PATH_MAX], stem[FL_NAME_MAX], prev[PREVIEW_LEN + 1], t[PREVIEW_LEN + 48];
        if (!fl_join(FFS_DIR_NOTAS, e[i].name, path, sizeof(path))) {
            continue;
        }
        lv_obj_t *c = watch_theme_tile(p->list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 2, 0);
        fl_stem(e[i].name, stem, sizeof(stem));
        ui_phone_text(stem, t, sizeof(t), FLEX_FONT_BODY);
        lv_obj_t *title = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, t);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        /* contenido REAL del archivo (noteReload de la referencia) */
        ffs_read_text(path, prev, sizeof(prev));
        fl_utf8_trim(prev);
        ui_phone_text(prev, t, sizeof(t), FLEX_FONT_SMALL);
        lv_obj_t *pv = watch_theme_label(c, FLEX_FONT_SMALL, t[0] ? FLEX_C_TXT2 : FLEX_C_MUTE, t[0] ? t : "(vacía)");
        lv_obj_set_width(pv, LV_PCT(100));
        lv_obj_set_height(pv, (t[0] ? 2 : 1) * lv_font_get_line_height(FLEX_FONT_SMALL));
        lv_label_set_long_mode(pv, LV_LABEL_LONG_MODE_DOTS);
        char *ud = lv_malloc(strlen(path) + 1);
        LV_ASSERT_MALLOC(ud);
        strcpy(ud, path);
        lv_obj_set_user_data(c, ud);
        lv_obj_add_event_cb(c, str_free_cb, LV_EVENT_DELETE, ud);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_LONG_PRESSED, NULL);
        shown_n++;
    }
    lv_free(e);
    if (shown_n == 0) {
        watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2, "No hay notas todavía");
        lv_obj_t *h = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, "Pulsa «Nueva nota» para crear una.");
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(h, LV_PCT(100));
    } else {
        lv_obj_t *h = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                        "Mantén pulsada una nota para renombrarla o tirarla.");
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(h, LV_PCT(100));
    }
}

static void nl_tick_cb(lv_timer_t *t)
{
    nl_page_t *p = lv_timer_get_user_data(t);
    const bool ready = ui_fs_ready(NULL);
    if (p->gen != ui_files_gen() || ready != p->ready) {
        p->gen = ui_files_gen();
        build(p);
    }
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_notes_create(lv_obj_t *parent)
{
    nl_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Notas", back_cb, NULL);
    p->list = watch_theme_list(page);
    p->gen = ui_files_gen();
    build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, nl_tick_cb, NOTES_TICK_MS, p);
    return page;
}

/* ======================= Editor ======================= */

typedef struct {
    char path[FL_PATH_MAX];
    lv_obj_t *ta, *status;
    bool dirty, failed;
    uint32_t last_key;
} ne_page_t;

static void save(ne_page_t *p)
{
    if (!p->dirty || !p->ta) {
        return;
    }
    p->failed = !ffs_write_text(p->path, lv_textarea_get_text(p->ta));
    p->dirty = p->failed;
    ui_files_changed();
    lv_label_set_text(p->status, p->failed ? "No se pudo guardar: se reintenta" : "Guardado");
    lv_obj_set_style_text_color(p->status, flex_color(p->failed ? FLEX_C_WARN : FLEX_C_MUTE), 0);
}

static void changed_cb(lv_event_t *e)
{
    ne_page_t *p = lv_event_get_user_data(e);
    p->dirty = true;
    p->last_key = lv_tick_get();
    lv_label_set_text(p->status, "Sin guardar");
    lv_obj_set_style_text_color(p->status, flex_color(FLEX_C_MUTE), 0);
}

static void ne_tick_cb(lv_timer_t *t)
{
    ne_page_t *p = lv_timer_get_user_data(t);
    if (p->dirty && lv_tick_get() - p->last_key >= AUTOSAVE_MS) {
        save(p);
    }
}

static void ne_free_cb(lv_event_t *e)
{
    ne_page_t *p = lv_event_get_user_data(e);
    save(p); /* al salir se guarda lo que quede (noteBackToList) */
    lv_free(p);
}

lv_obj_t *ui_note_edit_create(lv_obj_t *parent)
{
    ne_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    snprintf(p->path, sizeof(p->path), "%s", s_note_path);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    char stem[FL_NAME_MAX], t[FL_NAME_MAX + 32];
    fl_stem(p->path, stem, sizeof(stem));
    ui_phone_text(stem[0] ? stem : "Nota", t, sizeof(t), FLEX_FONT_TITLE);
    watch_theme_header(page, t, back_cb, NULL);

    char *buf = lv_malloc(FFS_TEXT_MAX);
    LV_ASSERT_MALLOC(buf);
    const bool ok = ui_fs_ready(NULL) && ffs_read_text(p->path, buf, FFS_TEXT_MAX) >= 0;
    if (!ok) {
        lv_free(buf);
        lv_obj_t *list = watch_theme_list(page);
        ui_fs_notice(list, "Notas", ui_fs_ready(NULL) ? "Esta nota ya no existe" : "El almacenamiento no está montado");
        p->status = NULL;
        lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
        return page;
    }
    fl_utf8_trim(buf);
    p->ta = lv_textarea_create(page);
    lv_textarea_set_max_length(p->ta, (FFS_TEXT_MAX - 1) / 2); /* caracteres de hasta 2 B (teclado) */
    lv_textarea_set_text(p->ta, buf);
    lv_free(buf);
    lv_textarea_set_placeholder_text(p->ta, "Escribe aquí");
    lv_obj_set_size(p->ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 236 - FLEX_HDR_H - 34);
    lv_obj_set_pos(p->ta, FLEX_SAFE_X, FLEX_HDR_H);
    ui_textarea_style(p->ta);
    lv_obj_set_style_text_font(p->ta, FLEX_FONT_SMALL, 0);
    lv_obj_set_style_pad_ver(p->ta, 8, 0);
    p->status = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE, "Guardado");
    lv_obj_set_pos(p->status, FLEX_SAFE_X + 8, 236 - 30);
    lv_obj_t *kb = ui_keyboard_create(page, p->ta, UI_KB_TEXT | UI_KB_NEWLINE, NULL, NULL);
    lv_obj_set_size(kb, FLEX_SCREEN_W - 2 * 16, 452 - 236);
    lv_obj_set_pos(kb, 16, 236);
    lv_obj_add_event_cb(p->ta, changed_cb, LV_EVENT_VALUE_CHANGED, p);
    lv_obj_add_event_cb(page, ne_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, ne_tick_cb, NOTES_TICK_MS, p);
    return page;
}
