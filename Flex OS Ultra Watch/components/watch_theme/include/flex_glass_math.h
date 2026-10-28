/*
 * flex_glass_math — fórmulas PURAS del material Liquid Glass (nivel 50),
 * transcritas de FlexOS_Ultra/FlexOS_Ultra_Theme.h (referencia 5dbc949):
 *   glassLuma()    :265-268  luma en dominio 565 crudo ((r5+g6)*5 + b5*2) >> 1
 *   glassTintMix() :447-457  tinte entre TINT_MIN y TINT_MAX según la
 *                            diferencia de luma fondo/tinte (DIFF_MAX 128)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_GLASS_TINT_DIFF_MAX 128

int flex_glass_luma565(uint16_t c);

/** Mezcla de tinte (alfa 0..255) para un panel cuyo fondo suma luma_sum en n muestras. */
uint8_t flex_glass_tint_mix(uint32_t luma_sum, int n, uint16_t tint565);

/**
 * Color resultante de pintar fg con alfa a (0..255) sobre bg (0xRRGGBB), con
 * redondeo. Sirve para precalcular superficies OPACAS cuando el fondo es
 * conocido (negro AMOLED): mismo aspecto que el vidrio translúcido, sin mezcla
 * por píxel en cada cuadro.
 */
uint32_t flex_mix_rgb(uint32_t fg, uint32_t bg, uint8_t a);

/** Suma la luma del rectángulo (x, y, rw, rh) de un búfer RGB565 de ancho w (1 de cada 4 px). */
void flex_glass_region_luma(const uint16_t *buf, int w, int h, int x, int y, int rw, int rh,
                            uint32_t *sum, int *n);

/* ---- Recetas del Flex Liquid Glass Engine (puras: se prueban en el PC) ----
 * Cada material (función) se resuelve, para un perfil y un estado, en un
 * cuerpo con dos extremos (arriba y abajo: degradado vertical o plano) y un
 * borde de luz. flex_glass.c traduce la receta a estilos de LVGL. */

typedef enum {
    FLEX_GLASS_LITE = 0,  /**< superficies opacas precalculadas, contornos, sin transiciones */
    FLEX_GLASS_STANDARD,  /**< translucidez plana y borde de luz */
    FLEX_GLASS_PREMIUM,   /**< translucidez con reflejo (degradado), borde de luz y transiciones */
    FLEX_GLASS_PROFILE_COUNT
} flex_glass_profile_t;

typedef enum {
    /* Capa de navegación y controles (Liquid Glass) */
    FLEX_GLASS_CONTROL = 0, /**< botón, mosaico, píldora: blanco translúcido con reflejo */
    FLEX_GLASS_BAR,         /**< barra de pestañas o de herramientas */
    FLEX_GLASS_LENS,        /**< lente de la pestaña o del segmento activos (dentro de una barra) */
    FLEX_GLASS_SHEET,       /**< lámina oscura flotante (aviso, diálogo): legible sobre cualquier fondo */
    FLEX_GLASS_SCRIM,       /**< velo que atenúa el contenido de debajo (panel rápido) */
    FLEX_GLASS_AMBIENT,     /**< fondo ambiental opaco de una página que aloja vidrio */
    /* Capa de contenido (material estándar, discreto) */
    FLEX_GLASS_CARD,        /**< tarjeta de contenido */
    FLEX_GLASS_ROW,         /**< fila de lista */
    FLEX_GLASS_GHOST,       /**< invisible en reposo; vidrio solo mientras se pulsa */
    FLEX_GLASS_ROLE_COUNT
} flex_glass_role_t;

typedef enum { FLEX_GLASS_ST_NORMAL = 0, FLEX_GLASS_ST_PRESSED, FLEX_GLASS_ST_DISABLED, FLEX_GLASS_ST_COUNT } flex_glass_state_t;

/** Sin tinte (los colores de 24 bits son tintes). */
#define FLEX_GLASS_TINT_NONE 0x1000000u

typedef struct {
    uint32_t top_rgb, bot_rgb; /**< color del cuerpo arriba y abajo (iguales y mismo alfa = plano) */
    uint8_t top_opa, bot_opa;  /**< alfa de cada extremo (255 = opaco) */
    uint8_t grad_end;          /**< 0..255: altura relativa donde termina el degradado */
    uint32_t rim_rgb;          /**< borde de luz */
    uint8_t rim_opa;
    uint8_t rim_w;             /**< 0 = sin borde */
    uint32_t text_rgb;         /**< texto principal encima */
    uint16_t trans_ms;         /**< transición al cambiar de estado (0 = instantáneo) */
} flex_glass_recipe_t;

