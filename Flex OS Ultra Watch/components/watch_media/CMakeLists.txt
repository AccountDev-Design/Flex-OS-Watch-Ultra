idf_component_register(
    SRCS "wm_sd_logic.c" "wm_fsclass.c" "wm_path.c" "wm_sd.c" "wm_vol.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs
    PRIV_REQUIRES sdmmc esp_driver_sdmmc esp_driver_gpio esp_timer watch_settings watch_storage
)
