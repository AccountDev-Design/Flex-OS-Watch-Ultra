/*
 * ui_files — explorador de archivos (F9.6) y las opciones de un archivo
 * que comparten Notas y el explorador.
 *
 * Como en la referencia (FlexOS_Ultra_AppFiles.h, FileKit.h):
 *  - lista lo que HAY: el conteo de una carpeta sale de abrirla;
 *  - las acciones (renombrar, papelera, borrar, restaurar) operan sobre el
 *    archivo real y la lista se rehace desde el disco después;
 *  - borrar para siempre pide confirmación (aquí, un segundo toque);
 *  - sin almacenamiento montado se dice por qué, no se enseña una lista
 *    vacía (fkNoFsScreen).
 * Abrir un .txt lo lleva al editor de Notas; lo que el reloj no sabe abrir
 * lo dice ("El reloj no abre este tipo de archivo").
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "flexfs.h"
#include "watch_storage.h"
#include "watch_theme.h"

#define FILES_TICK_MS 500
#define CONFIRM_MS    3000

static char s_next_dir[FL_PATH_MAX] = "/"; /* carpeta de la próxima página */
static char s_sel[FL_PATH_MAX];            /* archivo de la página de opciones */
static uint32_t s_gen = 1;                 /* cambia con cada operación: las listas se rehacen */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

void ui_files_changed(void)
{
    s_gen++;
}

uint32_t ui_files_gen(void)
{
    return s_gen;
}

bool ui_fs_ready(const char **why)
{
    watch_storage_info_t in;
    watch_storage_info(&in);
    if (in.state != STG_MOUNTED || !ffs_ready()) {
        if (why) {
            *why = in.state == STG_UNFORMATTED ? "La partición de datos no tiene formato: Almacenamiento > Formatear"
                   : in.state == STG_BUSY      ? "El almacenamiento está ocupado; vuelve a intentarlo"
                   : in.gate != STG_GATE_OK    ? stg_gate_text(in.gate)
                   : (in.detail ? in.detail : "El almacenamiento no está montado");
        }
        return false;
    }
    static bool dirs;
    if (!dirs) {
        ffs_init_dirs(); /* /Notas, /Documentos, /Paint, /Papelera */
        dirs = true;
    }
    return true;
}

lv_obj_t *ui_fs_notice(lv_obj_t *list, const char *app, const char *why)
{
    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    char t[64];
    snprintf(t, sizeof(t), "%s sin almacenamiento", app);
    watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, t);
    lv_obj_t *w = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT2, why ? why : "");
    lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(w, LV_PCT(100));
    return c;
}

void ui_files_open_dir(const char *dir)
{
    snprintf(s_next_dir, sizeof(s_next_dir), "%s", dir);
    ui_nav_push(ui_files_create);
}

void ui_file_options(const char *path)
{
    snprintf(s_sel, sizeof(s_sel), "%s", path);
    ui_nav_push(ui_file_opts_create);
}

static bool in_trash(const char *path)
{
    const size_t n = strlen(FFS_DIR_TRASH);
    return !strncmp(path, FFS_DIR_TRASH, n) && (path[n] == '/' || path[n] == 0);
}

/* ======================= Explorador ======================= */

typedef struct {
    char dir[FL_PATH_MAX];
    lv_obj_t *list;
    uint32_t gen;
    bool ready;
    lv_obj_t *empty_btn;
    uint32_t empty_at;
} fl_page_t;

static void entry_cb(lv_event_t *e)
{
    fl_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    const char *name = lv_obj_get_user_data(row);
    char path[FL_PATH_MAX];
    if (!name || !fl_join(p->dir, name, path, sizeof(path))) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        ui_file_options(path);
        return;
    }
    if (ffs_is_dir(path) && !in_trash(path)) {
        ui_files_open_dir(path);
    } else if (!in_trash(path) && !strcmp(fl_ext(path), FFS_EXT_NOTE)) {
        ui_note_open(path);
    } else {
        ui_file_options(path);
    }
}

