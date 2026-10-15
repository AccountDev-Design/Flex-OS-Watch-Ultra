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
| **B3-4** | Panel rápido (solo controles con función real) y raíz de Ajustes | implementado · compilado · pruebas en PC y en la vista previa · prueba física pendiente |
| **B3-5** | Ajustes: Pantalla, Batería, Diagnóstico (con la prueba de pantalla y táctil), Acerca de | implementado · compilado · pruebas en PC y en la vista previa · prueba física pendiente |
| **B3-6** | Integración: vista previa completa con ASan/UBSan, prueba "mono" de 600 acciones reproducibles con invariantes, superposiciones durante la navegación | hecho (pruebas de PC) |

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

## B3-4. Panel rápido y raíz de Ajustes

### Criterios de aceptación

| # | Criterio | Cómo se comprueba |
|---|---|---|
| P1 | Se abre arrastrando hacia abajo desde la franja superior de la esfera (64 px, marcada con un asa); la cortina **sigue al dedo** y al soltar se completa o vuelve | vista previa: posición durante el arrastre; arrastre corto cancelado |
| P2 | Un toque o un arrastre horizontal en la franja no abren nada | vista previa |
| P3 | Solo controles con función real | vista previa: los mosaicos pendientes no hacen nada |
| P4 | Brillo real: mínimo 10 %, como mucho un comando al panel cada 60 ms al arrastrar y siempre el valor final; nunca se reenvía el mismo valor; sin control de brillo dice "No disponible" | `tests/host` y vista previa |
| P5 | Retorno fiable a la esfera: arrastrar hacia arriba en el panel o tocar el asa inferior; desde Ajustes, "volver" | vista previa |
| P6 | Ajustes: cierra el panel (190 ms) y después abre la página (210 ms) | vista previa |
| P7 | Al cerrarse no queda nada: ni objetos en `lv_layer_top`, ni animaciones, ni temporizadores, ni memoria | 20 ciclos abrir/cerrar con arrastre y asa: mismos bloques y temporizadores |

**Contenido de P3:**

| Control | Comportamiento |
|---|---|
| Brillo | Real |
| Batería | % y estado reales, con el mismo indicador que la esfera |
| Ajustes | Abre la página |
| Wi-Fi y Bluetooth | "Pendiente" (F5), apagados y sin toque |

### Decisiones

- **Velo opaco.** La referencia deja pasar algo del fondo porque detrás hay un fondo desenfocado. Aquí no se desenfoca (coste).
  - Con 245/255 la esfera se leía como un fantasma a través de los controles (vista previa, +8 niveles sobre el negro).
  - Opaco no hay fantasma y LVGL deja de dibujar la esfera bajo el panel abierto.
- **Brillo.**
  - El valor inicial es el que tiene la pantalla (70 % al arrancar). No se guarda hasta F4.1 (NVS): tras reiniciar vuelve al 70 %.
  - La interfaz recibe el control de brillo por `watch_ui_boot_info_t` (`brightness_get/set`), así que la vista previa lo sustituye y F4.1 podrá añadir la persistencia sin tocar la interfaz.
  - El 10 % mínimo evita una pantalla que parezca apagada a plena luz sin forma visible de recuperarla.
- **Raíz de Ajustes.** Diagnóstico ya funciona; Pantalla, Batería y Acerca de llegan en B3-5 y hasta entonces están marcadas "Pendiente".

### Comprobado en el PC

- **`tests/host`:** 541 comprobaciones, 0 fallos (+14 del brillo: límites, ritmo de envío, valor final y vuelta del reloj).
- **`tools/sim`:** panel rápido y Ajustes: 0 fallos.
  - 181 → 181 bloques de LVGL y los mismos temporizadores tras 20 ciclos.
  - Vistas previas: `docs/img/f3_panel_arrastre.png`, `f3_panel.png` y `f3_ajustes.png`.
