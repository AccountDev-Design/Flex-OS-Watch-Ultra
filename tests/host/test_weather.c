/*
 * Pruebas del clima (F9.8): wx_logic + wx_core, el mismo código que va al
 * reloj. Reproduce las comprobaciones de tests/host/test_weather.cpp de Flex
 * OS Ultra (mismas respuestas reales de Open-Meteo) y añade las del reloj.
 * LÍMITE: aquí no hay TLS ni sockets; la descarga real (esp_http_client)
 * solo se prueba en el reloj (docs/PRUEBAS-RELOJ.md).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_util.h"
#include "wx_core.h"
#include "wx_logic.h"

/* Respuesta con el formato EXACTO de Open-Meteo (timeformat=unixtime),
 * recortada a 6 horas y 3 días: la de la referencia, byte a byte. */
static const char *SAMPLE =
    "{\"latitude\":-11.99,\"longitude\":-77.0,\"generationtime_ms\":0.42,\"utc_offset_seconds\":-18000,"
    "\"timezone\":\"America/Lima\",\"timezone_abbreviation\":\"-05\",\"elevation\":161.0,"
    "\"current_units\":{\"time\":\"unixtime\",\"interval\":\"seconds\",\"temperature_2m\":\"C\",\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":1755558600,\"interval\":900,\"temperature_2m\":21.3,\"apparent_temperature\":21.0,"
    "\"relative_humidity_2m\":78,\"is_day\":1,\"precipitation\":0.00,\"weather_code\":2,\"cloud_cover\":62,"
    "\"pressure_msl\":1013.4,\"wind_speed_10m\":27.4,\"wind_direction_10m\":200,\"wind_gusts_10m\":41.0},"
    "\"hourly_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"C\"},"
    "\"hourly\":{\"time\":[1755558000,1755561600,1755565200,1755568800,1755572400,1755576000],"
    "\"temperature_2m\":[21.4,20.6,19.9,19.5,19.2,19.0],"
    "\"apparent_temperature\":[21.1,20.3,19.6,19.2,18.9,18.7],"
    "\"relative_humidity_2m\":[78,80,82,83,84,85],"
    "\"precipitation\":[0.00,0.00,0.10,0.00,0.00,0.00],"
    "\"precipitation_probability\":[2,2,3,null,4,5],"
    "\"weather_code\":[2,2,3,3,3,2],"
    "\"wind_speed_10m\":[27.4,25.1,22.0,20.4,19.8,18.5],"
    "\"uv_index\":[0.40,0.10,0.00,0.00,0.00,0.00],"
    "\"visibility\":[24140.0,24140.0,20000.0,null,18000.0,17000.0]},"
    "\"daily_units\":{\"time\":\"unixtime\"},"
    "\"daily\":{\"time\":[1755493200,1755579600,1755666000],"
    "\"weather_code\":[3,2,61],"
    "\"temperature_2m_max\":[25.1,26.3,24.0],"
    "\"temperature_2m_min\":[17.2,17.8,18.1],"
    "\"sunrise\":[1755516720,1755603120,1755689520],"
    "\"sunset\":[1755559440,1755645840,1755732240],"
    "\"uv_index_max\":[6.10,5.80,null],"
    "\"precipitation_probability_max\":[3,9,48]}}";

/* Geocoding real, con un acento escapado (ú). */
static const char *GEO =
    "{\"results\":[{\"id\":3934905,\"name\":\"San Juan de Lurigancho\",\"latitude\":-11.99167,"
    "\"longitude\":-77.00694,\"elevation\":190.0,\"feature_code\":\"PPLA3\",\"country_code\":\"PE\","
    "\"admin1\":\"Lima region\",\"admin2\":\"Lima\",\"timezone\":\"America/Lima\",\"population\":1091303,"
    "\"country\":\"Per\\u00fa\",\"admin1_id\":3936451},"
    "{\"id\":3936456,\"name\":\"Lima\",\"latitude\":-12.04318,\"longitude\":-77.02824,"
    "\"country_code\":\"PE\",\"admin1\":\"Lima\",\"timezone\":\"America/Lima\",\"country\":\"Per\\u00fa\"}],"
    "\"generationtime_ms\":0.9}";

static wx_loc_t mk_loc(const char *name, float lat, float lon)
{
    wx_loc_t l;
    memset(&l, 0, sizeof(l));
    snprintf(l.name, sizeof(l.name), "%s", name);
    l.lat = lat;
    l.lon = lon;
    return l;
}

static wx_data_t W; /* 1,3 KB: fuera de la pila */

