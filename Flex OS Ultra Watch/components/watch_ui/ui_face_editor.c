/*
 * ui_face_editor — selector de esferas.
 *
 * Se abre manteniendo pulsada la esfera o desde Ajustes > Pantalla y fondo
 * de pantalla > Esfera (el mismo selector). Las vistas previas son las
 * esferas REALES: cada una se construye con ui_face_create_style (hora,
 * fecha, fondo y complicaciones de ahora mismo), se renderiza una vez con
 * lv_snapshot y se reduce a la mitad (205x251, RGB565 en PSRAM); desplazarse
 * en horizontal (una esfera por página) solo mueve imágenes.
 *
 * Ciclo de vida (corrección del reinicio del 2026-10-20, docs/F11-ESTABILIDAD.md):
 *  - NINGÚN render dentro de un callback de entrada. Antes la primera vista
 *    previa se renderizaba al crear la página, es decir, dentro del evento de
 *    pulsación larga: un render completo anidado sobre el procesado del táctil
 *    en una pila de 10 KB. Ahora todas salen de un temporizador, de una en una.
 *  - Ni durante una animación o un gesto de navegación: si ui_nav está ocupada
 *    (abrir, volver, arrastrar desde un borde) la siguiente vista previa
 *    espera; así un render largo nunca congela una transición.
 *  - Un solo búfer de captura (410x502 RGB565, 411 KB en PSRAM) reutilizado
 *    para las cinco vistas previas y liberado en cuanto terminan. Antes de
 *    reservarlo se comprueba el bloque contiguo mayor de PSRAM; si no cabe,
 *    la vista previa queda con su nombre (sin imagen) y se registra: nunca un
 *    fallo de reserva sin tratar.
 *  - Una sola esfera temporal viva a la vez, creada y borrada en el mismo tick.
 *  - Sin clip_corner (obligaba a LVGL a reservar una capa ARGB8888 por tarjeta
 *    en cada cuadro al desplazar o arrastrar): las esquinas redondeadas se
 *    pintan de negro en la miniatura una sola vez.
 *  - Al borrarse la página se sueltan primero las imágenes y después los
 *    píxeles; el temporizador se borra con ella.
 *
 * Tocar una esfera la aplica: se guarda en los ajustes (watch_settings, NVS:
 * se recupera tras reiniciar) y la raíz se rehace. Desde la esfera se vuelve
 * a ella; desde Ajustes, también directamente a la esfera.
 */
#include "ui_internal.h"

#include <string.h>
#include "watch_diag.h"
#include "watch_settings.h"
#include "watch_theme.h"

#define TH_W        (FLEX_SCREEN_W / 2)   /* 205 */
#define TH_H        (FLEX_SCREEN_H / 2)   /* 251 */
#define TH_R        40                    /* radio de las esquinas de la miniatura */
#define ITEM_W      FLEX_SCREEN_W   /* una esfera por página: nada mordido por las esquinas */
#define STRIP_Y     (FLEX_HDR_H + 8)
#define STRIP_H     (TH_H + 60)
#define GEN_MS      40
/* Margen de PSRAM que debe quedar libre además del búfer de captura. */
#define SNAP_MARGIN (256 * 1024)

typedef struct {
    lv_obj_t *page;
    lv_obj_t *strip;
    lv_obj_t *img[UI_FACE_COUNT_];
    lv_image_dsc_t dsc[UI_FACE_COUNT_];
    uint16_t *px[UI_FACE_COUNT_];
    bool tried[UI_FACE_COUNT_];
    int first;           /* la elegida: se genera primero */
    lv_draw_buf_t *snap; /* búfer de captura compartido */
    lv_timer_t *gen;
} ed_t;

static bool s_from_settings;
static uint32_t s_previews_made; /* para las pruebas: vistas previas renderizadas */

