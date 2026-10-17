/*
 * wx_logic — datos, parseo y textos del clima de Flex OS Ultra
 * (FlexOS_Weather.h/.cpp) portados a C puro, sin red ni sistema operativo:
 * el mismo código va al reloj, a tests/host y a la vista previa.
 *
 * Igual que la referencia:
 *  - Open-Meteo (sin clave), timeformat=unixtime, solo las variables que se
 *    pintan; 48 horas y 7 días;
 *  - escáner JSON propio que no construye árbol: busca la clave
 *    ENTRECOMILLADA (current no casa con current_units) y lee en sitio;
 *  - un null de la API NO es un 0: queda marcado (WXF_* / WX_NOVAL_*) y la
 *    interfaz dice "No disponible";
 *  - los tiempos unix se leen en doble precisión (con float, 1755558600 se
 *    quedaba en 1755558016);
 *  - una respuesta vacía, truncada o con el JSON de error de la API no se
 *    acepta (debe terminar en '}' y traer current.time y la temperatura);
 *  - el panel horario se alinea a la hora EN CURSO;
 *  - la frase del tiempo sale solo de datos reales y es determinista.
 * RELOJ: solo en español (la interfaz del reloj lo está); la geocodificación
 * se pide con language=es.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WX_HOURS   48 /* FLEXWX_HOURS */
#define WX_DAYS    7
#define WX_LOCS    5
#define WX_RESULTS 6
#define WX_NAME    36
#define WX_REGION  36
#define WX_QUERY   48

#define WX_HTTP_BUF 16384 /* respuesta de 48 h + 7 días: unos 5 KB */
#define WX_GEO_BUF  6144
#define WX_URL_MAX  768

/* Estado visual: el único conversor de weather_code (flexWeatherVisual). */
enum {
    WXV_CLEAR = 0,
    WXV_MOSTLY_CLEAR,
    WXV_PARTLY,
    WXV_CLOUDY,
    WXV_FOG,
    WXV_DRIZZLE,
    WXV_RAIN,
    WXV_HEAVY_RAIN,
    WXV_SLEET,
    WXV_SNOW,
    WXV_THUNDER,
    WXV_HAIL,
    WXV_COUNT
};

/* Estado del motor: nunca un bool ambiguo. */
enum {
    WXS_NEVER = 0, /* jamás hubo una descarga válida */
    WXS_LOADING,   /* primera descarga en curso */
    WXS_OK,        /* datos válidos de esta sesión */
    WXS_STALE,     /* válidos pero de la caché o el último intento falló */
    WXS_UPDATING,  /* hay datos y además se está refrescando */
    WXS_ERROR      /* sin datos y el último intento falló */
};

enum {
    WXE_NONE = 0,
    WXE_NOWIFI,
    WXE_CONNECT,
    WXE_TIMEOUT,
    WXE_HTTP,
    WXE_JSON,
    WXE_MEM,
    WXE_NOLOC,
    WXE_NORESULT,
    WXE_TLS
};

enum { WXQ_IDLE = 0, WXQ_BUSY, WXQ_OK, WXQ_EMPTY, WXQ_ERROR };

/* Enteros de ancho fijo: en décimas el error es invisible y ocupa la mitad. */
typedef struct {
    int32_t t;          /* unix UTC de la hora */
    int16_t temp10;     /* décimas de grado */
    int16_t feels10;
    int16_t wind10;     /* décimas de km/h */
    uint16_t vis100;    /* centenas de metro; WX_NOVAL_U16 = no disponible */
    uint16_t precip100; /* centésimas de mm */
    uint8_t code;
    uint8_t pop;        /* 0..100; WX_NOVAL_U8 = no disponible */
    uint8_t rh;
    uint8_t uv10;       /* décimas, saturado a 25,0 */
} wx_hour_t;

typedef struct {
    int32_t date; /* unix UTC del inicio del día local */
    int32_t sunrise;
    int32_t sunset;
    int16_t max10;
    int16_t min10;
    uint8_t code;
    uint8_t pop;
    uint8_t uv10;
} wx_day_t;

typedef struct {
    char name[WX_NAME];
    char region[WX_REGION]; /* "Región, País" */
    float lat, lon;
} wx_loc_t;