static void t_forecast(void)
{
    const wx_loc_t loc = mk_loc("San Juan de Lurigancho", -11.99f, -77.0f);
    CHECK_EQ(wx_parse_forecast(SAMPLE, strlen(SAMPLE), &loc, &W), WXE_NONE);
    CHECK_EQ(W.utc_offset, -18000);
    CHECK_EQ(W.obs_time, 1755558600); /* sin pérdida de precisión */
    CHECK(W.temp > 21.2f && W.temp < 21.4f);
    CHECK(W.code == 2 && W.is_day == 1);
    CHECK((W.have & WXF_HUMIDITY) && (int)W.humidity == 78);
    CHECK((W.have & WXF_WIND) && W.wind_speed > 27.3f);
    CHECK((W.have & WXF_GUST) && W.wind_gust > 40.9f);
    CHECK((W.have & WXF_PRESSURE) && W.pressure > 1013.0f);
    CHECK((W.have & WXF_WINDDIR) && W.wind_dir == 200);
    CHECK((W.have & WXF_FEELS) && (W.have & WXF_PRECIP) && (W.have & WXF_CLOUD));
    CHECK_EQ(W.hour_count, 6);
    CHECK_EQ(W.hours[0].t, 1755558000);
    CHECK(W.hours[0].temp10 == 214 && W.hours[1].temp10 == 206);
    CHECK_EQ(W.hours[2].precip100, 10);
    CHECK_EQ(W.hours[2].vis100, 200);
    CHECK((W.have & WXF_UV) && W.uv > 0.39f && W.uv < 0.41f);
    CHECK((W.have & WXF_POP) && W.pop == 2);
    CHECK_EQ(W.day_count, 3);
    CHECK(W.days[0].max10 == 251 && W.days[0].min10 == 172);
    CHECK_EQ(W.days[2].code, 61);
    CHECK((W.have & WXF_MINMAX) && (int)W.tmax == 25);
    CHECK((W.have & WXF_SUN) && W.sunrise == 1755516720 && W.sunset == 1755559440);
    CHECK(strcmp(W.loc.name, "San Juan de Lurigancho") == 0);
    CHECK(W.magic == WX_MAGIC && W.ver == WX_VER);
    /* null != 0 */
    CHECK_EQ(W.hours[3].pop, WX_NOVAL_U8);
    CHECK_EQ(W.hours[3].vis100, WX_NOVAL_U16);
    CHECK_EQ(W.days[2].uv10, WX_NOVAL_U8);
}

static void t_broken(void)
{
    static const char *const cases[] = {
        "",
        "{",
        "no soy json",
        "{\"error\":true,\"reason\":\"Latitude must be in range\"}",
        "{\"current\":{\"time\":123}}",
        "{\"current\":{\"time\":1,\"temperature_2m\":",
        "{\"current\":{\"temperature_2m\":20.0}}",       /* sin hora */
        "{\"current\":{\"time\":1,\"temperature_2m\":null}}", /* temperatura nula */
    };
    const wx_loc_t l = mk_loc("X", 0, 0);
    static wx_data_t tmp;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK_EQ(wx_parse_forecast(cases[i], strlen(cases[i]), &l, &tmp), WXE_JSON);
    }
    /* cortada dentro de "hourly": la conexión que cae */
    char half[2048];
    size_t n = strlen(SAMPLE) * 2 / 3;
    memcpy(half, SAMPLE, n);
    half[n] = 0;
    CHECK_EQ(wx_parse_forecast(half, n, &l, &tmp), WXE_JSON);
    /* RELOJ: cortada justo tras un '}' interno tampoco cuela */
    const char *cut = strstr(SAMPLE, "\"hourly_units\"");
    n = (size_t)(cut - SAMPLE) - 1; /* termina en el '}' de "current" */
    memcpy(half, SAMPLE, n);
    half[n] = 0;
    CHECK(half[n - 1] == '}');
    /* acaba en '}' pero el objeto raíz no está cerrado: current se lee, y
     * las horas y los días no existen (no se inventan) */
    if (wx_parse_forecast(half, n, &l, &tmp) == WXE_NONE) {
        CHECK_EQ(tmp.hour_count, 0);
        CHECK_EQ(tmp.day_count, 0);
    }
    CHECK_EQ(wx_parse_forecast(NULL, 0, &l, &tmp), WXE_JSON);
}

