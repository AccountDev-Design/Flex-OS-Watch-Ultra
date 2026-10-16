# F9 — Apps de Flex OS Ultra en el reloj

Inventario completo y estado: [INVENTARIO-APPS.md](INVENTARIO-APPS.md). Aquí está el detalle de las apps migradas.
La app Reloj (F9.1) está en [F3-IDENTIDAD-ORIGINAL.md](F3-IDENTIDAD-ORIGINAL.md) §3.

Todas aparecen en el lanzador con su icono original y solo cuando hacen algo real.

| Tarea | App | Estado |
|---|---|---|
| F9.1 | Reloj | 🟡 falta T69 |
| F9.2 | Calculadora | 🟡 falta T74 |
| F9.3 | Calendario | 🟡 falta T75 |
| F9.4 | Flex Compass | 🟡 falta T76 |
| F9.5 | Device Care | 🟡 falta T77 |

## Calculadora (F9.2)

**Lógica:** `components/watch_ui/apps_logic.c` transcribe `calcKey()`, `calcLine()`, `calcCompute()` y `calcFmt()`
(`FlexOS_Ultra_AppsBasic.h:30-151`) con el mismo estado (acumulador + operador pendiente + entrada).

- La línea muestra la operación tal como se escribe ("25 + 10").
- "2 + 3 + 4" da 9.
- Cambiar de operador no pierde el acumulador.
- 5/0 da "Error" (no un 0 falso), y con "Error" los operadores se ignoran.
- Los números tienen como mucho 16 cifras y se muestran con `%g` (6 cifras significativas), nunca "-0".

**Diseño** (`calcRender`, AppsBasic.h:220-265):

- Rejilla 5×4 a todo el ancho. La columna de operadores y "=" conservan su naranja de marca `rgb565(245,150,40)`; el
  resto son superficies.
- El display baja la letra (40 → 28 → 22) si la línea no cabe.
- La operación se conserva al salir y volver (en RAM hasta reiniciar; la referencia la guarda en un archivo de sesión).

**Adaptación:** el display ocupa la franja de la cabecera, a la derecha de "volver". Así las teclas miden 83×64 px
(la zona táctil mínima del reloj es 64) y la fila inferior queda dentro de las esquinas redondeadas.

![Calculadora](img/f9_calculadora.png)

## Calendario (F9.3)

`calRender()` (AppsBasic.h:351-402):

- título "mes año" en minúscula ("octubre 2026", `MO_FULL`);
- fila "D L M M J V S" (la semana empieza en domingo);
- el día de hoy en un círculo del acento con el número en blanco; se repinta al cambiar el día.

Añadido en el reloj (la referencia solo enseña el mes actual):

- cambiar de mes con los botones o deslizando arriba y abajo;
- tocar el título vuelve al mes actual;
- sin hora fiable no se marca ningún día y una nota lo dice.

La lógica (`cal_month`, `cal_shift`, `cal_cell`) se prueba en el PC con meses de 4, 5 y 6 filas, bisiestos y paso de
año.

![Calendario](img/f9_calendario.png)

## Flex Compass (F9.4)

La referencia **no enseña un rumbo cuando no hay rumbo**: sin su IMU de 9 ejes (BNO085) muestra la pantalla de
requisito con el estado real del sensor (`AppCompass.h:683-709`). En "Datos técnicos" pone "No disponible", nunca un
cero (`AppCompass.h:834-870`).

El QMI8658 del reloj tiene acelerómetro y giroscopio pero **no magnetómetro**. La app muestra:

- el requisito, con los textos de la referencia adaptados;
- el estado real de cada sensor (el magnetómetro, "No presente");
- "Heading: No disponible";
- cabeceo y alabeo reales del acelerómetro.

El IMU solo mide con la página abierta (recuento de usuarios: no apaga el sensor a otra página).

⚠️ **Limitación de hardware:** no hay brújula. Una brújula real necesitaría un magnetómetro externo por I²C.

![Flex Compass](img/f9_compass.png)

## Device Care (F9.5)

Es la pantalla "Salud de Flex OS" de la referencia (`DeviceCare.h:760-930`):

- **Puntuación 0-100 (`dcScoreFrom`, transcrita en `dc_score`):** sale solo de métricas medidas.
  - Una métrica con porcentaje vale su porcentaje, ×0,70 en aviso y ×0,35 en crítico.
  - Una sin porcentaje vale 100, 62 o 25.
  - Sin nada medido no se enseña número.
- **Anillo y palabra:** el anillo usa el color del nivel. La palabra es "Todo correcto" (≥ 80), "Necesita atención"
  (≥ 55) o "Estado crítico".
- **Filas con semáforo (`dcRowLed`)** y acceso a las pruebas: pantalla y táctil, sensores y diagnóstico completo.

| Métrica | Cómo se mide en el reloj | Umbrales |
|---|---|---|
| Memoria | RAM interna libre (`heap_caps`) | < 40 KB crítico, < 64 KB aviso (los de `FlexOS_Mem.h:117-118`) |
| PSRAM | libre y bloque mayor | el bloque mayor debe admitir el fondo (402 KB): < 402 KB o < 512 KB libres crítico; < 1 MB aviso |
| Estabilidad | motivo del último arranque (`esp_reset_reason`) y tiempo encendido | panic, watchdog o caída de tensión = aviso |
| Servicios | ajustes en NVS, hora fiable, LittleFS montado, IMU, AXP2101 | todos bien OK; falta uno aviso; más, crítico |
| Errores | subsistemas con fallos de bus I²C o de almacenamiento | 0 OK, 1 aviso, más crítico |
| Almacenamiento | LittleFS ocupado | ≥ 95 % crítico, ≥ 85 % aviso |
| Batería | nivel del AXP2101 | baja = aviso, crítica = crítico |

No se inventan las métricas del P4 sin equivalente aquí: el ritmo del bucle de Arduino, el contador persistente de
reinicios y la detección de caídas con BNO085.

![Device Care](img/f9_devcare.png)

## Pruebas

### En el PC

- `tests/host`: 1062 comprobaciones, entre ellas la calculadora, el calendario y la fórmula y los umbrales de Device
  Care (ASan + UBSan).
- Vista previa:
  - **Calculadora:** "25 + 10" en la línea, "35", "5/0" da "Error", y la operación se conserva al volver.
  - **Calendario:** octubre de 2026 con el 15 resaltado; pasa a noviembre sin marcar hoy, y el título vuelve a hoy.
  - **Flex Compass:** requisito y "No presente"; Heading "No disponible"; estado real; el IMU se apaga al salir.
  - **Device Care:** puntuación calculada, batería real y abre el diagnóstico.
  - Integridad visual de todas las páginas, prueba "mono" con las páginas nuevas y ASan/UBSan.

### En el reloj (pendientes)

| # | Prueba | Resultado esperado |
|---|---|---|
| T74 | Calculadora: "25 + 10 =", "5 / 0 =", salir y volver | 35; "Error" en rojo; se conserva la operación; teclas fáciles de acertar |
| T75 | Calendario: abrir, cambiar de mes con los botones y deslizando | Mes actual con hoy resaltado; el título vuelve a hoy |
| T76 | Flex Compass: inclinar el reloj | Pitch y Roll cambian; Heading "No disponible"; al salir el registro no muestra el IMU midiendo |
| T77 | Device Care | Puntuación y semáforos coherentes con Ajustes > Diagnóstico; "Estabilidad" con el tiempo encendido |