uint32_t ui_face_editor_previews_made(void)
{
    return s_previews_made;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* 2x2 -> 1 con promedio de canales (RGB565) y esquinas redondeadas en negro
 * (el fondo de la página): el aspecto de clip_corner sin su coste por cuadro. */
static void downscale(const uint16_t *src, uint32_t stride_px, uint16_t *dst)
{
    for (int y = 0; y < TH_H; y++) {
        const uint16_t *r0 = src + (size_t)(2 * y) * stride_px;
        const uint16_t *r1 = r0 + stride_px;
        for (int x = 0; x < TH_W; x++) {
            /* fuera del rectángulo redondeado: negro */
            const int cx = x < TH_R ? TH_R - x : (x >= TH_W - TH_R ? x - (TH_W - TH_R - 1) : 0);
            const int cy = y < TH_R ? TH_R - y : (y >= TH_H - TH_R ? y - (TH_H - TH_R - 1) : 0);
            if (cx && cy && cx * cx + cy * cy > TH_R * TH_R) {
                dst[(size_t)y * TH_W + x] = 0;
                continue;
            }
            const uint16_t p[4] = {r0[2 * x], r0[2 * x + 1], r1[2 * x], r1[2 * x + 1]};
            uint32_t r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                r += p[k] >> 11;
                g += (p[k] >> 5) & 0x3F;
                b += p[k] & 0x1F;
            }
            dst[(size_t)y * TH_W + x] = (uint16_t)(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
        }
    }
}

static bool snap_buffer(ed_t *e)
{
    if (e->snap) {
        return true;
    }
    const size_t need = (size_t)FLEX_SCREEN_W * FLEX_SCREEN_H * 2;
    if (!watch_mem_psram_fits(need + SNAP_MARGIN)) {
        LV_LOG_WARN("selector: sin PSRAM contigua para las vistas previas");
        return false;
    }
    e->snap = lv_draw_buf_create(FLEX_SCREEN_W, FLEX_SCREEN_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    return e->snap != NULL;
}

static void snap_release(ed_t *e)
{
    if (e->snap) {
        lv_draw_buf_destroy(e->snap);
        e->snap = NULL;
    }
}

static bool make_preview(ed_t *e, int style)
{
    e->tried[style] = true;
    if (!snap_buffer(e)) {
        /* Sin memoria para capturar: no se insiste (se quedan los nombres). */
        for (int i = 0; i < UI_FACE_COUNT_; i++) {
            e->tried[i] = true;
        }
        return false;
    }
    uint16_t *px = lv_malloc((size_t)TH_W * TH_H * 2);
    if (!px) {
        return false;
    }
    lv_obj_t *face = ui_face_create_style(e->page, style, false);
    if (!face) {
        lv_free(px);
        return false;
    }
    lv_obj_add_flag(face, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(face);
    const bool ok = lv_snapshot_take_to_draw_buf(face, LV_COLOR_FORMAT_RGB565, e->snap) == LV_RESULT_OK;
    lv_obj_delete(face);
    if (!ok) {
        lv_free(px);
        return false;
    }
    downscale((const uint16_t *)e->snap->data, e->snap->header.stride / 2, px);
    e->px[style] = px;
    lv_image_dsc_t *d = &e->dsc[style];
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = TH_W;
    d->header.h = TH_H;
    d->header.stride = TH_W * 2;
    d->data_size = (uint32_t)TH_W * TH_H * 2;
    d->data = (const uint8_t *)px;
    lv_image_set_src(e->img[style], d);
    s_previews_made++;
    return true;
}

static int next_style(const ed_t *e)
{
    if (!e->tried[e->first]) {
        return e->first;
    }
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        if (!e->tried[i]) {
            return i;
        }
    }
    return -1;
}

static void gen_cb(lv_timer_t *t)
{
    ed_t *e = lv_timer_get_user_data(t);
    /* Ni durante una transición o un gesto de borde, ni con la página oculta
     * (app suspendida) o el panel rápido encima. */
    if (ui_nav_busy() || ui_nav_top() != e->page || ui_qp_active()) {
        return;
    }
    const int style = next_style(e);
    if (style >= 0) {
        make_preview(e, style);
    }
    if (next_style(e) < 0) {
        snap_release(e); /* 411 KB de vuelta a la PSRAM en cuanto terminan */
        lv_timer_delete(t);
        e->gen = NULL;
    }
}

static void pick_cb(lv_event_t *ev)
{
    if (ui_nav_busy()) {
        return; /* un toque durante la apertura no aplica nada a medias */
    }
    const int style = (int)(intptr_t)lv_event_get_user_data(ev);
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.face != style) {
        ws.face = (uint8_t)style;
        watch_settings_update(&ws);
        ui_nav_replace_root(ui_face_create);
    }
    if (s_from_settings) {
        ui_nav_home(); /* al terminar desde Ajustes, a la esfera */
    } else {
        ui_nav_back();
    }
}

static void free_cb(lv_event_t *ev)
{
    ed_t *e = lv_event_get_user_data(ev);
    if (e->gen) {
        lv_timer_delete(e->gen);
        e->gen = NULL;
    }
    snap_release(e);
    /* LVGL borra los hijos DESPUÉS de este evento: primero se sueltan las
     * imágenes y luego sus píxeles, para que nada apunte a memoria libre. */
    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        if (e->img[i]) {
            lv_image_set_src(e->img[i], NULL);
        }
        lv_free(e->px[i]);
        e->px[i] = NULL;
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
    lv_obj_t *page = ui_page_new(parent);
    e->page = page;
    e->first = ws.face < UI_FACE_COUNT_ ? ws.face : 0;
    watch_theme_page(page);
    watch_theme_header(page, "Esferas", back_cb, NULL);

    lv_obj_t *strip = lv_obj_create(page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 0, STRIP_Y);
    lv_obj_set_size(strip, FLEX_SCREEN_W, STRIP_H);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_SCROLL_ONE);
    e->strip = strip;

    for (int i = 0; i < UI_FACE_COUNT_; i++) {
        lv_obj_t *item = lv_obj_create(strip);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, ITEM_W, STRIP_H);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(item, pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /* Tarjeta: fondo mientras no hay vista previa; la imagen ya trae las
         * esquinas redondeadas (sin clip_corner). */
        lv_obj_t *card = lv_obj_create(item);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, TH_W, TH_H);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(card, TH_R, 0);
        lv_obj_set_style_bg_color(card, flex_color(FLEX_C_SURF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        e->img[i] = lv_image_create(card);
        lv_obj_set_pos(e->img[i], 0, 0);
        lv_obj_remove_flag(e->img[i], LV_OBJ_FLAG_CLICKABLE);
        /* Aro: la elegida, en el acento */
        lv_obj_t *ring = lv_obj_create(item);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, TH_W + 8, TH_H + 8);
        lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, -4);
        lv_obj_set_style_radius(ring, TH_R + 4, 0);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_set_style_border_color(ring, flex_color(i == ws.face ? FLEX_C_ACCENT : FLEX_C_BORDER), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = watch_theme_label(item, FLEX_FONT_BODY, i == ws.face ? FLEX_C_ACCENT : FLEX_C_TXT,
                                           ui_face_name(i));
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    lv_obj_t *hint = watch_theme_label(page, FLEX_FONT_SMALL, FLEX_C_MUTE,
                                       ui_psave_active() ? "En ahorro de energía se usa la esfera mínima."
                                                         : "Desliza y toca una esfera para aplicarla.");
    lv_obj_set_width(hint, FLEX_SCREEN_W - 2 * FLEX_SAFE_X - 16);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, STRIP_Y + STRIP_H + 4);

    lv_obj_update_layout(strip);
    lv_obj_scroll_to_view(lv_obj_get_child(strip, e->first), LV_ANIM_OFF);

    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, e);
    /* Todas las vistas previas, también la primera, desde el temporizador. */
    e->gen = lv_timer_create(gen_cb, GEN_MS, e);
    return page;
}

void ui_face_editor_open(bool from_settings)
{
    s_from_settings = from_settings;
    ui_nav_push(ui_face_editor_create);
}
