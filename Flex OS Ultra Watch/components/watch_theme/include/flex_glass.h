/*
 * flex_glass — Flex Liquid Glass Engine: la ÚNICA implementación de Liquid
 * Glass de Flex OS Watch Ultra (LVGL 9.5, render por software en franjas).
 *
 * Qué toma de Apple (HIG Materials, WWDC25 "Meet Liquid Glass", SwiftUI
 * glassEffect/GlassEffectContainer) y cómo lo hace aquí, en C:
 *  - Capa funcional: el vidrio es para navegación y controles (CONTROL, BAR,
 *    LENS, SHEET); el contenido usa un material discreto (CARD, ROW). Nunca
 *    vidrio sobre vidrio: un control va sobre un velo, una página o un
 *    ambiente, no sobre otra lámina de vidrio.
 *  - Translucidez real: el cuerpo tiene alfa y LVGL compone lo que hay debajo
 *    (la esfera bajo el panel rápido). Reflejo especular arriba con un
 *    degradado vertical y un borde de luz de 1 px.
 *  - Refracción y desenfoque en tiempo real: NO. LVGL 9.5 tiene
 *    blur_backdrop, pero con búferes parciales desenfoca cada franja por
 *    separado (costuras) y solo la zona invalidada (parches), y cuesta
 *    cuatro pasadas por píxel en cada cuadro: no se usa. Ver
 *    docs/LIQUID-GLASS.md.
 *  - Tinte selectivo (activo y acción principal), respuesta al toque que
 *    "ilumina" el material (transición de estilo, no una capa ni una escala) y
 *    el fantasma (GHOST): invisible en reposo, vidrio solo mientras se toca.
 *  - Velo de atenuación (SCRIM) para el vidrio sobre contenido rico.
 *
 * Perfiles (flex_glass_set_profile): Premium, Estándar y Ligero. Todos los
 * objetos comparten estilos del motor: cambiar de perfil reescribe esos
 * estilos y LVGL refresca lo que los usa, sin recrear ni reconstruir nada.
 * Ligero pinta superficies opacas precalculadas sobre el negro de la página.
 *
 * Reglas de dibujo (rastros): nada de sombras, contornos exteriores ni hijos
 * flotantes de adorno; todo lo que dibuja una superficie cae dentro de sus
 * coordenadas, así que la invalidación de LVGL al moverla, ocultarla o
 * borrarla cubre siempre sus píxeles viejos y nuevos.
 *
 * Todas las funciones se llaman con LVGL bloqueado.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "flex_glass_math.h"
#include "flex_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Inicia los estilos compartidos (Premium por defecto). Idempotente. */
void flex_glass_init(void);
/** Cambia el perfil de todo el sistema en el acto (sin recrear objetos). */
void flex_glass_set_profile(flex_glass_profile_t p);
flex_glass_profile_t flex_glass_profile(void);
/** "Premium", "Estándar", "Ligero". */
const char *flex_glass_profile_name(flex_glass_profile_t p);

/* ---- Superficies ---- */

/** Objeto con el material role (sin desplazamiento ni toques; tamaño y radio
 * los pone quien lo crea). */
lv_obj_t *flex_glass_create(lv_obj_t *parent, flex_glass_role_t role);
/** Igual, en (x, y) con w x h y radio r. */
lv_obj_t *flex_glass_panel(lv_obj_t *parent, flex_glass_role_t role, int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r);
/** flex_glass_panel pulsable (flex_glass_interactive). */
lv_obj_t *flex_glass_pressable(lv_obj_t *parent, flex_glass_role_t role, int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t r);
/** Pone el material role en una parte de un objeto que ya existe (página,
 * pista de un deslizador...). */
void flex_glass_apply(lv_obj_t *obj, flex_glass_role_t role, lv_style_selector_t part);
/** Igual que flex_glass_apply y además los estados pulsado y desactivado
 * de esa parte (teclas de un teclado: LV_PART_ITEMS). */
