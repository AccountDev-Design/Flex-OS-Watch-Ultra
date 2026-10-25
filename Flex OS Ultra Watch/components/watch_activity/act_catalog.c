/*
 * act_catalog — ver include/act_catalog.h
 */
#include "act_catalog.h"

#include <string.h>

#define S  ACT_F_STEPS
#define D  ACT_F_DIST
#define A  ACT_F_AUTO
#define M  ACT_F_MOTION
#define U  ACT_F_DURATION
#define W  ACT_F_WATER
#define R  ACT_F_RUN
#define KM ACT_KCAL_MET
#define KS ACT_KCAL_STEPS

/* Orden de presentación (por categoría). Los ids no siguen este orden a
 * propósito: son los que se guardan. */
static const act_type_t kTypes[] = {
    /* Caminar y correr */
    {1, ACT_CAT_WALKRUN, ACT_IC_WALK, S | D | A, 35, KS, "Caminar al aire libre"},
    {2, ACT_CAT_WALKRUN, ACT_IC_WALK, S | D | A, 35, KS, "Caminar en interior"},
    {3, ACT_CAT_WALKRUN, ACT_IC_RUN, S | D | A | R, 90, KS, "Correr al aire libre"},
    {4, ACT_CAT_WALKRUN, ACT_IC_RUN, S | D | A | R, 90, KS, "Correr en interior"},
    {5, ACT_CAT_WALKRUN, ACT_IC_RUN, S | D | R, 80, KS, "Atletismo en pista"},
    /* Senderismo, ciclismo, escaleras, elíptica y remo */
    {10, ACT_CAT_MACHINES, ACT_IC_HIKE, S | D | A, 60, KM, "Senderismo"},
    {11, ACT_CAT_MACHINES, ACT_IC_BIKE, U, 75, KM, "Ciclismo al aire libre"},
    {12, ACT_CAT_MACHINES, ACT_IC_BIKE, U, 68, KM, "Ciclismo en interior"},
    {13, ACT_CAT_MACHINES, ACT_IC_STAIRS, S | A, 88, KM, "Subir escaleras"},
    {14, ACT_CAT_MACHINES, ACT_IC_MACHINE, M, 90, KM, "Escaladora"},
    {15, ACT_CAT_MACHINES, ACT_IC_MACHINE, M, 50, KM, "Elíptica"},
    {16, ACT_CAT_MACHINES, ACT_IC_MACHINE, M, 70, KM, "Remo"},
    {17, ACT_CAT_MACHINES, ACT_IC_STAIRS, M, 75, KM, "Step"},
    /* Fuerza, funcional, core, HIIT y cardio */
    {20, ACT_CAT_STRENGTH, ACT_IC_STRENGTH, M, 50, KM, "Fuerza tradicional"},
    {21, ACT_CAT_STRENGTH, ACT_IC_STRENGTH, M, 50, KM, "Entrenamiento funcional"},
    {22, ACT_CAT_STRENGTH, ACT_IC_STRENGTH, M, 38, KM, "Core"},
    {23, ACT_CAT_STRENGTH, ACT_IC_HIIT, M, 80, KM, "HIIT"},
    {24, ACT_CAT_STRENGTH, ACT_IC_HIIT, M, 60, KM, "Cardio mixto"},
    {25, ACT_CAT_STRENGTH, ACT_IC_HIIT, M, 70, KM, "Entrenamiento cruzado"},
    {26, ACT_CAT_STRENGTH, ACT_IC_HIIT, M, 118, KM, "Saltar a la comba"},
    {27, ACT_CAT_STRENGTH, ACT_IC_STRETCH, M, 38, KM, "Gimnasia"},
    /* Yoga, pilates, baile y flexibilidad */
    {30, ACT_CAT_MINDBODY, ACT_IC_YOGA, U, 25, KM, "Yoga"},
    {31, ACT_CAT_MINDBODY, ACT_IC_YOGA, U, 30, KM, "Pilates"},
    {32, ACT_CAT_MINDBODY, ACT_IC_DANCE, M, 35, KM, "Barre"},
    {33, ACT_CAT_MINDBODY, ACT_IC_YOGA, U, 30, KM, "Taichí"},
    {34, ACT_CAT_MINDBODY, ACT_IC_YOGA, U, 20, KM, "Mente y cuerpo"},
    {35, ACT_CAT_MINDBODY, ACT_IC_STRETCH, U, 23, KM, "Flexibilidad"},
    {36, ACT_CAT_MINDBODY, ACT_IC_DANCE, M, 50, KM, "Baile"},
    {37, ACT_CAT_MINDBODY, ACT_IC_DANCE, M, 45, KM, "Baile social"},
    {38, ACT_CAT_MINDBODY, ACT_IC_DANCE, M, 73, KM, "Baile cardio"},
    {39, ACT_CAT_MINDBODY, ACT_IC_STRETCH, U, 25, KM, "Preparación y recuperación"},
    {40, ACT_CAT_MINDBODY, ACT_IC_STRETCH, U, 25, KM, "Enfriamiento"},
    /* Pelota y raqueta */
    {45, ACT_CAT_BALL, ACT_IC_BALL, S | M, 70, KM, "Fútbol"},
    {46, ACT_CAT_BALL, ACT_IC_BALL, S | M, 65, KM, "Baloncesto"},
    {47, ACT_CAT_BALL, ACT_IC_BALL, M, 40, KM, "Voleibol"},
    {48, ACT_CAT_BALL, ACT_IC_BALL, S | M, 80, KM, "Balonmano"},
    {49, ACT_CAT_BALL, ACT_IC_BALL, S | M, 83, KM, "Rugby"},
    {50, ACT_CAT_BALL, ACT_IC_BALL, S | M, 80, KM, "Fútbol americano"},
    {51, ACT_CAT_BALL, ACT_IC_BALL, S | M, 80, KM, "Fútbol australiano"},
    {52, ACT_CAT_BALL, ACT_IC_BALL, S | M, 80, KM, "Hockey"},
    {53, ACT_CAT_BALL, ACT_IC_BALL, S | M, 80, KM, "Lacrosse"},
    {54, ACT_CAT_BALL, ACT_IC_BALL, M, 50, KM, "Béisbol"},
    {55, ACT_CAT_BALL, ACT_IC_BALL, M, 50, KM, "Sóftbol"},
    {56, ACT_CAT_BALL, ACT_IC_BALL, M, 48, KM, "Críquet"},
    {57, ACT_CAT_BALL, ACT_IC_RACKET, S | M, 73, KM, "Tenis"},
    {58, ACT_CAT_BALL, ACT_IC_RACKET, S | M, 60, KM, "Pádel"},
    {59, ACT_CAT_BALL, ACT_IC_RACKET, M, 55, KM, "Bádminton"},
    {60, ACT_CAT_BALL, ACT_IC_RACKET, S | M, 73, KM, "Squash"},
    {61, ACT_CAT_BALL, ACT_IC_RACKET, S | M, 70, KM, "Ráquetbol"},
    {62, ACT_CAT_BALL, ACT_IC_RACKET, M, 40, KM, "Tenis de mesa"},
    {63, ACT_CAT_BALL, ACT_IC_RACKET, M, 41, KM, "Pickleball"},
    {64, ACT_CAT_BALL, ACT_IC_TARGET, U, 30, KM, "Bolos"},
    {65, ACT_CAT_BALL, ACT_IC_TARGET, U, 48, KM, "Golf"},
    {66, ACT_CAT_BALL, ACT_IC_TARGET, M, 40, KM, "Deportes de disco"},
    /* Combate */
    {68, ACT_CAT_COMBAT, ACT_IC_COMBAT, M, 70, KM, "Boxeo"},
    {69, ACT_CAT_COMBAT, ACT_IC_COMBAT, M, 73, KM, "Kickboxing"},
    {70, ACT_CAT_COMBAT, ACT_IC_COMBAT, M, 70, KM, "Artes marciales"},
    {71, ACT_CAT_COMBAT, ACT_IC_COMBAT, M, 60, KM, "Lucha"},
    {72, ACT_CAT_COMBAT, ACT_IC_COMBAT, M, 60, KM, "Esgrima"},
    /* Natación y agua: solo duración (no hay medida de brazadas ni largos) */
    {73, ACT_CAT_WATER, ACT_IC_SWIM, U | W, 60, KM, "Natación en piscina"},
    {74, ACT_CAT_WATER, ACT_IC_SWIM, U | W, 60, KM, "Natación en aguas abiertas"},
    {75, ACT_CAT_WATER, ACT_IC_SWIM, U | W, 100, KM, "Waterpolo"},
    {76, ACT_CAT_WATER, ACT_IC_WATER, U | W, 53, KM, "Fitness acuático"},
    {77, ACT_CAT_WATER, ACT_IC_WATER, U | W, 30, KM, "Surf"},
    {78, ACT_CAT_WATER, ACT_IC_WATER, U | W, 50, KM, "Piragüismo y remo"},
    {80, ACT_CAT_WATER, ACT_IC_WATER, U | W, 30, KM, "Vela"},
    {81, ACT_CAT_WATER, ACT_IC_WATER, U | W, 70, KM, "Buceo"},
    {82, ACT_CAT_WATER, ACT_IC_WATER, U | W, 40, KM, "Deportes acuáticos"},
    /* Invierno, montaña y exterior */
    {85, ACT_CAT_OUTDOOR, ACT_IC_SNOW, U, 53, KM, "Esquí alpino"},
    {86, ACT_CAT_OUTDOOR, ACT_IC_SNOW, M, 90, KM, "Esquí de fondo"},
    {87, ACT_CAT_OUTDOOR, ACT_IC_SNOW, U, 53, KM, "Snowboard"},
    {88, ACT_CAT_OUTDOOR, ACT_IC_SNOW, M, 70, KM, "Patinaje"},
    {89, ACT_CAT_OUTDOOR, ACT_IC_SNOW, U, 40, KM, "Curling"},
    {90, ACT_CAT_OUTDOOR, ACT_IC_SNOW, M, 50, KM, "Deportes de nieve"},
    {91, ACT_CAT_OUTDOOR, ACT_IC_MOUNTAIN, M, 73, KM, "Escalada"},
    {92, ACT_CAT_OUTDOOR, ACT_IC_STAR, U, 55, KM, "Equitación"},
    {93, ACT_CAT_OUTDOOR, ACT_IC_TARGET, U, 50, KM, "Caza"},
    {94, ACT_CAT_OUTDOOR, ACT_IC_TARGET, U, 35, KM, "Pesca"},
    {95, ACT_CAT_OUTDOOR, ACT_IC_TARGET, U, 43, KM, "Tiro con arco"},
    /* Otras */
    {ACT_TYPE_OTHER, ACT_CAT_OTHER, ACT_IC_STAR, M, 30, KM, "Otro"},
    {96, ACT_CAT_OTHER, ACT_IC_STAR, M, 40, KM, "Juego"},
    {97, ACT_CAT_OTHER, ACT_IC_STAR, M, 38, KM, "Fitness con videojuegos"},
    /* Adaptadas: la muñeca empuja la silla, así que no hay "pasos" que contar */
    {98, ACT_CAT_ADAPTED, ACT_IC_WHEEL, U, 30, KM, "Silla de ruedas, ritmo de paseo"},
    {99, ACT_CAT_ADAPTED, ACT_IC_WHEEL, U, 60, KM, "Silla de ruedas, ritmo de carrera"},
    {100, ACT_CAT_ADAPTED, ACT_IC_WHEEL, U, 60, KM, "Handbike"},
};
#define N_TYPES (sizeof(kTypes) / sizeof(kTypes[0]))

