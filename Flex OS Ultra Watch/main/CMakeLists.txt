idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES watch_display watch_touch watch_power watch_time watch_diag watch_ui watch_settings watch_theme watch_imu watch_storage watch_net watch_phone watch_weather watch_ota watch_activity
                  esp_app_format esp_driver_gpio
)
