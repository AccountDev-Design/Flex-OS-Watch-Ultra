/*
 * ui_face_editor — selector de esferas (rediseño 2026-10, diseño del usuario:
 * design/referencias/editor_esferas.png).
 *
 * Carrusel horizontal de tarjetas de 276x358 (esquinas de 26 px) con la esfera
 * centrada y las vecinas asomando a los lados, una fila de puntos arriba (uno
 * por esfera; el de la centrada, blanco) y el nombre debajo.
 *
 * Se abre:
 *  - manteniendo pulsada la esfera sin moverse: la esfera "se aleja" hasta su
 *    tarjeta (zoom) mientras aparecen las vecinas y los puntos;
 *  - desde Ajustes > Pantalla y fondo de pantalla > Esfera: entra deslizando.
 * Tocar la tarjeta centrada la aplica: crece hasta llenar la pantalla y se
 * vuelve a la esfera (también desde Ajustes). Tocar una vecina la centra.
 * Salir sin cambiar nada: el gesto del borde derecho.
 *
 * Vistas previas: las esferas REALES (ui_face_create_style: hora, fecha, fondo
 * y complicaciones de ahora mismo) capturadas con lv_snapshot y reducidas con
 * flex_thumb (media de área, escala uniforme: no se deforman). Ciclo de vida
 * y memoria (siguen las reglas del arreglo del 2026-10-20, docs/F11-ESTABILIDAD.md):
 *  - NINGÚN render dentro de un callback de entrada: las capturas salen de un
 *    temporizador, también la de la esfera al mantenerla pulsada (se toma en
 *    un temporizador de un solo disparo, después del evento).
 *  - Ni durante una animación, un gesto de navegación o el desplazamiento del
 *    carrusel: la siguiente vista previa espera.
 *  - Ventana deslizante: solo hay miniaturas de la centrada y de dos a cada
 *    lado (5 x 193 KB en PSRAM como mucho); al desplazarse se sueltan las que
 *    quedan lejos y se generan las nuevas. Antes de reservar se comprueba el
 *    bloque contiguo mayor de PSRAM; sin sitio, la tarjeta queda gris con su
 *    nombre debajo (nunca un fallo de reserva sin tratar).
 *  - Un búfer de captura (410x502 RGB565, 411 KB) y el del fondo de las
 *    esferas temporales (watch_theme_preview_*), solo mientras se generan.
 *  - Las esferas temporales se crean, se capturan y se borran en el mismo tick
 *    con el tema en modo vista previa: su fondo nunca pisa el de la esfera
 *    activa (antes, la vista previa de una esfera con otro fondo lo
 *    reescribía y la esfera activa mostraba el fondo equivocado).
 *  - Sin clip_corner ni opacidad de grupo (capas ARGB8888 por cuadro): las
 *    esquinas vienen redondeadas en la miniatura y los fundidos cambian la
 *    opacidad de cada imagen, no la de un contenedor.
 *  - Al borrarse la página se paran la animación y los temporizadores y se
 *    sueltan primero las imágenes y después los píxeles.
 * La esfera elegida se guarda en los ajustes (watch_settings, NVS: se
 * recupera tras reiniciar) y la raíz se rehace fuera del evento táctil, en un
 * temporizador al terminar el zoom.
 */
#include "ui_internal.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "flex_thumb.h"
#include "misc/cache/lv_cache.h" /* lv_image_cache_drop (no está en lvgl.h) */
#include "watch_diag.h"
#include "watch_settings.h"
#include "watch_theme.h"

