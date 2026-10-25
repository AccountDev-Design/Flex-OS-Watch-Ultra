/*
 * ui_wb — interfaz de Bienestar (privada de watch_ui). Todas las funciones se
 * llaman desde la tarea de LVGL.
 *
 * La interfaz solo LEE el servicio de actividad (watch_activity_get y su
 * caché del historial, copias bajo un cerrojo breve) y le manda comandos; no
 * crea otra instancia ni hace E/S. Las páginas se crean al abrirlas y se
 * borran al cerrarlas (con sus temporizadores, ui_page_timer); al refrescar
 * solo se reescribe lo que cambió.
 */
#pragma once

#include "act_catalog.h"
#include "act_engine.h"
#include "lvgl.h"
#include "watch_activity.h"

/* Colores de Bienestar (identidad Flex OS sobre negro AMOLED) */
#define WB_C_MOVE      0xFF3B6B /* anillo de mover (calorías activas) */
#define WB_C_EXER      0xB6F33A /* ejercicio */
#define WB_C_STEPS     0x3ADDEB /* pasos */
#define WB_C_STEPS_TXT 0xA9A2FF /* cifra de pasos en las tarjetas */
#define WB_C_DIST      0x5AB8FA
#define WB_C_TIMER     0xFFD60A /* cronómetro del entrenamiento */
#define WB_C_PACE      0x5FE3A1
#define WB_C_GO        0xB6F33A /* empezar */
#define WB_C_STOP      0xFF453A /* terminar */
#define WB_C_CARD_TOP  0x1E2126
#define WB_C_CARD_BOT  0x111316

/* ---- Iconos de entrenamiento (ui_wb_icons.c) ---- */
/** Icono vectorial de size px en rgb (sobre transparente). */
lv_obj_t *ui_wb_icon_create(lv_obj_t *parent, act_icon_t icon, int32_t size, uint32_t rgb);
void ui_wb_icon_set(lv_obj_t *obj, act_icon_t icon, uint32_t rgb);
/** Disco de color de categoría con el icono blanco dentro (filas y cabeceras). */
lv_obj_t *ui_wb_icon_badge(lv_obj_t *parent, act_icon_t icon, int32_t d, uint32_t bg_rgb);
uint32_t ui_wb_cat_color(act_cat_t c);

/* ---- Anillos (ui_wb_rings.c) ---- */
/** Tres anillos concéntricos de d px (mover, ejercicio, pasos) con grosor w. */
lv_obj_t *ui_wb_rings_create(lv_obj_t *parent, int32_t d, int32_t w);
/** Pone los porcentajes; animate: transición suave desde lo que se ve. */
void ui_wb_rings_set(lv_obj_t *rings, const act_rings_t *r, bool animate);

/* ---- Páginas ---- */
lv_obj_t *ui_wb_workouts_create(lv_obj_t *parent);     /**< selector: recientes, favoritos, categorías */
lv_obj_t *ui_wb_category_create(lv_obj_t *parent);     /**< tipos de ui_wb_open_category() */
lv_obj_t *ui_wb_search_create(lv_obj_t *parent);       /**< búsqueda con teclado */
lv_obj_t *ui_wb_workout_create(lv_obj_t *parent);      /**< cuenta atrás, en curso y resumen */
lv_obj_t *ui_wb_history_create(lv_obj_t *parent);
lv_obj_t *ui_wb_sessions_create(lv_obj_t *parent);     /**< todos los entrenamientos, por páginas */
lv_obj_t *ui_wb_session_create(lv_obj_t *parent);      /**< detalle de ui_wb_open_session() */
lv_obj_t *ui_wb_settings_create(lv_obj_t *parent);
lv_obj_t *ui_wb_edit_create(lv_obj_t *parent);         /**< editor de un valor (ui_wb_open_edit) */
lv_obj_t *ui_wb_calib_create(lv_obj_t *parent);        /**< calibración de la distancia */

void ui_wb_open_category(act_cat_t c);
/** El entrenamiento que abrirá ui_wb_workout_create (0 = el que está en marcha). */
void ui_wb_open_workout(uint8_t type, bool from_suggestion);
void ui_wb_open_session(const act_ses_rec_t *r);

/** Editor genérico de un número entero (escala: valor guardado / mostrado). */
typedef struct {
    const char *title;
    const char *unit;
    int32_t min, max, step, value;
    uint8_t decimals;     /**< 1: el valor está en décimas (peso) */
    bool optional;        /**< se puede dejar "Sin definir" (valor 0) */
    void (*apply)(int32_t v);
} ui_wb_edit_t;
void ui_wb_open_edit(const ui_wb_edit_t *e);

/** Vuelve a la página de Bienestar de debajo (app o tarjetas) sin pasar por
 * cada página intermedia; si no hay ninguna, atrás un nivel. */
void ui_wb_back_home(void);

/* ---- Ayudas compartidas (ui_wb_home.c) ---- */
/** Cambia el texto solo si cambia (no invalida el objeto en cada refresco). */
void ui_wb_set_text(lv_obj_t *label, const char *text);
/**
 * Texto que cabe sin cortarse (el verificador visual no admite "..."): con
 * la fuente grande si entra en una línea de w px; si no, con la pequeña en
 * dos. Devuelve el número de líneas.
 */
int ui_wb_fit(lv_obj_t *label, const char *text, int32_t w, const lv_font_t *big, const lv_font_t *small);
/** Tarjeta oscura de Bienestar de w x h dentro de una lista (ancho 100 %). */
lv_obj_t *ui_wb_card(lv_obj_t *list, int32_t h, bool clickable);
/** Fila pulsable con icono de entrenamiento, título y subtítulo opcional. */
lv_obj_t *ui_wb_row(lv_obj_t *list, act_icon_t icon, uint32_t color, const char *title, const char *sub);
/** Nombre corto del estado de la sesión ("En curso", "Pausa automática"...). */
const char *ui_wb_state_text(const act_view_t *v);
/** Texto de las métricas que puede medir un tipo ("Tiempo, pasos, distancia..."). */
void ui_wb_metrics_text(const act_type_t *t, char *b, size_t n);
