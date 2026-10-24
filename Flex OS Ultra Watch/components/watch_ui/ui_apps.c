/*
 * ui_apps — cajón de aplicaciones: la Caja de aplicaciones de Flex OS Ultra
 * (FlexOS_Ultra_AppDrawer.h) en forma de panal, como Apple Watch / One UI
 * Watch, para 410x502.
 *
 * Se abre deslizando hacia arriba desde el borde inferior de la esfera (sube
 * siguiendo al dedo) y se vuelve a la esfera deslizando hacia arriba desde el
 * borde inferior del cajón (o desde cualquiera de los bordes laterales).
 *
 * De la referencia se conservan los ICONOS ORIGINALES de cada app
 * (flex_app_icons, el dibujo de drawAppIcon) y el punto de estado sobre el
 * icono (AppDrawer.h:690): aquí, en Reloj mientras el cronómetro o el
 * temporizador cuentan. Cambia la forma: iconos circulares de 92 px sin
 * nombre debajo, en filas alternas de 3 y 4 huecos desplazadas medio paso
 * (rejilla hexagonal: cada icono equidista de sus seis vecinos), todas
 * centradas y desplazables en vertical si no caben.
 *
 * Abrir una app la retoma si estaba suspendida (se salió con el gesto hacia
 * arriba) con su estado; si no, la abre nueva (ui_nav_open_app).
 *
 * Solo aparecen las apps que funcionan de verdad en el reloj
 * (docs/INVENTARIO-APPS.md): ni pantallas vacías ni "próximamente". Sensores
 * no está en el cajón: sus pruebas siguen en Device Care > Pruebas.
 */
#include "ui_internal.h"

#include <string.h>
#include "flex_app_icons.h"
#include "ui_gesture.h"
#include "watch_theme.h"

#define APPS_TICK_MS 500
#define ICON_D       FLEX_APP_ICON_SIZE        /* 92 */
#define PITCH        100                       /* entre centros de una fila */
#define ROW_STEP     87                        /* PITCH * sin(60°): panal regular */
#define WIDE_N       4                         /* iconos en las filas anchas */
#define NARROW_N     3
#define GRID_PAD_V   36
/* Punto de estado: radio 7 px a 72 px en la referencia -> 9 px a 92 px */
#define BADGE_D      18

typedef struct {
    flex_app_t app;
    ui_page_create_t create;
} app_entry_t;

/* Apps del reloj con su página. Una app del inventario entra aquí cuando
 * hace algo real; el orden es el de la rejilla. */
static const app_entry_t kApps[] = {
    {FLEX_APP_RELOJ, ui_reloj_create},
    {FLEX_APP_FLEX_PHONE, ui_flexphone_create},
    {FLEX_APP_NAVEGADOR, ui_browser_create},
    {FLEX_APP_MUSICA, ui_music_create},
    {FLEX_APP_NOTAS, ui_notes_create},
    {FLEX_APP_PAINT, ui_paint_create},
    {FLEX_APP_GALERIA, ui_gallery_create},
    {FLEX_APP_CALENDARIO, ui_calendar_create},
    {FLEX_APP_CALCULADORA, ui_calc_create},
    {FLEX_APP_CLIMA, ui_weather_create},
    {FLEX_APP_FLEX_COMPASS, ui_compass_create},
    {FLEX_APP_DEVICE_CARE, ui_devcare_create},
    {FLEX_APP_ALMACENAMIENTO, ui_storage_create},
    {FLEX_APP_AJUSTES, ui_settings_create},
};
#define APPS_N ((int)(sizeof(kApps) / sizeof(kApps[0])))

typedef struct {
    lv_obj_t *badge; /* punto de estado de Reloj */
} apps_t;

static void open_cb(lv_event_t *e)
{
    ui_nav_open_app((ui_page_create_t)lv_event_get_user_data(e));
}

/* Filas del panal: huecos alternos de 3 y 4. Cada fila se centra, y para
 * que una fila incompleta quede centrada SIN salirse de la rejilla
 * hexagonal, su número de iconos ha de tener la misma paridad que sus
 * huecos (si no, uno pasa a la fila siguiente): 13 apps = 3, 4, 3, 2, 1. */
#define MAX_ROWS 12

static int hex_rows(int counts[MAX_ROWS])
{
    int rows = 0, left = APPS_N;
    while (left > 0 && rows < MAX_ROWS) {
        const int slots = (rows % 2 == 0) ? NARROW_N : WIDE_N;
        int take = left < slots ? left : slots;
        if (take < slots && (take % 2) != (slots % 2) && take > 1) {
            take--;
        }
        counts[rows++] = take;
        left -= take;
    }
    return rows;
}

/* Posición del icono i en el panal. */
static void hex_pos(int i, int32_t *x, int32_t *y)
{
    int counts[MAX_ROWS];
    const int rows = hex_rows(counts);
    int first = 0;
    for (int row = 0; row < rows; row++) {
        if (i < first + counts[row]) {
            const int slots = (row % 2 == 0) ? NARROW_N : WIDE_N;
            const int32_t full_w = (slots - 1) * PITCH;
            /* (huecos - iconos) / 2 huecos de margen: exacto con la misma paridad */
            const int32_t x0 = (FLEX_SCREEN_W - full_w) / 2 + (slots - counts[row]) * PITCH / 2;
            *x = x0 + (i - first) * PITCH - ICON_D / 2;
            *y = row * ROW_STEP;
            return;
        }
        first += counts[row];
    }
    *x = 0;
    *y = 0;
}

static int rows_needed(void)
{
    int counts[MAX_ROWS];
    return hex_rows(counts);
}

