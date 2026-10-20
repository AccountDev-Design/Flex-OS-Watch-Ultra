/*
 * Pruebas de F3.1 en la vista previa: iconos vectoriales y componentes del
 * tema (cabecera, lista, filas, botón de vidrio).
 *
 *  - Cada icono, a 20, 32 y 64 px, dibuja algo, ocupa al menos media caja,
 *    queda centrado y NO pinta fuera de su caja (se dibuja dentro de un
 *    objeto más grande, así que LVGL no lo recorta y un desborde se vería).
 *  - Los componentes responden al dedo simulado: la zona "volver" y una fila
 *    activa entregan un CLICKED; una fila pendiente no entrega nada.
 *  - Crear y borrar la pantalla de componentes 20 veces no deja memoria.
 */
#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "watch_theme.h"

#define MARGIN 10
static lv_obj_t *s_orig; /* pantalla a la que se vuelve antes de borrar la de prueba */
static const int32_t SIZES[] = {20, 32, 64};
#define N_SIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))

static const char *const ICON_NAMES[FLEX_ICON_COUNT_] = {
    "rayo", "check", "cruz", "engranaje", "sol", "wifi", "bluetooth",
    "chevron_izq", "chevron_der", "info", "pulso", "reloj",
    "imagen", "esfera", "globo", "campana", "reloj_arena", "cronometro", "rejilla", "movimiento",
    "luna", "almacen", "mas", "menos", "reproducir", "pausa", "detener", "bandera",
    "actualizar",
};

static void holder_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_area_increase(&box, -MARGIN, -MARGIN);
    flex_icon_draw(lv_event_get_layer(e), (flex_icon_t)(intptr_t)lv_event_get_user_data(e), &box,
                   lv_color_white(), LV_OPA_COVER);
}

static bool lit(int x, int y)
{
    const uint16_t c = sim_fb[y * SIM_W + x];
    return ((c >> 11) & 0x1F) > 6 || ((c >> 5) & 0x3F) > 12 || (c & 0x1F) > 6;
}

static void check_icon(const lv_area_t *b, int sz, int i)
{
    int inside = 0, outside = 0;
    int32_t bx1 = SIM_W, by1 = SIM_H, bx2 = -1, by2 = -1;
    for (int32_t py = b->y1 - MARGIN; py <= b->y2 + MARGIN; py++) {
        for (int32_t px = b->x1 - MARGIN; px <= b->x2 + MARGIN; px++) {
            if (!lit(px, py)) {
                continue;
            }
            if (px < b->x1 || px > b->x2 || py < b->y1 || py > b->y2) {
                outside++;
                continue;
            }
            inside++;
            bx1 = px < bx1 ? px : bx1;
            bx2 = px > bx2 ? px : bx2;
            by1 = py < by1 ? py : by1;
            by2 = py > by2 ? py : by2;
        }
    }
    EXPECT(outside == 0, "icono %s a %d px pinta %d px fuera de su caja", ICON_NAMES[i], sz, outside);
    EXPECT(inside >= sz, "icono %s a %d px casi vacio (%d px)", ICON_NAMES[i], sz, inside);
    if (inside > 0) {
        const int32_t span = (bx2 - bx1 + 1) > (by2 - by1 + 1) ? (bx2 - bx1 + 1) : (by2 - by1 + 1);
        EXPECT(span * 2 >= sz, "icono %s a %d px ocupa solo %d px", ICON_NAMES[i], sz, (int)span);
        const int32_t cx = (bx1 + bx2) / 2 - (b->x1 + b->x2) / 2;
        const int32_t cy = (by1 + by2) / 2 - (b->y1 + b->y2) / 2;
        EXPECT(LV_ABS(cx) * 5 <= sz && LV_ABS(cy) * 5 <= sz, "icono %s a %d px descentrado (%d, %d)", ICON_NAMES[i],
               sz, (int)cx, (int)cy);
    }
}

static void check_icons(const char *out)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_screen_load(scr);

    /* Rejilla por tamaño; si no caben todos, en varias pantallas. */
    for (int s = 0; s < N_SIZES; s++) {
        const int32_t cell = SIZES[s] + 2 * MARGIN;
        int first = 0, page = 0;
        while (first < FLEX_ICON_COUNT_) {
            lv_obj_clean(scr);
            lv_area_t boxes[FLEX_ICON_COUNT_];
            int32_t x = 4, y = 4;
            int last = first;
            for (; last < FLEX_ICON_COUNT_; last++) {
                if (x + cell > SIM_W) {
                    x = 4;
                    y += cell + 2;
                }
                if (y + cell > SIM_H) {
                    break;
                }
                lv_obj_t *h = lv_obj_create(scr);
                lv_obj_remove_style_all(h);
                lv_obj_set_pos(h, x, y);
                lv_obj_set_size(h, cell, cell);
                lv_obj_add_event_cb(h, holder_draw_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)last);
                boxes[last] = (lv_area_t){x + MARGIN, y + MARGIN, x + MARGIN + SIZES[s] - 1, y + MARGIN + SIZES[s] - 1};
                x += cell + 2;
            }
            lv_refr_now(NULL);
            char name[32];
            snprintf(name, sizeof(name), "f3_iconos_%d_%d", (int)SIZES[s], page++);
            sim_save(out, name);
            for (int i = first; i < last; i++) {
                check_icon(&boxes[i], (int)SIZES[s], i);
            }
            first = last;
        }
    }
    lv_screen_load(s_orig);
    lv_obj_delete(scr);
}

