/*
 * watch_activity — ver include/watch_activity.h
 */
#include "watch_activity.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "watch_imu.h"
#include "watch_settings.h"
#include "watch_storage.h"
#include "watch_time.h"

static const char *TAG = "watch_act";

#define TASK_STACK      6144   /* stdio + LittleFS al guardar; se mide (stack_free) */
#define TASK_PRIO       1      /* por debajo del IMU (2): nunca lo retrasa */
#define TASK_CORE       0      /* LVGL va en el 1 */
#define RING_CAP        64     /* ~1,3 s a 49 Hz; la tarea despierta a la mitad */
#define BATCH           16     /* 256 B: el bucle vacía el anillo por tandas */
#define TICK_MS         1000
#define CKPT_FS_MS      (5u * 60u * 1000u)   /* punto de control en LittleFS */
#define CKPT_NVS_MS     (15u * 60u * 1000u)  /* en NVS (sin LittleFS): menos a menudo */
#define STALE_MS        3000   /* sin muestras: el sensor no da datos */
#define STORAGE_WAIT_MS 6000   /* a que watch_storage intente montar al arrancar */
#define STILL_SLOW_S    30     /* quieto tanto tiempo y sin sesión: leer a 25 Hz */
#define PERIOD_FAST     20
#define PERIOD_SLOW     40
#define MAX_GAP_MS      (7u * 86400000u)
#define DIR_PATH        WATCH_STORAGE_BASE "/.sistema/bienestar"
#define STATE_VER       1

#define NOTE_SAMPLES (1u << 0)
#define NOTE_CMD     (1u << 1)
#define NOTE_HIST    (1u << 2)
#define NOTE_PAGE    (1u << 3)
#define NOTE_CONFIG  (1u << 4)

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_started;
static act_engine_t s_eng;              /* RAM interna: se toca con cada muestra */
static watch_activity_history_t *s_hist; /* PSRAM: caché que copia la interfaz */
static watch_activity_history_t *s_hist_tmp;
static act_ses_rec_t *s_page;
static uint32_t s_page_skip, s_page_req, s_page_gen;
static int s_page_n;
static uint32_t s_gen, s_hist_gen;
static watch_imu_sample_t s_batch[BATCH];
static uint8_t s_ckpt[ACT_STATE_MAX];
static uint8_t s_ckpt_last[ACT_STATE_MAX]; /* lo último guardado: sin cambios no se escribe */
static size_t s_ckpt_last_n;
static struct {
    bool imu_present, imu_ok, storage_ok, persistent, stream_on;
    uint32_t ui_seq;
    uint8_t ui_ev;
    uint32_t dropped;
    uint16_t period;
    uint32_t stack_free;
    uint32_t saves, save_errors;
} s_st;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void bump(void)
{
    __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
}

/* Hora local y UTC (o -1 / 0 si no hay hora válida). */
static void clock_now(int64_t *local_s, int64_t *utc_s)
{
    wt_datetime_t dt;
    *local_s = watch_time_now(&dt) == WATCH_TIME_OK ? wt_to_seconds(&dt) : -1;
    if (!watch_time_now_utc(utc_s)) {
        *utc_s = 0;
    }
}

static bool storage_mounted(void)
{
    watch_storage_info_t si;
    watch_storage_info(&si);
    return si.state == STG_MOUNTED;
}

/* Antes de escribir en LittleFS: la carpeta sigue ahí (Almacenamiento pudo
 * formatear o desmontar). Si no, se pasa a NVS hasta que vuelva. */
static bool storage_check(void)
{
    if (!s_st.storage_ok) {
        return false;
    }
    if (storage_mounted() && act_hist_mkdir(DIR_PATH)) {
        return true;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.storage_ok = false;
    s_st.persistent = watch_settings_persistent();
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "LittleFS no disponible: el estado va a NVS y el historial se pausa");
    return false;
}

/* ---- Configuración ---- */