static void build(char *out, size_t cap, int n_hours, int first_off_h, int n_days)
{
    const long obs = 1755558600;
    const long h0 = obs - (obs % 3600) - (long)first_off_h * 3600;
    size_t o = 0;
    o += snprintf(out + o, cap - o,
                  "{\"utc_offset_seconds\":-18000,\"current\":{\"time\":%ld,"
                  "\"temperature_2m\":21.3,\"weather_code\":2,\"is_day\":1},\"hourly\":{\"time\":[",
                  obs);
    for (int i = 0; i < n_hours; i++) {
        o += snprintf(out + o, cap - o, "%s%ld", i ? "," : "", h0 + (long)i * 3600);
    }
    o += snprintf(out + o, cap - o, "],\"temperature_2m\":[");
    for (int i = 0; i < n_hours; i++) {
        o += snprintf(out + o, cap - o, "%s%d.5", i ? "," : "", 10 + (i % 20));
    }
    o += snprintf(out + o, cap - o, "],\"weather_code\":[");
    for (int i = 0; i < n_hours; i++) {
        o += snprintf(out + o, cap - o, "%s2", i ? "," : "");
    }
    o += snprintf(out + o, cap - o, "]},\"daily\":{\"time\":[");
    for (int i = 0; i < n_days; i++) {
        o += snprintf(out + o, cap - o, "%s%ld", i ? "," : "", h0 + (long)i * 86400);
    }
    o += snprintf(out + o, cap - o, "],\"temperature_2m_max\":[");
    for (int i = 0; i < n_days; i++) {
        o += snprintf(out + o, cap - o, "%s%d.0", i ? "," : "", 20 + i);
    }
    o += snprintf(out + o, cap - o, "],\"temperature_2m_min\":[");
    for (int i = 0; i < n_days; i++) {
        o += snprintf(out + o, cap - o, "%s%d.0", i ? "," : "", 10 + i);
    }
    o += snprintf(out + o, cap - o, "],\"weather_code\":[");
    for (int i = 0; i < n_days; i++) {
        o += snprintf(out + o, cap - o, "%s3", i ? "," : "");
    }
    snprintf(out + o, cap - o, "]}}");
}

static void t_align(void)
{
    static char j[WX_HTTP_BUF];
    const wx_loc_t l = mk_loc("Z", 0, 0);
    const long obs = 1755558600, cur = obs - (obs % 3600);
    build(j, sizeof(j), 24, 6, 7);
    CHECK_EQ(wx_parse_forecast(j, strlen(j), &l, &W), WXE_NONE);
    CHECK_EQ(W.hours[0].t, cur); /* empieza en la hora EN CURSO */
    CHECK_EQ(W.hour_count, 24 - 6);
    CHECK_EQ(W.hours[0].temp10, 165); /* la hora 6 del array: 16,5 */
    /* RELOJ: variables que no vienen quedan "no disponible", no a 0 */
    CHECK_EQ(W.hours[0].feels10, WX_NOVAL_I16);
    CHECK_EQ(W.hours[0].pop, WX_NOVAL_U8);
    CHECK(!(W.have & WXF_UV) && !(W.have & WXF_VIS) && !(W.have & WXF_POP));
    CHECK_EQ(W.days[0].pop, WX_NOVAL_U8);
    CHECK(!(W.have & WXF_SUN)); /* sin sunrise/sunset no hay sol */

    build(j, sizeof(j), 96, 0, 16);
    CHECK(strlen(j) < WX_HTTP_BUF);
    CHECK_EQ(wx_parse_forecast(j, strlen(j), &l, &W), WXE_NONE);
    CHECK_EQ(W.hour_count, WX_HOURS);
    CHECK_EQ(W.day_count, WX_DAYS);

    build(j, sizeof(j), 4, 10, 3);
    CHECK_EQ(wx_parse_forecast(j, strlen(j), &l, &W), WXE_NONE);
    CHECK_EQ(W.hour_count, 0); /* todas pasadas */

    const char *only = "{\"utc_offset_seconds\":0,\"current\":{\"time\":1755558600,"
                       "\"temperature_2m\":9.5,\"weather_code\":61,\"is_day\":0}}";
    CHECK_EQ(wx_parse_forecast(only, strlen(only), &l, &W), WXE_NONE);
    CHECK(W.hour_count == 0 && W.day_count == 0);
    CHECK(W.temp > 9.4f && W.temp < 9.6f);
    CHECK(!(W.have & WXF_MINMAX));
    CHECK(!(W.have & WXF_SUN));
    CHECK_EQ(W.is_day, 0);
}

