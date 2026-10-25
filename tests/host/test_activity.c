/*
 * Pruebas de PC de Bienestar (watch_activity): detector de pasos con señales
 * sintéticas, máquina de estados del entrenamiento, métricas, día en curso,
 * persistencia e historial. Las señales sintéticas comprueban el ALGORITMO;
 * no sustituyen la validación física en el reloj (T185-T204).
 */
#define _DEFAULT_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "act_catalog.h"
#include "act_engine.h"
#include "act_metrics.h"
#include "act_session.h"
#include "act_steps.h"
#include "act_store.h"
#include "test_util.h"
#include "ui_wb_logic.h"

#define CHECK_NEAR(a, b, tol) do { double _a = (double)(a), _b = (double)(b); g_checks++; \
    if (fabs(_a - _b) > (tol)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d: %s = %.2f, esperado %.2f ± %.2f\n", __FILE__, __LINE__, #a, _a, _b, \
            (double)(tol)); } } while (0)

/* ---- Generador de señales del acelerómetro en la muñeca ---- */

typedef struct {
    double t;         /* s */
    uint32_t index;   /* contador del sensor */
    int64_t t0_ms;
    unsigned seed;
    int drop_every;   /* 0 = sin pérdidas; n = se pierde una de cada n */
    int dup_every;    /* 0 = sin duplicados; n = una de cada n se lee dos veces */
    int jitter_ms;    /* retraso de lectura 0..jitter */
    double phase;     /* fase del paso */
} gen_t;

static double rnd(gen_t *g)
{
    g->seed = g->seed * 1103515245u + 12345u;
    return ((g->seed >> 8) & 0xFFFF) / 65535.0;
}

static void gen_init(gen_t *g, unsigned seed)
{
    memset(g, 0, sizeof(*g));
    g->t0_ms = 100000;
    g->seed = seed;
    g->index = 1000;
}

typedef void (*sink_t)(void *ctx, const act_sample_t *s);

/* Emite una muestra con aceleración (x, y, z) en mg. */
static void emit(gen_t *g, double x, double y, double z, sink_t sink, void *ctx)
{
    const double P = ACT_SAMPLE_PERIOD_US / 1e6;
    g->t += P;
    g->index++;
    if (g->drop_every && (g->index % (uint32_t)g->drop_every) == 0) {
        return; /* el lector no llegó a tiempo: se la perdió */
    }
    act_sample_t s;
    s.index = g->index;
    s.t_ms = g->t0_ms + (int64_t)llround(g->t * 1000.0) + (g->jitter_ms ? (int64_t)(rnd(g) * g->jitter_ms) : 0);
    s.restart = false;
    const double v[3] = {x, y, z};
    for (int i = 0; i < 3; i++) {
        double c = v[i];
        c = c > 4095 ? 4095 : (c < -4096 ? -4096 : c);
        s.a[i] = (int16_t)lround(c);
    }
    sink(ctx, &s);
    if (g->dup_every && (g->index % (uint32_t)g->dup_every) == 0) {
        sink(ctx, &s); /* la misma muestra otra vez */
    }
}

/* Caminar/correr: la gravedad gira con el balanceo del brazo (medio ritmo)
 * y la parte dinámica tiene el ritmo de los pasos y su armónico. */
static void gen_steps(gen_t *g, double secs, double spm, double amp, double noise, sink_t sink, void *ctx)
{
    const double P = ACT_SAMPLE_PERIOD_US / 1e6;
    const int n = (int)(secs / P);
    const double f = spm / 60.0;
    for (int i = 0; i < n; i++) {
        g->phase += 2.0 * M_PI * f * P;
        const double th = 0.45 * sin(g->phase / 2.0);
        const double gx = 0.0, gy = 1000.0 * sin(th), gz = 1000.0 * cos(th);
        const double dyn = amp * (0.55 * sin(g->phase) + 0.25 * sin(2.0 * g->phase + 0.6));
        const double nz = noise * (rnd(g) - 0.5) * 2.0;
        emit(g, gx + 0.3 * dyn + nz, gy + 0.2 * dyn + nz, gz + 0.9 * dyn + nz, sink, ctx);
    }
}

/* Reloj quieto (en la mesa o la muñeca en reposo) con ruido. */
static void gen_still(gen_t *g, double secs, double noise, sink_t sink, void *ctx)
{
    const double P = ACT_SAMPLE_PERIOD_US / 1e6;
    const int n = (int)(secs / P);
    for (int i = 0; i < n; i++) {
        const double nz = noise * (rnd(g) - 0.5) * 2.0;
        emit(g, 30 + nz, -20 + nz, 1000 + nz, sink, ctx);
    }
}

/* Gesto aislado de la muñeca (mirar la hora, coger algo): un pulso de dur s. */
static void gen_gesture(gen_t *g, double dur, double amp, sink_t sink, void *ctx)
{
    const double P = ACT_SAMPLE_PERIOD_US / 1e6;
    const int n = (int)(dur / P);
    for (int i = 0; i < n; i++) {
        const double u = (double)i / n;
        const double dyn = amp * sin(M_PI * u) * sin(2.0 * M_PI * u * 1.5);
        emit(g, 200 + dyn, 400 + 0.5 * dyn, 900 - 0.3 * dyn, sink, ctx);
    }
}

/* Golpe: un impacto de pocos ms (aplauso, golpe en la mesa). */
static void gen_impact(gen_t *g, double amp, sink_t sink, void *ctx)
{
    emit(g, amp, amp * 0.5, 1000 + amp, sink, ctx);
    emit(g, -amp * 0.6, -amp * 0.3, 1000 - amp * 0.4, sink, ctx);
    emit(g, amp * 0.2, 0, 1000, sink, ctx);
}

static void sink_det(void *ctx, const act_sample_t *s)
{
    act_steps_feed((act_steps_t *)ctx, s);
}

static void sink_eng(void *ctx, const act_sample_t *s)
{
    act_engine_feed((act_engine_t *)ctx, s, 1);
}

static int64_t gen_now(const gen_t *g)
{
    return g->t0_ms + (int64_t)llround(g->t * 1000.0);
}

/* ---- 1-4. Detector de pasos ---- */

static void test_steps_walk_run(void)
{
    act_steps_t d;
    gen_t g;
    /* Caminar 60 s a 110 pasos/min: 110 pasos */
    act_steps_init(&d, NULL);
    gen_init(&g, 1);
    gen_steps(&g, 60, 110, 420, 25, sink_det, &d);
    CHECK_NEAR(d.steps, 110, 110 * 0.04);
    CHECK(d.steps_run < d.steps / 10);
    CHECK_NEAR(act_steps_cadence(&d), 110, 6);
    CHECK(d.motion == ACT_MOT_WALK);
    /* Caminar despacio: 85 pasos/min con poca amplitud */
    act_steps_init(&d, NULL);
    gen_init(&g, 2);
    gen_steps(&g, 60, 85, 260, 20, sink_det, &d);
    CHECK_NEAR(d.steps, 85, 85 * 0.06);
    /* Correr 60 s a 165 pasos/min */
    act_steps_init(&d, NULL);
    gen_init(&g, 3);
    gen_steps(&g, 60, 165, 1300, 40, sink_det, &d);
    CHECK_NEAR(d.steps, 165, 165 * 0.04);
    CHECK(d.steps_run > d.steps * 8 / 10);
    CHECK(d.motion == ACT_MOT_RUN);
    /* Del paseo a la carrera: la clase cambia con histéresis, no cada segundo */
    act_steps_init(&d, NULL);
    gen_init(&g, 4);
    gen_steps(&g, 30, 110, 420, 25, sink_det, &d);
    CHECK(d.motion == ACT_MOT_WALK);
    gen_steps(&g, 30, 165, 1300, 40, sink_det, &d);
    CHECK(d.motion == ACT_MOT_RUN);
    CHECK_NEAR(d.steps, 55 + 82, 137 * 0.05);
    /* Quieto: clase "quieto" y sin pasos */
    gen_still(&g, 10, 3, sink_det, &d);
    CHECK(d.motion == ACT_MOT_STILL);
    CHECK(!d.walking);
}

static void test_steps_reject(void)
{
    act_steps_t d;
    gen_t g;
    /* Ruido de escribir o de ir sentado: 0 pasos */
    act_steps_init(&d, NULL);
    gen_init(&g, 10);
    gen_still(&g, 120, 40, sink_det, &d);
    CHECK_EQ(d.steps, 0);
    /* Gestos aislados de la muñeca, separados unos segundos */
    for (int i = 0; i < 12; i++) {
        gen_gesture(&g, 0.4 + 0.05 * (i % 4), 700 + 60 * (i % 5), sink_det, &d);
        gen_still(&g, 2.5 + (i % 3), 8, sink_det, &d);
    }
    CHECK_EQ(d.steps, 0);
    /* Golpes (aplausos, golpes en la mesa) */
    for (int i = 0; i < 10; i++) {
        gen_impact(&g, 3500, sink_det, &d);
        gen_still(&g, 0.6, 5, sink_det, &d);
    }
    CHECK_EQ(d.steps, 0);
    CHECK(d.impacts > 0 || d.steps == 0);
    /* Cuatro pasos sueltos y parar: no llegan a confirmarse */
    gen_steps(&g, 4 * 60.0 / 100.0, 100, 420, 20, sink_det, &d);
    gen_still(&g, 4, 5, sink_det, &d);
    CHECK_EQ(d.steps, 0);
    /* Vibración (coche, máquina): rápida y pequeña */
    for (int i = 0; i < 2000; i++) {
        const double v = 60.0 * sin(i * 2.0 * M_PI * 12.0 * ACT_SAMPLE_PERIOD_US / 1e6);
        emit(&g, 10 + v, 10, 1000 + v, sink_det, &d);
    }
    CHECK_EQ(d.steps, 0);
    /* Valores imposibles (caída libre, ceros): se cuentan y no cambian nada */
    act_steps_init(&d, NULL);
    gen_init(&g, 11);
    for (int i = 0; i < 100; i++) {
        emit(&g, 0, 0, 0, sink_det, &d);
    }
    CHECK_EQ(d.invalid, 100);
    CHECK_EQ(d.steps, 0);
}

static void test_steps_timing(void)
{
    act_steps_t d, ref;
    gen_t g;
    /* Referencia: lectura perfecta */
    act_steps_init(&ref, NULL);
    gen_init(&g, 20);
    gen_steps(&g, 90, 112, 400, 25, sink_det, &ref);
    /* Muestras perdidas (una de cada 8: el lector a 50 Hz frente a 56 Hz) y
     * retraso de lectura de hasta 18 ms: mismo resultado */
    act_steps_init(&d, NULL);
    gen_init(&g, 20);
    g.drop_every = 8;
    g.jitter_ms = 18;
    gen_steps(&g, 90, 112, 400, 25, sink_det, &d);
    CHECK_NEAR(d.steps, ref.steps, 4);
    CHECK_EQ(d.gaps, 0);
    /* Lectura a 25 Hz (modo de reposo del servicio): sigue contando */
    act_steps_init(&d, NULL);
    gen_init(&g, 20);
    g.drop_every = 2;
    gen_steps(&g, 90, 112, 400, 25, sink_det, &d);
    CHECK_NEAR(d.steps, ref.steps, ref.steps * 0.06);
    /* Muestras duplicadas: se descartan, ningún paso dos veces */
    act_steps_init(&d, NULL);
    gen_init(&g, 20);
    g.dup_every = 3;
    gen_steps(&g, 90, 112, 400, 25, sink_det, &d);
    CHECK_EQ(d.steps, ref.steps);
    CHECK(d.dups > 0);
    /* Un hueco de 400 ms en medio: discontinuidad; se pierde a lo sumo el tren en curso */
    act_steps_init(&d, NULL);
    gen_init(&g, 20);
    gen_steps(&g, 45, 112, 400, 25, sink_det, &d);
    g.index += 22;
    g.t += 22 * ACT_SAMPLE_PERIOD_US / 1e6;
    gen_steps(&g, 45, 112, 400, 25, sink_det, &d);
    CHECK_EQ(d.gaps, 1);
    CHECK(d.steps <= ref.steps + 1);
    CHECK(d.steps + 6 >= ref.steps - 2);
    /* El reloj monótono salta (tarea parada) sin que el contador lo diga: discontinuidad */
    act_steps_init(&d, NULL);
    gen_init(&g, 21);
    gen_steps(&g, 10, 112, 400, 25, sink_det, &d);
    g.t0_ms += 2000;
    gen_steps(&g, 10, 112, 400, 25, sink_det, &d);
    CHECK(d.gaps >= 1);
    /* La misma muestra reenviada no se procesa dos veces (cuenta de muestras) */
    act_steps_init(&d, NULL);
    act_sample_t s = {.index = 5, .t_ms = 1000, .a = {0, 0, 1000}};
    act_steps_feed(&d, &s);
    act_steps_feed(&d, &s);
    CHECK_EQ(d.samples, 1);
    CHECK_EQ(d.dups, 1);
    /* Primera muestra tras reiniciar el sensor: empieza de nuevo */
    s.index = 6;
    s.t_ms = 1018;
    s.restart = true;
    act_steps_feed(&d, &s);
    CHECK_EQ(d.gaps, 1);
}

/* ---- 5. Distancia y calibración ---- */

static void test_distance(void)
{
    act_config_t c;
    act_config_default(&c);
    act_stride_src_t src;
    CHECK_EQ(act_stride_cm(&c, false, &src), 70);
    CHECK_EQ(src, ACT_STRIDE_GENERIC);
    CHECK_EQ(act_stride_cm(&c, true, &src), 100);
    c.height_cm = 175;
    CHECK_EQ(act_stride_cm(&c, false, &src), 73); /* 0,415 x 175 */
    CHECK_EQ(src, ACT_STRIDE_HEIGHT);
    CHECK_EQ(act_stride_cm(&c, true, NULL), 114);
    c.stride_walk_cm = 75;
    CHECK_EQ(act_stride_cm(&c, false, &src), 75);
    CHECK_EQ(src, ACT_STRIDE_USER);
    /* Calibración con un recorrido conocido */
    CHECK_EQ(act_calibrate_stride(400, 520), 77);
    CHECK_EQ(act_calibrate_stride(400, 20), 0);   /* muy pocos pasos: no sirve */
    CHECK_EQ(act_calibrate_stride(0, 500), 0);
    CHECK_EQ(act_calibrate_stride(5000, 600), 0); /* 8,3 m por paso: imposible */
    CHECK_EQ(act_calib_error_pct(440, 400), 10);
    CHECK_EQ(act_calib_error_pct(360, 400), -10);
    /* Distancia del motor = pasos x zancada calibrada */
    act_engine_t e;
    act_engine_init(&e, &c);
    gen_t g;
    gen_init(&g, 30);
    gen_steps(&g, 120, 110, 420, 25, sink_eng, &e);
    act_engine_tick(&e, gen_now(&g), 20000 * 86400LL + 36000, 0);
    act_view_t v;
    act_engine_view(&e, &v);
    CHECK_EQ(v.dist_m, e.day.steps * 75 / 100);
    CHECK(!v.dist_generic);
}

/* ---- 6. Objetivos y cambio de día ---- */

static void test_day(void)
{
    act_config_t c;
    act_config_default(&c);
    c.goal_steps = 500;
    c.weight_dkg = 700;
    act_engine_t e;
    act_engine_init(&e, &c);
    gen_t g;
    gen_init(&g, 40);
    const int64_t day0 = 20100;
    /* 23:55: se caminan 6 minutos y se cruza la medianoche */
    int64_t local = day0 * 86400 + 23 * 3600 + 55 * 60;
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    for (int i = 0; i < 360; i++) {
        gen_steps(&g, 1.0, 110, 420, 25, sink_eng, &e);
        local++;
        act_engine_tick(&e, gen_now(&g), local, local + 18000);
    }
    const uint32_t ev = act_engine_take_events(&e);
    CHECK(ev & ACT_EV_DAY_CLOSED);
    CHECK_EQ(e.closed.day, day0);
    CHECK(e.closed.steps >= 500);
    CHECK(e.closed.goal_bits & 4u); /* objetivo de pasos cumplido ese día */
    CHECK(e.closed.kcal > 0);
    CHECK(!(e.closed.flags & ACT_DAY_NO_WEIGHT));
    CHECK_EQ(e.day.day, day0 + 1);
    CHECK(e.day.steps > 0 && e.day.steps < e.closed.steps); /* el día nuevo empieza de cero */
    CHECK_NEAR(e.closed.steps + e.day.steps, 660, 660 * 0.05);
    act_view_t v;
    act_engine_view(&e, &v);
    CHECK_EQ(v.rings.pct[2], v.steps * 100 / 500);
    /* Sin hora: los pasos van al día sin fecha y se asignan al primer día válido */
    act_engine_init(&e, &c);
    gen_init(&g, 41);
    gen_steps(&g, 30, 110, 420, 25, sink_eng, &e);
    act_engine_tick(&e, gen_now(&g), -1, 0);
    CHECK_EQ(e.day.day, ACT_NO_DAY);
    CHECK(e.day.steps > 0);
    act_engine_tick(&e, gen_now(&g) + 1000, day0 * 86400 + 3600, 0);
    CHECK_EQ(e.day.day, day0);
    CHECK(e.day.flags & ACT_DAY_LATE_DATE);
    CHECK(!(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED));

    /* La hora se corrige hacia atrás pasada la medianoche (SNTP con el RTC
     * adelantado): el día nuevo sigue abierto y el anterior no se cierra otra
     * vez (le daría un segundo registro). */
    act_engine_init(&e, &c);
    gen_init(&g, 42);
    local = day0 * 86400 + 23 * 3600 + 59 * 60;
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    gen_steps(&g, 30, 110, 420, 25, sink_eng, &e);
    local += 65; /* 00:00:05 */
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    CHECK(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED);
    CHECK_EQ(e.day.day, day0 + 1);
    local -= 7; /* 23:59:58 del día anterior */
    gen_steps(&g, 20, 110, 420, 25, sink_eng, &e);
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    CHECK(!(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED));
    CHECK_EQ(e.day.day, day0 + 1);
    const uint32_t kept = e.day.steps;
    CHECK(kept > 0); /* los pasos de esos segundos no se pierden */
    local += 3; /* otra vez el día nuevo: nada que cerrar */
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    CHECK(!(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED));
    CHECK_EQ(e.day.steps, kept);
    /* Una fecha mucho más atrás (estaba mal puesta): se cierra y empieza la buena */
    local -= 5 * 86400;
    act_engine_tick(&e, gen_now(&g), local, local + 18000);
    CHECK(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED);
    CHECK_EQ(e.closed.day, day0 + 1);
    CHECK_EQ(e.day.day, day0 - 4);
    CHECK_EQ(e.day.steps, 0);
    /* Igual al restaurar: un día atrás sigue el guardado; un día adelante se cierra */
    act_day_t saved = e.closed;
    act_engine_init(&e, &c);
    act_engine_restore(&e, &saved, NULL, 1, 0, saved.day - 1, 0);
    CHECK(!(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED));
    CHECK_EQ(e.day.day, saved.day);
    CHECK_EQ(e.day.steps, saved.steps);
    act_engine_init(&e, &c);
    act_engine_restore(&e, &saved, NULL, 1, 0, saved.day + 1, 0);
    CHECK(act_engine_take_events(&e) & ACT_EV_DAY_CLOSED);
    CHECK_EQ(e.closed.steps, saved.steps);
    CHECK_EQ(e.day.day, saved.day + 1);
}

/* ---- 7-10. Pausa, reanudación y finalización automáticas ---- */

typedef struct {
    act_engine_t e;
    gen_t g;
    int64_t local;
} rig_t;

static void rig_init(rig_t *r, const act_config_t *c, unsigned seed)
{
    act_engine_init(&r->e, c);
    gen_init(&r->g, seed);
    r->local = 20200LL * 86400 + 10 * 3600;
    act_engine_tick(&r->e, gen_now(&r->g), r->local, r->local + 18000);
}

/* secs segundos de una actividad (spm > 0 caminar, 0 quieto, -1 gestos) con un tick por segundo. */
static void rig_run(rig_t *r, int secs, double spm)
{
    for (int i = 0; i < secs; i++) {
        if (spm > 0) {
            gen_steps(&r->g, 1.0, spm, spm > 140 ? 1300 : 420, 25, sink_eng, &r->e);
        } else if (spm < 0) {
            if (i % 3 == 0) {
                gen_gesture(&r->g, 0.5, 800, sink_eng, &r->e);
                gen_still(&r->g, 0.5, 6, sink_eng, &r->e);
            } else {
                gen_still(&r->g, 1.0, 6, sink_eng, &r->e);
            }
        } else {
            gen_still(&r->g, 1.0, 4, sink_eng, &r->e);
        }
        r->local++;
        act_engine_tick(&r->e, gen_now(&r->g), r->local, r->local + 18000);
    }
}

static void test_auto(void)
{
    act_config_t c;
    act_config_default(&c);
    c.weight_dkg = 700;
    rig_t r;
    /* Pausa tras 20 s sin actividad, con la pausa desde el último paso */
    rig_init(&r, &c, 50);
    CHECK(act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), r.local + 18000, false));
    CHECK(!act_engine_start(&r.e, ACT_TYPE_RUN_OUT, gen_now(&r.g), 0, false)); /* ya hay una */
    rig_run(&r, 60, 110);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
    act_engine_take_events(&r.e);
    rig_run(&r, 15, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE); /* 15 s: aún no */
    rig_run(&r, 8, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    CHECK_EQ(r.e.wk.s.pause_kind, ACT_PAUSE_AUTO);
    CHECK(act_engine_take_events(&r.e) & ACT_EV_AUTO_PAUSE);
    act_view_t v;
    act_engine_view(&r.e, &v);
    CHECK_NEAR(v.active_ms / 1000.0, 60, 2.5); /* la espera no cuenta como ejercicio */
    CHECK_EQ(r.e.wk.s.auto_pauses, 1);
    /* Gestos de la muñeca durante la pausa: no reanudan */
    rig_run(&r, 30, -1);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    /* Caminar de nuevo: reanuda tras movimiento sostenido, desde el primer paso */
    const uint32_t steps_before = r.e.wk.steps;
    rig_run(&r, 12, 110);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
    CHECK(act_engine_take_events(&r.e) & ACT_EV_AUTO_RESUME);
    CHECK_EQ(r.e.wk.s.auto_resumes, 1);
    CHECK(r.e.wk.steps >= steps_before + 18); /* los pasos del tren que reanudó son de la sesión */
    act_engine_view(&r.e, &v);
    CHECK_NEAR(v.active_ms / 1000.0, 72, 3.5);
    /* Parar del todo: pausa y, a los 5 min, fin con los datos guardados */
    rig_run(&r, 25, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    act_engine_take_events(&r.e);
    const uint32_t active_end = act_session_active_ms(&r.e.wk.s, r.e.now_ms);
    rig_run(&r, 290, 0); /* 5 min desde que se VIO la pausa (a los 20 s de parar) */
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    rig_run(&r, 10, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_COMPLETED);
    CHECK_EQ(r.e.wk.s.end_reason, ACT_END_AUTO);
    const uint32_t ev = act_engine_take_events(&r.e);
    CHECK(ev & ACT_EV_AUTO_END);
    CHECK(ev & ACT_EV_SESSION_DONE);
    CHECK_EQ(act_session_active_ms(&r.e.done.s, r.e.done.s.end_ms), active_end);
    act_ses_rec_t rec;
    act_ses_from(&r.e.done, &r.e.cfg, &rec);
    CHECK_EQ(rec.end_reason, ACT_END_AUTO);
    CHECK_NEAR(rec.active_s, 72, 3.5);
    CHECK(rec.paused_s < 60); /* la pausa final (5 min) no se cuenta */
    CHECK(rec.steps > 100 && rec.dist_m > 70 && rec.kcal_valid && rec.kcal > 0);
    CHECK_EQ(r.e.day.workouts, 1);
    /* Un segundo fin no hace nada */
    CHECK(!act_engine_end(&r.e, gen_now(&r.g)));
    CHECK(!(act_engine_take_events(&r.e) & ACT_EV_SESSION_DONE));

    /* Cada función se puede desactivar por separado */
    act_config_t c2 = c;
    c2.autos.auto_pause = false;
    rig_init(&r, &c2, 51);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 20, 110);
    rig_run(&r, 60, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
    c2 = c;
    c2.autos.auto_resume = false;
    rig_init(&r, &c2, 52);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 20, 110);
    rig_run(&r, 25, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    rig_run(&r, 20, 110);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    c2 = c;
    c2.autos.auto_end = false;
    rig_init(&r, &c2, 53);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 20, 110);
    rig_run(&r, 620, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    /* Umbral configurable */
    c2 = c;
    c2.autos.pause_after_s = 40;
    rig_init(&r, &c2, 54);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 20, 110);
    rig_run(&r, 30, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
    rig_run(&r, 15, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    /* Pausa manual: no se reanuda sola, pero sí termina sola */
    rig_init(&r, &c, 55);
    act_engine_start(&r.e, ACT_TYPE_RUN_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 30, 165);
    CHECK(act_engine_pause(&r.e, gen_now(&r.g)));
    CHECK(!act_engine_pause(&r.e, gen_now(&r.g))); /* el mismo evento dos veces: nada */
    rig_run(&r, 20, 165);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_PAUSED);
    CHECK(act_engine_resume(&r.e, gen_now(&r.g)));
    CHECK(!act_engine_resume(&r.e, gen_now(&r.g)));
    /* Tipos sin pasos (yoga): nunca se pausan solos aunque la muñeca esté quieta */
    rig_init(&r, &c, 56);
    act_engine_start(&r.e, 30, gen_now(&r.g), 0, false);
    rig_run(&r, 120, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
    /* Sin datos del sensor, tampoco (no hay evidencia) */
    rig_init(&r, &c, 57);
    act_engine_set_sensor(&r.e, false);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), 0, false);
    rig_run(&r, 60, 0);
    CHECK_EQ(r.e.wk.s.state, ACT_SES_ACTIVE);
}