static void load_config(act_config_t *c)
{
    uint8_t buf[64];
    size_t n = sizeof(buf);
    uint8_t ver = 0;
    if (watch_settings_blob_get(WS_BLOB_WELLNESS, &ver, buf, &n) != ESP_OK || !act_config_unpack(buf, n, ver, c)) {
        act_config_default(c);
    }
}

static void save_config(const act_config_t *c)
{
    uint8_t buf[64];
    const size_t n = act_config_pack(c, buf, sizeof(buf));
    if (n) {
        watch_settings_blob_set(WS_BLOB_WELLNESS, ACT_CFG_VER, buf, n);
    }
}

/* ---- Persistencia (siempre SIN el cerrojo) ---- */

/* Cabecera (2) + hora del guardado (8) al principio y CRC (4) al final:
 * lo que hay entre medias son los datos. */
#define CKPT_DATA_OFF 10
#define CKPT_CRC_LEN  4

static void save_state(void)
{
    int64_t local, utc;
    clock_now(&local, &utc);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t n = act_state_pack(&s_eng, now_ms(), utc, s_ckpt, sizeof(s_ckpt));
    xSemaphoreGive(s_lock);
    if (!n) {
        return;
    }
    if (n == s_ckpt_last_n && memcmp(s_ckpt + CKPT_DATA_OFF, s_ckpt_last + CKPT_DATA_OFF,
                                     n - CKPT_DATA_OFF - CKPT_CRC_LEN) == 0) {
        return; /* nada nuevo (de noche, sin pasos): la flash no se toca */
    }
    bool ok;
    if (storage_check()) {
        ok = act_state_save(DIR_PATH, s_ckpt, n);
        if (ok) {
            /* el respaldo de NVS ya no hace falta: se borra (si lo había) */
            watch_settings_blob_set(WS_BLOB_WB_STATE, STATE_VER, NULL, 0);
        }
    } else {
        ok = watch_settings_blob_set(WS_BLOB_WB_STATE, STATE_VER, s_ckpt, n) == ESP_OK;
    }
    if (ok) {
        memcpy(s_ckpt_last, s_ckpt, n);
        s_ckpt_last_n = n;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (ok) {
        s_st.saves++;
    } else {
        s_st.save_errors++;
    }
    xSemaphoreGive(s_lock);
    if (!ok) {
        ESP_LOGW(TAG, "no se pudo guardar el estado (%s)", s_st.storage_ok ? "LittleFS" : "NVS");
    }
}

static size_t load_state(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    if (s_st.storage_ok) {
        n = act_state_load(DIR_PATH, buf, cap);
    }
    if (!n) {
        uint8_t ver = 0;
        size_t m = cap;
        if (watch_settings_blob_get(WS_BLOB_WB_STATE, &ver, buf, &m) == ESP_OK && ver == STATE_VER) {
            n = m;
        }
    }
    return n;
}

/* ---- Caché del historial ---- */

/* Rehace la caché leyendo los archivos (en la tarea, sin el cerrojo). */
static void rebuild_history(void)
{
    if (!s_hist || !s_hist_tmp) {
        return;
    }
    watch_activity_history_t *h = s_hist_tmp;
    act_config_t cfg;
    act_day_t today;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *h = *s_hist; /* sin LittleFS se conserva lo de este arranque */
    cfg = s_eng.cfg;
    today = s_eng.day;
    xSemaphoreGive(s_lock);
    h->today = today.day;
    h->storage = s_st.storage_ok;
    if (s_st.storage_ok && today.day != ACT_NO_DAY) {
        act_hist_days(DIR_PATH, today.day - WB_HIST_DAYS, today.day - 1, h->days, WB_HIST_DAYS);
        h->nrecent = (uint8_t)act_hist_sessions(DIR_PATH, 0, h->recent, WB_HIST_RECENT);
        act_stats_build(DIR_PATH, &today, &cfg, &h->stats);
        if (h->nrecent) {
            h->last = h->recent[0];
            h->last_valid = true;
        }
    } else if (!s_st.storage_ok) {
        /* Sin historial guardado: las estadísticas solo con hoy y lo de este arranque. */
        act_stats_init(&h->stats);
        int32_t prev = ACT_NO_DAY;
        uint16_t run = 0;
        for (int i = 0; i < WB_HIST_DAYS; i++) {
            act_stats_add_day(&h->stats, &h->days[i], &prev, &run);
        }
        h->stats.sessions = h->nrecent;
        act_stats_finish(&h->stats, today.day, false, prev, run);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    h->gen = s_hist->gen + 1;
    *s_hist = *h;
    s_hist_gen = h->gen;
    xSemaphoreGive(s_lock);
    bump();
}

/* Sin LittleFS: el día cerrado y la sesión terminada se quedan en la caché. */
static void hist_ram_day(const act_day_t *d)
{
    if (!s_hist || d->day == ACT_NO_DAY) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memmove(&s_hist->days[0], &s_hist->days[1], sizeof(s_hist->days[0]) * (WB_HIST_DAYS - 1));
    s_hist->days[WB_HIST_DAYS - 1] = *d;
    xSemaphoreGive(s_lock);
}

static void hist_ram_session(const act_ses_rec_t *r)
{
    if (!s_hist) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memmove(&s_hist->recent[1], &s_hist->recent[0], sizeof(s_hist->recent[0]) * (WB_HIST_RECENT - 1));
    s_hist->recent[0] = *r;
    if (s_hist->nrecent < WB_HIST_RECENT) {
        s_hist->nrecent++;
    }
    s_hist->last = *r;
    s_hist->last_valid = true;
    xSemaphoreGive(s_lock);
}

/* ---- Eventos del motor ---- */

static void handle_events(uint32_t ev, bool *ckpt_now, bool *hist_now)
{
    if (ev & ACT_EV_DAY_CLOSED) {
        act_day_t d;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        d = s_eng.closed;
        xSemaphoreGive(s_lock);
        if (storage_check()) {
            if (!act_hist_append_day(DIR_PATH, &d)) {
                ESP_LOGW(TAG, "no se pudo guardar el día %ld", (long)d.day);
            }
        }
        hist_ram_day(&d);
        ESP_LOGI(TAG, "día cerrado: %lu pasos, %lu s de ejercicio", (unsigned long)d.steps,
                 (unsigned long)(d.ex_wk_s + d.ex_auto_s));
        *ckpt_now = *hist_now = true;
    }
    if (ev & ACT_EV_SESSION_DONE) {
        act_ses_rec_t r;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        act_ses_from(&s_eng.done, &s_eng.cfg, &r);
        xSemaphoreGive(s_lock);
        /* Primero el registro y después el estado sin la sesión: un corte en
         * medio deja la sesión dos veces como mucho, y la segunda se descarta
         * por su id (nunca se pierde ni se duplica). */
        if (storage_check() && !act_hist_append_ses(DIR_PATH, &r)) {
            ESP_LOGW(TAG, "no se pudo guardar el entrenamiento %lu", (unsigned long)r.id);
        }
        hist_ram_session(&r);
        ESP_LOGI(TAG, "entrenamiento %lu terminado (%s): %lu s activos, %lu s en pausa, %lu pasos",
                 (unsigned long)r.id, r.end_reason == ACT_END_AUTO ? "automático" : "manual",
                 (unsigned long)r.active_s, (unsigned long)r.paused_s, (unsigned long)r.steps);
        *ckpt_now = *hist_now = true;
    }
    if (ev & ACT_EV_SESSION) {
        *ckpt_now = true;
    }
    if (ev & ACT_EV_CONFIG) {
        act_config_t c;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        c = s_eng.cfg;
        xSemaphoreGive(s_lock);
        save_config(&c);
    }
    const uint8_t ui = (uint8_t)(((ev & ACT_EV_AUTO_PAUSE) ? WB_UI_AUTO_PAUSE : 0) |
                                 ((ev & ACT_EV_AUTO_RESUME) ? WB_UI_AUTO_RESUME : 0) |
                                 ((ev & ACT_EV_AUTO_END) ? WB_UI_AUTO_END : 0) |
                                 ((ev & ACT_EV_SUGGEST) ? WB_UI_SUGGEST : 0));
    if (ui) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.ui_ev = ui;
        s_st.ui_seq++;
        xSemaphoreGive(s_lock);
        bump();
    }
}

/* ---- IMU ---- */

/* El flujo se abre si hay que contar (seguimiento activo o entrenamiento en marcha). */
static void update_stream(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool want = s_eng.cfg.tracking || act_session_running(&s_eng.wk.s);
    xSemaphoreGive(s_lock);
    if (want && !s_st.stream_on && s_st.imu_present) {
        const esp_err_t err = watch_imu_stream_open(RING_CAP, xTaskGetCurrentTaskHandle(), NOTE_SAMPLES);
        s_st.stream_on = err == ESP_OK;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "flujo del IMU no disponible: %s", esp_err_to_name(err));
            s_st.imu_present = err != ESP_ERR_INVALID_STATE;
        }
    } else if (!want && s_st.stream_on) {
        watch_imu_stream_close();
        s_st.stream_on = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        act_engine_gap(&s_eng);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "seguimiento desactivado: IMU en reposo");
    }
}

