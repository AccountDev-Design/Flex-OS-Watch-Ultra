idf_component_register(
    SRCS "flash_probe_logic.c" "watch_diag.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES spi_flash esp_partition
)
