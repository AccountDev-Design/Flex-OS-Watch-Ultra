#!/usr/bin/env bash
# Comprobaciones estáticas de la Galería del reloj (prueba 10 de docs/GALERIA.md):
#  1. no hay un servidor propio: ni esp_http_server ni sockets que escuchen;
#  2. el único servicio remoto escrito en el código es el de Flex Account /
#     Flex Cloud de Flex OS Ultra (FlexOS_Account.cpp); el P4 es la dirección
#     que la persona escribe. Ni otro proveedor de almacenamiento ni otra nube;
#  3. ninguna credencial en el código (la del reloj se genera al vincular y
#     vive en la NVS) ni en el registro.
# Con REF_DIR (Flex-OS-Ecosistema) se comprueba además que el servicio es el
# mismo que usa Flex OS Ultra.
# Uso: tests/host/gallery_checks.sh   (desde cualquier carpeta)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW="$HERE/../../Flex OS Ultra Watch/components"
SRC=("$FW/watch_gallery" "$FW/watch_ui/ui_gallery.c")
fail=0
bad() { echo "  FALLO: $*"; fail=1; }

# 1. servidor
if grep -rnP '(httpd_start|httpd_register_uri|esp_http_server|(?<![A-Za-z0-9_])(listen|accept|bind)\s*\()' "${SRC[@]}"; then
    bad "la Galería abre un servidor o un socket que escucha"
fi

# 2. servicios remotos
hosts=$(grep -rhoE '"[a-z0-9-]+(\.[a-z0-9-]+)+\.(com|site|net|io|org|app|dev|cloud)\b' "${SRC[@]}" | tr -d '"' | sort -u)
for h in $hosts; do
    [ "$h" = "flex-developer-studio.ralvarezsantos980.chatgpt.site" ] || bad "servicio desconocido en el código: $h"
done
if grep -rniE 'amazonaws|firebase|dropbox|googleapis|onedrive|backblaze|cloudinary|supabase' "${SRC[@]}"; then
    bad "otro proveedor de almacenamiento"
fi
if [ -n "${REF_DIR:-}" ]; then
    grep -q 'flex-developer-studio.ralvarezsantos980.chatgpt.site/api/cloud/me' "$REF_DIR/FlexOS_Ultra/FlexOS_Account.cpp" ||
        bad "el servicio no es el de FlexOS_Account.cpp de la referencia"
fi

# 3. credenciales. Solo valen dos constantes PÚBLICAS: el alfabeto base64url y
#    el keyId de la clave ES256 con la que Flex Account firma (la pública, la
#    misma de FlexOS_Account.cpp; no da acceso a nada).
KEY_ID=69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69
if grep -rnE '"[A-Za-z0-9_-]{40,}"' "${SRC[@]}" | grep -v "chatgpt.site\|ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_\|$KEY_ID"; then
    bad "una cadena con forma de credencial en el código"
fi
if [ -n "${REF_DIR:-}" ]; then
    grep -q "$KEY_ID" "$REF_DIR/FlexOS_Ultra/FlexOS_Account.cpp" || bad "el keyId de Flex Account no es el de la referencia"
fi
if grep -rnE 'ESP_LOG[EWIDV]?\(.*(bearer|token|cookie|fxs)' "${SRC[@]}"; then
    bad "el registro podría escribir la credencial o la sesión"
fi

if [ $fail -eq 0 ]; then
    echo "Galería: sin servidor propio, solo el servicio de Flex OS Ultra, sin credenciales en el código: OK"
fi
exit $fail
