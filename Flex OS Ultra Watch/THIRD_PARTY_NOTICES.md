# Avisos de terceros

| Elemento | Uso en este repositorio | Licencia | Origen |
|---|---|---|---|
| Outfit Regular (`assets/fonts/Outfit-Regular.ttf`) y las fuentes LVGL generadas a partir de ella (`components/watch_theme/fonts/`) | incluido y convertido | SIL Open Font License 1.1 (`assets/fonts/OFL.txt`), © 2021 The Outfit Project Authors | https://github.com/Outfitio/Outfit-Fonts, commit 9027738 (comprobado SHA-256 idéntico) |
| Outfit ExtraLight, Medium y SemiBold (`assets/fonts/Outfit-*.ttf`) y sus fuentes LVGL (`flex_outfit_xl_105`, `flex_outfit_med_*`, `flex_outfit_sb_101`) | incluido y convertido (rediseño 2026-10) | SIL Open Font License 1.1 (`assets/fonts/OFL.txt`), © 2021 The Outfit Project Authors | https://github.com/Outfitio/Outfit-Fonts, commit 9027738, sin cambios |
| League Spartan Bold (`assets/fonts/LeagueSpartan-Bold.ttf`) y sus fuentes LVGL (`flex_spartan_*`) | incluido y convertido: instancia wght=700 de `LeagueSpartan[wght].ttf` con `fonttools varLib.instancer` | SIL Open Font License 1.1 (`assets/fonts/OFL-LeagueSpartan.txt`), © The League Spartan Project Authors | google/fonts, `ofl/leaguespartan` |
| FlexCondensed Medium (`assets/fonts/FlexCondensed-Medium.ttf`) y sus fuentes LVGL (`flex_cond_*`) | **versión modificada** de Barlow Condensed Medium: estrechada al 80 % en horizontal y con otro nombre (`tools/fonts/derive_condensed.py`), como exige la OFL para una versión modificada | SIL Open Font License 1.1 (`assets/fonts/OFL-Barlow.txt`), © 2017 The Barlow Project Authors | google/fonts, `ofl/barlowcondensed` |
| Fondos de las seis esferas nuevas (`assets/faces/face_*.jpg`) | derivados de las capturas de diseño que aportó el usuario (`design/faces/*.png`) con `tools/faces/gen_faces.py`: recorte a 410x502, borrado de los textos (relleno de OpenCV, método Telea) y, en "Ultra", la esfera redibujada por el script (marcas y números) | las capturas son material del usuario; el repositorio no recoge la licencia de las fotografías que contienen: **confirmar que se pueden distribuir** antes de publicar el firmware | aportadas en la petición del rediseño (2026-10) |
| Capturas de referencia del rediseño (`design/referencias/*.png`) | solo para comparar la vista previa con el diseño (no van en el firmware) | material del usuario | ídem |
| OpenCV, Pillow, NumPy, fontTools | herramientas para generar los recursos (no se distribuyen) | Apache-2.0 / HPND / BSD-3-Clause / MIT | pip |
| BSP `waveshare/esp32_s3_touch_amoled_2_06` 2.0.0 | dependencia gestionada (no se copia) | Apache-2.0 | waveshareteam/Waveshare-ESP32-components |
| `waveshare/esp_lcd_sh8601` | dependencia transitiva | Apache-2.0 | ídem |
| LVGL 9.5.0 | dependencia gestionada | MIT | lvgl/lvgl |
| esp_lvgl_port, esp_lcd_touch, esp_lcd_touch_ft5x06, esp_io_expander, esp_lcd_panel_io_additions, cmake_utilities, esp_codec_dev | dependencias transitivas | Apache-2.0 | espressif |
| ESP-IDF 5.5.5 | entorno de compilación | Apache-2.0 | espressif/esp-idf |
| Mapa de registros del AXP2101 | consultado (no se copia código) | XPowersLib: MIT, © 2022 Lewis He | ejemplo oficial `01_AXP2101` |
| Tokens de diseño de Flex OS Ultra | transcritos | proyecto propio del autor (repositorio de referencia sin archivo de licencia) | accountdev-design/Flex-OS-Ecosistema @ 5dbc949 |
| `lv_font_conv` 1.5.3 | herramienta (no se distribuye) | MIT | lvgl/lv_font_conv |

Rediseño 2026-10: la imagen de referencia del cajón en panal que envió el usuario
(de otro fabricante) se usó solo como referencia de COMPORTAMIENTO (lente,
desplazamiento, reciclado de celdas); no se copió ningún icono, ilustración ni
recurso gráfico suyo y no está en el repositorio. Los iconos son los de Flex OS
(`flex_icons.c`, `flex_app_icons.c`), dibujados con primitivas de LVGL.