/* Vacía el anillo en el motor. Devuelve cuántas muestras llegaron. */
static size_t drain(void)
{
    size_t total = 0;
    for (;;) {
        uint32_t dropped = 0;
        const size_t n = watch_imu_stream_read(s_batch, BATCH, &dropped);
        if (!n && !dropped) {
            break;
        }
        const int64_t now = now_ms();
        const uint32_t now32 = (uint32_t)now;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (dropped) {
            act_engine_gap(&s_eng); /* el anillo se desbordó: sin inventar lo perdido */
            s_st.dropped += dropped;
        }
        for (size_t i = 0; i < n; i++) {
            act_sample_t a;
            a.index = s_batch[i].index;
            a.t_ms = now - (int64_t)(uint32_t)(now32 - s_batch[i].t_ms); /* de 32 a 64 bits */
            a.a[0] = s_batch[i].a_mg[0];
            a.a[1] = s_batch[i].a_mg[1];
            a.a[2] = s_batch[i].a_mg[2];
            a.restart = s_batch[i].restart != 0;
            act_engine_feed(&s_eng, &a, 1);
        }
        xSemaphoreGive(s_lock);
        total += n;
        if (n < BATCH) {
            break;
        }
    }
    return total;
}

/* ---- Tarea ---- */

static void restore_at_boot(void)
{
    const size_t n = load_state(s_ckpt, sizeof(s_ckpt));
    act_day_t day;
    act_workout_t wk;
    uint32_t next = 0;
    int64_t saved_utc = 0;
    const bool ok = n && act_state_unpack(s_ckpt, n, &day, &wk, &next, &saved_utc);
    int64_t local, utc;
    clock_now(&local, &utc);
    const int32_t today = local >= 0 ? (int32_t)(local / 86400) : ACT_NO_DAY;
    uint32_t gap = 0;
    if (ok && saved_utc > 0 && utc > saved_utc) {
        const int64_t g = (utc - saved_utc) * 1000;
        gap = g > MAX_GAP_MS ? MAX_GAP_MS : (uint32_t)g;
    }
    /* ids que no se repiten aunque el estado se perdiera */
    const uint32_t max_id = s_st.storage_ok ? act_hist_max_id(DIR_PATH) : 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (ok) {
        act_engine_restore(&s_eng, &day, &wk, next > max_id ? next : max_id + 1, now_ms(), today, gap);
    } else if (max_id) {
        act_engine_restore(&s_eng, NULL, NULL, max_id + 1, now_ms(), today, 0);
    }
    xSemaphoreGive(s_lock);
    if (ok) {
        ESP_LOGI(TAG, "estado restaurado: %lu pasos hoy%s (apagado %lu s)", (unsigned long)day.steps,
                 act_session_running(&wk.s) ? ", entrenamiento en pausa" : "", (unsigned long)(gap / 1000));
    } else if (n) {
        ESP_LOGW(TAG, "estado guardado dañado o de otra versión: no se usa");
    }
}

