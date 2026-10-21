# F11 · Estabilidad: fallos fatales, memoria y fluidez

Fecha: 2026-10-21. Rama `ccr-569b85ce-6eudu3` de `AccountDev-Design/Flex-OS-Watch-Ultra`.
Punto de partida: `e9fe357` (F10). El repositorio de referencia `Flex-OS-Ecosistema` no se ha tocado.

**Estado de todo lo de este documento:** compilado con ESP-IDF 5.5.5 para `esp32s3` (0 avisos), 3019 comprobaciones de
PC en verde y vista previa completa con LVGL 9.5 real en verde, también con ASan/UBSan y seis semillas de la prueba
aleatoria. **Nada se ha probado todavía en el reloj.** Ningún fallo se da por cerrado hasta pasar sus pruebas físicas
(T142-T150, §9).

## 1. Cómo se investigó

- El vídeo del 2026-10-20 (esfera Parallax con fondo Océano: mantener pulsada la esfera, salir del selector con el
  borde izquierdo y el reloj se reinicia) se reprodujo paso a paso en la vista previa. En el PC **no fallaba**: la pila
  de un hilo de PC es de 8 MB. Se añadió una sonda de pila (`SIM_STACK_PROBE=1`): la vista previa corre LVGL en un
  hilo con la pila pintada y mide cuánto se usó en cada batería.
- Las pilas reales del reloj se midieron compilando el firmware con `-fstack-usage` para Xtensa (los tamaños de marco
  que da GCC para el chip, no los del PC).
