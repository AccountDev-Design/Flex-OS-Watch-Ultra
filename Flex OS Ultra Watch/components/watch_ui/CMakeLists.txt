idf_component_register(
    SRCS "watch_ui.c" "ui_nav.c" "ui_gesture.c" "ui_face.c" "ui_diag.c" "ui_logic.c" "ui_system.c" "ui_charge_anim.c" "ui_banner.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag watch_power watch_theme watch_time
)
