/*
 * Pruebas del cajón en panal infinito (ui_grid_logic) y del desenfoque de su
 * fondo (flex_blur): lógica pura, sin LVGL.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "flex_blur.h"
#include "flex_thumb.h"
#include "flex_tokens.h"
#include "ui_grid_logic.h"

#include "test_util.h"

static float dist(grid_vec_t a, grid_vec_t b)
{
    return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static void test_torus(void)
{
    /* Periodo 4 en q y en r: la identidad de una celda no depende del recorrido */
    for (int q = -9; q <= 9; q++) {
        for (int r = -9; r <= 9; r++) {
            const int s = grid_slot(q, r);
            CHECK(s >= 0 && s < GRID_SLOTS);
            CHECK_EQ(grid_slot(q + 4, r), s);
            CHECK_EQ(grid_slot(q, r - 4), s);
            CHECK_EQ(grid_slot(q + 8, r + 12), s);
        }
    }
    /* Dos celdas distintas con el mismo hueco están a 4 pasos o más (sin duplicados visibles) */
    float minp = 1e9f;
    for (int dq = -8; dq <= 8; dq++) {
        for (int dr = -8; dr <= 8; dr++) {
            if ((dq || dr) && grid_slot(dq, dr) == grid_slot(0, 0)) {
                const grid_vec_t p = grid_cell_pos(dq, dr), o = {0, 0};
                const float d = dist(p, o);
                if (d < minp) {
                    minp = d;
                }
            }
        }
    }
    CHECK(fabsf(minp - 4.0f * GRID_PITCH) < 0.01f);
    CHECK(2.0f * GRID_VIS_R < minp); /* la zona visible no cabe dos veces en un periodo */
    /* Vecinos a un paso en las seis direcciones */
    const int nb[6][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, -1}, {-1, 1}};
    for (int k = 0; k < 6; k++) {
        const grid_vec_t p = grid_cell_pos(nb[k][0], nb[k][1]), o = {0, 0};
        CHECK(fabsf(dist(p, o) - GRID_PITCH) < 0.01f);
    }
    /* Celda más cercana: el centro de cada celda y puntos dentro de su hexágono */
    for (int q = -5; q <= 5; q++) {
        for (int r = -5; r <= 5; r++) {
            const grid_vec_t p = grid_cell_pos(q, r);
            int a, b;
            grid_nearest_cell(p.x + 0.3f * GRID_PITCH, p.y - 0.2f * GRID_PITCH, &a, &b);
            CHECK(a == q && b == r);
            grid_nearest_cell(p.x, p.y, &a, &b);
            CHECK(a == q && b == r);
        }
    }
    /* Copia más cercana de un hueco */
    int q, r;
    grid_nearest_copy(grid_slot(1, 0), 10.0f * GRID_PITCH, 0.0f, &q, &r);
    CHECK_EQ(grid_slot(q, r), grid_slot(1, 0));
    const grid_vec_t pc = grid_cell_pos(q, r), tgt = {10.0f * GRID_PITCH, 0};
    CHECK(dist(pc, tgt) <= 2.31f * GRID_PITCH); /* radio de cobertura del periodo 4 */
}

