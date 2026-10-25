# Bienestar: núcleo puro (act_*.c, probado en tests/host y en la vista previa)
# y el servicio de ESP-IDF (watch_activity.c).
idf_component_register(
    SRCS "act_catalog.c" "act_steps.c" "act_session.c" "act_metrics.c" "act_engine.c" "act_store.c"
         "watch_activity.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_settings
    PRIV_REQUIRES watch_imu watch_storage watch_time esp_timer heap vfs
)