- `LV_ASSERT_HANDLER` del reloj es `while (1);`: un fallo de LVGL **cuelga** la pantalla, no reinicia. Un reinicio como
  el del vídeo es un pánico de la CPU (desbordamiento de pila, puntero nulo o memoria corrupta). El firmware guarda el
  volcado en la flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`): si vuelve a pasar, `idf.py coredump-info` dice la
  línea exacta (T149).

## 2. Fallo 1: reinicio al salir del editor de esferas

### Causa raíz (desbordamiento de la pila de LVGL)

Tres cosas se sumaban en la tarea de LVGL, que tenía 10 KB de pila:

1. **El decodificador TJpgDec de LVGL** (`decoder_info`) declara en la pila un búfer `uint8_t workb[4096]`: su marco
   mide **4272 B** (`-fstack-usage`). Se registra el último, así que LVGL le pregunta **el primero** por cada imagen
   que dibuja, aunque la imagen sea un mapa de bits RGB565 y no un JPEG. El selector dibuja cinco imágenes.
2. **El selector renderizaba las vistas previas dentro del evento** `LONG_PRESSED` (y al crearse): una captura de
   pantalla completa (`lv_snapshot`) anidada dentro del procesado del toque, del despacho de eventos y del dibujo.
3. **Las miniaturas usaban `clip_corner`** (esquinas recortadas): LVGL dibuja cada una en una capa intermedia, lo que
   anida un segundo despacho de dibujo.

En la vista previa, la batería de esferas llegaba a **16,5 KB** de pila. Con el gesto de borde (que mueve el selector
mientras se dibuja) el pico coincidía con la imagen más profunda y la pila de 10 KB se desbordaba: pánico y reinicio.

### Corrección

- `watch_ui.c`: TJpgDec se retira de la cadena de decodificadores al arrancar (`lv_tjpgd_deinit`). El reloj no usa
  JPEG a través de LVGL: las páginas del navegador se decodifican con su propio decodificador, fuera de este camino.
- `watch_display.c`: la pila de la tarea de LVGL pasa de 10 a **16 KB**, con un registro cada 30 s del mínimo libre
  real (`pila de LVGL: minimo libre X de 16384 B`).
- `ui_face_editor.c` (reescrito sin perder funciones):
  - nada se renderiza al crear ni dentro de un evento; las vistas previas se generan de una en una desde un
    temporizador (40 ms) y **solo** si la navegación está quieta, el selector está arriba y el panel rápido cerrado;
    un gesto de borde a medias o una transición pausan la generación;
  - un único búfer de captura (410×502 RGB565) reutilizado y liberado al terminar; antes de pedirlo se comprueba que
    la PSRAM tiene ese bloque **más 256 KB** de margen. Si no, el selector funciona igual con nombres, sin miniaturas,
    y no reintenta en bucle;
  - las esquinas redondeadas se pintan en negro al reducir la imagen, en lugar de `clip_corner` (sin capas);
  - al cerrar se borra el temporizador, la captura y la fuente de cada imagen **antes** de liberar sus píxeles;
  - tocar una esfera durante una transición se ignora.
- Medido después: la batería de esferas baja de 16,5 a **12,7 KB** de pila en la vista previa (que es un PC: en el
  reloj los marcos son de otro tamaño, por eso se registra el valor real, T142).

### Regresión

`sim_faces.c`: el escenario del vídeo (fondo Océano, esfera Parallax) 6 veces, saliendo por los bordes izquierdo,
derecho e inferior, a veces con las vistas previas a medias. `sim_mem.c`: 8 ciclos completos del selector sin fugas ni
temporizadores, ninguna vista previa durante una transición o un gesto, y el selector sin PSRAM para la captura.

## 3. Fallo 2: «memoria insuficiente» al abrir una sesión del navegador

### Causa raíz (la RAM interna se la quedaba LVGL)

- LVGL usaba `LV_STDLIB_CLIB` (`malloc`) y el proyecto tiene `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`: toda reserva
  de menos de 16 KB va a la RAM interna. Así, **todos** los objetos, estilos y textos de la interfaz vivían en la RAM
  interna, la misma que necesitan el Wi-Fi, el Bluetooth, TLS y la pila de la sesión del navegador.
- Medido en la vista previa (mismo LVGL): esfera 13 KB; con el cajón 20 KB; con Ajustes 29 KB; con Device Care 41 KB;
  más las apps suspendidas. La sesión del navegador exige ≥ 20 KB internos libres además de su pila; con Wi-Fi y BLE
  activos no quedaba.
- Además, cerrar el navegador esperaba hasta 3 s **dentro de la tarea de LVGL** a que la sesión terminase, y la pila
  de la sesión (8 KB con TLS) iba justa.

### Corrección

- `lv_mem_watch.c` (nuevo) + `LV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM`: LVGL reserva en **PSRAM** y solo recurre a la RAM
  interna si la PSRAM no puede; cuenta esas recaídas y los fallos, que salen en el registro de la pila de LVGL. No es
  una «compactación» ficticia: la memoria de LVGL simplemente deja de competir con la radio.
- `watch_browser.c`: antes de abrir una sesión, `watch_browser_mem_check` comprueba el **bloque contiguo** interno (pila
  de la sesión + 512 B), la RAM interna libre (pila + 20 KB), el bloque de PSRAM para el búfer de recepción y la PSRAM
  libre (+160 KB). Si falta, no reserva nada y explica cuánto hay: «Memoria insuficiente. Falta RAM interna: X KB libres
  (bloque Y KB). Reintento automático.»
- `ui_browser.c`: antes de abrir suelta la caché de imágenes de LVGL (memoria propia y recuperable, no de otras apps);
  si no hay memoria, reintenta solo cada 3 s mientras la página está a la vista; al retomar la app pide una imagen
  nueva; no decodifica imágenes con la página oculta.
- El cierre es **asíncrono**: la tarea de la sesión libera su búfer y se marca terminada; LVGL ya no espera. Mientras
  se cierra, abrir otra dice «Cerrando la sesión anterior…» y reintenta.
- La pila de la sesión pasa de 8 a 10 KB; la tarea registra su mínimo libre al cerrar (`sesion cerrada: pila minima
  libre X de 10240 B`) y avisa si baja de 2 KB.

### Regresión

`sim_mem.c`: con 20 KB internos, el navegador no abre la sesión, no reserva nada y lo explica con las cifras; al volver
la memoria se abre sola; 8 aperturas y cierres sin fugas ni temporizadores.

## 4. Fallo 3: cierre del navegador (puntero nulo)

### Causa raíz

La prueba aleatoria con UBSan (semilla 13) encontró un acceso a puntero nulo en `page_touch_cb`. El lienzo de la página
escucha **todos** los eventos (`LV_EVENT_ALL`). Al borrar la página, LVGL envía `LV_EVENT_DELETE` primero a la página
(cuyo `delete_cb` libera el estado `U` y lo pone a `NULL`) y después a cada hijo: el lienzo recibía su `DELETE` y leía
`U->act`. Solo pasaba si el borrado ocurría dentro del procesado de un toque (con `lv_indev_active()` válido): el caso
real es **abrir otra app desde el cajón con el navegador suspendido**. En el reloj eso es un `LoadProhibited` y un
reinicio.

### Corrección y regresión

`page_touch_cb` sale si `U` ya no existe. `sim_mem.c` reproduce el caso exacto (navegador suspendido con una página
remota; un toque abre otra app y lo descarta): **sin la corrección, UBSan lo detecta; con ella, pasa** (comprobado en
los dos sentidos).

## 5. Wi-Fi

- Las cinco causas de fallo al activar encontradas en F10 siguen corregidas (`F10-EXPERIENCIA.md` §7.1).
- Lo nuevo de F11 que le afecta: la RAM interna que se llevaba LVGL queda libre para el Wi-Fi y el Bluetooth
  (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` y NimBLE en memoria externa ya estaban).
