# B3 — Interfaz, tema y navegación (F3)

B3 avanza en bloques pequeños. Cada bloque compila el firmware completo, pasa
las pruebas de PC (`tests/host`) y las de la vista previa (`tools/sim`), pero
**nada de esto está verificado en el reloj** hasta que pasen las pruebas
físicas de la tabla final.

| Bloque | Contenido | Estado |
|---|---|---|
| **B3-1** | Identidad consolidada: 9 iconos vectoriales nuevos, estados de los controles de vidrio (pulsado, activo, no disponible), cabecera estándar, lista y fila de Ajustes | implementado · compilado · pruebas en la vista previa · prueba física pendiente |
| **B3-2** | Servicio de hora en solo lectura (RTC PCF85063) y esfera del reloj | implementado · compilado · pruebas en PC y en la vista previa · prueba física pendiente |
| **B3-3** | Navegación: pila de páginas, transiciones 210/190 ms, volver (botón y gesto desde el borde), arranque con splash | implementado · compilado · pruebas en PC y en la vista previa · prueba física tras B3-5 |
| B3-4 | Panel rápido (solo controles con función real) | pendiente |
| B3-5 | Ajustes: Pantalla, Batería, Diagnóstico, Acerca de | pendiente |
| B3-6 | Pruebas de integración en la vista previa (gestos, fugas, animaciones) | pendiente |

## B3-1. Identidad: iconos y componentes

### Criterios de aceptación

| # | Criterio | Cómo se comprueba |
|---|---|---|
| A1 | Iconos de las pantallas de F3 (engranaje, sol, Wi-Fi, Bluetooth, chevrons, info, pulso, reloj) dibujados con primitivas, sin mapas de bits, a partir de las recetas de la referencia | código + vista previa `f3_iconos` |
| A2 | Cada icono, a 20, 32 y 64 px: dibuja algo, ocupa al menos media caja, está centrado (±20 %) y no pinta fuera de su caja | `sim_theme.c`, comprobado píxel a píxel |
| A3 | Los iconos valen sobre cualquier fondo (sin "recortes" pintados con el color del fondo) | el engranaje usa un anillo en vez del hueco de `qpIcoBg` |
| A4 | Controles pulsables: se aclaran mientras el dedo está encima, el estado activo usa el acento y el no disponible se ve apagado y no recibe toques | vista previa `f3_fila_pulsada` + dedo simulado |
| A5 | Cabecera estándar: zona "volver" de 64×64 px con el chevron centrado; el título nunca la pisa | toque simulado en el borde de la zona |
| A6 | Filas de Ajustes de 72 px: icono de acento, título, valor y chevron; una fila pendiente dice "Pendiente" y no entrega toques | vista previa `f3_componentes` + toque simulado |
| A7 | Sin desenfoque ni animaciones por toque; las filas son un solo rectángulo (sin capa de brillo) | código |
| A8 | Crear y borrar la pantalla de componentes no deja memoria | 20 ciclos con el asignador de LVGL del simulador |

### Decisiones de diseño

- **Medidas.** La referencia (P4, ~217 ppi) usa zonas táctiles de 56 px y filas de 66 px. El reloj tiene ~315 ppi. Por eso:
  - zona táctil mínima: 64 px (`FLEX_TOUCH_MIN`);
  - filas: 72 px, para que quepan 5 bajo la cabecera de 76 px.
  - Los radios se conservan en píxeles: fila 16 y tarjeta 26.
- **Pulsado.** La referencia responde al toque con un destello blanco animado (`Widgets.h:593`). Aquí el tinte se aclara mientras el dedo está encima, sin animación: no añade trabajo por cuadro.
- **Fondo de las pantallas de lista.**
  - Color plano de página (`FLEX_C_PAGE`, como `PAGE_BG` en Ajustes de la referencia).
  - Las filas usan vidrio plano con el tinte adaptativo calculado una sola vez sobre ese color.
  - Al desplazar la lista no hay que mezclar el fondo Onyx en cada cuadro.
- **Acentos de Ajustes.** Se usan los mismos colores de la referencia (`SET_ACCENT`):

  | Categoría | Color |
  |---|---|
  | Pantalla | ámbar |
  | Batería | verde |
  | Red | azul claro |
  | Sistema, Acerca de | azul |

