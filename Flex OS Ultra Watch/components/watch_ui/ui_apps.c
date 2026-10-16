/*
 * ui_apps — lanzador de aplicaciones: la Caja de aplicaciones de Flex OS
 * Ultra (FlexOS_Ultra_AppDrawer.h) adaptada a 410x502.
 *
 * Se abre deslizando la esfera hacia la izquierda y se vuelve con el gesto
 * desde el borde izquierdo o la cabecera.
 *
 * De la referencia se conserva:
 *  - los ICONOS ORIGINALES de cada app (flex_app_icons, el dibujo de
 *    drawAppIcon), al mismo tamaño físico: 72 px a 217 ppi = 104 px aquí;
 *  - la rejilla con el nombre centrado bajo el icono, ajustado a su
 *    columna (uiFontFit + uiLabelFit, AppDrawer.h:700-707);
 *  - el punto de estado sobre el icono (AppDrawer.h:690): aquí, en Reloj
 *    mientras el cronómetro o el temporizador cuentan.
 * Cambia: 3 columnas en vez de 4 (410 px de ancho) y sin buscador (con tan
 * pocas apps no hace falta, y el teclado no cabe con la rejilla).
 *
 * Solo aparecen las apps que funcionan de verdad en el reloj
 * (docs/INVENTARIO-APPS.md): ni pantallas vacías ni "próximamente".
 */
#include "ui_internal.h"

#include "flex_app_icons.h"
#include "ui_gesture.h"
#include "watch_theme.h"

#define APPS_TICK_MS 500
#define APPS_COLS    3
#define CELL_W       128                      /* paso de columna */
#define CELL_GAP_Y   6                        /* icono -> nombre */
#define ROW_STEP     (FLEX_APP_ICON_SIZE + CELL_GAP_Y + 23 + 17)
#define GRID_X0      ((FLEX_SCREEN_W - ((APPS_COLS - 1) * CELL_W + FLEX_APP_ICON_SIZE)) / 2 - (CELL_W - FLEX_APP_ICON_SIZE) / 2)
#define GRID_TOP     6
/* Nombre bajo el icono. La referencia, si no cabe en su columna, primero
 * encoge la letra (uiFontFit) y solo después recorta. Aquí 18 px es la menor
 * legible, así que se aprieta el espaciado (hasta -1 px) y, si aun así no
 * cabe, recorta con "..." (LV_LABEL_LONG_MODE_DOTS). */
#define LABEL_W      CELL_W
/* Punto de estado: radio 7 px a 72 px en la referencia -> 10 px a 104 px */
#define BADGE_D      20
#define BADGE_INSET  3

typedef struct {
    flex_app_t app;
    ui_page_create_t create;
} app_entry_t;

/* Apps del reloj con su página. Una app del inventario entra aquí cuando
 * hace algo real; el orden es el de la rejilla. */
static const app_entry_t kApps[] = {
    {FLEX_APP_RELOJ, ui_reloj_create},
    {FLEX_APP_FLEX_PHONE, ui_flexphone_create},
    {FLEX_APP_MUSICA, ui_music_create},
    {FLEX_APP_NOTAS, ui_notes_create},
    {FLEX_APP_PAINT, ui_paint_create},
    {FLEX_APP_CALENDARIO, ui_calendar_create},
    {FLEX_APP_CALCULADORA, ui_calc_create},
    {FLEX_APP_FLEX_COMPASS, ui_compass_create},
    {FLEX_APP_DEVICE_CARE, ui_devcare_create},
    {FLEX_APP_SENSORES, ui_sensors_create},
    {FLEX_APP_ALMACENAMIENTO, ui_storage_create},
    {FLEX_APP_AJUSTES, ui_settings_create},
};
#define APPS_N ((int)(sizeof(kApps) / sizeof(kApps[0])))

typedef struct {
    lv_obj_t *badge; /* punto de estado de Reloj */
} apps_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void open_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

/* El icono se oscurece mientras el dedo está encima de su celda (icono o
 * nombre): el estado de la celda se copia al icono, que es quien se tiñe. */
static void press_cb(lv_event_t *e)
{
    lv_obj_t *icon = lv_obj_get_child(lv_event_get_target_obj(e), 0);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_obj_add_state(icon, LV_STATE_PRESSED);
    } else {
        lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }
}

