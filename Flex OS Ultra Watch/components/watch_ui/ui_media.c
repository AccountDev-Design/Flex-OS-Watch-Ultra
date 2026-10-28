/*
 * ui_media — piezas de interfaz que comparten las apps multimedia
 * (docs/MULTIMEDIA.md §9):
 *  - "Guardar en": el destino de lo nuevo (Reloj o microSD). Lo elige la
 *    persona y NUNCA cambia solo; si la microSD elegida no está, las apps lo
 *    dicen al intentar guardar;
 *  - Almacenamiento > microSD: estado real (watch_media), capacidad y
 *    espacio en 64 bits, expulsar, volver a montar y formatear en FAT32 con
 *    DOBLE confirmación (pantalla propia y segundo toque) y un ticket de un
 *    solo uso del servicio;
 *  - el chip de destino que enseñan la grabadora y la Galería.
 * Nada de esto hace E/S: lee el estado de los servicios y les pide cosas.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_logic.h"
#include "FlexOS_MediaLib.h"
#include "watch_media.h"
#include "watch_theme.h"

#define TICK_MS        400
#define CONFIRM_MS     5000

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* Mientras una de estas páginas está abierta, la microSD se mira más a menudo. */
static void interest_off_cb(lv_event_t *e)
{
    (void)e;
    wm_sd_interest(false);
}

static void want_interest(lv_obj_t *page)
{
    wm_sd_interest(true);
    lv_obj_add_event_cb(page, interest_off_cb, LV_EVENT_DELETE, NULL);
}

void ui_media_free_text(int vol, char *out, size_t n)
{
    const wm_vol_t v = (wm_vol_t)vol;
    wm_vol_info_t vi;
    if (!wm_vol_info(v, &vi)) {
        if (v == WM_VOL_SD) {
            /* Corto: va en el valor de una fila. El motivo entero lo dicen la
             * nota de "Guardar en" y la página de la microSD. */
            wm_sd_info_t i;
            wm_sd_info(&i);
            const char *t = "No disponible";
            switch (i.state) {
            case WSD_ABSENT:
                t = i.ejected ? "Expulsada" : "Sin tarjeta";
                break;
            case WSD_DETECTING:
            case WSD_MOUNTING:
                t = "Preparando…";
                break;
            case WSD_BUSY:
                t = "Ocupada";
                break;
            case WSD_REMOVING:
                t = "Expulsando…";
                break;
            default:
                break;
            }
            snprintf(out, n, "%s", t);
        } else {
            snprintf(out, n, "No disponible");
        }
        return;
    }
    if (!vi.total_bytes) {
        snprintf(out, n, "Lista");
        return;
    }
    char f[16];
    wm_fmt_bytes(vi.free_bytes, f, sizeof(f));
    snprintf(out, n, "%s libres", f);
}

void ui_copy_utf8(char *dst, size_t cap, const char *src)
{
    if (!cap) {
        return;
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) {
            n--; /* src[n] es el primer byte que no cabe: se retrocede hasta el inicio de su carácter */
        }
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

void ui_media_display_name(const void *rec, char *out, size_t n)
{
    const FlexMlRec *r = rec;
    const char *src = r->name[0] ? r->name : r->path;
    const char *slash = strrchr(src, '/');
    if (slash) {
        src = slash + 1;
    }
    snprintf(out, n, "%s", src);
    char *dot = strrchr(out, '.');
    if (dot && dot != out && strlen(dot) <= 5) {
        *dot = 0; /* "Grabación 27-10 18.04.wav" -> sin ".wav" */
    }
}

/* ======================= Guardar en ======================= */

typedef struct {
    lv_obj_t *row[WM_VOL_N];
    lv_obj_t *note;
    uint32_t sd_gen;
} dest_page_t;

static void dest_refresh(dest_page_t *p)
{
    wm_vol_t cur = WM_VOL_WATCH;
    const wm_dest_status_t st = wm_dest_get(&cur);
    static const flex_icon_t kIcon[WM_VOL_N] = {[WM_VOL_WATCH] = FLEX_ICON_WATCH, [WM_VOL_SD] = FLEX_ICON_SDCARD};
    for (int v = 0; v < WM_VOL_N; v++) {
        char free_t[64];
        ui_media_free_text((wm_vol_t)v, free_t, sizeof(free_t));
        watch_theme_row_set_value(p->row[v], free_t);
        /* el elegido lleva la marca (como la zona horaria elegida) */
        flex_icon_set(lv_obj_get_child(p->row[v], 0), (wm_vol_t)v == cur ? FLEX_ICON_CHECK : kIcon[v]);
    }
    set_text(p->note, st == WM_DEST_OK ? "Lo nuevo (grabaciones, fotos, lo que llega desde el móvil) se guarda aquí. "
                                         "Nunca se guarda en otro sitio sin preguntarte."
                                       : wm_dest_text(st));
    watch_theme_set_text_color(p->note, st == WM_DEST_OK ? FLEX_C_MUTE : FLEX_C_WARN);
}

static void dest_pick_cb(lv_event_t *e)
{
    wm_dest_set((wm_vol_t)(uintptr_t)lv_event_get_user_data(e));
    lv_obj_t *page = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_current_target_obj(e)));
    dest_refresh(lv_obj_get_user_data(page));
}

