/*
 * Bienestar en la vista previa: la MISMA API que watch_activity, con el motor
 * real (act_engine: detector de pasos, sesiones, día) y el almacenamiento
 * real (act_store sobre una carpeta temporal). En vez de la tarea del reloj,
 * sim_wb_service() corre en cada paso de sim_run(): genera las muestras del
 * acelerómetro del perfil de movimiento elegido (quieto, caminar a N pasos
 * por minuto, gestos sueltos), las pasa al motor y hace el tick de cada
 * segundo con la hora simulada. Las muestras son SINTÉTICAS: sirven para
 * probar la interfaz y el flujo de datos, no el hardware.
 */
#define _DEFAULT_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sim.h"
#include "watch_activity.h"
#include "watch_time.h"

static act_engine_t s_eng;
static bool s_started, s_storage = true, s_imu = true;
static uint32_t s_gen, s_hist_gen, s_ui_seq, s_page_gen;
static uint8_t s_ui_ev;
static watch_activity_history_t s_hist;
static act_ses_rec_t s_page[WB_PAGE];
static int s_page_n;
static uint32_t s_page_skip;
static char s_dir[200];
static uint32_t s_last_ms, s_tick_ms;
static double s_phase, s_sample_acc;
static uint32_t s_index = 1000;
static double s_spm, s_amp;
static unsigned s_seed = 7;
static uint32_t s_saves;
int sim_wb_state_saves;

static void bump(void)
{
    s_gen++;
}

static double rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return ((s_seed >> 8) & 0xFFFF) / 65535.0;
}

static void clear_dir(void)
{
    char cmd[260];
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/flex_sim_wb_%d", (int)getpid());
    if (system(cmd) != 0) {
        printf("aviso: no se pudo limpiar la carpeta de Bienestar\n");
    }
    snprintf(s_dir, sizeof(s_dir), "/tmp/flex_sim_wb_%d/.sistema/bienestar", (int)getpid());
}

static void rebuild_history(void)
{
    watch_activity_history_t h = s_hist;
    h.today = s_eng.day.day;
    h.storage = s_storage;
    if (s_storage && s_eng.day.day != ACT_NO_DAY) {
        act_hist_days(s_dir, s_eng.day.day - WB_HIST_DAYS, s_eng.day.day - 1, h.days, WB_HIST_DAYS);
        h.nrecent = (uint8_t)act_hist_sessions(s_dir, 0, h.recent, WB_HIST_RECENT);
        act_stats_build(s_dir, &s_eng.day, &s_eng.cfg, &h.stats);
        if (h.nrecent) {
            h.last = h.recent[0];
            h.last_valid = true;
        }
    }
    h.gen = s_hist.gen + 1;
    s_hist = h;
    s_hist_gen = h.gen;
    bump();
}

static void ensure(void);

void sim_wb_reset(const act_config_t *cfg, bool storage)
{
    act_engine_init(&s_eng, cfg);
    memset(&s_hist, 0, sizeof(s_hist));
    for (int i = 0; i < WB_HIST_DAYS; i++) {
        s_hist.days[i].day = ACT_NO_DAY;
    }
    act_stats_init(&s_hist.stats);
    s_storage = storage;
    clear_dir();
    if (storage) {
        act_hist_mkdir(s_dir);
    }
    s_spm = 0;
    s_amp = 0;
    s_imu = true;
    s_started = true;
    s_last_ms = s_tick_ms = 0;
    s_ui_seq++;
    s_ui_ev = 0;
    rebuild_history();
}

void sim_wb_motion(double spm, double amp)
{
    s_spm = spm;
    s_amp = amp;
}

void sim_wb_imu(bool present)
{
    s_imu = present;
}

const act_engine_t *sim_wb_engine(void)
{
    return &s_eng;
}

const char *sim_wb_dir(void)
{
    return s_dir;
}

