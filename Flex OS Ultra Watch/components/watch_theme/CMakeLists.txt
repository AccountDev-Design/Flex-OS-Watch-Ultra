idf_component_register(
    SRCS "watch_theme.c" "flex_wallpaper.c" "flex_glass_math.c" "flex_batt_style.c"
         "flex_icons.c" "flex_battery_icon.c"
         "fonts/flex_outfit_18.c" "fonts/flex_outfit_22.c" "fonts/flex_outfit_28.c"
         "fonts/flex_outfit_40.c" "fonts/flex_outfit_clock_120.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES heap log
)
