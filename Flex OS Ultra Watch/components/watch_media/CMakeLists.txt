idf_component_register(
    SRCS "wm_sd_logic.c" "wm_fsclass.c" "wm_path.c" "wm_sd.c" "wm_vol.c" "wm_text.c" "wm_store.c" "wm_web_logic.c" "wm_web.c"
         "wm_video_logic.c" "wm_video.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs flexmedia
    PRIV_REQUIRES sdmmc esp_driver_sdmmc esp_driver_gpio esp_timer lwip watch_net watch_settings watch_storage watch_files
                  watch_time watch_diag
)
