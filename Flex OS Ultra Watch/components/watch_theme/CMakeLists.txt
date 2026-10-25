idf_component_register(
    SRCS "watch_theme.c" "flex_wallpaper.c" "flex_glass_math.c" "flex_batt_style.c"
         "flex_icons.c" "flex_battery_icon.c" "flex_safe.c" "flex_app_icons.c" "flex_device_icons.c"
         "flex_blur.c" "flex_jpeg.c" "flex_thumb.c"
         "fonts/flex_outfit_18.c" "fonts/flex_outfit_22.c" "fonts/flex_outfit_28.c"
         "fonts/flex_outfit_40.c" "fonts/flex_outfit_clock_120.c"
         "fonts/flex_spartan_16.c" "fonts/flex_spartan_20.c" "fonts/flex_spartan_22.c" "fonts/flex_spartan_24.c"
         "fonts/flex_spartan_32.c" "fonts/flex_spartan_40.c" "fonts/flex_spartan_clock_188.c"
         "fonts/flex_outfit_15.c" "fonts/flex_outfit_med_28.c"
         "fonts/flex_cond_156.c" "fonts/flex_cond_196.c" "fonts/flex_cond_248.c"
         "fonts/flex_outfit_xl_105.c" "fonts/flex_outfit_sb_101.c"
         "fonts/flex_outfit_med_34.c" "fonts/flex_outfit_med_48.c"
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
set(_icons reloj calend calc clima musica notas flexphone nav brujula devcare almacen paint flexstore ajustes sensores galeria)
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

# Fondos de las esferas fotográficas (tools/faces/gen_faces.py): JPEG en flash,
# con su final (_end) para conocer el tamaño. Mismo .incbin alineado que los iconos.
set(_face_dir "${CMAKE_CURRENT_LIST_DIR}/../../assets/faces")
set(_faces montana analogica gato tierra flores taj)
set(_fasm "${CMAKE_CURRENT_BINARY_DIR}/flex_face_bg_data.S")
set(_ftxt "/* GENERADO por components/watch_theme/CMakeLists.txt: no editar. */\n")
string(APPEND _ftxt ".section .rodata.flex_face_bg, \"a\"\n")
set(_fdeps "")
foreach(_n IN LISTS _faces)
    set(_f "${_face_dir}/face_${_n}.jpg")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "falta ${_f}: ejecuta tools/faces/gen_faces.py")
    endif()
    list(APPEND _fdeps "${_f}")
    string(APPEND _ftxt ".balign 4\n.global _binary_face_${_n}_jpg_start\n_binary_face_${_n}_jpg_start:\n.incbin \"${_f}\"\n")
    string(APPEND _ftxt ".global _binary_face_${_n}_jpg_end\n_binary_face_${_n}_jpg_end:\n")
endforeach()
file(CONFIGURE OUTPUT "${_fasm}" CONTENT "${_ftxt}" @ONLY)
target_sources(${COMPONENT_LIB} PRIVATE "${_fasm}")
set_property(SOURCE "${_fasm}" APPEND PROPERTY OBJECT_DEPENDS ${_fdeps})
