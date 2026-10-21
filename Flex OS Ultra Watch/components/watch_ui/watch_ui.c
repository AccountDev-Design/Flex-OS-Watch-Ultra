/*
 * watch_ui — arranque de la interfaz. Ver include/watch_ui.h
 *
 * Arranque sin pantallas congeladas (como el splash de la referencia,
 * Home.h:277-312, pero sin esperas artificiales):
 *  1. Nada más iniciar la pantalla, el splash: negro con "Flex OS" que aparece
 *     en 600 ms. Solo cambia la opacidad del texto, así que cada cuadro
 *     redibuja un área pequeña. LVGL lo anima mientras app_main arranca el
 *     táctil, la batería, el RTC y la sonda de flash.
 *  2. Cuando la interfaz está lista, el texto se apaga en 200 ms (o menos si
 *     aún no había terminado de aparecer) y se carga la esfera.
 *  3. El logotipo se ve como mínimo SPLASH_MIN_MS desde que aparece. En el
 *     reloj el arranque tarda bastante menos y el logotipo era solo un
 *     destello; si el arranque tarda más, no se añade ninguna espera.
 */
#include "watch_ui.h"

#include "ui_internal.h"
#include "watch_theme.h"
#if LV_USE_TJPGD
#include "libs/tjpgd/lv_tjpgd.h"
#endif

#define SPLASH_OUT_MS (FLEX_ANIM_BOOT_FADE_MS / 3)
#define SPLASH_MIN_MS 3000u

static watch_ui_boot_info_t s_boot;

static struct {
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *sub;
    lv_obj_t *next; /* pantalla que se carga al terminar */
    uint32_t shown_at;
    lv_timer_t *hold; /* espera hasta SPLASH_MIN_MS */
} s_splash;

const watch_ui_boot_info_t *ui_boot_info(void)
{
    return &s_boot;
}

bool ui_splash_active(void)
{
    return s_splash.scr != NULL;
}

static void set_text_opa(int32_t v)
{
    lv_obj_set_style_text_opa(s_splash.title, (lv_opa_t)v, 0);
    lv_obj_set_style_text_opa(s_splash.sub, (lv_opa_t)(v * 7 / 10), 0);
}

/* Entrada y salida con funciones distintas: lv_anim_start() sustituye la
 * animación con el mismo par (objeto, función). */
static void opa_in_cb(void *var, int32_t v)
{
    (void)var;
    set_text_opa(v);
}

static void opa_out_cb(void *var, int32_t v)
{
    (void)var;
    set_text_opa(v);
}

/* Las animaciones del splash van ligadas a s_splash y no a la pantalla:
 * lv_screen_load() borra las animaciones de la pantalla que carga. */
static void splash_done_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_t *old = s_splash.scr;
    lv_screen_load(s_splash.next);
    s_splash.scr = NULL;
    s_splash.next = NULL;
    lv_obj_delete_async(old);
}

void watch_ui_boot_splash(void)
{
    if (s_splash.scr) {
        return;
    }
#if LV_USE_TJPGD
    /* Fuera de la cadena de decodificadores de imagen de LVGL: el navegador
     * decodifica sus JPEG con TJpgDec directamente (ui_jpeg.c) y ninguna
     * imagen de LVGL es JPEG. Registrado el último, LVGL lo consultaba el
     * PRIMERO para cada imagen que dibujaba, y su decoder_info reserva 4272 B
     * de pila (uint8_t workb[4096], marco fijo al entrar aunque la imagen no
     * sea JPEG; medido con -fstack-usage) en el punto más profundo del
     * render. Con la pila de 10 KB de la tarea de LVGL eso desbordaba en
     * pantallas con muchas imágenes dentro de capas (selector de esferas con
     * el cajón detrás): reinicio del 2026-10-20 (docs/F11-ESTABILIDAD.md). */
    lv_tjpgd_deinit();
#endif
    watch_theme_init();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_splash.title = watch_theme_label(scr, FLEX_FONT_LARGE, FLEX_C_WHITE, "Flex OS");
    lv_obj_align(s_splash.title, LV_ALIGN_CENTER, 0, -16);
    s_splash.sub = watch_theme_label(scr, FLEX_FONT_SMALL, FLEX_C_TXT2, "Watch Ultra");
    lv_obj_align_to(s_splash.sub, s_splash.title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    s_splash.scr = scr;
    s_splash.shown_at = lv_tick_get();
    set_text_opa(LV_OPA_TRANSP);
    lv_screen_load(scr);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_splash);
    lv_anim_set_exec_cb(&a, opa_in_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, FLEX_ANIM_BOOT_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void fade_out(void);

static void hold_done_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_splash.hold = NULL;
    fade_out();
}

static void leave_splash(lv_obj_t *next)
{
    if (!s_splash.scr) {
        lv_screen_load(next);
        return;
    }
    s_splash.next = next;
    const uint32_t shown = lv_tick_elaps(s_splash.shown_at);
    const uint32_t out = SPLASH_OUT_MS;
    if (shown + out < SPLASH_MIN_MS) {
        /* La interfaz ya está lista debajo; solo falta el tiempo mínimo. */
        s_splash.hold = lv_timer_create(hold_done_cb, SPLASH_MIN_MS - out - shown, NULL);
        return;
    }
    fade_out();
}

static void fade_out(void)
{
    lv_anim_delete(&s_splash, opa_in_cb);
    const int32_t from = lv_obj_get_style_text_opa(s_splash.title, 0);
    uint32_t ms = (uint32_t)(SPLASH_OUT_MS * from / LV_OPA_COVER);
    if (ms < 60) {
        ms = 60;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_splash);
    lv_anim_set_exec_cb(&a, opa_out_cb);
    lv_anim_set_values(&a, from, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, splash_done_cb);
    lv_anim_start(&a);
}

void watch_ui_home_create(const watch_ui_boot_info_t *info)
{
    s_boot = *info;
    watch_theme_init();
    ui_system_start(info->power_ok);
    leave_splash(ui_nav_start(ui_face_create));
}
