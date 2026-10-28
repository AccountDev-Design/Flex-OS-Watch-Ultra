/*
 * Pruebas del Flex Liquid Glass Engine (flex_glass_math.c): recetas de los
 * materiales por perfil, contraste del texto sobre ellos, perfil automático
 * por rendimiento y perfil efectivo. Lógica pura, sin LVGL.
 */
#include "flex_glass_math.h"
#include "flex_tokens.h"
#include <stdlib.h>
#include <string.h>
#include "test_util.h"
#include "ws_logic.h"

#define BLACK 0x000000u
#define WHITE 0xFFFFFFu

/* Lo que se ve de la parte alta y de la baja de una receta sobre bg. */
static uint32_t top_on(flex_glass_role_t role, flex_glass_profile_t p, flex_glass_state_t st, uint32_t tint,
                       uint32_t bg)
{
    flex_glass_recipe_t r;
    flex_glass_recipe(role, p, st, tint, &r);
    return flex_glass_visible(&r, bg, true);
}

static void test_contrast_formula(void)
{
    /* Valores de referencia de WCAG 2.x */
    CHECK_EQ(flex_contrast_x100(WHITE, BLACK), 2100);
    CHECK_EQ(flex_contrast_x100(BLACK, WHITE), 2100);
    CHECK_EQ(flex_contrast_x100(0x777777, WHITE), 448); /* el gris clásico que no llega a 4,5:1 */
    CHECK_EQ(flex_contrast_x100(0x808080, 0x808080), 100);
}

static void test_profiles(void)
{
    for (int role = 0; role < FLEX_GLASS_ROLE_COUNT; role++) {
        for (int st = 0; st < FLEX_GLASS_ST_COUNT; st++) {
            flex_glass_recipe_t lite, std, pre;
            flex_glass_recipe((flex_glass_role_t)role, FLEX_GLASS_LITE, (flex_glass_state_t)st, FLEX_GLASS_TINT_NONE,
                              &lite);
            flex_glass_recipe((flex_glass_role_t)role, FLEX_GLASS_STANDARD, (flex_glass_state_t)st,
                              FLEX_GLASS_TINT_NONE, &std);
            flex_glass_recipe((flex_glass_role_t)role, FLEX_GLASS_PREMIUM, (flex_glass_state_t)st,
                              FLEX_GLASS_TINT_NONE, &pre);
            const bool ghost_rest = role == FLEX_GLASS_GHOST && st != FLEX_GLASS_ST_PRESSED;
            /* Ligero: opaco (o nada, el fantasma en reposo), sin transiciones,
             * el borde ya mezclado */
            if (ghost_rest) {
                CHECK_EQ(lite.top_opa, 0);
                CHECK_EQ(lite.bot_opa, 0);
            } else {
                CHECK(!flex_glass_recipe_translucent(&lite));
            }
            CHECK_EQ(lite.trans_ms, 0);
            CHECK(lite.rim_w == 0 || lite.rim_opa == 255);
            /* Estándar: plano (un solo color), salvo el ambiente (opaco) */
            if (role != FLEX_GLASS_AMBIENT) {
                CHECK_EQ(std.top_rgb, std.bot_rgb);
                CHECK_EQ(std.top_opa, std.bot_opa);
            }
            CHECK(std.trans_ms > 0 && std.trans_ms < pre.trans_ms);
            /* Premium: el vidrio deja ver lo de debajo y lleva el reflejo
             * arriba (más luz arriba que abajo); el ambiente es opaco */
            if (role == FLEX_GLASS_AMBIENT) {
                CHECK(!flex_glass_recipe_translucent(&pre));
            } else if (!ghost_rest) {
                CHECK(flex_glass_recipe_translucent(&pre));
            }
            if (role == FLEX_GLASS_CONTROL || role == FLEX_GLASS_BAR || role == FLEX_GLASS_LENS) {
                CHECK(pre.top_opa > pre.bot_opa);
                CHECK_EQ(pre.rim_w, FLEX_GLASS_RIM_W); /* borde de luz de 1 px, nunca grueso */
            }
            /* Nada de vidrio se dibuja fuera de su caja (rastros): ni sombra
             * ni contorno, solo un borde interior de como mucho 1 px */
            CHECK(pre.rim_w <= 1 && std.rim_w <= 1 && lite.rim_w <= 1);
        }
    }
    /* El velo en Ligero es negro opaco: LVGL no dibuja la esfera debajo */
    flex_glass_recipe_t scrim;
    flex_glass_recipe(FLEX_GLASS_SCRIM, FLEX_GLASS_LITE, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &scrim);
    CHECK_EQ(scrim.top_rgb, BLACK);
    CHECK_EQ(scrim.top_opa, 255);
    /* Pulsado ilumina (más luz que en reposo) en todos los perfiles */
    for (int p = 0; p < FLEX_GLASS_PROFILE_COUNT; p++) {
        CHECK(top_on(FLEX_GLASS_CONTROL, (flex_glass_profile_t)p, FLEX_GLASS_ST_PRESSED, FLEX_GLASS_TINT_NONE, BLACK) >
              top_on(FLEX_GLASS_CONTROL, (flex_glass_profile_t)p, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, BLACK));
        CHECK(top_on(FLEX_GLASS_CARD, (flex_glass_profile_t)p, FLEX_GLASS_ST_PRESSED, FLEX_GLASS_TINT_NONE, BLACK) >
              top_on(FLEX_GLASS_CARD, (flex_glass_profile_t)p, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, BLACK));
    }
    /* Ligero reproduce el aspecto Estándar sobre el negro de la página */
    const uint32_t a = top_on(FLEX_GLASS_CARD, FLEX_GLASS_STANDARD, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, BLACK);
    const uint32_t b = top_on(FLEX_GLASS_CARD, FLEX_GLASS_LITE, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, BLACK);
    CHECK_EQ(a, b);
}

