/*
 * ui_wb_settings — Ajustes de Bienestar: seguimiento, pausa / reanudación /
 * finalización automáticas, objetivos, datos personales opcionales,
 * sensibilidad del podómetro, calibración de la distancia, qué se mide y el
 * diagnóstico del sensor. Cada cambio va al servicio (que lo guarda en NVS
 * por lotes, watch_settings) y se aplica al momento. Sin ajustes de
 * vibración: el reloj no tiene motor.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include "ui_wb.h"
#include "ui_wb_logic.h"
#include "watch_theme.h"

#define CARD_W     (FLEX_SCREEN_W - 2 * FLEX_LIST_X)
#define SET_TICK_MS 500

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

/* ---- Interruptores ---- */

typedef enum {
    SWR_TRACKING = 0,
    SWR_DETECT,
    SWR_PAUSE,
    SWR_RESUME,
    SWR_END,
    SWR_COUNT
} sw_id_t;

typedef enum {
    V_SENS = 0,
    V_PAUSE_S,
    V_END_MIN,
    V_GOAL_KCAL,
    V_GOAL_EX,
    V_GOAL_STEPS,
    V_WEIGHT,
    V_HEIGHT,
    V_AGE,
    V_STRIDE_W,
    V_STRIDE_R,
    V_COUNT
} val_id_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *sw[SWR_COUNT];
    lv_obj_t *val[V_COUNT];
    lv_obj_t *diag[8];
    lv_obj_t *stride_info;
    act_config_t shown;
    bool have;
} set_t;

static bool *sw_field(act_config_t *c, sw_id_t id)
{
    switch (id) {
    case SWR_TRACKING:
        return &c->tracking;
    case SWR_DETECT:
        return &c->auto_detect;
    case SWR_PAUSE:
        return &c->autos.auto_pause;
    case SWR_RESUME:
        return &c->autos.auto_resume;
    default:
        return &c->autos.auto_end;
    }
}

static void sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_current_target(e);
    const sw_id_t id = (sw_id_t)(uintptr_t)lv_event_get_user_data(e);
    act_config_t c;
    watch_activity_config(&c);
    *sw_field(&c, id) = lv_obj_has_state(sw, LV_STATE_CHECKED);
    watch_activity_set_config(&c);
}

static lv_obj_t *switch_row(lv_obj_t *list, flex_icon_t icon, const char *title, sw_id_t id)
{
    lv_obj_t *r = watch_theme_list_row(list, icon, WB_C_EXER, title, NULL, false);
    lv_obj_t *sw = watch_theme_switch(r, 64, 36);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(sw, sw_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)id);
    /* la fila entera también lo cambia (zona táctil grande) */
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return sw;
}

/* ---- Valores con editor ---- */

static void fmt_val(const act_config_t *c, val_id_t id, char *b, size_t n)
{
    act_stride_src_t src;
    switch (id) {
    case V_SENS: {
        static const char *const k[ACT_SENS_COUNT] = {"Baja", "Normal", "Alta"};
        snprintf(b, n, "%s", k[c->sensitivity < ACT_SENS_COUNT ? c->sensitivity : 1]);
        break;
    }
    case V_PAUSE_S:
        snprintf(b, n, "%u s", c->autos.pause_after_s);
        break;
    case V_END_MIN:
        snprintf(b, n, "%u min", c->autos.end_after_s / 60u);
        break;
    case V_GOAL_KCAL:
        snprintf(b, n, "%u kcal", c->goal_kcal);
        break;
    case V_GOAL_EX:
        snprintf(b, n, "%u min", c->goal_ex_min);
        break;
    case V_GOAL_STEPS:
        wb_fmt_int(b, n, c->goal_steps);
        break;
    case V_WEIGHT:
        if (c->weight_dkg) {
            wb_fmt_weight(b, n, c->weight_dkg);
        } else {
            snprintf(b, n, "Sin definir");
        }
        break;
    case V_HEIGHT:
        if (c->height_cm) {
            snprintf(b, n, "%u cm", c->height_cm);
        } else {
            snprintf(b, n, "Sin definir");
        }
        break;
    case V_AGE:
        if (c->age) {
            snprintf(b, n, "%u años", c->age);
        } else {
            snprintf(b, n, "Sin definir");
        }
        break;
    case V_STRIDE_W:
    case V_STRIDE_R: {
        const bool run = id == V_STRIDE_R;
        const uint16_t cm = act_stride_cm(c, run, &src);
        snprintf(b, n, "%s%u cm", src == ACT_STRIDE_USER ? "" : "Auto ", cm);
        break;
    }
    default:
        b[0] = 0;
        break;
    }
}