static void t_geo(void)
{
    wx_loc_t r[WX_RESULTS];
    CHECK_EQ(wx_parse_geo(GEO, strlen(GEO), r, WX_RESULTS), 2);
    CHECK(strcmp(r[0].name, "San Juan de Lurigancho") == 0);
    CHECK(strcmp(r[0].region, "Lima region, Per\xC3\xBA") == 0); /* ú -> UTF-8 */
    CHECK(r[0].lat < -11.9f && r[0].lat > -12.0f);
    CHECK(strcmp(r[1].name, "Lima") == 0 && strcmp(r[1].region, "Lima, Per\xC3\xBA") == 0);
    CHECK_EQ(wx_parse_geo(GEO, strlen(GEO), r, 1), 1); /* respeta el tope */
    const char *none = "{\"generationtime_ms\":0.9}";
    CHECK_EQ(wx_parse_geo(none, strlen(none), r, WX_RESULTS), 0);
    /* coordenadas imposibles o sin nombre no se dan por buenas */
    const char *bad = "{\"results\":[{\"name\":\"A\",\"latitude\":99,\"longitude\":0},"
                      "{\"name\":\"\",\"latitude\":1,\"longitude\":1},{\"name\":\"B\",\"latitude\":1}]}";
    CHECK_EQ(wx_parse_geo(bad, strlen(bad), r, WX_RESULTS), 0);
    /* RELOJ: región larga recortada sin partir una letra */
    const char *lng = "{\"results\":[{\"name\":\"C\",\"latitude\":1,\"longitude\":2,"
                      "\"admin1\":\"\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\\u00c1\","
                      "\"country\":\"\\u00d1\\u00d1\\u00d1\\u00d1\\u00d1\\u00d1\\u00d1\\u00d1\\u00d1\"}]}";
    CHECK_EQ(wx_parse_geo(lng, strlen(lng), r, WX_RESULTS), 1);
    const size_t k = strlen(r[0].region);
    CHECK(k < WX_REGION && k % 2 == 0); /* solo letras de 2 bytes y ", " */
    CHECK(((uint8_t)r[0].region[k - 1] & 0xC0) == 0x80);
    /* escáner: "current" no casa con "current_units" */
    const char *j = "{\"current_units\":{\"a\":1},\"current\":{\"a\":2}}";
    const char *s = NULL, *e = NULL;
    float f = 0;
    CHECK(wx_j_block(j, j + strlen(j), "current", &s, &e) && wx_j_num(wx_j_find(s, e, "a"), e, &f) && f == 2.0f);
    /* cadena recortada en un carácter de varios bytes */
    char nm[4];
    CHECK(wx_j_str("\"a\xC3\xB1\xC3\xB1\"", "\"a\xC3\xB1\xC3\xB1\"" + 6, nm, sizeof(nm)) && strcmp(nm, "a\xC3\xB1") == 0);
    CHECK(wx_j_str("\"ab\\u00f1\"", "\"ab\\u00f1\"" + 10, nm, sizeof(nm)) && strcmp(nm, "ab") == 0);
}

static void t_texts(void)
{
    CHECK_EQ(wx_visual(0), WXV_CLEAR);
    CHECK_EQ(wx_visual(2), WXV_PARTLY);
    CHECK_EQ(wx_visual(65), WXV_HEAVY_RAIN);
    CHECK_EQ(wx_visual(95), WXV_THUNDER);
    CHECK_EQ(wx_visual(56), WXV_SLEET);
    CHECK_EQ(wx_visual(199), WXV_CLOUDY);
    bool range = true, named = true;
    for (int c = -5; c < 260; c++) {
        range = range && wx_visual(c) < WXV_COUNT;
        named = named && wx_cond_name(c) && wx_cond_name(c)[0];
    }
    CHECK(range && named);
    CHECK(strcmp(wx_cond_name(2), "Parcialmente nublado") == 0);
    CHECK(strcmp(wx_cond_name(99), "Tormenta con granizo") == 0);
    for (uint8_t e = WXE_NOWIFI; e <= WXE_TLS; e++) {
        CHECK(wx_error_text(e)[0] != 0);
    }
    CHECK(wx_error_text(WXE_NONE)[0] == 0);
    CHECK(strcmp(wx_error_text(WXE_TLS), "Certificado del servidor no válido") == 0);
    CHECK(strcmp(wx_uv_label(2.4f), "Bajo") == 0 && strcmp(wx_uv_label(2.6f), "Moderado") == 0);
    CHECK(strcmp(wx_uv_label(7.0f), "Alto") == 0 && strcmp(wx_uv_label(10.0f), "Muy alto") == 0);
    CHECK(strcmp(wx_uv_label(11.0f), "Extremo") == 0);
    CHECK(strcmp(wx_wind_dir(0), "N") == 0 && strcmp(wx_wind_dir(200), "S") == 0);
    CHECK(strcmp(wx_wind_dir(225), "SO") == 0 && strcmp(wx_wind_dir(338), "N") == 0);
    CHECK(strcmp(wx_wind_dir(-90), "O") == 0 && strcmp(wx_wind_dir(720 + 45), "NE") == 0);
    CHECK_EQ(wx_weekday(0), 4);             /* 1970-01-01, jueves */
    CHECK_EQ(wx_weekday(1755558600 - 18000), 1); /* 2025-08-18 18:10 en Lima: lunes */
    CHECK_EQ(wx_weekday(-1), 3);            /* 1969-12-31: miércoles */
    int h = -1, m = -1;
    wx_hm(1755558600 - 18000, &h, &m);
    CHECK(h == 18 && m == 10);
    wx_hm(-60, &h, &m);
    CHECK(h == 23 && m == 59);

    const wx_loc_t l = mk_loc("Test", 0, 0);
    CHECK_EQ(wx_parse_forecast(SAMPLE, strlen(SAMPLE), &l, &W), WXE_NONE);
    char d1[160], d2[160], d3[160];
    const int32_t morning = 1755475200 + 10 * 3600; /* 10:00 del 18 de agosto en Lima */
    wx_describe(&W, morning, d1, sizeof(d1));
    wx_describe(&W, morning, d2, sizeof(d2));
    printf("   frase: %s\n", d1);
    CHECK(strstr(d1, "Cielo parcialmente cubierto y viento.") == d1); /* 27 km/h */
    CHECK(strstr(d1, "Máxima de 25 °C.") != NULL);
    CHECK(strstr(d1, "lluvia") == NULL); /* 2 %: no se avisa */
    CHECK(strcmp(d1, d2) == 0);          /* determinista */
    wx_describe(&W, 1755558600 - 18000, d3, sizeof(d3)); /* 18:10: por la tarde */
    CHECK(strstr(d3, "Mínima de 17 °C.") != NULL);
    wx_describe(&W, 0, d3, sizeof(d3)); /* sin hora: mediodía */
    CHECK(strstr(d3, "Máxima") != NULL);
    W.have &= (uint16_t)~WXF_WIND;
    W.wind_gust = 50;
    W.pop = 60;
    wx_describe(&W, 1755558600, d3, sizeof(d3));
    CHECK(strstr(d3, " con rachas fuertes.") != NULL && strstr(d3, "Probabilidad de lluvia del 60 %.") != NULL);
    W.code = 63;
    W.have = 0;
    wx_describe(&W, 1755558600, d3, sizeof(d3));
    CHECK(strcmp(d3, "Lluvia.") == 0); /* sin datos extra no se inventa nada */
    wx_describe(NULL, 0, d3, sizeof(d3));
    CHECK(d3[0] == 0);
    char tiny[8];
    wx_parse_forecast(SAMPLE, strlen(SAMPLE), &l, &W);
    wx_describe(&W, 0, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7);
}

