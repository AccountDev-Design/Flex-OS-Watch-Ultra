# F10 · Experiencia Watch Ultra: navegación, esferas, AOD, ahorro y estabilidad

Fecha: 2026-10-20. Rama `ccr-569b85ce-6eudu3` de `AccountDev-Design/Flex-OS-Watch-Ultra`.
Referencia de solo lectura: `AccountDev-Design/Flex-OS-Ecosistema`, rama `claude/flexos-ota-system-blq9g9-2csacx`
@ `5dbc949` (no se ha modificado).

**Estado de todo lo de este documento:** compilado con ESP-IDF 5.5.5 para `esp32s3` (sin avisos) y probado en el PC
(pruebas de lógica con ASan/UBSan y vista previa con LVGL 9.5 real, también con sanitizadores). **Nada se ha probado
todavía en el reloj**: las pruebas físicas son T124-T141 (§9).

## 1. Auditoría inicial (antes de tocar nada)

| Función | Estado encontrado | Acción |
|---|---|---|
| Arranque, pantalla, táctil, controladores (BSP oficial) | Implementado y funcional (verificado en el reloj en F1) | Conservado sin cambios |
| Splash | El logotipo se iba en cuanto la interfaz estaba lista (< 1 s): un destello | Corregido: mínimo 3 s (§7) |
| Medidor de FPS/CPU | Activo en `sdkconfig.defaults` ("solo para B1"), franja negra abajo | Retirado de la interfaz (§7) |
| Navegación | Pila de páginas con un gesto (borde izquierdo = atrás); la esfera abría las apps hacia la izquierda; panel rápido solo desde la esfera | Ampliada (§2), mismo gestor `ui_nav` |
| Lanzador | Rejilla de 3 columnas con nombres, iconos de esquinas redondeadas; incluía Sensores | Panal circular sin nombres (§3) |
| Esferas | Una sola esfera (digital + complicación de batería). El ajuste `face` existía en NVS pero no se usaba | 5 esferas, selector y persistencia (§4) |
| Panel rápido | Wi-Fi, Bluetooth, batería, Ajustes, brillo: funcionales | Ampliado: Ahorro y AOD; desde cualquier página (§5) |
| AOD / protección AMOLED | Ausentes (solo apagado por inactividad con atenuación fija a 1/3) | Implementados en el servicio existente `ui_screen` (§6) |
| Flex Ultra Power Saving | Ausente | Implementado (`ui_psave.c`) (§6) |
| Device Care | Existente: salud, métricas reales y pruebas | **No se recreó**: se añadieron secciones (§6) |
| Clima | Existente: motor de `FlexOS_Weather` portado (Open-Meteo, TLS verificado, 225 comprobaciones) | Sin cambios; ahora también alimenta la esfera modular |
| Brújula | Existente: pantalla de requisito sin magnetómetro + cabeceo/alabeo | Ampliada con rumbo RELATIVO por giroscopio (§8) |
| Wi-Fi | Implementado; antecedente de fallo al activar | Causas encontradas en el código y corregidas (§7.1) |
| Pruebas | 2756 comprobaciones de PC y vista previa completa en verde | Ahora 3019 comprobaciones y una batería nueva "esferas" |

## 2. Navegación y gestos globales (`ui_nav.c`, `ui_apps.c`, `ui_qp.c`)

Un solo gestor (`ui_nav`), ampliado: no hay otro paralelo. Cada página abierta tiene cuatro franjas de borde; todos
los gestos siguen al dedo y al soltar se completan o se cancelan con el reconocedor existente (`ui_gesture`: 35 % del
recorrido o velocidad, rechazo si el primer movimiento va por el otro eje).

