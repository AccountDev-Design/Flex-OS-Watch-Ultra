#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
static inline void heap_caps_free(void *p) { free(p); }
static inline void *heap_caps_calloc(size_t n, size_t sz, unsigned caps) { (void)caps; return calloc(n, sz); }
/* la PSRAM del reloj (8 MB) para lo que se enseña en pantalla */
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 7u * 1024u * 1024u; }
