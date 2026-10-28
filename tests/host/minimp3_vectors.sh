#!/usr/bin/env bash
# minimp3 del reloj frente al original, con los vectores de conformidad ISO
# que trae el propio repositorio de minimp3 (no se copian aquí: 26 MB).
#
#   MINIMP3_REF=/ruta/a/minimp3 tests/host/minimp3_vectors.sh
#
# Para cada vector (*.bit con su *.pcm) que sea de capa III:
#  1. el minimp3 del reloj (área de trabajo en el heap) y el original (en la
#     pila) dan EXACTAMENTE la misma salida (cmp);
#  2. la relación señal/ruido contra el PCM de referencia del ISO, que se
#     informa. Los de capa I y II no dan ninguna trama (el reloj compila
#     MINIMP3_ONLY_MP3) y se saltan.
set -euo pipefail
cd "$(dirname "$0")"
REF=${MINIMP3_REF:?"define MINIMP3_REF con la ruta a un clon de lieff/minimp3"}
OUT=build/minimp3
mkdir -p "$OUT"
FW="../../Flex OS Ultra Watch/components/minimp3/include"
CC=${CC:-cc}
FLAGS="-std=c11 -O2 -g -fsanitize=address,undefined -DMINIMP3_ONLY_MP3 -DMINIMP3_NO_SIMD"
$CC $FLAGS -DMINIMP3_EXTERNAL_SCRATCH -I"$FW" minimp3_check.c -o "$OUT/check_watch" -lm
$CC $FLAGS -I"$REF" minimp3_check.c -o "$OUT/check_orig" -lm

n=0; same=0; skipped=0
for bit in "$REF"/vectors/*.bit "$REF"/vectors/*.mp3; do
    [ -f "$bit" ] || continue
    base=${bit%.*}
    [ -f "$base.pcm" ] || continue
    name=$(basename "$base")
    "$OUT/check_watch" "$bit" "$OUT/w.pcm" > "$OUT/w.txt"
    "$OUT/check_orig" "$bit" "$OUT/o.pcm" > /dev/null
    if grep -q "^0 tramas" "$OUT/w.txt"; then
        skipped=$((skipped + 1))       # capa I o II: el reloj solo compila la III
        continue
    fi
    n=$((n + 1))
    if cmp -s "$OUT/w.pcm" "$OUT/o.pcm"; then
        same=$((same + 1))
    else
        echo "DISTINTO: $name"
    fi
    psnr=$(python3 - "$OUT/w.pcm" "$base.pcm" <<'EOF'
import array, math, sys
a = array.array("h"); b = array.array("h")
a.frombytes(open(sys.argv[1], "rb").read()[: (len(open(sys.argv[1], "rb").read()) // 2) * 2])
raw = open(sys.argv[2], "rb").read(); b.frombytes(raw[: (len(raw) // 2) * 2])
m = min(len(a), len(b))
if m == 0:
    print("0.0"); sys.exit()
mse = sum((a[i] - b[i]) ** 2 for i in range(m)) / m
print("%.1f" % (99.0 if mse == 0 else 10 * math.log10(32768.0 ** 2 / mse)))
EOF
)
    printf "  %-28s %s  PSNR %s dB\n" "$name" "$(cat "$OUT/w.txt")" "$psnr"
done
echo "=== minimp3: $n vectores de capa III, $same idénticos al original ($skipped de capa I/II saltados) ==="
[ "$n" -gt 0 ] && [ "$same" -eq "$n" ]