### Comprobado en el PC

- **Firmware completo (`tools/offline/build_offline.sh build`):** sin avisos de compilación en `watch_theme` ni en `watch_ui`.
  - App: 827 872 B (B2-5: 826 272 B).
  - DIRAM: 86 831 B (+56 B de estilos).
  - Particiones sin cambios.
- **`tests/host`:** 363 comprobaciones, 0 fallos (B3-1 no añade lógica pura).
- **`tools/sim`:** iconos (12 × 3 tamaños), componentes y fugas: 0 fallos.
  - Memoria de LVGL: 10 664 B antes y después de 20 ciclos.
  - Vistas previas: `docs/img/f3_iconos.png`, `f3_componentes.png` y `f3_fila_pulsada.png`.

### Pruebas en el reloj (pendientes)

Los componentes aparecerán en pantalla con B3-4 y B3-5. Estas pruebas se harán entonces:

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T23 | Iconos y textos legibles | abrir Ajustes y el panel rápido | Iconos nítidos, sin cortes en los bordes; textos sin solaparse con el chevron ni con la zona "volver" |
| T24 | Respuesta al toque | mantener el dedo sobre una fila y sobre la zona "volver" | Se aclaran mientras se pulsa; al soltar, abren o vuelven una sola vez |
| T25 | Filas "Pendiente" | tocar Wi-Fi y Bluetooth | No hacen nada y se ven apagadas |

## B3-2. Hora (RTC PCF85063, solo lectura) y esfera

### Auditoría previa

- **BSP oficial:** no tiene soporte de RTC; los `0x51` que aparecen en él son el comando de brillo del panel.
- **Auditoría de la Fase 0:** el PCF85063 está en 0x51 del bus I2C compartido, con INT en GPIO39. Su reserva de alimentación sale del AXP2101; no se sabe si hay pila o supercondensador.
- **Referencia (`FlexOS_Ultra_Clock.h`):** guarda la hora como una época UTC con la zona fija de Lima (UTC-5) y, sin hora, usa una **semilla de fábrica inventada** (4-7-2026 13:23). Aquí **no** se copia la semilla: sin hora fiable se dice que no hay hora.
- **Driver:** propio y mínimo, en lugar de SensorLib, para no añadir dependencias. Solo hace falta leer 11 registros.

### Servicio `watch_time`

- **Lectura:** una transacción I2C lee los registros 00h-0Ah. El chip congela los contadores durante la lectura, así que la hora no se lee a medias.
  - Se lee al arrancar y cada 10 min; si falla, se reintenta cada 30 s.
  - Entre lecturas la hora avanza con `esp_timer`: la interfaz nunca espera al bus.
- **Validez:** la hora se da por **no configurada** cuando:
  - el bit OS indica que el oscilador se paró;
  - el bit STOP está activo;
  - hay una cifra BCD o un campo fuera de rango (incluido el 31 de noviembre o el 29 de febrero de un año no bisiesto);
  - la fecha es anterior a 2025.
  - El día de la semana del chip (asignable por el usuario) no se usa: se calcula de la fecha.
- **Solo lectura:** no escribe el RTC ni el reloj del sistema. Poner en hora, SNTP y zona horaria son F4.2.
- **Zona horaria (D17):** provisionalmente, la hora del RTC se muestra como hora local.
- **Registro serie:** fecha y hora leídas, motivo si no es fiable, y los 11 registros en crudo al arrancar o cuando cambia el estado.

### Comprobado en el PC (servicio)

- **`tests/host`:** 459 comprobaciones, 0 fallos (+96).
  - Calendario de ±8000 años y días de la semana conocidos.
  - Cambio de día y de año; segundos antes de 1970.
  - Textos en español con tildes y recortes seguros.
  - PCF85063: OS, STOP, modo 12 h (12 AM/PM), 14 campos imposibles, 29 de febrero, bordes y fechas anteriores a 2025.
- **Firmware completo:** sin avisos. App: 830 816 B.