/**
 * Receta del material role en el perfil p y el estado st. tint es un color
 * 0xRRGGBB (vidrio tintado: lo activo o la acción principal) o
 * FLEX_GLASS_TINT_NONE. En FLEX_GLASS_LITE todo sale opaco, precalculado
 * sobre el negro de la página (el fondo conocido): LVGL no mezcla nada por
 * píxel ni dibuja lo que queda debajo.
 */
void flex_glass_recipe(flex_glass_role_t role, flex_glass_profile_t p, flex_glass_state_t st, uint32_t tint,
                       flex_glass_recipe_t *out);

/** ¿El material deja ver lo que hay debajo? (cuerpo con alfa < 255) */
bool flex_glass_recipe_translucent(const flex_glass_recipe_t *r);

/** Color que se ve de la receta sobre un fondo bg (top: parte alta; si no, la baja). */
uint32_t flex_glass_visible(const flex_glass_recipe_t *r, uint32_t bg, bool top);

/** Contraste (WCAG 2.x) entre dos colores, multiplicado por 100 (450 = 4,5:1). */
int flex_contrast_x100(uint32_t a, uint32_t b);

/* ---- Perfil automático por rendimiento medido ----
 * Cuenta los cuadros "pesados" (casi toda la pantalla, p. ej. el panel rápido
 * siguiendo al dedo) que tardan más de FLEX_GLASS_AUTO_SLOW_MS. Tras
 * FLEX_GLASS_AUTO_SLOW_N seguidos baja un nivel el techo de calidad (nunca
 * por debajo de Estándar: Ligero es decisión del usuario o del ahorro). Un
 * cuadro pesado rápido reinicia la cuenta. No vuelve a subir solo durante la
 * sesión: sin vaivenes. */
#define FLEX_GLASS_AUTO_SLOW_MS 45u
#define FLEX_GLASS_AUTO_FAST_MS 30u
#define FLEX_GLASS_AUTO_SLOW_N  12u

typedef struct {
    uint16_t slow;            /**< cuadros pesados lentos seguidos */
    flex_glass_profile_t cap; /**< techo actual */
    uint32_t drops;           /**< veces que ha bajado el techo */
} flex_glass_auto_t;

void flex_glass_auto_init(flex_glass_auto_t *a);
/** Un cuadro dibujado (ms que tardó; heavy: cubría casi toda la pantalla).
 * true si el techo acaba de bajar. */
bool flex_glass_auto_frame(flex_glass_auto_t *a, uint32_t ms, bool heavy);

/** Ajuste del usuario (0 automático, 1 Premium, 2 Estándar, 3 Ligero) -> perfil efectivo. */
#define FLEX_GLASS_SETTING_AUTO 0
#define FLEX_GLASS_SETTING_COUNT 4
flex_glass_profile_t flex_glass_effective(uint8_t setting, bool power_saving, const flex_glass_auto_t *a);

/* ---- Desenfoque DESDE LA FUENTE (docs/LIQUID-GLASS.md §12) ----
 * No se lee nunca la pantalla: se desenfoca una imagen conocida (portada,
 * foto, fotograma) una vez por cambio, en un búfer pequeño propio, y se dibuja
 * como cualquier imagen. Así no hay costuras entre franjas, ni parches al
 * redibujar una zona, ni rastros de lo que había antes (§4.1). */

/** Reduce src (sw x sh, stride en píxeles) a dst (dw x dh) llenándolo
 * ("cover": recorta centrado lo que sobra) con la media de cada celda. */
void flex_blur_fit565(const uint16_t *src, int sw, int sh, int sstride, uint16_t *dst, int dw, int dh);

/** Bytes de trabajo que pide flex_blur_box565 para una imagen w x h. */
int flex_blur_work_bytes(int w, int h, int radius);
/** Desenfoque de caja separable (passes pasadas ~ gaussiano), en su sitio, con
 * los bordes repetidos. work: flex_blur_work_bytes() bytes. false si los
 * argumentos no valen (no toca la imagen). */
bool flex_blur_box565(uint16_t *px, int w, int h, int radius, int passes, uint8_t *work);

/** Oscurece (mul/255) y acerca al gris (desat/255): el fondo que deja leer
 * texto blanco encima, como el material de Apple sobre una portada. */
void flex_blur_tone565(uint16_t *px, int n, uint8_t mul, uint8_t desat);

#ifdef __cplusplus
}
#endif