static void act_task(void *arg)
{
    (void)arg;
    /* LittleFS lo monta watch_storage en su tarea: se espera a que lo intente. */
    const int64_t t0 = now_ms();
    watch_storage_info_t si;
    do {
        watch_storage_info(&si);
        if (si.state != STG_LOCKED || si.gate != STG_GATE_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    } while (now_ms() - t0 < STORAGE_WAIT_MS);
    const bool fs = storage_mounted() && act_hist_mkdir(DIR_PATH);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.storage_ok = fs;
    s_st.persistent = fs || watch_settings_persistent();
    xSemaphoreGive(s_lock);
    if (s_st.storage_ok) {
        act_repair_t rp;
        if (act_hist_repair(DIR_PATH, &rp) && rp.rewritten) {
            ESP_LOGW(TAG, "historial reparado: %lu registros quitados (cortes o duplicados)",
                     (unsigned long)rp.dropped);
        }
    }
    restore_at_boot();
    rebuild_history();
    update_stream();
    ESP_LOGI(TAG, "en marcha: IMU %s, historial %s", s_st.stream_on ? "contando" : "sin flujo",
             s_st.storage_ok ? "en LittleFS" : "solo en RAM (sin LittleFS)");

    int64_t last_tick = 0, last_sample = now_ms(), last_ckpt = now_ms();
    uint32_t ckpt_gen = 0;
    uint16_t period = PERIOD_FAST;
    uint8_t goal_bits = 0xFF;
    uint32_t steps_mark = 0;
    for (;;) {
        uint32_t note = 0;
        xTaskNotifyWait(0, UINT32_MAX, &note, pdMS_TO_TICKS(TICK_MS));
        if (note & NOTE_CONFIG) {
            update_stream();
        }
        if (s_st.stream_on && drain()) {
            last_sample = now_ms();
        }
        const int64_t now = now_ms();
        /* ¿llegan datos? Sin ellos no hay pausa automática (no hay evidencia). */
        const bool fresh = s_st.stream_on && now - last_sample < STALE_MS;
        bool ckpt_now = false, hist_now = (note & NOTE_HIST) != 0;
        if (now - last_tick >= TICK_MS || (note & NOTE_CMD)) {
            last_tick = now;
            int64_t local, utc;
            clock_now(&local, &utc);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_st.imu_ok != fresh) {
                if (!fresh) {
                    act_engine_gap(&s_eng);
                }
                s_st.imu_ok = fresh;
            }
            act_engine_set_sensor(&s_eng, fresh);
            act_engine_tick(&s_eng, now, local, utc);
            const uint32_t ev = act_engine_take_events(&s_eng);
            /* Lectura más lenta solo con el reloj quieto y sin entrenamiento */
            const bool slow = s_eng.det.still_s >= STILL_SLOW_S && !act_session_running(&s_eng.wk.s);
            act_view_t v;
            act_engine_view(&s_eng, &v);
            xSemaphoreGive(s_lock);
            const uint16_t want = slow ? PERIOD_SLOW : PERIOD_FAST;
            if (want != period && s_st.stream_on) {
                period = want;
                watch_imu_stream_set_period(period);
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_st.period = period;
                xSemaphoreGive(s_lock);
            }
            handle_events(ev, &ckpt_now, &hist_now);
            /* Las estadísticas (racha, logros) cambian con los objetivos de hoy. */
            const uint32_t mark = v.steps >= 20000 ? 2 : (v.steps >= 10000 ? 1 : 0);
            if (v.rings.met_bits != goal_bits || mark != steps_mark) {
                hist_now |= goal_bits != 0xFF;
                goal_bits = v.rings.met_bits;
                steps_mark = mark;
            }
            if (ev & (ACT_EV_SESSION | ACT_EV_SESSION_DONE)) {
                update_stream(); /* entrenamiento con el seguimiento apagado */
            }
            bump();
        }
        /* LittleFS pudo formatearse o montarse después del arranque */
        if (!s_st.storage_ok && storage_mounted() && act_hist_mkdir(DIR_PATH)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.storage_ok = s_st.persistent = true;
            xSemaphoreGive(s_lock);
            ckpt_now = hist_now = true;
            ESP_LOGI(TAG, "LittleFS disponible: el historial se guarda desde ahora");
        }
        const uint32_t ckpt_every = s_st.storage_ok ? CKPT_FS_MS : CKPT_NVS_MS;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const uint32_t g = s_eng.gen;
        xSemaphoreGive(s_lock);
        if (ckpt_now || (g != ckpt_gen && now - last_ckpt >= (int64_t)ckpt_every)) {
            save_state();
            ckpt_gen = g;
            last_ckpt = now;
            const uint32_t sf = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.stack_free = sf;
            xSemaphoreGive(s_lock);
        }
        if (hist_now) {
            rebuild_history();
        }
        if (note & NOTE_PAGE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const uint32_t skip = s_page_req;
            xSemaphoreGive(s_lock);
            act_ses_rec_t *tmp = s_page;
            const int n = s_st.storage_ok ? act_hist_sessions(DIR_PATH, skip, tmp, WB_PAGE) : 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_page_n = n;
            s_page_skip = skip;
            s_page_gen++;
            xSemaphoreGive(s_lock);
            bump();
        }
    }
}

