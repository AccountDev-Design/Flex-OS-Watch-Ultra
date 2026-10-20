idf_component_register(
    SRCS "ota_core.c" "ota_sig.c" "watch_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES app_update esp_app_format esp_http_client esp-tls mbedtls nvs_flash esp_timer heap watch_net watch_power
)