void flex_glass_apply_states(lv_obj_t *obj, flex_glass_role_t role, lv_style_selector_t part);
/** Pulsable con los estados del material (pulsado ilumina, desactivado
 * apaga). Toma el material del objeto (de flex_glass_create/apply). */
void flex_glass_interactive(lv_obj_t *obj);
/**
 * Tinte selectivo: tint (0xRRGGBB) para los estados en `when`:
 * LV_STATE_DEFAULT = siempre tintado (acción principal), LV_STATE_CHECKED =
 * tintado solo mientras está activo. FLEX_GLASS_TINT_NONE quita el tinte.
 * Solo cambia algo si el tinte es otro (cada cambio de estilo redibuja).
 */
void flex_glass_set_tint(lv_obj_t *obj, uint32_t tint, lv_state_t when);

/* ---- Componentes ---- */

/**
 * Botón en píldora con el texto centrado (hijo 0, Outfit Medium). tint =
 * FLEX_GLASS_TINT_NONE para el vidrio normal (acción secundaria) o un color
 * para la principal (FLEX_C_PRIMARY) o una destructiva (FLEX_C_DANGER). Quien
 * lo crea escucha LV_EVENT_CLICKED; LV_STATE_DISABLED lo apaga.
 */
lv_obj_t *flex_glass_button(lv_obj_t *parent, const char *text, uint32_t tint, int32_t w, int32_t h);
/** Cambia texto (NULL = igual) y tinte (0 = igual, FLEX_GLASS_TINT_NONE = sin tinte). */
void flex_glass_button_set(lv_obj_t *btn, const char *text, uint32_t tint);
/** Botón circular de diámetro d con un icono centrado (hijo 0). */
lv_obj_t *flex_glass_icon_button(lv_obj_t *parent, flex_icon_t icon, int32_t icon_size, int32_t d, uint32_t tint);
/** Control de vidrio pulsable en (x, y), w x h (radio de tarjeta, relleno 18). */
lv_obj_t *flex_glass_control(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
/** Tarjeta de contenido pulsable en (x, y), w x h (radio de tarjeta, relleno
 * 16). Para una que no se pulsa: lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE). */
lv_obj_t *flex_glass_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
/** Interruptor: pista de vidrio, verde activo, botón blanco. */
lv_obj_t *flex_glass_switch(lv_obj_t *parent, int32_t w, int32_t h);
/** El mismo aspecto para un lv_switch que ya existe. */
void flex_glass_switch_style(lv_obj_t *sw, int32_t w, int32_t h);
/** Deslizador en píldora: pista de vidrio y relleno fill_rgb. */
lv_obj_t *flex_glass_slider(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t fill_rgb);
/** Lente de selección (LENS) dentro de una barra; se mueve con
 * flex_glass_lens_move. */
lv_obj_t *flex_glass_lens(lv_obj_t *bar, int32_t h);
/** Lleva la lente a x con ancho w (animada: ms > 0). */
void flex_glass_lens_move(lv_obj_t *lens, int32_t x, int32_t w, uint32_t ms);

/* ---- Esferas: placa CONGELADA ----
 * La placa de vidrio de las esferas (complicaciones, esfera Liquid Glass) es
 * idéntica a la de siempre y no cambia con el perfil: las esferas no se
 * tocan. Sobre negro es opaca y precalculada; sobre un fondo, translúcida con
 * el tinte adaptativo según la luma real del fondo que queda debajo. */
lv_obj_t *flex_glass_face_plate(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
/** Suelo de tinte de una placa (texto legible sobre fondos que no se controlan). */
void flex_glass_face_plate_min_tint(lv_obj_t *plate, lv_opa_t min_mix);
#define FLEX_GLASS_MIN_MIX_LIGHT 200

/** Estilos vivos del motor (diagnóstico y pruebas). */
uint32_t flex_glass_style_count(void);

#ifdef __cplusplus
}
#endif