static const char *const kCatNames[ACT_CAT_COUNT] = {
    "Caminar y correr",
    "Senderismo, ciclismo y máquinas",
    "Fuerza y HIIT",
    "Yoga, pilates y baile",
    "Pelota y raqueta",
    "Combate",
    "Natación y agua",
    "Invierno, montaña y exterior",
    "Otras",
    "Adaptadas",
};

static const uint8_t kCatIcons[ACT_CAT_COUNT] = {
    ACT_IC_RUN, ACT_IC_BIKE, ACT_IC_STRENGTH, ACT_IC_YOGA, ACT_IC_BALL,
    ACT_IC_COMBAT, ACT_IC_SWIM, ACT_IC_MOUNTAIN, ACT_IC_STAR, ACT_IC_WHEEL,
};

size_t act_catalog_count(void)
{
    return N_TYPES;
}

const act_type_t *act_catalog_at(size_t i)
{
    return i < N_TYPES ? &kTypes[i] : NULL;
}

const act_type_t *act_catalog_find(uint8_t id)
{
    for (size_t i = 0; i < N_TYPES; i++) {
        if (kTypes[i].id == id) {
            return &kTypes[i];
        }
    }
    return NULL;
}

const act_type_t *act_catalog_get(uint8_t id)
{
    const act_type_t *t = act_catalog_find(id);
    return t ? t : act_catalog_find(ACT_TYPE_OTHER);
}

