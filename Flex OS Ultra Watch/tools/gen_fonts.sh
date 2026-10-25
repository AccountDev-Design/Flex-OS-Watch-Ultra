#!/usr/bin/env bash
# Regenera las fuentes LVGL de Flex OS Watch Ultra a partir de assets/fonts/.
# Requiere Node.js; usa lv_font_conv 1.5.3 fijado.
#
# Orígenes (SIL OFL 1.1, ver los OFL*.txt de assets/fonts y THIRD_PARTY_NOTICES.md):
#  - Outfit (Regular, ExtraLight, Medium, SemiBold): https://github.com/Outfitio/Outfit-Fonts,
#    commit 9027738, sin cambios. Es la letra de Flex OS Ultra.
#  - League Spartan Bold: instancia wght=700 de LeagueSpartan[wght].ttf (google/fonts, ofl/leaguespartan)
#    con `fonttools varLib.instancer`. Es la letra de los diseños del usuario (Reloj, Almacenamiento,
#    panel rápido, alarmas).
#  - FlexCondensed-Medium: Barlow Condensed Medium (google/fonts, ofl/barlowcondensed) estrechada al
#    80 % en horizontal con tools/fonts/derive_condensed.py y con otro nombre (la OFL lo exige para una
#    versión modificada; Barlow no declara nombres reservados). Dígitos de las esferas fotográficas.
#
# Tamaños pensados para el AMOLED de 2,06" (410x502, ~315 ppi): el doble de
# densidad que el panel del P4, así que cada tamaño es mayor en píxeles que su
# equivalente en Flex OS Ultra para conservar el tamaño físico legible. Los de
# las pantallas rediseñadas salen de medir las imágenes de referencia escaladas
# a 410x502 (docs/REDISENO-2026-10.md).
set -euo pipefail
cd "$(dirname "$0")/.."
F=assets/fonts
OUT=components/watch_theme/fonts
CONV=(npx --yes lv_font_conv@1.5.3)
# ASCII + Latin-1 (acentos, ñ, ¿, ¡, °, ·) + guiones y puntos suspensivos.
TXT=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2026
# Dígitos grandes: solo ' ', '-', '0'-'9' y ':'.
DIGITS=0x20,0x2D,0x30-0x3A
# Fechas y rótulos en mayúsculas de las esferas: ' '..'Z', ÁÉÍÓÚÑÜ, ° y %.
CAPS=0x20-0x5A,0xB0,0xC1,0xC9,0xCD,0xD1,0xD3,0xDA,0xDC

font() { # nombre ttf tamaño rango
    "${CONV[@]}" --bpp 4 --size "$3" --no-compress --font "$F/$2" -r "$4" \
        --format lvgl --lv-include lvgl.h --lv-font-name "$1" -o "$OUT/$1.c"
}

for SIZE in 18 22 28 40; do
    font flex_outfit_${SIZE} Outfit-Regular.ttf "$SIZE" "$TXT"
done
font flex_outfit_clock_120 Outfit-Regular.ttf 120 0x2D,0x30-0x3A

# Pantallas rediseñadas (League Spartan Bold)
for SIZE in 16 20 22 24 32 40; do
    font flex_spartan_${SIZE} LeagueSpartan-Bold.ttf "$SIZE" "$TXT"
done
font flex_spartan_clock_188 LeagueSpartan-Bold.ttf 188 "$DIGITS"   # Reloj > Hora
font flex_outfit_15 Outfit-Regular.ttf 15 "$TXT"                   # rótulos pequeños (temporizador)

# Esferas
font flex_cond_156 FlexCondensed-Medium.ttf 156 "$DIGITS"          # Taj Mahal
font flex_cond_196 FlexCondensed-Medium.ttf 196 "$DIGITS"          # Montaña
font flex_cond_248 FlexCondensed-Medium.ttf 248 "$DIGITS"          # Flores (horas y minutos apilados)
font flex_outfit_xl_105 Outfit-ExtraLight.ttf 105 "$DIGITS"        # Gato y luna
font flex_outfit_sb_101 Outfit-SemiBold.ttf 101 "$DIGITS"          # Tierra
font flex_outfit_med_28 Outfit-Medium.ttf 28 "$CAPS"               # rodillos del temporizador
font flex_outfit_med_34 Outfit-Medium.ttf 34 "$CAPS"               # fechas, clima, cronómetro
font flex_outfit_med_48 Outfit-Medium.ttf 48 "$CAPS"               # fecha de la Tierra