/* Texto principal y secundario sobre cada material, en el peor fondo que le
 * toca en el sistema. */
static void test_legibility(void)
{
    for (int p = 0; p < FLEX_GLASS_PROFILE_COUNT; p++) {
        const flex_glass_profile_t pr = (flex_glass_profile_t)p;
        /* Controles y tarjetas sobre el negro de la página. En reposo, texto
         * principal AAA (7:1) y secundario AA (4,5:1); mientras el dedo está
         * encima (se ilumina), el principal sigue en AA. La lente solo lleva
         * texto principal (la fila o pestaña elegida). */
        static const flex_glass_role_t kOnPage[] = {FLEX_GLASS_CONTROL, FLEX_GLASS_CARD, FLEX_GLASS_ROW, FLEX_GLASS_BAR,
                                                    FLEX_GLASS_LENS};
        for (size_t i = 0; i < sizeof(kOnPage) / sizeof(kOnPage[0]); i++) {
            for (int top = 0; top < 2; top++) {
                flex_glass_recipe_t r, rp;
                flex_glass_recipe(kOnPage[i], pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &r);
                flex_glass_recipe(kOnPage[i], pr, FLEX_GLASS_ST_PRESSED, FLEX_GLASS_TINT_NONE, &rp);
                const uint32_t bg = flex_glass_visible(&r, FLEX_C_PAGE, top != 0);
                const uint32_t bgp = flex_glass_visible(&rp, FLEX_C_PAGE, top != 0);
                CHECK(flex_contrast_x100(FLEX_GLASS_TXT, bg) >= 700);
                CHECK(kOnPage[i] == FLEX_GLASS_LENS || flex_contrast_x100(FLEX_GLASS_TXT2, bg) >= 450);
                CHECK(flex_contrast_x100(FLEX_GLASS_TXT, bgp) >= 450);
            }
        }
        /* Lámina (avisos) sobre una esfera BLANCA: el texto se sigue leyendo */
        flex_glass_recipe_t sh;
        flex_glass_recipe(FLEX_GLASS_SHEET, pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &sh);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT, flex_glass_visible(&sh, WHITE, true)) >= 450);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT, flex_glass_visible(&sh, WHITE, false)) >= 450);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT2, flex_glass_visible(&sh, WHITE, false)) >= 300);
        /* Panel rápido: velo sobre una esfera blanca y la cápsula encima. Los
         * iconos (objetos gráficos) necesitan 3:1; la hora de la cabecera, 4,5:1 */
        flex_glass_recipe_t sc, ctl;
        flex_glass_recipe(FLEX_GLASS_SCRIM, pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &sc);
        flex_glass_recipe(FLEX_GLASS_CONTROL, pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &ctl);
        const uint32_t under_top = flex_glass_visible(&sc, WHITE, true);
        const uint32_t under_bot = flex_glass_visible(&sc, WHITE, false);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT, under_top) >= 450);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT, flex_glass_visible(&ctl, under_bot, true)) >= 300);
        /* Fondo ambiental (notificaciones): tarjeta encima, texto secundario */
        flex_glass_recipe_t amb, card;
        flex_glass_recipe(FLEX_GLASS_AMBIENT, pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &amb);
        flex_glass_recipe(FLEX_GLASS_CARD, pr, FLEX_GLASS_ST_NORMAL, FLEX_GLASS_TINT_NONE, &card);
        const uint32_t amb_top = flex_glass_visible(&amb, BLACK, true);
        CHECK(flex_contrast_x100(FLEX_GLASS_TXT2, flex_glass_visible(&card, amb_top, true)) >= 450);
        /* Vidrio tintado: el texto que elige la receta se lee sobre su tinte */
        static const uint32_t kTints[] = {FLEX_C_PRIMARY, FLEX_C_DANGER, FLEX_TINT_BLUE, FLEX_TINT_INDIGO,
                                          FLEX_TINT_AMBER, FLEX_TINT_PURPLE, FLEX_TINT_YELLOW, FLEX_TINT_LIME};
        for (size_t i = 0; i < sizeof(kTints) / sizeof(kTints[0]); i++) {
            flex_glass_recipe_t t;
            flex_glass_recipe(FLEX_GLASS_CONTROL, pr, FLEX_GLASS_ST_NORMAL, kTints[i], &t);
            const uint32_t mid = flex_glass_visible(&t, FLEX_C_PAGE, false);
            CHECK(flex_contrast_x100(t.text_rgb, mid) >= 300);
        }
    }
}