| Gesto | Dónde | Resultado |
|---|---|---|
| Deslizar hacia abajo | esfera (fuera de la franja superior) | Cajón de aplicaciones, que baja desde arriba |
| Desde el borde inferior hacia arriba (30 px) | cualquier página | A la esfera **sin cerrar la app**: sus páginas quedan suspendidas, ocultas y con su estado; abrirla de nuevo desde el cajón la retoma tal cual |
| Desde el borde derecho hacia la izquierda (20 px) | cualquier página | Atrás un nivel (la página sale por la izquierda) |
| Desde el borde izquierdo hacia la derecha (20 px) | cualquier página | Sale de la app al cajón (si no se abrió desde el cajón, se crea uno debajo); en el propio cajón, a la esfera |
| Desde el borde superior hacia abajo (22 px) | cualquier página | Panel rápido encima; la página de debajo no cambia |
| Mantener pulsado | esfera | Selector de esferas |
| Botón lateral (pulsación corta) | siempre | Apaga (o pasa al AOD) / enciende, como antes |

- **Cambios sin guardar:** una página puede marcarse (`ui_nav_set_dirty`). Atrás y salir al cajón no la descartan a la
  primera: aviso "Cambios sin guardar · Repite el gesto para descartar". Ir a la esfera nunca pierde nada.
- **Apps suspendidas:** reciben los eventos `ui_nav_event_suspend()` / `ui_nav_event_resume()`. Flex Compass y Sensores
  sueltan el IMU al suspenderse y lo vuelven a pedir al retomarse (no gastan con la app oculta). Abrir otra app desde el
  cajón cierra la suspendida.
- **Apagado de pantalla:** ya no cierra la app abierta; la suspende (antes `ui_nav_home` la borraba).
- Las páginas abiertas sin gestos de borde (lienzo de Paint, prueba de pantalla, instalación OTA) siguen sin ellos,
  también sin la franja del panel: necesitan los bordes.
- Las franjas inferior y laterales no tapan controles: los teclados terminan en y = 452 y los botones a 52 px del
  borde; el verificador visual de la vista previa lo comprueba en todas las pantallas.

## 3. Cajón de aplicaciones en panal

- Iconos **circulares** de 92 px **sin nombre**, en filas alternas de 3 y 4 huecos desplazadas medio paso (rejilla
  hexagonal: paso 100 px, filas a 87 px). Cada fila se centra sin salirse de la rejilla: 13 apps = 3, 4, 3, 2, 1.
- Son los iconos ORIGINALES de `drawAppIcon` (Flex OS Ultra), regenerados con base circular
  (`tools/icons/gen_app_icons.c`, modo `round`; `gen_icons.sh`). Sin máscaras ni escalado en el reloj: RGB565A8 tal cual,
  24,8 KB cada uno (antes 31,7 KB).
- Punto de estado en Reloj con el cronómetro o el temporizador en marcha (como antes).
- **Sensores sale del cajón**; su página, el servicio del IMU y su controlador siguen: se abre desde Device Care >
  Pruebas > Sensores, que ya la usaba.
- `ui_apps_find(drawer, nombre)` localiza el icono de una app (accesibilidad y pruebas).

## 4. Esferas y selector (`ui_face.c`, `ui_face_editor.c`)

| Esfera | Contenido |
|---|---|
| Digital | Hora grande y fecha sobre el fondo elegido (la de siempre, **sin la batería bajo la hora**) |
| Analógica | 12 marcas, agujas y segundero con líneas de LVGL; fecha abajo |
| Modular | Hora arriba y 4 complicaciones de vidrio: fecha, batería, próxima alarma (las alarmas reales) y clima (la caché real de `watch_weather`, "--" / "Sin datos" si no hay) |
| Liquid Glass | La hora dentro de una gran tarjeta de vidrio con el tinte adaptativo del tema |
| Parallax | Halo, tarjeta y hora en capas que se desplazan distinto con la inclinación **real** del acelerómetro. El IMU solo mide con la esfera a la vista y la pantalla encendida |

- **Selector:** mantener pulsada la esfera o Ajustes > Pantalla > Esfera (el mismo). Las vistas previas son las
  esferas reales renderizadas una vez con `lv_snapshot` y reducidas a 205 × 251 (RGB565, ~100 KB cada una en PSRAM),
  generadas de una en una (una cada 30 ms) para no bloquear; desplazamiento horizontal de una en una.