static void t_url(void)
{
    char u[WX_URL_MAX];
    const wx_loc_t l = mk_loc("Lima", -12.04318f, -77.02824f);
    CHECK(wx_url_forecast(&l, u, sizeof(u)));
    static const char kHead[] = "https://api.open-meteo.com/v1/forecast?latitude=-12.0432&longitude=-77.0282"
                                "&timezone=auto&timeformat=unixtime&wind_speed_unit=kmh&forecast_days=7"
                                "&forecast_hours=48&current=temperature_2m,";
    CHECK(strncmp(u, kHead, sizeof(kHead) - 1) == 0);
    CHECK(strstr(u, "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,"
                    "precipitation_probability,weather_code,wind_speed_10m,uv_index,visibility&daily=") != NULL);
    CHECK(strstr(u, "precipitation_probability_max") != NULL && strstr(u, "wind_gusts_10m") != NULL);
    printf("   URL del pronóstico: %zu bytes (búfer de envío %d)\n", strlen(u), WX_URL_MAX + 256);
    CHECK(strlen(u) + 64 < WX_URL_MAX + 256);
    CHECK(!wx_url_forecast(&l, u, 100)); /* no cabe: no se manda una URL cortada */
    char enc[64];
    wx_url_encode("S\xC3\xA3o Paulo & co", enc, sizeof(enc));
    CHECK(strcmp(enc, "S%C3%A3o+Paulo+%26+co") == 0);
    wx_url_encode("abcdefgh", enc, 8);
    CHECK(strlen(enc) < 8);
    CHECK(wx_url_geo("Cusco", u, sizeof(u)));
    CHECK(strcmp(u, "https://geocoding-api.open-meteo.com/v1/search?name=Cusco&count=6&language=es&format=json") == 0);
    CHECK(!wx_url_geo("", u, sizeof(u)));
}

/* La política de TLS del reloj: verificado con el paquete de CA de ESP-IDF y
 * nunca sin verificar (FLEXWX_ALLOW_INSECURE = 0 en la referencia). */
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    static char buf[64 * 1024];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void t_tls(void)
{
    const char *src = slurp("build/fw/components/watch_weather/watch_weather.c");
    CHECK(src != NULL);
    if (src) {
        CHECK(strstr(src, ".crt_bundle_attach = esp_crt_bundle_attach") != NULL);
        CHECK(strstr(src, "skip_cert_common_name_check") == NULL);
        CHECK(strstr(src, "cert_pem") == NULL);
        CHECK(strstr(src, "http://") == NULL);
    }
    const char *cfg = slurp("build/fw/sdkconfig.defaults");
    CHECK(cfg != NULL);
    if (cfg) {
        CHECK(strstr(cfg, "\nCONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y") != NULL);
        CHECK(strstr(cfg, "CONFIG_ESP_TLS_INSECURE=y") == NULL);
    }
}

static wx_core_t C;

