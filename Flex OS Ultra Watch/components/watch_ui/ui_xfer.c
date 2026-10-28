/*
 * ui_xfer — "Recibir desde el móvil" (docs/MULTIMEDIA.md §9): el QR del Flex
 * Web Server del reloj (wm_web), su código de 6 cifras, cuánto le queda,
 * dónde se guarda lo que llega y lo que se está recibiendo.
 *
 *  - El servidor vive mientras esta página está abierta (wm_web_keep en cada
 *    tic); al salir se apaga solo en cuanto termina lo que esté en marcha.
 *  - Escanear el QR es emparejar: lleva la dirección y el código. El código
 *    caduca (WWEB_CODE_TTL_MS) y "Código nuevo" lo cambia sin cerrar las
 *    sesiones abiertas; "Desconectar móviles" las cierra todas.
 *  - Lo recibido va SOLO al destino elegido (chip "Guardar en"); si no está
 *    listo, el servidor no acepta subidas y aquí se dice por qué.
 *  - El QR se pinta en un búfer L8 a tamaño final (1 B por píxel, solo con
 *    la página abierta): módulos enteros, sin escalado, nítido y estable.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "FlexOS_QR.h"
#include "misc/cache/lv_cache.h"
#include "ui_logic.h"
#include "watch_media.h"
#include "watch_theme.h"
#include "wm_web.h"

#define TICK_MS   250
#define QR_BOX    196            /* tarjeta blanca */
#define QR_MAXPX  176            /* lado máximo del código dentro de la tarjeta */
#define QR_QUIET  2              /* módulos blancos alrededor (la tarjeta pone más) */
#define BODY_W    330

typedef struct {
    lv_obj_t *page, *body, *card, *qr, *qr_msg, *qr_btn, *code, *left, *url, *chip, *state, *x_card, *x_name,
        *x_bar, *x_msg, *b_new, *b_drop;
    lv_image_dsc_t dsc;
    uint8_t *px;         /* L8, side x side */
    int side;
    char shown[64];      /* lo que lleva el QR que se ve ("" = ninguno) */
    uint32_t gen;
    bool first;
    bool x_seen;         /* la tarjeta de lo que llega ya se enseñó (se lleva a la vista una vez) */
} xf_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

/* Pinta el QR de text en el búfer (a tamaño final). false si no cabe. */
static bool qr_render(xf_t *p, const char *text)
{
    uint8_t *mods = lv_malloc(FLEXQR_BUF_BYTES); /* solo mientras se pinta */
    int n = 0, ver = 0;
    if (!mods || flexQrEncode(text, FLEXQR_ECC_M, 1, mods, FLEXQR_BUF_BYTES, &n, &ver) != FLEXQR_OK) {
        lv_free(mods);
        return false;
    }
    const int full = n + 2 * QR_QUIET;
    const int m = QR_MAXPX / full; /* píxeles por módulo: entero */
    if (m < 2) {
        lv_free(mods);
        return false;
    }
    const int side = full * m;
    if (!p->px || side != p->side) {
        lv_image_cache_drop(&p->dsc);
        lv_free(p->px);
        p->px = lv_malloc((size_t)side * side);
        if (!p->px) {
            p->side = 0;
            lv_free(mods);
            return false;
        }
        p->side = side;
    }
    memset(p->px, 0xFF, (size_t)side * side);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!(mods[y * n + x] & FLEXQR_DARK)) {
                continue;
            }
            for (int yy = 0; yy < m; yy++) {
                memset(p->px + (size_t)((y + QR_QUIET) * m + yy) * side + (x + QR_QUIET) * m, 0x00, (size_t)m);
            }
        }
    }
    lv_free(mods);
    p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    p->dsc.header.cf = LV_COLOR_FORMAT_L8;
    p->dsc.header.w = (uint32_t)side;
    p->dsc.header.h = (uint32_t)side;
    p->dsc.header.stride = (uint32_t)side;
    p->dsc.data_size = (uint32_t)side * side;
    p->dsc.data = p->px;
    lv_image_cache_drop(&p->dsc);
    lv_image_set_src(p->qr, &p->dsc);
    lv_obj_center(p->qr);
    return true;
}