### Pruebas en el reloj (pendientes)

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T26 | Lectura del RTC | arrancar con el monitor serie | Una línea `watch_time: RTC aaaa-mm-dd hh:mm:ss (ok)`, o `RTC sin hora fiable: …` más los registros en crudo. Cualquiera de las dos es válida; **envía el registro** |
| T27 | Deriva | si T26 dio una hora válida, compararla con un reloj de referencia al arrancar y 15 min después | Diferencia ≤ 2 s entre ambas comparaciones |

### Esfera

#### Criterios de aceptación

| # | Criterio | Cómo se comprueba |
|---|---|---|
| E1 | Hora y fecha reales del servicio: "9:41" (24 h) y "Jueves, 15 de octubre" | `tests/host` y vista previa |
| E2 | Batería real (caché de `ui_system`): indicador, % y estado | vista previa |
| E3 | Sin hora fiable: "--:--" más el motivo ("Leyendo la hora…", "Hora sin configurar" o "Reloj RTC no disponible"); sin datos de batería, "--" y "Sin datos". Nunca un valor inventado | `tests/host` y vista previa, los 3 estados |
| E4 | Eficiente: temporizador de 1 s ligado a la pantalla. Solo reescribe la hora al cambiar el minuto, la fecha al cambiar el día y la batería al cambiar su estado | la vista previa no envía **nada** a la pantalla durante 50 s del mismo minuto |
| E5 | Minuto y día avanzan, incluida la medianoche | vista previa: 9:41 → 9:42; 23:59:58 → 0:00 del viernes 16 |
| E6 | Crear y borrar la esfera no deja temporizadores ni memoria | 20 ciclos: mismos temporizadores y mismos bloques de LVGL |
| E7 | Geometría del panel real de 410×502 | vista previa |

#### Cambios en la interfaz

- **Pantalla de inicio:** ahora es la esfera.
- **Diagnóstico de B1:** batería con VBAT y USB, flash, memoria, coordenadas del táctil y el marco de 1 px de la prueba T2.
  - Pasa a una pantalla de diagnóstico (`ui_diag.c`) con su temporizador ligado a la pantalla.
  - Será accesible desde Ajustes en B3-5. Hasta entonces, T2 y T3 no se pueden repetir con este firmware.

#### Comprobado en el PC (esfera)

- **`tests/host`:** 482 comprobaciones, 0 fallos (+23 de los textos de la esfera).
- **`tools/sim`:** esfera con hora válida, reposo, avance de minuto y de día, tres estados sin hora, batería sin datos y ciclo de vida: 0 fallos.
  - Memoria: 164 → 164 bloques de LVGL.
  - Vistas previas: `docs/img/f3_esfera.png` y `f3_esfera_sin_hora.png`.
- **Firmware completo:** sin avisos.
  - App: 862 240 B. Crece unos 31 KB porque la esfera usa ya la fuente Outfit de 28 px.
  - DIRAM: 86 831 B.

#### Pruebas en el reloj (pendientes)

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T28 | Esfera con hora | arrancar | La hora y la fecha coinciden con la línea `watch_time: RTC …` de T26, el día de la semana es correcto y la batería coincide con el registro de `watch_power`. Si T26 dio "sin hora fiable", se ve "--:--" y "Hora sin configurar" |
| T29 | Cambio de minuto | mirar la esfera al cambiar el minuto | Cambia como mucho ~2 s después que el reloj de referencia, sin parpadeos del resto de la pantalla |

## B3-3. Navegación y arranque

### Criterios de aceptación

