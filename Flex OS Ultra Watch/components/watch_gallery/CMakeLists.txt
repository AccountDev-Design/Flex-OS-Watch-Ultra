idf_component_register(
    SRCS "gal_json.c" "gal_http.c" "gal_core.c" "gal_acct.c" "gal_jpeg.c" "gal_engine.c" "gal_net.c" "gal_local.c"
         "watch_gallery.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tcp_transport esp-tls mbedtls nvs_flash esp_timer heap esp_hw_support esp_app_format esp_rom
                  watch_net watch_storage watch_files watch_ota
)
# TJpgDec de la ROM con escala (gal_jpeg.c): ver gal_rom.ld
target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/gal_rom.ld")