static void empty_cb(lv_event_t *e)
{
    fl_page_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (p->empty_at && now - p->empty_at < CONFIRM_MS) {
        p->empty_at = 0;
        ffs_empty_trash();
        ui_files_changed();
        return;
    }
    p->empty_at = now;
    watch_theme_button_set(p->empty_btn, "Toca otra vez para vaciar", FLEX_C_DANGER);
}

static void name_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void build(fl_page_t *p)
{
    lv_obj_clean(p->list);
    p->empty_btn = NULL;
    const char *why = NULL;
    p->ready = ui_fs_ready(&why);
    if (!p->ready) {
        ui_fs_notice(p->list, "Archivos", why);
        return;
    }
    lv_obj_t *path = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE, p->dir);
    lv_label_set_long_mode(path, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(path, LV_PCT(100));
    lv_obj_set_style_pad_left(path, 8, 0);
    ffs_entry_t *e = lv_malloc(sizeof(ffs_entry_t) * FFS_LIST_MAX);
    LV_ASSERT_MALLOC(e);
    const int n = ffs_list(p->dir, e, FFS_LIST_MAX);
    const bool trash = in_trash(p->dir);
    if (n <= 0) {
        watch_theme_label(p->list, FLEX_FONT_BODY, FLEX_C_TXT2, n < 0 ? "La carpeta ya no existe" : "Carpeta vacía");
    }
    for (int i = 0; i < n; i++) {
        char val[32], title[FL_PATH_MAX];
        if (e[i].dir) {
            snprintf(val, sizeof(val), "%u %s", e[i].items, e[i].items == 1 ? "elemento" : "elementos");
        } else {
            fl_fmt_size(e[i].size, val, sizeof(val));
        }
        /* En la papelera se enseña de dónde vino */
        if (!trash || !fl_trash_origin(e[i].name, title, sizeof(title))) {
            snprintf(title, sizeof(title), "%s", e[i].name);
        }
        char shown[FL_PATH_MAX + 16];
        ui_phone_text(title, shown, sizeof(shown), FLEX_FONT_BODY);
        lv_obj_t *r = watch_theme_list_row(p->list, e[i].dir ? FLEX_ICON_STORAGE : FLEX_ICON_INFO,
                                           e[i].dir ? FLEX_C_CAT_SUN : FLEX_C_CAT_BLUE, shown, val,
                                           e[i].dir && !trash);
        char *nm = lv_malloc(strlen(e[i].name) + 1);
        LV_ASSERT_MALLOC(nm);
        strcpy(nm, e[i].name);
        lv_obj_set_user_data(r, nm);
        lv_obj_add_event_cb(r, name_free_cb, LV_EVENT_DELETE, nm);
        lv_obj_add_event_cb(r, entry_cb, LV_EVENT_CLICKED, p);
        lv_obj_add_event_cb(r, entry_cb, LV_EVENT_LONG_PRESSED, p);
    }
    lv_free(e);
    if (trash && n > 0) {
        p->empty_btn = watch_theme_button(p->list, "Vaciar la papelera", FLEX_C_SURF2, LV_PCT(100), 60);
        lv_obj_add_event_cb(p->empty_btn, empty_cb, LV_EVENT_CLICKED, p);
    }
    lv_obj_t *hint = watch_theme_label(p->list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       trash ? "Toca un elemento para restaurarlo o borrarlo."
                                             : "Mantén pulsado un elemento para renombrarlo o tirarlo.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_pad_left(hint, 8, 0);
}

static void fl_tick_cb(lv_timer_t *t)
{
    fl_page_t *p = lv_timer_get_user_data(t);
    const bool ready = ui_fs_ready(NULL);
    if (p->gen != s_gen || ready != p->ready) {
        p->gen = s_gen;
        build(p);
    }
    if (p->empty_at && lv_tick_get() - p->empty_at >= CONFIRM_MS) {
        p->empty_at = 0;
        watch_theme_button_set(p->empty_btn, "Vaciar la papelera", FLEX_C_SURF2);
    }
}

lv_obj_t *ui_files_create(lv_obj_t *parent)
{
    fl_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    snprintf(p->dir, sizeof(p->dir), "%s", s_next_dir);
    snprintf(s_next_dir, sizeof(s_next_dir), "/"); /* la siguiente, por defecto, la raíz */
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    char title[FL_NAME_MAX + 8];
    ui_phone_text(strcmp(p->dir, "/") ? fl_base(p->dir) : "Archivos", title, sizeof(title), FLEX_FONT_TITLE);
    watch_theme_header(page, title, back_cb, NULL);
    p->list = watch_theme_list(page);
    p->gen = s_gen;
    build(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, fl_tick_cb, FILES_TICK_MS, p);
    return page;
}

/* ======================= Opciones de un archivo ======================= */

typedef struct {
    char path[FL_PATH_MAX];
    lv_obj_t *page, *status, *del;
    uint32_t del_at;
} op_page_t;

static void op_done(op_page_t *p, bool ok, const char *fail)
{
    if (ok) {
        ui_files_changed();
        ui_nav_back();
    } else {
        lv_label_set_text(p->status, fail);
    }
}

static void op_open_cb(lv_event_t *e)
{
    op_page_t *p = lv_event_get_user_data(e);
    ui_note_open(p->path);
}

static void op_rename_cb(lv_event_t *e)
{
    op_page_t *p = lv_event_get_user_data(e);
    snprintf(s_sel, sizeof(s_sel), "%s", p->path);
    ui_nav_push(ui_file_rename_create);
}

static void op_trash_cb(lv_event_t *e)
{
    op_page_t *p = lv_event_get_user_data(e);
    op_done(p, ffs_trash(p->path), "No se pudo mover a la papelera (¿ruta demasiado larga?)");
}

static void op_restore_cb(lv_event_t *e)
{
    op_page_t *p = lv_event_get_user_data(e);
    op_done(p, ffs_restore(fl_base(p->path)), "No se pudo restaurar");
}

static void op_delete_cb(lv_event_t *e)
{
    op_page_t *p = lv_event_get_user_data(e);
    const uint32_t now = lv_tick_get();
    if (p->del_at && now - p->del_at < CONFIRM_MS) {
        op_done(p, ffs_delete(p->path), "No se pudo borrar");
        return;
    }
    p->del_at = now;
    watch_theme_button_set(p->del, "Toca otra vez para borrar", FLEX_C_DANGER);
}

static void op_tick_cb(lv_timer_t *t)
{
    op_page_t *p = lv_timer_get_user_data(t);
    /* Renombrado o borrado desde otra página: lo que enseña ya no existe.
     * Se cierra sola cuando vuelve a estar arriba (ui_nav_back no encadena
     * dos retrocesos mientras anima). */
    if (p->path[0] && !ffs_exists(p->path) && ui_nav_top() == p->page) {
        p->path[0] = 0;
        ui_nav_back();
        return;
    }
    if (p->del_at && lv_tick_get() - p->del_at >= CONFIRM_MS) {
        p->del_at = 0;
        watch_theme_button_set(p->del, "Borrar definitivamente", FLEX_C_SURF2);
    }
}

static lv_obj_t *op_button(lv_obj_t *list, const char *t, uint32_t rgb, lv_event_cb_t cb, op_page_t *p)
{
    lv_obj_t *b = watch_theme_button(list, t, rgb, LV_PCT(100), 60);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, p);
    return b;
}