/* ---- Energía sin contar dos veces y datos personales ---- */

static void test_energy(void)
{
    act_config_t c;
    act_config_default(&c);
    act_view_t v;
    /* Sin peso: no hay kcal (no se inventan) */
    rig_t r;
    rig_init(&r, &c, 60);
    rig_run(&r, 60, 110);
    act_engine_view(&r.e, &v);
    CHECK(!v.kcal_valid);
    CHECK_EQ(v.kcal, 0);
    CHECK(v.steps > 0);
    /* Al añadir el peso, el día de hoy ya sale (la energía es por kg) */
    c.weight_dkg = 700;
    act_engine_set_config(&r.e, &c);
    act_engine_view(&r.e, &v);
    CHECK(v.kcal_valid);
    /* 0,0005 kcal/kg/m x 70 kg x distancia */
    CHECK_NEAR(v.kcal, 0.0005 * 70 * v.dist_m, 1.0);
    /* Caminar con o sin entrenamiento de caminar: mismas calorías del día */
    rig_t a, b;
    rig_init(&a, &c, 61);
    rig_init(&b, &c, 61);
    act_engine_start(&b.e, ACT_TYPE_WALK_OUT, gen_now(&b.g), 0, false);
    rig_run(&a, 300, 110);
    rig_run(&b, 300, 110);
    act_view_t va, vb;
    act_engine_view(&a.e, &va);
    act_engine_view(&b.e, &vb);
    CHECK_EQ(va.steps, vb.steps);
    CHECK_NEAR(va.kcal, vb.kcal, 1.0);
    CHECK_EQ(vb.ses_kcal, vb.kcal);
    act_engine_end(&b.e, gen_now(&b.g));
    act_engine_view(&b.e, &vb);
    CHECK_NEAR(va.kcal, vb.kcal, 1.0); /* al terminar no se suma otra vez */
    /* Yoga de 30 min (2,5 MET) a 70 kg: (2,5 - 1) x 70 x 0,5 = 52,5 kcal,
     * aunque se camine durante la sesión (una sola fuente) */
    rig_init(&a, &c, 62);
    act_engine_start(&a.e, 30, gen_now(&a.g), 0, false);
    rig_run(&a, 1500, 0);
    rig_run(&a, 300, 110);
    act_engine_view(&a.e, &va);
    CHECK_NEAR(va.ses_kcal, 52.5, 1.0);
    CHECK_NEAR(va.kcal, 52.5, 1.0);
    CHECK(va.steps > 400); /* los pasos sí son del día */
    CHECK_EQ(va.ses_steps, 0); /* pero no del yoga */
    /* Las pausas no son ejercicio ni gastan */
    act_engine_pause(&a.e, gen_now(&a.g));
    rig_run(&a, 300, 0);
    act_engine_view(&a.e, &vb);
    CHECK_EQ(vb.ses_kcal, va.ses_kcal);
    CHECK_EQ(vb.active_ms / 1000, va.active_ms / 1000);
    CHECK_NEAR(vb.paused_ms / 1000.0, 300, 1.5);
    /* Ejercicio automático: caminar a buen ritmo 3 min suma; despacio o
     * gestos, no */
    rig_init(&a, &c, 63);
    rig_run(&a, 180, 112);
    act_engine_view(&a.e, &va);
    CHECK_NEAR(va.ex_s, 180, 12);
    rig_init(&a, &c, 64);
    rig_run(&a, 180, 85);
    rig_run(&a, 120, -1);
    act_engine_view(&a.e, &va);
    CHECK_EQ(va.ex_s, 0);
    /* Detección desactivada: no suma ni sugiere */
    act_config_t c3 = c;
    c3.auto_detect = false;
    rig_init(&a, &c3, 65);
    rig_run(&a, 240, 112);
    act_engine_view(&a.e, &va);
    CHECK_EQ(va.ex_s, 0);
    CHECK(!va.suggest);
}