static void sample(uint32_t now_ms)
{
    const double P = ACT_SAMPLE_PERIOD_US / 1e6;
    act_sample_t s;
    double x = 30, y = -20, z = 1000;
    if (s_spm > 0) {
        s_phase += 2.0 * M_PI * (s_spm / 60.0) * P;
        const double th = 0.45 * sin(s_phase / 2.0);
        const double dyn = s_amp * (0.55 * sin(s_phase) + 0.25 * sin(2.0 * s_phase + 0.6));
        x = 0.3 * dyn;
        y = 1000.0 * sin(th) + 0.2 * dyn;
        z = 1000.0 * cos(th) + 0.9 * dyn;
    } else if (s_spm < 0) {
        /* gestos sueltos: un pulso de 0,5 s cada 3 s */
        s_phase += P;
        const double u = fmod(s_phase, 3.0);
        if (u < 0.5) {
            const double d = 800.0 * sin(M_PI * u / 0.5) * sin(2.0 * M_PI * u * 3.0);
            x += d;
            y += 0.5 * d;
        }
    }
    const double nz = 6.0 * (rnd() - 0.5);
    s.index = ++s_index;
    s.t_ms = now_ms;
    s.a[0] = (int16_t)lround(x + nz);
    s.a[1] = (int16_t)lround(y + nz);
    s.a[2] = (int16_t)lround(z + nz);
    s.restart = false;
    act_engine_feed(&s_eng, &s, 1);
}

static void save_state(void)
{
    if (!s_storage) {
        return;
    }
    uint8_t buf[ACT_STATE_MAX];
    int64_t utc = 0;
    watch_time_now_utc(&utc);
    const size_t n = act_state_pack(&s_eng, s_last_ms, utc, buf, sizeof(buf));
    if (n && act_state_save(s_dir, buf, n)) {
        s_saves++;
        sim_wb_state_saves++;
    }
}

static void handle(uint32_t ev)
{
    bool hist = false;
    if (ev & ACT_EV_DAY_CLOSED) {
        if (s_storage) {
            act_hist_append_day(s_dir, &s_eng.closed);
        } else {
            memmove(&s_hist.days[0], &s_hist.days[1], sizeof(s_hist.days[0]) * (WB_HIST_DAYS - 1));
            s_hist.days[WB_HIST_DAYS - 1] = s_eng.closed;
        }
        hist = true;
    }
    if (ev & ACT_EV_SESSION_DONE) {
        act_ses_rec_t r;
        act_ses_from(&s_eng.done, &s_eng.cfg, &r);
        if (s_storage) {
            act_hist_append_ses(s_dir, &r);
        } else {
            memmove(&s_hist.recent[1], &s_hist.recent[0], sizeof(s_hist.recent[0]) * (WB_HIST_RECENT - 1));
            s_hist.recent[0] = r;
            s_hist.nrecent = s_hist.nrecent < WB_HIST_RECENT ? s_hist.nrecent + 1 : WB_HIST_RECENT;
            s_hist.last = r;
            s_hist.last_valid = true;
        }
        hist = true;
    }
    if (ev & (ACT_EV_SESSION | ACT_EV_SESSION_DONE | ACT_EV_DAY_CLOSED)) {
        save_state();
    }
    const uint8_t ui = (uint8_t)(((ev & ACT_EV_AUTO_PAUSE) ? WB_UI_AUTO_PAUSE : 0) |
                                 ((ev & ACT_EV_AUTO_RESUME) ? WB_UI_AUTO_RESUME : 0) |
                                 ((ev & ACT_EV_AUTO_END) ? WB_UI_AUTO_END : 0) |
                                 ((ev & ACT_EV_SUGGEST) ? WB_UI_SUGGEST : 0));
    if (ui) {
        s_ui_ev = ui;
        s_ui_seq++;
    }
    if (hist) {
        rebuild_history();
    }
}

static void tick(uint32_t now_ms)
{
    wt_datetime_t dt;
    const int64_t local = watch_time_now(&dt) == WATCH_TIME_OK ? wt_to_seconds(&dt) : -1;
    int64_t utc = 0;
    watch_time_now_utc(&utc);
    act_engine_set_sensor(&s_eng, s_imu);
    act_engine_tick(&s_eng, now_ms, local, utc);
    handle(act_engine_take_events(&s_eng));
    bump();
}

static void ensure(void)
{
    if (!s_started) {
        sim_wb_reset(NULL, true);
    }
}

