/*
 * ui_compass — app Flex Compass (rediseño 2026-10, diseño del usuario:
 * design/referencias/brujula.png).
 *
 * El QMI8658 del reloj tiene acelerómetro y giroscopio pero NO magnetómetro,
 * ni el reloj barómetro o GPS: aquí no hay Norte, ni altitud, ni coordenadas,
 * y no se inventan. La referencia de diseño enseña "315° NW", "ELEVATION",
 * "LAT" y "LNG"; esta pantalla conserva su aspecto (bisel con grados que gira,
 * anillo naranja con datos, aguja roja y blanca, cuña roja) con lo que el
 * reloj SÍ mide:
 *  - RUMBO RELATIVO: cuánto ha girado el reloj alrededor de la vertical desde
 *    una referencia que fija el usuario (al abrir la app, o manteniendo
 *    pulsada la esfera), integrando el giroscopio proyectado sobre la gravedad
 *    (watch_imu, imu_heading.c). Arriba, "90° REL"; el bisel gira para que ese
 *    valor quede bajo la marca superior y la aguja roja señala la referencia
 *    ("REF" en el bisel, donde la referencia pondría "N").
 *  - La cuña roja es la incertidumbre (± la deriva estimada del giroscopio,
 *    que crece al moverse) alrededor de la aguja.
 *  - Anillo naranja: inclinación y alabeo reales del acelerómetro, la deriva y
 *    "SIN NORTE" (en lugar de elevación, latitud y longitud).
 * Sin datos válidos no hay aguja ni cuña y los valores son "--".
 * El chevrón de abajo abre los detalles: "Fijar referencia", el requisito del
 * magnetómetro (textos de la referencia Flex OS Ultra, AppCompass.h:683-709) y
 * el estado real de cada sensor.
 *
 * Dibujo: la esfera es UN objeto con su propio callback de dibujo que calcula
 * las marcas y las letras ya giradas (como lv_arclabel por dentro): sin
 * capturas ni búferes grandes, sin capas por cuadro y sin renderizar nada en
 * el evento táctil que abre la app. Solo se invalida cuando cambia lo que se
 * ve (grado entero del rumbo con histéresis, deriva o un texto del anillo):
 * quieto, nada se redibuja. Cada franja de render solo recibe las tareas de
 * dibujo que la tocan.
 * El IMU solo mide mientras la app está abierta y a la vista
 * (watch_imu_set_active con recuento; se suelta si la app se suspende).
 */
#include "ui_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qmi8658.h"
#include "ui_logic.h"
#include "watch_imu.h"
#include "watch_settings.h"
#include "watch_theme.h"
#include "watch_time.h"

#define CMP_TICK_MS 100
/* Geometría medida en la referencia escalada a 410x502 */
#define DIAL_R      176                  /* borde exterior del bisel */
#define DIAL_D      (2 * DIAL_R)
#define DIAL_CX     205
#define DIAL_CY     262
#define BAND_W      15                   /* banda gris del bisel */
#define NUM_R       136.0f               /* línea base de los números del bisel */
#define CARD_R      153.0f               /* línea base de REF/90/180/270 */
#define RING_R      130                  /* anillo naranja: radio exterior */
#define RING_W      19
#define RING_IN     (RING_R - RING_W)    /* 111 */
#define WEDGE_R     108
#define NEEDLE_N    104.0f               /* punta roja (hacia la referencia) */
#define NEEDLE_S    112.0f               /* punta blanca */
#define HDR_Y       14
#define YAW_HYST    0.75f                /* grados: el entero mostrado no tiembla */
#define DEG         (3.14159265f / 180.0f)

#define C_TICK      0x9A9A9A
#define C_HATCH     0x5C5C60
#define C_NUM       0xB8B8B8
#define C_RING_TICK 0xB33100
#define C_RING_TXT  0x1A1A1A
#define C_CROSS     0x6E6E6E
#define C_NEEDLE_N  0xF03A20
#define C_NEEDLE_NE 0x8E1208

enum { TXT_TL = 0, TXT_TR, TXT_BL, TXT_BR, TXT_N };

