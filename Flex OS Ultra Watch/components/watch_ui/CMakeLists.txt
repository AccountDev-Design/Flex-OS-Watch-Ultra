idf_component_register(
    SRCS "watch_home.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag
    PRIV_REQUIRES watch_theme watch_power
)
