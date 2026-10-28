# minimp3 (CC0): decodificador de MP3 de un solo archivo. Ver minimp3.c y
# THIRD_PARTY_NOTICES.md. Las opciones van en la interfaz para que todos los
# que incluyen minimp3.h vean la misma estructura mp3dec_t.
idf_component_register(SRCS "minimp3.c" INCLUDE_DIRS "include")
target_compile_definitions(${COMPONENT_LIB} PUBLIC MINIMP3_EXTERNAL_SCRATCH MINIMP3_ONLY_MP3 MINIMP3_NO_SIMD)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-sign-compare -O2)