static void publish(uint32_t now, const wx_loc_t *l)
{
    wx_job_t job;
    wx_core_refresh(&C, now, true);
    CHECK(wx_core_take_job(&C, &job) && job.kind == WXJ_FETCH);
    CHECK(wx_loc_same(&job.loc, l));
    static wx_data_t d;
    CHECK_EQ(wx_parse_forecast(SAMPLE, strlen(SAMPLE), &job.loc, &d), WXE_NONE);
    wx_core_fetch_done(&C, now, &job, WXE_NONE, &d);
}

static void t_cache_and_states(void)
{
    const wx_loc_t sj = mk_loc("San Juan de Lurigancho", -11.99f, -77.0f);
    wx_core_init(&C, 1000);
    CHECK_EQ(wx_core_status(&C), WXS_NEVER);
    CHECK(wx_core_loc_add(&C, &sj, 1000));
    CHECK(C.locs_dirty && C.sel == 0 && C.req_fetch);
    wx_job_t job;
    CHECK(wx_core_take_job(&C, &job) && job.kind == WXJ_FETCH);
    CHECK_EQ(wx_core_status(&C), WXS_LOADING);
    wx_core_fetch_done(&C, 2000, &job, WXE_TIMEOUT, NULL);
    CHECK_EQ(wx_core_status(&C), WXS_ERROR);
    CHECK_EQ(C.err, WXE_TIMEOUT);
    publish(3000, &sj);
    CHECK_EQ(wx_core_status(&C), WXS_OK);
    CHECK(C.cache_dirty && C.err == WXE_NONE && C.fails == 0);
    CHECK_EQ(wx_core_age_s(&C, 3000 + 34 * 60 * 1000), 34 * 60);

    /* respuestas rotas: ni se publican ni corrompen lo que había */
    static wx_data_t before;
    before = C.data;
    wx_core_refresh(&C, 4000, true);
    CHECK(wx_core_take_job(&C, &job));
    CHECK_EQ(wx_core_status(&C), WXS_UPDATING);
    const uint32_t g0 = C.gen; /* empezar a descargar no cambia los datos */
    static wx_data_t junk;
    const int e = wx_parse_forecast("{", 1, &job.loc, &junk);
    wx_core_fetch_done(&C, 5000, &job, e, NULL);
    CHECK(memcmp(&before, &C.data, sizeof(before)) == 0 && C.have);
    CHECK_EQ(C.gen, g0);
    CHECK_EQ(wx_core_status(&C), WXS_STALE);
    CHECK_EQ(C.err, WXE_JSON);

    /* caché tras un "reinicio" */
    static wx_data_t saved;
    saved = C.data;
    const wx_loc_t locs[1] = {sj};
    wx_core_init(&C, 0);
    wx_core_set_locs(&C, locs, 1, 0);
    CHECK(wx_core_load_cache(&C, &saved, sizeof(saved)));
    CHECK(C.have && C.data.temp == saved.temp && C.data.obs_time == 1755558600 && C.data.hour_count == 6);
    CHECK(strcmp(C.data.loc.name, "San Juan de Lurigancho") == 0);
    CHECK_EQ(wx_core_status(&C), WXS_STALE); /* caché, no dato fresco */
    CHECK_EQ(wx_core_age_s(&C, 50000), -1);  /* sin hora real no se sabe */
    wx_core_set_clock(&C, 1500000000, 0);     /* semilla de fábrica: no vale */
    CHECK_EQ(wx_core_age_s(&C, 50000), -1);
    wx_core_set_clock(&C, 1755558600 + 34 * 60, 50000);
    CHECK_EQ(wx_core_age_s(&C, 52000), 34 * 60 + 2);
    CHECK_EQ(wx_core_now_local(&C, 52000), 1755558600 + 34 * 60 + 2 - 18000);
    /* corrupta, de otro tamaño o de otra ciudad: no se da por buena */
    static wx_data_t bad;
    memset(&bad, 'Z', sizeof(bad));
    wx_core_init(&C, 0);
    wx_core_set_locs(&C, locs, 1, 0);
    CHECK(!wx_core_load_cache(&C, &bad, sizeof(bad)));
    CHECK(!wx_core_load_cache(&C, &saved, sizeof(saved) - 4));
    bad = saved;
    bad.hour_count = WX_HOURS + 1;
    CHECK(!wx_core_load_cache(&C, &bad, sizeof(bad)));
    const wx_loc_t other[1] = {mk_loc("Cusco", -13.5f, -71.97f)};
    wx_core_set_locs(&C, other, 1, 0);
    CHECK(!wx_core_load_cache(&C, &saved, sizeof(saved)));
    CHECK(!C.have && wx_core_status(&C) == WXS_NEVER);
    /* sin hora del sistema: la de la respuesta más lo corrido */
    wx_core_init(&C, 0);
    wx_core_set_locs(&C, locs, 1, 0);
    publish(10000, &sj);
    CHECK_EQ(wx_core_now_utc(&C, 70000), 1755558600 + 60);
}

