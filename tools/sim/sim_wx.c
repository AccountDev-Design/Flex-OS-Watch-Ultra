/*
 * Clima de la vista previa: la API de watch_weather sobre el motor REAL
 * (wx_core) y el parser REAL (wx_parse_forecast / wx_parse_geo). Lo único
 * simulado es la red: cada descarga tarda SIM_WX_LAT_MS y devuelve un JSON
 * con el formato exacto de Open-Meteo (timeformat=unixtime) generado para la
 * hora del reloj de la vista previa; la búsqueda responde con ciudades fijas.
 *  - sim_wx_online(false): sin Wi-Fi (el motor ni lo intenta; la búsqueda
 *    falla con WXE_NOWIFI);
 *  - sim_wx_fail(err): las descargas fallan con ese error;
 *  - sim_wx_reboot(): "reinicia" guardando ubicaciones y caché como la NVS.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "lvgl.h"
#include "sim.h"
#include "watch_time.h"
#include "watch_weather.h"
#include "wx_core.h"

#define SIM_WX_LAT_MS 600

static wx_core_t C;
static bool s_init, s_online = true, s_running;
static int s_fail;
static uint32_t s_done_at;
static wx_job_t s_job;
static int s_fetches, s_searches;
static char s_json[WX_HTTP_BUF];
static wx_data_t s_parse;

typedef struct {
    const char *name, *admin1, *country; /* country con escapes \u como la API */
    float lat, lon;
    int32_t off;     /* desfase en verano */
    float base, amp; /* temperatura media y oscilación diaria */
} city_t;

static const city_t kCities[] = {
    {"Madrid", "Comunidad de Madrid", "Espa\\u00f1a", 40.4165f, -3.70256f, 7200, 21.0f, 7.0f},
    {"Madrid", "Cundinamarca", "Colombia", 4.72f, -74.26f, -18000, 14.0f, 5.0f},
    {"M\\u00e1laga", "Andaluc\\u00eda", "Espa\\u00f1a", 36.7202f, -4.4203f, 7200, 24.0f, 5.0f},
    {"Lima", "Lima", "Per\\u00fa", -12.04318f, -77.02824f, -18000, 19.0f, 3.0f},
    {"San Juan de Lurigancho", "Lima region", "Per\\u00fa", -11.99167f, -77.00694f, -18000, 19.5f, 3.5f},
};

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static int64_t utc_now(void)
{
    int64_t u;
    return watch_time_now_utc(&u) ? u : 1792000000;
}

static const city_t *city_of(const wx_loc_t *l)
{
    for (size_t i = 0; i < sizeof(kCities) / sizeof(kCities[0]); i++) {
        if (fabsf(kCities[i].lat - l->lat) < 0.01f && fabsf(kCities[i].lon - l->lon) < 0.01f) {
            return &kCities[i];
        }
    }
    return &kCities[0];
}

static float temp_at(const city_t *c, int32_t local)
{
    const float h = (float)(((local % 86400) + 86400) % 86400) / 3600.0f;
    return c->base + c->amp * sinf((h - 9.0f) / 24.0f * 6.2831853f);
}