- Tocar una la aplica, la guarda en NVS (clave `face`, que ya existía y no se usaba) y la esfera se rehace; se
  recupera tras reiniciar. Desde Ajustes, al aplicar se vuelve directamente a la esfera.
- `LV_USE_SNAPSHOT=1` se fuerza en `CMakeLists.txt` (y en `sdkconfig.defaults`): un `sdkconfig` ya generado en tu PC
  no lo tenía y `sdkconfig.defaults` solo cuenta al generar uno nuevo.
- **Pixel Shift** en las esferas: ±3 px, un paso por cambio de minuto (aprovecha ese redibujo: en reposo sigue sin
  redibujarse nada, lo comprueba la vista previa).

## 5. Panel rápido

- Se abre desde la franja superior de la esfera (como antes) **y desde el borde superior de cualquier página**; se
  cierra arrastrando hacia arriba (también desde el asa) o tocando el asa; la página de debajo queda igual.
- Mosaicos (todos leen y cambian el estado real de su servicio): Wi-Fi (`watch_net`), Bluetooth (`watch_phone`),
  Batería (abre Ajustes > Batería), Ajustes, **Ahorro** (Flex Ultra Power Saving) y **AOD** (no disponible con el
  ahorro activo), y el brillo físico.
- Corregido de paso: arrastrar desde el asa pasaba por encima del control de brillo y lo cambiaba (el asa no tenía
  `PRESS_LOCK`). Lo encontró la vista previa.

## 6. AOD, protección AMOLED, Flex Ultra Power Saving y Device Care

Todo vive en servicios que funcionan con Device Care cerrada (`ui_screen`, `ui_aod`, `ui_psave`, ajustes en NVS);
Device Care y el panel rápido solo los configuran.

**Atenuación progresiva** (Ajustes > Pantalla y Device Care: desactivada, suave 1/2, media 1/3 —la de antes—, fuerte
15 %): el brillo **físico** del panel (DCS 0x51 con `watch_display_set_level`, no la opacidad de LVGL) baja de forma
lineal durante los últimos segundos (`scr_dim_ramp`).

**AOD** (desactivado por defecto): al agotarse el tiempo, en vez de apagar el panel, capa negra opaca con la hora
atenuada (digital, o agujas sin segundero si la esfera es la analógica), la fecha y el % de batería; brillo físico al
8 %; una actualización por minuto y sin animaciones; Pixel Shift de ±8 px cada minuto con un patrón de 16 posiciones
repartido alrededor del centro. Un toque o el botón lo encienden. **Elevación de muñeca: no** — el controlador del
QMI8658 no usa sus interrupciones de movimiento (sin GPIO verificado) y medir sin parar gastaría más que el AOD.

**Pixel Shift** (activado por defecto) en AOD y esferas. Reduce el riesgo de marcas en el AMOLED; **no** lo elimina y
el reloj no puede detectar el quemado (lo dicen Ajustes y Device Care).

**Flex Ultra Power Saving** (`ui_psave.c`, un único modo):

- Al activarlo guarda en NVS el estado real anterior (brillo del panel, AOD, Wi-Fi, Bluetooth), baja el brillo a 30 %
  como máximo, desactiva el AOD sin tocar su preferencia, usa una esfera mínima en negro (sin fondo, sin IMU, sin
  complicaciones), acorta las transiciones a la mitad y el apagado a 10 s como máximo, y aplica la política de radios
  (Device Care: conservar, apagar Wi-Fi —por defecto: Clima y actualizaciones dejan de consultar— o apagar ambas).
- Al desactivarlo restaura lo guardado (brillo, AOD y cada radio que estaba encendida), sin reiniciar ni bloquear:
  las radios se encienden en sus tareas.