void sim_wb_service(uint32_t now_ms)
{
    ensure();
    if (!s_last_ms) {
        s_last_ms = s_tick_ms = now_ms;
        return;
    }
    const bool count = s_imu && (s_eng.cfg.tracking || act_session_running(&s_eng.wk.s));
    s_sample_acc += (double)(now_ms - s_last_ms);
    s_last_ms = now_ms;
    const double P = ACT_SAMPLE_PERIOD_US / 1000.0;
    while (s_sample_acc >= P) {
        s_sample_acc -= P;
        if (count) {
            sample(now_ms);
        }
    }
    if (now_ms - s_tick_ms >= 1000) {
        s_tick_ms = now_ms;
        tick(now_ms);
    }
}

/* ---- API de watch_activity ---- */

esp_err_t watch_activity_start(void)
{
    ensure();
    return ESP_OK;
}

void watch_activity_get(watch_activity_status_t *out)
{
    ensure();
    memset(out, 0, sizeof(*out));
    act_engine_view(&s_eng, &out->v);
    out->started = s_started;
    out->imu_present = s_imu;
    out->imu_ok = s_imu && (s_eng.cfg.tracking || act_session_running(&s_eng.wk.s));
    out->storage_ok = s_storage;
    out->persistent = true;
    out->ui_seq = s_ui_seq;
    out->ui_ev = s_ui_ev;
    out->samples = s_eng.det.samples;
    out->gaps = s_eng.det.gaps;
    out->dups = s_eng.det.dups;
    out->invalid = s_eng.det.invalid;
    out->ignored = s_eng.det.ignored;
    out->period_ms = 20;
    out->saves = s_saves;
}

uint32_t watch_activity_generation(void)
{
    return s_gen;
}

void watch_activity_config(act_config_t *out)
{
    ensure();
    *out = s_eng.cfg;
}

void watch_activity_set_config(const act_config_t *c)
{
    ensure();
    act_engine_set_config(&s_eng, c);
    rebuild_history();
}

static esp_err_t cmd(int op, uint8_t type, bool flag)
{
    ensure();
    const uint32_t now = s_last_ms ? s_last_ms : 1;
    int64_t utc = 0;
    watch_time_now_utc(&utc);
    bool ok = false;
    switch (op) {
    case 0:
        ok = act_engine_start(&s_eng, type, now, utc, flag);
        break;
    case 1:
        ok = act_engine_pause(&s_eng, now);
        break;
    case 2:
        ok = act_engine_resume(&s_eng, now);
        break;
    default:
        ok = act_engine_end(&s_eng, now);
        break;
    }
    if (ok) {
        handle(act_engine_take_events(&s_eng));
        bump();
    }
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t watch_activity_workout_start(uint8_t type, bool from_suggestion)
{
    return cmd(0, type, from_suggestion);
}

esp_err_t watch_activity_workout_pause(void)
{
    return cmd(1, 0, false);
}

esp_err_t watch_activity_workout_resume(void)
{
    return cmd(2, 0, false);
}

esp_err_t watch_activity_workout_end(void)
{
    return cmd(3, 0, false);
}

void watch_activity_suggest_dismiss(void)
{
    act_engine_dismiss_suggest(&s_eng);
    bump();
}

void watch_activity_calib_start(bool run)
{
    act_engine_calib_start(&s_eng, run);
    bump();
}

uint32_t watch_activity_calib_stop(void)
{
    const uint32_t n = act_engine_calib_steps(&s_eng);
    act_engine_calib_stop(&s_eng);
    bump();
    return n;
}

bool watch_activity_history(watch_activity_history_t *out)
{
    ensure();
    *out = s_hist;
    return s_hist.gen != 0;
}

uint32_t watch_activity_history_gen(void)
{
    return s_hist_gen;
}

void watch_activity_sessions_request(uint32_t skip)
{
    s_page_n = s_storage ? act_hist_sessions(s_dir, skip, s_page, WB_PAGE) : 0;
    s_page_skip = skip;
    s_page_gen++;
    bump();
}

int watch_activity_sessions_page(uint32_t *skip, act_ses_rec_t *out, int max, uint32_t *gen)
{
    const int n = s_page_n < max ? s_page_n : max;
    if (n > 0) {
        memcpy(out, s_page, sizeof(*out) * (size_t)n);
    }
    if (skip) {
        *skip = s_page_skip;
    }
    if (gen) {
        *gen = s_page_gen;
    }
    return n;
}