/* Pronóstico de 48 h y 7 días con el formato de la API */
static size_t fx_forecast(const wx_loc_t *l, int64_t utc, char *o, size_t cap)
{
    const city_t *c = city_of(l);
    const int32_t off = c->off, cur = (int32_t)(utc - utc % 900), h0 = (int32_t)(utc - utc % 3600);
    const int32_t lday = (int32_t)((utc + off) - (utc + off) % 86400) - off;
    static const int kHrCode[12] = {1, 1, 2, 2, 3, 3, 61, 61, 80, 2, 1, 0};
    static const int kDyCode[7] = {2, 1, 61, 3, 80, 95, 0};
    static const int kDyPop[7] = {10, 0, 70, 20, 55, 80, 5};
    size_t n = 0;
#define P(...) n += (size_t)snprintf(o + n, n < cap ? cap - n : 0, __VA_ARGS__)
    const int lh = (int)(((cur + off) % 86400 + 86400) % 86400 / 3600);
    P("{\"latitude\":%.4f,\"longitude\":%.4f,\"generationtime_ms\":0.31,\"utc_offset_seconds\":%d,"
      "\"timezone\":\"GMT\",\"elevation\":650.0,"
      "\"current_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"\\u00b0C\"},"
      "\"current\":{\"time\":%d,\"interval\":900,\"temperature_2m\":%.1f,\"apparent_temperature\":%.1f,"
      "\"relative_humidity_2m\":48,\"is_day\":%d,\"precipitation\":0.00,\"weather_code\":%d,\"cloud_cover\":40,"
      "\"pressure_msl\":1016.2,\"wind_speed_10m\":14.0,\"wind_direction_10m\":250,\"wind_gusts_10m\":31.0},"
      "\"hourly_units\":{\"time\":\"unixtime\"},\"hourly\":{\"time\":[",
      (double)l->lat, (double)l->lon, off, cur, (double)temp_at(c, cur + off), (double)(temp_at(c, cur + off) - 1.2f),
      lh >= 7 && lh < 21, kHrCode[(cur / 3600) % 12]);
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%d", i ? "," : "", h0 + i * 3600);
    }
    P("],\"temperature_2m\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%.1f", i ? "," : "", (double)temp_at(c, h0 + i * 3600 + off));
    }
    P("],\"apparent_temperature\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%.1f", i ? "," : "", (double)(temp_at(c, h0 + i * 3600 + off) - 1.0f));
    }
    P("],\"relative_humidity_2m\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%d", i ? "," : "", 40 + (i * 7) % 40);
    }
    P("],\"precipitation\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%.2f", i ? "," : "", kHrCode[((h0 / 3600) + i) % 12] >= 61 ? 0.4 : 0.0);
    }
    P("],\"precipitation_probability\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        if (i == 5) {
            P(",null"); /* null != 0 también en la vista previa */
        } else {
            P("%s%d", i ? "," : "", kHrCode[((h0 / 3600) + i) % 12] >= 61 ? 65 : (i * 3) % 20);
        }
    }
    P("],\"weather_code\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%d", i ? "," : "", kHrCode[((h0 / 3600) + i) % 12]);
    }
    P("],\"wind_speed_10m\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%.1f", i ? "," : "", 10.0 + (i % 6));
    }
    P("],\"uv_index\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        const int h = (int)(((h0 + i * 3600 + off) % 86400 + 86400) % 86400 / 3600);
        P("%s%.2f", i ? "," : "", h >= 8 && h <= 19 ? 7.0 * sin((h - 7) / 13.0 * 3.14159) : 0.0);
    }
    P("],\"visibility\":[");
    for (int i = 0; i < WX_HOURS; i++) {
        P("%s%s", i ? "," : "", i == 7 ? "null" : "24140.0");
    }
    P("]},\"daily_units\":{\"time\":\"unixtime\"},\"daily\":{\"time\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%d", i ? "," : "", lday + i * 86400);
    }
    P("],\"weather_code\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%d", i ? "," : "", kDyCode[i]);
    }
    P("],\"temperature_2m_max\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%.1f", i ? "," : "", (double)(c->base + c->amp + (float)((i * 5) % 7) - 3.0f));
    }
    P("],\"temperature_2m_min\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%.1f", i ? "," : "", (double)(c->base - c->amp + (float)((i * 3) % 5) - 2.0f));
    }
    P("],\"sunrise\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%d", i ? "," : "", lday + i * 86400 + 7 * 3600 + 45 * 60);
    }
    P("],\"sunset\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%d", i ? "," : "", lday + i * 86400 + 21 * 3600 + 5 * 60);
    }
    P("],\"uv_index_max\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%s", i ? "," : "", i == 6 ? "null" : "6.80");
    }
    P("],\"precipitation_probability_max\":[");
    for (int i = 0; i < WX_DAYS; i++) {
        P("%s%d", i ? "," : "", kDyPop[i]);
    }
    P("]}}");
#undef P
    return n < cap ? n : cap - 1;
}

static size_t fx_geo(const char *q, char *o, size_t cap)
{
    size_t n = (size_t)snprintf(o, cap, "{\"results\":[");
    int k = 0;
    for (size_t i = 0; i < sizeof(kCities) / sizeof(kCities[0]); i++) {
        const city_t *c = &kCities[i];
        if (strncasecmp(c->name, q, strlen(q)) != 0) {
            continue;
        }
        n += (size_t)snprintf(o + n, cap - n,
                              "%s{\"id\":%d,\"name\":\"%s\",\"latitude\":%.5f,\"longitude\":%.5f,\"admin1\":\"%s\","
                              "\"country\":\"%s\",\"timezone\":\"GMT\"}",
                              k ? "," : "", 3100 + (int)i, c->name, (double)c->lat, (double)c->lon, c->admin1,
                              c->country);
        k++;
    }
    if (k == 0) { /* la API sin resultados no manda "results" */
        return (size_t)snprintf(o, cap, "{\"generationtime_ms\":0.52}");
    }
    n += (size_t)snprintf(o + n, cap - n, "],\"generationtime_ms\":0.9}");
    return n;
}