/* ---- API ---- */

esp_err_t watch_activity_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    /* Reservas de una vez: caché del historial y página (PSRAM si la hay). */
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_hist = heap_caps_calloc(1, sizeof(*s_hist), caps);
    s_hist_tmp = heap_caps_calloc(1, sizeof(*s_hist_tmp), caps);
    s_page = heap_caps_calloc(WB_PAGE, sizeof(*s_page), caps);
    if (!s_hist || !s_hist_tmp || !s_page) {
        heap_caps_free(s_hist);
        heap_caps_free(s_hist_tmp);
        heap_caps_free(s_page);
        s_hist = s_hist_tmp = NULL;
        s_page = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        ESP_LOGE(TAG, "sin PSRAM para la caché del historial");
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < WB_HIST_DAYS; i++) {
        s_hist->days[i].day = ACT_NO_DAY;
    }
    act_stats_init(&s_hist->stats);
    act_config_t cfg;
    load_config(&cfg);
    act_engine_init(&s_eng, &cfg);
    watch_imu_snapshot_t snap;
    s_st.imu_present = watch_imu_get(&snap) && snap.state != IMU_ABSENT && snap.state != IMU_ERROR;
    s_st.period = PERIOD_FAST;
    s_started = true;
    if (xTaskCreatePinnedToCore(act_task, "watch_act", TASK_STACK, NULL, TASK_PRIO, &s_task, TASK_CORE) != pdPASS) {
        s_started = false;
        ESP_LOGE(TAG, "no se pudo crear la tarea");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "servicio de actividad: motor %u B (RAM interna), caché %u B (PSRAM), anillo %u muestras",
             (unsigned)sizeof(s_eng), (unsigned)(2 * sizeof(*s_hist) + WB_PAGE * sizeof(*s_page)),
             (unsigned)RING_CAP);
    return ESP_OK;
}