static void t_locations(void)
{
    wx_core_init(&C, 0);
    const wx_loc_t a = mk_loc("Cusco", -13.5f, -71.97f), b = mk_loc("Arequipa", -16.4f, -71.5f);
    CHECK(wx_core_loc_add(&C, &a, 0));
    CHECK(C.nloc == 1 && C.sel == 0);
    CHECK(wx_core_loc_add(&C, &a, 0) && C.nloc == 1); /* no se duplica */
    CHECK(wx_core_loc_add(&C, &b, 0) && C.nloc == 2 && C.sel == 1);
    for (int i = 0; i < 6; i++) {
        char n[16];
        snprintf(n, sizeof(n), "C%d", i);
        const wx_loc_t c = mk_loc(n, (float)i, (float)i);
        wx_core_loc_add(&C, &c, 0);
    }
    CHECK_EQ(C.nloc, WX_LOCS); /* tope */
    const wx_loc_t extra = mk_loc("Extra", 5, 5);
    CHECK(!wx_core_loc_add(&C, &extra, 0));
    CHECK(!wx_core_loc_add(&C, NULL, 0));
    /* "reinicio": las mismas, en el mismo orden */
    wx_loc_t copy[WX_LOCS];
    memcpy(copy, C.locs, sizeof(copy));
    const uint8_t n = C.nloc;
    const int8_t sel = C.sel;
    wx_core_init(&C, 0);
    wx_core_set_locs(&C, copy, n, sel);
    CHECK(C.nloc == n && C.sel == sel && strcmp(C.locs[0].name, "Cusco") == 0);
    /* borrar recoloca el resto y la elegida sigue siendo la misma ciudad */
    const wx_loc_t chosen = C.locs[C.sel];
    wx_core_loc_remove(&C, 0, 0);
    CHECK(C.nloc == n - 1 && strcmp(C.locs[0].name, "Arequipa") == 0);
    CHECK(wx_loc_same(&C.locs[C.sel], &chosen));
    wx_core_loc_remove(&C, 99, 0);
    CHECK_EQ(C.nloc, n - 1);
    while (C.nloc) {
        wx_core_loc_remove(&C, 0, 0);
    }
    CHECK_EQ(C.sel, -1);
    CHECK(!C.req_fetch);
    /* preferencias corruptas: coordenadas imposibles */
    wx_loc_t badl[WX_LOCS];
    memset(badl, 0, sizeof(badl));
    snprintf(badl[0].name, sizeof(badl[0].name), "Mala");
    badl[0].lat = 999;
    badl[0].lon = 999;
    wx_core_set_locs(&C, badl, 1, 0);
    CHECK(C.nloc == 0 && C.sel == -1);
    memcpy(badl, copy, sizeof(copy));
    wx_core_set_locs(&C, badl, 9, 0); /* cuenta imposible */
    CHECK_EQ(C.nloc, 0);
    wx_core_set_locs(&C, copy, 2, 7); /* selección fuera de rango */
    CHECK(C.nloc == 2 && C.sel == 0);
    copy[0].name[WX_NAME - 1] = 'x'; /* sin terminar */
    wx_core_set_locs(&C, copy, 1, 0);
    CHECK(C.nloc == 1 && strlen(C.locs[0].name) < WX_NAME);
}