static void pump(void)
{
    if (!s_init) {
        sim_wx_reset();
    }
    for (int guard = 0; guard < 4; guard++) {
        const uint32_t t = now_ms();
        wx_core_set_clock(&C, utc_now(), t);
        wx_core_tick(&C, t, s_online);
        if (!s_running) {
            if (!wx_core_take_job(&C, &s_job)) {
                return;
            }
            s_running = true;
            s_done_at = t + SIM_WX_LAT_MS;
        }
        if ((int32_t)(t - s_done_at) < 0) {
            return;
        }
        s_running = false;
        if (s_job.kind == WXJ_SEARCH) {
            s_searches++;
            wx_loc_t res[WX_RESULTS];
            int n = 0, err = s_online ? WXE_NONE : WXE_NOWIFI;
            s_searches -= !s_online;
            if (err == WXE_NONE) {
                const size_t len = fx_geo(s_job.query, s_json, sizeof(s_json));
                n = wx_parse_geo(s_json, len, res, WX_RESULTS);
            }
            wx_core_search_done(&C, err, res, n);
        } else {
            /* sin Wi-Fi el servicio falla antes de abrir un socket (http_get) */
            s_fetches += s_online;
            int err = !s_online ? WXE_NOWIFI : s_fail;
            if (err == WXE_NONE) {
                const size_t len = fx_forecast(&s_job.loc, utc_now(), s_json, sizeof(s_json));
                err = wx_parse_forecast(s_json, len, &s_job.loc, &s_parse);
            }
            wx_core_fetch_done(&C, t, &s_job, err, err == WXE_NONE ? &s_parse : NULL);
        }
    }
}

void sim_wx_reset(void)
{
    s_init = true;
    s_online = true;
    s_running = false;
    s_fail = WXE_NONE;
    s_fetches = s_searches = 0;
    wx_core_init(&C, now_ms());
}

void sim_wx_online(bool on)
{
    s_online = on;
}

void sim_wx_fail(int err)
{
    s_fail = err;
}

int sim_wx_fetches(void)
{
    return s_fetches;
}

int sim_wx_searches(void)
{
    return s_searches;
}

bool sim_wx_busy(void)
{
    return s_running;
}

void sim_wx_reboot(void)
{
    /* lo que el servicio del reloj habría dejado en la NVS */
    static wx_data_t cache;
    wx_loc_t locs[WX_LOCS];
    memcpy(locs, C.locs, sizeof(locs));
    const uint8_t n = C.nloc;
    const int8_t sel = C.sel;
    const bool have = C.have;
    cache = C.data;
    s_running = false;
    wx_core_init(&C, now_ms());
    wx_core_set_locs(&C, locs, n, sel);
    if (have) {
        wx_core_load_cache(&C, &cache, sizeof(cache));
    }
}

const wx_core_t *sim_wx_core(void)
{
    return &C;
}

/* ---- API de watch_weather ---- */

esp_err_t watch_weather_start(void)
{
    pump();
    return ESP_OK;
}

void watch_weather_get(watch_weather_status_t *out)
{
    pump();
    memset(out, 0, sizeof(*out));
    const uint32_t t = now_ms();
    out->status = wx_core_status(&C);
    out->err = C.err;
    out->have = C.have;
    out->gen = C.gen;
    out->age_s = wx_core_age_s(&C, t);
    out->now_local = wx_core_now_local(&C, t);
    out->now_utc = wx_core_now_utc(&C, t);
    out->nloc = C.nloc;
    out->sel = C.sel;
    memcpy(out->locs, C.locs, sizeof(out->locs));
    out->qst = C.qst;
    out->qgen = C.qgen;
    out->nres = C.nres;
    memcpy(out->res, C.res, sizeof(out->res));
}

bool watch_weather_data(wx_data_t *out)
{
    pump();
    if (C.have) {
        *out = C.data;
    }
    return C.have;
}

void watch_weather_refresh(bool force)
{
    pump();
    wx_core_refresh(&C, now_ms(), force);
}

void watch_weather_select(uint8_t i)
{
    pump();
    wx_core_loc_select(&C, i, now_ms());
}

bool watch_weather_add(const wx_loc_t *l)
{
    pump();
    return wx_core_loc_add(&C, l, now_ms());
}

void watch_weather_remove(uint8_t i)
{
    pump();
    wx_core_loc_remove(&C, i, now_ms());
}

void watch_weather_search(const char *q)
{
    pump();
    wx_core_search(&C, q);
}

void watch_weather_search_clear(void)
{
    wx_core_search_clear(&C);
}