static void test_suggest(void)
{
    act_config_t c;
    act_config_default(&c);
    c.weight_dkg = 700;
    rig_t r;
    rig_init(&r, &c, 70);
    rig_run(&r, 170, 110);
    CHECK(!r.e.suggest);
    rig_run(&r, 20, 110);
    CHECK(r.e.suggest);
    CHECK_EQ(r.e.suggest_type, ACT_TYPE_WALK_OUT);
    CHECK(act_engine_take_events(&r.e) & ACT_EV_SUGGEST);
    act_view_t before;
    act_engine_view(&r.e, &before);
    /* Aceptar: el entrenamiento empieza con el primer paso y se lleva sus pasos */
    CHECK(act_engine_start(&r.e, r.e.suggest_type, gen_now(&r.g), r.local + 18000, true));
    act_view_t v;
    act_engine_view(&r.e, &v);
    CHECK_NEAR(v.active_ms / 1000.0, 190, 4);
    CHECK(v.ses_steps + 10 >= before.steps);
    CHECK_NEAR(v.kcal, before.kcal, 1.0); /* el día no cambia: solo cambia de fuente */
    CHECK_NEAR(v.ex_s, 190, 6);           /* y el ejercicio automático no se suma dos veces */
    CHECK(!r.e.suggest);
    /* Un gesto no sugiere nada */
    rig_init(&r, &c, 71);
    rig_run(&r, 300, -1);
    CHECK(!r.e.suggest);
}