/* Aplicadores del editor (leen la configuración actual: nada se pisa). */
#define APPLY(name, stmt)                    \
    static void name(int32_t v)              \
    {                                        \
        act_config_t c;                      \
        watch_activity_config(&c);           \
        stmt;                                \
        watch_activity_set_config(&c);       \
    }
APPLY(ap_pause, c.autos.pause_after_s = (uint16_t)v)
APPLY(ap_end, c.autos.end_after_s = (uint16_t)(v * 60))
APPLY(ap_kcal, c.goal_kcal = (uint16_t)v)
APPLY(ap_ex, c.goal_ex_min = (uint16_t)v)
APPLY(ap_steps, c.goal_steps = (uint16_t)v)
APPLY(ap_weight, c.weight_dkg = (uint16_t)v)
APPLY(ap_height, c.height_cm = (uint8_t)v)
APPLY(ap_age, c.age = (uint8_t)v)
APPLY(ap_sw, c.stride_walk_cm = (uint8_t)v)
APPLY(ap_sr, c.stride_run_cm = (uint8_t)v)
#undef APPLY

static void val_cb(lv_event_t *e)
{
    const val_id_t id = (val_id_t)(uintptr_t)lv_event_get_user_data(e);
    act_config_t c;
    watch_activity_config(&c);
    ui_wb_edit_t ed = {0};
    switch (id) {
    case V_SENS:
        c.sensitivity = (uint8_t)((c.sensitivity + 1) % ACT_SENS_COUNT); /* se cambia con un toque */
        watch_activity_set_config(&c);
        return;
    case V_PAUSE_S:
        ed = (ui_wb_edit_t){"Pausar tras", "s", ACT_PAUSE_MIN_S, ACT_PAUSE_MAX_S, 5, c.autos.pause_after_s, 0, false,
                            ap_pause};
        break;
    case V_END_MIN:
        ed = (ui_wb_edit_t){"Terminar tras", "min en pausa", ACT_END_MIN_S / 60, ACT_END_MAX_S / 60, 1,
                            c.autos.end_after_s / 60, 0, false, ap_end};
        break;
    case V_GOAL_KCAL:
        ed = (ui_wb_edit_t){"Calorías activas", "kcal", ACT_GOAL_KCAL_MIN, ACT_GOAL_KCAL_MAX, 10, c.goal_kcal, 0,
                            false, ap_kcal};
        break;
    case V_GOAL_EX:
        ed = (ui_wb_edit_t){"Ejercicio", "min", ACT_GOAL_EX_MIN, ACT_GOAL_EX_MAX, 5, c.goal_ex_min, 0, false, ap_ex};
        break;
    case V_GOAL_STEPS:
        ed = (ui_wb_edit_t){"Pasos", "pasos", ACT_GOAL_STEPS_MIN, ACT_GOAL_STEPS_MAX, 250, c.goal_steps, 0, false,
                            ap_steps};
        break;
    case V_WEIGHT:
        ed = (ui_wb_edit_t){"Peso", "kg", ACT_WEIGHT_MIN_DKG, ACT_WEIGHT_MAX_DKG, 5, c.weight_dkg ? c.weight_dkg : 700,
                            1, true, ap_weight};
        break;
    case V_HEIGHT:
        ed = (ui_wb_edit_t){"Altura", "cm", ACT_HEIGHT_MIN_CM, ACT_HEIGHT_MAX_CM, 1, c.height_cm ? c.height_cm : 170, 0,
                            true, ap_height};
        break;
    case V_AGE:
        ed = (ui_wb_edit_t){"Edad", "años", ACT_AGE_MIN, ACT_AGE_MAX, 1, c.age ? c.age : 30, 0, true, ap_age};
        break;
    case V_STRIDE_W:
    case V_STRIDE_R: {
        const bool run = id == V_STRIDE_R;
        ed = (ui_wb_edit_t){run ? "Zancada al correr" : "Zancada al caminar", "cm", ACT_STRIDE_MIN_CM,
                            ACT_STRIDE_MAX_CM, 1, act_stride_cm(&c, run, NULL), 0, true, run ? ap_sr : ap_sw};
        break;
    }
    default:
        return;
    }
    ui_wb_open_edit(&ed);
    ui_nav_push(ui_wb_edit_create);
}

