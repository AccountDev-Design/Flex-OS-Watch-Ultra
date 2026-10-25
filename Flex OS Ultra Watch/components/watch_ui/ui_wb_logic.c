/*
 * ui_wb_logic — ver include/ui_wb_logic.h
 */
#include "ui_wb_logic.h"

#include <stdio.h>
#include "wt_time.h"

int wb_fmt_int(char *b, size_t n, uint32_t v)
{
    if (v < 1000) {
        return snprintf(b, n, "%lu", (unsigned long)v);
    }
    if (v < 1000000) {
        return snprintf(b, n, "%lu.%03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000));
    }
    return snprintf(b, n, "%lu.%03lu.%03lu", (unsigned long)(v / 1000000), (unsigned long)(v / 1000 % 1000),
                    (unsigned long)(v % 1000));
}

int wb_fmt_dur(char *b, size_t n, uint32_t ms)
{
    const uint32_t s = ms / 1000;
    if (s >= 3600) {
        return snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                        (unsigned long)(s % 60));
    }
    return snprintf(b, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void wb_fmt_stopwatch(char *mmss, size_t n1, char *tenth, size_t n2, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (s > 99 * 60 + 59) {
        /* Más de 99:59: horas y minutos (las décimas ya no aportan nada). */
        snprintf(mmss, n1, "%lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
        snprintf(tenth, n2, "H");
        return;
    }
    snprintf(mmss, n1, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    snprintf(tenth, n2, ",%lu", (unsigned long)(ms / 100 % 10));
}

int wb_fmt_dur_words(char *b, size_t n, uint32_t s)
{
    if (s >= 3600) {
        return snprintf(b, n, "%lu h %02lu min", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    }
    if (s >= 60) {
        return snprintf(b, n, "%lu min", (unsigned long)(s / 60));
    }
    return snprintf(b, n, "%lu s", (unsigned long)s);
}

int wb_fmt_dist(char *b, size_t n, uint32_t m)
{
    if (m >= 1000) {
        const uint32_t dm = (m + 5) / 10; /* centésimas de km */
        return snprintf(b, n, "%lu,%02lu km", (unsigned long)(dm / 100), (unsigned long)(dm % 100));
    }
    return snprintf(b, n, "%lu m", (unsigned long)m);
}

int wb_fmt_pace(char *b, size_t n, uint32_t spk)
{
    if (!spk || spk > 59 * 60 + 59) {
        return snprintf(b, n, "--");
    }
    return snprintf(b, n, "%lu'%02lu\"", (unsigned long)(spk / 60), (unsigned long)(spk % 60));
}

int wb_fmt_weight(char *b, size_t n, uint16_t dkg)
{
    return snprintf(b, n, "%u,%u kg", dkg / 10u, dkg % 10u);
}

const char *wb_day_letter(int32_t day)
{
    static const char *const L[7] = {"D", "L", "M", "X", "J", "V", "S"};
    return L[wt_weekday_from_days(day)];
}

int wb_fmt_day(char *b, size_t n, int32_t day, int32_t today)
{
    if (day == today) {
        return snprintf(b, n, "Hoy");
    }
    if (day == today - 1) {
        return snprintf(b, n, "Ayer");
    }
    static const char *const W[7] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
    static const char *const M[12] = {"ene", "feb", "mar", "abr", "may", "jun",
                                      "jul", "ago", "sep", "oct", "nov", "dic"};
    int y, mo, d;
    wt_civil_from_days(day, &y, &mo, &d);
    return snprintf(b, n, "%s %d %s", W[wt_weekday_from_days(day)], d, M[(mo - 1) % 12]);
}

int32_t wb_bar_h(uint32_t v, uint32_t max, int32_t h)
{
    if (!v || !max || h <= 0) {
        return 0;
    }
    if (v >= max) {
        return h;
    }
    const int32_t r = (int32_t)((uint64_t)v * (uint32_t)h / max);
    return r < 2 ? 2 : r;
}
