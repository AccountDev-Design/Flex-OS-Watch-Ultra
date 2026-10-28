# Biblioteca de medios, miniaturas, QR y Flex Web Server de Flex OS Ultra
# (Flex-OS-Ecosistema @ 5dbc949), portados al reloj (docs/MULTIMEDIA.md).
# C++ portable: lo mismo se compila en tests/host y en tools/sim.
# El JPEG lo decodifica TJpgDec (flex_jpeg_tjpgd.c): el de la ROM en el reloj.
idf_component_register(
    SRCS "FlexOS_MediaLib.cpp" "FlexOS_Media.cpp" "FlexOS_MediaStore.cpp" "FlexOS_MediaThumb.cpp"
         "FlexOS_JPEGEnc.cpp" "FlexOS_QR.cpp" "FlexOS_HttpShare.cpp" "FlexOS_MediaWeb.cpp"
         "flex_jpeg_tjpgd.c" "flex_mp3.c" "flex_id3.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_rom
)
target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/flexmedia_rom.ld")
# El código de referencia usa extensiones de C++ que GCC acepta con -std=gnu++17
# (lo mismo que su Makefile de pruebas); sin excepciones ni RTTI (ESP-IDF).
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