- Sobrevive a un reinicio. Sin cambio de frecuencia de la CPU: `esp_pm` no está habilitado y cambiarlo sin medir en el
  reloj es arriesgado (queda para F8.4).

**Device Care** (la existente, ampliada): secciones "Flex Ultra Power Saving" (ahorro y política de radios),
"Protección de pantalla" (AOD, Pixel Shift, atenuación, con la advertencia) y "Conectividad" (estado real del Wi-Fi y
del Bluetooth), además de la salud, las métricas y las pruebas que ya tenía. Sin FPS ni CPU.

## 7. Estabilidad

### 7.1 Wi-Fi: investigación del fallo al activarlo

**No hay reloj ni registro del fallo en este entorno:** no se ha podido reproducir. Se auditó `watch_net.c` línea a
línea y se encontraron causas reales de reinicio y de estado incorrecto, todas en la ruta de "activar el Wi-Fi":

| # | Evidencia en el código | Consecuencia | Corrección |
|---|---|---|---|
| W1 | `wifi_init_once()` llamaba a `esp_netif_create_default_wifi_sta()` en **cada** intento si el anterior no había terminado. En ESP-IDF 5.5 esa función hace `assert(netif)` y la interfaz `WIFI_STA_DEF` ya existe → `esp_netif_new` devuelve NULL | Si un primer intento fallaba más adelante (p. ej. `esp_wifi_init` sin RAM interna con el BLE encendido y los búferes de pantalla), **el segundo toque del interruptor reiniciaba el reloj** (abort) | Cada paso no repetible se hace una sola vez y se recuerda (`s_netif_ready`, `s_sta_netif`, `s_handlers_ready`); si falla `esp_wifi_set_mode`, `esp_wifi_deinit` para reintentar limpio |
| W2 | Pila de la tarea de 4 KB con `persist()`/`load()` poniendo ~1,6 KB de búferes en ella y `scan_done()` otros ~0,6 KB, mientras `esp_wifi_init/start` (calibración del PHY con NVS) y `nvs_set_blob` corren en la misma tarea | Margen de pila muy escaso al encender → desbordamiento posible | Búferes estáticos (solo los usa la tarea) y pila de 6 KB |
| W3 | Sin comprobación de RAM interna antes de `esp_wifi_init` (16 búferes RX estáticos en RAM interna) | Fallos a medias difíciles de diagnosticar | Se registra la RAM interna libre y su bloque mayor antes y después; por debajo de 48 KB no se intenta y la interfaz muestra el fallo |
| W4 | Eventos que llegan tras apagar (`STA_DISCONNECTED`, `GOT_IP`, `SCAN_DONE`, `STA_START`) se procesaban igual | "Falló"/"Conectado" con el Wi-Fi apagado; un `GOT_IP` tardío arrancaba SNTP con la radio parada | Se ignoran con el Wi-Fi apagado |
| W5 | `esp_wifi_start()` sin comprobar | Interfaz en "Buscando" para siempre si fallaba | Error registrado y estado "falló" con el interruptor apagado |

El interruptor no se ha deshabilitado ni se ha retirado el Wi-Fi de la interfaz. Lo que falta, en el reloj (T124-T128):
ciclos de encender/apagar, conectar, señal débil, Wi-Fi + BLE a la vez, y copiar las líneas `watch_net` (ahora incluyen
la RAM interna).

### 7.2 Splash

El logotipo se ve **al menos 3 s** desde que aparece. La interfaz se crea debajo cuando los servicios críticos han
arrancado (como antes); solo se espera el tiempo que falte hasta 3 s y después el fundido de siempre. Si el arranque
tarda más de 3 s, no se añade ninguna espera.

### 7.3 Interfaz limpia

- Medidor de FPS/CPU fuera de la interfaz normal: `LV_USE_PERF_MONITOR=0` forzado en `CMakeLists.txt` (un `sdkconfig`
  antiguo lo seguía activando) y retirado de `sdkconfig.defaults`. Para medir (T9/T42), a propósito:
  `idf.py -DFLEX_PERF_MONITOR=1 build` con `CONFIG_LV_USE_PERF_MONITOR=y`.
