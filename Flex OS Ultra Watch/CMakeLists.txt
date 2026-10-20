# Flex OS Watch Ultra — Waveshare ESP32-S3-Touch-AMOLED-2.06
# ESP-IDF 5.5.5 + LVGL 9.5.0 (ver docs/B1-ARRANQUE.md)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Opciones de LVGL que el código necesita sea cual sea el sdkconfig existente
# (sdkconfig.defaults solo cuenta al generar un sdkconfig nuevo):
#  - LV_USE_SNAPSHOT=1: vistas previas reales del selector de esferas;
#  - LV_USE_PERF_MONITOR=0: nunca el medidor de FPS/CPU en la interfaz
#    normal. Solo para medir (pruebas T9/T42), y a propósito:
#      idf.py -DFLEX_PERF_MONITOR=1 build   (con CONFIG_LV_USE_PERF_MONITOR=y)
# lv_conf_internal.h respeta una definición previa (#ifndef).
idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_SNAPSHOT=1" APPEND)
if(FLEX_PERF_MONITOR)
    idf_build_set_property(COMPILE_DEFINITIONS "FLEX_PERF_MONITOR=1" APPEND)
else()
    idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_PERF_MONITOR=0" APPEND)
endif()
project(flex_os_watch_ultra)
