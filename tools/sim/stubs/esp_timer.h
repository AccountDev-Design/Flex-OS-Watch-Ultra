#pragma once
#include <stdint.h>
/* Reloj monótono de la vista previa (sim_main.c): avanza con sim_run(). */
int64_t esp_timer_get_time(void);
