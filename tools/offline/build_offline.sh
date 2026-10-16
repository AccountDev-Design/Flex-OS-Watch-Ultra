#!/usr/bin/env bash
# Compila Flex OS Watch Ultra SIN acceso al Registro de Componentes de
# Espressif (entornos cuyo proxy bloquea components.espressif.com).
#
# NO es la forma normal de compilar: en un PC con acceso al registro usa
# `idf.py build` dentro de "Flex OS Ultra Watch/" (docs/B1-ARRANQUE.md). Este
# script copia ese proyecto a una carpeta de trabajo y sustituye las
# dependencias del registro por copias
# locales de las MISMAS versiones, tomadas de sus repositorios de GitHub
# (override_path). El manifiesto real del repositorio no se modifica.
#
# Uso:  FLEX_DEPS_DIR=/ruta/deps tools/offline/build_offline.sh [argumentos de idf.py]
# FLEX_DEPS_DIR debe contener una carpeta por componente (ver la lista DEPS).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PROJECT="$ROOT/Flex OS Ultra Watch"
[ -f "$PROJECT/CMakeLists.txt" ] || { echo "no se encuentra el proyecto en $PROJECT" >&2; exit 2; }
DEPS=${FLEX_DEPS_DIR:?"define FLEX_DEPS_DIR"}
# La carpeta de trabajo conserva el nombre del proyecto (con espacios) para
# compilar en las mismas condiciones que en el repositorio.
WORK=${FLEX_OFFLINE_WORK:-"$ROOT/../flex-watch-offline-build/Flex OS Ultra Watch"}

# nombre_en_el_registro:carpeta_local
DEPS_MAP=(
    "waveshare/esp32_s3_touch_amoled_2_06:esp32_s3_touch_amoled_2_06"
    "waveshare/esp_lcd_sh8601:esp_lcd_sh8601"
    "lvgl/lvgl:lvgl"
    "espressif/esp_lvgl_port:esp_lvgl_port"
    "espressif/esp_lcd_touch:esp_lcd_touch"
    "espressif/esp_lcd_touch_ft5x06:esp_lcd_touch_ft5x06"
    "espressif/esp_lcd_panel_io_additions:esp_lcd_panel_io_additions"
    "espressif/esp_io_expander:esp_io_expander"
    "espressif/cmake_utilities:cmake_utilities"
    "espressif/esp_codec_dev:esp_codec_dev"
    "joltwallet/littlefs:littlefs"
)

# Copia limpia de las fuentes conservando lo generado en la carpeta de trabajo
# (build/, sdkconfig, dependencies.lock) para no recompilar todo cada vez.
mkdir -p "$WORK"
find "$WORK" -mindepth 1 -maxdepth 1 \
    ! -name build ! -name sdkconfig ! -name sdkconfig.old ! -name dependencies.lock \
    ! -name managed_components -exec rm -rf {} +
(cd "$PROJECT" && tar --exclude=./build --exclude=./managed_components --exclude=./.git \
    --exclude=./dependencies.lock --exclude=./sdkconfig --exclude=./sdkconfig.old \
    -cf - .) | tar -xf - -C "$WORK"

{
    echo "# GENERADO por tools/offline/build_offline.sh: no editar ni subir."
    echo "dependencies:"
    echo "  idf: \">=5.5.5,<5.6\""
    for entry in "${DEPS_MAP[@]}"; do
        name=${entry%%:*}
        dir=${entry#*:}
        [ -d "$DEPS/$dir" ] || { echo "falta $DEPS/$dir" >&2; exit 2; }
        echo "  $name:"
        echo "    override_path: \"$DEPS/$dir\""
    done
} > "$WORK/main/idf_component.yml"

cd "$WORK"
idf.py "${@:-build}"