typedef struct {
    lv_obj_t *dial, *heading, *time;
    int yaw;        /* grado entero mostrado; -1 sin rumbo */
    float yaw_f;    /* valor del que sale (histéresis) */
    int drift;      /* ± grados (0 sin rumbo) */
    char txt[TXT_N][48];
    int64_t time_key;
    bool imu_on;
} cmp_page_t;

static cmp_page_t *s_live; /* para las pruebas */

const char *ui_compass_ring_text(int i)
{
    return s_live && i >= 0 && i < TXT_N ? s_live->txt[i] : NULL;
}

/* ---- Dibujo de la esfera ---- */

typedef struct {
    lv_layer_t *layer;
    int32_t cx, cy;
} dctx_t;

/* ¿Toca el rectángulo la franja que se está dibujando? */
static bool in_clip(const dctx_t *d, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    const lv_area_t *c = &d->layer->_clip_area;
    return !(x2 < c->x1 || x1 > c->x2 || y2 < c->y1 || y1 > c->y2);
}

/* Ángulo "de brújula" (0 = arriba, en el sentido de las agujas) a punto. */
static void polar(const dctx_t *d, float top_deg, float r, float *x, float *y)
{
    const float a = (top_deg - 90.0f) * DEG;
    *x = (float)d->cx + cosf(a) * r;
    *y = (float)d->cy + sinf(a) * r;
}

static void seg(const dctx_t *d, float x1, float y1, float x2, float y2, int32_t w, uint32_t rgb, bool round)
{
    const int32_t m = w + 1;
    if (!in_clip(d, (int32_t)fminf(x1, x2) - m, (int32_t)fminf(y1, y2) - m, (int32_t)fmaxf(x1, x2) + m,
                 (int32_t)fmaxf(y1, y2) + m)) {
        return;
    }
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.p1.x = (lv_value_precise_t)lroundf(x1);
    l.p1.y = (lv_value_precise_t)lroundf(y1);
    l.p2.x = (lv_value_precise_t)lroundf(x2);
    l.p2.y = (lv_value_precise_t)lroundf(y2);
    l.width = w;
    l.color = flex_color(rgb);
    l.round_start = round;
    l.round_end = round;
    lv_draw_line(d->layer, &l);
}

static void ray(const dctx_t *d, float top_deg, float r1, float r2, int32_t w, uint32_t rgb)
{
    float x1, y1, x2, y2;
    polar(d, top_deg, r1, &x1, &y1);
    polar(d, top_deg, r2, &x2, &y2);
    seg(d, x1, y1, x2, y2, w, rgb, false);
}

static void ring(const dctx_t *d, int32_t r, int32_t w, float a1, float a2, uint32_t rgb, lv_opa_t opa)
{
    if (!in_clip(d, d->cx - r, d->cy - r, d->cx + r, d->cy + r)) {
        return;
    }
    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = d->cx;
    a.center.y = d->cy;
    a.radius = (uint16_t)r;
    a.width = w;
    a.start_angle = (lv_value_precise_t)a1;
    a.end_angle = (lv_value_precise_t)a2;
    a.color = flex_color(rgb);
    a.opa = opa;
    lv_draw_arc(d->layer, &a);
}

/* UTF-8 (hasta 3 bytes: basta para el español y °, ±) */
static uint32_t next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) {
        cp = p[0];
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else {
        cp = '?';
        n = 1;
    }
    *s += n;
    return cp;
}

/* Texto curvo centrado en top_deg con la línea base en el radio r.
 * up: se lee en el sentido de las agujas con la cabeza hacia fuera (arriba
 * del anillo); si no, al revés, con la cabeza hacia el centro (abajo: se lee
 * derecho). */