- `watch_net.c` registra la memoria antes y después de encender y al apagar (`[mem] wifi: antes de encender`,
  `wifi: encendido`, `wifi: apagado`). Solo registra: no cambia el comportamiento.
- **No verificado en el reloj** (T144).

## 6. Diagnóstico de memoria (solo registro, nada en la interfaz)

- `watch_diag_mem_start()` (al arrancar) engancha `heap_caps_register_failed_alloc_callback`: cuenta cada reserva
  fallida y guarda su tamaño y capacidades (el gancho solo cuenta; el registro se hace después, fuera de la reserva).
- `watch_diag_mem_event(qué)` escribe una línea:
  `[mem] abrir: interna X B (+d, min M, bloque B) | PSRAM Y B (+d, min M, bloque B)` y, si las hay,
  `[mem] N reservas fallidas desde el arranque (la última: T B, capacidades 0x…)`.
- Se registra al abrir y cerrar páginas (`abrir`, `volver`, `al cajon`, `a la esfera`) y en el Wi-Fi: así se ve la
  evolución de la memoria abriendo y cerrando apps.
- La pila de LVGL (mínimo libre) y las recaídas de LVGL a RAM interna, cada 30 s si empeoran.
- Sin contadores de FPS ni CPU en la interfaz (sigue retirado; `FLEX_PERF_MONITOR` solo para pruebas).

## 7. Gesto del cajón

- **Antes:** deslizar hacia abajo desde el centro de la esfera abría el cajón.
- **Ahora:** desde el **borde inferior** de la esfera hacia arriba (franja de 56 px con una pequeña marca). El cajón
  sube siguiendo al dedo; si el gesto se queda corto (sin recorrido ni velocidad suficientes) vuelve a bajar (`ui_nav_drag_open_*`). Deslizar desde el centro
  ya no hace nada.
- Sin cambios: borde superior hacia abajo = panel rápido desde cualquier página; borde derecho = atrás; borde izquierdo
  = salir al cajón; en una app, borde inferior = a la esfera sin cerrarla.
- Regresión: `sim_apps.c` (centro hacia abajo y hacia arriba no abren nada; lateral tampoco; un gesto corto se cancela;
  el gesto completo abre el cajón).

## 8. Redibujado y fluidez

- Batería nueva `sim_redraw.c`: deja cada página quieta 5 s y cuenta los píxeles enviados a la pantalla.
- Antes, en reposo: Sensores ~2 pantallas por segundo; Flex Compass, Device Care, Wi-Fi y Flex Phone también
  repintaban sin cambios. **Ahora: 0 px en todas las páginas medidas.**
- Cómo: `watch_theme_set_text_color`, `watch_theme_set_bg_color` y `watch_theme_set_visible` solo tocan el objeto si
  el valor cambia; textos comparados antes de asignarlos; la aguja de la brújula y el nivel de Sensores solo se mueven
  si cambia el píxel de la punta; el tiempo encendido de Device Care con resolución de minuto.
