idf_component_register(
    SRCS "stg_logic.c" "stg_usage.c" "watch_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag
    PRIV_REQUIRES littlefs esp_partition spi_flash vfs
)