static void arc_text(const dctx_t *d, const char *txt, const lv_font_t *f, uint32_t rgb, float top_deg, float r,
                     bool up)
{
    /* Anchos con el interletraje (kerning) de cada par, como una etiqueta */
    float len = 0;
    for (const char *s = txt; *s;) {
        const uint32_t cp = next_cp(&s);
        const char *t = s;
        len += (float)lv_font_get_glyph_width(f, cp, *t ? next_cp(&t) : 0);
    }
    const int32_t lh = lv_font_get_line_height(f);
    float pos = 0;
    for (const char *s = txt; *s;) {
        const uint32_t cp = next_cp(&s);
        const char *t = s;
        const float w = (float)lv_font_get_glyph_width(f, cp, *t ? next_cp(&t) : 0);
        const float off = (pos + w / 2 - len / 2) / r / DEG; /* grados */
        const float a = top_deg + (up ? off : -off);
        pos += w;
        if (cp == ' ') {
            continue;
        }
        float x, y;
        polar(d, a, r, &x, &y);
        if (!in_clip(d, (int32_t)x - lh, (int32_t)y - lh, (int32_t)x + lh, (int32_t)y + lh)) {
            continue;
        }
        lv_draw_letter_dsc_t g;
        lv_draw_letter_dsc_init(&g);
        g.font = f;
        g.color = flex_color(rgb);
        g.unicode = cp;
        g.rotation = (int32_t)lroundf((up ? a : a + 180.0f) * 10.0f);
        const lv_point_t pt = {(int32_t)lroundf(x), (int32_t)lroundf(y)};
        lv_draw_letter(d->layer, &g, &pt);
    }
}

static void disc(const dctx_t *d, int32_t r, uint32_t rgb)
{
    ring(d, r, r, 0, 360, rgb, LV_OPA_COVER);
}

static void dial_draw_cb(lv_event_t *e)
{
    cmp_page_t *p = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const dctx_t d = {lv_event_get_layer(e), c.x1 + DIAL_R, c.y1 + DIAL_R};
    const float rot = p->yaw >= 0 ? (float)p->yaw : 0.0f; /* el bisel gira al revés que el reloj */

    /* Bisel: banda gris rayada, marcas, números y REF/90/180/270 */
    ring(&d, DIAL_R, BAND_W, 0, 360, FLEX_CMP_BEZEL, LV_OPA_COVER);
    for (int v = 0; v < 360; v += 3) {
        ray(&d, (float)v - rot, DIAL_R - BAND_W + 1, DIAL_R - 1, 1, C_HATCH);
    }
    for (int v = 0; v < 360; v += 5) {
        const float a = (float)v - rot;
        if (v % 90 == 0) {
            continue; /* ahí va el rótulo */
        }
        if (v % 30 == 0) {
            ray(&d, a, NUM_R + 16, DIAL_R, 3, FLEX_C_WHITE);
        } else {
            ray(&d, a, v % 10 == 0 ? 150 : 153, 158, v % 10 == 0 ? 2 : 1, C_TICK);
        }
    }
    char b[12];
    for (int v = 30; v < 360; v += 30) {
        if (v % 90 == 0) {
            continue;
        }
        snprintf(b, sizeof(b), "%d", v);
        arc_text(&d, b, FLEX_TYPE_FOOTNOTE, C_NUM, (float)v - rot, NUM_R, true);
    }
    static const char *const kCard[4] = {"REF", "90", "180", "270"};
    for (int k = 0; k < 4; k++) {
        arc_text(&d, kCard[k], FLEX_TYPE_LABEL, FLEX_C_WHITE, (float)(k * 90) - rot, CARD_R, true);
    }

    /* Anillo naranja (fijo) con sus marcas, puntos y datos reales */
    ring(&d, RING_R, RING_W, 0, 360, FLEX_CMP_ORANGE, LV_OPA_COVER);
    for (int v = 0; v < 360; v += 3) {
        ray(&d, (float)v, RING_IN, RING_IN + 5, 1, C_RING_TICK);
    }
    for (int k = 0; k < 4; k++) {
        float x, y;
        polar(&d, (float)(k * 90), RING_R - RING_W / 2.0f, &x, &y);
        if (in_clip(&d, (int32_t)x - 4, (int32_t)y - 4, (int32_t)x + 4, (int32_t)y + 4)) {
            lv_draw_rect_dsc_t r;
            lv_draw_rect_dsc_init(&r);
            r.radius = LV_RADIUS_CIRCLE;
            r.bg_color = flex_color(C_RING_TXT);
            const lv_area_t a = {(int32_t)x - 3, (int32_t)y - 3, (int32_t)x + 3, (int32_t)y + 3};
            lv_draw_rect(d.layer, &r, &a);
        }
    }
    const float base_up = RING_IN + 4.0f, base_dn = RING_R - 4.0f;
    arc_text(&d, p->txt[TXT_TL], FLEX_TYPE_FOOTNOTE, C_RING_TXT, -45.0f, base_up, true);
    arc_text(&d, p->txt[TXT_TR], FLEX_TYPE_FOOTNOTE, C_RING_TXT, 45.0f, base_up, true);
    arc_text(&d, p->txt[TXT_BR], FLEX_TYPE_FOOTNOTE, C_RING_TXT, 135.0f, base_dn, false);
    arc_text(&d, p->txt[TXT_BL], FLEX_TYPE_FOOTNOTE, C_RING_TXT, 225.0f, base_dn, false);

    /* Retícula */
    seg(&d, (float)(d.cx - RING_IN), (float)d.cy, (float)(d.cx + RING_IN), (float)d.cy, 2, C_CROSS, false);
    seg(&d, (float)d.cx, (float)(d.cy - RING_IN), (float)d.cx, (float)(d.cy + RING_IN), 2, C_CROSS, false);

    /* Cuña de incertidumbre y aguja: hacia la referencia (0° del bisel) */
    if (p->yaw >= 0) {
        const float n = -rot; /* hacia dónde queda la referencia */
        /* El mismo ángulo en la convención de LVGL (0 = a la derecha), en 0..360:
         * lv_draw_arc no admite ángulos negativos (dibujaría el círculo entero) */
        const float lv = fmodf(n - 90.0f + 720.0f, 360.0f);
        ring(&d, WEDGE_R, WEDGE_R, fmodf(lv - (float)p->drift + 360.0f, 360.0f), fmodf(lv + (float)p->drift, 360.0f),
             FLEX_CMP_RED, LV_OPA_40);
        float tx, ty, sx, sy;
        polar(&d, n, NEEDLE_N, &tx, &ty);
        polar(&d, n + 180.0f, NEEDLE_S, &sx, &sy);
        const float cx = (float)d.cx, cy = (float)d.cy;
        seg(&d, cx, cy, sx, sy, 13, FLEX_C_WHITE, true);
        seg(&d, cx, cy, sx, sy, 5, 0x111111, true);
        seg(&d, cx, cy, tx, ty, 13, C_NEEDLE_NE, true);
        seg(&d, cx, cy, tx, ty, 7, C_NEEDLE_N, true);
    }
    /* Eje */
    disc(&d, 18, 0x0A0A0A);
    ring(&d, 18, 4, 0, 360, FLEX_C_WHITE, LV_OPA_COVER);
    disc(&d, 7, 0x8A8A8A);
}

