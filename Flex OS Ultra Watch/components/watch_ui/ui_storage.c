/*
 * ui_storage — app Almacenamiento (composición de design/referencias/
 * almacenamiento.png, con el Flex Liquid Glass Engine y la escala tipográfica
 * del sistema: sin negritas, barras finas y una sola tarjeta discreta) y su
 * página de detalles (F4.6).
 *
 * Página principal, todo con datos reales:
 *  - "Memoria interna": la flash de 32 MB (tamaño leído del chip). Ocupado =
 *    las particiones del sistema (firmware, copia de actualización, recursos,
 *    ajustes: todo lo que no es "storage") + lo usado en LittleFS. Sin
 *    LittleFS montado se dice y solo se cuenta el sistema;
 *  - "PSRAM": total y libre del montón de PSRAM (watch_diag_mem);
 *  - tarjeta "Memoria y sistema" con el estado de la memoria
 *    (ui_mem_health) que abre los detalles, y "Optimizar": suelta lo que se
 *    puede regenerar (fondo de la última vista previa del selector de esferas,
 *    caché de imágenes de LVGL, app suspendida) y dice cuánto se liberó. No
 *    borra archivos;
 *  - leyenda por categorías (stg_usage, en la tarea del almacenamiento: no
 *    bloquea la interfaz) que abre el explorador de Documentos y la Papelera.
 *
 * Página de detalles (Ajustes > Almacenamiento de F4.6): LittleFS en la
 * partición "storage" (16 MB por encima de 16 MB).
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
#include <string.h>
#include "files_logic.h"
#include "ui_logic.h"
#include "watch_diag.h"
#include "watch_media.h"
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

lv_obj_t *ui_storage_details_create(lv_obj_t *parent)
{
    stp_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Detalles", back_cb, NULL);
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

/* ======================= Página principal (diseño del usuario) ======================= */

#define MAIN_TICK_MS 1000
/* Geometría para 410x502 (las esquinas redondeadas mandan arriba y abajo) */
#define X0           34
#define BAR_W        342
#define BAR_H        14          /* barra fina en píldora: dato, no bloque */
#define SEC1_Y       74          /* "Memoria interna" */
#define SEC2_Y       142         /* "PSRAM" */
#define SEC3_Y       210         /* "microSD" (abre su página) */
#define BAR_DY       36          /* de la etiqueta a su barra */
#define BODY_Y       64          /* el cuerpo desplazable empieza bajo el título */
#define BY(y)        ((y) - BODY_Y)
#define CARD_X       30
#define CARD_Y       282
#define CARD_W       350
#define CARD_H       84
#define CARD_R       26
#define OPT_W        128
#define OPT_H        46
#define LEG_Y0       400 /* centro de la primera fila de la leyenda */
#define LEG_DY       34
#define LEG_DOT      12
#define C_TRACK      0x2C2E34    /* pista de las barras */
#define C_PSRAM      0x64D2FF    /* PSRAM: cian sereno (antes lima a toda saturación) */
#define C_SD         0xF0AA32    /* microSD: el ámbar de "Pantalla" (FLEX_C_CAT_SUN) */

enum { LEG_DOCS = 0, LEG_SYS, LEG_APPS, LEG_TRASH, LEG_N };

typedef struct {
    lv_obj_t *flash_val, *flash_bar, *psram_val, *psram_bar, *sd_val, *sd_bar;
    lv_obj_t *health_dot, *health;
    lv_obj_t *leg_val[LEG_N];
    lv_obj_t *leg_row[LEG_N];
    uint32_t usage_gen;
    bool asked;
} stm_t;

static uint64_t flash_total(void)
{
    const watch_flash_report_t *f = &ui_boot_info()->flash;
    return f->physical_size ? f->physical_size : f->configured_size;
}

