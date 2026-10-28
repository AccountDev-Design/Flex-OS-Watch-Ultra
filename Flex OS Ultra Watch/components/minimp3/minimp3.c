/*
 * minimp3 (https://github.com/lieff/minimp3, CC0, commit ea99364): la unidad
 * de compilación de la implementación. El único cambio en minimp3.h está
 * marcado "FLEX OS WATCH ULTRA" (área de trabajo externa, MINIMP3_EXTERNAL_SCRATCH).
 * Solo capa III (MINIMP3_ONLY_MP3) y sin SIMD (el Xtensa del S3 no tiene SSE/NEON).
 */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