- Nada de reconstruir páginas para actualizar un dato. No se ha tocado el doble búfer ni el DMA.
- **Limitación de hardware conocida:** el BSP no expone la línea TE de la pantalla, y con búferes parciales una
  transición a pantalla completa puede mostrar una «cortina». No se ha cambiado la configuración de render a ciegas.

## 9. Pruebas físicas (T142-T150)

Con el monitor serie abierto (`idf.py -p PUERTO monitor`) y copiando las líneas `[mem]`, `pila de LVGL` y
`watch_browser`.

| Prueba | Qué hacer | Qué mirar |
|---|---|---|
| T142 | Fondo Océano + esfera Parallax. Mantener pulsada la esfera y salir del selector por el borde izquierdo; repetir 10 veces, también por los bordes derecho e inferior | Ningún reinicio. `pila de LVGL: minimo libre` por encima de 3000 B |
| T143 | Abrir el selector y hacer un gesto de borde mientras aparecen las miniaturas | Las miniaturas salen de una en una, sin tirones; el gesto no se corta |
| T144 | Activar y desactivar el Wi-Fi 10 veces (panel rápido y Ajustes), con el Bluetooth conectado | Ningún reinicio. Copiar las líneas `[mem] wifi:` |
| T145 | Con Wi-Fi y Bluetooth activos, abrir el Navegador | Se abre la sesión. Si sale «Memoria insuficiente…», anotar las cifras: debe reintentarse solo |
| T146 | Abrir y cerrar el Navegador 10 veces; después, con el navegador suspendido (borde inferior), abrir otra app desde el cajón | Ningún reinicio. `sesion cerrada: pila minima libre` por encima de 2048 B |
| T147 | En la esfera: deslizar desde el borde inferior hacia arriba; luego deslizar desde el centro hacia abajo y hacia arriba | Solo el primero abre el cajón, siguiendo al dedo; un gesto corto vuelve a bajar |
| T148 | Dejar quietas Sensores, Flex Compass y Device Care 1 min | Sin parpadeos; solo cambian los números que cambian |
| T149 | Si en cualquier prueba el reloj se reinicia | `idf.py -p PUERTO coredump-info` y enviarme la salida completa |
| T150 | 30 min de uso normal abriendo y cerrando apps | Las líneas `[mem]`: la RAM interna vuelve al mismo valor al volver a la esfera; `reservas fallidas` = 0 o con su tamaño |

## 10. Pendiente y conocido

- Todo lo anterior, sin verificar en el reloj (T142-T150).
- Notas (autoguardado) y las listas del navegador escriben en LittleFS desde la tarea de LVGL: escrituras pequeñas, pero
  bloquean la interfaz unos milisegundos. Se puede pasar a una tarea aparte si T150 muestra tirones.
- La «cortina» en transiciones a pantalla completa (sin TE, §8).

## 11. Compilar, grabar y verificar

```
cd "Flex OS Ultra Watch"
idf.py set-target esp32s3      # solo la primera vez
idf.py build
idf.py -p PUERTO flash monitor
```

Sin red (como en este entorno): `FLEX_DEPS_DIR=... bash tools/offline/build_offline.sh build`.
Pruebas de PC: `make -C tests/host test` (con `IDF_PATH`). Vista previa: `make -C tools/sim LVGL_DIR=...` y
`tools/sim/build/sim tools/sim/out` (con `SAN=1` para los sanitizadores; `SIM_ONLY=memoria`, `SIM_STACK_PROBE=1`).

Tamaño del firmware (estático; `esp_idf_size`): DIRAM 170 051 → 170 187 B (+136 B), imagen 2 538 623 → 2 541 207 B.
En ejecución: +6 KB de pila de LVGL y +2 KB de pila de la sesión del navegador (solo con sesión abierta), en RAM
interna; a cambio, todas las reservas de LVGL (13-41 KB según la pantalla, más las apps suspendidas) salen de la RAM
interna a la PSRAM.
