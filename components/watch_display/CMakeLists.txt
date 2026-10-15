idf_component_register(
    SRCS "watch_display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd
    PRIV_REQUIRES esp_hw_support
)
