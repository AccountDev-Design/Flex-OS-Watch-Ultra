/*
 * ui_logic — ver include/ui_logic.h
 */
#include "ui_logic.h"

#include <stdio.h>

flex_batt_kind_t ui_batt_kind(batt_state_t state, batt_level_t level)
{
    switch (state) {
    case BATT_STATE_NO_BATTERY:    return FLEX_BATT_ABSENT;
    case BATT_STATE_CHARGING:      return FLEX_BATT_CHARGING;
    case BATT_STATE_FULL:          return FLEX_BATT_FULL;
    case BATT_STATE_EXTERNAL_IDLE: return FLEX_BATT_EXTERNAL;
    case BATT_STATE_DISCHARGING:
        if (level == BATT_LEVEL_CRITICAL) {
            return FLEX_BATT_CRITICAL;
        }
        if (level == BATT_LEVEL_LOW) {
            return FLEX_BATT_LOW;
        }
        return FLEX_BATT_NORMAL;
    default:
        return FLEX_BATT_UNKNOWN;
    }
}

const char *ui_batt_state_text(batt_state_t state)
{
    switch (state) {
    case BATT_STATE_NO_BATTERY:    return "Sin batería";
    case BATT_STATE_DISCHARGING:   return "Descargando";
    case BATT_STATE_CHARGING:      return "Cargando";
    case BATT_STATE_FULL:          return "Carga completa";
    case BATT_STATE_EXTERNAL_IDLE: return "Externa sin carga";
    default:                       return "Sin datos";
    }
}

void ui_charge_gate_init(ui_charge_gate_t *g)
{
    g->seen_events = 0;
    g->primed = false;
}

bool ui_charge_gate_update(ui_charge_gate_t *g, uint32_t charge_events, batt_state_t prev_state,
                           batt_state_t state)
{
    if (!g->primed) {
        g->seen_events = charge_events;
        g->primed = true;
        return false;
    }
    if (charge_events == g->seen_events) {
        return false;
    }
    g->seen_events = charge_events;
    return state == BATT_STATE_CHARGING && prev_state != BATT_STATE_UNKNOWN;
}

void ui_face_time(watch_time_state_t state, const wt_datetime_t *dt, ui_face_time_t *out)
{
    ui_face_time_fmt(state, dt, true, out);
}

void ui_face_time_fmt(watch_time_state_t state, const wt_datetime_t *dt, bool h24, ui_face_time_t *out)
{
    out->valid = (state == WATCH_TIME_OK && dt != NULL);
    out->ampm[0] = '\0';
    if (out->valid) {
        if (h24) {
            wt_fmt_hhmm(out->time, sizeof(out->time), dt);
        } else {
            wt_fmt_hhmm12(out->time, sizeof(out->time), dt);
            snprintf(out->ampm, sizeof(out->ampm), "%s", wt_ampm(dt));
        }
        wt_fmt_date_long(out->date, sizeof(out->date), dt);
        /* minutos desde 1970 (cambia una vez por minuto), par en 24 h e
         * impar en 12 h: cambiar el formato también refresca la esfera */
        out->key = (wt_days_from_civil(dt->year, dt->month, dt->day) * 1440 + dt->hour * 60 + dt->minute) * 2 +
                   (h24 ? 0 : 1);
        return;
    }
    snprintf(out->time, sizeof(out->time), "--:--");
    switch (state) {
    case WATCH_TIME_NOT_SET:
        snprintf(out->date, sizeof(out->date), "Hora sin configurar");
        break;
    case WATCH_TIME_NO_RTC:
        snprintf(out->date, sizeof(out->date), "Reloj RTC no disponible");
        break;
    default:
        snprintf(out->date, sizeof(out->date), "Leyendo la hora…");
        break;
    }
    out->key = -1 - (int64_t)state; /* negativo: no choca con una hora válida */
}

int ui_brightness_clamp(int percent)
{
    if (percent < UI_BRIGHT_MIN) {
        return UI_BRIGHT_MIN;
    }
    return percent > UI_BRIGHT_MAX ? UI_BRIGHT_MAX : percent;
}

bool ui_brightness_should_apply(int value, int applied, uint32_t last_ms, uint32_t now_ms, bool final)
{
    if (value == applied) {
        return false;
    }
    return final || (uint32_t)(now_ms - last_ms) >= UI_BRIGHT_APPLY_MS;
}

int ui_fmt_volts(char *buf, size_t n, int mv)
{
    if (mv < 0) {
        return snprintf(buf, n, "--");
    }
    return snprintf(buf, n, "%d,%02d V", mv / 1000, (mv % 1000) / 10);
}

int ui_fmt_ma(char *buf, size_t n, int ma)
{
    if (ma < 0) {
        return snprintf(buf, n, "--");
    }
    return snprintf(buf, n, "%d mA", ma);
}

int ui_fmt_bytes(char *buf, size_t n, uint32_t bytes)
{
    const uint32_t MB = 1024u * 1024u;
    if (bytes >= MB) {
        return snprintf(buf, n, "%lu,%lu MB", (unsigned long)(bytes / MB),
                        (unsigned long)((bytes % MB) * 10u / MB));
    }
    return snprintf(buf, n, "%lu KB", (unsigned long)(bytes / 1024u));
}

const char *ui_time_state_text(watch_time_state_t st)
{
    switch (st) {
    case WATCH_TIME_OK:      return "Correcta";
    case WATCH_TIME_NOT_SET: return "Sin configurar";
    case WATCH_TIME_NO_RTC:  return "No responde";
    default:                 return "Leyendo…";
    }
}

/* Desfases estándar de UTC-12 a UTC+14. Sin horario de verano (D17): se
 * nombran ciudades que no lo usan siempre que se puede. */
const ui_tz_entry_t ui_tz_table[] = {
    {-720, "Isla Baker"},      {-660, "Pago Pago"},        {-600, "Honolulu"},        {-570, "Marquesas"},
    {-540, "Islas Gambier"},   {-480, "Islas Pitcairn"},   {-420, "Phoenix"},         {-360, "Ciudad de México"},
    {-300, "Lima, Bogotá"},    {-240, "Caracas, La Paz"},  {-210, "Terranova"},       {-180, "Buenos Aires"},
    {-120, "Georgia del Sur"}, {-60, "Cabo Verde"},        {0, "Reikiavik, Acra"},    {60, "Lagos, Argel"},
    {120, "El Cairo"},         {180, "Moscú, Riad"},       {210, "Teherán"},          {240, "Dubái, Bakú"},
    {270, "Kabul"},            {300, "Karachi"},           {330, "India"},            {345, "Katmandú"},
    {360, "Daca"},             {390, "Rangún"},            {420, "Bangkok"},          {480, "Pekín, Singapur"},
    {525, "Eucla"},            {540, "Tokio, Seúl"},       {570, "Darwin"},           {600, "Brisbane"},
    {630, "Lord Howe"},        {660, "Nueva Caledonia"},   {720, "Fiyi"},             {765, "Islas Chatham"},
    {780, "Tonga"},            {840, "Kiritimati"},
};
const size_t ui_tz_count = sizeof(ui_tz_table) / sizeof(ui_tz_table[0]);

int ui_tz_index(int off_min)
{
    for (size_t i = 0; i < ui_tz_count; i++) {
        if (ui_tz_table[i].off_min == off_min) {
            return (int)i;
        }
    }
    return -1;
}