- Batería bajo la hora: retirada (está en el panel rápido y en la esfera modular).

### 7.4 Memoria (compilación real)

| | Antes | Ahora |
|---|---|---|
| DIRAM estática | 167 843 B | 170 051 B (+2,2 KB: búferes de `watch_net` fuera de la pila) |
| Flash (código + datos) | 2 478 204 B | 2 385 508 B (iconos de 92 px) |
| Imagen | 0x282710 | 58 % libre en la partición de 6 MB |

Vistas previas del selector: 5 × 100 KB en PSRAM solo con el selector abierto, más 411 KB temporales por captura
(`CONFIG_SPIRAM_USE_MALLOC`: reservas > 16 KB van a PSRAM).

## 8. Flex Compass: rumbo relativo

El reloj no tiene magnetómetro y no se añade. `imu_heading.c` (lógica pura, en `watch_imu`) integra en **cada muestra**
del IMU (56 Hz, en su tarea; la interfaz solo copia instantáneas) el giro alrededor de la vertical: proyección del
giroscopio sobre la gravedad, así que vale también con el reloj inclinado.

- Sesgo del giroscopio estimado con el reloj quieto (giro < 2 °/s y 1 g ± 60 mg durante 1 s); quieto no se integra.
- Incertidumbre mostrada siempre ("± N°"): crece con el tiempo en movimiento (0,03 °/s con sesgo estimado, 0,3 °/s
  sin él) y un 2 % de lo girado; en ámbar a partir de 20°.
- Sin gravedad clara (caída, golpe) o con muestras perdidas (> 100 ms) no se integra.
- "Fijar referencia" pone 0° hacia donde apunta el reloj; al abrir la app también. La aguja señala la referencia.
- La app dice que es **relativo** y que **no** es el Norte magnético ni el geográfico; "Heading" en datos técnicos
  pone "Relativo (sin Norte)". Se conservan la pantalla de requisito de la referencia (`AppCompass.h`) y los datos
  reales. El IMU sigue con recuento de usuarios (como el Flex IMU Service de la referencia).

## 9. Pruebas

**En el PC (hechas):**

- `make -C tests/host`: 3019 comprobaciones, 0 fallos (ASan + UBSan). Nuevas: AOD y rampa (`scr_next_ex`,
  `scr_dim_ramp`), Pixel Shift (límites, ciclo, reparto, pasos distintos), ajustes nuevos (valores por defecto,
  normalización, `ws_dim_level`) y rumbo relativo (sesgo, 90° plano e inclinado, vuelta por 360°, caída libre, huecos,
  incertidumbre).
- Vista previa (`tools/sim`, LVGL 9.5): `TODO OK (0 fallos)`, y con ASan + UBSan con las semillas 2, 9 y 13 de la
  prueba "mono". Baterías actualizadas a los gestos nuevos (nav, panel, apps, pantalla, energía, esfera, mono, ota,
  teléfono) y batería nueva `esferas` (5 estilos, selector con vistas previas, aplicar y guardar, desde Ajustes,
  modular, Parallax con IMU, esfera mínima del ahorro). La prueba "mono" (600 acciones) incluye los cuatro bordes, el
  cajón, el selector y comprueba las páginas suspendidas; sin fugas.
- Firmware: `idf.py build` con ESP-IDF 5.5.5 (componentes del registro sustituidos por copias de las mismas versiones,
  `tools/offline/build_offline.sh`): sin errores ni avisos.

**En el reloj (pendientes):**

