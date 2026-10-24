#!/usr/bin/env bash
# Galería del reloj contra los servidores REALES del ecosistema (ver gal_e2e.c):
#   - Flex Web Server de Flex OS Ultra: FlexOS_MediaWeb.cpp compilado en el PC
#     (tests/host/flexweb_host.cpp de Flex-OS-Ecosistema), sin tocar ese repositorio;
#   - Flex Cloud: el servidor Node de cloud/ de Flex Developer Studio en modo
#     desarrollo; las fotos de la "web" las sube gal_web_seed.py por su API.
#
# Uso (desde la raíz del repositorio):
#   REF_DIR=/ruta/Flex-OS-Ecosistema CLOUD_DIR=/ruta/flex-developer-studio/cloud \
#   LVGL_DIR=/ruta/lvgl-9.5.0 IDF_PATH=/ruta/esp-idf make -C tests/host gallery-e2e
# Requiere g++ y libcrypto (flexweb_host), Node >= 22.13, Python 3 con Pillow.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
: "${REF_DIR:?define REF_DIR (Flex-OS-Ecosistema, rama claude/flexos-ota-system-blq9g9-2csacx)}"
: "${CLOUD_DIR:?define CLOUD_DIR (cloud/ de Flex Developer Studio)}"
OUT="$HERE/build/e2e"
mkdir -p "$OUT"

pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT

# 1. El servidor del P4, compilado FUERA de su repositorio, y la prueba de que no se tocó.
before=$(git -C "$REF_DIR" status --porcelain)
make -s -C "$REF_DIR/tests/host" BUILD="$OUT/ref" "$OUT/ref/flexweb_host"
after=$(git -C "$REF_DIR" status --porcelain)
[ "$before" == "$after" ] || { echo "flexweb_host modificó el repositorio de referencia" >&2; exit 1; }
echo "flexweb_host de $(git -C "$REF_DIR" rev-parse --short HEAD) ($(git -C "$REF_DIR" rev-parse --abbrev-ref HEAD))"

# 2. Fotos grandes (no se versionan)
python3 "$ROOT/tools/sim/gen_gal_fixtures.py" --big "$OUT/fix" > /dev/null

# 3. Flex OS Ultra: su Flex Web Server con un catálogo vacío y --storage (el P4 de
#    verdad registra siempre Flex Storage y la papelera, FlexOS_Ultra_WebServer.h:385-388);
#    "jobs" hace las miniaturas pendientes, como la tarea de fondo del P4.
P4DIR=$(mktemp -d)
rm -f "$OUT/p4.json"
( while sleep 0.3; do echo jobs; done ) | "$OUT/ref/flexweb_host" "$P4DIR" --storage > "$OUT/p4.json" 2> "$OUT/p4.log" &
pids+=($!)
for _ in $(seq 50); do [ -s "$OUT/p4.json" ] && break; sleep 0.1; done
P4PORT=$(head -1 "$OUT/p4.json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["port"])')
P4CODE=$(head -1 "$OUT/p4.json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')

# 4. Flex Cloud en modo desarrollo, en un puerto libre y con datos temporales
CPORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
CDATA=$(mktemp -d)
( cd "$CLOUD_DIR" && FLEX_CLOUD_PORT=$CPORT FLEX_CLOUD_DATA_DIR="$CDATA" FLEX_ACCOUNT_MODE=dev FLEX_CLOUD_DEV_LOGIN=1 \
  FLEX_CLOUD_SECRET=e2e-de-la-galeria-del-reloj-0123456789abcdef \
  node --disable-warning=ExperimentalWarning src/server.js > "$OUT/cloud.log" 2>&1 ) &
pids+=($!)
for _ in $(seq 100); do
  curl -fsS "http://127.0.0.1:$CPORT/api/cloud/health" > /dev/null 2>&1 && break; sleep 0.1
done
echo "Flex Cloud $(git -C "$CLOUD_DIR" rev-parse --short HEAD) en $CPORT: $(curl -fsS "http://127.0.0.1:$CPORT/api/cloud/health")"

# 5. La "web": cuenta, credencial del reloj registrada y una foto con su miniatura
read -r TOKEN WEBID < <(python3 "$HERE/gal_web_seed.py" "$CPORT" "$OUT/fix/gal_big_4000x3000.jpg")

# 6. El motor del reloj contra los dos
WROOT=$(mktemp -d)
"$HERE/build/gal_e2e" "$WROOT" "$OUT/fix" "$P4PORT" "$P4CODE" "$CPORT" "$TOKEN" "$WEBID"
rm -rf "$WROOT" "$P4DIR" "$CDATA"
