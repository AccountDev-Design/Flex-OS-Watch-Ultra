/*
 * watch_time_types — tipos del servicio de hora sin dependencias de ESP-IDF,
 * para que la lógica de la interfaz que los usa se pueda probar en el PC.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rtc_pcf85063.h"
#include "wt_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCH_TIME_STARTING = 0, /**< todavía no se ha leído el RTC */
    WATCH_TIME_OK,           /**< hora válida */
    WATCH_TIME_NOT_SET,      /**< el RTC responde pero su hora no es fiable */
    WATCH_TIME_NO_RTC,       /**< el RTC no responde en el bus */
} watch_time_state_t;

typedef struct {
    watch_time_state_t state;
    rtc_read_t last_read;                 /**< detalle de la última lectura */
    uint8_t regs[PCF85063_READ_LEN];      /**< registros 00h..0Ah de la última lectura */
    bool rtc_found;
    uint32_t reads;
    uint32_t errors;                      /**< transacciones I2C fallidas */
    int64_t synced_us;                    /**< esp_timer de la última lectura válida */
} watch_time_info_t;

#ifdef __cplusplus
}
#endif