/* Icono circular: se oscurece mientras el dedo está encima. */
static lv_obj_t *cell(lv_obj_t *grid, const app_entry_t *a, int i, int32_t top)
{
    int32_t x, y;
    hex_pos(i, &x, &y);
    lv_obj_t *img = lv_image_create(grid);
    lv_image_set_src(img, flex_app_icon(a->app));
    lv_obj_set_pos(img, x, top + y);
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(img, 4);
    lv_obj_set_style_image_recolor(img, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_add_event_cb(img, open_cb, LV_EVENT_CLICKED, (void *)a->create);
    return img;
}

static lv_obj_t *badge(lv_obj_t *grid, lv_obj_t *icon)
{
    lv_obj_t *b = lv_obj_create(grid);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BADGE_D, BADGE_D);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, flex_color(FLEX_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* aro negro: separa el punto del icono */
    lv_obj_set_style_border_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    /* arriba a la derecha, sobre el borde del círculo (45°) */
    lv_obj_set_pos(b, lv_obj_get_x(icon) + ICON_D * 85 / 100 - BADGE_D / 2,
                   lv_obj_get_y(icon) + ICON_D * 15 / 100 - BADGE_D / 2);
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
    /* Rejilla a pantalla completa (sin cabecera): se sale con los gestos. */
    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, FLEX_SCREEN_W, FLEX_SCREEN_H);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    const int32_t grid_h = (rows_needed() - 1) * ROW_STEP + ICON_D;
    int32_t top = (FLEX_SCREEN_H - grid_h) / 2;
    if (top < GRID_PAD_V) {
        top = GRID_PAD_V;
    }
    for (int i = 0; i < APPS_N; i++) {
        lv_obj_t *c = cell(grid, &kApps[i], i, top);
        if (kApps[i].app == FLEX_APP_RELOJ) {
            a->badge = badge(grid, c);
        }
    }
    /* Aire bajo la última fila al desplazar (por encima del borde redondeado) */
    lv_obj_t *pad = lv_obj_create(grid);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, GRID_PAD_V);
    lv_obj_set_pos(pad, 0, top + grid_h);

    refresh(a);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, a);
    ui_page_timer(page, tick_cb, APPS_TICK_MS, a);
    return page;
}

lv_obj_t *ui_apps_find(lv_obj_t *drawer, const char *name)
{
    if (!drawer || !name || lv_obj_get_child_count(drawer) == 0) {
        return NULL;
    }
    lv_obj_t *grid = lv_obj_get_child(drawer, 0);
    for (int i = 0; i < APPS_N; i++) {
        if (strcmp(flex_app_name(kApps[i].app), name) != 0) {
            continue;
        }
        const void *src = flex_app_icon(kApps[i].app);
        for (uint32_t k = 0; k < lv_obj_get_child_count(grid); k++) {
            lv_obj_t *c = lv_obj_get_child(grid, (int32_t)k);
            if (lv_obj_check_type(c, &lv_image_class) && lv_image_get_src(c) == src) {
                return c;
            }
        }
    }
    return NULL;
}

/* ---- Gesto en la esfera: desde el borde inferior hacia arriba, el cajón ----
 *
 * Solo desde la franja inferior (FACE_PULL_H): un deslizamiento que empieza en
 * el centro no abre nada. El reconocedor (ui_gesture) exige superar el umbral
 * de movimiento con dirección dominante hacia arriba (si el primer movimiento
 * claro es lateral o hacia abajo, se descarta) y al soltar decide con el
 * recorrido (35 %) o la velocidad; si no, el cajón vuelve hacia abajo. Una
 * franja propia con PRESS_LOCK: el mismo movimiento no lo ve ningún otro gesto
 * (panel rápido, pulsación larga del selector). */
#define FACE_PULL_H 56

static struct {
    ui_drag_t drag;
    bool opening;
} s_pull;

static void face_pull_cb(lv_event_t *e)
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
        ui_drag_init(&s_pull.drag, UI_AXIS_Y, -1, FLEX_SCREEN_H);
        ui_drag_press(&s_pull.drag, p.x, p.y, now);
        s_pull.opening = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (ui_drag_move(&s_pull.drag, p.x, p.y, now) != UI_DRAG_ACTIVE) {
            return;
        }
        if (!s_pull.opening) {
            if (ui_nav_depth() != 1 || ui_nav_busy() || ui_qp_active()) {
                s_pull.drag.state = UI_DRAG_REJECTED;
                return;
            }
            s_pull.opening = ui_nav_drag_open_begin(ui_apps_create);
            if (!s_pull.opening) {
                s_pull.drag.state = UI_DRAG_REJECTED;
                return;
            }
        }
        ui_nav_drag_open_move(s_pull.drag.travel);
    } else {
        const ui_drag_result_t r = ui_drag_release(&s_pull.drag, p.x, p.y, now);
        if (s_pull.opening) {
            ui_nav_drag_open_end(r == UI_DRAG_COMMIT, s_pull.drag.travel);
            s_pull.opening = false;
        }
    }
}

void ui_apps_attach(lv_obj_t *face_page)
{
    lv_obj_t *strip = lv_obj_create(face_page);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, FLEX_SCREEN_W, FACE_PULL_H);
    lv_obj_set_pos(strip, 0, FLEX_SCREEN_H - FACE_PULL_H);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(strip, face_pull_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(strip, face_pull_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(strip, face_pull_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(strip, face_pull_cb, LV_EVENT_PRESS_LOST, NULL);
    /* Indicio visual: una pequeña asa abajo */
    lv_obj_t *pill = lv_obj_create(strip);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 40, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, flex_color(FLEX_C_TXT2), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_50, 0);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
}