static void dest_tick_cb(lv_timer_t *t)
{
    dest_refresh(lv_timer_get_user_data(t));
}

lv_obj_t *ui_dest_create(lv_obj_t *parent)
{
    dest_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    lv_obj_set_user_data(page, p);
    watch_theme_header(page, "Guardar en", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    p->row[WM_VOL_WATCH] = watch_theme_list_row(list, FLEX_ICON_WATCH, FLEX_C_CAT_BLUE, "Reloj", "", false);
    p->row[WM_VOL_SD] = watch_theme_list_row(list, FLEX_ICON_SDCARD, FLEX_C_CAT_SUN, "microSD", "", false);
    for (int v = 0; v < WM_VOL_N; v++) {
        lv_obj_add_event_cb(p->row[v], dest_pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)v);
    }
    p->note = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    lv_label_set_long_mode(p->note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->note, LV_PCT(100));
    lv_obj_set_style_pad_left(p->note, 16, 0);
    lv_obj_set_style_pad_right(p->note, 16, 0);
    want_interest(page);
    dest_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, dest_tick_cb, TICK_MS * 2, p);
    return page;
}

/* ======================= chip de destino ======================= */

static void chip_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_dest_create);
}

lv_obj_t *ui_dest_chip(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *chip = flex_glass_button(parent, "", FLEX_GLASS_TINT_NONE, w, h);
    lv_obj_add_event_cb(chip, chip_cb, LV_EVENT_CLICKED, NULL);
    ui_dest_chip_refresh(chip, NULL);
    return chip;
}

void ui_dest_chip_refresh(lv_obj_t *chip, const char *extra)
{
    wm_vol_t v = WM_VOL_WATCH;
    const wm_dest_status_t st = wm_dest_get(&v);
    char t[96];
    if (st != WM_DEST_OK) {
        snprintf(t, sizeof(t), "%s: %s", wm_vol_name(v), st == WM_DEST_SD_ABSENT ? "sin tarjeta" : "no lista");
    } else if (extra && extra[0]) {
        snprintf(t, sizeof(t), "%s · %s", wm_vol_name(v), extra);
    } else {
        char f[48];
        ui_media_free_text(v, f, sizeof(f));
        snprintf(t, sizeof(t), "%s · %s", wm_vol_name(v), f);
    }
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
    watch_theme_set_text_color(l, st == WM_DEST_OK ? FLEX_GLASS_TXT : FLEX_C_WARN);
}

/* ======================= Almacenamiento > microSD ======================= */

typedef struct {
    lv_obj_t *state, *cap, *free_r, *kind, *speed;
    lv_obj_t *msg;
    lv_obj_t *b_eject, *b_retry, *b_format, *b_dest;
    uint32_t gen;
} sd_page_t;

static const char *kind_text(const wm_sd_info_t *i)
{
    switch (i->kind) {
    case WM_CARD_SDSC: return "SD";
    case WM_CARD_SDHC: return "SDHC";
    case WM_CARD_SDXC: return "SDXC";
    default:           return "--";
    }
}