/* Lo que ocupan las particiones del sistema (todo lo que no es "storage"). */
static uint64_t system_bytes(void)
{
    const watch_flash_report_t *f = &ui_boot_info()->flash;
    const uint64_t total = flash_total();
    return (f->storage_found && f->storage_size <= total) ? total - f->storage_size : total;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void set_bar(lv_obj_t *bar, int32_t permille)
{
    if (lv_bar_get_value(bar) != permille) {
        lv_bar_set_value(bar, permille, LV_ANIM_OFF);
    }
}

static void main_refresh(stm_t *p)
{
    char b[40];
    watch_storage_info_t in;
    watch_storage_info(&in);
    const bool mounted = in.state == STG_MOUNTED;
    if (mounted && !p->asked) {
        p->asked = true;
        watch_storage_request(STG_REQ_USAGE); /* recuento en la tarea del almacenamiento */
    }
    /* Memoria interna (flash) */
    const uint64_t total = flash_total(), sys = system_bytes();
    if (total == 0) {
        set_text(p->flash_val, "--");
        set_bar(p->flash_bar, 0);
    } else if (mounted) {
        ui_fmt_usage(b, sizeof(b), sys + in.used, total);
        set_text(p->flash_val, b);
        set_bar(p->flash_bar, ui_permille(sys + in.used, total));
    } else {
        set_text(p->flash_val, "Datos sin montar");
        set_bar(p->flash_bar, ui_permille(sys, total));
    }
    /* PSRAM */
    watch_mem_report_t m;
    watch_diag_mem(&m);
    const uint64_t pt = m.psram_total, pu = m.psram_total > m.psram_free ? m.psram_total - m.psram_free : 0;
    ui_fmt_usage(b, sizeof(b), pu, pt);
    set_text(p->psram_val, b);
    set_bar(p->psram_bar, ui_permille(pu, pt));
    /* microSD (watch_media): uso real o por qué no se puede usar */
    wm_sd_info_t sd;
    wm_sd_info(&sd);
    if (sd.state == WSD_READY && sd.space_valid && sd.total_bytes) {
        char f[16], t[16];
        const uint64_t used = sd.total_bytes > sd.free_bytes ? sd.total_bytes - sd.free_bytes : 0;
        wm_fmt_bytes(used, f, sizeof(f));
        wm_fmt_bytes(sd.total_bytes, t, sizeof(t));
        char sb[48];
        snprintf(sb, sizeof(sb), "%s/%s  (%d%%)", f, t, (int)(ui_permille(used, sd.total_bytes) / 10));
        set_text(p->sd_val, sb);
        set_bar(p->sd_bar, ui_permille(used, sd.total_bytes));
    } else {
        const char *t = sd.state == WSD_ABSENT ? (sd.ejected ? "Expulsada" : "Sin tarjeta")
                        : sd.state == WSD_ERROR ? (sd.err == WSD_E_EXFAT || sd.err == WSD_E_NTFS || sd.err == WSD_E_GPT
                                                       ? "Formato no compatible"
                                                       : "No se puede usar")
                        : sd.state == WSD_READY ? "Lista"
                                                : "Comprobando…";
        set_text(p->sd_val, t);
        set_bar(p->sd_bar, 0);
    }
    /* Estado de la memoria */
    const ui_mem_health_t h = ui_mem_health(m.internal_free, m.internal_largest, m.psram_largest, m.alloc_fails);
    set_text(p->health, ui_mem_health_text(h));
    watch_theme_set_bg_color(p->health_dot, h == UI_MEM_OK ? FLEX_ST_APPS : (h == UI_MEM_TIGHT ? FLEX_C_WARN : FLEX_C_ERR));
    /* Leyenda */
    fl_fmt_size((uint32_t)(sys > UINT32_MAX ? UINT32_MAX : sys), b, sizeof(b));
    set_text(p->leg_val[LEG_SYS], total ? b : "--");
    if (mounted && in.usage_gen) {
        const uint64_t docs = in.usage.docs, trash = in.usage.trash;
        const uint64_t apps = in.used > docs + trash ? in.used - docs - trash : 0;
        fl_fmt_size((uint32_t)docs, b, sizeof(b));
        set_text(p->leg_val[LEG_DOCS], b);
        fl_fmt_size((uint32_t)apps, b, sizeof(b));
        set_text(p->leg_val[LEG_APPS], b);
        fl_fmt_size((uint32_t)trash, b, sizeof(b));
        set_text(p->leg_val[LEG_TRASH], b);
    } else {
        const char *t = mounted ? "Contando…" : "--";
        set_text(p->leg_val[LEG_DOCS], t);
        set_text(p->leg_val[LEG_APPS], t);
        set_text(p->leg_val[LEG_TRASH], t);
    }
    for (int i = 0; i < LEG_N; i++) {
        if (i == LEG_DOCS || i == LEG_TRASH) {
            if (mounted) {
                lv_obj_remove_state(p->leg_row[i], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(p->leg_row[i], LV_STATE_DISABLED);
            }
        }
    }
    p->usage_gen = in.usage_gen;
}

static void main_tick_cb(lv_timer_t *t)
{
    main_refresh(lv_timer_get_user_data(t));
}

static void details_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_storage_details_create);
}

