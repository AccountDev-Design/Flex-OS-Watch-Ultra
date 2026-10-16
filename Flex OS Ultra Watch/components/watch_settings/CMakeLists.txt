idf_component_register(
    SRCS "ws_logic.c" "watch_settings.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash esp_timer
)
