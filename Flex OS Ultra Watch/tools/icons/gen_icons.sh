#!/usr/bin/env bash
# Regenera assets/icons/app_*.bin (iconos de las apps, RGB565A8 de 104x104)
# a partir del dibujo vectorial de Flex OS Ultra (gen_app_icons.c).
# Uso, desde cualquier carpeta:  "Flex OS Ultra Watch/tools/icons/gen_icons.sh"
# Requiere un compilador de C del PC (gcc o clang). No hace falta ESP-IDF.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../assets/icons"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
${CC:-cc} -O2 -std=c11 -Wall -Wextra -Werror -o "$TMP/gen" "$HERE/gen_app_icons.c" -lm
mkdir -p "$OUT"
"$TMP/gen" "$TMP" 104
cp "$TMP"/app_*.bin "$OUT/"
echo "iconos escritos en $OUT"