/* ---- Datos ---- */

static float ang_diff(float a, float b)
{
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
    return d < 0 ? -d : d;
}

static bool set_txt(char *dst, size_t n, const char *src)
{
    if (strcmp(dst, src) == 0) {
        return false;
    }
    snprintf(dst, n, "%s", src);
    return true;
}

static void set_label(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void refresh(cmp_page_t *p)
{
    watch_imu_snapshot_t im;
    const bool have = watch_imu_get(&im);
    const bool live = have && im.state == IMU_RUNNING && im.samples > 0;
    const bool valid = live && im.heading.valid;
    bool dirty = false;

    int yaw = -1, drift = 0;
    if (valid) {
        const float y = fmodf(im.heading.yaw + 360.0f, 360.0f);
        if (p->yaw < 0 || ang_diff(y, p->yaw_f) >= YAW_HYST) {
            p->yaw_f = y;
            yaw = (int)lroundf(y) % 360;
        } else {
            yaw = p->yaw;
        }
        drift = (int)lroundf(im.heading.drift);
        drift = drift < 1 ? 1 : (drift > 60 ? 60 : drift);
    }
    if (yaw != p->yaw || drift != p->drift) {
        p->yaw = yaw;
        p->drift = drift;
        dirty = true;
    }

    char b[48]; /* del tamaño de txt[]: snprintf no puede truncar */
    int32_t pitch, roll;
    const bool tilt = live && qmi_tilt(im.acc_mg, &pitch, &roll);
    if (tilt) {
        snprintf(b, sizeof(b), "INCLINACIÓN %ld°", (long)lroundf((float)pitch / 10.0f));
    } else {
        snprintf(b, sizeof(b), "INCLINACIÓN --");
    }
    dirty |= set_txt(p->txt[TXT_TL], sizeof(p->txt[0]), b);
    if (tilt) {
        snprintf(b, sizeof(b), "ALABEO %ld°", (long)lroundf((float)roll / 10.0f));
    } else {
        snprintf(b, sizeof(b), "ALABEO --");
    }
    dirty |= set_txt(p->txt[TXT_TR], sizeof(p->txt[0]), b);
    if (valid) {
        snprintf(b, sizeof(b), "DERIVA ±%d°", drift);
    } else {
        snprintf(b, sizeof(b), "DERIVA --");
    }
    dirty |= set_txt(p->txt[TXT_BL], sizeof(p->txt[0]), b);
    dirty |= set_txt(p->txt[TXT_BR], sizeof(p->txt[0]), "SIN NORTE");
    if (dirty) {
        lv_obj_invalidate(p->dial);
    }

    if (valid) {
        snprintf(b, sizeof(b), "%d° REL", yaw);
    } else {
        snprintf(b, sizeof(b), "--° REL");
    }
    set_label(p->heading, b);

    wt_datetime_t dt;
    const watch_time_state_t st = watch_time_now(&dt);
    watch_settings_t ws;
    watch_settings_get(&ws);
    ui_face_time_t t;
    ui_face_time_fmt(st, st == WATCH_TIME_OK ? &dt : NULL, ws.h24, &t);
    if (t.key != p->time_key) {
        p->time_key = t.key;
        set_label(p->time, t.time);
    }
}

static void tick_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t));
}