static void fmt_code(const char *c, char *out, size_t n)
{
    /* "123 456": se lee y se dicta mejor */
    snprintf(out, n, "%.3s %.3s", c, c + 3);
}

static void xf_refresh(xf_t *p)
{
    wm_web_keep(); /* mientras esta página está abierta, el servidor sigue */
    wm_web_status_t s;
    wm_web_status(&s);
    if (!p->first && s.gen == p->gen) {
        return;
    }
    p->first = false;
    p->gen = s.gen;
    char t[160], url[64];
    const bool ready = s.state == WWEB_READY;
    const bool valid = ready && s.code_left_s > 0 && wweb_url(url, sizeof(url), s.ip, s.port, s.code);
    /* QR o el motivo de que no lo haya */
    if (valid && strcmp(url, p->shown) != 0) {
        if (qr_render(p, url)) {
            snprintf(p->shown, sizeof(p->shown), "%s", url);
        } else {
            p->shown[0] = 0;
        }
    }
    const bool show_qr = valid && p->shown[0];
    watch_theme_set_visible(p->qr, show_qr);
    watch_theme_set_visible(p->qr_msg, !show_qr);
    watch_theme_set_visible(p->qr_btn, !show_qr && (s.state == WWEB_NO_WIFI || (ready && !s.code_left_s)));
    if (!show_qr) {
        if (s.state == WWEB_NO_WIFI) {
            set_text(p->qr_msg, "Conecta el reloj a una Wi-Fi: el móvil tiene que estar en la misma.");
            set_text(lv_obj_get_child(p->qr_btn, 0), "Wi-Fi");
        } else if (ready && !s.code_left_s) {
            set_text(p->qr_msg, "El código ha caducado.");
            set_text(lv_obj_get_child(p->qr_btn, 0), "Código nuevo");
        } else if (s.state == WWEB_ERROR) {
            set_text(p->qr_msg, s.err[0] ? s.err : wweb_state_text(s.state));
        } else {
            set_text(p->qr_msg, wweb_state_text(s.state));
        }
    }
    /* código, cuenta atrás y dirección */
    if (valid) {
        fmt_code(s.code, t, sizeof(t));
        set_text(p->code, t);
        snprintf(t, sizeof(t), "Caduca en %u:%02u", (unsigned)(s.code_left_s / 60u), (unsigned)(s.code_left_s % 60u));
        set_text(p->left, t);
        snprintf(t, sizeof(t), "%s:%u", s.ip, (unsigned)s.port);
        set_text(p->url, t);
    }
    watch_theme_set_visible(p->code, valid);
    watch_theme_set_visible(p->left, valid);
    watch_theme_set_visible(p->url, valid);
    /* destino */
    ui_dest_chip_refresh(p->chip, NULL);
    /* estado: móviles, destino que no está, lo rechazado */
    uint32_t rgb = FLEX_C_TXT2;
    if (!s.dest_ok) {
        wm_vol_t v = WM_VOL_WATCH;
        snprintf(t, sizeof(t), "%s", wm_dest_text(wm_dest_get(&v)));
        rgb = FLEX_C_WARN;
    } else if (!ready) {
        t[0] = 0;
    } else if (s.sessions > 0) {
        snprintf(t, sizeof(t), "%d móvil%s conectado%s · %u recibido%s", s.sessions, s.sessions == 1 ? "" : "es",
                 s.sessions == 1 ? "" : "s", (unsigned)s.uploads, s.uploads == 1 ? "" : "s");
    } else {
        snprintf(t, sizeof(t), "Escanea el código con la cámara del móvil o abre la dirección en su navegador.");
    }
    set_text(p->state, t);
    watch_theme_set_text_color(p->state, rgb);
    watch_theme_set_visible(p->state, t[0] != 0);
    /* lo que se está recibiendo */
    const bool xshow = s.x_state != WWEB_X_NONE;
    watch_theme_set_visible(p->x_card, xshow);
    if (xshow && !p->x_seen) {
        lv_obj_scroll_to_view(p->x_card, LV_ANIM_ON); /* lo que llega, a la vista sin buscarlo */
    }
    p->x_seen = xshow;
    if (xshow) {
        set_text(p->x_name, s.x_name[0] ? s.x_name : "Archivo");
        int pct = s.x_total ? (int)((uint64_t)s.x_done * 100u / s.x_total) : 0;
        if (s.x_state == WWEB_X_CHECK || s.x_state == WWEB_X_DONE) {
            pct = 100;
        }
        lv_bar_set_value(p->x_bar, pct * 10, LV_ANIM_OFF); /* la barra va de 0 a 1000 */
        switch (s.x_state) {
        case WWEB_X_RUN:
            snprintf(t, sizeof(t), "Recibiendo · %d %%", pct);
            break;
        case WWEB_X_CHECK:
            snprintf(t, sizeof(t), "Comprobando que se puede abrir…");
            break;
        case WWEB_X_DONE:
            snprintf(t, sizeof(t), "Guardado en %s", wm_vol_name(s.dest));
            break;
        default:
            snprintf(t, sizeof(t), "%s", s.x_msg[0] ? s.x_msg : "No se pudo recibir");
            break;
        }
        set_text(p->x_msg, t);
        watch_theme_set_text_color(p->x_msg, s.x_state == WWEB_X_FAIL ? FLEX_C_WARN : FLEX_C_TXT2);
    }
    enable(p->b_new, ready);
    enable(p->b_drop, ready && s.sessions > 0);
}