- **Firmware completo:** sin avisos.
  - App: 871 520 B.
  - DIRAM: 87 247 B.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T33 | Abrir el panel | arrastrar hacia abajo desde el borde superior de la esfera, despacio y luego rápido | El panel sigue al dedo; al soltar se abre del todo o vuelve; un toque arriba no lo abre |
| T34 | Brillo | arrastrar el control de brillo de un extremo a otro | El brillo cambia con suavidad mientras se arrastra; no baja del 10 %; el registro serie no muestra errores |
| T35 | Pendientes | tocar Wi-Fi y Bluetooth | No hacen nada; se ven apagados con "Pendiente" |
| T36 | Cerrar y Ajustes | cerrar arrastrando hacia arriba y con el asa; abrir Ajustes > Diagnóstico y volver dos veces | Siempre se vuelve a la esfera; sin pantallas congeladas |

## B3-5. Páginas de Ajustes

### Criterios de aceptación

| # | Criterio | Cómo se comprueba |
|---|---|---|
| A1 | Ajustes: cuatro filas con función; los valores (brillo, % de batería, versión) son reales y se refrescan cada segundo, solo si cambian | vista previa: el brillo cambiado en Pantalla aparece al volver |
| A2 | Pantalla: el mismo control de brillo real del panel rápido (componente compartido), aviso honesto de que aún no se guarda y "Apagado" marcado "Pendiente" (F4) | vista previa |
| A3 | Batería: estado real; umbrales leídos de `battery_thresholds.h` y marcados **provisionales** mientras no estén calibrados (D4); configuración del cargador en **solo lectura**; sin datos, "--" | vista previa con datos y sin ellos |
| A4 | Diagnóstico: memoria, búferes de LVGL, flash (declarada, JEDEC, sonda > 16 MB), RTC (estado, última lectura, registros) y servicio de batería (lecturas, errores, tiempo de lectura I2C, pila mínima) | vista previa |
| A5 | Prueba de pantalla y táctil (T2, T3 de B1): marco de 1 px y coordenadas. Se abre **sin** el gesto del borde para poder tocar el borde izquierdo | vista previa: un toque en x = 2 da coordenadas y no navega |
| A6 | Acerca de: versión del firmware, ESP-IDF y LVGL; PSRAM y flash medidas en el arranque | vista previa |
| A7 | Todas las etiquetas de todas las pantallas se pueden dibujar con su fuente | recorrido automático de las etiquetas reales (esfera, panel, cada página) |
| A8 | Las listas se desplazan arrastrando, sin navegar | vista previa: desplazamiento > 150 px y misma página |
| A9 | Sin fugas | cada página 5 veces: mismos bloques, temporizadores y 0 animaciones |

### Hallazgos de la vista previa (corregidos)

- **Listas que no se desplazaban.** Sobre filas de información (no pulsables), LVGL no encontraba un objeto pulsable desde el que desplazar, porque la lista tampoco lo era.
  - Ahora la lista es pulsable.
  - La prueba exige el desplazamiento; antes solo comprobaba que no se navegara.
- **Títulos partidos en dos líneas.** En LVGL 9, los "..." solo actúan con la altura fija: los títulos de fila tienen ahora la altura de una línea.
- **Glifos que faltaban.** "≤" y "≥" no están en las fuentes Outfit generadas (Latin-1, guiones y "…"); el texto de los umbrales se escribe sin ellos. La comprobación automática A7 evita que vuelva a pasar.

### Comprobado en el PC

- **`tests/host`:** 557 comprobaciones, 0 fallos (+16 de formatos).
  - "4,08 V" con coma decimal.
  - mA y KB/MB.
  - "--" para lo no disponible.
  - Estado del RTC.
- **`tools/sim`:** todas las baterías, 0 fallos.
  - Ajustes: 182 → 182 bloques de LVGL y los mismos temporizadores.
  - Vistas previas: `docs/img/f3_ajustes_raiz.png`, `f3_pantalla.png`, `f3_bateria.png`, `f3_bateria_cargador.png`, `f3_diagnostico.png`, `f3_prueba_pantalla.png` y `f3_acerca_de.png`.
  - Las cifras de memoria y de búferes de esas vistas son de ejemplo del simulador, no medidas.