static void sd_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_sd_create);
}

static void clean_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_media_clean_create);
}

static void sd_interest_off_cb(lv_event_t *e)
{
    (void)e;
    wm_sd_interest(false);
}

static void optimize_cb(lv_event_t *e)
{
    stm_t *p = lv_event_get_user_data(e);
    watch_mem_report_t a, b;
    watch_diag_mem(&a);
    size_t freed = watch_theme_release_caches();
    if (ui_nav_suspended_count() > 0) {
        ui_nav_discard_suspended(); /* la app que quedó en segundo plano */
    }
    watch_diag_mem(&b);
    const int64_t measured = ((int64_t)b.psram_free + b.internal_free) - ((int64_t)a.psram_free + a.internal_free);
    if (measured > (int64_t)freed) {
        freed = (size_t)measured;
    }
    char t[48];
    if (freed > 0) {
        char sz[16];
        ui_fmt_bytes(sz, sizeof(sz), (uint32_t)freed);
        snprintf(t, sizeof(t), "Liberados %s de memoria", sz);
    } else {
        snprintf(t, sizeof(t), "No había nada que liberar");
    }
    ui_banner_show_info("Optimizado", t);
    main_refresh(p);
}

static void browse_main_cb(lv_event_t *e)
{
    ui_files_open_dir(lv_event_get_user_data(e));
}

static lv_obj_t *label_at(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, const char *t, int32_t x, int32_t y)
{
    lv_obj_t *l = watch_theme_label(parent, f, rgb, t);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *value_right(lv_obj_t *parent, int32_t right, int32_t y)
{
    lv_obj_t *l = watch_theme_label(parent, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "--");
    lv_obj_set_width(l, 200);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l, right - 200, y);
    return l;
}

