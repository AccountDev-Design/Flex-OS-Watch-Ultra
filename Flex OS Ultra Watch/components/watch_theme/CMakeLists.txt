idf_component_register(
    SRCS "watch_theme.c" "flex_wallpaper.c" "flex_glass_math.c" "flex_batt_style.c"
         "flex_icons.c" "flex_battery_icon.c" "flex_safe.c" "flex_app_icons.c" "flex_device_icons.c"
         "fonts/flex_outfit_18.c" "fonts/flex_outfit_22.c" "fonts/flex_outfit_28.c"
         "fonts/flex_outfit_40.c" "fonts/flex_outfit_clock_120.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES heap log esp_timer
)

if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()

# Iconos originales de las apps (tools/icons/gen_icons.sh): RGB565A8 en flash.
# No se usa EMBED_FILES porque su plantilla de ESP-IDF 5.5.5
# (tools/cmake/scripts/data_file_embed_asm.cmake) no alinea los datos, y LVGL
# lee el plano RGB565 en palabras de 16 bits: una dirección impar haría saltar
# LoadStoreAlignment en el Xtensa. Aquí cada icono empieza en un múltiplo de 4.
set(_icon_dir "${CMAKE_CURRENT_LIST_DIR}/../../assets/icons")
set(_icons reloj calend calc clima musica notas flexphone nav brujula devcare almacen paint flexstore ajustes sensores)
set(_asm "${CMAKE_CURRENT_BINARY_DIR}/flex_app_icons_data.S")
set(_txt "/* GENERADO por components/watch_theme/CMakeLists.txt: no editar. */\n")
string(APPEND _txt ".section .rodata.flex_app_icons, \"a\"\n")
set(_deps "")
foreach(_n IN LISTS _icons)
    set(_f "${_icon_dir}/app_${_n}.bin")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "falta ${_f}: ejecuta tools/icons/gen_icons.sh")
    endif()
    list(APPEND _deps "${_f}")
    string(APPEND _txt ".balign 4\n.global _binary_app_${_n}_bin_start\n_binary_app_${_n}_bin_start:\n.incbin \"${_f}\"\n")
endforeach()
file(CONFIGURE OUTPUT "${_asm}" CONTENT "${_txt}" @ONLY)
target_sources(${COMPONENT_LIB} PRIVATE "${_asm}")
set_property(SOURCE "${_asm}" APPEND PROPERTY OBJECT_DEPENDS ${_deps})