/* Celda: icono (hijo 0) y nombre (hijo 1). */
static lv_obj_t *cell(lv_obj_t *grid, const app_entry_t *a, int i)
{
    const int32_t line = lv_font_get_line_height(FLEX_FONT_SMALL);
    lv_obj_t *c = lv_obj_create(grid);
    lv_obj_remove_style_all(c);
    /* BADGE_INSET de más arriba: el punto de estado sobresale del icono y
     * LVGL recorta los hijos al área de su padre; así queda dentro de la celda
     * sin mover el icono ni el nombre en pantalla. */
    lv_obj_set_size(c, CELL_W, BADGE_INSET + FLEX_APP_ICON_SIZE + CELL_GAP_Y + line);
    lv_obj_set_pos(c, GRID_X0 + (i % APPS_COLS) * CELL_W, GRID_TOP - BADGE_INSET + (i / APPS_COLS) * ROW_STEP);
    lv_obj_set_style_pad_top(c, BADGE_INSET, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *img = lv_image_create(c);
    lv_image_set_src(img, flex_app_icon(a->app));
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor(img, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_40, LV_STATE_PRESSED);

    const char *name = flex_app_name(a->app);
    lv_obj_t *n = watch_theme_label(c, FLEX_FONT_SMALL, FLEX_C_TXT, name);
    lv_point_t sz;
    lv_text_get_size(&sz, name, FLEX_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > LABEL_W) {
        lv_obj_set_style_text_letter_space(n, -1, 0);
    }
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(n, LABEL_W, line);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, FLEX_APP_ICON_SIZE + CELL_GAP_Y);

    lv_obj_add_event_cb(c, open_cb, LV_EVENT_CLICKED, (void *)a->create);
    lv_obj_add_event_cb(c, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(c, press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(c, press_cb, LV_EVENT_PRESS_LOST, NULL);
    return c;
}

static lv_obj_t *badge(lv_obj_t *c)
{
    lv_obj_t *b = lv_obj_create(c);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BADGE_D, BADGE_D);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* aro negro: separa el punto del icono, como sobre el fondo en la referencia */
    lv_obj_set_style_border_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_pos(b, (CELL_W + FLEX_APP_ICON_SIZE) / 2 - BADGE_D + BADGE_INSET, -BADGE_INSET);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

static void refresh(apps_t *a)
{
    if (!a->badge) {
        return;
    }
    const bool busy = ui_sw()->st == SW_RUN || ui_tmr()->st == TMR_RUN;
    if (busy != !lv_obj_has_flag(a->badge, LV_OBJ_FLAG_HIDDEN)) {
        if (busy) {
            lv_obj_remove_flag(a->badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(a->badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_apps_create(lv_obj_t *parent)
{
    apps_t *a = lv_malloc_zeroed(sizeof(*a));
    LV_ASSERT_MALLOC(a);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Aplicaciones", back_cb, NULL);
    lv_obj_t *grid = watch_theme_list(page);
    /* Las celdas se colocan a mano: sin flex, la rejilla no se recalcula. */
    lv_obj_set_layout(grid, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(grid, 0, 0);
    for (int i = 0; i < APPS_N; i++) {
        lv_obj_t *c = cell(grid, &kApps[i], i);
        if (kApps[i].app == FLEX_APP_RELOJ) {
            a->badge = badge(c);
        }
    }
    /* Aire bajo la última fila para que su nombre suba por encima del borde
     * redondeado al desplazar hasta el final. */
    lv_obj_t *pad = lv_obj_create(grid);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, 1);
    lv_obj_set_pos(pad, 0, GRID_TOP + ((APPS_N + APPS_COLS - 1) / APPS_COLS) * ROW_STEP);

    refresh(a);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, a);
    ui_page_timer(page, tick_cb, APPS_TICK_MS, a);
    return page;
}

/* ---- Gesto en la esfera: deslizar hacia la izquierda abre el lanzador ---- */

static struct {
    ui_drag_t drag;
    bool opened;
} s_swipe;

static void face_swipe_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        ui_drag_init(&s_swipe.drag, UI_AXIS_X, -1, FLEX_SCREEN_W);
        ui_drag_press(&s_swipe.drag, p.x, p.y, now);
        s_swipe.opened = false;
    } else if (code == LV_EVENT_PRESSING) {
        /* En cuanto el gesto es claro se abre (sin esperar a soltar). */
        if (!s_swipe.opened && ui_drag_move(&s_swipe.drag, p.x, p.y, now) == UI_DRAG_ACTIVE &&
            ui_nav_depth() == 1 && !ui_qp_active()) {
            s_swipe.opened = ui_nav_push(ui_apps_create);
        }
    } else {
        s_swipe.drag.state = UI_DRAG_IDLE;
    }
}

void ui_apps_attach(lv_obj_t *face_page)
{
    lv_obj_add_flag(face_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(face_page, face_swipe_cb, LV_EVENT_PRESS_LOST, NULL);
}