/* ---- 11-12, 15. Persistencia e historial ---- */

static char g_dir[256];

static void tmp_dir(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/flex_act_%d/.sistema/bienestar", (int)getpid());
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/flex_act_%d", (int)getpid());
    if (system(cmd) != 0) {
        fprintf(stderr, "no se pudo limpiar %s\n", g_dir);
    }
    CHECK(act_hist_mkdir(g_dir));
}

static void test_state_restore(void)
{
    act_config_t c;
    act_config_default(&c);
    c.weight_dkg = 800;
    rig_t r;
    rig_init(&r, &c, 80);
    act_engine_start(&r.e, ACT_TYPE_RUN_OUT, gen_now(&r.g), r.local + 18000, false);
    rig_run(&r, 120, 165);
    act_view_t v0;
    act_engine_view(&r.e, &v0);
    uint8_t buf[ACT_STATE_MAX];
    const size_t n = act_state_pack(&r.e, gen_now(&r.g), r.local + 18000, buf, sizeof(buf));
    CHECK(n > 0 && n <= ACT_STATE_MAX);
    act_day_t day;
    act_workout_t wk;
    uint32_t next;
    int64_t saved;
    CHECK(act_state_unpack(buf, n, &day, &wk, &next, &saved));
    CHECK_EQ(day.steps, r.e.day.steps);
    CHECK_EQ(wk.s.id, r.e.wk.s.id);
    CHECK_EQ(next, r.e.next_id);
    CHECK_EQ(saved, r.local + 18000);
    /* Un byte cambiado: no se usa */
    buf[20] ^= 0x40;
    CHECK(!act_state_unpack(buf, n, &day, &wk, &next, &saved));
    buf[20] ^= 0x40;
    CHECK(!act_state_unpack(buf, n - 1, &day, &wk, &next, &saved));
    /* Reinicio: el mismo día, 30 s apagado. La sesión vuelve pausada con sus
     * datos; los pasos no se duplican */
    act_engine_t e2;
    act_engine_init(&e2, &c);
    CHECK(act_state_unpack(buf, n, &day, &wk, &next, &saved));
    act_engine_restore(&e2, &day, &wk, next, 5000, (int32_t)(r.local / 86400), 30000);
    act_engine_tick(&e2, 5000, r.local + 30, r.local + 18030);
    act_view_t v1;
    act_engine_view(&e2, &v1);
    CHECK_EQ(v1.steps, v0.steps);
    CHECK_EQ(v1.ses_steps, v0.ses_steps);
    CHECK_EQ(v1.ses, ACT_SES_PAUSED);
    CHECK_EQ(v1.pause_kind, ACT_PAUSE_RESTORED);
    CHECK_NEAR(v1.active_ms / 1000.0, v0.active_ms / 1000.0, 1);
    CHECK_NEAR(v1.paused_ms / 1000.0, 30, 1);
    CHECK_EQ(v1.kcal, v0.kcal);
    CHECK(e2.day.flags & ACT_DAY_RESTORED);
    /* Se puede seguir */
    CHECK(act_engine_resume(&e2, 6000));
    /* Apagado más de 5 min: termina en cuanto arranca, en el momento del apagado */
    act_engine_init(&e2, &c);
    act_engine_restore(&e2, &day, &wk, next, 5000, (int32_t)(r.local / 86400), 400000);
    act_engine_tick(&e2, 5000, r.local + 400, 0);
    CHECK_EQ(e2.wk.s.state, ACT_SES_COMPLETED);
    CHECK(act_engine_take_events(&e2) & ACT_EV_SESSION_DONE);
    act_ses_rec_t rec;
    act_ses_from(&e2.done, &e2.cfg, &rec);
    CHECK_NEAR(rec.active_s, v0.active_ms / 1000.0, 1);
    CHECK_EQ(rec.steps, v0.ses_steps);
    /* Reinicio al día siguiente: el día guardado se cierra (y se entrega para guardarlo) */
    act_engine_init(&e2, &c);
    act_engine_restore(&e2, &day, NULL, next, 5000, (int32_t)(r.local / 86400) + 1, 0);
    CHECK(act_engine_take_events(&e2) & ACT_EV_DAY_CLOSED);
    CHECK_EQ(e2.closed.steps, day.steps);
    CHECK_EQ(e2.day.steps, 0);
    /* Archivo de estado: escritura atómica y lectura */
    tmp_dir();
    CHECK(act_state_save(g_dir, buf, n));
    uint8_t rb[ACT_STATE_MAX];
    CHECK_EQ(act_state_load(g_dir, rb, sizeof(rb)), n);
    CHECK(memcmp(rb, buf, n) == 0);
    /* Almacenamiento que falla (carpeta que no existe): false, sin colgarse */
    CHECK(!act_state_save("/nonexistent/flex", buf, n));
    CHECK_EQ(act_state_load("/nonexistent/flex", rb, sizeof(rb)), 0);
    act_day_t d0 = {.day = 5};
    CHECK(!act_hist_append_day("/nonexistent/flex", &d0));
}

