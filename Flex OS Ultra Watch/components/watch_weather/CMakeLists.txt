idf_component_register(
    SRCS "wx_logic.c" "wx_core.c" "watch_weather.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_timer heap watch_net watch_time
)
