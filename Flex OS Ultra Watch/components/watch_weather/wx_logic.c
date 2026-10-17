/*
 * wx_logic — ver include/wx_logic.h. Port de FlexOS_Weather.cpp (secciones
 * 1, 3, 4, 7 y el parseo de 8): mismas reglas, mismos números.
 */
#include "wx_logic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WX_HOST_FORECAST "https://api.open-meteo.com/v1/forecast"
#define WX_HOST_GEO      "https://geocoding-api.open-meteo.com/v1/search"

#define WX_CUR_VARS "temperature_2m,apparent_temperature,relative_humidity_2m,is_day," \
                    "precipitation,weather_code,cloud_cover,pressure_msl,"             \
                    "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
#define WX_HR_VARS  "temperature_2m,apparent_temperature,relative_humidity_2m,precipitation," \
                    "precipitation_probability,weather_code,wind_speed_10m,uv_index,visibility"
#define WX_DY_VARS  "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset," \
                    "uv_index_max,precipitation_probability_max"

/* ======================= Conversores ======================= */

uint8_t wx_visual(int code)
{
    switch (code) {
    case 0: return WXV_CLEAR;
    case 1: return WXV_MOSTLY_CLEAR;
    case 2: return WXV_PARTLY;
    case 3: return WXV_CLOUDY;
    case 45: case 48: return WXV_FOG;
    case 51: case 53: case 55: return WXV_DRIZZLE;
    case 56: case 57: return WXV_SLEET; /* llovizna helada: como la referencia (W.cpp:66) */
    case 61: case 63: return WXV_RAIN;
    case 65: return WXV_HEAVY_RAIN;
    case 66: case 67: return WXV_SLEET;
    case 71: case 73: case 75: case 77: return WXV_SNOW;
    case 80: case 81: return WXV_RAIN;
    case 82: return WXV_HEAVY_RAIN;
    case 85: case 86: return WXV_SNOW;
    case 95: return WXV_THUNDER;
    case 96: case 99: return WXV_HAIL;
    default: break;
    }
    /* desconocido: nublado es lo que menos afirma sobre el cielo */
    return WXV_CLOUDY;
}

const char *wx_cond_name(int code)
{
    static const char *const k[WXV_COUNT] = {
        "Despejado", "Mayormente despejado", "Parcialmente nublado", "Nublado", "Niebla", "Llovizna",
        "Lluvia",    "Lluvia intensa",       "Aguanieve",            "Nieve",   "Tormenta",
        "Tormenta con granizo",
    };
    return k[wx_visual(code)];
}

const char *wx_error_text(uint8_t err)
{
    switch (err) {
    case WXE_NOWIFI: return "Sin conexión Wi-Fi";
    case WXE_CONNECT: return "No se pudo conectar al servidor";
    case WXE_TIMEOUT: return "El servidor tardó demasiado";
    case WXE_HTTP: return "Respuesta no válida del servidor";
    case WXE_JSON: return "Datos incompletos";
    case WXE_MEM: return "Memoria insuficiente";
    case WXE_NOLOC: return "Sin ubicación";
    case WXE_NORESULT: return "Sin resultados";
    case WXE_TLS: return "Certificado del servidor no válido";
    default: return "";
    }
}

const char *wx_uv_label(float uv)
{
    const long u = lroundf(uv);
    return u <= 2 ? "Bajo" : u <= 5 ? "Moderado" : u <= 7 ? "Alto" : u <= 10 ? "Muy alto" : "Extremo";
}

const char *wx_wind_dir(int deg)
{
    static const char *const k[8] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
    deg %= 360;
    if (deg < 0) {
        deg += 360;
    }
    return k[((deg + 22) / 45) % 8];
}

static int32_t day_sec(int32_t t)
{
    return (int32_t)(((t % 86400) + 86400) % 86400);
}

int wx_weekday(int32_t local_unix)
{
    const int64_t d = ((int64_t)local_unix - day_sec(local_unix)) / 86400; /* días desde 1970 */
    return (int)(((d + 4) % 7 + 7) % 7);
}

void wx_hm(int32_t local_unix, int *h, int *m)
{
    const int32_t s = day_sec(local_unix);
    if (h) {
        *h = (int)(s / 3600);
    }
    if (m) {
        *m = (int)(s % 3600 / 60);
    }
}