static void test_history(void)
{
    tmp_dir();
    act_config_t c;
    act_config_default(&c);
    c.weight_dkg = 650;
    c.goal_steps = 5000;
    /* Días: 10 seguidos, el último dos veces (reinicio entre guardar el día y el estado) */
    for (int i = 0; i < 10; i++) {
        act_day_t d = {0};
        d.day = 20300 + i;
        d.steps = 6000 + 100 * i;
        d.goal_bits = 4;
        d.walk_cm = 400000;
        CHECK(act_hist_append_day(g_dir, &d));
    }
    act_day_t last = {0};
    last.day = 20309;
    last.steps = 6900;
    last.goal_bits = 4;
    last.walk_cm = 400000;
    CHECK(act_hist_append_day(g_dir, &last)); /* igual: no se añade */
    /* El mismo día cerrado otra vez con el punto de control de antes (menos
     * pasos): se queda el que había */
    act_day_t older = last;
    older.steps = 6500;
    CHECK(act_hist_append_day(g_dir, &older));
    act_day_t out[14];
    CHECK_EQ(act_hist_days(g_dir, 20300, 20313, out, 14), 10);
    CHECK_EQ(out[9].steps, 6900);
    CHECK_EQ(out[10].day, ACT_NO_DAY);
    CHECK_EQ(act_hist_days(g_dir, 20300, 20313, out, 5), -1); /* no cabe: nunca fuera de rango */
    char p[300];
    snprintf(p, sizeof(p), "%s/%s", g_dir, ACT_FILE_DAYS);
    struct stat st;
    CHECK(stat(p, &st) == 0 && st.st_size == 10 * ACT_DAY_REC_LEN);
    /* Sesiones: no se duplican por id */
    for (uint32_t id = 1; id <= 5; id++) {
        act_ses_rec_t s = {0};
        s.id = id;
        s.type = ACT_TYPE_RUN_OUT;
        s.active_s = 600 + id;
        s.paused_s = 30;
        s.total_s = s.active_s + s.paused_s;
        s.steps = 1000 * id;
        CHECK(act_hist_append_ses(g_dir, &s));
        CHECK(act_hist_append_ses(g_dir, &s));
    }
    act_ses_rec_t ss[8];
    CHECK_EQ(act_hist_sessions(g_dir, 0, ss, 8), 5);
    CHECK_EQ(ss[0].id, 5); /* la más reciente primero */
    CHECK_EQ(ss[4].id, 1);
    CHECK_EQ(act_hist_sessions(g_dir, 2, ss, 8), 3);
    CHECK_EQ(ss[0].id, 3);
    CHECK_EQ(act_hist_max_id(g_dir), 5);
    /* Estadísticas y logros */
    act_day_t today = {0};
    today.day = 20310;
    today.steps = 3000; /* hoy aún no cumplido: la racha llega hasta ayer */
    act_stats_t sts;
    CHECK(act_stats_build(g_dir, &today, &c, &sts));
    CHECK_EQ(sts.days, 11);
    CHECK_EQ(sts.streak, 10);
    CHECK_EQ(sts.best_streak, 10);
    CHECK_EQ(sts.best_steps, 6900);
    CHECK_EQ(sts.sessions, 5);
    CHECK(sts.ach & (1u << ACT_ACH_FIRST_WORKOUT));
    CHECK(sts.ach & (1u << ACT_ACH_STREAK_7));
    CHECK(!(sts.ach & (1u << ACT_ACH_STEPS_10K)));
    today.steps = 12000;
    CHECK(act_stats_build(g_dir, &today, &c, &sts));
    CHECK_EQ(sts.streak, 11);
    CHECK(sts.ach & (1u << ACT_ACH_STEPS_10K));
    /* Corte durante un añadido: medio registro al final -> la reparación lo quita */
    FILE *f = fopen(p, "ab");
    CHECK(f != NULL);
    if (f) {
        const uint8_t junk[20] = {'D', 1, 2, 3};
        fwrite(junk, 1, sizeof(junk), f);
        fclose(f);
    }
    act_repair_t rp;
    CHECK(act_hist_repair(g_dir, &rp));
    CHECK(rp.rewritten);
    CHECK_EQ(rp.days, 10);
    CHECK(stat(p, &st) == 0 && st.st_size == 10 * ACT_DAY_REC_LEN);
    /* Registro dañado y duplicado del mismo día en medio: se quitan */
    act_day_t dup = {0};
    dup.day = 20309;
    dup.steps = 7100; /* el mismo día, más completo: sustituye al que había */
    CHECK(act_hist_append_day(g_dir, &dup));
    CHECK(stat(p, &st) == 0 && st.st_size == 10 * ACT_DAY_REC_LEN); /* sin esperar al arranque */
    /* Duplicados escritos tal cual (un archivo de antes): la reparación deja el
     * más completo, sea el primero o el último */
    f = fopen(p, "ab");
    if (f) {
        uint8_t rec[ACT_DAY_REC_LEN];
        act_day_t less = dup;
        less.steps = 6000;
        act_day_pack(&less, rec);
        fwrite(rec, 1, sizeof(rec), f);
        fclose(f);
    }
    CHECK_EQ(act_hist_days(g_dir, 20309, 20309, out, 1), 1);
    CHECK_EQ(out[0].steps, 7100);
    f = fopen(p, "r+b");
    if (f) {
        fseek(f, 3 * ACT_DAY_REC_LEN + 10, SEEK_SET);
        fputc(0x55, f); /* daña el día 4 */
        fclose(f);
    }
    CHECK(act_hist_repair(g_dir, &rp));
    CHECK_EQ(rp.days, 9);
    CHECK_EQ(rp.dropped, 2);
    CHECK_EQ(act_hist_days(g_dir, 20300, 20313, out, 14), 9);
    CHECK_EQ(out[9].steps, 7100);
    CHECK_EQ(out[3].day, ACT_NO_DAY);
    /* Sin cambios: no se reescribe */
    CHECK(act_hist_repair(g_dir, &rp));
    CHECK(!rp.rewritten);
    /* Recorte: más de ACT_DAYS_MAX días -> se quedan los ACT_DAYS_KEEP últimos */
    tmp_dir();
    for (int i = 0; i < ACT_DAYS_MAX + 5; i++) {
        act_day_t d = {0};
        d.day = 10000 + i;
        d.steps = (uint32_t)i;
        act_hist_append_day(g_dir, &d);
    }
    CHECK(act_hist_repair(g_dir, &rp));
    CHECK_EQ(rp.days, ACT_DAYS_KEEP);
    act_day_t first[1];
    CHECK_EQ(act_hist_days(g_dir, 10000 + ACT_DAYS_MAX + 5 - ACT_DAYS_KEEP,
                           10000 + ACT_DAYS_MAX + 5 - ACT_DAYS_KEEP, first, 1), 1);
    CHECK_EQ(act_hist_days(g_dir, 10000, 10000, first, 1), 0);
    /* Sesión terminada -> registro -> lectura idéntica */
    act_config_t cc;
    act_config_default(&cc);
    cc.weight_dkg = 700;
    rig_t r;
    rig_init(&r, &cc, 90);
    act_engine_start(&r.e, ACT_TYPE_WALK_OUT, gen_now(&r.g), r.local + 18000, false);
    rig_run(&r, 200, 110);
    act_engine_end(&r.e, gen_now(&r.g));
    act_ses_rec_t a, b;
    act_ses_from(&r.e.done, &r.e.cfg, &a);
    uint8_t rec[ACT_SES_REC_LEN];
    act_ses_pack(&a, rec);
    CHECK(act_ses_unpack(rec, &b));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK_EQ(a.start_utc, r.local + 18000 - 200);
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/flex_act_%d", (int)getpid());
    if (system(cmd) != 0) {
        fprintf(stderr, "no se pudo limpiar\n");
    }
}