const char *act_cat_name(act_cat_t c)
{
    return (unsigned)c < ACT_CAT_COUNT ? kCatNames[c] : "";
}

act_icon_t act_cat_icon(act_cat_t c)
{
    return (unsigned)c < ACT_CAT_COUNT ? (act_icon_t)kCatIcons[c] : ACT_IC_STAR;
}

size_t act_catalog_in_cat(act_cat_t c, uint8_t *ids, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < N_TYPES && n < max; i++) {
        if (kTypes[i].cat == c) {
            ids[n++] = kTypes[i].id;
        }
    }
    return n;
}

/* Letras acentuadas del español en UTF-8 (2 bytes, 0xC3 xx) -> ASCII. */
static char fold2(unsigned char b)
{
    switch (b) {
    case 0xA1: case 0x81: return 'a'; /* á Á */
    case 0xA9: case 0x89: return 'e'; /* é É */
    case 0xAD: case 0x8D: return 'i'; /* í Í */
    case 0xB3: case 0x93: return 'o'; /* ó Ó */
    case 0xBA: case 0x9A: case 0xBC: case 0x9C: return 'u'; /* ú Ú ü Ü */
    case 0xB1: case 0x91: return 'n'; /* ñ Ñ */
    default: return 0;
    }
}

void act_fold(const char *in, char *out, size_t n)
{
    if (!out || n == 0) {
        return;
    }
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && k + 1 < n; p++) {
        if (*p == 0xC3 && p[1]) {
            const char c = fold2(p[1]);
            p++;
            if (c) {
                out[k++] = c;
            }
            continue;
        }
        if (*p >= 0x80) {
            continue; /* otros caracteres no ASCII no cuentan para buscar */
        }
        out[k++] = (char)((*p >= 'A' && *p <= 'Z') ? *p + ('a' - 'A') : *p);
    }
    out[k] = 0;
}