lv_obj_t *ui_file_opts_create(lv_obj_t *parent)
{
    op_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    snprintf(p->path, sizeof(p->path), "%s", s_sel);
    const bool trash = in_trash(p->path);
    char shown[FL_PATH_MAX + 16], origin[FL_PATH_MAX];
    lv_obj_t *page = ui_page_new(parent);
    p->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Opciones", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    lv_obj_t *c = watch_theme_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    const bool orig = trash && fl_trash_origin(p->path, origin, sizeof(origin));
    ui_phone_text(orig ? fl_base(origin) : fl_base(p->path), shown, sizeof(shown), FLEX_FONT_BODY);
    lv_obj_t *nm = watch_theme_label(c, FLEX_FONT_BODY, FLEX_C_TXT, shown);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(nm, LV_PCT(100));
    char where[FL_PATH_MAX + 16];
    if (orig) {
        snprintf(where, sizeof(where), "Estaba en %s", origin);
    } else {
        fl_parent(p->path, where, sizeof(where));
    }
    ui_phone_text(where, shown, sizeof(shown), FLEX_FONT_SMALL);
    lv_obj_t *wl = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_MUTE, shown);
    lv_label_set_long_mode(wl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(wl, LV_PCT(100));

    if (trash) {
        op_button(list, "Restaurar", FLEX_C_PRIMARY, op_restore_cb, p);
    } else {
        if (!strcmp(fl_ext(p->path), FFS_EXT_NOTE)) {
            op_button(list, "Abrir", FLEX_C_PRIMARY, op_open_cb, p);
        } else if (!ffs_is_dir(p->path)) {
            watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "El reloj no abre este tipo de archivo");
        }
        op_button(list, "Renombrar", FLEX_C_SURF2, op_rename_cb, p);
        op_button(list, "Mover a la papelera", FLEX_C_SURF2, op_trash_cb, p);
    }
    p->del = op_button(list, "Borrar definitivamente", FLEX_C_SURF2, op_delete_cb, p);
    p->status = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_WARN, "");
    lv_label_set_long_mode(p->status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->status, LV_PCT(100));
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, op_tick_cb, FILES_TICK_MS, p);
    return page;
}

