#!/usr/bin/env bash
# Regenera las fuentes LVGL de Flex OS Watch Ultra a partir de Outfit-Regular.ttf.
# Fuente: https://github.com/Outfitio/Outfit-Fonts (commit 9027738, SIL OFL 1.1,
# ver assets/fonts/OFL.txt). Requiere Node.js; usa lv_font_conv 1.5.3 fijado.
#
# Tamaños pensados para el AMOLED de 2,06" (410x502, ~315 ppi): el doble de
# densidad que el panel del P4, así que cada tamaño es mayor en píxeles que su
# equivalente en Flex OS Ultra para conservar el tamaño físico legible.
set -euo pipefail
cd "$(dirname "$0")/.."
TTF=assets/fonts/Outfit-Regular.ttf
OUT=components/watch_theme/fonts
CONV=(npx --yes lv_font_conv@1.5.3)
# ASCII + Latin-1 (acentos, ñ, ¿, ¡, °, ·) + guiones y puntos suspensivos.
TXT=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2026
for SIZE in 18 22 28 40; do
    NAME=flex_outfit_${SIZE}
    "${CONV[@]}" --bpp 4 --size "$SIZE" --no-compress --font "$TTF" -r "$TXT" \
        --format lvgl --lv-include lvgl.h --lv-font-name "$NAME" -o "$OUT/$NAME.c"
done
# Dígitos grandes de la esfera: solo '-', '0'-'9' y ':'.
"${CONV[@]}" --bpp 4 --size 120 --no-compress --font "$TTF" -r 0x2D,0x30-0x3A \
    --format lvgl --lv-include lvgl.h --lv-font-name flex_outfit_clock_120 -o "$OUT/flex_outfit_clock_120.c"