static void t_cadence(void)
{
    const wx_loc_t a = mk_loc("Cusco", -13.5f, -71.97f), b = mk_loc("Lima", -12.0f, -77.0f);
    const wx_loc_t locs[2] = {a, b};
    wx_job_t job;
    wx_core_init(&C, 100000);
    wx_core_set_locs(&C, locs, 2, 0);
    wx_core_tick(&C, 100000 + 3999, true);
    CHECK(!C.req_fetch); /* el primer intento, a los 4 s */
    wx_core_tick(&C, 100000 + 4000, false);
    CHECK(!C.req_fetch); /* sin red no se intenta */
    wx_core_tick(&C, 100000 + 4000, true);
    CHECK(C.req_fetch);
    CHECK(wx_core_take_job(&C, &job) && wx_loc_same(&job.loc, &a));
    wx_core_tick(&C, 100000 + 5000, true);
    CHECK(!C.req_fetch); /* ocupado: no se apila otra */
    uint32_t t = 110000;
    wx_core_fetch_done(&C, t, &job, WXE_CONNECT, NULL);
    CHECK_EQ(C.next_ms, t + WX_RETRY_MS);
    for (int i = 0; i < 2; i++) {
        t = C.next_ms;
        wx_core_tick(&C, t - 1, true);
        CHECK(!C.req_fetch);
        wx_core_tick(&C, t, true);
        CHECK(wx_core_take_job(&C, &job));
        wx_core_fetch_done(&C, t, &job, WXE_TIMEOUT, NULL);
    }
    CHECK_EQ(C.fails, 3);
    CHECK_EQ(C.next_ms, t + WX_RETRY_LONG_MS); /* tras 3 fallos, 15 min */
    t = C.next_ms;
    wx_core_tick(&C, t, true);
    CHECK(wx_core_take_job(&C, &job));
    static wx_data_t d;
    wx_parse_forecast(SAMPLE, strlen(SAMPLE), &job.loc, &d);
    wx_core_fetch_done(&C, t, &job, WXE_NONE, &d);
    CHECK(C.fails == 0 && C.next_ms == t + WX_REFRESH_MS);
    /* refresco manual: no descarga si lo último es de hace < 5 min */
    wx_core_refresh(&C, t + WX_MANUAL_MIN_MS - 1, false);
    CHECK(!C.req_fetch);
    wx_core_refresh(&C, t + WX_MANUAL_MIN_MS, false);
    CHECK(C.req_fetch);
    CHECK(wx_core_take_job(&C, &job));
    wx_core_fetch_done(&C, t + WX_MANUAL_MIN_MS, &job, WXE_NONE, &d);
    t += WX_MANUAL_MIN_MS;
    wx_core_tick(&C, t + WX_REFRESH_MS, true);
    CHECK(C.req_fetch); /* a los 25 min, otra */
    CHECK(wx_core_take_job(&C, &job));

    /* RELOJ: se cambia de ciudad durante la descarga: los datos de la otra
     * no se enseñan con el nombre de esta, y se pide la nueva */
    wx_core_loc_select(&C, 1, t + WX_REFRESH_MS + 10);
    CHECK(!C.have && C.req_fetch && wx_loc_same(&C.cmd_loc, &b));
    const uint32_t g = C.gen;
    wx_core_fetch_done(&C, t + WX_REFRESH_MS + 20, &job, WXE_NONE, &d);
    CHECK(!C.have && C.gen == g); /* lo descartado no cambia lo que se ve */
    CHECK(wx_core_take_job(&C, &job) && wx_loc_same(&job.loc, &b));
    /* elegir la misma con datos frescos no descarga otra vez */
    static wx_data_t db;
    wx_parse_forecast(SAMPLE, strlen(SAMPLE), &b, &db);
    wx_core_fetch_done(&C, t + WX_REFRESH_MS + 30, &job, WXE_NONE, &db);
    CHECK(C.have && wx_core_status(&C) == WXS_OK);
    wx_core_loc_select(&C, 1, t + WX_REFRESH_MS + 40);
    CHECK(!C.req_fetch);
    /* sin ubicación: refresh da WXE_NOLOC */
    wx_core_init(&C, 0);
    wx_core_refresh(&C, 0, true);
    CHECK(C.err == WXE_NOLOC && !C.req_fetch);
}

static void t_search(void)
{
    wx_job_t job;
    wx_core_init(&C, 0);
    const wx_loc_t a = mk_loc("Cusco", -13.5f, -71.97f);
    wx_core_loc_add(&C, &a, 0); /* deja una descarga pedida */
    wx_core_search(&C, "Lima");
    CHECK_EQ(C.qst, WXQ_BUSY);
    wx_core_search(&C, "Otra"); /* ya hay una en cola */
    CHECK(strcmp(C.query, "Lima") == 0);
    CHECK(wx_core_take_job(&C, &job) && job.kind == WXJ_SEARCH && strcmp(job.query, "Lima") == 0);
    CHECK(C.req_fetch); /* la búsqueda no se come el refresco pendiente */
    wx_loc_t r[WX_RESULTS];
    const int n = wx_parse_geo(GEO, strlen(GEO), r, WX_RESULTS);
    wx_core_search_done(&C, WXE_NONE, r, n);
    CHECK(C.qst == WXQ_OK && C.nres == 2);
    CHECK(wx_core_take_job(&C, &job) && job.kind == WXJ_FETCH);
    wx_core_search(&C, "zzzz");
    wx_core_take_job(&C, &job);
    wx_core_search_done(&C, WXE_NONE, r, 0);
    CHECK(C.qst == WXQ_EMPTY && C.nres == 0);
    wx_core_search(&C, "Lima");
    wx_core_take_job(&C, &job);
    wx_core_search_done(&C, WXE_NOWIFI, NULL, 0);
    CHECK(C.qst == WXQ_ERROR && C.err == WXE_NOWIFI);
    wx_core_search_clear(&C);
    CHECK_EQ(C.qst, WXQ_IDLE);
}

void test_weather_all(void)
{
    t_forecast();
    t_broken();
    t_align();
    t_geo();
    t_texts();
    t_url();
    t_tls();
    t_cache_and_states();
    t_locations();
    t_cadence();
    t_search();
    printf("   clima: wx_data_t %zu B, wx_core_t %zu B\n", sizeof(wx_data_t), sizeof(wx_core_t));
    CHECK(sizeof(wx_data_t) < 1400); /* cabe holgado en un blob de NVS */
}
