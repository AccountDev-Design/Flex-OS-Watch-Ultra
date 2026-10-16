idf_component_register(
    SRCS "qmi8658.c" "watch_imu.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_timer
)
