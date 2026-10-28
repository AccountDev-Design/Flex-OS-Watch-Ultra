idf_component_register(
    SRCS "wa_logic.c" "wa_hw.c" "wa_player.c" "wa_rec.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c flexmedia watch_media
    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_codec_dev esp_timer minimp3 watch_settings watch_time watch_diag
)
