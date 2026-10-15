# B3 — Interfaz, tema y navegación (F3)

B3 avanza en bloques pequeños. Cada bloque compila el firmware completo, pasa
las pruebas de PC (`tests/host`) y las de la vista previa (`tools/sim`), pero
**nada de esto está verificado en el reloj** hasta que pasen las pruebas
físicas de la tabla final.

| Bloque | Contenido | Estado |
|---|---|---|
| **B3-1** | Identidad consolidada: 9 iconos vectoriales nuevos, estados de los controles de vidrio (pulsado, activo, no disponible), cabecera estándar, lista y fila de Ajustes | implementado · compilado · pruebas en la vista previa · prueba física pendiente |
| **B3-2** | Servicio de hora en solo lectura (RTC PCF85063) y esfera del reloj | implementado · compilado · pruebas en PC y en la vista previa · prueba física pendiente |
| B3-3 | Navegación: pila de pantallas, transiciones 210/190 ms, volver, gestos | pendiente |
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
