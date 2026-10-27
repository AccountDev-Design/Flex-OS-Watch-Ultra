# Flex OS Watch Ultra — Waveshare ESP32-S3-Touch-AMOLED-2.06
# ESP-IDF 5.5.5 + LVGL 9.5.0 (ver docs/B1-ARRANQUE.md)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Opciones de LVGL que el código necesita sea cual sea el sdkconfig existente
# (sdkconfig.defaults solo cuenta al generar un sdkconfig nuevo):
#  - LV_USE_SNAPSHOT=1: vistas previas reales del selector de esferas;
#  - LV_USE_ARCLABEL=1: rótulo curvo "Batería" de la esfera Ultra (rediseño
#    2026-10; activo por defecto en LVGL 9.5, se fija por si un sdkconfig
#    antiguo lo apagó);
#  - LV_USE_PERF_MONITOR=0: nunca el medidor de FPS/CPU en la interfaz
#    normal. Solo para medir (pruebas T9/T42), y a propósito:
#      idf.py -DFLEX_PERF_MONITOR=1 build   (con CONFIG_LV_USE_PERF_MONITOR=y)
# lv_conf_internal.h respeta una definición previa (#ifndef).
idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_SNAPSHOT=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_ARCLABEL=1" APPEND)
# Asignador de LVGL propio (components/watch_display/lv_mem_watch.c): objetos,
# estilos y estado de las páginas en PSRAM, no en la RAM interna que necesitan
# Wi-Fi, BLE, lwIP y las pilas de las tareas.
idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM" APPEND)
# Medir los marcos de pila con el compilador del chip (un .su por archivo):
#   idf.py -DFLEX_STACK_USAGE=1 build
#   python3 ../tools/stack/check_frames.py build
# (la Galería se reiniciaba por un array de 8 KB en la pila de su tarea).
if(FLEX_STACK_USAGE)
    idf_build_set_property(COMPILE_OPTIONS "-fstack-usage" APPEND)
endif()
if(FLEX_PERF_MONITOR)
    idf_build_set_property(COMPILE_DEFINITIONS "FLEX_PERF_MONITOR=1" APPEND)
else()
    idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_PERF_MONITOR=0" APPEND)
endif()
project(flex_os_watch_ultra)