static void sd_refresh(sd_page_t *p)
{
    wm_sd_info_t i;
    wm_sd_info(&i);
    char b[96];
    wm_sd_describe(&i, b, sizeof(b));
    watch_theme_info_set(p->state, i.state == WSD_READY ? "Lista" : i.state == WSD_ABSENT ? (i.ejected ? "Expulsada" : "Sin tarjeta")
                                   : i.state == WSD_ERROR ? "No se puede usar"
                                                          : wsd_state_name(i.state));
    if (i.card_valid) {
        wm_fmt_bytes(i.card_bytes, b, sizeof(b));
        watch_theme_info_set(p->cap, b);
        char k[40];
        if (i.fat_bits) {
            snprintf(k, sizeof(k), "%s · FAT%u", kind_text(&i), (unsigned)i.fat_bits);
        } else {
            snprintf(k, sizeof(k), "%s", kind_text(&i));
        }
        watch_theme_info_set(p->kind, k);
        snprintf(k, sizeof(k), "%u MHz", (unsigned)(i.freq_khz / 1000));
        watch_theme_info_set(p->speed, i.freq_khz ? k : "--");
    } else {
        watch_theme_info_set(p->cap, "--");
        watch_theme_info_set(p->kind, "--");
        watch_theme_info_set(p->speed, "--");
    }
    if (i.space_valid) {
        char f[16], t[16];
        wm_fmt_bytes(i.free_bytes, f, sizeof(f));
        wm_fmt_bytes(i.total_bytes, t, sizeof(t));
        snprintf(b, sizeof(b), "%s de %s", f, t);
        watch_theme_info_set(p->free_r, b);
    } else {
        watch_theme_info_set(p->free_r, "--");
    }
    /* El mensaje: lo que pasa y qué se puede hacer. */
    char m[200];
    if (i.state == WSD_ERROR) {
        snprintf(m, sizeof(m), "%s", wsd_err_text(i.err));
    } else if (i.state == WSD_ABSENT && !i.ejected) {
        snprintf(m, sizeof(m), "Inserta una tarjeta microSD: el reloj la detecta sola en unos segundos. Admite "
                               "SD, SDHC y SDXC hasta 2 TB en FAT32.");
    } else if (i.ejected) {
        snprintf(m, sizeof(m), "Ya puedes sacar la tarjeta. Para volver a usarla sin sacarla, toca «Volver a montar».");
    } else if (i.state == WSD_REMOVING) {
        wm_sd_describe(&i, m, sizeof(m));
    } else {
        m[0] = 0;
    }
    set_text(p->msg, m);
    watch_theme_set_visible(p->msg, m[0] != 0);
    enable(p->b_eject, i.state == WSD_READY || (i.state == WSD_ERROR && i.card_valid));
    enable(p->b_retry, i.state == WSD_ERROR || i.ejected);
    enable(p->b_format, i.can_format);
    char d[64];
    wm_vol_t v = WM_VOL_WATCH;
    wm_dest_get(&v);
    snprintf(d, sizeof(d), "%s", wm_vol_name(v));
    watch_theme_row_set_value(p->b_dest, d);
}

static void sd_tick_cb(lv_timer_t *t)
{
    sd_refresh(lv_timer_get_user_data(t));
}

static void eject_cb(lv_event_t *e)
{
    sd_page_t *p = lv_event_get_user_data(e);
    if (wm_sd_eject()) {
        ui_banner_show_info("microSD", "Expulsando: se cierran los archivos abiertos");
    }
    sd_refresh(p);
}

static void retry_cb(lv_event_t *e)
{
    sd_page_t *p = lv_event_get_user_data(e);
    wm_sd_retry();
    sd_refresh(p);
}

static void format_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_sd_format_create);
}

static void dest_open_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(ui_dest_create);
}