#define N           UI_FACE_COUNT_
#define CARD_W      276
#define CARD_H      358
#define CARD_R      26
#define CARD_Y      72
#define CARD_GAP    29
#define CARD_X      ((FLEX_SCREEN_W - CARD_W) / 2) /* 67 */
#define PITCH       (CARD_W + CARD_GAP)
#define DOT_D       14
#define DOT_DX      25
#define DOT_Y       25
#define DOT_OFF_OPA 90
#define CARD_EDGE   0x3A3D44 /* contorno: que una esfera negra no se pierda en la página negra */
#define NAME_Y      442
#define WIN         2     /* miniaturas vivas: la centrada y WIN a cada lado */
#define GEN_MS      40
#define ZOOM_MS     260
/* Escala con la que la tarjeta llena la pantalla a lo ancho (y de sobra a lo alto) */
#define ZOOM_SCALE  ((LV_SCALE_NONE * FLEX_SCREEN_W + CARD_W - 1) / CARD_W)
#define THUMB_BYTES ((size_t)CARD_W * CARD_H * 2)
/* Margen de PSRAM que debe quedar libre además de lo que se reserva. */
#define SNAP_MARGIN (256 * 1024)

typedef struct {
    lv_obj_t *page;
    lv_obj_t *strip;
    lv_obj_t *name;
    lv_obj_t *zoom;  /* imagen del zoom (encima de todo, solo mientras anima) */
    lv_obj_t *card[N];
    lv_obj_t *img[N];
    lv_obj_t *dot[N];
    lv_image_dsc_t dsc[N];
    uint16_t *px[N];
    bool failed[N];  /* sin memoria o sin captura: no se reintenta hasta volver a abrir */
    int cur;         /* la centrada */
    int shown;       /* la de los puntos y el nombre */
    int32_t vis;     /* 0..256: cuánto se ven las vecinas, los puntos y el nombre (zoom) */
    lv_draw_buf_t *snap;
    void *scratch;   /* pesos de la reducción (flex_thumb), en PSRAM, con snap */
    lv_timer_t *gen;
    lv_timer_t *apply;
    bool animating;
    bool from_settings;
} ed_t;

static bool s_from_settings;
static bool s_zoom_open;
static lv_timer_t *s_open_timer;
static uint32_t s_previews_made; /* para las pruebas: vistas previas renderizadas */
static ed_t *s_live;             /* el selector abierto (uno como mucho) */

uint32_t ui_face_editor_previews_made(void)
{
    return s_previews_made;
}

int ui_face_editor_centered(void)
{
    return s_live && ui_nav_top() == s_live->page && !s_live->animating ? s_live->cur : -1;
}

int ui_face_editor_thumbs(void)
{
    int n = 0;
    for (int i = 0; s_live && i < N; i++) {
        n += s_live->px[i] != NULL;
    }
    return n;
}

/* ---- Miniaturas ---- */