static void delete_cb(lv_event_t *e)
{
    cmp_page_t *p = lv_event_get_user_data(e);
    if (p->imu_on) {
        watch_imu_set_active(false);
    }
    if (s_live == p) {
        s_live = NULL;
    }
    lv_free(p);
}

/* App suspendida (gesto hacia arriba): el IMU se suelta; al volver, se pide. */
static void suspend_cb(lv_event_t *e)
{
    cmp_page_t *p = lv_event_get_user_data(e);
    const bool on = lv_event_get_code(e) == (lv_event_code_t)ui_nav_event_resume();
    if (on != p->imu_on) {
        p->imu_on = on;
        watch_imu_set_active(on);
    }
}

static void reset_ref(void)
{
    watch_imu_heading_reset();
    ui_banner_show_info("Referencia fijada", "Ahora apunta a 0°");
}

static void dial_long_cb(lv_event_t *e)
{
    (void)e;
    reset_ref();
}

/* ---- Detalles: referencia, requisito del Norte y estado real de los sensores ---- */

typedef struct {
    lv_obj_t *acc, *gyr, *state, *head, *pitch, *roll, *deg, *unc;
} cmp_det_t;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void row_set(lv_obj_t *row, const char *v, uint32_t rgb)
{
    watch_theme_info_set(row, v);
    watch_theme_set_text_color(lv_obj_get_child(row, 1), rgb);
}

/* "-1,5°" desde décimas de grado */
static void fmt_d10(char *b, size_t n, int32_t d10)
{
    const int32_t a = d10 < 0 ? -d10 : d10;
    snprintf(b, n, "%s%ld,%ld°", d10 < 0 ? "-" : "", (long)(a / 10), (long)(a % 10));
}