void watch_activity_get(watch_activity_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_started) {
        out->v.day = ACT_NO_DAY;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    act_engine_view(&s_eng, &out->v);
    out->started = true;
    out->imu_present = s_st.imu_present;
    out->imu_ok = s_st.imu_ok;
    out->storage_ok = s_st.storage_ok;
    out->persistent = s_st.persistent;
    out->ui_seq = s_st.ui_seq;
    out->ui_ev = s_st.ui_ev;
    out->samples = s_eng.det.samples;
    out->dropped = s_st.dropped;
    out->gaps = s_eng.det.gaps;
    out->dups = s_eng.det.dups;
    out->invalid = s_eng.det.invalid;
    out->ignored = s_eng.det.ignored;
    out->period_ms = s_st.period;
    out->stack_free = s_st.stack_free;
    out->saves = s_st.saves;
    out->save_errors = s_st.save_errors;
    xSemaphoreGive(s_lock);
}

uint32_t watch_activity_generation(void)
{
    return __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
}

void watch_activity_config(act_config_t *out)
{
    if (!s_started) {
        load_config(out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_eng.cfg;
    xSemaphoreGive(s_lock);
}

void watch_activity_set_config(const act_config_t *c)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    act_engine_set_config(&s_eng, c);
    const act_config_t saved = s_eng.cfg;
    xSemaphoreGive(s_lock);
    save_config(&saved); /* RAM al momento; NVS lo escribe watch_settings por lotes */
    xTaskNotify(s_task, NOTE_CONFIG | NOTE_HIST, eSetBits);
    bump();
}

static esp_err_t command(int op, uint8_t type, bool flag)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t local, utc;
    clock_now(&local, &utc);
    (void)local;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t now = now_ms();
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
    case 3:
        ok = act_engine_end(&s_eng, now);
        break;
    default:
        break;
    }
    xSemaphoreGive(s_lock);
    if (ok) {
        xTaskNotify(s_task, NOTE_CMD | NOTE_CONFIG, eSetBits);
        bump();
    }
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t watch_activity_workout_start(uint8_t type, bool from_suggestion)
{
    return command(0, type, from_suggestion);
}