static bool snap_buffer(ed_t *e)
{
    if (e->snap) {
        return true;
    }
    const size_t need = (size_t)FLEX_SCREEN_W * FLEX_SCREEN_H * 2;
    if (!watch_mem_psram_fits(need + SNAP_MARGIN)) {
        LV_LOG_WARN("selector: sin PSRAM contigua para capturar las vistas previas");
        return false;
    }
    e->snap = lv_draw_buf_create(FLEX_SCREEN_W, FLEX_SCREEN_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    e->scratch = heap_caps_malloc(flex_thumb_scratch_bytes(CARD_W), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!e->snap || !e->scratch) {
        if (e->snap) {
            lv_draw_buf_destroy(e->snap);
            e->snap = NULL;
        }
        heap_caps_free(e->scratch);
        e->scratch = NULL;
        return false;
    }
    return true;
}

static void snap_release(ed_t *e)
{
    if (e->snap) {
        lv_draw_buf_destroy(e->snap);
        e->snap = NULL;
    }
    heap_caps_free(e->scratch);
    e->scratch = NULL;
}

static void thumb_set(ed_t *e, int i, uint16_t *px)
{
    e->px[i] = px;
    lv_image_dsc_t *d = &e->dsc[i];
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = CARD_W;
    d->header.h = CARD_H;
    d->header.stride = CARD_W * 2;
    d->data_size = (uint32_t)THUMB_BYTES;
    d->data = (const uint8_t *)px;
    lv_image_set_src(e->img[i], d);
}

static void thumb_drop(ed_t *e, int i)
{
    if (!e->px[i]) {
        return;
    }
    lv_image_set_src(e->img[i], NULL); /* primero la imagen, después sus píxeles */
    lv_image_cache_drop(&e->dsc[i]);
    heap_caps_free(e->px[i]);
    e->px[i] = NULL;
}

/* Captura obj (410x502) y la reduce a la miniatura i. */
static bool thumb_from(ed_t *e, int i, lv_obj_t *obj)
{
    if (!watch_mem_psram_fits(THUMB_BYTES + SNAP_MARGIN) || !snap_buffer(e)) {
        return false;
    }
    uint16_t *px = heap_caps_malloc(THUMB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px) {
        return false;
    }
    if (lv_snapshot_take_to_draw_buf(obj, LV_COLOR_FORMAT_RGB565, e->snap) != LV_RESULT_OK ||
        flex_thumb_cover((const uint16_t *)e->snap->data, FLEX_SCREEN_W, FLEX_SCREEN_H, e->snap->header.stride / 2,
                         px, CARD_W, CARD_H, CARD_R, e->scratch) != 0) {
        heap_caps_free(px);
        return false;
    }
    thumb_set(e, i, px);
    s_previews_made++;
    return true;
}

static bool make_preview(ed_t *e, int style)
{
    watch_theme_preview_begin();
    lv_obj_t *face = ui_face_create_style(e->page, style, false);
    bool ok = false;
    if (face) {
        lv_obj_add_flag(face, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(face);
        ok = thumb_from(e, style, face);
        lv_obj_delete(face);
    }
    watch_theme_preview_end();
    if (!ok) {
        e->failed[style] = true;
        LV_LOG_WARN("selector: sin vista previa de la esfera %d", style);
    }
    return ok;
}

static bool in_window(const ed_t *e, int i)
{
    return i >= e->cur - WIN && i <= e->cur + WIN;
}

/* La que toca generar: la centrada, luego las vecinas de dentro hacia fuera. */
static int next_needed(const ed_t *e)
{
    for (int d = 0; d <= WIN; d++) {
        const int c[2] = {e->cur + d, e->cur - d};
        for (int k = 0; k < (d ? 2 : 1); k++) {
            if (c[k] >= 0 && c[k] < N && !e->px[c[k]] && !e->failed[c[k]]) {
                return c[k];
            }
        }
    }
    return -1;
}

static void gen_stop(ed_t *e)
{
    if (e->gen) {
        lv_timer_delete(e->gen);
        e->gen = NULL;
    }
    snap_release(e);               /* 411 KB de vuelta a la PSRAM */
    watch_theme_preview_release(); /* y el fondo de las esferas temporales */
}

static void gen_cb(lv_timer_t *t)
{
    ed_t *e = lv_timer_get_user_data(t);
    /* Ni durante una transición, un zoom o un desplazamiento, ni con la página
     * oculta (app suspendida) o el panel rápido encima. */
    if (ui_nav_busy() || ui_nav_top() != e->page || ui_qp_active() || e->animating ||
        lv_obj_is_scrolling(e->strip)) {
        return;
    }
    const int style = next_needed(e);
    if (style >= 0) {
        make_preview(e, style);
    }
    if (next_needed(e) < 0) {
        gen_stop(e);
    }
}

static void gen_start(ed_t *e)
{
    if (!e->gen && next_needed(e) >= 0) {
        e->gen = lv_timer_create(gen_cb, GEN_MS, e);
    }
}

/* ---- Carrusel ---- */

/* Opacidad de fondo directa (no la de grupo, que pediría una capa por punto) */
static void dot_paint(const ed_t *e, int k)
{
    const int32_t base = k == e->shown ? LV_OPA_COVER : DOT_OFF_OPA;
    lv_obj_set_style_bg_opa(e->dot[k], (lv_opa_t)(base * e->vis / 256), 0);
}

static void show_index(ed_t *e, int i)
{
    if (i == e->shown) {
        return;
    }
    const int old = e->shown;
    e->shown = i;
    if (old >= 0) {
        dot_paint(e, old);
    }
    dot_paint(e, i);
    lv_label_set_text(e->name, ui_face_name(i));
}

static int centered(const ed_t *e)
{
    const int32_t i = (lv_obj_get_scroll_x(e->strip) + PITCH / 2) / PITCH;
    return LV_CLAMP(0, (int)i, N - 1);
}

/* Centrada otra: se sueltan las miniaturas lejanas y se piden las nuevas. */
static void set_cur(ed_t *e, int i)
{
    e->cur = i;
    show_index(e, i);
    for (int k = 0; k < N; k++) {
        if (!in_window(e, k)) {
            thumb_drop(e, k);
        }
    }
    gen_start(e);
}

static void scroll_cb(lv_event_t *ev)
{
    ed_t *e = lv_event_get_user_data(ev);
    if (lv_event_get_code(ev) == LV_EVENT_SCROLL_END) {
        set_cur(e, centered(e));
    } else {
        show_index(e, centered(e));
    }
}

/* ---- Zoom ---- */

/* Opacidad de una tarjeta (fondo, imagen y contorno; sin capas). */
static void card_opa(ed_t *e, int k, lv_opa_t o)
{
    lv_obj_set_style_image_opa(e->img[k], o, 0);
    lv_obj_set_style_bg_opa(e->card[k], o, 0);
    lv_obj_set_style_outline_opa(e->card[k], o, 0);
}

/* v: 0 = la esfera llena la pantalla; 256 = la tarjeta en su sitio, con las
 * vecinas, los puntos y el nombre visibles. */
static void reveal(ed_t *e, int32_t v)
{
    const lv_opa_t o = (lv_opa_t)LV_MIN(255, v);
    e->vis = v;
    for (int k = e->cur - 1; k <= e->cur + 1; k += 2) {
        if (k >= 0 && k < N) {
            card_opa(e, k, o);
        }
    }
    for (int k = 0; k < N; k++) {
        dot_paint(e, k);
    }
    lv_obj_set_style_text_opa(e->name, o, 0);
    if (e->zoom) {
        lv_image_set_scale(e->zoom, (uint32_t)(ZOOM_SCALE - (ZOOM_SCALE - LV_SCALE_NONE) * v / 256));
    }
}

static void zoom_exec_cb(void *var, int32_t v)
{
    reveal(var, v);
}

static void zoom_end(ed_t *e)
{
    if (e->zoom) {
        lv_obj_delete(e->zoom);
        e->zoom = NULL;
    }
    card_opa(e, e->cur, LV_OPA_COVER);
    e->animating = false;
}

static void open_done_cb(lv_anim_t *a)
{
    ed_t *e = a->var;
    reveal(e, 256);
    zoom_end(e);
    gen_start(e);
}

static void apply_cb(lv_timer_t *t);

static void pick_done_cb(lv_anim_t *a)
{
    ed_t *e = a->var;
    /* La raíz se rehace fuera de la animación, en un temporizador. */
    if (!e->apply) {
        e->apply = lv_timer_create(apply_cb, 0, e);
    }
}

/* Imagen del zoom sobre la tarjeta centrada (que se vuelve invisible). */
static bool zoom_begin(ed_t *e, int32_t v_from, int32_t v_to, uint32_t ms, lv_anim_path_cb_t path,
                       lv_anim_completed_cb_t done)
{
    if (!e->px[e->cur]) {
        return false;
    }
    lv_obj_t *z = lv_image_create(e->page);
    lv_image_set_src(z, &e->dsc[e->cur]);
    lv_obj_set_pos(z, CARD_X, CARD_Y);
    lv_image_set_pivot(z, CARD_W / 2, CARD_H / 2);
    lv_obj_remove_flag(z, LV_OBJ_FLAG_CLICKABLE);
    e->zoom = z;
    e->animating = true;
    card_opa(e, e->cur, LV_OPA_TRANSP);
    reveal(e, v_from);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, e);
    lv_anim_set_exec_cb(&a, zoom_exec_cb);
    lv_anim_set_values(&a, v_from, v_to);
    lv_anim_set_duration(&a, ui_psave_active() ? ms / 2 : ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
    return true;
}

static void apply_cb(lv_timer_t *t)
{
    ed_t *e = lv_timer_get_user_data(t);
    if (ui_nav_busy()) {
        return; /* un gesto de navegación a medias: se espera a que termine */
    }
    lv_timer_delete(t);
    e->apply = NULL;
    const int style = e->cur;
    const bool from_settings = e->from_settings;
    gen_stop(e);
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.face != style) {
        ws.face = (uint8_t)style;
        watch_settings_update(&ws);
        ui_nav_replace_root(ui_face_create);
    }
    /* La página (y e) se borran aquí: nada de e después. */
    if (from_settings) {
        ui_nav_home_instant();
    } else {
        ui_nav_back_instant();
    }
}

static void pick_cb(lv_event_t *ev)
{
    ed_t *e = lv_event_get_user_data(ev);
    const int i = (int)lv_obj_get_index(lv_event_get_current_target(ev));
    if (e->animating || e->apply || ui_nav_busy() || lv_obj_is_scrolling(e->strip)) {
        return; /* un toque durante la apertura o un zoom no aplica nada a medias */
    }
    if (i != e->cur) {
        lv_obj_scroll_to_view(e->card[i], LV_ANIM_ON); /* una vecina: se centra */
        return;
    }
    if (!zoom_begin(e, 256, 0, ZOOM_MS, lv_anim_path_ease_in_out, pick_done_cb)) {
        e->apply = lv_timer_create(apply_cb, 0, e); /* sin miniatura: se aplica sin zoom */
    }
}

/* ---- Página ---- */

static void free_cb(lv_event_t *ev)
{
    ed_t *e = lv_event_get_user_data(ev);
    lv_anim_delete(e, NULL);
    if (e->apply) {
        lv_timer_delete(e->apply);
        e->apply = NULL;
    }
    gen_stop(e);
    /* LVGL borra los hijos DESPUÉS de este evento: primero se sueltan las
     * imágenes y luego sus píxeles, para que nada apunte a memoria libre. */
    if (e->zoom) {
        lv_image_set_src(e->zoom, NULL);
    }
    for (int i = 0; i < N; i++) {
        thumb_drop(e, i);
    }
    if (s_live == e) {
        s_live = NULL;
    }
    lv_free(e);
}

lv_obj_t *ui_face_editor_create(lv_obj_t *parent)
{
    ed_t *e = lv_malloc_zeroed(sizeof(*e));
    if (!e) {
        return NULL; /* ui_nav no abre nada: sin memoria no se cuelga */
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    e->cur = ws.face < N ? ws.face : 0;
    e->shown = -1;
    e->vis = 256;
    e->from_settings = s_from_settings;
    const bool zoom = s_zoom_open;
    s_zoom_open = false;

    lv_obj_t *page = ui_page_new(parent);
    e->page = page;
    watch_theme_page(page);

    lv_obj_t *strip = lv_obj_create(page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(strip, CARD_X, 0);
    lv_obj_set_style_pad_right(strip, CARD_X, 0);
    lv_obj_set_style_pad_top(strip, CARD_Y, 0);
    lv_obj_set_style_pad_column(strip, CARD_GAP, 0);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_add_event_cb(strip, scroll_cb, LV_EVENT_SCROLL, e);
    lv_obj_add_event_cb(strip, scroll_cb, LV_EVENT_SCROLL_END, e);
    e->strip = strip;

    for (int i = 0; i < N; i++) {
        /* Tarjeta: gris mientras no hay vista previa; la imagen ya trae las
         * esquinas redondeadas (sin clip_corner). */
        lv_obj_t *card = lv_obj_create(strip);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_radius(card, CARD_R, 0);
        lv_obj_set_style_bg_color(card, flex_color(FLEX_C_SURF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_width(card, 2, 0);
        lv_obj_set_style_outline_color(card, flex_color(CARD_EDGE), 0);
        lv_obj_set_style_outline_opa(card, LV_OPA_COVER, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(card, pick_cb, LV_EVENT_CLICKED, e);
        e->card[i] = card;
        e->img[i] = lv_image_create(card);
        lv_obj_set_pos(e->img[i], 0, 0);
        lv_obj_remove_flag(e->img[i], LV_OBJ_FLAG_CLICKABLE);
    }

    /* Puntos: uno por esfera, el de la centrada en blanco */
    const int32_t dots_w = (N - 1) * DOT_DX + DOT_D;
    for (int i = 0; i < N; i++) {
        lv_obj_t *d = lv_obj_create(page);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_D, DOT_D);
        lv_obj_set_pos(d, (FLEX_SCREEN_W - dots_w) / 2 + i * DOT_DX, DOT_Y);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, flex_color(FLEX_C_WHITE), 0);
        lv_obj_set_style_bg_opa(d, DOT_OFF_OPA, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        e->dot[i] = d;
    }
    e->name = watch_theme_label(page, FLEX_TYPE_LABEL, 0xA8A8A8, "");
    lv_obj_set_width(e->name, FLEX_SCREEN_W);
    lv_obj_set_style_text_align(e->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(e->name, 0, NAME_Y);
    if (ui_psave_active()) {
        /* En ahorro de energía la esfera a la vista es la mínima: se dice. */
        lv_obj_t *hint = watch_theme_label(page, FLEX_TYPE_FOOTNOTE, FLEX_C_MUTE, "En ahorro de energía: esfera mínima");
        lv_obj_set_width(hint, FLEX_SCREEN_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(hint, 0, NAME_Y + 26);
    }
    show_index(e, e->cur);

    lv_obj_update_layout(strip);
    lv_obj_scroll_to_view(e->card[e->cur], LV_ANIM_OFF);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, e);
    s_live = e;

    /* Desde la esfera: su captura (estamos en un temporizador, no en el evento
     * táctil) es la tarjeta centrada, que empieza llenando la pantalla. */
    if (zoom && ui_nav_page(0) && thumb_from(e, e->cur, ui_nav_page(0))) {
        zoom_begin(e, 0, 256, ZOOM_MS, lv_anim_path_ease_out, open_done_cb);
    } else {
        gen_start(e); /* todas las vistas previas, también la primera, desde el temporizador */
    }
    return page;
}

void ui_face_editor_open(bool from_settings)
{
    s_from_settings = from_settings;
    s_zoom_open = false;
    ui_nav_push(ui_face_editor_create);
}

static void open_zoom_cb(lv_timer_t *t)
{
    (void)t;
    s_open_timer = NULL; /* un solo disparo: LVGL lo borra al volver */
    if (ui_nav_depth() != 1 || ui_nav_busy() || ui_qp_active()) {
        return;
    }
    s_from_settings = false;
    s_zoom_open = true;
    if (!ui_nav_push_instant(ui_face_editor_create)) {
        s_zoom_open = false;
    }
}

void ui_face_editor_open_zoom(void)
{
    if (s_open_timer) {
        return;
    }
    s_open_timer = lv_timer_create(open_zoom_cb, 0, NULL);
    lv_timer_set_repeat_count(s_open_timer, 1);
}
