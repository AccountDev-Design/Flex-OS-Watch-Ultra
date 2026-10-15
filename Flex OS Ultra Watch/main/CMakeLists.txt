idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES watch_display watch_touch watch_power watch_diag watch_ui
                  esp_app_format
)
