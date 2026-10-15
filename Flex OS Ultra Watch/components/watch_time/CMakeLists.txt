idf_component_register(
    SRCS "wt_time.c" "rtc_pcf85063.c" "watch_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_timer
)
