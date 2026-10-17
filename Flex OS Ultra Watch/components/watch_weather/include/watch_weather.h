/*
 * watch_weather — clima de Open-Meteo en el reloj (F9.8): el motor de
 * FlexOS_Weather (wx_core + wx_logic) con la red de ESP-IDF.
 *
 *  - Una sola fuente de verdad: la app y la esfera leen las mismas
 *    instantáneas. Nadie más habla con Open-Meteo.
 *  - Toda la red la hace una tarea propia (esp_http_client sobre TLS
 *    VERIFICADO con el paquete de CA de ESP-IDF; nunca sin verificar). La
 *    interfaz solo copia instantáneas bajo un cerrojo: nunca ve un
 *    pronóstico a medio escribir y nunca espera a la red.
 *  - Solo usa el Wi-Fi si ya está conectado (watch_net): no lo enciende. Sin
 *    Internet el BLE con el teléfono sigue igual.
 *  - Ubicaciones y última descarga en NVS (espacio "flexwx", como la
 *    referencia): tras reiniciar se ve el último pronóstico marcado como
 *    caché, con su edad real si el reloj está en hora.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "wx_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t status;   /* WXS_* */
    uint8_t err;      /* WXE_* del último intento */
    bool have;        /* hay datos (watch_weather_data los copia) */
    uint32_t gen;     /* cambia con lo que se enseña */
    int32_t age_s;    /* -1 = no se sabe */
    int32_t now_local;/* unix + desfase de la ubicación; 0 = no se sabe */
    int64_t now_utc;  /* 0 = no se sabe */
    uint8_t nloc;
    int8_t sel;
    wx_loc_t locs[WX_LOCS];
    uint8_t qst;      /* WXQ_* */
    uint32_t qgen;
    uint8_t nres;
    wx_loc_t res[WX_RESULTS];
} watch_weather_status_t;

esp_err_t watch_weather_start(void);
void watch_weather_get(watch_weather_status_t *out);
/** Copia el pronóstico publicado. false si no hay ninguno. */
bool watch_weather_data(wx_data_t *out);
void watch_weather_refresh(bool force);
void watch_weather_select(uint8_t i);
bool watch_weather_add(const wx_loc_t *l);
void watch_weather_remove(uint8_t i);
void watch_weather_search(const char *q);
void watch_weather_search_clear(void);

#ifdef __cplusplus
}
#endif