void wx_describe(const wx_data_t *w, int32_t now_local, char *out, size_t n)
{
    if (!out || n == 0) {
        return;
    }
    out[0] = 0;
    if (!w) {
        return;
    }
    /* 1) cielo: del weather_code real */
    const char *sky;
    switch (wx_visual(w->code)) {
    case WXV_CLEAR:
    case WXV_MOSTLY_CLEAR: sky = "Cielo despejado"; break;
    case WXV_PARTLY: sky = "Cielo parcialmente cubierto"; break;
    case WXV_CLOUDY: sky = "Cielo cubierto"; break;
    case WXV_FOG: sky = "Niebla"; break;
    default: sky = wx_cond_name(w->code); break;
    }
    /* 2) viento o rachas reales */
    const char *extra = "";
    if ((w->have & WXF_WIND) && w->wind_speed >= 25.0f) {
        extra = " y viento";
    } else if ((w->have & WXF_GUST) && w->wind_gust >= 45.0f) {
        extra = " con rachas fuertes";
    }
    /* 3) mínima por la tarde/noche, máxima por la mañana (hora de la ubicación) */
    char tail[40] = "";
    if (w->have & WXF_MINMAX) {
        const int hour = now_local ? (int)(day_sec(now_local) / 3600) : 12;
        const bool evening = hour >= 15 || hour < 6;
        /* RELOJ: "°C" (la fuente del reloj tiene el signo; la de la placa no) */
        snprintf(tail, sizeof(tail), evening ? " Mínima de %ld °C." : " Máxima de %ld °C.",
                 lroundf(evening ? w->tmin : w->tmax));
    }
    /* 4) aviso de lluvia solo si la probabilidad real lo justifica */
    char rain[48] = "";
    if ((w->have & WXF_POP) && w->pop >= 40) {
        snprintf(rain, sizeof(rain), " Probabilidad de lluvia del %d %%.", (int)w->pop);
    }
    snprintf(out, n, "%s%s.%s%s", sky, extra, tail, rain);
}

/* ======================= Escáner JSON ======================= */
/* Requiere que el texto acabe en 0 en s[len] (strtod puede mirar un byte más
 * allá de un número); la tarea de red y las pruebas lo garantizan. */

const char *wx_j_ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
    }
    return p;
}

