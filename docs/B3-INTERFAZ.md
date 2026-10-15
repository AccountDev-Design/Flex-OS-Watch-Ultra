# B3 — Interfaz, tema y navegación (F3)

B3 avanza en bloques pequeños. Cada bloque compila el firmware completo, pasa
las pruebas de PC (`tests/host`) y las de la vista previa (`tools/sim`), pero
**nada de esto está verificado en el reloj** hasta que pasen las pruebas
físicas de la tabla final.

| Bloque | Contenido | Estado |
|---|---|---|
| **B3-1** | Identidad consolidada: 9 iconos vectoriales nuevos, estados de los controles de vidrio (pulsado, activo, no disponible), cabecera estándar, lista y fila de Ajustes | implementado · compilado · pruebas en la vista previa · prueba física pendiente |
| B3-2 | Servicio de hora en solo lectura (RTC PCF85063) y esfera del reloj | pendiente |
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