static lv_obj_t *val_row(set_t *s, lv_obj_t *list, flex_icon_t icon, uint32_t accent, const char *title, val_id_t id)
{
    lv_obj_t *r = watch_theme_list_row(list, icon, accent, title, "", id != V_SENS);
    lv_obj_add_event_cb(r, val_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
    s->val[id] = r;
    return r;
}

static void note(lv_obj_t *list, const char *t)
{
    lv_obj_t *l = watch_theme_label(list, FLEX_FONT_TINY, FLEX_C_MUTE, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
}

static void refresh(set_t *s)
{
    act_config_t c;
    watch_activity_config(&c);
    if (!s->have || memcmp(&c, &s->shown, sizeof(c)) != 0) {
        s->have = true;
        s->shown = c;
        for (int i = 0; i < SWR_COUNT; i++) {
            const bool on = *sw_field(&c, (sw_id_t)i);
            if (on != lv_obj_has_state(s->sw[i], LV_STATE_CHECKED)) {
                lv_obj_set_state(s->sw[i], LV_STATE_CHECKED, on);
            }
        }
        for (int i = 0; i < V_COUNT; i++) {
            char b[32];
            fmt_val(&c, (val_id_t)i, b, sizeof(b));
            watch_theme_row_set_value(s->val[i], b);
        }
    }
    watch_activity_status_t st;
    watch_activity_get(&st);
    char b[32];
    snprintf(b, sizeof(b), "%lu", (unsigned long)st.samples);
    watch_theme_info_set(s->diag[0], st.imu_present ? b : "Sin IMU");
    snprintf(b, sizeof(b), "%lu", (unsigned long)st.dropped);
    watch_theme_info_set(s->diag[1], b);
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)st.gaps, (unsigned long)st.dups);
    watch_theme_info_set(s->diag[2], b);
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)st.invalid, (unsigned long)st.ignored);
    watch_theme_info_set(s->diag[3], b);
    snprintf(b, sizeof(b), "%u ms%s", st.period_ms, st.imu_ok ? "" : " (sin datos)");
    watch_theme_info_set(s->diag[4], b);
    if (st.stack_free) {
        snprintf(b, sizeof(b), "%lu B", (unsigned long)st.stack_free);
    } else {
        snprintf(b, sizeof(b), "--");
    }
    watch_theme_info_set(s->diag[5], b);
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)st.saves, (unsigned long)st.save_errors);
    watch_theme_info_set(s->diag[6], b);
    watch_theme_info_set(s->diag[7], st.storage_ok ? "LittleFS" : (st.persistent ? "NVS (sin historial)" : "No"));
}

static void tick_cb(lv_timer_t *t)
{
    set_t *s = lv_timer_get_user_data(t);
    if (ui_nav_top() == s->page && ui_screen_is_on()) {
        refresh(s);
    }
}

static void free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void push_cb(lv_event_t *e)
{
    ui_nav_push((ui_page_create_t)lv_event_get_user_data(e));
}