| # | Criterio | Cómo se comprueba |
|---|---|---|
| N1 | Pila de páginas con la esfera como raíz; abrir en 210 ms y volver en 190 ms, ease-out cúbico (curva `appTrP` de la referencia) y paralaje del 30 % en la página de debajo | vista previa: a los 180 ms sigue animando y a los 260 ms está en reposo; posición intermedia con paralaje |
| N2 | Volver con el botón de la cabecera y con un arrastre desde el borde izquierdo que **sigue al dedo** y, al soltar, se completa o se cancela | `tests/host` (reconocedor) y vista previa con el dedo simulado |
| N3 | Los gestos no interfieren: un toque sigue siendo un toque, un arrastre vertical desde el borde no navega y el gesto solo se reconoce en una franja de 20 px bajo la cabecera (no tapa el botón volver) | `tests/host` y vista previa |
| N4 | Sin callbacks duplicados: abrir durante una animación se ignora (un doble toque no abre dos páginas); volver durante una apertura la termina en el acto y vuelve | vista previa |
| N5 | Sin fugas ni animaciones huérfanas: en reposo, 0 animaciones, una sola página visible (las de debajo, ocultas: LVGL no las dibuja) y los temporizadores de cada página se borran con ella | vista previa: 20 ciclos abrir/volver con botón y gesto, mismos bloques y temporizadores |
| N6 | Arranque sin pantallas congeladas: pantalla por defecto en negro antes de encender el brillo, splash animado mientras arrancan los servicios y paso a la esfera sin esperas artificiales | vista previa: el splash se anima a los 300 ms y la esfera entra en ≤ 240 ms |
| N7 | Interfaces para F4 sin implementarlo: `ui_nav_push/back/home`, `ui_page_new` y el reconocedor `ui_gesture` reutilizable para la cortina del panel rápido | código |

### Decisiones

- **Transición: deslizar con paralaje** (la de Ajustes en la referencia) en lugar del zoom desde el icono de las apps de la referencia.
  - El zoom de una página entera exige en LVGL una capa con transformación a pantalla completa (≈ 800 KB y un remuestreo por cuadro).
  - Deslizar solo mueve las dos páginas.
- **Páginas dentro de una sola pantalla de LVGL.** Una sola animación mueve las dos páginas con la curva propia; la carga de pantallas de LVGL es lineal.
  - Las animaciones van ligadas al estado de la navegación y no a la pantalla, porque `lv_screen_load()` borra las animaciones de la pantalla que carga.
  - La vista previa encontró ese fallo en el splash, que se quedaba sin animación; está corregido.
- **Umbrales del gesto** (provisionales hasta probarlos en la muñeca):
  - margen de 12 px antes de decidir;
  - se completa al 35 % del recorrido o con 700 px/s (con al menos un 8 %);
  - se cancela si el dedo vuelve a 300 px/s;
  - la animación final dura lo proporcional a lo que falta, con un mínimo de 60 ms (`ATR_MIN_MS` de la referencia).
- **Franja del gesto.** Mide 20 px y tapa los 4 px izquierdos de las filas de las listas: un toque en esos 4 px no abre la fila.
- **Arranque.**
  - El splash es el de la referencia ("Flex OS" que aparece sobre negro en 600 ms) sin su pausa fija de 2 s.
  - Solo cambia la opacidad del texto, lo que redibuja un área pequeña.
  - La pantalla por defecto de LVGL (tema claro, blanca) se pone en negro antes de encender el brillo.

### Comprobado en el PC

- **`tests/host`:** 527 comprobaciones, 0 fallos (+45 del reconocedor).
  - Toque, margen, eje, sentido y diagonal.
  - Límites del recorrido.
  - Completar por recorrido o por lanzamiento.
  - Cancelar por recorrido corto o al volver deprisa.
  - Velocidad a 10 y 33 ms entre muestras, y con el reloj dando la vuelta.
  - Duración restante y curva.
- **`tools/sim`:** arranque, esfera y navegación: 0 fallos.
  - Navegación: 165 → 165 bloques de LVGL y los mismos temporizadores tras 20 ciclos.
  - Vistas previas: `docs/img/f3_splash.png`, `f3_nav_apertura.png` y `f3_nav_gesto.png`.
- **Firmware completo:** sin avisos.
  - App: 863 056 B.
  - DIRAM: 86 967 B.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T30 | Arranque | encender o reiniciar mirando la pantalla | Sin destello blanco ni basura. "Flex OS" aparece sobre negro y la esfera entra en cuanto arranca (sin pantalla congelada) |
| T31 | Abrir y volver (tras B3-5) | abrir Ajustes y una categoría; volver con el botón | Transiciones suaves de ~0,2 s, sin parpadeos; la página de debajo se mueve menos (paralaje) |
| T32 | Gesto volver (tras B3-5) | arrastrar desde el borde izquierdo: corto y lento, largo, y rápido | Corto y lento vuelve a su sitio; largo o rápido vuelve a la página anterior. Un desplazamiento vertical de la lista no navega |