/* ======================= Renombrar ======================= */

typedef struct {
    char path[FL_PATH_MAX];
    lv_obj_t *ta, *status;
} rn_page_t;

static void rn_ok(void *ud)
{
    rn_page_t *p = ud;
    const char *nm = lv_textarea_get_text(p->ta);
    char np[FL_PATH_MAX];
    if (!fl_name_ok(nm)) {
        lv_label_set_text(p->status, "Nombre no válido: sin / ni @, ni espacios al principio o al final");
        return;
    }
    if (!ffs_rename(p->path, nm, np, sizeof(np))) {
        lv_label_set_text(p->status, "Ya hay otro con ese nombre");
        return;
    }
    ui_files_changed();
    ui_nav_back(); /* las opciones se cierran solas: su ruta ya no existe */
}

lv_obj_t *ui_file_rename_create(lv_obj_t *parent)
{
    rn_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    snprintf(p->path, sizeof(p->path), "%s", s_sel);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Renombrar", back_cb, NULL);
    p->ta = lv_textarea_create(page);
    lv_textarea_set_one_line(p->ta, true);
    lv_textarea_set_max_length(p->ta, (FL_NAME_MAX - 8) / 2); /* caracteres de hasta 2 B + extensión */
    char stem[FL_NAME_MAX];
    if (ffs_is_dir(p->path)) {
        snprintf(stem, sizeof(stem), "%s", fl_base(p->path));
    } else {
        fl_stem(p->path, stem, sizeof(stem));
    }
    lv_textarea_set_text(p->ta, stem);
    lv_obj_set_size(p->ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 56);
    lv_obj_set_pos(p->ta, FLEX_SAFE_X, FLEX_HDR_H + 4);
    ui_textarea_style(p->ta);
    p->status = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_WARN, "");
    lv_label_set_long_mode(p->status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->status, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 16);
    lv_obj_set_pos(p->status, FLEX_SAFE_X + 8, FLEX_HDR_H + 66);
    lv_obj_t *kb = ui_keyboard_create(page, p->ta, UI_KB_TEXT, rn_ok, p);
    lv_obj_set_size(kb, FLEX_SCREEN_W - 2 * 16, 452 - 236);
    lv_obj_set_pos(kb, 16, 236);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    return page;
}
