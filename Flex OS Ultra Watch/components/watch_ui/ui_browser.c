/*
 * ui_browser — app Navegador (IC_NAV de Flex OS Ultra) en el reloj (F6.2).
 *
 * La interfaz de FlexOS_BrowserApp.cpp rehecha con LVGL para 410x502; la
 * sesión es watch_browser (br_net) y la web la renderiza el servicio remoto.
 *  - barra: atrás, adelante, recargar/detener, omnibox (candado verde con
 *    HTTPS válido, punto ámbar con HTTP) y menú; progreso de 3 px;
 *  - zona de página: el JPEG de cada FRAME decodificado en un lienzo RGB565
 *    (PSRAM); arrastrar = SCROLL agrupado cada 50 ms con el signo invertido,
 *    tocar sin arrastrar = POINTER TAP en coordenadas de la página;
 *  - sin imagen: el panel de estado de la sesión (estado, frase de la etapa,
 *    fuente y servidor -nunca la credencial-, contadores y pista);
 *  - origen de la web (flex://settings): Automático, Flex Phone, Nube o PC.
 *    Con el teléfono, el reloj le pide su Browser Relay por BLE y la sesión
 *    se rehace sola cuando aparece, cambia de dirección o se va; las
 *    imágenes que el relay empuja se pintan según llegan, sin tocar nada;
 *  - omnibox con el teclado del sistema y la resolución de la referencia
 *    (flexBrResolveInput: páginas internas, URL, búsqueda o bloqueo);
 *  - páginas internas flex://newtab, history, bookmarks, settings,
 *    downloads, offline y about; menú; hasta 4 pestañas;
 *  - "Teclado para la página" y apertura automática con STATE bit 6: cada
 *    tecla va directa al servicio y no se guarda en el reloj.
 * RELOJ: sin tira de pestañas (se elige en "Pestañas", que deja la altura a
 * la página); sin caché JPEG: al volver a la página se pide (REQ_FRAME); sin
 * reproductor de vídeo (no se anuncia la capacidad MEDIA); la calidad cicla
 * hasta 90 (la referencia llegaba a 92, ambigüedad 13).
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "flex_icons.h"
#include "flex_jpeg.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "watch_browser.h"
#include "watch_theme.h"

/* RELOJ: la esquina de la pantalla (radio 124) solo deja x 58..351 en la
 * fila de arriba: el omnibox va solo arriba y los botones abajo, dentro del
 * arco; la página queda entre las dos barras. */
#define TB_Y      FLEX_SAFE_TOP
#define TB_BTN    44
#define OMNI_X    58
#define OMNI_W    (351 - OMNI_X)
#define PG_Y      (TB_Y + TB_BTN + 4)
#define BB_Y      448 /* barra de abajo: filas 448..492 */
#define PG_W      FLEX_SCREEN_W
#define PG_H      (BB_Y - 4 - PG_Y)
#define PROG_X    (OMNI_X + 16)
#define PROG_W    (OMNI_W - 32)
#define KB_TOP    236
#define PK_H      (KB_TOP - PG_Y) /* zona de página con el teclado abierto */
#define TICK_MS   30
#define ERR_MS    6000
#define SCROLL_MS 50
#define C_OK      0x5AC88C
#define C_WARN    0xF0B45A
#define C_ERR     0xEB5050

enum { ED_NONE = 0, ED_OMNI, ED_SERVER, ED_TOKEN, ED_TPL, ED_CLOUD, ED_PAGEKEY };
enum { V_TAB = 0, V_TABS }; /* lo que se ve en la zona de página */

typedef struct {
    bool used;
    int page; /* BRP_* o BRP_NONE = página remota */
    char url[FLEXBR_URL_MAX];
    char title[FLEXBR_TITLE_MAX];
    uint32_t last_use, seen_gen, seen_nav;
    uint8_t flags, progress;
} ub_tab_t;

typedef struct {
    lv_obj_t *page, *back, *fwd, *reload_i, *stop_i, *omni, *omni_lbl, *omni_dot, *menu_btn, *progress;
    lv_obj_t *canvas, *status, *st_dot, *st_name, *st_info, *st_srv, *st_cnt, *st_hint;
    lv_obj_t *inner, *errbar, *menu, *edit, *edit_ta, *edit_kb;
    uint16_t *buf;
    bool has_frame;
    int view, ed;
    bool pk_guard;
    ub_tab_t tab[FLEXBR_MAX_TABS];
    int act;
    BrSettings set;
    uint32_t caps;
    /* lo último que se copió de la sesión */
    int net_state;
    uint32_t info_gen, err_gen, msgs, kb_in;
    BrStats stats;
    char info[BR_INFO_MAX];
    bool need_retry;   /* sesión pendiente por falta de memoria (F11) */
    uint32_t retry_ms;
    /* táctil de la página */
    bool pressed, dragging;
    lv_point_t p0, plast;
    int32_t acc_dx, acc_dy;
    uint32_t last_scroll, err_until, lists_gen;
    bool probing;
    watch_browser_source_t src; /* de dónde sale la web ahora */
    uint32_t src_ms;
} ub_t;

static ub_t *U;

/* ======================= utilidades ======================= */

static ub_tab_t *T(void)
{
    return &U->tab[U->act];
}

static uint8_t ch_of(int i)
{
    return (uint8_t)(i + 1); /* canal remoto = índice local + 1 (como la referencia) */
}

