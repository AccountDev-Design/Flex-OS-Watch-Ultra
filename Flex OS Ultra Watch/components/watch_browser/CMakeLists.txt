idf_component_register(
    SRCS "br_logic.c" "br_net.c" "br_lists.c" "br_backend.c" "watch_browser.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tcp_transport esp-tls mbedtls esp_http_client nvs_flash esp_timer heap esp_hw_support
                  watch_net watch_files watch_phone watch_diag
)
