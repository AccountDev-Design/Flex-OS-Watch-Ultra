idf_component_register(
    SRCS "watch_home.c" "ui_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag watch_power watch_theme
)
