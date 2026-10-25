/*
 * flex_tokens — tokens de diseño de Flex OS Ultra para el reloj.
 *
 * Valores TRANSCRITOS de la referencia Arduino (accountdev-design/
 * Flex-OS-Ecosistema, rama claude/flexos-ota-system-blq9g9-2csacx @ 5dbc949):
 *   paleta oscura   FlexOS_Ultra/FlexOS_Ultra_Theme.h:114-141   (kThemeDark)
 *   tema "AMOLED"   FlexOS_Ultra/FlexOS_Ultra_Wallpaper.h:507   (LOOKS[3])
 *   fondo "Onyx"    FlexOS_Ultra/FlexOS_Ultra_Wallpaper.h       (wallOnyx)
 *   vidrio nivel 50 FlexOS_Ultra/FlexOS_Ultra_Theme.h:421-439   (glassLevelApply)
 *   borde del vidrio FlexOS_Ultra/FlexOS_Ultra_Theme.h:524-530
 *   movimiento      FlexOS_Ultra/FlexOS_Ultra_AppFramework.h:866-940, Home.h:278-320
 *
 * Geometría: el P4 tiene ~217 ppi (4,3" 480x800); el reloj ~315 ppi (2,06"
 * 410x502). Las medidas de interfaz NO se escalan por 410/480: los tamaños de
 * letra y las zonas táctiles se agrandan en píxeles para conservar su tamaño
 * físico, y los márgenes se rediseñan para la pantalla pequeña.
 *
 * Cabecera C pura (sin LVGL).
 */
#pragma once

/* ---- Paleta oscura (0xRRGGBB) ---------------------------------------- */
/* Fondo de página: NEGRO PURO. En un AMOLED cada píxel negro está apagado:
 * es el fondo que menos consume y el que pidió el usuario para el modo oscuro
 * (2026-10-15). La referencia usa 0x12141C (gris muy oscuro) en pantallas LCD. */
#define FLEX_C_PAGE      0x000000
#define FLEX_C_SURF      0x222632
#define FLEX_C_SURF2     0x303648
#define FLEX_C_GLASS     0x303648
#define FLEX_C_GLASS2    0x28325A
#define FLEX_C_TXT       0xF0F2F8
#define FLEX_C_TXT2      0xA0A6B6
#define FLEX_C_MUTE      0x787E8E
#define FLEX_C_BORDER    0x424A5E
#define FLEX_C_DIVIDER   0x343A4A
#define FLEX_C_TRACK     0x383E56
#define FLEX_C_PRIMARY   0x3C6EEB
#define FLEX_C_DANGER    0xC84646
#define FLEX_C_OK        0x5AC88C
#define FLEX_C_WARN      0xF0B45A
#define FLEX_C_ERR       0xEB5050
#define FLEX_C_ACC_SOFT  0x8CB4FA
#define FLEX_C_BLACK     0x000000
#define FLEX_C_WHITE     0xFFFFFF

/* Acentos de las categorías de Ajustes (AppSettings.h:413-416, SET_ACCENT) */
#define FLEX_C_CAT_BLUE   0x4678EB   /* General, Sistema, Acerca de */
#define FLEX_C_CAT_SUN    0xF0AA32   /* Pantalla */
#define FLEX_C_CAT_GREEN  0x50BE6E   /* Batería */
#define FLEX_C_CAT_NET    0x3C96EB   /* Red e Internet */

/* ---- Tema "AMOLED" (oscuro, plano, fondo Onyx): por defecto en el reloj -- */
#define FLEX_C_ACCENT       0x788CFF
#define FLEX_C_ACCENT_LIGHT 0xB4C4FF

/* ---- Fondo Onyx: negro + halo radial (480x800 en el P4) --------------- */
#define FLEX_ONYX_HALO_RGB  0x1E40B4
#define FLEX_ONYX_CX_480    240
#define FLEX_ONYX_CY_800    690
#define FLEX_ONYX_R_IN_480  60
#define FLEX_ONYX_R_OUT_480 380
#define FLEX_ONYX_ALPHA     46

/* ---- Material Liquid Glass, nivel 50 (alfa 0..255) -------------------- */
#define FLEX_GLASS_TINT_MIN     46
#define FLEX_GLASS_TINT_MAX     70
#define FLEX_GLASS_TINT_BASE    58
#define FLEX_GLASS_SPECULAR     26   /* blanco, 45 % superior, decrece a 0 */
#define FLEX_GLASS_SHADE        30   /* negro, 55 % inferior, crece desde 0 */
#define FLEX_GLASS_EDGE_STRONG  156
#define FLEX_GLASS_EDGE_WEAK    104
#define FLEX_GLASS_EDGE_HI      0xCDD6E4
#define FLEX_GLASS_EDGE_LO      0x161C28
/* Estados de los controles de vidrio. Activo = acento con la mezcla de
 * qpMixAcc() (QuickPanelGlass.h:154). Pulsado: la referencia responde con un
 * destello blanco (Widgets.h:593); aquí, para no animar nada por cada toque,
 * el tinte se aclara mientras el dedo está encima. */
#define FLEX_GLASS_ACTIVE_MIX   178
#define FLEX_C_GLASS_PRESSED    0x56607E
#define FLEX_GLASS_PRESSED_MIX  170
#define FLEX_GLASS_DISABLED_MIX 26

/* ---- Geometría del reloj (px del panel de 410x502) ------------------- */
#define FLEX_SCREEN_W        410
#define FLEX_SCREEN_H        502
/* Márgenes mínimos. Cerca de las esquinas redondeadas del panel manda
 * flex_safe.h (radio estimado, pendiente de la prueba T44). */