- **Firmware completo:** sin avisos.
  - App: 876 160 B.
  - DIRAM: 87 159 B.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T37 | Ajustes | panel rápido > Ajustes | Cuatro filas con brillo, % de batería y versión reales; ninguna "Pendiente" |
| T38 | Batería | Ajustes > Batería; desplazar hasta el final | Nivel, tensión y USB coinciden con el registro serie de `watch_power`; la configuración del cargador coincide con la línea `config` del registro; los umbrales dicen "provisionales" |
| T39 | Diagnóstico | Ajustes > Diagnóstico | RAM y PSRAM libres coinciden con el registro serie; el estado del RTC coincide con T26 |
| T40 | Prueba de pantalla (= T2 y T3 de B1) | Diagnóstico > Prueba de pantalla; mirar los bordes y tocar las 4 esquinas y el centro | Marco azul de 1 px en los 4 bordes; coordenadas cerca de (0,0), (409,0), (0,501), (409,501) y (205,251) |

## B3-6. Integración

### Qué se comprueba

- **Vista previa entera con ASan y UBSan** (`make -C tools/sim SAN=1`), incluido LVGL: sin usos tras liberar, desbordes, comportamiento indefinido ni fugas al salir.
- **Prueba "mono"** (`sim_monkey.c`): 600 acciones pseudoaleatorias con semilla fija; con `SIM_SEED` se prueba otra.
  - **Acciones:**
    - toques en cualquier punto;
    - gestos desde el borde y desde arriba;
    - arrastres hacia arriba;
    - "volver";
    - aperturas de páginas sin esperar a que termine la animación anterior;
    - cambios de batería que disparan la animación de carga y los avisos;
    - cambios del estado de la hora.
  - **Invariantes, cada 10 acciones en reposo:**
    - navegación quieta;
    - una sola página visible y en su sitio;
    - profundidad entre 1 y 6;
    - panel rápido cerrado o abierto del todo y solo sobre la esfera;
    - sin animaciones vivas salvo los avisos;
    - todos los textos con glifo.
  - **Al terminar:** los mismos bloques de memoria y temporizadores que al empezar.
- **Superposiciones**, de forma determinista:
  - el aviso de batería baja aparece encima del panel rápido abierto, se cierra con un toque y el panel sigue funcionando;
  - la animación de carga aparece encima de Ajustes > Batería, se cierra con un toque y la página sigue debajo con el estado nuevo.
  - El aviso bajo respeta su enfriamiento de 10 minutos: la prueba tuvo que dejarlo pasar para que volviera a sonar.

### Resultados

| Prueba | Resultado |
|---|---|
| `tests/host` (ASan + UBSan) | 557 comprobaciones, 0 fallos |
| `tools/sim`, todas las baterías | 0 fallos |
| `tools/sim` con ASan + UBSan | 0 fallos, sin informes de los sanitizadores |
| Prueba mono, 30 semillas × 600 acciones (18 000 acciones) | 0 fallos. En cada semilla, entre 10 y 36 de 61 comprobaciones en una página (profundidad máxima 6) y entre 4 y 19 con el panel abierto |
| Prueba mono con ASan + UBSan, 5 semillas | 0 fallos, sin informes |

Vistas previas: `docs/img/f3_aviso_sobre_panel.png` y `f3_carga_sobre_pagina.png`.

**Límite de estas pruebas:**

- Comprueban la lógica y el ciclo de vida de la interfaz con LVGL real, pero **no** el reloj: ni la fluidez (FPS), ni el consumo, ni el táctil físico, ni los tiempos reales del bus I2C. Nada de eso se ha medido.
- Las pruebas T14-T40 lo verificarán.