static bool is_sep(char c)
{
    return c == ' ' || c == ',' || c == '-' || c == '/' || c == '(' || c == ')' || c == '.';
}

/* ¿Alguna palabra de name empieza por tok (len bytes)? */
static bool word_prefix(const char *name, const char *tok, size_t len)
{
    for (const char *w = name; *w;) {
        while (*w && is_sep(*w)) {
            w++;
        }
        if (!*w) {
            break;
        }
        if (strncmp(w, tok, len) == 0) {
            return true;
        }
        while (*w && !is_sep(*w)) {
            w++;
        }
    }
    return false;
}

size_t act_catalog_search(const char *query, uint8_t *ids, size_t max)
{
    char q[40];
    act_fold(query, q, sizeof(q));
    /* sin nada que buscar: sin resultados (la interfaz enseña las categorías) */
    bool any = false;
    for (const char *c = q; *c; c++) {
        any |= !is_sep(*c);
    }
    if (!any) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < N_TYPES && n < max; i++) {
        char name[64];
        act_fold(kTypes[i].name, name, sizeof(name));
        bool ok = true;
        for (const char *t = q; *t && ok;) {
            while (*t && is_sep(*t)) {
                t++;
            }
            size_t len = 0;
            while (t[len] && !is_sep(t[len])) {
                len++;
            }
            if (len) {
                ok = word_prefix(name, t, len);
            }
            t += len;
        }
        if (ok) {
            ids[n++] = kTypes[i].id;
        }
    }
    return n;
}