#define FLEX_SAFE_X          22
#define FLEX_SAFE_TOP        20
#define FLEX_STATUS_H        48
#define FLEX_RADIUS_CARD     26   /* tarjeta grande del panel rápido (QuickPanel.h:105) */
#define FLEX_RADIUS_MODULE   20
#define FLEX_RADIUS_ROW      16   /* tarjeta de fila de Ajustes (AppSettings.h:431) */
/* Zona táctil mínima: 44 px a 217 ppi (QuickPanel.h:117) ~ 5 mm -> 64 px a 315 ppi */
#define FLEX_TOUCH_MIN       64

/* Cabecera estándar (AppFramework.h:399-405): zona de "volver" a la
 * izquierda y título después de ella. Allí la zona mide 56 (>= 44 a 217 ppi);
 * aquí FLEX_TOUCH_MIN. La zona se aparta de la esquina redondeada lo que pida
 * flex_safe.h (watch_theme_header); FLEX_HDR_X es solo el mínimo. */
#define FLEX_HDR_ZONE        FLEX_TOUCH_MIN
#define FLEX_HDR_X           8
#define FLEX_HDR_GAP         8
#define FLEX_HDR_H           (FLEX_SAFE_TOP + FLEX_HDR_ZONE)

/* Filas de lista (Ajustes): 66 px + 8 de separación en la referencia. Aquí
 * 72 (por encima de la zona táctil mínima) para que quepan 5 filas bajo la
 * cabecera en 502 px. */
#define FLEX_LIST_X          FLEX_SAFE_X
/* Hueco negro bajo las listas: la parte visible de la lista termina donde la
 * esquina redondeada ya no muerde ni el lateral de una fila que asoma a
 * medio desplazar (x = FLEX_LIST_X) ni su contenido. Lo comprueba
 * tests/host/test_safe.c para el FLEX_CORNER_R vigente. */
#define FLEX_LIST_BOTTOM     52
#define FLEX_ROW_H           72
#define FLEX_ROW_GAP         8
#define FLEX_ROW_ICON        32
#define FLEX_INFO_ROW_H      56   /* fila de solo información (no pulsable) */

/* ---- Rediseño 2026-10: colores de los diseños del usuario ---------------
 * Muestreados de las referencias escaladas a 410x502 (design/referencias,
 * docs/REDISENO-2026-10.md). Solo los usan las pantallas rediseñadas. */
/* Liquid Glass oscuro (tarjetas sobre negro: Reloj, Almacenamiento, alarmas) */
#define FLEX_LG_DARK_TOP     0x24282E   /* degradado vertical del cristal */
#define FLEX_LG_DARK_BOT     0x121518
#define FLEX_LG_DARK_EDGE    0xA9BCCF   /* borde de luz */
#define FLEX_LG_DARK_EDGE_OPA 170
#define FLEX_LG_GLOW         0xFFFFFF   /* reflejo del borde inferior */
#define FLEX_LG_GLOW_OPA     200
/* Liquid Glass claro (mosaicos sobre el degradado del panel rápido) */
#define FLEX_LG_LIGHT_FILL_OPA 34       /* blanco */
#define FLEX_LG_LIGHT_EDGE_OPA 120
/* Panel rápido */
#define FLEX_QP_GRAD_L       0x56BCC8
#define FLEX_QP_GRAD_R       0x9EE38C
#define FLEX_QP_ACTIVE       0x5CE1E6   /* mosaico activo y relleno del brillo */
/* Reloj */
#define FLEX_RD_TAB_ACTIVE   0x5170FF
#define FLEX_RD_TAB_TOP      0x2A2A2A
#define FLEX_RD_TAB_BOT      0x0E0E0E
#define FLEX_RD_DIAL         0x26273B
#define FLEX_RD_DIAL_CENTER  0x2E3148
#define FLEX_RD_HAND         0x6869EE
#define FLEX_RD_START        0x9D39EE
#define FLEX_RD_START_BOT    0x8A27E6
#define FLEX_RD_SWITCH_ON    0x00D54F
#define FLEX_RD_SWITCH_OFF   0xB2B2B2
#define FLEX_RD_ALARM_SUB    0x6669A2   /* "Diariamente" activa */
#define FLEX_RD_ALARM_OFF    0x6B7075   /* alarma desactivada */
#define FLEX_RD_FADED        0x3C3F44   /* valores vecinos del rodillo */
#define FLEX_RD_FADED_BLUE   0x4D5070
/* Almacenamiento */
#define FLEX_ST_FLASH        0x5170FF
#define FLEX_ST_PSRAM        0xC1FF72
#define FLEX_ST_TRACK        0x737373
#define FLEX_ST_BUTTON       0x4C6CFF
#define FLEX_ST_DOCS         0xFF3131
#define FLEX_ST_SYSTEM       0xFFDE59
#define FLEX_ST_APPS         0x3CC027
#define FLEX_ST_TRASH        0x38B6FF
/* Brújula */
#define FLEX_CMP_ORANGE      0xFF4F06
#define FLEX_CMP_RED         0xE0301E
#define FLEX_CMP_BEZEL       0x3C3C3F

/* ---- Movimiento (ms) -------------------------------------------------- */
#define FLEX_ANIM_APP_OPEN_MS   210
#define FLEX_ANIM_APP_CLOSE_MS  190
#define FLEX_ANIM_BOOT_FADE_MS  600
#define FLEX_ANIM_BANNER_IN_MS  220
#define FLEX_ANIM_BANNER_OUT_MS 180