| Prueba | Qué hacer | Qué esperar |
|---|---|---|
| T124 | Wi-Fi: encender y apagar 10 veces seguidas desde el panel y desde Ajustes | Sin reinicios; en el registro, `iniciando Wi-Fi: RAM interna libre …` y `Wi-Fi iniciado` una vez |
| T125 | Wi-Fi con el Bluetooth (Flex Phone) encendido: encender, conectar, apagar | Sin reinicios; anotar la RAM interna de las líneas `watch_net` |
| T126 | Conectar a tu red, apagar el router, encenderlo | Reintentos con cuenta atrás y reconexión sola |
| T127 | Contraseña mala; red con señal débil (lejos del router) | "Contraseña incorrecta" sin reintentos; con señal débil, reintentos sin bloqueos de la interfaz |
| T128 | Abrir el Clima y el navegador con Wi-Fi y BLE a la vez | Sin congelaciones de la interfaz |
| T129 | Arranque | El logotipo "Flex OS" se ve unos 3 s y la esfera entra con fundido; sin medidor de FPS abajo |
| T130 | Esfera: deslizar hacia abajo | Baja el cajón en panal; iconos circulares nítidos, sin nombres, centrados |
| T131 | En una app: borde derecho → izquierda; borde izquierdo → derecha; borde inferior → arriba; borde superior → abajo | Atrás; al cajón; a la esfera (reabrirla desde el cajón la retoma igual); panel rápido encima |
| T132 | Listas y teclados cerca de los bordes (Ajustes, Notas, contraseña Wi-Fi) | Desplazar y escribir no dispara gestos por error |
| T133 | Mantener pulsada la esfera, recorrer y elegir cada esfera; reiniciar | Vistas previas reales; la elegida se conserva tras reiniciar |
| T134 | Ajustes > Pantalla > Esfera: elegir una | Vuelve directamente a la esfera con la nueva |
| T135 | Esfera Parallax: inclinar el reloj | Las capas se mueven con suavidad; la batería no cae más deprisa con la esfera tapada |
| T136 | Atenuación (Ajustes > Pantalla): desactivada, suave, media, fuerte | El brillo baja poco a poco los últimos 3 s (con 15 s de apagado) |
| T137 | AOD activado: esperar el apagado; tocar | Negro con la hora gris y brillo bajo; se mueve unos píxeles cada minuto; un toque enciende. Medir consumo si puedes |
| T138 | Flex Ultra Power Saving desde el panel con Wi-Fi y BT encendidos; salir | Brillo ≤ 30 %, esfera mínima, Wi-Fi apagado (política por defecto), AOD no disponible; al salir vuelven el brillo, el Wi-Fi y el BT |
| T139 | Ahorro activo y reiniciar | Sigue activo tras reiniciar; al salir restaura lo de antes del reinicio |
| T140 | Device Care: secciones nuevas; cerrar la app | Los ajustes siguen funcionando con la app cerrada |
| T141 | Flex Compass: girar el reloj 90° sobre la mesa y volver; "Fijar referencia" | ~90° y vuelta a ~0°; la incertidumbre crece al moverse y no en reposo; nunca habla de Norte |

## 10. Archivos

- Nuevos: `components/watch_ui/ui_psave.c`, `ui_aod.c`, `ui_face_editor.c`; `components/watch_imu/imu_heading.c`,
  `include/imu_heading.h`; `tools/sim/sim_faces.c`; este documento.
- Ampliados: `ui_nav.c` (gestos, suspensión, cambios sin guardar), `ui_apps.c` (panal), `ui_face.c` (esferas),
  `ui_qp.c`, `ui_screen.c` + `ui_screen_logic.c` (AOD, rampa, Pixel Shift), `ui_devcare.c`, `ui_display_page.c`,
  `ui_compass.c`, `ui_sensors.c`, `ui_banner.c` (aviso informativo), `ui_system.c`, `watch_ui.c` (splash),
  `watch_settings` (claves `aod`, `pshift`, `dim`, `psave`, `ps_radio`, `ps_bright`, `ps_flags`; el esquema no cambia:
  si faltan, valores por defecto), `watch_imu.c`, `watch_net.c`, `watch_display.c`, `CMakeLists.txt`,
  `sdkconfig.defaults`, iconos (`assets/icons`, `tools/icons`).