lv_obj_t *ui_wb_settings_create(lv_obj_t *parent)
{
    set_t *s = lv_malloc_zeroed(sizeof(*s));
    if (!s) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    s->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Ajustes de Bienestar", back_cb, NULL);
    lv_obj_t *list = watch_theme_list(page);

    watch_theme_section(list, "Seguimiento");
    s->sw[SWR_TRACKING] = switch_row(list, FLEX_ICON_MOTION, "Contar pasos", SWR_TRACKING);
    note(list, "Apagado, el sensor descansa (salvo durante un entrenamiento).");
    s->sw[SWR_DETECT] = switch_row(list, FLEX_ICON_PULSE, "Detectar actividad", SWR_DETECT);
    note(list, "Caminatas y carreras sostenidas: suman ejercicio y proponen entrenar. Nunca empieza solo.");
    val_row(s, list, FLEX_ICON_MOTION, WB_C_STEPS, "Sensibilidad", V_SENS);

    watch_theme_section(list, "Entrenamientos");
    s->sw[SWR_PAUSE] = switch_row(list, FLEX_ICON_PAUSE, "Pausa automática", SWR_PAUSE);
    val_row(s, list, FLEX_ICON_HOURGLASS, WB_C_TIMER, "Pausar tras", V_PAUSE_S);
    s->sw[SWR_RESUME] = switch_row(list, FLEX_ICON_PLAY, "Reanudar solo", SWR_RESUME);
    s->sw[SWR_END] = switch_row(list, FLEX_ICON_STOP, "Terminar solo", SWR_END);
    val_row(s, list, FLEX_ICON_HOURGLASS, WB_C_TIMER, "Terminar tras", V_END_MIN);
    note(list, "Solo en caminar, correr, senderismo y escaleras: en otros tipos la muñeca puede estar quieta "
               "mientras entrenas.");

    watch_theme_section(list, "Objetivos diarios");
    val_row(s, list, FLEX_ICON_FLAG, WB_C_MOVE, "Calorías activas", V_GOAL_KCAL);
    val_row(s, list, FLEX_ICON_FLAG, WB_C_EXER, "Ejercicio", V_GOAL_EX);
    val_row(s, list, FLEX_ICON_FLAG, WB_C_STEPS, "Pasos", V_GOAL_STEPS);

    watch_theme_section(list, "Datos personales (opcionales)");
    val_row(s, list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Peso", V_WEIGHT);
    val_row(s, list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Altura", V_HEIGHT);
    val_row(s, list, FLEX_ICON_INFO, FLEX_C_CAT_BLUE, "Edad", V_AGE);
    note(list, "El peso hace falta para estimar calorías. La edad se guarda, pero sin pulsómetro no mejora la "
               "estimación y no se usa.");

    watch_theme_section(list, "Distancia");
    val_row(s, list, FLEX_ICON_FLAG, WB_C_DIST, "Caminando", V_STRIDE_W);
    val_row(s, list, FLEX_ICON_FLAG, WB_C_DIST, "Corriendo", V_STRIDE_R);
    note(list, "Zancada (longitud de un paso). Auto: de la altura, o genérica si falta.");
    lv_obj_t *cal = watch_theme_list_row(list, FLEX_ICON_FLAG, WB_C_GO, "Calibrar distancia", NULL, true);
    lv_obj_add_event_cb(cal, push_cb, LV_EVENT_CLICKED, (void *)ui_wb_calib_create);

    watch_theme_section(list, "Qué se mide");
    note(list, "Medido con el sensor de movimiento: pasos, cadencia, movimiento de la muñeca y tiempos de "
               "actividad y pausa.");
    note(list, "Estimado: distancia (pasos x zancada, sin GPS), calorías activas (sin frecuencia cardíaca) y "
               "ritmo.");
    note(list, "No disponible en este reloj: frecuencia cardíaca, SpO2, GPS, pisos, brazadas y largos.");

    watch_theme_section(list, "Diagnóstico del sensor");
    static const char *const kDiag[8] = {"Muestras", "Perdidas (anillo)", "Huecos / repetidas",
                                         "Inválidas / ignoradas", "Lectura", "Pila libre (mín.)",
                                         "Guardados / errores", "Almacenamiento"};
    for (int i = 0; i < 8; i++) {
        s->diag[i] = watch_theme_info_row(list, kDiag[i], "--");
    }
    refresh(s);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, s);
    ui_page_timer(page, tick_cb, SET_TICK_MS, s);
    return page;
}

/* ---- Editor de un valor ---- */

static ui_wb_edit_t s_edit;

void ui_wb_open_edit(const ui_wb_edit_t *e)
{
    s_edit = *e;
}

typedef struct {
    ui_wb_edit_t e;
    int32_t v;
    bool unset;
    lv_obj_t *val;
} edit_t;

static void edit_show(edit_t *p)
{
    char b[24];
    if (p->unset) {
        snprintf(b, sizeof(b), "Sin definir");
    } else if (p->e.decimals) {
        snprintf(b, sizeof(b), "%ld,%ld %s", (long)(p->v / 10), (long)(p->v % 10), p->e.unit);
    } else {
        char n[16];
        wb_fmt_int(n, sizeof(n), (uint32_t)p->v);
        snprintf(b, sizeof(b), "%s %s", n, p->e.unit);
    }
    ui_wb_set_text(p->val, b);
}

static void step_cb(lv_event_t *e)
{
    edit_t *p = lv_event_get_user_data(e);
    const int32_t dir = (intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    p->unset = false;
    p->v += dir * p->e.step;
    p->v = p->v < p->e.min ? p->e.min : (p->v > p->e.max ? p->e.max : p->v);
    edit_show(p);
}

static void unset_cb(lv_event_t *e)
{
    edit_t *p = lv_event_get_user_data(e);
    p->unset = !p->unset;
    edit_show(p);
}

static void save_cb(lv_event_t *e)
{
    edit_t *p = lv_event_get_user_data(e);
    if (p->e.apply) {
        p->e.apply(p->unset ? 0 : p->v);
    }
    ui_nav_back();
}

static lv_obj_t *step_btn(lv_obj_t *page, const char *t, int32_t dir, int32_t x, edit_t *p)
{
    lv_obj_t *b = watch_theme_lg_button(page, FLEX_LG_DARK, x, 210, 150, 84, 42, FLEX_GLOW_NONE);
    lv_obj_t *l = watch_theme_label(b, FLEX_FONT_S40, FLEX_C_TXT, t);
    lv_obj_center(l);
    lv_obj_set_user_data(b, (void *)(intptr_t)dir);
    lv_obj_add_event_cb(b, step_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_event_cb(b, step_cb, LV_EVENT_LONG_PRESSED_REPEAT, p);
    return b;
}

lv_obj_t *ui_wb_edit_create(lv_obj_t *parent)
{
    edit_t *p = lv_malloc_zeroed(sizeof(*p));
    if (!p) {
        return NULL;
    }
    p->e = s_edit;
    p->v = p->e.value < p->e.min ? p->e.min : (p->e.value > p->e.max ? p->e.max : p->e.value);
    lv_obj_t *page = ui_page_new(parent);
    watch_theme_page(page);
    watch_theme_header(page, p->e.title, back_cb, NULL);
    p->val = watch_theme_label(page, FLEX_FONT_S40, WB_C_EXER, "");
    lv_obj_align(p->val, LV_ALIGN_TOP_MID, 0, FLEX_HDR_H + 34);
    step_btn(page, "-", -1, 36, p);
    step_btn(page, "+", 1, FLEX_SCREEN_W - 36 - 150, p);
    if (p->e.optional) {
        lv_obj_t *u = watch_theme_button(page, "Sin definir", FLEX_C_SURF2, 200, 50);
        lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 312);
        lv_obj_add_event_cb(u, unset_cb, LV_EVENT_CLICKED, p);
    }
    lv_obj_t *s = watch_theme_button(page, "Guardar", WB_C_GO, FLEX_SCREEN_W - 2 * 40, 60);
    lv_obj_set_style_text_color(s, flex_color(FLEX_C_BLACK), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_add_event_cb(s, save_cb, LV_EVENT_CLICKED, p);
    edit_show(p);
    lv_obj_add_event_cb(page, free_cb, LV_EVENT_DELETE, p);
    return page;
}

/* ---- Calibración de la distancia con un recorrido conocido ---- */

typedef enum { CAL_CHOOSE = 0, CAL_COUNT, CAL_DIST, CAL_RESULT } cal_step_t;

typedef struct {
    lv_obj_t *page, *body;
    cal_step_t step;
    int next;      /* paso que construir en el próximo tick (-1 = ninguno) */
    bool run;
    uint32_t steps;
    int32_t real_m;
    lv_obj_t *count, *dist;
} cal_t;

static void cal_build(cal_t *c, cal_step_t st);

static void cal_mode_cb(lv_event_t *e)
{
    cal_t *c = lv_event_get_user_data(e);
    c->run = (uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e)) == 1;
    watch_activity_calib_start(c->run);
    c->next = CAL_COUNT;
}

static void cal_stop_cb(lv_event_t *e)
{
    cal_t *c = lv_event_get_user_data(e);
    c->steps = watch_activity_calib_stop();
    c->next = CAL_DIST;
}

static void cal_dist_show(cal_t *c)
{
    char b[24];
    wb_fmt_dist(b, sizeof(b), (uint32_t)c->real_m);
    ui_wb_set_text(c->dist, b);
}

static void cal_step_cb(lv_event_t *e)
{
    cal_t *c = lv_event_get_user_data(e);
    const int32_t dir = (intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    c->real_m += dir * 10;
    c->real_m = c->real_m < 100 ? 100 : (c->real_m > 5000 ? 5000 : c->real_m);
    cal_dist_show(c);
}

static void cal_calc_cb(lv_event_t *e)
{
    ((cal_t *)lv_event_get_user_data(e))->next = CAL_RESULT;
}

static void cal_apply_cb(lv_event_t *e)
{
    cal_t *c = lv_event_get_user_data(e);
    act_config_t cfg;
    watch_activity_config(&cfg);
    const uint16_t cm = act_calibrate_stride((uint32_t)c->real_m, c->steps);
    if (cm) {
        const uint32_t est = c->steps * act_stride_cm(&cfg, c->run, NULL) / 100u;
        const int err = act_calib_error_pct(est, (uint32_t)c->real_m);
        if (c->run) {
            cfg.stride_run_cm = (uint8_t)cm;
            cfg.calib_run_err = (int8_t)err;
        } else {
            cfg.stride_walk_cm = (uint8_t)cm;
            cfg.calib_walk_err = (int8_t)err;
        }
        watch_activity_set_config(&cfg);
    }
    ui_nav_back();
}

static void cal_restart_cb(lv_event_t *e)
{
    ((cal_t *)lv_event_get_user_data(e))->next = CAL_CHOOSE;
}

static lv_obj_t *cal_text(lv_obj_t *b, const char *t, int32_t y, uint32_t rgb, const lv_font_t *f)
{
    lv_obj_t *l = watch_theme_label(b, f, rgb, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, FLEX_SCREEN_W - 60);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static lv_obj_t *cal_button(lv_obj_t *b, const char *t, uint32_t rgb, int32_t x, int32_t y, int32_t w,
                            lv_event_cb_t cb, cal_t *c, uintptr_t ud)
{
    lv_obj_t *bt = watch_theme_button(b, t, rgb, w, 58);
    if (rgb == WB_C_GO) {
        lv_obj_set_style_text_color(bt, flex_color(FLEX_C_BLACK), 0);
    }
    lv_obj_set_pos(bt, x, y);
    lv_obj_set_user_data(bt, (void *)ud);
    lv_obj_add_event_cb(bt, cb, LV_EVENT_CLICKED, c);
    return bt;
}

static void cal_build(cal_t *c, cal_step_t st)
{
    if (c->body) {
        lv_obj_delete(c->body);
    }
    c->body = lv_obj_create(c->page);
    lv_obj_remove_style_all(c->body);
    lv_obj_set_pos(c->body, 0, FLEX_HDR_H);
    lv_obj_set_size(c->body, FLEX_SCREEN_W, FLEX_SCREEN_H - FLEX_HDR_H);
    lv_obj_remove_flag(c->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(c->body, 1);
    c->step = st;
    lv_obj_t *b = c->body;
    char t[96];
    switch (st) {
    case CAL_CHOOSE:
        cal_text(b, "Recorre una distancia que conozcas (por ejemplo, 400 m en una pista) a tu ritmo de siempre.", 6,
                 FLEX_C_TXT2, FLEX_FONT_SMALL);
        cal_button(b, "Caminar", WB_C_GO, 30, 150, 168, cal_mode_cb, c, 0);
        cal_button(b, "Correr", WB_C_GO, FLEX_SCREEN_W - 30 - 168, 150, 168, cal_mode_cb, c, 1);
        cal_text(b, "La distancia estimada nunca es GPS: calibrar corrige la zancada, no el algoritmo.", 230,
                 FLEX_C_MUTE, FLEX_FONT_TINY);
        break;
    case CAL_COUNT:
        cal_text(b, c->run ? "Corriendo: pasos contados" : "Caminando: pasos contados", 10, FLEX_C_TXT2,
                 FLEX_FONT_SMALL);
        c->count = cal_text(b, "0", 60, WB_C_STEPS_TXT, FLEX_FONT_S40);
        cal_text(b, "Al llegar al final del recorrido, pulsa \"He llegado\".", 130, FLEX_C_MUTE, FLEX_FONT_TINY);
        cal_button(b, "He llegado", WB_C_GO, 40, 200, FLEX_SCREEN_W - 80, cal_stop_cb, c, 0);
        break;
    case CAL_DIST: {
        snprintf(t, sizeof(t), "%lu pasos. ¿Qué distancia recorriste?", (unsigned long)c->steps);
        cal_text(b, t, 6, FLEX_C_TXT2, FLEX_FONT_SMALL);
        c->real_m = c->real_m ? c->real_m : 400;
        c->dist = cal_text(b, "", 70, WB_C_DIST, FLEX_FONT_S40);
        cal_dist_show(c);
        cal_button(b, "-10 m", FLEX_C_SURF2, 30, 140, 168, cal_step_cb, c, (uintptr_t)(intptr_t)-1);
        cal_button(b, "+10 m", FLEX_C_SURF2, FLEX_SCREEN_W - 30 - 168, 140, 168, cal_step_cb, c, 1);
        cal_button(b, "Calcular", WB_C_GO, 40, 226, FLEX_SCREEN_W - 80, cal_calc_cb, c, 0);
        break;
    }
    case CAL_RESULT: {
        act_config_t cfg;
        watch_activity_config(&cfg);
        const uint16_t cm = act_calibrate_stride((uint32_t)c->real_m, c->steps);
        const uint32_t est = c->steps * act_stride_cm(&cfg, c->run, NULL) / 100u;
        if (!cm) {
            cal_text(b, "Con tan pocos pasos (o esa distancia) no se puede calibrar. Recorre al menos 100 m.", 10,
                     FLEX_C_WARN, FLEX_FONT_SMALL);
            cal_button(b, "Repetir", WB_C_GO, 40, 200, FLEX_SCREEN_W - 80, cal_restart_cb, c, 0);
            break;
        }
        char e1[16], e2[16];
        wb_fmt_dist(e1, sizeof(e1), est);
        wb_fmt_dist(e2, sizeof(e2), (uint32_t)c->real_m);
        snprintf(t, sizeof(t), "Estimada: %s · Real: %s\nError antes de calibrar: %d %%", e1, e2,
                 act_calib_error_pct(est, (uint32_t)c->real_m));
        cal_text(b, t, 6, FLEX_C_TXT2, FLEX_FONT_SMALL);
        snprintf(t, sizeof(t), "Zancada nueva: %u cm", cm);
        cal_text(b, t, 96, WB_C_GO, FLEX_FONT_S24);
        cal_button(b, "Aplicar", WB_C_GO, 30, 170, 168, cal_apply_cb, c, 0);
        cal_button(b, "Repetir", FLEX_C_SURF2, FLEX_SCREEN_W - 30 - 168, 170, 168, cal_restart_cb, c, 0);
        break;
    }
    }
}

static void cal_tick_cb(lv_timer_t *t)
{
    cal_t *c = lv_timer_get_user_data(t);
    if (c->next >= 0) {
        const cal_step_t st = (cal_step_t)c->next;
        c->next = -1;
        cal_build(c, st); /* aquí, fuera del evento del botón que lo pidió */
        return;
    }
    if (c->step != CAL_COUNT || ui_nav_top() != c->page || !ui_screen_is_on()) {
        return;
    }
    watch_activity_status_t st;
    watch_activity_get(&st);
    char b[16];
    wb_fmt_int(b, sizeof(b), st.v.calib_steps);
    ui_wb_set_text(c->count, b);
}

static void cal_free_cb(lv_event_t *e)
{
    cal_t *c = lv_event_get_user_data(e);
    if (c->step == CAL_COUNT) {
        watch_activity_calib_stop(); /* salir a medias no deja la calibración abierta */
    }
    lv_free(c);
}

lv_obj_t *ui_wb_calib_create(lv_obj_t *parent)
{
    cal_t *c = lv_malloc_zeroed(sizeof(*c));
    if (!c) {
        return NULL;
    }
    lv_obj_t *page = ui_page_new(parent);
    c->page = page;
    watch_theme_page(page);
    watch_theme_header(page, "Calibrar distancia", back_cb, NULL);
    c->next = -1;
    cal_build(c, CAL_CHOOSE);
    lv_obj_add_event_cb(page, cal_free_cb, LV_EVENT_DELETE, c);
    ui_page_timer(page, cal_tick_cb, 100, c);
    return page;
}