static void tick_cb(lv_timer_t *t)
{
    xf_refresh(lv_timer_get_user_data(t));
}

static void qr_btn_cb(lv_event_t *e)
{
    xf_t *p = lv_event_get_user_data(e);
    wm_web_status_t s;
    wm_web_status(&s);
    if (s.state == WWEB_NO_WIFI) {
        ui_nav_push(ui_wifi_create);
    } else {
        wm_web_new_code();
        p->first = true;
    }
}

static void new_cb(lv_event_t *e)
{
    xf_t *p = lv_event_get_user_data(e);
    wm_web_new_code();
    p->first = true;
}

static void drop_cb(lv_event_t *e)
{
    xf_t *p = lv_event_get_user_data(e);
    wm_web_drop_all();
    p->first = true;
}

static void del_cb(lv_event_t *e)
{
    xf_t *p = lv_event_get_user_data(e);
    wm_web_release(); /* se apaga solo al terminar lo que esté en marcha */
    lv_image_cache_drop(&p->dsc);
    lv_free(p->px);
    lv_free(p);
}

static lv_obj_t *center_label(lv_obj_t *parent, const lv_font_t *f, uint32_t rgb, int32_t y)
{
    lv_obj_t *l = watch_theme_label(parent, f, rgb, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, BODY_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, (FLEX_SCREEN_W - BODY_W) / 2, y);
    return l;
}

