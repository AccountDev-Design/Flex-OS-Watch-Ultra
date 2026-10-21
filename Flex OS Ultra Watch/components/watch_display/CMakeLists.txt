idf_component_register(
    SRCS "watch_display.c" "lv_mem_watch.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd
    PRIV_REQUIRES esp_hw_support heap
)