/* ---- Componentes ---- */
static struct {
    int back;
    int row;
    int pending;
    int button;
    lv_obj_t *row_ok;
    lv_obj_t *row_pending;
    lv_obj_t *button_obj;
} s_c;

static void count_cb(lv_event_t *e)
{
    (*(int *)lv_event_get_user_data(e))++;
}

static lv_obj_t *build_components(void)
{
    watch_theme_init();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    watch_theme_page(scr);
    watch_theme_header(scr, "Ajustes", count_cb, &s_c.back);
    lv_obj_t *list = watch_theme_list(scr);
    s_c.row_ok = watch_theme_list_row(list, FLEX_ICON_SUN, FLEX_C_CAT_SUN, "Pantalla", "Brillo 60 %", true);
    lv_obj_add_event_cb(s_c.row_ok, count_cb, LV_EVENT_CLICKED, &s_c.row);
    lv_obj_t *r = watch_theme_list_row(list, FLEX_ICON_BOLT, FLEX_C_CAT_GREEN, "Batería", "84 %", true);
    (void)r;
    s_c.row_pending = watch_theme_list_row(list, FLEX_ICON_WIFI, FLEX_C_CAT_NET, "Wi-Fi", NULL, false);
    watch_theme_row_set_pending(s_c.row_pending);
    lv_obj_add_event_cb(s_c.row_pending, count_cb, LV_EVENT_CLICKED, &s_c.pending);
    lv_obj_t *bt = watch_theme_list_row(list, FLEX_ICON_BLUETOOTH, FLEX_C_CAT_NET, "Bluetooth", NULL, false);
    watch_theme_row_set_pending(bt);
    watch_theme_list_row(list, FLEX_ICON_PULSE, FLEX_C_CAT_BLUE, "Diagnóstico", NULL, true);
    watch_theme_list_row(list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Acerca de", "0.1.0-b1", true);
    return scr;
}

static void check_components(const char *out)
{
    memset(&s_c, 0, sizeof(s_c));
    lv_obj_t *scr = build_components();
    lv_screen_load(scr);
    sim_run(100);
    sim_save(out, "f3_componentes");

    /* Zona "volver": un toque en su borde (no en el glifo) también cuenta. */
    lv_area_t z;
    lv_obj_get_coords(lv_obj_get_child(lv_obj_get_child(scr, 0), 0), &z); /* cabecera -> zona */
    sim_tap(z.x1 + 6, z.y2 - 6);
    EXPECT(s_c.back == 1, "la zona volver entrego %d clics (esperado 1)", s_c.back);

    /* Fila activa: pulsada se ve distinta; al soltar, un clic. */
    lv_area_t a;
    lv_obj_get_coords(s_c.row_ok, &a);
    sim_press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    sim_run(60);
    EXPECT(lv_obj_has_state(s_c.row_ok, LV_STATE_PRESSED), "la fila no muestra el estado pulsado");
    sim_save(out, "f3_fila_pulsada");
    sim_release();
    sim_run(60);
    EXPECT(s_c.row == 1, "la fila entrego %d clics (esperado 1)", s_c.row);
    EXPECT(!lv_obj_has_state(s_c.row_ok, LV_STATE_PRESSED), "la fila sigue pulsada al soltar");

    /* Fila pendiente: ningún clic. */
    lv_obj_get_coords(s_c.row_pending, &a);
    sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    EXPECT(s_c.pending == 0, "una fila pendiente entrego %d clics", s_c.pending);

    lv_screen_load(s_orig);
    lv_obj_delete(scr);
}

static void check_leaks(void)
{
    lv_obj_t *base = lv_obj_create(NULL);
    lv_screen_load(base);
    /* Calentamiento: cachés de fuentes y de dibujo que LVGL guarda tras el primer uso. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *scr = build_components();
        lv_screen_load(scr);
        sim_run(40);
        lv_screen_load(base);
        lv_obj_delete(scr);
        sim_run(40);
    }
    const size_t before = sim_mem_used();
    const uint32_t blocks = sim_mem_blocks();
    for (int i = 0; i < 20; i++) {
        lv_obj_t *scr = build_components();
        lv_screen_load(scr);
        sim_run(40);
        lv_screen_load(base);
        lv_obj_delete(scr);
        sim_run(40);
    }
    const size_t after = sim_mem_used();
    EXPECT(sim_mem_blocks() == blocks, "fuga: %u bloques antes, %u despues de 20 ciclos", (unsigned)blocks,
           (unsigned)sim_mem_blocks());
    printf("componentes: %u -> %u bloques de LVGL (%zu B -> %zu B) tras 20 ciclos\n", (unsigned)blocks,
           (unsigned)sim_mem_blocks(), before, after);
    lv_screen_load(s_orig);
    lv_obj_delete(base);
}

void sim_suite_theme(const char *out)
{
    const int f0 = sim_fails;
    s_orig = lv_screen_active();
    check_icons(out);
    check_components(out);
    check_leaks();
    printf("tema F3.1: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
