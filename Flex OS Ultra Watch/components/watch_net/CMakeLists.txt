idf_component_register(
    SRCS "net_logic.c" "net_text.c" "watch_net.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer watch_time watch_settings watch_diag
)
