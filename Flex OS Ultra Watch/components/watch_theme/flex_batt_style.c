/*
 * flex_batt_style — ver include/flex_batt_style.h
 */
#include "flex_batt_style.h"
#include "flex_tokens.h"

uint32_t flex_batt_fill_rgb(flex_batt_kind_t kind)
{
    switch (kind) {
    case FLEX_BATT_LOW:      return FLEX_C_WARN;
    case FLEX_BATT_CRITICAL: return FLEX_C_ERR;
    case FLEX_BATT_CHARGING:
    case FLEX_BATT_FULL:     return FLEX_C_OK;
    case FLEX_BATT_EXTERNAL: return FLEX_C_ACC_SOFT;
    case FLEX_BATT_NORMAL:   return FLEX_C_TXT;
    default:                 return FLEX_C_MUTE;
    }
}

uint32_t flex_batt_outline_rgb(flex_batt_kind_t kind)
{
    switch (kind) {
    case FLEX_BATT_UNKNOWN:
    case FLEX_BATT_ABSENT:   return FLEX_C_MUTE;
    case FLEX_BATT_CRITICAL: return FLEX_C_ERR;
    default:                 return FLEX_C_TXT;
    }
}

flex_batt_glyph_t flex_batt_glyph(flex_batt_kind_t kind)
{
    switch (kind) {
    case FLEX_BATT_CHARGING: return FLEX_BATT_GLYPH_BOLT;
    case FLEX_BATT_EXTERNAL: return FLEX_BATT_GLYPH_BOLT_DIM;
    case FLEX_BATT_FULL:     return FLEX_BATT_GLYPH_CHECK;
    case FLEX_BATT_ABSENT:   return FLEX_BATT_GLYPH_CROSS;
    default:                 return FLEX_BATT_GLYPH_NONE;
    }
}

int flex_batt_fill_width(int inner_w, flex_batt_kind_t kind, int percent)
{
    if (inner_w <= 0 || percent < 0 || kind == FLEX_BATT_UNKNOWN || kind == FLEX_BATT_ABSENT) {
        return 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    int w = (inner_w * percent + 50) / 100;
    /* Con carga por encima de 0 % siempre queda un trazo visible. */
    if (percent > 0 && w < 2) {
        w = inner_w < 2 ? inner_w : 2;
    }
    return w;
}