lv_obj_t *ui_xfer_create(lv_obj_t *parent)
{
    xf_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    p->page = page;
    watch_theme_header(page, "Desde el móvil", back_cb, NULL);

    /* Cuerpo que se desplaza y acaba antes de la esquina, como las listas. */
    lv_obj_t *body = lv_obj_create(page);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, FLEX_HDR_H);
    lv_obj_set_size(body, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H - FLEX_LIST_BOTTOM);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
    p->body = body;
    int32_t y = 4;

    /* Tarjeta blanca del QR (el código necesita su margen claro). */
    p->card = lv_obj_create(body);
    lv_obj_remove_style_all(p->card);
    lv_obj_set_size(p->card, QR_BOX, QR_BOX);
    lv_obj_set_pos(p->card, (FLEX_SCREEN_W - QR_BOX) / 2, y);
    lv_obj_set_style_radius(p->card, 24, 0);
    lv_obj_set_style_bg_opa(p->card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p->card, lv_color_white(), 0);
    lv_obj_remove_flag(p->card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    p->qr = lv_image_create(p->card);
    lv_obj_add_flag(p->qr, LV_OBJ_FLAG_HIDDEN);
    p->qr_msg = watch_theme_label(p->card, FLEX_FONT_SMALL, 0x3A3F4A, "");
    lv_label_set_long_mode(p->qr_msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p->qr_msg, QR_BOX - 32);
    lv_obj_set_style_text_align(p->qr_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p->qr_msg, LV_ALIGN_CENTER, 0, -26);
    p->qr_btn = flex_glass_button(p->card, "", FLEX_C_PRIMARY, 150, 48);
    lv_obj_align(p->qr_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(p->qr_btn, qr_btn_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_flag(p->qr_btn, LV_OBJ_FLAG_HIDDEN);
    y += QR_BOX + 10;

    p->code = center_label(body, FLEX_FONT_CAPS, FLEX_C_TXT, y);
    y += lv_font_get_line_height(FLEX_FONT_CAPS);
    p->left = center_label(body, FLEX_TYPE_CAPTION, FLEX_C_TXT2, y);
    y += lv_font_get_line_height(FLEX_TYPE_CAPTION) + 2;
    p->url = center_label(body, FLEX_TYPE_FOOTNOTE, FLEX_C_MUTE, y);
    y += lv_font_get_line_height(FLEX_TYPE_FOOTNOTE) + 12;

    p->chip = ui_dest_chip(body, 300, 44);
    lv_obj_set_pos(p->chip, (FLEX_SCREEN_W - 300) / 2, y);
    y += 44 + 10;
    p->state = center_label(body, FLEX_FONT_SMALL, FLEX_C_TXT2, y);
    y += 2 * lv_font_get_line_height(FLEX_FONT_SMALL) + 10;

    /* Lo que se está recibiendo (o lo último que llegó). */
    p->x_card = flex_glass_panel(body, FLEX_GLASS_CARD, (FLEX_SCREEN_W - BODY_W) / 2, y, BODY_W, 92, 22);
    lv_obj_remove_flag(p->x_card, LV_OBJ_FLAG_CLICKABLE);
    p->x_name = watch_theme_label(p->x_card, FLEX_TYPE_LABEL, FLEX_GLASS_TXT, "");
    lv_label_set_long_mode(p->x_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->x_name, BODY_W - 40, lv_font_get_line_height(FLEX_TYPE_LABEL));
    lv_obj_set_pos(p->x_name, 16, 10);
    p->x_bar = watch_theme_bar(p->x_card, BODY_W - 40, 8, FLEX_C_PRIMARY, 0x2C2E34);
    lv_obj_set_pos(p->x_bar, 16, 44);
    p->x_msg = watch_theme_label(p->x_card, FLEX_TYPE_FOOTNOTE, FLEX_C_TXT2, "");
    lv_label_set_long_mode(p->x_msg, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->x_msg, BODY_W - 40, lv_font_get_line_height(FLEX_TYPE_FOOTNOTE));
    lv_obj_set_pos(p->x_msg, 16, 58);
    lv_obj_add_flag(p->x_card, LV_OBJ_FLAG_HIDDEN);
    y += 92 + 12;

    p->b_new = flex_glass_button(body, "Código nuevo", FLEX_GLASS_TINT_NONE, BODY_W, 52);
    lv_obj_set_pos(p->b_new, (FLEX_SCREEN_W - BODY_W) / 2, y);
    lv_obj_add_event_cb(p->b_new, new_cb, LV_EVENT_CLICKED, p);
    y += 52 + 10;
    p->b_drop = flex_glass_button(body, "Desconectar móviles", FLEX_GLASS_TINT_NONE, BODY_W, 52);
    lv_obj_set_pos(p->b_drop, (FLEX_SCREEN_W - BODY_W) / 2, y);
    lv_obj_add_event_cb(p->b_drop, drop_cb, LV_EVENT_CLICKED, p);
    y += 52 + 16;
    lv_obj_t *pad = lv_obj_create(body);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, 1);
    lv_obj_set_pos(pad, 0, y);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_CLICKABLE);

    p->first = true;
    xf_refresh(p);
    lv_obj_add_event_cb(page, del_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, tick_cb, TICK_MS, p);
    return page;
}
