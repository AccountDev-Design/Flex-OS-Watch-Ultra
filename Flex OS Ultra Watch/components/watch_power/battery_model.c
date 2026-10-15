/*
 * battery_model — ver include/battery_model.h
 */
#include "battery_model.h"

#include <stddef.h>
#include <string.h>

void axp2101_decode(const axp2101_regs_t *r, axp2101_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->chip_ok = (r->chip_id == AXP2101_CHIP_ID_VALUE);
    out->batt_present = (r->status1 >> 3) & 1;
    out->vbus_present = (r->status1 >> 5) & 1;
    out->thermal_reg = (r->status1 >> 1) & 1;
    out->input_limited = r->status1 & 1;
    out->vindpm = (r->status2 >> 3) & 1;

    /* [6:5]; el bit 7 está reservado */
    out->dir = (axp_dir_t)((r->status2 >> 5) & 0x03);

    const uint8_t stage = r->status2 & 0x07;
    out->stage = (stage <= AXP_CHG_STOPPED) ? (axp_chg_stage_t)stage : AXP_CHG_UNKNOWN;

    out->charger_enabled = (r->gauge_ctrl >> 1) & 1;
    out->gauge_enabled = (r->gauge_ctrl >> 3) & 1;
    out->vbat_adc_enabled = r->adc_ctrl & 0x01;
    out->ts_adc_enabled = (r->adc_ctrl >> 1) & 1;

    out->vbat_mv = -1;
    if (out->batt_present && out->vbat_adc_enabled) {
        const int mv = ((r->vbat_h & 0x3F) << 8) | r->vbat_l;
        if (mv >= BATT_VBAT_MIN_MV && mv <= BATT_VBAT_MAX_MV) {
            out->vbat_mv = mv;
        }
    }

    out->percent = -1;
    if (out->batt_present && out->gauge_enabled && r->percent <= 100) {
        out->percent = r->percent;
    }
}

void axp2101_decode_cfg(const axp2101_cfg_regs_t *r, axp2101_cfg_t *out)
{
    static const int kIinLimMa[8] = {100, 500, 900, 1000, 1500, 2000, -1, -1};
    static const int kCvMv[8] = {-1, 4000, 4100, 4200, 4350, 4400, -1, -1};

    memset(out, 0, sizeof(*out));
    out->vindpm_mv = 3880 + 80 * (r->vindpm & 0x0F);
    out->input_limit_ma = kIinLimMa[r->iin_lim & 0x07];

    const int pre = r->iprechg & 0x0F;
    out->precharge_ma = (pre <= 8) ? 25 * pre : -1;

    const int icc = r->icc & 0x1F;
    if (icc <= 8) {
        out->charge_ma = 25 * icc;
    } else if (icc <= 16) {
        out->charge_ma = 200 + 100 * (icc - 8);
    } else {
        out->charge_ma = -1;
    }

    const int term = r->iterm & 0x0F;
    out->term_ma = (term <= 8) ? 25 * term : -1;
    out->term_enabled = (r->iterm >> 4) & 1;
    out->cv_mv = kCvMv[r->cv & 0x07];

    out->charger_enabled = (r->gauge_ctrl >> 1) & 1;
    out->button_cell_charge = (r->gauge_ctrl >> 2) & 1;
    out->gauge_enabled = (r->gauge_ctrl >> 3) & 1;
    out->vbat_adc_enabled = r->adc_ctrl & 1;
    out->ts_adc_enabled = (r->adc_ctrl >> 1) & 1;
    out->ts_affects_charger = ((r->ts_ctrl >> 4) & 1) == 0;
    out->ts_current_mode = (r->ts_ctrl >> 2) & 0x03;
    out->batt_detect_enabled = r->bat_det & 1;
}

batt_state_t batt_classify(const axp2101_status_t *s)
{
    if (!s->chip_ok) {
        return BATT_STATE_UNKNOWN;
    }
    if (!s->batt_present) {
        return BATT_STATE_NO_BATTERY;
    }
    const bool charging = (s->dir == AXP_DIR_CHARGE) && (s->stage <= AXP_CHG_CV);
    if (charging) {
        return BATT_STATE_CHARGING;
    }
    if (s->vbus_present && s->stage == AXP_CHG_DONE) {
        return BATT_STATE_FULL;
    }
    if (s->vbus_present) {
        return BATT_STATE_EXTERNAL_IDLE;
    }
    return BATT_STATE_DISCHARGING;
}

void batt_tracker_init(batt_tracker_t *t)
{
    memset(t, 0, sizeof(*t));
    t->state = BATT_STATE_UNKNOWN;
    t->prev_state = BATT_STATE_UNKNOWN;
    t->pending = BATT_STATE_UNKNOWN;
    t->percent = -1;
    t->vbat_mv = -1;
    t->level = BATT_LEVEL_UNKNOWN;
}

static int median3(int a, int b, int c)
{
    if (a > b) { int x = a; a = b; b = x; }
    if (b > c) { int x = b; b = c; c = x; }
    if (a > b) { int x = a; a = b; b = x; }
    return b;
}

static int pct_filtered(const batt_tracker_t *t)
{
    if (t->pct_n == 0) {
        return -1;
    }
    if (t->pct_n < 3) {
        return t->pct_hist[t->pct_n - 1];
    }
    return median3(t->pct_hist[0], t->pct_hist[1], t->pct_hist[2]);
}

static void pct_push(batt_tracker_t *t, int pct)
{
    if (t->pct_n < 3) {
        t->pct_hist[t->pct_n++] = pct;
    } else {
        t->pct_hist[0] = t->pct_hist[1];
        t->pct_hist[1] = t->pct_hist[2];
        t->pct_hist[2] = pct;
    }
}