lv_obj_t *ui_sd_create(lv_obj_t *parent)
{
    sd_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "microSD", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    p->state = watch_theme_info_row(list, "Estado", "--");
    p->msg = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->msg, LV_PCT(100));
    lv_obj_set_style_pad_left(p->msg, 16, 0);
    lv_obj_set_style_pad_right(p->msg, 16, 0);
    p->free_r = watch_theme_info_row(list, "Libre", "--");
    p->cap = watch_theme_info_row(list, "Capacidad", "--");
    p->kind = watch_theme_info_row(list, "Tipo", "--");
    p->speed = watch_theme_info_row(list, "Bus (1 bit)", "--");
    watch_theme_section(list, "Uso");
    p->b_dest = watch_theme_list_row(list, FLEX_ICON_STORAGE, FLEX_C_CAT_BLUE, "Guardar en", "", true);
    lv_obj_add_event_cb(p->b_dest, dest_open_cb, LV_EVENT_CLICKED, p);
    watch_theme_section(list, "Acciones");
    p->b_eject = watch_theme_list_row(list, FLEX_ICON_SDCARD, FLEX_C_CAT_SUN, "Expulsar", NULL, false);
    lv_obj_add_event_cb(p->b_eject, eject_cb, LV_EVENT_CLICKED, p);
    p->b_retry = watch_theme_list_row(list, FLEX_ICON_UPDATE, FLEX_C_CAT_BLUE, "Volver a montar", NULL, false);
    lv_obj_add_event_cb(p->b_retry, retry_cb, LV_EVENT_CLICKED, p);
    p->b_format = watch_theme_list_row(list, FLEX_ICON_CROSS, FLEX_C_ERR, "Formatear en FAT32", NULL, true);
    lv_obj_add_event_cb(p->b_format, format_open_cb, LV_EVENT_CLICKED, p);
    want_interest(page);
    sd_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, sd_tick_cb, TICK_MS, p);
    return page;
}

/* ======================= formatear: doble confirmación ======================= */

typedef struct {
    lv_obj_t *btn, *info;
    uint32_t armed_ms; /* primer toque (0 = no) */
    bool started;
} fmt_page_t;

static void fmt_refresh(fmt_page_t *p)
{
    wm_sd_info_t i;
    wm_sd_info(&i);
    if (p->started) {
        char b[120];
        wm_sd_describe(&i, b, sizeof(b));
        set_text(p->info, i.state == WSD_READY ? "Formateada y lista." : b);
        enable(p->btn, false);
        return;
    }
    if (!i.can_format) {
        set_text(p->info, "Ahora no se puede formatear (sin tarjeta o en uso).");
        enable(p->btn, false);
        return;
    }
    enable(p->btn, true);
    if (p->armed_ms && lv_tick_elaps(p->armed_ms) > CONFIRM_MS) {
        p->armed_ms = 0;
        flex_glass_button_set(p->btn, "Formatear", 0);
    }
}

static void fmt_tick_cb(lv_timer_t *t)
{
    fmt_refresh(lv_timer_get_user_data(t));
}

static void fmt_cb(lv_event_t *e)
{
    fmt_page_t *p = lv_event_get_user_data(e);
    if (!p->armed_ms) {
        /* Primera confirmación (la segunda es esta pantalla misma). */
        p->armed_ms = (lv_tick_get() - 1u) | 1u; /* distinto de 0 y nunca en el futuro (lv_tick_elaps) */
        flex_glass_button_set(p->btn, "Sí, borrar todo", 0);
        return;
    }
    p->armed_ms = 0;
    const uint32_t ticket = wm_sd_format_ticket();
    if (ticket && wm_sd_format(ticket)) {
        p->started = true;
        flex_glass_button_set(p->btn, "Formateando…", 0);
    } else {
        set_text(p->info, "No se pudo empezar a formatear.");
    }
    fmt_refresh(p);
}

lv_obj_t *ui_sd_format_create(lv_obj_t *parent)
{
    fmt_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Formatear microSD", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    wm_sd_info_t i;
    wm_sd_info(&i);
    char cap[16] = "--";
    if (i.card_valid) {
        wm_fmt_bytes(i.card_bytes, cap, sizeof(cap));
    }
    char t[240];
    snprintf(t, sizeof(t),
             "Se borrará TODO lo que hay en la tarjeta de %s y se creará un sistema FAT32 nuevo. No se puede "
             "deshacer. Si tiene algo importante, cópialo antes en un ordenador.",
             cap);
    lv_obj_t *warn = watch_theme_label(list, FLEX_FONT_BODY, FLEX_C_TXT, t);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(warn, LV_PCT(92));
    p->btn = flex_glass_button(list, "Formatear", FLEX_C_DANGER, 260, 64);
    lv_obj_add_event_cb(p->btn, fmt_cb, LV_EVENT_CLICKED, p);
    p->info = watch_theme_label(list, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->info, LV_PCT(92));
    want_interest(page);
    fmt_refresh(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, fmt_tick_cb, TICK_MS, p);
    return page;
}