static void det_refresh(cmp_det_t *p)
{
    watch_imu_snapshot_t im;
    const bool have = watch_imu_get(&im);
    const imu_state_t st = have ? im.state : IMU_ABSENT;
    const bool live = st == IMU_RUNNING && im.samples > 0;
    char b[64];
    if (live && im.heading.valid) {
        snprintf(b, sizeof(b), "%d°", (int)lroundf(fmodf(im.heading.yaw + 360.0f, 360.0f)) % 360);
        set_label(p->deg, b);
        snprintf(b, sizeof(b), "± %d° · %s", (int)lroundf(im.heading.drift),
                 im.heading.bias_ok ? "giroscopio calibrado" : "quieto 1 s para calibrar");
        set_label(p->unc, b);
        watch_theme_set_text_color(p->unc, im.heading.drift > 20.0f ? FLEX_C_WARN : FLEX_C_TXT2);
        row_set(p->head, "Relativo (sin Norte)", FLEX_C_TXT2);
    } else {
        set_label(p->deg, "--");
        set_label(p->unc, live ? "Sin vertical clara: sujeta el reloj" : "Esperando al IMU");
        row_set(p->head, "No disponible", FLEX_C_MUTE);
    }
    const char *stxt = st == IMU_RUNNING ? "Conectado"
                       : st == IMU_STARTING || st == IMU_IDLE ? "Detectando"
                       : st == IMU_ERROR ? "Error del bus" : "IMU no detectada";
    row_set(p->state, stxt, live ? FLEX_C_OK : FLEX_C_MUTE);
    row_set(p->acc, live ? "Activo" : "No disponible", live ? FLEX_C_OK : FLEX_C_MUTE);
    row_set(p->gyr, live ? "Activo" : "No disponible", live ? FLEX_C_OK : FLEX_C_MUTE);
    int32_t pitch, roll;
    if (live && qmi_tilt(im.acc_mg, &pitch, &roll)) {
        fmt_d10(b, sizeof(b), pitch);
        row_set(p->pitch, b, FLEX_C_TXT);
        fmt_d10(b, sizeof(b), roll);
        row_set(p->roll, b, FLEX_C_TXT);
    } else {
        row_set(p->pitch, "No disponible", FLEX_C_MUTE);
        row_set(p->roll, "No disponible", FLEX_C_MUTE);
    }
}

static void det_tick_cb(lv_timer_t *t)
{
    det_refresh(lv_timer_get_user_data(t));
}

static void det_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    reset_ref();
}