static void test_auto(void)
{
    flex_glass_auto_t a;
    flex_glass_auto_init(&a);
    CHECK_EQ(a.cap, FLEX_GLASS_PREMIUM);
    /* Cuadros pequeños (un número que cambia) no cuentan aunque tarden */
    for (int i = 0; i < 100; i++) {
        CHECK(!flex_glass_auto_frame(&a, 200, false));
    }
    CHECK_EQ(a.cap, FLEX_GLASS_PREMIUM);
    /* Una racha corta de cuadros pesados lentos no basta; uno rápido reinicia */
    for (unsigned i = 0; i < FLEX_GLASS_AUTO_SLOW_N - 1; i++) {
        CHECK(!flex_glass_auto_frame(&a, FLEX_GLASS_AUTO_SLOW_MS + 10, true));
    }
    CHECK(!flex_glass_auto_frame(&a, FLEX_GLASS_AUTO_FAST_MS - 5, true));
    CHECK_EQ(a.slow, 0);
    for (unsigned i = 0; i < FLEX_GLASS_AUTO_SLOW_N - 1; i++) {
        CHECK(!flex_glass_auto_frame(&a, FLEX_GLASS_AUTO_SLOW_MS + 10, true));
    }
    /* La franja intermedia ni suma ni reinicia */
    CHECK(!flex_glass_auto_frame(&a, (FLEX_GLASS_AUTO_SLOW_MS + FLEX_GLASS_AUTO_FAST_MS) / 2, true));
    CHECK_EQ(a.slow, FLEX_GLASS_AUTO_SLOW_N - 1);
    CHECK(flex_glass_auto_frame(&a, FLEX_GLASS_AUTO_SLOW_MS + 10, true)); /* el que completa la racha */
    CHECK_EQ(a.cap, FLEX_GLASS_STANDARD);
    CHECK_EQ(a.drops, 1);
    /* Nunca baja a Ligero solo, ni vuelve a subir */
    for (int i = 0; i < 200; i++) {
        flex_glass_auto_frame(&a, 300, true);
    }
    CHECK_EQ(a.cap, FLEX_GLASS_STANDARD);
    for (int i = 0; i < 200; i++) {
        flex_glass_auto_frame(&a, 5, true);
    }
    CHECK_EQ(a.cap, FLEX_GLASS_STANDARD);
}

