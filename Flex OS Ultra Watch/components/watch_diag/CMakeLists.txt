idf_component_register(
    SRCS "flash_probe_logic.c" "mem_logic.c" "watch_diag.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES spi_flash esp_partition heap
)