/* El aviso ocupa el sitio de la barra de abajo mientras se ve */
static void bottom_bar(bool show)
{
    lv_obj_t *const b[] = {U->back, U->fwd, lv_obj_get_parent(U->reload_i), U->menu_btn};
    for (size_t i = 0; i < 4; i++) {
        if (show) {
            lv_obj_remove_flag(b[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(b[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hide_err(void)
{
    lv_obj_add_flag(U->errbar, LV_OBJ_FLAG_HIDDEN);
    U->err_until = 0;
    bottom_bar(true);
}

static void errbar_cb(lv_event_t *e)
{
    (void)e;
    hide_err(); /* tocar el aviso lo cierra */
}

static void show_err(const char *msg)
{
    char t[160];
    ui_phone_text(msg, t, sizeof(t), FLEX_FONT_SMALL);
    lv_label_set_text(lv_obj_get_child(U->errbar, 0), t);
    lv_obj_remove_flag(U->errbar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(U->errbar);
    bottom_bar(false);
    U->err_until = lv_tick_get() + ERR_MS;
}

static br_net_t *net(void)
{
    return watch_browser_net();
}

static uint16_t page_h(void)
{
    return U->ed == ED_PAGEKEY ? PK_H : PG_H;
}

static void push_viewport(void)
{
    br_net_t *n = net();
    if (n) {
        br_net_set_viewport(n, PG_W, page_h(), ch_of(U->act));
    }
}

static void set_hidden(lv_obj_t *o, bool h)
{
    if (h) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Con servidor configurado, la sesión abierta (se puede configurar con la app abierta).
 *
 * Preparación de memoria (F11), antes de abrir: se sueltan las cachés de
 * imagen de LVGL (lo único prescindible que es del propio navegador o del
 * sistema; nada de otras apps) y se mide lo que la sesión necesita de verdad
 * (watch_browser_mem_check). Si no cabe, el motivo con las cifras y un
 * reintento solo cada SESSION_RETRY_MS: en cuanto se libera memoria (p. ej.
 * al cerrar otra app), la sesión se abre sin que el usuario haga nada. Nunca
 * se fuerza ni se reinicia el reloj. */
#define SESSION_RETRY_MS 3000

static void ensure_session(void)
{
    /* Con el teléfono vinculado la sesión se abre aunque su relay aún no esté:
     * es la tarea de la sesión la que se lo pide y se conecta cuando aparece. */
    if (((U->caps & FLEXBR_CAP_REMOTE) || U->src.phone_possible) && !watch_browser_net()) {
        lv_image_cache_drop(NULL); /* caché de imágenes de LVGL (si la hay) */
        char why[96];
        char m[160];
        bool ok = watch_browser_mem_check(why, sizeof(why));
        if (!ok) {
            snprintf(m, sizeof(m), "Memoria insuficiente. %s. Reintento automático.", why);
        } else {
            const esp_err_t r = watch_browser_open();
            ok = r == ESP_OK;
            if (r == ESP_ERR_INVALID_STATE) {
                snprintf(m, sizeof(m), "Cerrando la sesión anterior…");
            } else if (!ok) {
                snprintf(m, sizeof(m), "Memoria insuficiente para la sesión. Reintento automático.");
            }
        }
        if (!ok) {
            show_err(m);
            U->retry_ms = lv_tick_get();
            U->need_retry = true;
            return;
        }
        U->need_retry = false;
        push_viewport();
    }
}

static void clear_canvas(void)
{
    if (U->buf) {
        memset(U->buf, 0, (size_t)PG_W * PG_H * 2);
        lv_obj_invalidate(U->canvas);
    }
    U->has_frame = false;
}

static void build_inner(int kind);
static void refresh_chrome(void);
static void show_view(void);

/* ======================= navegación ======================= */

static void open_internal(int page)
{
    ub_tab_t *t = T();
    t->page = page;
    snprintf(t->url, sizeof(t->url), "flex://%s", flexBrPageName(page));
    snprintf(t->title, sizeof(t->title), "%s", flexBrPageTitle(page));
    t->flags = t->progress = 0;
    U->view = V_TAB;
    show_view();
}

static void navigate(const char *url)
{
    const int pg = flexBrInternalPage(url);
    if (pg != BRP_NONE) {
        open_internal(pg);
        return;
    }
    if (!(U->caps & FLEXBR_CAP_REMOTE) && !U->src.phone_possible) {
        show_err("Sin sesión remota: revisa Ajustes");
        open_internal(BRP_OFFLINE);
        return;
    }
    if (!watch_browser_online()) {
        open_internal(BRP_OFFLINE);
        return;
    }
    ub_tab_t *t = T();
    t->page = BRP_NONE;
    snprintf(t->url, sizeof(t->url), "%s", url);
    flexBrUrlLabel(url, t->title, sizeof(t->title));
    t->flags = FBP_ST_LOADING;
    t->progress = 1;
    U->view = V_TAB;
    clear_canvas();
    br_lists_t *L = watch_browser_lists();
    if (L && U->set.saveHistory) {
        br_hist_add(L, url, t->title, lv_tick_get());
    }
    br_net_t *n = net();
    if (n) {
        br_net_navigate(n, ch_of(U->act), url);
    }
    show_view();
}

static void commit_omnibox(const char *text)
{
    BrResolved r;
    switch (flexBrResolveInput(text, &U->set, &r)) {
    case BRI_INTERNAL: open_internal(r.page); break;
    case BRI_URL:
    case BRI_SEARCH: navigate(r.url); break;
    case BRI_BLOCKED: show_err(flexBrBlockReason(r.block)); break;
    default: break; /* vacío: no se navega */
    }
}

/* ======================= pestañas ======================= */

static void select_tab(int i)
{
    if (i < 0 || i >= FLEXBR_MAX_TABS || !U->tab[i].used) {
        return;
    }
    U->act = i;
    U->tab[i].last_use = lv_tick_get();
    clear_canvas();
    br_net_t *n = net();
    if (n) {
        push_viewport();
        br_net_cmd(n, FBP_C_TAB_SELECT, ch_of(i));
        if (U->tab[i].page == BRP_NONE) {
            br_net_cmd(n, FBP_C_REQ_FRAME, ch_of(i));
        }
    }
    U->view = V_TAB;
    show_view();
}

static void new_tab(void)
{
    int i = 0;
    while (i < FLEXBR_MAX_TABS && U->tab[i].used) {
        i++;
    }
    br_net_t *n = net();
    if (i == FLEXBR_MAX_TABS) { /* lleno: se cierra la menos usada (brTabNew) */
        i = 0;
        for (int k = 1; k < FLEXBR_MAX_TABS; k++) {
            if (U->tab[k].last_use < U->tab[i].last_use) {
                i = k;
            }
        }
        if (n) {
            br_net_cmd(n, FBP_C_TAB_CLOSE, ch_of(i));
        }
    }
    memset(&U->tab[i], 0, sizeof(U->tab[i]));
    U->tab[i].used = true;
    if (n && i > 0) {
        br_net_cmd(n, FBP_C_TAB_NEW, ch_of(i));
    }
    U->act = i;
    U->tab[i].last_use = lv_tick_get();
    clear_canvas();
    open_internal(BRP_NEWTAB);
}

static void close_tab(int i)
{
    int used = 0;
    for (int k = 0; k < FLEXBR_MAX_TABS; k++) {
        used += U->tab[k].used;
    }
    br_net_t *n = net();
    if (used <= 1) { /* la última no se cierra: se vacía (brTabClose) */
        U->act = i;
        open_internal(BRP_NEWTAB);
        return;
    }
    if (n) {
        br_net_cmd(n, FBP_C_TAB_CLOSE, ch_of(i));
    }
    U->tab[i].used = false;
    if (U->act == i) {
        int k = 0;
        while (!U->tab[k].used) {
            k++;
        }
        select_tab(k);
    } else {
        build_inner(V_TABS);
    }
}

/* ======================= edición y teclado ======================= */

static void close_edit(void)
{
    const bool was_pk = U->ed == ED_PAGEKEY;
    U->ed = ED_NONE;
    if (U->edit) {
        lv_obj_delete(U->edit);
        U->edit = U->edit_ta = U->edit_kb = NULL;
    }
    if (was_pk) {
        push_viewport();
    }
}

static void edit_ok(void *ud)
{
    (void)ud;
    char text[FLEXBR_INPUT_MAX + 4];
    snprintf(text, sizeof(text), "%s", lv_textarea_get_text(U->edit_ta));
    const int ed = U->ed;
    if (ed == ED_PAGEKEY) {
        br_net_t *n = net();
        if (n) {
            br_net_key(n, ch_of(U->act), FBP_VK_ENTER);
        }
        return;
    }
    close_edit();
    if (ed == ED_OMNI) {
        commit_omnibox(text);
        return;
    }
    if (ed == ED_SERVER) {
        bool tls;
        char host[FLEXBR_HOST_MAX], path[128];
        uint16_t port;
        if (strlen(text) >= FLEXBR_SERVER_MAX) {
            show_err("La dirección del servidor es demasiado larga");
        } else if (text[0] && !flexBrParseWsUrl(text, &tls, host, sizeof(host), &port, path, sizeof(path))) {
            show_err("Debe ser wss://host:puerto/ruta");
        } else {
            memcpy(U->set.server, text, strlen(text) + 1);
            watch_browser_set_settings(&U->set);
            show_err(text[0] ? "Servidor guardado" : "Servidor borrado");
        }
    } else if (ed == ED_CLOUD) {
        bool tls;
        char host[FLEXBR_HOST_MAX], path[128];
        uint16_t port;
        if (strlen(text) >= FLEXBR_SERVER_MAX) {
            show_err("La dirección del servidor es demasiado larga");
        } else if (text[0] && (!flexBrParseWsUrl(text, &tls, host, sizeof(host), &port, path, sizeof(path)) || !tls)) {
            show_err("La nube debe ser wss://host:puerto/ruta");
        } else {
            memcpy(U->set.cloudServer, text, strlen(text) + 1);
            watch_browser_set_settings(&U->set);
            show_err(text[0] ? "Servidor en la nube guardado" : "Servidor en la nube borrado");
        }
    } else if (ed == ED_TOKEN) {
        if (strlen(text) >= FLEXBR_TOKEN_MAX) {
            show_err("La credencial es demasiado larga");
        } else {
            memcpy(U->set.token, text, strlen(text) + 1);
            watch_browser_set_settings(&U->set);
            show_err("Credencial guardada");
        }
    } else if (ed == ED_TPL) {
        if (strlen(text) >= FLEXBR_SERVER_MAX) {
            show_err("La plantilla es demasiado larga");
        } else if (text[0] && !strstr(text, "%s")) {
            show_err("La plantilla debe contener %s");
        } else {
            memcpy(U->set.searchCustom, text, strlen(text) + 1);
            if (text[0]) {
                U->set.searchEngine = 3;
            }
            watch_browser_set_settings(&U->set);
        }
    }
    watch_browser_source(&U->src);
    U->caps = watch_browser_caps();
    ensure_session();
    build_inner(V_TAB);
}

static void edit_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_edit();
}

/* Teclado de la página: cada tecla va directa al servicio y el campo oculto
 * vuelve a ser " " (privacidad: el reloj no acumula lo escrito). */
static void pk_changed_cb(lv_event_t *e)
{
    (void)e;
    if (U->pk_guard || !U->edit_ta) {
        return;
    }
    const char *t = lv_textarea_get_text(U->edit_ta);
    br_net_t *n = net();
    if (n) {
        if (t[0] == 0) {
            br_net_key(n, ch_of(U->act), FBP_VK_BACKSPACE);
        } else if (t[0] == ' ' && t[1]) {
            br_net_key_text(n, ch_of(U->act), t + 1);
        }
    }
    U->pk_guard = true;
    lv_textarea_set_text(U->edit_ta, " ");
    U->pk_guard = false;
}

static void open_edit(int ed)
{
    close_edit();
    U->ed = ed;
    lv_obj_t *o = lv_obj_create(U->page);
    lv_obj_remove_style_all(o);
    U->edit = o;
    if (ed == ED_PAGEKEY) { /* solo el teclado: la página sigue viéndose arriba */
        lv_obj_set_size(o, FLEX_SCREEN_W, FLEX_SCREEN_H - KB_TOP);
        lv_obj_set_pos(o, 0, KB_TOP);
        lv_obj_set_style_bg_color(o, flex_color(FLEX_C_PAGE), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        U->edit_ta = lv_textarea_create(o);
        lv_textarea_set_one_line(U->edit_ta, true);
        lv_obj_add_flag(U->edit_ta, LV_OBJ_FLAG_HIDDEN);
        U->pk_guard = true;
        lv_textarea_set_text(U->edit_ta, " ");
        U->pk_guard = false;
        lv_obj_add_event_cb(U->edit_ta, pk_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
        U->edit_kb = ui_keyboard_create(o, U->edit_ta, UI_KB_TEXT, edit_ok, NULL);
        lv_obj_set_size(U->edit_kb, FLEX_SCREEN_W - 32, 452 - KB_TOP);
        lv_obj_set_pos(U->edit_kb, 16, 0);
        push_viewport(); /* la página se renegocia a la altura que queda */
        return;
    }
    lv_obj_set_size(o, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_style_bg_color(o, flex_color(FLEX_C_PAGE), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    static const char *const kLabel[] = {"", "Dirección o búsqueda", "Servidor (wss://...)",
                                         "Credencial del dispositivo", "Plantilla del buscador (%s)",
                                         "Servidor en la nube (wss://...)"};
    watch_theme_header(o, kLabel[ed], edit_cancel_cb, NULL);
    U->edit_ta = lv_textarea_create(o);
    lv_textarea_set_one_line(U->edit_ta, true);
    lv_textarea_set_max_length(U->edit_ta, ed == ED_OMNI ? FLEXBR_INPUT_MAX - 1 : FLEXBR_SERVER_MAX - 1);
    lv_obj_set_size(U->edit_ta, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, 56);
    lv_obj_set_pos(U->edit_ta, FLEX_SAFE_X, FLEX_HDR_H + 8);
    ui_textarea_style(U->edit_ta);
    /* el campo empieza con lo que hay (la credencial nunca: empieza vacía) */
    if (ed == ED_OMNI && T()->page == BRP_NONE) {
        lv_textarea_set_text(U->edit_ta, T()->url);
    } else if (ed == ED_SERVER) {
        lv_textarea_set_text(U->edit_ta, U->set.server);
    } else if (ed == ED_TPL) {
        lv_textarea_set_text(U->edit_ta, U->set.searchCustom);
    } else if (ed == ED_CLOUD) {
        lv_textarea_set_text(U->edit_ta, U->set.cloudServer);
    }
    U->edit_kb = ui_keyboard_create(o, U->edit_ta, ed == ED_OMNI ? UI_KB_TEXT : UI_KB_ASCII, edit_ok, NULL);
    lv_obj_set_size(U->edit_kb, FLEX_SCREEN_W - 32, 452 - KB_TOP);
    lv_obj_set_pos(U->edit_kb, 16, KB_TOP);
}

/* ======================= menú ======================= */

enum {
    M_NEWTAB = 1, M_TABS, M_BOOKMARK, M_BOOKMARKS, M_HISTORY, M_DOWNLOADS, M_PAGEKEY, M_SETTINGS, M_ABOUT, M_CLOSE
};

static void close_menu(void)
{
    if (U->menu) {
        lv_obj_delete(U->menu);
        U->menu = NULL;
    }
}

static void menu_item_cb(lv_event_t *e)
{
    const int id = (int)(intptr_t)lv_event_get_user_data(e);
    close_menu();
    switch (id) {
    case M_NEWTAB:
        new_tab();
        open_edit(ED_OMNI); /* como la referencia: pestaña nueva, a escribir */
        break;
    case M_TABS:
        U->view = V_TABS;
        show_view();
        break;
    case M_BOOKMARK: {
        br_lists_t *L = watch_browser_lists();
        bool added;
        if (L && br_bm_toggle(L, T()->url, T()->title, &added)) {
            show_err(added ? "Añadido a favoritos" : "Quitado de favoritos");
        }
        break;
    }
    case M_BOOKMARKS: open_internal(BRP_BOOKMARKS); break;
    case M_HISTORY: open_internal(BRP_HISTORY); break;
    case M_DOWNLOADS: open_internal(BRP_DOWNLOADS); break;
    case M_PAGEKEY: open_edit(ED_PAGEKEY); break;
    case M_SETTINGS: open_internal(BRP_SETTINGS); break;
    case M_ABOUT: open_internal(BRP_ABOUT); break;
    case M_CLOSE: ui_nav_back(); break;
    default: break;
    }
}

static void menu_bg_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
        close_menu(); /* tocar fuera cierra el menú */
    }
}

static void menu_add(lv_obj_t *col, const char *text, int id, bool enabled, uint32_t rgb)
{
    /* Fantasma del motor: vidrio solo mientras se toca */
    lv_obj_t *b = flex_glass_create(col, FLEX_GLASS_GHOST);
    lv_obj_set_size(b, LV_PCT(100), 42);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_left(b, 14, 0);
    lv_obj_t *l = watch_theme_label(b, FLEX_FONT_BODY, enabled ? rgb : FLEX_C_MUTE, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    if (enabled) {
        flex_glass_interactive(b);
        lv_obj_add_event_cb(b, menu_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    }
}

static void open_menu(void)
{
    close_menu();
    /* Velo del motor sobre la página web y, encima, una lámina legible sea
     * cual sea el contenido de debajo */
    lv_obj_t *bg = flex_glass_create(U->page, FLEX_GLASS_SCRIM);
    lv_obj_set_size(bg, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, menu_bg_cb, LV_EVENT_CLICKED, NULL);
    U->menu = bg;
    lv_obj_t *p = flex_glass_panel(bg, FLEX_GLASS_SHEET, FLEX_SCREEN_W - 300, PG_Y + 4, 280, LV_SIZE_CONTENT, 26);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE); /* un toque entre opciones no cierra el menú */
    lv_obj_set_style_pad_all(p, 6, 0);
    lv_obj_set_style_pad_row(p, 0, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_max_height(p, BB_Y - PG_Y - 12, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    const bool remote = T()->page == BRP_NONE && T()->url[0];
    br_lists_t *L = watch_browser_lists();
    const bool marked = L && br_bm_find(L, T()->url) >= 0;
    char tabs[32];
    int used = 0;
    for (int k = 0; k < FLEXBR_MAX_TABS; k++) {
        used += U->tab[k].used;
    }
    snprintf(tabs, sizeof(tabs), "Pestañas (%d)", used);
    menu_add(p, "Nueva pestaña", M_NEWTAB, true, FLEX_C_TXT);
    menu_add(p, tabs, M_TABS, true, FLEX_C_TXT);
    if (remote) {
        menu_add(p, marked ? "Quitar de favoritos" : "Añadir a favoritos", M_BOOKMARK, true, FLEX_C_TXT);
    }
    menu_add(p, "Favoritos", M_BOOKMARKS, true, FLEX_C_TXT);
    menu_add(p, "Historial", M_HISTORY, true, FLEX_C_TXT);
    menu_add(p, "Descargas", M_DOWNLOADS, true, FLEX_C_TXT);
    menu_add(p, "Teclado para la página", M_PAGEKEY, remote && U->net_state == BRN_READY, FLEX_C_TXT);
    menu_add(p, "Ajustes del navegador", M_SETTINGS, true, FLEX_C_TXT);
    menu_add(p, "Acerca de", M_ABOUT, true, FLEX_C_TXT);
    menu_add(p, "Cerrar el navegador", M_CLOSE, true, FLEX_C_DANGER);
}

/* ======================= páginas internas ======================= */

typedef enum {
    A_NONE = 0, A_URL, A_TAB, A_TABCLOSE, A_NEWTAB, A_SERVER, A_TOKEN, A_PROBE, A_ENGINE, A_TPL, A_PROFILE,
    A_QUALITY, A_SCALE, A_HIST, A_TEL, A_LOCAL, A_INSEC, A_CLR_H, A_CLR_B, A_CLR_ALL, A_SOURCE, A_CLOUD
} act_t;

static void inner_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    const act_t a = (act_t)(intptr_t)lv_event_get_user_data(e);
    const char *url = lv_obj_get_user_data(o);
    br_lists_t *L = watch_browser_lists();
    BrSettings *s = &U->set;
    bool save = true;
    switch (a) {
    case A_URL:
        if (url) {
            navigate(url);
        }
        return;
    case A_TAB: select_tab((int)(intptr_t)url); return;
    case A_TABCLOSE: close_tab((int)(intptr_t)url); return;
    case A_NEWTAB:
        new_tab();
        open_edit(ED_OMNI);
        return;
    case A_SERVER: open_edit(ED_SERVER); return;
    case A_TOKEN: open_edit(ED_TOKEN); return;
    case A_TPL: open_edit(ED_TPL); return;
    case A_PROBE: /* la fuente en uso; sin ninguna, el motivo llega como resultado */
        watch_browser_probe_start();
        U->probing = true;
        return;
    case A_CLOUD: open_edit(ED_CLOUD); return;
    case A_SOURCE: s->source = (uint8_t)((s->source + 1) % BRSRC_N); break;
    case A_ENGINE: s->searchEngine = (uint8_t)((s->searchEngine + 1) % 4); break;
    case A_PROFILE: {
        s->profile = (uint8_t)((s->profile + 1) % BRQ_N);
        static const uint8_t kQ[BRQ_N] = {45, 62, 38}, kE[BRQ_N] = {85, 100, 70};
        s->quality = kQ[s->profile];
        s->scalePct = kE[s->profile];
        break;
    }
    case A_QUALITY: s->quality = (uint8_t)(s->quality >= 85 ? 25 : (s->quality + 10 > 90 ? 90 : s->quality + 10)); break;
    case A_SCALE: s->scalePct = (uint8_t)(s->scalePct >= 100 ? 50 : s->scalePct + 10); break;
    case A_HIST: s->saveHistory = !s->saveHistory; break;
    case A_TEL: s->telemetry = !s->telemetry; break;
    case A_LOCAL:
        s->allowLocal = !s->allowLocal;
        show_err(s->allowLocal ? "Red local permitida (solo desarrollo)" : "Red local bloqueada");
        break;
    case A_INSEC:
        s->allowInsecure = !s->allowInsecure;
        show_err(s->allowInsecure ? "TLS no obligatorio (solo desarrollo)" : "TLS obligatorio");
        break;
    case A_CLR_H:
        if (L) {
            br_hist_clear(L);
        }
        show_err("Historial borrado");
        save = false;
        break;
    case A_CLR_B:
        if (L) {
            br_bm_clear(L);
        }
        show_err("Favoritos borrados");
        save = false;
        break;
    case A_CLR_ALL:
        if (L) {
            br_hist_clear(L);
            br_bm_clear(L);
        }
        show_err("Datos de navegación borrados");
        save = false;
        break;
    default: return;
    }
    if (save) {
        watch_browser_set_settings(s);
    } else {
        watch_browser_persist(true);
    }
    watch_browser_source(&U->src);
    U->caps = watch_browser_caps();
    build_inner(V_TAB);
}

/* Fila tocable. Historial, favoritos y pestañas recortan a una línea (títulos
 * remotos); los ajustes se leen enteros. */
static bool s_row_wrap;

static lv_obj_t *row(lv_obj_t *list, const char *title, const char *sub, act_t a, const void *ud, uint32_t rgb)
{
    lv_obj_t *r = flex_glass_tile(list, 0, 0, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(r, 10, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(r, 2, 0);
    char t[160];
    ui_phone_text(title, t, sizeof(t), FLEX_FONT_BODY);
    lv_obj_t *l = watch_theme_label(r, FLEX_FONT_BODY, rgb, t);
    lv_obj_set_width(l, LV_PCT(100));
    if (!s_row_wrap) {
        lv_obj_set_height(l, lv_font_get_line_height(FLEX_FONT_BODY));
    }
    lv_label_set_long_mode(l, s_row_wrap ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    if (sub && sub[0]) {
        ui_phone_text(sub, t, sizeof(t), FLEX_FONT_SMALL);
        lv_obj_t *s = watch_theme_label(r, FLEX_FONT_SMALL, FLEX_C_TXT2, t);
        lv_obj_set_width(s, LV_PCT(100));
        if (!s_row_wrap) {
            lv_obj_set_height(s, lv_font_get_line_height(FLEX_FONT_SMALL));
        }
        lv_label_set_long_mode(s, s_row_wrap ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    }
    if (a == A_NONE) {
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_user_data(r, (void *)ud);
        lv_obj_add_event_cb(r, inner_cb, LV_EVENT_CLICKED, (void *)(intptr_t)a);
    }
    return r;
}

static lv_obj_t *text(lv_obj_t *list, const lv_font_t *f, uint32_t rgb, const char *t)
{
    lv_obj_t *l = watch_theme_label(list, f, rgb, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

static const char *cap_reason(uint32_t cap)
{
    if (cap == FLEXBR_CAP_REMOTE) {
        return U->src.why[0] ? U->src.why : "Falta configurar el servidor en Ajustes";
    }
    if (cap == FLEXBR_CAP_FS) {
        return "Sin sistema de archivos montado";
    }
    if (cap == FLEXBR_CAP_MEDIA) {
        return "El reloj no lleva reproductor de vídeo";
    }
    return "";
}

static void build_settings(lv_obj_t *L)
{
    const BrSettings *s = &U->set;
    s_row_wrap = true;
    char v[160];
    if (U->src.src != BRSRC_NONE) {
        snprintf(v, sizeof(v), "%s · en uso: %s", flexBrSourceName(s->source), flexBrSourceName(U->src.src));
    } else {
        snprintf(v, sizeof(v), "%s · %s", flexBrSourceName(s->source), U->src.why);
    }
    row(L, "Origen de la web", v, A_SOURCE, NULL, FLEX_C_TXT);
    text(L, FLEX_FONT_SMALL, FLEX_C_MUTE,
         "Automático: Flex Phone si está conectado (el reloj le pide su servidor por Bluetooth y navega por el "
         "Wi-Fi de casa), si no la nube y por último el PC.");
    row(L, "Servidor del PC/Ubuntu", s->server[0] ? s->server : "sin configurar", A_SERVER, NULL, FLEX_C_TXT);
    row(L, "Servidor en la nube", s->cloudServer[0] ? s->cloudServer : "sin configurar", A_CLOUD, NULL, FLEX_C_TXT);
    row(L, "Credencial del PC y la nube", s->token[0] ? "configurada" : "sin configurar", A_TOKEN, NULL,
        FLEX_C_TXT);
    row(L, "Probar el servidor", U->probing ? "Comprobando..." : "Comprueba /v1/health del origen en uso", A_PROBE,
        NULL, FLEX_C_TXT);
    static const char *const kEng[4] = {"DuckDuckGo", "Google", "Bing", "Personalizado"};
    row(L, "Buscador", kEng[s->searchEngine & 3], A_ENGINE, NULL, FLEX_C_TXT);
    row(L, "Plantilla personalizada", s->searchCustom[0] ? s->searchCustom : "usar %s", A_TPL, NULL, FLEX_C_TXT);
    static const char *const kProf[BRQ_N] = {"Baja latencia", "Equilibrado", "Ahorro de datos"};
    row(L, "Perfil de rendimiento", kProf[s->profile < BRQ_N ? s->profile : 1], A_PROFILE, NULL, FLEX_C_TXT);
    snprintf(v, sizeof(v), "%u", (unsigned)s->quality);
    row(L, "Calidad de imagen", v, A_QUALITY, NULL, FLEX_C_TXT);
    snprintf(v, sizeof(v), "%u %%", (unsigned)s->scalePct);
    row(L, "Escala del viewport", v, A_SCALE, NULL, FLEX_C_TXT);
    row(L, "Guardar historial", s->saveHistory ? "Sí" : "No", A_HIST, NULL, FLEX_C_TXT);
    row(L, "Telemetría local", s->telemetry ? "Sí" : "No", A_TEL, NULL, FLEX_C_TXT);
    snprintf(v, sizeof(v), "%s · Solo para desarrollo: abre IP y nombres internos", s->allowLocal ? "Sí" : "No");
    row(L, "Permitir red local", v, A_LOCAL, NULL, FLEX_C_TXT);
    snprintf(v, sizeof(v), "%s · Solo para desarrollo: el tráfico viaja en claro", s->allowInsecure ? "Sí" : "No");
    row(L, "Permitir ws:// sin TLS", v, A_INSEC, NULL, FLEX_C_TXT);
    row(L, "Borrar historial", NULL, A_CLR_H, NULL, FLEX_C_DANGER);
    row(L, "Borrar favoritos", NULL, A_CLR_B, NULL, FLEX_C_DANGER);
    row(L, "Borrar todos los datos de navegación", NULL, A_CLR_ALL, NULL, FLEX_C_DANGER);
    s_row_wrap = false;
}

static void build_about(lv_obj_t *L)
{
    char b[128];
    text(L, FLEX_FONT_BODY, FLEX_C_TXT, "Navegador de Flex OS");
    snprintf(b, sizeof(b), "Plataforma: %s", FLEXBR_PLAT_NAME);
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    snprintf(b, sizeof(b), "Protocolo: FBP/%d", FBP_VERSION);
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    int used = 0;
    for (int k = 0; k < FLEXBR_MAX_TABS; k++) {
        used += U->tab[k].used;
    }
    snprintf(b, sizeof(b), "Pestañas: %d de %d", used, FLEXBR_MAX_TABS);
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    snprintf(b, sizeof(b), "Viewport: %ux%u (renegociado %u veces)", (unsigned)U->stats.viewW,
             (unsigned)U->stats.viewH, (unsigned)U->stats.viewportMsgs);
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    if (U->stats.relayKind == BRRELAY_PHONE) {
        if (U->stats.relayBuild >= 0 && U->stats.relayBuild >= FLEXBR_RELAY_MIN_BUILD) {
            snprintf(b, sizeof(b), "Relay del teléfono: build r%d", U->stats.relayBuild);
        } else {
            snprintf(b, sizeof(b), "Relay del teléfono: ANTIGUO, reinstala Flex Phone");
        }
        text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    } else if (U->stats.relayKind == BRRELAY_OTHER) {
        text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, "Servicio: Ubuntu/PC");
    }
    snprintf(b, sizeof(b), "Origen: %s", U->src.src != BRSRC_NONE ? flexBrSourceName(U->src.src) : U->src.why);
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    watch_theme_section(L, "Capacidades");
    static const struct {
        uint32_t cap;
        const char *name;
    } kCaps[] = {{FLEXBR_CAP_REMOTE, "Sesión remota"}, {FLEXBR_CAP_TILES, "Regiones sucias"},
                 {FLEXBR_CAP_MEDIA, "Reproductor de vídeo"}, {FLEXBR_CAP_TABS, "Varias pestañas"},
                 {FLEXBR_CAP_PSRAM, "PSRAM"}, {FLEXBR_CAP_FS, "Almacenamiento"}};
    for (size_t i = 0; i < sizeof(kCaps) / sizeof(kCaps[0]); i++) {
        const bool on = U->caps & kCaps[i].cap;
        snprintf(b, sizeof(b), "%s: %s", kCaps[i].name, on ? "Sí" : cap_reason(kCaps[i].cap));
        text(L, FLEX_FONT_SMALL, on ? FLEX_C_TXT2 : FLEX_C_MUTE, b);
    }
    watch_theme_section(L, "Telemetría local");
    if (!U->set.telemetry) {
        text(L, FLEX_FONT_SMALL, FLEX_C_MUTE,
             "La telemetría está desactivada. Se puede encender en Ajustes; no sale del dispositivo.");
        return;
    }
    const BrStats *s = &U->stats;
    snprintf(b, sizeof(b), "PSRAM libre: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    static const char *const kN[] = {"Frames dibujados", "Frames descartados", "Frames corruptos", "Ida y vuelta",
                                     "Recibido", "Enviado", "Reconexiones", "Navegaciones", "Errores"};
    const uint32_t kv[] = {s->framesOk, s->framesDropped, s->framesBad, s->rttMs, s->bytesIn / 1024,
                           s->bytesOut / 1024, s->reconnects, s->navCount, s->errCount};
    static const char *const kU[] = {"", "", "", " ms", " KB", " KB", "", "", ""};
    for (size_t i = 0; i < sizeof(kv) / sizeof(kv[0]); i++) {
        snprintf(b, sizeof(b), "%s: %u%s", kN[i], (unsigned)kv[i], kU[i]);
        text(L, FLEX_FONT_SMALL, FLEX_C_TXT2, b);
    }
}

static void build_inner(int kind)
{
    lv_obj_t *L = U->inner;
    lv_obj_clean(L);
    br_lists_t *ls = watch_browser_lists();
    if (kind == V_TABS) {
        text(L, FLEX_FONT_TITLE, FLEX_C_TXT, "Pestañas");
        for (int i = 0; i < FLEXBR_MAX_TABS; i++) {
            if (!U->tab[i].used) {
                continue;
            }
            char lbl[FLEXBR_TITLE_MAX];
            flexBrUrlLabel(U->tab[i].url, lbl, sizeof(lbl));
            lv_obj_t *r = row(L, U->tab[i].title[0] ? U->tab[i].title : "Nueva pestaña", lbl, A_TAB,
                              (void *)(intptr_t)i, i == U->act ? FLEX_C_ACCENT_LIGHT : FLEX_C_TXT);
            lv_obj_t *x = flex_glass_button(r, "×", FLEX_GLASS_TINT_NONE, 48, 40);
            lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_align(x, LV_ALIGN_RIGHT_MID, 6, 0);
            lv_obj_set_user_data(x, (void *)(intptr_t)i);
            lv_obj_add_event_cb(x, inner_cb, LV_EVENT_CLICKED, (void *)(intptr_t)A_TABCLOSE);
        }
        lv_obj_t *nb = flex_glass_button(L, "Nueva pestaña", FLEX_C_PRIMARY, LV_PCT(100), 50);
        lv_obj_add_event_cb(nb, inner_cb, LV_EVENT_CLICKED, (void *)(intptr_t)A_NEWTAB);
        return;
    }
    const int pg = T()->page;
    if (pg != BRP_NEWTAB && pg != BRP_SETTINGS) {
        text(L, FLEX_FONT_TITLE, FLEX_C_TXT, flexBrPageTitle(pg));
    }
    switch (pg) {
    case BRP_NEWTAB: {
        text(L, FLEX_FONT_TITLE, FLEX_C_TXT, "Navegador de Flex OS Ultra");
        const char *st;
        uint32_t c = FLEX_C_TXT2;
        if (!(U->caps & FLEXBR_CAP_REMOTE)) {
            st = cap_reason(FLEXBR_CAP_REMOTE);
            c = C_WARN;
        } else if (U->net_state == BRN_READY) {
            st = "Sesión remota lista";
            c = C_OK;
        } else if (U->net_state == BRN_NOWIFI) {
            st = "Sin Wi-Fi";
            c = C_ERR;
        } else if (U->net_state == BRN_CONNECTING || U->net_state == BRN_HANDSHAKE) {
            st = "Conectando con el servicio...";
        } else {
            st = "Servicio no disponible";
            c = C_WARN;
        }
        text(L, FLEX_FONT_SMALL, c, st);
        if (ls && ls->bn) {
            watch_theme_section(L, "Favoritos");
            for (int i = 0; i < ls->bn && i < 6; i++) {
                char lbl[FLEXBR_TITLE_MAX];
                flexBrUrlLabel(ls->b[i].url, lbl, sizeof(lbl));
                row(L, ls->b[i].title, lbl, A_URL, ls->b[i].url, FLEX_C_TXT);
            }
        } else {
            text(L, FLEX_FONT_SMALL, FLEX_C_MUTE, "Escribe una dirección o una búsqueda en la barra de arriba.");
        }
        if (!(U->caps & FLEXBR_CAP_REMOTE)) {
            lv_obj_t *b = flex_glass_button(L, "Ajustes del navegador", FLEX_C_PRIMARY, LV_PCT(100), 52);
            lv_obj_add_event_cb(b, menu_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)M_SETTINGS);
        }
        break;
    }
    case BRP_HISTORY:
        if (!ls || ls->hn == 0) {
            text(L, FLEX_FONT_BODY, FLEX_C_TXT2, "No hay nada en el historial");
        }
        for (int i = 0; ls && i < ls->hn && i < 40; i++) {
            char lbl[FLEXBR_TITLE_MAX + 24], host[FLEXBR_TITLE_MAX];
            flexBrUrlLabel(ls->h[i].url, host, sizeof(host));
            snprintf(lbl, sizeof(lbl), "%s · %u", host, (unsigned)ls->h[i].visits);
            row(L, ls->h[i].title[0] ? ls->h[i].title : host, lbl, A_URL, ls->h[i].url, FLEX_C_TXT);
        }
        break;
    case BRP_BOOKMARKS:
        if (!ls || ls->bn == 0) {
            text(L, FLEX_FONT_BODY, FLEX_C_TXT2, "Aún no hay favoritos");
        }
        for (int i = 0; ls && i < ls->bn; i++) {
            char lbl[FLEXBR_TITLE_MAX];
            flexBrUrlLabel(ls->b[i].url, lbl, sizeof(lbl));
            row(L, ls->b[i].title, lbl, A_URL, ls->b[i].url, FLEX_C_TXT);
        }
        break;
    case BRP_DOWNLOADS: {
        text(L, FLEX_FONT_SMALL, FLEX_C_TXT2,
             "Este dispositivo no guarda descargas: no hay espacio para ficheros grandes y el contenido lo sirve "
             "el servicio remoto. Aquí se listan los archivos que las páginas han intentado descargar.");
        br_net_t *n = net();
        int k = 0;
        if (n) {
            br_net_lock(n);
            for (; k < n->dl_n; k++) {
                char sz[24];
                const uint32_t b = n->dl[k].size;
                if (b >= 1024u * 1024u) {
                    snprintf(sz, sizeof(sz), "%.1f MB", (double)b / (1024.0 * 1024.0));
                } else if (b >= 1024u) {
                    snprintf(sz, sizeof(sz), "%u KB", (unsigned)(b / 1024));
                } else {
                    snprintf(sz, sizeof(sz), "%u B", (unsigned)b);
                }
                row(L, n->dl[k].name, sz, A_NONE, NULL, FLEX_C_TXT);
            }
            br_net_unlock(n);
        }
        if (k == 0) {
            text(L, FLEX_FONT_BODY, FLEX_C_MUTE, "Ninguna descarga en esta sesión");
        }
        break;
    }
    case BRP_OFFLINE:
        text(L, FLEX_FONT_SMALL, C_WARN,
             !watch_browser_online()                ? "El dispositivo no está conectado a ninguna red Wi-Fi."
             : (U->caps & FLEXBR_CAP_REMOTE)         ? "No se puede hablar con el servicio de navegación."
                                                     : cap_reason(FLEXBR_CAP_REMOTE));
        text(L, FLEX_FONT_SMALL, FLEX_C_TXT2,
             "El historial, los favoritos y las páginas internas siguen funcionando sin conexión.");
        break;
    case BRP_SETTINGS:
        text(L, FLEX_FONT_TITLE, FLEX_C_TXT, flexBrPageTitle(pg));
        build_settings(L);
        break;
    case BRP_ABOUT: build_about(L); break;
    default: break;
    }
    U->lists_gen = ls ? ls->gen : 0;
}

/* ======================= estado y barra ======================= */

static void update_status(void)
{
    static const uint32_t kDot[] = {C_WARN, C_ERR, C_WARN, C_WARN, C_OK, C_ERR};
    const int st = U->net_state;
    lv_obj_set_style_bg_color(U->st_dot, flex_color(kDot[st >= 0 && st <= BRN_ERROR ? st : BRN_ERROR]), 0);
    lv_label_set_text(U->st_name, br_net_state_name(st));
    char t[160];
    ui_phone_text(U->info, t, sizeof(t), FLEX_FONT_SMALL);
    lv_label_set_text(U->st_info, t);
    char srv[FLEXBR_SERVER_MAX + 32];
    const watch_browser_source_t *src = &U->src;
    if (src->src == BRSRC_PHONE) {
        snprintf(srv, sizeof(srv), "Flex Phone · %u.%u.%u.%u:%u", src->relay_ip[0], src->relay_ip[1],
                 src->relay_ip[2], src->relay_ip[3], (unsigned)src->relay_port);
    } else if (src->src == BRSRC_CLOUD) {
        snprintf(srv, sizeof(srv), "Nube · %s", U->set.cloudServer);
    } else if (src->src == BRSRC_MANUAL) {
        snprintf(srv, sizeof(srv), "PC · %s", U->set.server);
    } else {
        snprintf(srv, sizeof(srv), "Sin origen");
    }
    ui_phone_text(srv, t, sizeof(t), FLEX_FONT_SMALL); /* nunca la credencial */
    lv_label_set_text(U->st_srv, t);
    snprintf(t, sizeof(t), "mensajes %u  ·  imágenes %u  ·  err %u  ·  %u KB", (unsigned)U->msgs,
             (unsigned)U->stats.framesOk, (unsigned)U->stats.errCount, (unsigned)(U->stats.bytesIn / 1024));
    lv_label_set_text(U->st_cnt, t);
    const char *hint = "";
    if (src->src == BRSRC_NONE) {
        /* el motivo ya está en la línea de la etapa: aquí, qué hacer */
        hint = src->phone_possible ? "Enciéndelo en Flex Phone > Web del teléfono o cambia el origen en flex://settings"
                                   : "Configura el servicio en flex://settings";
    } else if (st == BRN_NOWIFI) {
        hint = "Conecta el Wi-Fi desde Ajustes del sistema";
    } else if (strncmp(U->info, "Sin imágenes", 13) == 0) {
        hint = "El servicio responde pero no manda imágenes";
    }
    lv_label_set_text(U->st_hint, hint);
}

static void refresh_chrome(void)
{
    const ub_tab_t *t = T();
    const bool remote = t->page == BRP_NONE && U->view == V_TAB;
    char lbl[FLEXBR_TITLE_MAX + 8];
    if (U->view == V_TABS) {
        snprintf(lbl, sizeof(lbl), "Pestañas");
    } else if (t->page != BRP_NONE) {
        snprintf(lbl, sizeof(lbl), "%s", flexBrPageTitle(t->page));
    } else if (t->url[0]) {
        flexBrUrlLabel(t->url, lbl, sizeof(lbl));
    } else {
        snprintf(lbl, sizeof(lbl), "Buscar o escribir dirección");
    }
    char sane[FLEXBR_TITLE_MAX + 24];
    ui_phone_text(lbl, sane, sizeof(sane), FLEX_FONT_SMALL);
    lv_label_set_text(U->omni_lbl, sane);
    const bool secure = remote && (t->flags & FBP_ST_SECURE), insecure = remote && (t->flags & FBP_ST_INSECURE);
    set_hidden(U->omni_dot, !secure && !insecure);
    lv_obj_set_style_bg_color(U->omni_dot, flex_color(secure ? C_OK : C_WARN), 0);
    lv_obj_set_style_opa(U->back, remote && !(t->flags & FBP_ST_CAN_BACK) ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(U->fwd, remote && (t->flags & FBP_ST_CAN_FWD) ? LV_OPA_COVER : LV_OPA_40, 0);
    const bool loading = remote && (t->flags & FBP_ST_LOADING);
    set_hidden(U->stop_i, !loading);
    set_hidden(U->reload_i, loading);
    if (loading) {
        const int p = t->progress < 3 ? 3 : t->progress;
        lv_obj_set_width(U->progress, PROG_W * p / 100);
        lv_obj_set_style_bg_color(U->progress, flex_color(FLEX_C_PRIMARY), 0);
    } else {
        lv_obj_set_width(U->progress, PROG_W);
        lv_obj_set_style_bg_color(U->progress, flex_color(FLEX_C_DIVIDER), 0);
    }
}

static void show_view(void)
{
    const bool inner = U->view == V_TABS || T()->page != BRP_NONE;
    set_hidden(U->inner, !inner);
    set_hidden(U->canvas, inner || !U->has_frame);
    set_hidden(U->status, inner || U->has_frame);
    if (inner) {
        build_inner(U->view);
        lv_obj_scroll_to_y(U->inner, 0, LV_ANIM_OFF);
    } else {
        update_status();
    }
    refresh_chrome();
}

/* ======================= barra de herramientas ======================= */

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (U->ed != ED_NONE) { /* el teclado de la página se cierra con "atrás" */
        close_edit();
        return;
    }
    if (U->view == V_TABS) {
        U->view = V_TAB;
        show_view();
        return;
    }
    ub_tab_t *t = T();
    if (t->page != BRP_NONE) { /* en una página interna: a la remota, o a nueva pestaña */
        if (t->page != BRP_NEWTAB) {
            if (t->seen_nav || t->flags) {
                t->page = BRP_NONE;
                br_net_t *n = net();
                if (n) {
                    br_net_cmd(n, FBP_C_REQ_FRAME, ch_of(U->act));
                }
                show_view();
            } else {
                open_internal(BRP_NEWTAB);
            }
        }
        return;
    }
    br_net_t *n = net();
    if (n && (t->flags & FBP_ST_CAN_BACK)) {
        br_net_cmd(n, FBP_C_BACK, ch_of(U->act));
    } else {
        open_internal(BRP_NEWTAB);
    }
}

static void fwd_cb(lv_event_t *e)
{
    (void)e;
    br_net_t *n = net();
    if (n && T()->page == BRP_NONE && (T()->flags & FBP_ST_CAN_FWD)) {
        br_net_cmd(n, FBP_C_FORWARD, ch_of(U->act));
    }
}

static void reload_cb(lv_event_t *e)
{
    (void)e;
    br_net_t *n = net();
    if (T()->page != BRP_NONE || U->view == V_TABS) {
        show_view(); /* interna: se repinta */
    } else if (n) {
        br_net_cmd(n, T()->flags & FBP_ST_LOADING ? FBP_C_STOP : FBP_C_RELOAD, ch_of(U->act));
    }
}

static void omni_cb(lv_event_t *e)
{
    (void)e;
    open_edit(ED_OMNI);
}

static void menu_cb(lv_event_t *e)
{
    (void)e;
    open_menu();
}

static void dots_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = flex_color(FLEX_C_TXT);
    d.radius = LV_RADIUS_CIRCLE;
    for (int i = -1; i <= 1; i++) {
        const lv_area_t r = {cx - 3, cy + i * 9 - 3, cx + 3, cy + i * 9 + 3};
        lv_draw_rect(lv_event_get_layer(e), &d, &r);
    }
}

static void reload_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = 10;
    d.width = 3;
    d.start_angle = 300;
    d.end_angle = 240;
    d.color = flex_color(FLEX_C_TXT);
    d.rounded = 1;
    lv_draw_arc(lv_event_get_layer(e), &d);
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = flex_color(FLEX_C_TXT);
    t.p[0].x = cx + 3;
    t.p[0].y = cy - 15;
    t.p[1].x = cx + 11;
    t.p[1].y = cy - 9;
    t.p[2].x = cx + 2;
    t.p[2].y = cy - 4;
    lv_draw_triangle(lv_event_get_layer(e), &t);
}

static lv_obj_t *tb_button(lv_obj_t *parent, int32_t x, lv_event_cb_t cb)
{
    lv_obj_t *b = flex_glass_control(parent, x, BB_Y, TB_BTN, TB_BTN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *draw_icon(lv_obj_t *parent, lv_event_cb_t draw)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 30, 30);
    lv_obj_center(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

/* ======================= la página remota ======================= */

static void page_touch_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    lv_point_t p;
    /* LV_EVENT_ALL: al borrar la página, delete_cb (en la página) libera U
     * antes de que se borren los hijos, y el lienzo aún recibe PRESS_LOST /
     * DELETE si el dedo seguía apoyado. */
    if (!U || !in) {
        return;
    }
    lv_indev_get_point(in, &p);
    br_net_t *n = net();
    const uint8_t ch = ch_of(U->act);
    if (code == LV_EVENT_PRESSED) {
        U->pressed = true;
        U->dragging = false;
        U->p0 = U->plast = p;
        U->acc_dx = U->acc_dy = 0;
        U->last_scroll = lv_tick_get();
        return;
    }
    if (!U->pressed) {
        return; /* un dedo que viene de otra capa se ignora */
    }
    if (code == LV_EVENT_PRESSING) {
        if (!U->dragging && (LV_ABS(p.y - U->p0.y) > 8 || LV_ABS(p.x - U->p0.x) > 10)) {
            U->dragging = true;
        }
        if (U->dragging) {
            U->acc_dx += p.x - U->plast.x;
            U->acc_dy += p.y - U->plast.y;
            U->plast = p;
            if (n && lv_tick_get() - U->last_scroll >= SCROLL_MS && (U->acc_dx || U->acc_dy)) {
                br_net_scroll(n, ch, (int16_t)-U->acc_dx, (int16_t)-U->acc_dy);
                U->acc_dx = U->acc_dy = 0;
                U->last_scroll = lv_tick_get();
            }
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        U->pressed = false;
        if (U->dragging) {
            if (n && (U->acc_dx || U->acc_dy)) {
                br_net_scroll(n, ch, (int16_t)-U->acc_dx, (int16_t)-U->acc_dy);
            }
        } else if (n && code == LV_EVENT_RELEASED) {
            lv_area_t a;
            lv_obj_get_coords(lv_event_get_current_target_obj(e), &a);
            const int32_t x = p.x - a.x1, y = p.y - a.y1;
            if (x >= 0 && y >= 0 && x < PG_W && y < page_h()) {
                br_net_tap(n, ch, (uint16_t)x, (uint16_t)y);
            }
        }
        U->dragging = false;
    }
}

/* Copia de la sesión y un FRAME como mucho por vuelta (brFlexTick) */
static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (U->err_until && (int32_t)(lv_tick_get() - U->err_until) >= 0) {
        hide_err();
    }
    if (U->need_retry && lv_tick_elaps(U->retry_ms) >= SESSION_RETRY_MS) {
        ensure_session();
    }
    if (U->probing) {
        char m[64];
        if (watch_browser_probe_result(m, sizeof(m))) {
            U->probing = false;
            show_err(m);
            if (U->view == V_TAB && T()->page == BRP_SETTINGS) {
                build_inner(V_TAB);
            }
        }
    }
    br_lists_t *ls = watch_browser_lists();
    watch_browser_persist(false);
    if (lv_tick_get() - U->src_ms >= 500) { /* el relay del teléfono va y viene solo */
        U->src_ms = lv_tick_get();
        watch_browser_source_t ns;
        watch_browser_source(&ns);
        if (ns.src != U->src.src || strcmp(ns.why, U->src.why) != 0 || ns.relay_port != U->src.relay_port ||
            memcmp(ns.relay_ip, U->src.relay_ip, 4) != 0 || ns.phone_possible != U->src.phone_possible) {
            const bool moved = ns.src != U->src.src || ns.relay_port != U->src.relay_port ||
                               memcmp(ns.relay_ip, U->src.relay_ip, 4) != 0;
            U->src = ns;
            U->caps = watch_browser_caps();
            ensure_session();
            ub_tab_t *ct = T();
            br_net_t *sn = net();
            if (moved && ns.src != BRSRC_NONE && sn && ct->page == BRP_NONE && ct->url[0]) {
                /* Otro servicio: la página que se veía no existe allí. Se pide
                 * de nuevo (queda en cola hasta que la sesión nueva esté lista). */
                clear_canvas();
                ct->flags = FBP_ST_LOADING;
                ct->progress = 1;
                br_net_navigate(sn, ch_of(U->act), ct->url);
                refresh_chrome();
                show_view();
            } else if (moved && ns.src == BRSRC_NONE && ct->page == BRP_NONE && U->has_frame) {
                /* Sin servicio: una imagen vieja no es la página; se enseña el motivo */
                clear_canvas();
                show_view();
                update_status();
            }
            if (U->view == V_TAB && (ct->page == BRP_NEWTAB || ct->page == BRP_SETTINGS) && !U->edit) {
                build_inner(V_TAB);
            } else if (U->view == V_TAB && ct->page == BRP_NONE && !U->has_frame) {
                update_status();
            }
        }
    }
    br_net_t *n = net();
    if (!n) {
        return;
    }
    char err[BR_INFO_MAX] = "";
    br_tab_state_t rs;
    bool changed = false;
    br_net_lock(n);
    const int st = n->state;
    if (n->err_gen != U->err_gen) {
        U->err_gen = n->err_gen;
        snprintf(err, sizeof(err), "%s", n->err);
    }
    const bool info = n->info_gen != U->info_gen || st != U->net_state || n->msgs_in != U->msgs;
    if (info) {
        U->info_gen = n->info_gen;
        snprintf(U->info, sizeof(U->info), "%s", n->info);
    }
    U->msgs = n->msgs_in;
    U->stats = n->stats;
    memcpy(&rs, &n->tab[ch_of(U->act)], sizeof(rs));
    br_net_unlock(n);
    if (err[0]) {
        show_err(err);
    }
    const bool state_changed = st != U->net_state;
    U->net_state = st;
    ub_tab_t *t = T();
    if (rs.gen != t->seen_gen) { /* STATE de la pestaña activa */
        t->seen_gen = rs.gen;
        t->flags = rs.flags;
        t->progress = rs.progress;
        if (rs.title[0]) {
            snprintf(t->title, sizeof(t->title), "%s", rs.title);
            if (ls && t->page == BRP_NONE) {
                br_hist_title(ls, t->url, rs.title);
            }
        }
        changed = true;
        /* foco en un campo de la página: se abre su teclado, y se cierra al perderlo */
        if ((rs.flags & FBP_ST_EDITING) && U->ed == ED_NONE && t->page == BRP_NONE && !U->menu) {
            open_edit(ED_PAGEKEY);
        } else if (!(rs.flags & FBP_ST_EDITING) && U->ed == ED_PAGEKEY) {
            close_edit();
        }
    }
    if (rs.nav_gen != t->seen_nav) { /* NAVIGATED: la URL buena y el historial */
        t->seen_nav = rs.nav_gen;
        if (t->page == BRP_NONE) {
            snprintf(t->url, sizeof(t->url), "%s", rs.url);
            if (ls && U->set.saveHistory) {
                br_hist_add(ls, rs.url, t->title, lv_tick_get());
            }
        }
        changed = true;
    }
    if (changed) {
        refresh_chrome();
    }
    const bool on_page = U->view == V_TAB && t->page == BRP_NONE;
    if (on_page && !U->has_frame && (info || state_changed)) {
        update_status();
    }
    if (!on_page && state_changed && U->view == V_TAB && t->page == BRP_NEWTAB) {
        build_inner(V_TAB); /* la línea de estado de la nueva pestaña */
    }
    if (!on_page && ls && ls->gen != U->lists_gen && U->view == V_TAB &&
        (t->page == BRP_HISTORY || t->page == BRP_BOOKMARKS)) {
        build_inner(V_TAB);
    }
    push_viewport();
    FbpFrame f;
    uint8_t ch;
    uint16_t seq;
    if (!br_net_frame_peek(n, &f, &ch, &seq)) {
        return;
    }
    /* Oculta (app suspendida con el gesto hacia arriba): no se decodifica;
     * al volver se pide una imagen nueva (resume_cb). */
    if (!on_page || U->menu || ch != ch_of(U->act) || !U->buf || lv_obj_has_flag(U->page, LV_OBJ_FLAG_HIDDEN)) {
        br_net_frame_done(n, BR_FR_SKIPPED);
        return;
    }
    const int ph = page_h();
    if (f.x >= PG_W || f.y >= ph) {
        br_net_frame_done(n, BR_FR_SKIPPED);
        return;
    }
    const int r = flex_jpeg_draw(f.img, f.imgLen, U->buf, PG_W, ph, f.x, f.y, f.w, f.h);
    br_net_frame_done(n, r == 0 ? BR_FR_DRAWN : BR_FR_BAD);
    if (r != 0) {
        snprintf(U->info, sizeof(U->info), "Imagen ilegible (código %d)", r);
        br_net_cmd(n, FBP_C_REQ_FRAME, ch);
        return;
    }
    lv_image_cache_drop(lv_canvas_get_image(U->canvas));
    lv_area_t a;
    lv_obj_get_coords(U->canvas, &a);
    const lv_area_t inv = {a.x1 + f.x, a.y1 + f.y, a.x1 + LV_MIN(f.x + f.w, PG_W) - 1,
                           a.y1 + LV_MIN(f.y + f.h, ph) - 1};
    lv_obj_invalidate_area(U->canvas, &inv);
    if (!U->has_frame) {
        U->has_frame = true;
        show_view();
    }
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    const ub_tab_t *t = T();
    watch_browser_set_last(t->page == BRP_NONE ? t->url : "");
    watch_browser_close();
    if (U->buf) {
        heap_caps_free(U->buf);
    }
    lv_free(U);
    U = NULL;
}

/* Al retomar la app suspendida: la imagen de la página puede ser vieja. */
static void resume_cb(lv_event_t *e)
{
    (void)e;
    br_net_t *n = net();
    if (U && n) {
        br_net_cmd(n, FBP_C_REQ_FRAME, ch_of(U->act));
    }
}

lv_obj_t *ui_browser_create(lv_obj_t *parent)
{
    if (U) {
        return NULL; /* una sola instancia: la sesión y el estado son únicos */
    }
    U = lv_malloc_zeroed(sizeof(*U));
    LV_ASSERT_MALLOC(U);
    watch_browser_get_settings(&U->set);
    watch_browser_source(&U->src);
    U->src_ms = lv_tick_get();
    U->caps = watch_browser_caps();
    U->net_state = -1;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    U->page = page;

    /* arriba: el omnibox; abajo: [<] [>] [recargar] [menú] */
    U->omni = flex_glass_control(page, OMNI_X, TB_Y, OMNI_W, TB_BTN);
    lv_obj_set_style_radius(U->omni, TB_BTN / 2, 0);
    lv_obj_add_event_cb(U->omni, omni_cb, LV_EVENT_CLICKED, NULL);
    U->omni_dot = lv_obj_create(U->omni);
    lv_obj_remove_style_all(U->omni_dot);
    lv_obj_set_size(U->omni_dot, 10, 10);
    lv_obj_set_style_radius(U->omni_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(U->omni_dot, LV_OPA_COVER, 0);
    lv_obj_align(U->omni_dot, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_remove_flag(U->omni_dot, LV_OBJ_FLAG_CLICKABLE);
    U->omni_lbl = watch_theme_label(U->omni, FLEX_FONT_SMALL, FLEX_C_TXT, "");
    lv_obj_set_size(U->omni_lbl, OMNI_W - 40, lv_font_get_line_height(FLEX_FONT_SMALL));
    lv_label_set_long_mode(U->omni_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(U->omni_lbl, LV_ALIGN_LEFT_MID, 28, 0);
    const int32_t bx0 = (FLEX_SCREEN_W - (4 * TB_BTN + 3 * 18)) / 2;
    U->back = tb_button(page, bx0, back_cb);
    flex_icon_create(U->back, FLEX_ICON_CHEVRON_LEFT, 24, FLEX_C_TXT);
    U->fwd = tb_button(page, bx0 + (TB_BTN + 18), fwd_cb);
    flex_icon_create(U->fwd, FLEX_ICON_CHEVRON_RIGHT, 24, FLEX_C_TXT);
    lv_obj_t *rl = tb_button(page, bx0 + 2 * (TB_BTN + 18), reload_cb);
    U->reload_i = draw_icon(rl, reload_draw_cb);
    U->stop_i = flex_icon_create(rl, FLEX_ICON_STOP, 22, FLEX_C_TXT);
    lv_obj_center(U->stop_i);
    U->menu_btn = tb_button(page, bx0 + 3 * (TB_BTN + 18), menu_cb);
    draw_icon(U->menu_btn, dots_draw_cb);
    U->progress = lv_obj_create(page);
    lv_obj_remove_style_all(U->progress);
    lv_obj_set_pos(U->progress, PROG_X, PG_Y - 3);
    lv_obj_set_size(U->progress, PROG_W, 3);
    lv_obj_set_style_bg_opa(U->progress, LV_OPA_COVER, 0);

    /* lienzo de la página (PSRAM) */
    U->buf = heap_caps_calloc((size_t)PG_W * PG_H, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    U->canvas = lv_canvas_create(page);
    if (U->buf) {
        lv_canvas_set_buffer(U->canvas, U->buf, PG_W, PG_H, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_set_pos(U->canvas, 0, PG_Y);
    lv_obj_add_flag(U->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(U->canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(U->canvas, page_touch_cb, LV_EVENT_ALL, NULL);

    /* panel de estado de la sesión (sin imagen todavía) */
    U->status = lv_obj_create(page);
    lv_obj_remove_style_all(U->status);
    lv_obj_set_pos(U->status, FLEX_SAFE_X, PG_Y + 40);
    lv_obj_set_size(U->status, FLEX_SCREEN_W - 2 * FLEX_SAFE_X, PG_H - 60);
    lv_obj_set_flex_flow(U->status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(U->status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(U->status, 8, 0);
    lv_obj_remove_flag(U->status, LV_OBJ_FLAG_SCROLLABLE);
    U->st_dot = lv_obj_create(U->status);
    lv_obj_remove_style_all(U->st_dot);
    lv_obj_set_size(U->st_dot, 14, 14);
    lv_obj_set_style_radius(U->st_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(U->st_dot, LV_OPA_COVER, 0);
    U->st_name = watch_theme_label(U->status, FLEX_FONT_TITLE, FLEX_C_TXT, "");
    U->st_info = text(U->status, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    U->st_srv = text(U->status, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    U->st_cnt = text(U->status, FLEX_FONT_SMALL, FLEX_C_MUTE, "");
    U->st_hint = text(U->status, FLEX_FONT_SMALL, C_WARN, "");
    lv_obj_t *const kc[] = {U->st_info, U->st_srv, U->st_cnt, U->st_hint};
    for (size_t i = 0; i < 4; i++) {
        lv_obj_set_style_text_align(kc[i], LV_TEXT_ALIGN_CENTER, 0);
    }

    /* páginas internas */
    U->inner = watch_theme_list(page);
    lv_obj_set_pos(U->inner, 0, PG_Y + 2);
    lv_obj_set_height(U->inner, PG_H - 2);
    lv_obj_set_style_pad_bottom(U->inner, 8, 0);

    /* barra de error flotante (6 s) */
    /* aviso de 6 s sobre la barra de abajo (dentro del arco), en dos líneas */
    U->errbar = flex_glass_tile(page, 82, BB_Y - 4, 246, 52);
    lv_obj_set_style_bg_color(U->errbar, flex_color(0xB43C3C), 0);
    lv_obj_set_style_pad_ver(U->errbar, 2, 0);
    lv_obj_set_style_pad_hor(U->errbar, 10, 0);
    lv_obj_set_style_radius(U->errbar, 22, 0);
    lv_obj_add_event_cb(U->errbar, errbar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = watch_theme_label(U->errbar, FLEX_FONT_SMALL, FLEX_C_WHITE, "");
    lv_obj_set_width(el, LV_PCT(100));
    lv_label_set_long_mode(el, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(el, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(el);
    lv_obj_add_flag(U->errbar, LV_OBJ_FLAG_HIDDEN);

    /* pestaña 0 y sesión */
    U->tab[0].used = true;
    U->tab[0].last_use = lv_tick_get();
    U->act = 0;
    ensure_session(); /* con la geometría antes de la red: el HELLO lleva el tamaño */
    char last[FLEXBR_URL_MAX];
    watch_browser_get_last(last, sizeof(last));
    if (last[0] && (U->caps & FLEXBR_CAP_REMOTE) && flexBrCheckUrl(last, &U->set) == BRB_NONE &&
        watch_browser_online()) {
        navigate(last);
    } else {
        open_internal(BRP_NEWTAB);
    }
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(page, resume_cb, (lv_event_code_t)ui_nav_event_resume(), NULL);
    ui_page_timer(page, tick_cb, TICK_MS, NULL);
    return page;
}
