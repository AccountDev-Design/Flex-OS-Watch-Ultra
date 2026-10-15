# B4 — Calidad: respuesta, rendimiento, AMOLED, área segura e integridad visual

Correcciones pedidas tras la prueba física del 2026-10-15. En esa prueba el usuario envió fotos del Diagnóstico y un vídeo de la carga. Lo que muestran:

- **Flash:** 32 MB, JEDEC c84019, sin aliasing.
- **AXP2101:** 538 lecturas sin errores, I2C 1,5 ms (máximo 1,8 ms), pila mínima de 656 B.
- **RTC:** OS=1, sin hora.
- **Rendimiento:** 50 FPS en reposo.

Al desplazar la lista o mover el panel rápido, el medidor de LVGL bajó a 22-33 FPS con hasta 65 ms de dibujo por cuadro.

| Bloque | Contenido | Estado |
|---|---|---|
| C1 | Respuesta rápida al enchufar y desenchufar (B2-6 en `B2-ENERGIA.md`) | implementado · pruebas de PC · prueba física pendiente (T41) |
| C2 | Rendimiento del dibujo, animación de carga sin tirón y negro AMOLED | implementado · pruebas de PC · prueba física pendiente (T42, T43) |
| C3 | Área segura con las esquinas redondeadas del panel | pendiente |
| C4 | Verificador automático de integridad visual en todas las pantallas | pendiente |

## C2. Rendimiento, animación de carga y negro AMOLED

### Causas encontradas

1. **Tirón al empezar la animación de carga: un error de animación.**
   - En LVGL 9.5 `lv_anim_t.early_apply` vale 1 por defecto: una animación **con retraso** aplica su valor inicial en el momento de crearla.
   - La salida programada a los 2 s ponía el contenido a opacidad completa nada más empezar, y la entrada lo bajaba a 0 para volver a subirlo: un destello.
   - El aviso de batería tenía el mismo fallo: aparecía de golpe en su sitio antes de entrar deslizando.
   - Corregido con `lv_anim_set_early_apply(false)` en las animaciones con retraso. La vista previa lo comprueba: en su primer cuadro la animación de carga está casi transparente y el aviso todavía está fuera de la pantalla.
2. **La animación de carga fundía un contenedor de pantalla completa con hijos.**
   - LVGL lo dibuja en una capa intermedia y la mezcla en cada cuadro, sobre un velo negro al 88 % encima de la esfera.
   - Ahora el fondo es negro opaco desde el primer cuadro y solo se funden la opacidad del anillo y de los textos y el color del rayo: zonas pequeñas, sin capas.
3. **LVGL 9.5 no tiene en cuenta `lv_layer_top()` al decidir qué está tapado** (`lv_refr.c`, `refr_configured_layer`): con el panel rápido abierto se seguía dibujando la esfera entera debajo.
   - El panel rápido y la animación de carga son ahora hijos de la pantalla de navegación, encima de las páginas (`ui_overlay_parent`). LVGL ya omite lo que tapan, también mientras el panel baja siguiendo al dedo.
   - Las páginas nuevas se insertan siempre debajo de esas capas.
   - El panel ya no tiene radio: con esquinas redondeadas no cuenta como "cubre la pantalla".
4. **El fondo Onyx era una imagen de 411 KB en PSRAM** que se copiaba en cada cuadro completo, y las tarjetas eran translúcidas, con mezcla por píxel y una capa de brillo con degradado.

### Negro AMOLED

- **Fondo:** `FLEX_C_PAGE` pasa de `#12141C` (gris muy oscuro de la referencia, pensado para LCD) a **`#000000`**. Es el fondo de la esfera, las páginas, el panel rápido, la animación de carga y el splash.
  - En un AMOLED los píxeles negros están apagados: menos consumo.
  - La reducción de consumo no se ha medido.
- **Superficies (filas, mosaicos, tarjetas):**
  - Conservan el tinte de vidrio de la referencia (`glassTintMix`), **precalculado sobre negro** y opaco (`flex_mix_rgb`, con pruebas): `#0B0C10`, con borde de luz arriba y a la izquierda.
  - Los estados pulsado, activo y no disponible también son opacos.
  - Se mantiene el contraste del texto (`#F0F2F8` y `#A0A6B6` sobre casi negro).
- **Fondo Onyx:** sigue disponible (`watch_theme_set_wallpaper`). Con él las tarjetas vuelven a ser translúcidas. Será una opción de Ajustes > Pantalla con F4.1.

### Comprobado en el PC

- **`tests/host`:** 625 comprobaciones, 0 fallos (+7 de la mezcla de color).
- **`tools/sim`:** todas las baterías, 0 fallos, incluidas las comprobaciones nuevas contra el destello de la animación y la aparición de golpe del aviso.
- **Banco de dibujo** (`SIM_BENCH=1 tools/sim/build/sim out`): tiempo de CPU del **PC** por cuadro, mediana de 7 ejecuciones. Sirve para comparar antes y después; **no** son tiempos del reloj, que es más lento y además ya no lee el fondo de la PSRAM.

  | Situación | Antes | Después | Mejora |
  |---|---|---|---|
  | Esfera, cuadro completo | 0,42 ms | 0,22 ms | ×1,9 |
  | Panel rápido abierto | 1,13 ms | 0,49 ms | ×2,3 |
  | Panel rápido a medias (siguiendo al dedo) | 0,88 ms | 0,37 ms | ×2,4 |
  | Desplazar la lista de Batería | 0,54 ms | 0,27 ms | ×2,0 |
  | Animación de carga (primer segundo) | 0,40 ms | 0,08 ms | ×5 |
  | Transición de apertura | 1,03 ms | 0,66 ms | ×1,6 |

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T42 | Fluidez | con el medidor de LVGL visible: desplazar la lista de Ajustes > Batería y bajar y subir el panel rápido despacio | Anota FPS y ms del medidor. Antes de B4: 22-33 FPS y hasta 65 ms. Se espera una mejora clara; el valor exacto solo se sabrá en el reloj |
| T43 | Animación de carga y negro | conectar el USB con la esfera visible | La animación aparece sin destello ni tirón, sobre negro puro. Los fondos de la esfera, Ajustes y el panel son negro puro: en una habitación a oscuras no se distingue el borde del panel |