static void test_effective(void)
{
    flex_glass_auto_t a;
    flex_glass_auto_init(&a);
    CHECK_EQ(flex_glass_effective(WS_GLASS_AUTO, false, &a), FLEX_GLASS_PREMIUM);
    CHECK_EQ(flex_glass_effective(WS_GLASS_AUTO, false, NULL), FLEX_GLASS_PREMIUM);
    CHECK_EQ(flex_glass_effective(WS_GLASS_PREMIUM, false, &a), FLEX_GLASS_PREMIUM);
    CHECK_EQ(flex_glass_effective(WS_GLASS_STANDARD, false, &a), FLEX_GLASS_STANDARD);
    CHECK_EQ(flex_glass_effective(WS_GLASS_LITE, false, &a), FLEX_GLASS_LITE);
    /* El ahorro de energía manda */
    CHECK_EQ(flex_glass_effective(WS_GLASS_PREMIUM, true, &a), FLEX_GLASS_LITE);
    /* Automático sigue al techo medido; uno fijo no */
    a.cap = FLEX_GLASS_STANDARD;
    CHECK_EQ(flex_glass_effective(WS_GLASS_AUTO, false, &a), FLEX_GLASS_STANDARD);
    CHECK_EQ(flex_glass_effective(WS_GLASS_PREMIUM, false, &a), FLEX_GLASS_PREMIUM);
    /* Los valores del ajuste coinciden con los del motor */
    CHECK_EQ(WS_GLASS_AUTO, FLEX_GLASS_SETTING_AUTO);
    CHECK_EQ(WS_GLASS_COUNT, FLEX_GLASS_SETTING_COUNT);
    /* Ajuste guardado fuera de rango: vuelve a automático */
    ws_settings_t s;
    ws_defaults(&s);
    CHECK_EQ(s.glass, WS_GLASS_AUTO);
    s.glass = 9;
    CHECK(ws_normalize(&s));
    CHECK_EQ(s.glass, WS_GLASS_AUTO);
    ws_settings_t t = s;
    t.glass = WS_GLASS_LITE;
    CHECK(!ws_equal(&s, &t));
}

/* Desenfoque desde la fuente (§12): conserva el color medio, reparte un
 * impulso de forma simétrica, no lee fuera y "cover" recorta centrado. */
static uint16_t c565(int r, int g, int b)
{
    return (uint16_t)(r << 11 | g << 5 | b);
}

