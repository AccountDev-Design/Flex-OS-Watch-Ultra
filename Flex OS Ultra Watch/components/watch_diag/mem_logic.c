/*
 * mem_logic — ver include/mem_logic.h
 */
#include "mem_logic.h"

#include <stdio.h>

bool mem_stack_internal_first(bool psram_first, size_t stack, size_t int_free, size_t int_block)
{
    if (psram_first) {
        return false;
    }
    return int_block >= stack && int_free >= stack + MEM_INTERNAL_KEEP;
}

void mem_fmt_size(size_t bytes, char *out, size_t n)
{
    if (!out || !n) {
        return;
    }
    if (bytes >= 1024u * 1024u) {
        const unsigned long tenths = (unsigned long)(bytes * 10u / (1024u * 1024u));
        snprintf(out, n, "%lu,%lu MB", tenths / 10u, tenths % 10u);
    } else {
        snprintf(out, n, "%lu KB", (unsigned long)(bytes / 1024u));
    }
}

void mem_why_text(char *out, size_t n, size_t need, bool psram, size_t int_free, size_t ps_free)
{
    if (!out || !n) {
        return;
    }
    char a[16], b[16], c[16];
    mem_fmt_size(need, a, sizeof(a));
    mem_fmt_size(psram ? ps_free : int_free, b, sizeof(b));
    mem_fmt_size(psram ? int_free : ps_free, c, sizeof(c));
    const char *mem = psram ? "PSRAM" : "RAM interna";
    if (need) {
        snprintf(out, n, "Faltan %s de %s (hay %s). %s libre: %s.", a, mem, b, psram ? "RAM interna" : "PSRAM", c);
    } else {
        snprintf(out, n, "Sin %s suficiente (hay %s). %s libre: %s.", mem, b, psram ? "RAM interna" : "PSRAM", c);
    }
}
