/* lv_conf.h SOLO para la vista previa de PC (tools/sim). El firmware usa Kconfig. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
/* Asignador propio de LVGL (solo en el PC) para poder medir fugas con
 * lv_mem_monitor(). El firmware usa el de la libc (CONFIG_LV_USE_CLIB_MALLOC). */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (2U * 1024U * 1024U)
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16
#define LV_GRADIENT_MAX_STOPS 4
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_USE_SYSMON 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_BUILD_EXAMPLES 0
/* Una aserción de LVGL termina la vista previa (en vez de quedarse en un
 * bucle infinito) para que el fallo se vea enseguida. */
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();
/* JPEG baseline del navegador (F6): el TJpgDec que trae LVGL, como en el reloj */
#define LV_USE_TJPGD 1
/* Vistas previas del selector de esferas, como en el reloj */
#define LV_USE_SNAPSHOT 1

#endif
