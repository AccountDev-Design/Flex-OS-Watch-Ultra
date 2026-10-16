idf_component_register(
    SRCS "phone_text.c" "phone_core.c" "watch_phone.c" "ble_link.c"
    INCLUDE_DIRS "include"
    REQUIRES flexlink
    PRIV_REQUIRES bt nvs_flash esp_timer esp_hw_support heap
)