static lv_obj_t *para(lv_obj_t *list, const lv_font_t *font, uint32_t rgb, const char *text)
{
    lv_obj_t *l = watch_theme_label(list, font, rgb, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(l, 8, 0);
    return l;
}

static lv_obj_t *details_create(lv_obj_t *parent)
{
    cmp_det_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, "Flex Compass", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    /* Rumbo relativo, incertidumbre y "Fijar referencia" */
    lv_obj_t *card = flex_glass_tile(list, 0, 0, LV_PCT(100), 190);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    p->deg = watch_theme_label(card, FLEX_FONT_LARGE, FLEX_C_TXT, "--");
    lv_obj_align(p->deg, LV_ALIGN_TOP_MID, 0, 0);
    p->unc = watch_theme_label(card, FLEX_FONT_SMALL, FLEX_C_TXT2, "");
    lv_obj_set_width(p->unc, LV_PCT(100));
    lv_obj_set_style_text_align(p->unc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->unc, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(p->unc, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_t *rb = flex_glass_button(card, "Fijar referencia", FLEX_C_PRIMARY, 240, 56);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(rb, reset_cb, LV_EVENT_CLICKED, NULL);
    para(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
         "La aguja roja señala la referencia (REF). Rumbo relativo medido con el giroscopio: no es el Norte "
         "magnético ni el geográfico. La cuña roja es su incertidumbre, que aumenta al moverse; fija la referencia "
         "de nuevo (aquí o manteniendo pulsada la esfera) cuando crezca.");

    /* Pantalla de requisito (AppCompass.h:683-695) */
    para(list, FLEX_FONT_TITLE, FLEX_C_TXT, "Norte: requiere magnetómetro");
    para(list, FLEX_FONT_SMALL, FLEX_C_TXT2,
         "Flex Compass necesita un módulo IMU con magnetómetro para determinar la orientación y el Norte.");
    para(list, FLEX_FONT_SMALL, FLEX_C_MUTE,
         "El QMI8658 de este reloj mide aceleración y giro (6 ejes), pero no el campo magnético: no se muestra "
         "ningún rumbo respecto al Norte. Tampoco hay barómetro ni GPS: sin altitud ni coordenadas.");

    watch_theme_section(list, "Sensores");
    p->state = watch_theme_info_row(list, "Estado", "");
    p->acc = watch_theme_info_row(list, "Acelerómetro", "");
    p->gyr = watch_theme_info_row(list, "Giroscopio", "");
    row_set(watch_theme_info_row(list, "Magnetómetro", ""), "No presente", FLEX_C_MUTE);
    row_set(watch_theme_info_row(list, "Barómetro", ""), "No presente", FLEX_C_MUTE);
    row_set(watch_theme_info_row(list, "GPS", ""), "No presente", FLEX_C_MUTE);
    row_set(watch_theme_info_row(list, "Sensor Fusion", ""), "Inactivo", FLEX_C_MUTE);

    watch_theme_section(list, "Datos técnicos");
    p->head = watch_theme_info_row(list, "Heading", "");
    p->pitch = watch_theme_info_row(list, "Pitch", "");
    p->roll = watch_theme_info_row(list, "Roll", "");

    det_refresh(p);
    lv_obj_add_event_cb(page, det_free_cb, LV_EVENT_DELETE, p);
    ui_page_timer(page, det_tick_cb, CMP_TICK_MS, p);
    return page;
}

static void more_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_push(details_create);
}

/* ---- Página principal ---- */

lv_obj_t *ui_compass_create(lv_obj_t *parent)
{
    cmp_page_t *p = lv_malloc_zeroed(sizeof(*p));
    LV_ASSERT_MALLOC(p);
    p->yaw = -1;
    p->time_key = INT64_MIN;
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);

    /* Rumbo (naranja) y hora, apartados de las esquinas redondeadas */
    p->heading = watch_theme_label(page, FLEX_TYPE_TITLE, FLEX_CMP_ORANGE, "--° REL");
    lv_obj_set_pos(p->heading, 60, HDR_Y);
    p->time = watch_theme_label(page, FLEX_TYPE_TITLE, FLEX_C_WHITE, "--:--");
    lv_obj_set_width(p->time, 150);
    lv_obj_set_style_text_align(p->time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(p->time, 350 - 150, HDR_Y);

    /* Marca fija superior */
    lv_obj_t *lub = lv_obj_create(page);
    lv_obj_remove_style_all(lub);
    lv_obj_set_size(lub, 5, 21);
    lv_obj_set_pos(lub, DIAL_CX - 2, 62);
    lv_obj_set_style_radius(lub, 2, 0);
    lv_obj_set_style_bg_color(lub, flex_color(FLEX_C_WHITE), 0);
    lv_obj_set_style_bg_opa(lub, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lub, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Esfera: un objeto que se dibuja a sí mismo; mantenerla pulsada fija la referencia */
    lv_obj_t *dial = lv_obj_create(page);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, DIAL_D, DIAL_D);
    lv_obj_set_pos(dial, DIAL_CX - DIAL_R, DIAL_CY - DIAL_R);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dial, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dial, dial_draw_cb, LV_EVENT_DRAW_MAIN, p);
    lv_obj_add_event_cb(dial, dial_long_cb, LV_EVENT_LONG_PRESSED, p);
    p->dial = dial;

    /* Chevrón: detalles */
    lv_obj_t *more = lv_obj_create(page);
    lv_obj_remove_style_all(more);
    lv_obj_set_size(more, 120, 52);
    lv_obj_set_pos(more, DIAL_CX - 60, FLEX_SCREEN_H - 58);
    lv_obj_remove_flag(more, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(more, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(more, more_cb, LV_EVENT_CLICKED, NULL);
    static lv_point_precise_t kChev[3] = {{0, 0}, {13, 12}, {26, 0}};
    lv_obj_t *chev = lv_line_create(more);
    lv_line_set_points(chev, kChev, 3);
    lv_obj_set_style_line_width(chev, 4, 0);
    lv_obj_set_style_line_color(chev, flex_color(FLEX_C_WHITE), 0);
    lv_obj_set_style_line_rounded(chev, true, 0);
    lv_obj_align(chev, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(chev, LV_OBJ_FLAG_CLICKABLE);

    watch_imu_set_active(true);
    p->imu_on = true;
    watch_imu_heading_reset(); /* referencia: hacia donde apunta al abrir */
    refresh(p);
    lv_obj_add_event_cb(page, delete_cb, LV_EVENT_DELETE, p);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_suspend(), p);
    lv_obj_add_event_cb(page, suspend_cb, (lv_event_code_t)ui_nav_event_resume(), p);
    ui_page_timer(page, tick_cb, CMP_TICK_MS, p);
    s_live = p;
    return page;
}
