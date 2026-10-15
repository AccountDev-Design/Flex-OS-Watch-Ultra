idf_component_register(
    SRCS "battery_model.c" "battery_alerts.c" "power_sched.c" "watch_power.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_timer
)