#define WXF_FEELS    0x0001
#define WXF_HUMIDITY 0x0002
#define WXF_PRECIP   0x0004
#define WXF_WIND     0x0008
#define WXF_GUST     0x0010
#define WXF_PRESSURE 0x0020
#define WXF_CLOUD    0x0040
#define WXF_UV       0x0080
#define WXF_VIS      0x0100
#define WXF_POP      0x0200
#define WXF_MINMAX   0x0400
#define WXF_SUN      0x0800
#define WXF_WINDDIR  0x1000

#define WX_NOVAL_I16 ((int16_t)-32768)
#define WX_NOVAL_U8  ((uint8_t)255)
#define WX_NOVAL_U16 ((uint16_t)0xFFFF)

#define WX_MAGIC 0x57583031u /* 'WX01' */
#define WX_VER   1

typedef struct {
    uint32_t magic;
    uint16_t ver;
    uint16_t have; /* WXF_* */
    wx_loc_t loc;  /* ubicación a la que corresponden ESTOS datos */
    int32_t obs_time;   /* current.time (unix UTC) */
    int32_t utc_offset; /* segundos respecto a UTC de la ubicación */
    float temp, feels, humidity, precip, wind_speed, wind_gust, pressure, cloud, uv, visibility;
    int16_t wind_dir;
    uint8_t pop, code, is_day, hour_count, day_count, pad_;
    float tmax, tmin; /* de hoy */
    int32_t sunrise, sunset;
    wx_hour_t hours[WX_HOURS];
    wx_day_t days[WX_DAYS];
} wx_data_t;

/* ---- Escáner JSON (expuesto para las pruebas) ---- */
const char *wx_j_ws(const char *p, const char *e);
const char *wx_j_skip(const char *p, const char *e);
const char *wx_j_find(const char *s, const char *e, const char *key);
bool wx_j_block(const char *s, const char *e, const char *key, const char **os, const char **oe);
bool wx_j_num(const char *p, const char *e, float *out);
bool wx_j_int(const char *p, const char *e, int32_t *out);
bool wx_j_str(const char *p, const char *e, char *out, size_t n);

/** Pronóstico -> *out (lo rellena entero). WXE_NONE o WXE_JSON; con error
 * *out puede quedar a medias: no se publica. */
int wx_parse_forecast(const char *s, size_t len, const wx_loc_t *loc, wx_data_t *out);
/** Resultados del geocoding. Devuelve cuántos válidos (0 = ninguno). */
int wx_parse_geo(const char *s, size_t len, wx_loc_t *res, int max);

/** URL del pronóstico de loc. false si no cabe. */
bool wx_url_forecast(const wx_loc_t *loc, char *out, size_t n);
/** URL de búsqueda de ciudad (language=es). */
bool wx_url_geo(const char *query, char *out, size_t n);
void wx_url_encode(const char *in, char *out, size_t n);

/* ---- Conversores deterministas ---- */
uint8_t wx_visual(int code);
const char *wx_cond_name(int code);
const char *wx_error_text(uint8_t err);
/** Frase a partir de datos reales; now_local = unix + desfase (0 = no se sabe:
 * cuenta como mediodía, como la referencia). w NULL -> "". */
void wx_describe(const wx_data_t *w, int32_t now_local, char *out, size_t n);
const char *wx_uv_label(float uv);
const char *wx_wind_dir(int deg);
/** 0 = domingo (1970-01-01 fue jueves). */
int wx_weekday(int32_t local_unix);
/** Hora del día (0..23) y minutos de un instante local. */
void wx_hm(int32_t local_unix, int *h, int *m);

/* ---- Persistencia ---- */
/** ¿Una caché leída de NVS es buena? (la sanea: nombres terminados). */
bool wx_cache_ok(wx_data_t *w, size_t n);
/** Sanea ubicaciones leídas de NVS: si alguna es imposible, ninguna vale.
 * Devuelve cuántas quedan y corrige *sel. */
uint8_t wx_locs_sanitize(wx_loc_t *locs, uint8_t n, int8_t *sel);
bool wx_loc_same(const wx_loc_t *a, const wx_loc_t *b);

#ifdef __cplusplus
}
#endif