static void test_layout(void)
{
    const grid_vec_t center = {FLEX_SCREEN_W / 2.0f, FLEX_SCREEN_H / 2.0f};
    grid_item_t it[GRID_MAX_ITEMS];
    /* En reposo con la celda (0,0) centrada: 13 celdas (centro, 6 + 6) */
    const grid_vec_t z = {0, 0};
    int n = grid_layout(z, center, center, it, GRID_MAX_ITEMS);
    CHECK_EQ(n, 13);
    for (int i = 0; i < n; i++) {
        if (it[i].q == 0 && it[i].r == 0) {
            CHECK(fabsf(it[i].scale - 1.0f) < 1e-4f); /* tamaño nativo en el foco */
            CHECK(fabsf(it[i].x - center.x) < 1e-3f && fabsf(it[i].y - center.y) < 1e-3f);
        } else {
            CHECK(it[i].scale < 1.0f && it[i].scale >= GRID_MIN_SCALE * 0.55f - 1e-4f);
        }
    }
    /* Ningún hueco repetido en ningún desplazamiento (barrido fino) */
    for (int k = 0; k < 4000; k++) {
        const grid_vec_t off = {(float)((k * 37) % 1300) - 650.0f + 0.37f * k, (float)((k * 53) % 1100) - 550.0f};
        const grid_vec_t lens = {(float)(k % 410), (float)((k * 7) % 502)};
        n = grid_layout(off, lens, center, it, GRID_MAX_ITEMS);
        CHECK(n > 0 && n <= GRID_MAX_ITEMS);
        int seen = 0;
        for (int i = 0; i < n; i++) {
            CHECK((seen & (1 << it[i].slot)) == 0);
            seen |= 1 << it[i].slot;
            CHECK(it[i].opa > 0);
        }
    }
    /* La lupa sigue al punto: el icono más cercano a él es el más grande */
    const grid_vec_t lens = {center.x + GRID_PITCH, center.y};
    n = grid_layout(z, lens, center, it, GRID_MAX_ITEMS);
    int big = -1;
    for (int i = 0; i < n; i++) {
        if (big < 0 || it[i].scale > it[big].scale) {
            big = i;
        }
    }
    CHECK(big >= 0 && it[big].q == 1 && it[big].r == 0);
    /* Desplazamiento continuo: posiciones sin saltos entre pasos pequeños */
    grid_item_t a[GRID_MAX_ITEMS], b[GRID_MAX_ITEMS];
    for (int k = 0; k < 400; k++) {
        const grid_vec_t o1 = {k * 1.7f, k * -0.9f}, o2 = {k * 1.7f + 0.5f, k * -0.9f + 0.25f};
        const int na = grid_layout(o1, center, center, a, GRID_MAX_ITEMS);
        const int nb = grid_layout(o2, center, center, b, GRID_MAX_ITEMS);
        for (int i = 0; i < na; i++) {
            for (int j = 0; j < nb; j++) {
                if (a[i].q == b[j].q && a[i].r == b[j].r) {
                    CHECK(fabsf(a[i].x - b[j].x) < 3.0f && fabsf(a[i].y - b[j].y) < 3.0f);
                    CHECK(fabsf(a[i].scale - b[j].scale) < 0.03f);
                }
            }
        }
    }
    /* Visibilidad: 1 dentro, 0 fuera, monótona */
    CHECK(grid_visibility(0) == 1.0f && grid_visibility(GRID_VIS_R) == 0.0f);
    for (float d = 0; d < GRID_VIS_R + 10; d += 1.0f) {
        CHECK(grid_visibility(d + 1.0f) <= grid_visibility(d));
    }
    CHECK(grid_lens_scale(0) == 1.0f && fabsf(grid_lens_scale(GRID_LENS_R) - GRID_MIN_SCALE) < 1e-5f);
}