static void main_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_storage_create(lv_obj_t *parent)
{
    stm_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    const int32_t lh_body = lv_font_get_line_height(FLEX_TYPE_BODY);
    const int32_t lh_cap = lv_font_get_line_height(FLEX_TYPE_CAPTION);

    /* Con la microSD la composición pasa del alto de la pantalla: va en un
     * cuerpo que se desplaza en vertical, bajo el título fijo (BODY_Y) y hasta
     * FLEX_LIST_BOTTOM px antes del borde, como las listas, para que las
     * esquinas no muerdan lo que se desplaza. Las coordenadas de abajo son de
     * la pantalla; BY() las pasa al cuerpo. */
    lv_obj_t *body = lv_obj_create(page);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, BODY_Y);
    lv_obj_set_size(body, FLEX_SCREEN_W, FLEX_SCREEN_H - BODY_Y - FLEX_LIST_BOTTOM);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE); /* LVGL solo desplaza desde un objeto pulsable */

    lv_obj_t *title = watch_theme_label(page, FLEX_TYPE_TITLE, FLEX_GLASS_TXT, "Almacenamiento");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* Dos medidas: etiqueta regular, valor en gris a la derecha y una barra
     * fina debajo (los valores alineados por su línea base) */
    label_at(body, FLEX_TYPE_BODY, FLEX_GLASS_TXT, "Memoria interna", X0, BY(SEC1_Y));
    p->flash_val = value_right(body, X0 + BAR_W, BY(SEC1_Y) + lh_body - lh_cap - 1);
    p->flash_bar = watch_theme_bar(body, BAR_W, BAR_H, FLEX_ST_FLASH, C_TRACK);
    lv_obj_set_pos(p->flash_bar, X0, BY(SEC1_Y) + BAR_DY);

    label_at(body, FLEX_TYPE_BODY, FLEX_GLASS_TXT, "PSRAM", X0, BY(SEC2_Y));
    p->psram_val = value_right(body, X0 + BAR_W, BY(SEC2_Y) + lh_body - lh_cap - 1);
    p->psram_bar = watch_theme_bar(body, BAR_W, BAR_H, C_PSRAM, C_TRACK);
    lv_obj_set_pos(p->psram_bar, X0, BY(SEC2_Y) + BAR_DY);

    /* microSD: la misma medida, toda la franja abre su página (estado,
     * expulsar, formatear y "Guardar en"). */
    lv_obj_t *sd_row = flex_glass_panel(body, FLEX_GLASS_GHOST, X0 - 8, BY(SEC3_Y) - 6, BAR_W + 16, BAR_DY + BAR_H + 14, 14);
    flex_glass_interactive(sd_row);
    lv_obj_add_event_cb(sd_row, sd_open_cb, LV_EVENT_CLICKED, p);
    label_at(sd_row, FLEX_TYPE_BODY, FLEX_GLASS_TXT, "microSD", 8, 6 - FLEX_GLASS_RIM_W);
    p->sd_val = value_right(sd_row, BAR_W + 8, 6 - FLEX_GLASS_RIM_W + lh_body - lh_cap - 1);
    p->sd_bar = watch_theme_bar(sd_row, BAR_W, BAR_H, C_SD, C_TRACK);
    lv_obj_set_pos(p->sd_bar, 8, 6 - FLEX_GLASS_RIM_W + BAR_DY);
    lv_obj_remove_flag(p->sd_bar, LV_OBJ_FLAG_CLICKABLE);
    wm_sd_interest(true);
    lv_obj_add_event_cb(body, sd_interest_off_cb, LV_EVENT_DELETE, NULL);

    /* Tarjeta de contenido (material discreto del motor): toda la tarjeta
     * abre los detalles; "Optimizar" es la acción principal (tintada). */
    lv_obj_t *card = flex_glass_panel(body, FLEX_GLASS_CARD, CARD_X, BY(CARD_Y), CARD_W, CARD_H, CARD_R);
    flex_glass_interactive(card);
    lv_obj_add_event_cb(card, details_cb, LV_EVENT_CLICKED, p);
    /* Título de tarjeta en Medium 20 (como las notificaciones): deja aire
     * hasta "Optimizar"; título y estado, centrados en la tarjeta */
    const int32_t lh_t = lv_font_get_line_height(FLEX_TYPE_LABEL);
    const int32_t ty = (CARD_H - 2 * FLEX_GLASS_RIM_W - (lh_t + 4 + lh_cap)) / 2;
    lv_obj_t *ct = watch_theme_label(card, FLEX_TYPE_LABEL, FLEX_GLASS_TXT, "Memoria y sistema");
    lv_obj_set_pos(ct, 18, ty);
    p->health_dot = lv_obj_create(card);
    lv_obj_remove_style_all(p->health_dot);
    lv_obj_set_size(p->health_dot, 9, 9);
    lv_obj_set_style_radius(p->health_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(p->health_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p->health_dot, flex_color(FLEX_ST_APPS), 0);
    lv_obj_remove_flag(p->health_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(p->health_dot, 19, ty + lh_t + 4 + (lh_cap - 9) / 2);
    p->health = label_at(card, FLEX_TYPE_CAPTION, FLEX_GLASS_TXT2, "", 34, ty + lh_t + 4);
    lv_obj_t *opt = flex_glass_button(card, "Optimizar", FLEX_C_PRIMARY, OPT_W, OPT_H);
    lv_obj_align(opt, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_ext_click_area(opt, 8);
    lv_obj_add_event_cb(opt, optimize_cb, LV_EVENT_CLICKED, p);

    /* Leyenda por categorías: punto de color, nombre regular y tamaño en gris */
    static const struct {
        const char *name;
        uint32_t rgb;
        const char *dir; /* explorador al tocarla (NULL: detalles) */
    } kLeg[LEG_N] = {
        {"Documentos", FLEX_ST_DOCS, "/"},
        {"Sistema", FLEX_ST_SYSTEM, NULL},
        {"Aplicaciones", FLEX_ST_APPS, NULL},
        {"Papelera", FLEX_ST_TRASH, "/Papelera"},
    };
    for (int i = 0; i < LEG_N; i++) {
        const int32_t cy = LEG_Y0 + i * LEG_DY;
        /* Fila fantasma: se ilumina como vidrio solo mientras se toca */
        lv_obj_t *row = flex_glass_panel(body, FLEX_GLASS_GHOST, X0 - 8, BY(cy) - LEG_DY / 2, BAR_W + 16, LEG_DY, 12);
        flex_glass_interactive(row);
        if (kLeg[i].dir) {
            lv_obj_add_event_cb(row, browse_main_cb, LV_EVENT_CLICKED, (void *)kLeg[i].dir);
        } else {
            lv_obj_add_event_cb(row, details_cb, LV_EVENT_CLICKED, p);
        }
        p->leg_row[i] = row;
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, LEG_DOT, LEG_DOT);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, flex_color(kLeg[i].rgb), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(dot, 10, (LEG_DY - 2 - LEG_DOT) / 2);
        lv_obj_t *n = watch_theme_label(row, FLEX_TYPE_BODY, FLEX_GLASS_TXT, kLeg[i].name);
        lv_obj_set_pos(n, 34, (LEG_DY - 2 - lh_body) / 2);
        p->leg_val[i] = value_right(row, BAR_W + 4, (LEG_DY - 2 - lh_cap) / 2);
    }

    /* Liberar espacio: los archivos multimedia más grandes del reloj y de la
     * microSD, que la persona borra con dos toques (ui_media_clean_create). */
    const int32_t cy = LEG_Y0 + LEG_N * LEG_DY + 8;
    lv_obj_t *clean = flex_glass_panel(body, FLEX_GLASS_GHOST, X0 - 8, BY(cy) - LEG_DY / 2, BAR_W + 16, LEG_DY, 12);
    flex_glass_interactive(clean);
    lv_obj_add_event_cb(clean, clean_open_cb, LV_EVENT_CLICKED, p);
    lv_obj_t *ci = flex_icon_create(clean, FLEX_ICON_TRASH, 18, FLEX_C_ERR);
    lv_obj_set_pos(ci, 7, (LEG_DY - 2 - 18) / 2);
    lv_obj_t *cn = watch_theme_label(clean, FLEX_TYPE_BODY, FLEX_GLASS_TXT, "Liberar espacio");
    lv_obj_set_pos(cn, 34, (LEG_DY - 2 - lh_body) / 2);
    lv_obj_t *cv = value_right(clean, BAR_W + 4, (LEG_DY - 2 - lh_cap) / 2);
    lv_label_set_text(cv, "Multimedia");

    /* margen bajo la última fila */
    lv_obj_t *pad = lv_obj_create(body);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, 1);
    lv_obj_set_pos(pad, 0, BY(cy) + LEG_DY / 2 + 12);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_CLICKABLE);

    main_refresh(p);
    lv_obj_add_event_cb(page, main_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, main_tick_cb, MAIN_TICK_MS, p);
    return page;
}