/* ---- Configuración, catálogo y rangos ---- */

static void test_config_catalog(void)
{
    act_config_t c, d;
    act_config_default(&c);
    CHECK(c.autos.auto_pause && c.autos.auto_resume && c.autos.auto_end);
    CHECK_EQ(c.autos.pause_after_s, 20);
    CHECK_EQ(c.autos.end_after_s, 300);
    c.weight_dkg = 723;
    c.height_cm = 181;
    CHECK(act_config_toggle_fav(&c, ACT_TYPE_RUN_OUT));
    CHECK(act_config_toggle_fav(&c, 30));
    CHECK(act_config_is_fav(&c, 30));
    CHECK(act_config_toggle_fav(&c, ACT_TYPE_RUN_OUT)); /* quitar */
    CHECK(!act_config_is_fav(&c, ACT_TYPE_RUN_OUT));
    CHECK(!act_config_toggle_fav(&c, 250));              /* no existe */
    act_config_push_recent(&c, 3);
    act_config_push_recent(&c, 30);
    act_config_push_recent(&c, 3);
    CHECK_EQ(c.recent[0], 3);
    CHECK_EQ(c.recent[1], 30);
    CHECK_EQ(c.recent[2], 0);
    uint8_t buf[64];
    const size_t n = act_config_pack(&c, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(act_config_unpack(buf, n, ACT_CFG_VER, &d));
    CHECK(memcmp(&c, &d, sizeof(c)) == 0);
    CHECK(!act_config_unpack(buf, n - 1, ACT_CFG_VER, &d));
    CHECK(!act_config_unpack(buf, n, ACT_CFG_VER + 1, &d));
    CHECK_EQ(d.goal_steps, 8000); /* rechazado: valores por defecto */
    /* Valores fuera de rango guardados: se corrigen */
    memset(buf, 0xFF, sizeof(buf));
    CHECK(act_config_unpack(buf, n, ACT_CFG_VER, &d));
    CHECK(d.autos.pause_after_s <= ACT_PAUSE_MAX_S && d.goal_steps <= ACT_GOAL_STEPS_MAX);
    CHECK(d.weight_dkg <= ACT_WEIGHT_MAX_DKG && d.height_cm <= ACT_HEIGHT_MAX_CM);
    CHECK_EQ(d.fav[0], 0); /* ids que no existen: fuera */
    CHECK(d.sensitivity < ACT_SENS_COUNT);
    /* Catálogo: ids únicos, todos con categoría, icono y MET > 1 */
    const size_t cnt = act_catalog_count();
    CHECK(cnt >= 80);
    int per_cat[ACT_CAT_COUNT] = {0};
    for (size_t i = 0; i < cnt; i++) {
        const act_type_t *t = act_catalog_at(i);
        CHECK(t->cat < ACT_CAT_COUNT && t->icon < ACT_IC_COUNT && t->met10 > 10 && t->name[0]);
        per_cat[t->cat]++;
        for (size_t j = i + 1; j < cnt; j++) {
            CHECK(act_catalog_at(j)->id != t->id);
        }
        /* Los de solo duración no prometen pasos ni distancia; la pausa
         * automática solo existe con pasos */
        if (t->flags & ACT_F_DURATION) {
            CHECK(!(t->flags & (ACT_F_STEPS | ACT_F_DIST | ACT_F_AUTO)));
        }
        if (t->flags & (ACT_F_AUTO | ACT_F_DIST)) {
            CHECK(t->flags & ACT_F_STEPS);
        }
        if (t->kcal == ACT_KCAL_STEPS) {
            CHECK(t->flags & ACT_F_DIST);
        }
    }
    for (int k = 0; k < ACT_CAT_COUNT; k++) {
        CHECK(per_cat[k] > 0);
        CHECK(act_cat_name((act_cat_t)k)[0]);
    }
    CHECK(act_catalog_at(cnt) == NULL);
    CHECK(act_catalog_find(250) == NULL);
    CHECK_EQ(act_catalog_get(250)->id, ACT_TYPE_OTHER);
    const act_type_t *golf = act_catalog_find(65);
    CHECK(golf && (golf->flags & ACT_F_DURATION));
    uint8_t ids[16];
    CHECK_EQ(act_catalog_search("", ids, 16), 0);
    size_t k = act_catalog_search("cor", ids, 16);
    CHECK(k >= 3); /* Correr x2 y Core */
    k = act_catalog_search("NATACION", ids, 16);
    CHECK_EQ(k, 2);
    k = act_catalog_search("fútbol amer", ids, 16);
    CHECK_EQ(k, 1);
    CHECK_EQ(ids[0], 50);
    k = act_catalog_search("pádel", ids, 1);
    CHECK_EQ(k, 1); /* nunca escribe más de max */
    char fb[8];
    act_fold("ÁÉÍÓÚÑü largo", fb, sizeof(fb));
    CHECK(strcmp(fb, "aeiounu") == 0);
    /* Energía y anillos */
    CHECK_EQ(act_ee_steps(100, false), 500);
    CHECK_EQ(act_ee_steps(100, true), 1000);
    CHECK_EQ(act_ee_met(10, 3600000), 0);
    CHECK_EQ(act_ee_met(35, 3600000), 2500000); /* 2,5 kcal/kg en una hora a 3,5 MET */
    uint32_t kcal;
    act_config_default(&c);
    CHECK(!act_kcal(&c, 2500000, &kcal));
    c.weight_dkg = 800;
    CHECK(act_kcal(&c, 2500000, &kcal));
    CHECK_EQ(kcal, 200);
    act_rings_t rg;
    act_rings(&c, 800, true, 45 * 60, 4000, &rg);
    CHECK_EQ(rg.pct[0], 200);
    CHECK_EQ(rg.pct[1], 150);
    CHECK_EQ(rg.pct[2], 50);
    CHECK_EQ(rg.met_bits, 3);
    act_rings(&c, 0, false, 0, 0, &rg);
    CHECK_EQ(rg.pct[0], 0);
    CHECK(!rg.kcal_valid);
}

/* La sesión por sí sola: transiciones y tiempos exactos */
static void test_session_sm(void)
{
    act_session_t s;
    act_auto_cfg_t c;
    act_auto_cfg_default(&c);
    act_session_reset(&s);
    CHECK(!act_session_pause(&s, 0));
    CHECK(!act_session_end(&s, 0));
    CHECK(act_session_start(&s, 7, 1, true, 1000));
    CHECK_EQ(s.state, ACT_SES_ACTIVE);
    act_session_evidence(&s, 5000);
    CHECK_EQ(act_session_tick(&s, &c, 24000, false, 0), ACT_SEV_NONE);
    CHECK_EQ(act_session_tick(&s, &c, 25000, false, 0), ACT_SEV_AUTO_PAUSE);
    CHECK_EQ(act_session_tick(&s, &c, 25500, false, 0), ACT_SEV_NONE); /* no se pausa dos veces */
    CHECK_EQ(act_session_active_ms(&s, 30000), 4000);
    CHECK_EQ(act_session_paused_ms(&s, 30000), 25000);
    /* tren de 2 s: aún no reanuda; 3 s: sí, desde su primer paso */
    CHECK_EQ(act_session_tick(&s, &c, 40000, true, 38000), ACT_SEV_NONE);
    CHECK_EQ(act_session_tick(&s, &c, 41000, true, 38000), ACT_SEV_AUTO_RESUME);
    CHECK_EQ(act_session_active_ms(&s, 41000), 4000 + 3000);
    CHECK_EQ(act_session_paused_ms(&s, 41000), 33000);
    CHECK_EQ(act_session_total_ms(&s, 41000), 40000);
    CHECK(act_session_end(&s, 50000));
    CHECK_EQ(s.end_reason, ACT_END_MANUAL);
    CHECK_EQ(act_session_active_ms(&s, 99999), 16000);
    CHECK(!act_session_end(&s, 60000));
    CHECK(act_session_start(&s, 8, 30, false, 0)); /* desde COMPLETED se puede empezar otra */
}

/* Textos de la interfaz de Bienestar */
static void test_ui_texts(void)
{
    char b[32], c[8];
    wb_fmt_int(b, sizeof(b), 0);
    CHECK(strcmp(b, "0") == 0);
    wb_fmt_int(b, sizeof(b), 5855);
    CHECK(strcmp(b, "5.855") == 0);
    wb_fmt_int(b, sizeof(b), 1234567);
    CHECK(strcmp(b, "1.234.567") == 0);
    wb_fmt_dur(b, sizeof(b), 63000);
    CHECK(strcmp(b, "1:03") == 0);
    wb_fmt_dur(b, sizeof(b), 3723000);
    CHECK(strcmp(b, "1:02:03") == 0);
    wb_fmt_stopwatch(b, sizeof(b), c, sizeof(c), 1624320);
    CHECK(strcmp(b, "27:04") == 0 && strcmp(c, ",3") == 0);
    wb_fmt_stopwatch(b, sizeof(b), c, sizeof(c), 7200000);
    CHECK(strcmp(b, "2:00") == 0 && strcmp(c, "H") == 0);
    wb_fmt_dur_words(b, sizeof(b), 3900);
    CHECK(strcmp(b, "1 h 05 min") == 0);
    wb_fmt_dur_words(b, sizeof(b), 42);
    CHECK(strcmp(b, "42 s") == 0);
    wb_fmt_dist(b, sizeof(b), 4537);
    CHECK(strcmp(b, "4,54 km") == 0);
    wb_fmt_dist(b, sizeof(b), 850);
    CHECK(strcmp(b, "850 m") == 0);
    wb_fmt_pace(b, sizeof(b), 517);
    CHECK(strcmp(b, "8'37\"") == 0);
    wb_fmt_pace(b, sizeof(b), 0);
    CHECK(strcmp(b, "--") == 0);
    wb_fmt_pace(b, sizeof(b), 4000);
    CHECK(strcmp(b, "--") == 0);
    wb_fmt_weight(b, sizeof(b), 725);
    CHECK(strcmp(b, "72,5 kg") == 0);
    /* 1-1-1970 fue jueves */
    CHECK(strcmp(wb_day_letter(0), "J") == 0);
    CHECK(strcmp(wb_day_letter(4), "L") == 0);
    wb_fmt_day(b, sizeof(b), 20000, 20000);
    CHECK(strcmp(b, "Hoy") == 0);
    wb_fmt_day(b, sizeof(b), 19999, 20000);
    CHECK(strcmp(b, "Ayer") == 0);
    wb_fmt_day(b, sizeof(b), 20375, 20390); /* 14-10-2025, martes */
    CHECK(strcmp(b, "mar 14 oct") == 0);
    CHECK_EQ(wb_bar_h(0, 100, 50), 0);
    CHECK_EQ(wb_bar_h(1, 1000, 50), 2);
    CHECK_EQ(wb_bar_h(500, 1000, 50), 25);
    CHECK_EQ(wb_bar_h(2000, 1000, 50), 50);
    CHECK_EQ(wb_bar_h(5, 0, 50), 0);
}

void test_activity_all(void)
{
    test_ui_texts();
    test_steps_walk_run();
    test_steps_reject();
    test_steps_timing();
    test_distance();
    test_day();
    test_session_sm();
    test_auto();
    test_energy();
    test_suggest();
    test_state_restore();
    test_history();
    test_config_catalog();
}