static void test_physics(void)
{
    /* Muelle crítico: llega al objetivo sin oscilar ni pasarse */
    grid_vec_t p = {100, -50}, v = {0, 0}, t = {0, 0};
    float last = 1e9f;
    for (int i = 0; i < 120; i++) {
        grid_spring_step(&p, &v, t, GRID_SNAP_OMEGA, 0.016f);
        const float d = dist(p, t);
        CHECK(d <= last + 1e-3f);
        CHECK(p.x >= -0.01f && p.y <= 0.01f);
        last = d;
    }
    CHECK(last < 0.5f);
    /* Paso grande (tras una pausa): estable */
    p.x = 300;
    p.y = 0;
    v.x = v.y = 0;
    grid_spring_step(&p, &v, t, GRID_LENS_OMEGA, 0.05f);
    CHECK(p.x < 300.0f && p.x > -5.0f);
    /* Inercia: frena de forma exponencial; recorrido total = v·tau */
    grid_vec_t o = {0, 0}, vel = {1000, -500};
    for (int i = 0; i < 400; i++) {
        grid_inertia_step(&o, &vel, 0.01f);
    }
    CHECK(fabsf(o.x - 1000.0f * GRID_FRICTION_TAU) < 2.0f);
    CHECK(fabsf(o.y + 500.0f * GRID_FRICTION_TAU) < 1.0f);
    CHECK(fabsf(vel.x) < 1.0f);
    /* Centrado: deja una celda exactamente en el centro */
    const grid_vec_t off = {37.0f, -61.0f};
    const grid_vec_t s = grid_snap_target(off);
    int q, r;
    grid_nearest_cell(-s.x, -s.y, &q, &r);
    const grid_vec_t cp = grid_cell_pos(q, r);
    CHECK(fabsf(cp.x + s.x) < 1e-3f && fabsf(cp.y + s.y) < 1e-3f);
    CHECK(dist(s, off) <= GRID_PITCH * 0.6f);
    /* Velocidad del dedo */
    grid_vel_t vs;
    grid_vel_reset(&vs);
    for (int i = 0; i < 6; i++) {
        grid_vel_add(&vs, (float)(i * 10), (float)(-i * 5), (uint32_t)(i * 16));
    }
    grid_vec_t sp = grid_vel_get(&vs, 80);
    CHECK(fabsf(sp.x - 625.0f) < 1.0f && fabsf(sp.y + 312.5f) < 1.0f);
    sp = grid_vel_get(&vs, 300); /* el dedo se quedó quieto antes de soltar */
    CHECK(sp.x == 0.0f && sp.y == 0.0f);
    grid_vel_reset(&vs);
    grid_vel_add(&vs, 0, 0, 0);
    grid_vel_add(&vs, 400, 0, 16);
    sp = grid_vel_get(&vs, 16);
    CHECK(sp.x <= GRID_MAX_SPEED + 0.5f);
}

static void test_blur(void)
{
    const int W = FLEX_SCREEN_W, H = FLEX_SCREEN_H;
    uint16_t *src = malloc((size_t)W * H * 2), *dst = malloc((size_t)W * H * 2);
    uint8_t *small = malloc(flex_blur_small_bytes(W, H)), *tmp = malloc(flex_blur_tmp_bytes(W, H));
    int sw, sh;
    flex_blur_small_size(W, H, &sw, &sh);
    CHECK_EQ(sw, 103);
    CHECK_EQ(sh, 126);
    /* Color uniforme: sale el mismo (con dim = 255), sin bordes raros */
    for (int i = 0; i < W * H; i++) {
        src[i] = (uint16_t)((20 << 11) | (40 << 5) | 10);
    }
    flex_blur_t b;
    flex_blur_begin(&b, src, dst, W, H, small, tmp, 255);
    int steps = 0;
    while (!flex_blur_step(&b, 48)) {
        steps++;
    }
    CHECK(steps > 10); /* de verdad se reparte en muchos pasos */
    int bad = 0;
    for (int i = 0; i < W * H; i++) {
        const int r = dst[i] >> 11, g = (dst[i] >> 5) & 0x3F, bl = dst[i] & 0x1F;
        bad += abs(r - 20) > 1 || abs(g - 40) > 1 || abs(bl - 10) > 1;
    }
    CHECK_EQ(bad, 0);
    /* Un borde nítido blanco/negro queda suave (desenfocado) y oscurecido con dim */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            src[y * W + x] = x < W / 2 ? 0xFFFF : 0x0000;
        }
    }
    flex_blur_begin(&b, src, dst, W, H, small, tmp, 165);
    flex_blur_run(&b);
    const int row = H / 2;
    const int g_l = (dst[row * W + 10] >> 5) & 0x3F, g_r = (dst[row * W + W - 10] >> 5) & 0x3F;
    const int g_m = (dst[row * W + W / 2] >> 5) & 0x3F;
    CHECK(g_l >= 38 && g_l <= 43); /* 63 * 165 / 255 = 40,8 */
    CHECK(g_r <= 1);
    CHECK(g_m > 10 && g_m < 32); /* transición suave en el borde */
    int ramp = 0;
    for (int x = 0; x < W; x++) {
        const int g = (dst[row * W + x] >> 5) & 0x3F;
        ramp += g > 3 && g < 37;
    }
    CHECK(ramp >= 16); /* el salto se reparte en decenas de píxeles */
    free(src);
    free(dst);
    free(small);
    free(tmp);
}


