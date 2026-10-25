idf_component_register(
    SRCS "watch_ui.c" "ui_nav.c" "ui_gesture.c" "ui_face.c" "ui_qp.c" "ui_bright.c" "ui_settings.c" "ui_display_page.c" "ui_wallpaper.c"
         "ui_battery_page.c" "ui_diag.c" "ui_touchtest.c" "ui_about.c" "ui_datetime.c"
         "ui_clock_logic.c" "ui_clock_core.c" "ui_apps.c" "ui_grid_logic.c" "ui_reloj.c" "ui_calc.c" "ui_calendar.c" "apps_logic.c" "ui_devcare.c" "ui_compass.c" "ui_wifi.c" "ui_keyboard.c" "ui_notes.c" "ui_files.c" "ui_paint.c" "ui_flexphone.c" "ui_notifs.c" "ui_music.c" "ui_weather.c" "ui_browser.c" "ui_ota.c" "ui_stopwatch.c" "ui_timer.c" "ui_alarms.c" "ui_screen_logic.c" "ui_screen.c" "ui_sensors.c" "ui_storage.c" "ui_logic.c" "ui_system.c" "ui_charge_anim.c" "ui_banner.c"
         "ui_psave.c" "ui_aod.c" "ui_face_editor.c" "ui_gallery.c"
         "ui_wb_logic.c" "ui_wb_icons.c" "ui_wb_rings.c" "ui_wb_home.c" "ui_wb_workout.c" "ui_wb_history.c" "ui_wb_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_diag watch_power watch_theme watch_time watch_settings watch_imu watch_storage watch_net watch_phone watch_files watch_weather watch_browser watch_ota watch_gallery watch_activity
    PRIV_REQUIRES esp_timer heap
)