esp_err_t watch_activity_workout_pause(void)
{
    return command(1, 0, false);
}

esp_err_t watch_activity_workout_resume(void)
{
    return command(2, 0, false);
}

esp_err_t watch_activity_workout_end(void)
{
    return command(3, 0, false);
}

void watch_activity_suggest_dismiss(void)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    act_engine_dismiss_suggest(&s_eng);
    xSemaphoreGive(s_lock);
    bump();
}

void watch_activity_calib_start(bool run)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    act_engine_calib_start(&s_eng, run);
    xSemaphoreGive(s_lock);
    bump();
}

uint32_t watch_activity_calib_stop(void)
{
    if (!s_started) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t n = act_engine_calib_steps(&s_eng);
    act_engine_calib_stop(&s_eng);
    xSemaphoreGive(s_lock);
    bump();
    return n;
}

bool watch_activity_history(watch_activity_history_t *out)
{
    if (!s_started || !s_hist) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = *s_hist;
    xSemaphoreGive(s_lock);
    return out->gen != 0;
}

uint32_t watch_activity_history_gen(void)
{
    return __atomic_load_n(&s_hist_gen, __ATOMIC_ACQUIRE);
}

void watch_activity_sessions_request(uint32_t skip)
{
    if (!s_started) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_page_req = skip;
    xSemaphoreGive(s_lock);
    xTaskNotify(s_task, NOTE_PAGE, eSetBits);
}

int watch_activity_sessions_page(uint32_t *skip, act_ses_rec_t *out, int max, uint32_t *gen)
{
    if (!s_started || !s_page) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int n = s_page_n < max ? s_page_n : max;
    memcpy(out, s_page, sizeof(*out) * (size_t)(n > 0 ? n : 0));
    if (skip) {
        *skip = s_page_skip;
    }
    if (gen) {
        *gen = s_page_gen;
    }
    xSemaphoreGive(s_lock);
    return n;
}
