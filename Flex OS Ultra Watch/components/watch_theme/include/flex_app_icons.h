/*
 * flex_app_icons — iconos ORIGINALES de las apps de Flex OS Ultra.
 *
 * Son el dibujo de drawAppIcon() de la referencia (FlexOS_Ultra_Icons.h:65-305,
 * estilo "Plano"), rasterizado en el PC por tools/icons/gen_app_icons.c a
 * 92x92 px con antialiasing y BASE CIRCULAR (modo "round": el cajón en panal
 * del reloj, sin nombres, como Apple Watch / One UI Watch; el glifo es el
 * original). Se guardan en flash como RGB565A8 (assets/icons/app_*.bin,
 * 24,8 KB cada uno) y LVGL los dibuja tal cual: sin escalar, sin decodificar,
 * sin máscaras de recorte y sin reservar memoria.
 *
 * Los iconos de Modo PC (DeX), Juegos, Cámara, Galería y Multimedia no se
 * incluyen: esas apps están excluidas en el reloj (docs/INVENTARIO-APPS.md).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_APP_ICON_SIZE 92

/** Apps con icono original. El orden es el de la Caja de aplicaciones. */
typedef enum {
    FLEX_APP_RELOJ = 0,
    FLEX_APP_CALENDARIO,
    FLEX_APP_CALCULADORA,
    FLEX_APP_CLIMA,
    FLEX_APP_MUSICA,
    FLEX_APP_NOTAS,
    FLEX_APP_FLEX_PHONE,
    FLEX_APP_NAVEGADOR,
    FLEX_APP_FLEX_COMPASS,
    FLEX_APP_DEVICE_CARE,
    FLEX_APP_ALMACENAMIENTO,
    FLEX_APP_PAINT,
    FLEX_APP_FLEX_STORE,
    FLEX_APP_AJUSTES,
    FLEX_APP_SENSORES, /**< solo en el reloj: icono nuevo con el lenguaje de la referencia */
    FLEX_APP_COUNT_
} flex_app_t;

/** Descriptor de imagen (RGB565A8, FLEX_APP_ICON_SIZE px) o NULL si app no es válida. */
const lv_image_dsc_t *flex_app_icon(flex_app_t app);

/** Nombre de la app: el original de APP[][0] (FlexOS_Ultra_Session.h:345) o el del reloj. */
const char *flex_app_name(flex_app_t app);

#ifdef __cplusplus
}
#endif
