/*
 * ui_storage — Ajustes > Almacenamiento (F4.6): LittleFS en la partición
 * "storage" (16 MB por encima de 16 MB).
 *
 * Todo lo que escribe es a petición y en este orden:
 *  1. "Probar escritura": un sector de prueba al final de storage, que
 *     comprueba que la zona baja no cambia (watch_storage).
 *  2. "Formatear": solo tras la prueba, con DOS toques (borra los datos de
 *     fábrica de esa zona).
 *  3. "Probar archivo": escritura atómica de un archivo con un contador que
 *     sobrevive al reinicio.
 * Con la puerta cerrada (chip, tamaño o sonda de 32 MB) no se ofrece nada y
 * se dice por qué.
 */
#include "ui_internal.h"

#include <stdio.h>
#include "ui_logic.h"
#include "watch_storage.h"
#include "watch_theme.h"

#define STG_TICK_MS     300
#define CONFIRM_MS      4000

typedef struct {
    lv_obj_t *state;
    lv_obj_t *space;
    lv_obj_t *sector;
    lv_obj_t *file;
    lv_obj_t *b_sector;
    lv_obj_t *b_format;
    lv_obj_t *b_file;
    lv_obj_t *b_browse;
    lv_obj_t *b_trash;
    uint32_t confirm_ms; /* primer toque de "Formatear" (0 = no) */
} stp_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void enable(lv_obj_t *row, bool on)
{
    if (on) {
        lv_obj_remove_state(row, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(row, LV_STATE_DISABLED);
    }
}

static void refresh(stp_t *p)
{
    char buf[48];
    watch_storage_info_t in;
    watch_storage_info(&in);
    const bool open = in.gate == STG_GATE_OK;
    switch (in.state) {
    case STG_MOUNTED:     watch_theme_info_set(p->state, "Montado (LittleFS)"); break;
    case STG_UNFORMATTED: watch_theme_info_set(p->state, "Sin formato"); break;
    case STG_BUSY:        watch_theme_info_set(p->state, "Trabajando…"); break;
    case STG_ERROR:       watch_theme_info_set(p->state, in.detail ? in.detail : "Error"); break;
    default:              watch_theme_info_set(p->state, open ? "Sin arrancar" : stg_gate_text(in.gate)); break;
    }
    if (in.state == STG_MOUNTED) {
        char u[16], t[16];
        ui_fmt_bytes(u, sizeof(u), (uint32_t)in.used);
        ui_fmt_bytes(t, sizeof(t), (uint32_t)in.total);
        snprintf(buf, sizeof(buf), "%s de %s", u, t);
        watch_theme_info_set(p->space, buf);
    } else {
        watch_theme_info_set(p->space, "--");
    }
    watch_theme_info_set(p->sector, in.sector_test == 0 ? (in.state == STG_MOUNTED ? "No hace falta" : "Sin hacer")
                                    : in.sector_test > 0 ? "Superada"
                                                         : (in.detail ? in.detail : "Fallida"));
    if (in.file_test > 0) {
        snprintf(buf, sizeof(buf), "Bien (n.º %lu)", (unsigned long)in.file_count);
        watch_theme_info_set(p->file, buf);
    } else {
        watch_theme_info_set(p->file, in.file_test < 0 ? "Fallida" : "Sin hacer");
    }
    const bool busy = in.state == STG_BUSY;
    enable(p->b_sector, open && !busy && in.state != STG_MOUNTED && in.sector_test != 1);
    enable(p->b_format, open && !busy && in.state != STG_MOUNTED && in.sector_test == 1);
    enable(p->b_file, open && !busy && in.state == STG_MOUNTED);
    enable(p->b_browse, in.state == STG_MOUNTED);
    enable(p->b_trash, in.state == STG_MOUNTED);
    if (p->confirm_ms && lv_tick_elaps(p->confirm_ms) > CONFIRM_MS) {
        p->confirm_ms = 0;
        watch_theme_row_set_value(p->b_format, NULL);
    }
}

static void browse_cb(lv_event_t *e)
{
    ui_files_open_dir(lv_event_get_user_data(e));
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void sector_cb(lv_event_t *e)
{
    watch_storage_request(STG_REQ_SECTOR_TEST);
    refresh(lv_event_get_user_data(e));
}

static void format_cb(lv_event_t *e)
{
    stp_t *p = lv_event_get_user_data(e);
    if (!p->confirm_ms) {
        p->confirm_ms = lv_tick_get();
        if (!p->confirm_ms) {
            p->confirm_ms = 1;
        }
        watch_theme_row_set_value(p->b_format, "Toca otra vez");
        return;
    }
    p->confirm_ms = 0;
    watch_theme_row_set_value(p->b_format, NULL);
    watch_storage_request(STG_REQ_FORMAT);
    refresh(p);
}

static void file_cb(lv_event_t *e)
{
    watch_storage_request(STG_REQ_FILE_TEST);
    refresh(lv_event_get_user_data(e));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_storage_create(lv_obj_t *parent)
{
    stp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Almacenamiento", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Partición storage (16 MB)");
    p->state = watch_theme_info_row(list, "Estado", "--");
    p->space = watch_theme_info_row(list, "Espacio", "--");
    p->sector = watch_theme_info_row(list, "Prueba del sector", "--");
    p->file = watch_theme_info_row(list, "Archivo de prueba", "--");

    /* Explorador (F9.6, AppFiles.h): lo que hay de verdad en el disco */
    watch_theme_section(list, "Archivos");
    p->b_browse = watch_theme_list_row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_SUN, "Explorar archivos", NULL, true);
    lv_obj_add_event_cb(p->b_browse, browse_cb, LV_EVENT_CLICKED, (void *)"/");
    p->b_trash = watch_theme_list_row(list, FLEX_ICON_CROSS, FLEX_C_MUTE, "Papelera", NULL, true);
    lv_obj_add_event_cb(p->b_trash, browse_cb, LV_EVENT_CLICKED, (void *)"/Papelera");

    watch_theme_section(list, "Acciones");
    p->b_sector = watch_theme_list_row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_BLUE, "Probar escritura", NULL, false);
    lv_obj_add_event_cb(p->b_sector, sector_cb, LV_EVENT_CLICKED, p);
    p->b_format = watch_theme_list_row(list, FLEX_ICON_CROSS, FLEX_C_ERR, "Formatear", NULL, false);
    lv_obj_add_event_cb(p->b_format, format_cb, LV_EVENT_CLICKED, p);
    p->b_file = watch_theme_list_row(list, FLEX_ICON_CHECK, FLEX_C_OK, "Probar archivo", NULL, false);
    lv_obj_add_event_cb(p->b_file, file_cb, LV_EVENT_CLICKED, p);
    lv_obj_t *n = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                    "Formatear borra los datos de fábrica de los 16 MB altos (la imagen de "
                                    "fábrica se puede volver a grabar). Solo tras la prueba de escritura.");
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(n, LV_PCT(100));
    lv_obj_set_style_pad_left(n, 16, 0);

    refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, STG_TICK_MS, p);
    return page;
}
