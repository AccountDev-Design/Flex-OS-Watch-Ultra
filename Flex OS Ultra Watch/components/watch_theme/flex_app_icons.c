/*
 * flex_app_icons — ver include/flex_app_icons.h
 *
 * Los binarios de assets/icons se incrustan con EMBED_FILES (CMakeLists.txt):
 * ESP-IDF publica cada archivo como _binary_<nombre>_bin_start/_end. La vista
 * previa de PC (tools/sim) los enlaza igual con objcopy.
 */
#include "flex_app_icons.h"

#define ICON_BYTES (FLEX_APP_ICON_SIZE * FLEX_APP_ICON_SIZE * 3)

#define ICON_EXTERN(n) extern const uint8_t _binary_app_##n##_bin_start[]

ICON_EXTERN(reloj);
ICON_EXTERN(calend);
ICON_EXTERN(calc);
ICON_EXTERN(clima);
ICON_EXTERN(musica);
ICON_EXTERN(notas);
ICON_EXTERN(flexphone);
ICON_EXTERN(nav);
ICON_EXTERN(brujula);
ICON_EXTERN(devcare);
ICON_EXTERN(almacen);
ICON_EXTERN(paint);
ICON_EXTERN(flexstore);
ICON_EXTERN(ajustes);
ICON_EXTERN(sensores);
ICON_EXTERN(galeria);
ICON_EXTERN(bienestar);

#define ICON_DSC(n)                                                                                     \
    {                                                                                                   \
        .header = {                                                                                     \
            .magic = LV_IMAGE_HEADER_MAGIC,                                                             \
            .cf = LV_COLOR_FORMAT_RGB565A8,                                                             \
            .flags = 0,                                                                                 \
            .w = FLEX_APP_ICON_SIZE,                                                                    \
            .h = FLEX_APP_ICON_SIZE,                                                                    \
            .stride = FLEX_APP_ICON_SIZE * 2,                                                           \
        },                                                                                              \
        .data_size = ICON_BYTES, .data = _binary_app_##n##_bin_start,                                   \
    }

/* Mismo orden que flex_app_t. */
static const lv_image_dsc_t kIcons[FLEX_APP_COUNT_] = {
    ICON_DSC(reloj),   ICON_DSC(calend),  ICON_DSC(calc),    ICON_DSC(clima),     ICON_DSC(musica),
    ICON_DSC(notas),   ICON_DSC(flexphone), ICON_DSC(nav),   ICON_DSC(brujula),   ICON_DSC(devcare),
    ICON_DSC(almacen), ICON_DSC(paint),   ICON_DSC(flexstore), ICON_DSC(ajustes),   ICON_DSC(sensores),
    ICON_DSC(galeria), ICON_DSC(bienestar),
};

/* Nombres de APP[][0] (FlexOS_Ultra_Session.h:345), en el orden de flex_app_t. */
static const char *const kNames[FLEX_APP_COUNT_] = {
    "Reloj",      "Calendario", "Calculadora",    "Clima",         "Música", "Notas",      "Flex Phone",
    "Navegador",  "Flex Compass", "Device Care",  "Almacenamiento", "Paint", "Flex Store", "Ajustes",
    "Sensores",   "Galería",    "Bienestar",
};

const lv_image_dsc_t *flex_app_icon(flex_app_t app)
{
    return ((unsigned)app < FLEX_APP_COUNT_) ? &kIcons[app] : NULL;
}

const char *flex_app_name(flex_app_t app)
{
    return ((unsigned)app < FLEX_APP_COUNT_) ? kNames[app] : "";
}