batt_level_t batt_level_next(batt_level_t cur, int pct)
{
    if (pct < 0) {
        return BATT_LEVEL_UNKNOWN;
    }
    /* Empeorar es inmediato; mejorar exige superar el umbral de salida. */
    if (pct <= BATT_CRITICAL_ENTER_PCT) {
        return BATT_LEVEL_CRITICAL;
    }
    switch (cur) {
    case BATT_LEVEL_CRITICAL:
        if (pct < BATT_CRITICAL_EXIT_PCT) {
            return BATT_LEVEL_CRITICAL;
        }
        return (pct < BATT_LOW_EXIT_PCT) ? BATT_LEVEL_LOW : BATT_LEVEL_NORMAL;
    case BATT_LEVEL_LOW:
        return (pct < BATT_LOW_EXIT_PCT) ? BATT_LEVEL_LOW : BATT_LEVEL_NORMAL;
    default:
        return (pct <= BATT_LOW_ENTER_PCT) ? BATT_LEVEL_LOW : BATT_LEVEL_NORMAL;
    }
}

uint32_t batt_hold_ms(batt_state_t from, batt_state_t to)
{
    if (from == BATT_STATE_CHARGING && to == BATT_STATE_EXTERNAL_IDLE) {
        return BATT_HOLD_CHARGING_EXIT_MS;
    }
    if (from == BATT_STATE_DISCHARGING && to == BATT_STATE_EXTERNAL_IDLE) {
        return BATT_HOLD_PLUG_IDLE_MS;
    }
    if (to == BATT_STATE_FULL || to == BATT_STATE_NO_BATTERY || to == BATT_STATE_UNKNOWN) {
        return BATT_HOLD_SLOW_MS;
    }
    return BATT_HOLD_FAST_MS;
}

bool batt_tracker_pending(const batt_tracker_t *t)
{
    return t->pending != t->state;
}

void batt_tracker_update(batt_tracker_t *t, const axp2101_status_t *s, bool read_ok)
{
    t->clock_ms += BATT_LEGACY_PERIOD_MS;
    batt_tracker_update_at(t, s, read_ok, t->clock_ms);
}

void batt_tracker_update_at(batt_tracker_t *t, const axp2101_status_t *s, bool read_ok, uint32_t now_ms)
{
    t->charging_started = false;

    batt_state_t raw = read_ok ? batt_classify(s) : BATT_STATE_UNKNOWN;

    /* Confirmación: muestras seguidas y tiempo mínimo (batt_hold_ms). */
    if (raw == t->state) {
        t->pending = raw;
        t->pending_count = 0;
    } else if (raw == t->pending) {
        if (t->pending_count < 255) {
            t->pending_count++;
        }
        const uint32_t held = now_ms - t->pending_since_ms; /* resta sin signo: tolera la vuelta */
        if (t->pending_count >= BATT_CONFIRM_SAMPLES - 1 && held >= batt_hold_ms(t->state, raw)) {
            const batt_state_t prev = t->state;
            t->prev_state = prev;
            t->state = raw;
            t->pending_count = 0;
            if (raw == BATT_STATE_CHARGING && prev != BATT_STATE_CHARGING) {
                t->charging_started = true;
                t->charge_events++;
            }
            /* Cambio de fase (p. ej. se desenchufa): el % se vuelve a tomar
             * de la lectura actual, sin arrastrar el filtro anterior. */
            t->pct_n = 0;
            t->percent = -1;
            if (raw == BATT_STATE_UNKNOWN) {
                t->vbat_mv = -1;
                t->level = BATT_LEVEL_UNKNOWN;
            }
        }
    } else {
        t->pending = raw;
        t->pending_count = 0;
        t->pending_since_ms = now_ms;
    }

    if (!read_ok) {
        return; /* conserva los últimos valores mostrados */
    }

    /* Porcentaje: mediana de las 3 últimas lecturas válidas. En descarga el
     * valor mostrado no sube (el gauge puede oscilar ±1 %). */
    if (s->percent >= 0) {
        pct_push(t, s->percent);
        const int med = pct_filtered(t);
        if (t->state == BATT_STATE_DISCHARGING && t->percent >= 0 && med > t->percent) {
            /* se mantiene el valor anterior */
        } else {
            t->percent = med;
        }
    } else if (s->batt_present && s->gauge_enabled) {
        t->invalid_samples++;
    } else {
        t->percent = -1;
        t->pct_n = 0;
    }

    /* VBAT: media móvil exponencial (alfa = 1/4) en cuartos de mV. */
    if (s->vbat_mv >= 0) {
        if (t->vbat_mv < 0) {
            t->vbat_ema_x4 = s->vbat_mv * 4;
        } else {
            t->vbat_ema_x4 += s->vbat_mv - t->vbat_ema_x4 / 4;
        }
        t->vbat_mv = t->vbat_ema_x4 / 4;
    } else {
        t->vbat_mv = -1;
    }

    t->level = batt_level_next(t->level, t->percent);
}

const char *batt_state_name(batt_state_t st)
{
    switch (st) {
    case BATT_STATE_NO_BATTERY:    return "sin bateria";
    case BATT_STATE_DISCHARGING:   return "descargando";
    case BATT_STATE_CHARGING:      return "cargando";
    case BATT_STATE_FULL:          return "carga completa";
    case BATT_STATE_EXTERNAL_IDLE: return "externa sin carga";
    default:                       return "sin datos";
    }
}