const char *wx_j_skip(const char *p, const char *e)
{
    p = wx_j_ws(p, e);
    if (p >= e) {
        return e;
    }
    if (*p == '{' || *p == '[') {
        const char open = *p, close = open == '{' ? '}' : ']';
        int depth = 0;
        bool str = false;
        while (p < e) {
            const char c = *p;
            if (str) {
                if (c == '\\') {
                    p += 2;
                    continue;
                }
                if (c == '"') {
                    str = false;
                }
            } else if (c == '"') {
                str = true;
            } else if (c == open) {
                depth++;
            } else if (c == close && --depth == 0) {
                return p + 1;
            }
            p++;
        }
        return e;
    }
    if (*p == '"') {
        p++;
        while (p < e) {
            if (*p == '\\') {
                p += 2;
                continue;
            }
            if (*p == '"') {
                return p + 1;
            }
            p++;
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

const char *wx_j_find(const char *s, const char *e, const char *key)
{
    const size_t kl = strlen(key);
    if (!s || !e || e <= s) {
        return NULL;
    }
    for (const char *p = s; p + kl + 2 < e; p++) {
        if (*p != '"' || memcmp(p + 1, key, kl) != 0 || p[1 + kl] != '"') {
            continue;
        }
        const char *q = wx_j_ws(p + 2 + kl, e);
        if (q >= e || *q != ':') {
            continue;
        }
        return wx_j_ws(q + 1, e);
    }
    return NULL;
}

bool wx_j_block(const char *s, const char *e, const char *key, const char **os, const char **oe)
{
    const char *v = wx_j_find(s, e, key);
    if (!v || v >= e || (*v != '{' && *v != '[')) {
        return false;
    }
    const char *end = wx_j_skip(v, e);
    /* un bloque sin cerrar (respuesta cortada) no vale */
    if (end <= v + 1 || end[-1] != (*v == '{' ? '}' : ']')) {
        return false;
    }
    *os = v + 1;
    *oe = end - 1;
    return true;
}

bool wx_j_num(const char *p, const char *e, float *out)
{
    if (!p || p >= e || *p == 'n' || *p == 'N') { /* null != 0 */
        return false;
    }
    char *endp = NULL;
    const float v = strtof(p, &endp);
    if (endp == p) {
        return false;
    }
    *out = v;
    return true;
}

bool wx_j_int(const char *p, const char *e, int32_t *out)
{
    if (!p || p >= e || *p == 'n' || *p == 'N') {
        return false;
    }
    char *endp = NULL;
    const long long v = strtoll(p, &endp, 10);
    if (endp == p) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

static bool j_numd(const char *p, const char *e, double *out)
{
    if (!p || p >= e || *p == 'n' || *p == 'N') {
        return false;
    }
    char *endp = NULL;
    const double v = strtod(p, &endp); /* doble: los unix no caben en un float */
    if (endp == p) {
        return false;
    }
    *out = v;
    return true;
}

bool wx_j_str(const char *p, const char *e, char *out, size_t n)
{
    if (!p || p >= e || *p != '"' || n == 0) {
        return false;
    }
    p++;
    size_t o = 0;
    while (p < e && *p != '"') {
        uint32_t cp;
        if (*p == '\\') {
            p++;
            if (p >= e) {
                break;
            }
            switch (*p) {
            case 'n': cp = '\n'; p++; break;
            case 't': cp = '\t'; p++; break;
            case 'r': cp = '\r'; p++; break;
            case 'b':
            case 'f': cp = ' '; p++; break;
            case 'u':
                if (p + 4 >= e) {
                    return false;
                }
                cp = 0;
                for (int i = 1; i <= 4; i++) {
                    const char c = p[i];
                    cp <<= 4;
                    if (c >= '0' && c <= '9') {
                        cp |= (uint32_t)(c - '0');
                    } else if (c >= 'a' && c <= 'f') {
                        cp |= (uint32_t)(c - 'a' + 10);
                    } else if (c >= 'A' && c <= 'F') {
                        cp |= (uint32_t)(c - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                p += 5;
                break;
            default: cp = (uint8_t)*p; p++; break;
            }
        } else {
            cp = (uint8_t)*p++;
            if (cp >= 0x80) { /* ya viene en UTF-8: tal cual */
                if (o + 1 >= n) {
                    break;
                }
                out[o++] = (char)cp;
                continue;
            }
        }
        if (cp < 0x80) {
            if (o + 1 >= n) {
                break;
            }
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            if (o + 2 >= n) {
                break;
            }
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 3 >= n) {
                break;
            }
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    /* RELOJ: si se recortó a mitad de un carácter UTF-8, se quita el resto */
    size_t i = o;
    while (i > 0 && ((uint8_t)out[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i > 0) {
        const uint8_t b = (uint8_t)out[i - 1];
        const size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (o - (i - 1) < need) {
            o = i - 1;
        }
    }
    out[o] = 0;
    return true;
}

/* Arrays: se escriben directamente en los registros (base + i*stride) con un
 * setter por variable; ni un array temporal de 48 números en la pila. */
typedef void (*wx_set_t)(void *rec, double v, bool ok);

static int arr_rec(const char *s, const char *e, const char *key, void *base, size_t stride, int maxn, wx_set_t set)
{
    const char *as = NULL, *ae = NULL;
    if (!wx_j_block(s, e, key, &as, &ae)) {
        return 0;
    }
    const char *p = wx_j_ws(as, ae);
    int i = 0;
    while (p < ae && i < maxn) {
        double v = 0;
        const bool ok = j_numd(p, ae, &v);
        set((uint8_t *)base + (size_t)i * stride, v, ok);
        i++;
        while (p < ae && *p != ',') {
            p++;
        }
        if (p < ae) {
            p++;
        }
        p = wx_j_ws(p, ae);
    }
    return i;
}

/* ======================= Setters ======================= */

static int16_t t10(double v, bool ok)
{
    if (!ok) {
        return WX_NOVAL_I16;
    }
    double x = v * 10.0;
    x = x > 32000.0 ? 32000.0 : x < -32000.0 ? -32000.0 : x;
    return (int16_t)lround(x);
}

static uint8_t pct(double v, bool ok)
{
    if (!ok) {
        return WX_NOVAL_U8;
    }
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    return (uint8_t)lround(v);
}

static uint8_t code8(double v, bool ok)
{
    return ok ? (uint8_t)((v < 0 || v > 254) ? 3 : (int)v) : 3;
}

static uint8_t uv10(double v, bool ok)
{
    if (!ok) {
        return WX_NOVAL_U8;
    }
    double x = (v < 0 ? 0 : v) * 10.0;
    return (uint8_t)lround(x > 250.0 ? 250.0 : x);
}

static uint16_t u16s(double x, bool ok)
{
    if (!ok) {
        return WX_NOVAL_U16;
    }
    x = x < 0 ? 0 : x > 65000.0 ? 65000.0 : x;
    return (uint16_t)lround(x);
}

#define HR(r) ((wx_hour_t *)(r))
#define DY(r) ((wx_day_t *)(r))
static void set_h_time(void *r, double v, bool ok) { HR(r)->t = ok ? (int32_t)v : 0; }
static void set_h_temp(void *r, double v, bool ok) { HR(r)->temp10 = t10(v, ok); }
static void set_h_feels(void *r, double v, bool ok) { HR(r)->feels10 = t10(v, ok); }
static void set_h_wind(void *r, double v, bool ok) { HR(r)->wind10 = t10(v, ok); }
static void set_h_rh(void *r, double v, bool ok) { HR(r)->rh = pct(v, ok); }
static void set_h_pop(void *r, double v, bool ok) { HR(r)->pop = pct(v, ok); }
static void set_h_code(void *r, double v, bool ok) { HR(r)->code = code8(v, ok); }
static void set_h_prec(void *r, double v, bool ok) { HR(r)->precip100 = u16s(v * 100.0, ok); }
static void set_h_uv(void *r, double v, bool ok) { HR(r)->uv10 = uv10(v, ok); }
static void set_h_vis(void *r, double v, bool ok) { HR(r)->vis100 = u16s(v / 100.0, ok); } /* m -> centenas */
static void set_d_date(void *r, double v, bool ok) { DY(r)->date = ok ? (int32_t)v : 0; }
static void set_d_rise(void *r, double v, bool ok) { DY(r)->sunrise = ok ? (int32_t)v : 0; }
static void set_d_set(void *r, double v, bool ok) { DY(r)->sunset = ok ? (int32_t)v : 0; }
static void set_d_max(void *r, double v, bool ok) { DY(r)->max10 = t10(v, ok); }
static void set_d_min(void *r, double v, bool ok) { DY(r)->min10 = t10(v, ok); }
static void set_d_code(void *r, double v, bool ok) { DY(r)->code = code8(v, ok); }
static void set_d_pop(void *r, double v, bool ok) { DY(r)->pop = pct(v, ok); }
static void set_d_uv(void *r, double v, bool ok) { DY(r)->uv10 = uv10(v, ok); }

/* ======================= Pronóstico ======================= */

int wx_parse_forecast(const char *s, size_t len, const wx_loc_t *loc, wx_data_t *w)
{
    if (!s || !w || !loc) {
        return WXE_JSON;
    }
    const char *e = s + len;
    /* una respuesta completa termina en '}': caza la conexión que se corta */
    const char *z = e;
    while (z > s && (z[-1] == '\n' || z[-1] == '\r' || z[-1] == ' ' || z[-1] == '\t')) {
        z--;
    }
    if (z == s || z[-1] != '}') {
        return WXE_JSON;
    }
    memset(w, 0, sizeof(*w));
    w->magic = WX_MAGIC;
    w->ver = WX_VER;
    w->loc = *loc;

    float f;
    int32_t iv;
    const char *p = wx_j_find(s, e, "utc_offset_seconds");
    if (p && wx_j_int(p, e, &iv)) {
        w->utc_offset = iv;
    }
    const char *cs = NULL, *ce = NULL;
    if (!wx_j_block(s, e, "current", &cs, &ce)) {
        return WXE_JSON;
    }
    p = wx_j_find(cs, ce, "time");
    if (!p || !wx_j_int(p, ce, &w->obs_time)) {
        return WXE_JSON;
    }
    p = wx_j_find(cs, ce, "temperature_2m");
    if (!p || !wx_j_num(p, ce, &w->temp)) {
        return WXE_JSON; /* sin temperatura no hay nada que enseñar */
    }
    p = wx_j_find(cs, ce, "weather_code");
    w->code = (p && wx_j_num(p, ce, &f)) ? code8(f, true) : 3;
    p = wx_j_find(cs, ce, "is_day");
    w->is_day = (p && wx_j_num(p, ce, &f)) ? (f > 0.5f) : 1;
    static const struct {
        const char *key;
        size_t off;
        uint16_t flag;
    } kCur[] = {
        {"apparent_temperature", offsetof(wx_data_t, feels), WXF_FEELS},
        {"relative_humidity_2m", offsetof(wx_data_t, humidity), WXF_HUMIDITY},
        {"precipitation", offsetof(wx_data_t, precip), WXF_PRECIP},
        {"cloud_cover", offsetof(wx_data_t, cloud), WXF_CLOUD},
        {"pressure_msl", offsetof(wx_data_t, pressure), WXF_PRESSURE},
        {"wind_speed_10m", offsetof(wx_data_t, wind_speed), WXF_WIND},
        {"wind_gusts_10m", offsetof(wx_data_t, wind_gust), WXF_GUST},
    };
    for (size_t i = 0; i < sizeof(kCur) / sizeof(kCur[0]); i++) {
        p = wx_j_find(cs, ce, kCur[i].key);
        if (p && wx_j_num(p, ce, (float *)((uint8_t *)w + kCur[i].off))) {
            w->have |= kCur[i].flag;
        }
    }
    p = wx_j_find(cs, ce, "wind_direction_10m");
    if (p && wx_j_num(p, ce, &f)) {
        w->wind_dir = (int16_t)lroundf(f);
        w->have |= WXF_WINDDIR;
    }

    const char *hs = NULL, *he = NULL;
    if (wx_j_block(s, e, "hourly", &hs, &he)) {
        wx_hour_t *h = w->hours;
        const size_t st = sizeof(wx_hour_t);
        /* RELOJ: un array que falta o llega más corto que "time" deja sus
         * huecos como "no disponible", no como un 0 inventado */
        for (int i = 0; i < WX_HOURS; i++) {
            h[i].temp10 = h[i].feels10 = h[i].wind10 = WX_NOVAL_I16;
            h[i].vis100 = h[i].precip100 = WX_NOVAL_U16;
            h[i].pop = h[i].rh = h[i].uv10 = WX_NOVAL_U8;
            h[i].code = 3;
        }
        int n = arr_rec(hs, he, "time", h, st, WX_HOURS, set_h_time);
        arr_rec(hs, he, "temperature_2m", h, st, WX_HOURS, set_h_temp);
        arr_rec(hs, he, "apparent_temperature", h, st, WX_HOURS, set_h_feels);
        arr_rec(hs, he, "relative_humidity_2m", h, st, WX_HOURS, set_h_rh);
        arr_rec(hs, he, "precipitation", h, st, WX_HOURS, set_h_prec);
        arr_rec(hs, he, "precipitation_probability", h, st, WX_HOURS, set_h_pop);
        arr_rec(hs, he, "weather_code", h, st, WX_HOURS, set_h_code);
        arr_rec(hs, he, "wind_speed_10m", h, st, WX_HOURS, set_h_wind);
        arr_rec(hs, he, "uv_index", h, st, WX_HOURS, set_h_uv);
        arr_rec(hs, he, "visibility", h, st, WX_HOURS, set_h_vis);
        /* alineado a la hora EN CURSO: el bloque que contiene obs_time */
        int i0 = 0;
        for (int i = 0; i < n; i++) {
            if (w->hours[i].t + 3600 > w->obs_time) {
                i0 = i;
                break;
            }
            i0 = i + 1;
        }
        if (i0 > 0 && i0 < n) {
            memmove(w->hours, w->hours + i0, (size_t)(n - i0) * sizeof(wx_hour_t));
            n -= i0;
        } else if (i0 >= n) {
            n = 0; /* todas pasadas: no se publica un panel viejo */
        }
        w->hour_count = (uint8_t)n;
        if (n > 0) {
            if (w->hours[0].uv10 != WX_NOVAL_U8) {
                w->uv = w->hours[0].uv10 / 10.0f;
                w->have |= WXF_UV;
            }
            if (w->hours[0].vis100 != WX_NOVAL_U16) {
                w->visibility = w->hours[0].vis100 * 100.0f;
                w->have |= WXF_VIS;
            }
            if (w->hours[0].pop != WX_NOVAL_U8) {
                w->pop = w->hours[0].pop;
                w->have |= WXF_POP;
            }
        }
    }

    const char *ds = NULL, *de = NULL;
    if (wx_j_block(s, e, "daily", &ds, &de)) {
        wx_day_t *d = w->days;
        const size_t st = sizeof(wx_day_t);
        for (int i = 0; i < WX_DAYS; i++) {
            d[i].max10 = d[i].min10 = WX_NOVAL_I16;
            d[i].pop = d[i].uv10 = WX_NOVAL_U8;
            d[i].code = 3;
        }
        const int n = arr_rec(ds, de, "time", d, st, WX_DAYS, set_d_date);
        arr_rec(ds, de, "weather_code", d, st, WX_DAYS, set_d_code);
        arr_rec(ds, de, "temperature_2m_max", d, st, WX_DAYS, set_d_max);
        arr_rec(ds, de, "temperature_2m_min", d, st, WX_DAYS, set_d_min);
        arr_rec(ds, de, "sunrise", d, st, WX_DAYS, set_d_rise);
        arr_rec(ds, de, "sunset", d, st, WX_DAYS, set_d_set);
        arr_rec(ds, de, "uv_index_max", d, st, WX_DAYS, set_d_uv);
        arr_rec(ds, de, "precipitation_probability_max", d, st, WX_DAYS, set_d_pop);
        w->day_count = (uint8_t)n;
        if (n > 0) {
            if (w->days[0].max10 != WX_NOVAL_I16 && w->days[0].min10 != WX_NOVAL_I16) {
                w->tmax = w->days[0].max10 / 10.0f;
                w->tmin = w->days[0].min10 / 10.0f;
                w->have |= WXF_MINMAX;
            }
            if (w->days[0].sunrise && w->days[0].sunset) {
                w->sunrise = w->days[0].sunrise;
                w->sunset = w->days[0].sunset;
                w->have |= WXF_SUN;
            }
            if (!(w->have & WXF_UV) && w->days[0].uv10 != WX_NOVAL_U8) {
                w->uv = w->days[0].uv10 / 10.0f;
                w->have |= WXF_UV;
            }
        }
    }
    return WXE_NONE;
}

/* ======================= Geocoding ======================= */

int wx_parse_geo(const char *s, size_t len, wx_loc_t *res, int max)
{
    if (!s || !res || max <= 0) {
        return 0;
    }
    const char *e = s + len, *rs = NULL, *re = NULL;
    if (!wx_j_block(s, e, "results", &rs, &re)) {
        return 0;
    }
    int cnt = 0;
    const char *p = wx_j_ws(rs, re);
    while (p < re && cnt < max) {
        if (*p != '{') {
            p++;
            continue;
        }
        const char *oe = wx_j_skip(p, re), *os = p + 1;
        const char *ie = oe > os ? oe - 1 : os;
        wx_loc_t L;
        memset(&L, 0, sizeof(L));
        const char *q = wx_j_find(os, ie, "name");
        const bool ok_name = q && wx_j_str(q, ie, L.name, sizeof(L.name));
        float lat = 0, lon = 0;
        q = wx_j_find(os, ie, "latitude");
        const bool ok_lat = q && wx_j_num(q, ie, &lat);
        q = wx_j_find(os, ie, "longitude");
        const bool ok_lon = q && wx_j_num(q, ie, &lon);
        if (ok_name && ok_lat && ok_lon && L.name[0] && lat >= -90.0f && lat <= 90.0f && lon >= -180.0f &&
            lon <= 180.0f) {
            L.lat = lat;
            L.lon = lon;
            char a1[WX_REGION] = "", co[WX_REGION] = "", reg[WX_REGION * 2 + 4] = "";
            q = wx_j_find(os, ie, "admin1");
            if (q) {
                wx_j_str(q, ie, a1, sizeof(a1));
            }
            q = wx_j_find(os, ie, "country");
            if (q) {
                wx_j_str(q, ie, co, sizeof(co));
            }
            if (a1[0] && co[0]) {
                snprintf(reg, sizeof(reg), "%s, %s", a1, co);
            } else {
                snprintf(reg, sizeof(reg), "%s", co[0] ? co : a1);
            }
            /* recorte explícito y sin partir un carácter */
            size_t k = strlen(reg);
            if (k >= sizeof(L.region)) {
                k = sizeof(L.region) - 1;
                while (k > 0 && ((uint8_t)reg[k] & 0xC0) == 0x80) {
                    k--;
                }
            }
            memcpy(L.region, reg, k);
            L.region[k] = 0;
            res[cnt++] = L;
        }
        p = wx_j_ws(oe, re);
        while (p < re && *p == ',') {
            p = wx_j_ws(p + 1, re);
        }
    }
    return cnt;
}

/* ======================= URLs ======================= */

bool wx_url_forecast(const wx_loc_t *loc, char *out, size_t n)
{
    if (!loc || !out) {
        return false;
    }
    const int k = snprintf(out, n,
                           WX_HOST_FORECAST "?latitude=%.4f&longitude=%.4f&timezone=auto&timeformat=unixtime"
                                            "&wind_speed_unit=kmh&forecast_days=%d&forecast_hours=%d"
                                            "&current=" WX_CUR_VARS "&hourly=" WX_HR_VARS "&daily=" WX_DY_VARS,
                           (double)loc->lat, (double)loc->lon, WX_DAYS, WX_HOURS);
    return k > 0 && (size_t)k < n;
}

void wx_url_encode(const char *in, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    if (!out || n == 0) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && o + 4 < n; p++) {
        const unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

bool wx_url_geo(const char *query, char *out, size_t n)
{
    char enc[WX_QUERY * 3 + 4];
    wx_url_encode(query, enc, sizeof(enc));
    if (!enc[0] || !out) {
        return false;
    }
    const int k = snprintf(out, n, WX_HOST_GEO "?name=%s&count=%d&language=es&format=json", enc, WX_RESULTS);
    return k > 0 && (size_t)k < n;
}

/* ======================= Persistencia ======================= */

bool wx_cache_ok(wx_data_t *w, size_t n)
{
    if (!w || n != sizeof(*w) || w->magic != WX_MAGIC || w->ver != WX_VER || w->hour_count > WX_HOURS ||
        w->day_count > WX_DAYS || w->loc.name[0] == 0) {
        return false;
    }
    w->loc.name[WX_NAME - 1] = 0;
    w->loc.region[WX_REGION - 1] = 0;
    return true;
}

static bool loc_valid(const wx_loc_t *l)
{
    return l->name[0] && l->lat >= -90.0f && l->lat <= 90.0f && l->lon >= -180.0f && l->lon <= 180.0f;
}

uint8_t wx_locs_sanitize(wx_loc_t *locs, uint8_t n, int8_t *sel)
{
    if (n > WX_LOCS) {
        n = 0;
    }
    for (uint8_t i = 0; i < n; i++) {
        locs[i].name[WX_NAME - 1] = 0;
        locs[i].region[WX_REGION - 1] = 0;
        if (!loc_valid(&locs[i])) {
            n = 0; /* unas prefs corruptas no dejan la app apuntando a la nada */
            break;
        }
    }
    if (sel) {
        if (n == 0) {
            *sel = -1;
        } else if (*sel >= (int8_t)n) {
            *sel = 0;
        }
    }
    return n;
}

bool wx_loc_same(const wx_loc_t *a, const wx_loc_t *b)
{
    return a && b && strncmp(a->name, b->name, WX_NAME) == 0 && fabsf(a->lat - b->lat) < 0.01f &&
           fabsf(a->lon - b->lon) < 0.01f;
}
