idf_component_register(
    SRCS "watch_home.c" "ui_logic.c" "ui_system.c" "ui_charge_anim.c" "ui_banner.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag watch_power watch_theme
)