static void test_source_blur(void)
{
    enum { W = 40, H = 30 };
    static uint16_t img[W * H];
    uint8_t work[3 * W];
    CHECK_EQ(flex_blur_work_bytes(W, H, 4), 3 * W);
    /* Un color liso no cambia. */
    for (int i = 0; i < W * H; i++) {
        img[i] = c565(20, 40, 10);
    }
    CHECK(flex_blur_box565(img, W, H, 4, 3, work));
    int same = 1;
    for (int i = 0; i < W * H; i++) {
        same &= img[i] == c565(20, 40, 10);
    }
    CHECK(same);
    /* Un impulso blanco sobre negro: se reparte simétrico y se conserva la energía. */
    memset(img, 0, sizeof(img));
    img[15 * W + 20] = c565(31, 63, 31);
    CHECK(flex_blur_box565(img, W, H, 2, 1, work));
    const uint16_t l = img[15 * W + 18], r = img[15 * W + 22], u = img[13 * W + 20], d = img[17 * W + 20];
    CHECK_EQ(l, r);
    CHECK_EQ(u, d);
    CHECK_EQ(img[15 * W + 23], 0); /* fuera del radio no llega nada */
    /* Un borde: la mitad izquierda roja y la derecha azul se funden en la frontera. */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            img[y * W + x] = x < W / 2 ? c565(31, 0, 0) : c565(0, 0, 31);
        }
    }
    CHECK(flex_blur_box565(img, W, H, 3, 3, work));
    CHECK_EQ(img[0], c565(31, 0, 0));          /* lejos del borde no cambia */
    CHECK_EQ(img[W - 1], c565(0, 0, 31));
    const uint16_t mid = img[10 * W + W / 2];
    CHECK(((mid >> 11) & 31) > 0 && (mid & 31) > 0); /* mezcla en la frontera */
    /* Argumentos que no valen: no toca nada. */
    CHECK(!flex_blur_box565(NULL, W, H, 2, 1, work));
    CHECK(!flex_blur_box565(img, 0, H, 2, 1, work));
    CHECK(!flex_blur_box565(img, W, H, -1, 1, work));
    CHECK(flex_blur_box565(img, W, H, 0, 3, work)); /* radio 0: igual */
    /* Radio mayor que la imagen: los bordes repetidos, sin leer fuera (ASan). */
    for (int i = 0; i < W * H; i++) {
        img[i] = (uint16_t)(i * 7919u);
    }
    CHECK(flex_blur_box565(img, W, H, 60, 2, work));
    /* "cover": 80x30 -> 20x20 usa el centro (30x30) y promedia celdas. */
    static uint16_t src[80 * 30];
    for (int y = 0; y < 30; y++) {
        for (int x = 0; x < 80; x++) {
            src[y * 80 + x] = (x >= 25 && x < 55) ? c565(0, 63, 0) : c565(31, 0, 0);
        }
    }
    uint16_t dst[20 * 20];
    flex_blur_fit565(src, 80, 30, 80, dst, 20, 20);
    int green = 1;
    for (int i = 0; i < 400; i++) {
        green &= dst[i] == c565(0, 63, 0);
    }
    CHECK(green); /* lo rojo de los lados quedó fuera */
    /* Ampliar (2x2 -> 6x6) y stride mayor que el ancho. */
    const uint16_t tiny[2 * 3] = {c565(31, 0, 0), c565(0, 63, 0), 0xFFFF, c565(0, 0, 31), c565(31, 63, 31), 0xFFFF};
    uint16_t big[36];
    flex_blur_fit565(tiny, 2, 2, 3, big, 6, 6);
    CHECK_EQ(big[0], c565(31, 0, 0));
    CHECK_EQ(big[35], c565(31, 63, 31));
    /* Tono: oscurecer y desaturar. */
    uint16_t t[2] = {c565(31, 63, 31), c565(31, 0, 0)};
    flex_blur_tone565(t, 2, 128, 0);
    CHECK(((t[0] >> 5) & 63) >= 31 && ((t[0] >> 5) & 63) <= 32);
    uint16_t gr = c565(31, 0, 0);
    flex_blur_tone565(&gr, 1, 255, 255);
    const int rr = (gr >> 11) & 31, gg = (gr >> 5) & 63, bb = gr & 31;
    CHECK(abs(rr * 2 - gg) <= 2 && abs(bb * 2 - gg) <= 2); /* gris */
}

void test_glass_all(void)
{
    test_source_blur();
    test_contrast_formula();
    test_profiles();
    test_legibility();
    test_auto();
    test_effective();
}