/* ---- Miniaturas del selector de esferas (flex_thumb) ---- */

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void test_thumb(void)
{
    enum { SW = 410, SH = 502, DW = 276, DH = 358, R = 26 };
    uint16_t *src = malloc((size_t)SW * SH * 2), *dst = malloc((size_t)DW * DH * 2);
    void *sc = malloc(flex_thumb_scratch_bytes(DW));
    CHECK(src && dst && sc);
    if (!src || !dst || !sc) {
        free(src);
        free(dst);
        free(sc);
        return;
    }
    /* Color liso: igual dentro, negro fuera de las esquinas redondeadas */
    for (int i = 0; i < SW * SH; i++) {
        src[i] = rgb565(20, 40, 10);
    }
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, DW, DH, R, sc), 0);
    CHECK_EQ(dst[(DH / 2) * DW + DW / 2], rgb565(20, 40, 10));
    CHECK_EQ(dst[R * DW + 1], rgb565(20, 40, 10));     /* borde recto */
    CHECK_EQ(dst[0], 0);                                 /* esquina */
    CHECK_EQ(dst[DW - 1], 0);
    CHECK_EQ(dst[(DH - 1) * DW], 0);
    CHECK_EQ(dst[(DH - 1) * DW + DW - 1], 0);
    /* Escala uniforme (sin deformar): llena en vertical (358/502) y recorta
     * en horizontal lo mismo a cada lado. Una franja blanca en x 0..11 (lo
     * recortado: (410 - 276 * 502 / 358) / 2 = 11,5) no debe verse. */
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            src[y * SW + x] = (x < 11 || x >= SW - 11) ? rgb565(31, 63, 31) : rgb565(0, 0, 0);
        }
    }
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, DW, DH, 0, sc), 0);
    int lit = 0;
    for (int y = 0; y < DH; y++) {
        lit += dst[y * DW] != 0 || dst[y * DW + DW - 1] != 0;
    }
    CHECK_EQ(lit, 0);
    /* Media del área: un degradado horizontal sigue creciendo sin saltos */
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            src[y * SW + x] = rgb565(0, x * 63 / (SW - 1), 0);
        }
    }
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, DW, DH, 0, sc), 0);
    int prev = (dst[(DH / 2) * DW] >> 5) & 0x3F, jumps = 0;
    for (int x = 1; x < DW; x++) {
        const int g = (dst[(DH / 2) * DW + x] >> 5) & 0x3F;
        jumps += g < prev || g - prev > 2;
        prev = g;
    }
    CHECK_EQ(jumps, 0);
    /* Un patrón de líneas de 1 px se promedia: con una reducción de 1,4x cada
     * píxel cubre 1,4 líneas, así que salen grises (de ~0,29 a ~0,71), nunca el
     * blanco o el negro puros que daría tomar un píxel de cada tantos. */
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            src[y * SW + x] = (x & 1) ? rgb565(31, 63, 31) : 0;
        }
    }
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, DW, DH, 0, sc), 0);
    int lo = 63, hi = 0;
    for (int x = 4; x < DW - 4; x++) {
        const int g = (dst[(DH / 2) * DW + x] >> 5) & 0x3F;
        lo = g < lo ? g : lo;
        hi = g > hi ? g : hi;
    }
    CHECK(lo >= 12 && hi <= 51);
    /* Entradas imposibles: no escribe nada */
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, SW + 1, DH, 0, sc), -1);
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, 50, 60, 0, sc), -1); /* más de 3 veces */
    CHECK_EQ(flex_thumb_cover(NULL, SW, SH, SW, dst, DW, DH, 0, sc), -1);
    CHECK_EQ(flex_thumb_cover(src, SW, SH, SW, dst, DW, DH, 0, NULL), -1); /* sin búfer de trabajo */
    free(src);
    free(dst);
    free(sc);
}

void test_grid_all(void)
{
    test_torus();
    test_layout();
    test_physics();
    test_blur();
    test_thumb();
}
