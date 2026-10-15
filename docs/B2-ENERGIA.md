# B2 — Batería y energía

B2 avanza en bloques pequeños. **Ningún bloque escribe registros del AXP2101.**

| Bloque | Contenido | Estado |
|---|---|---|
| **B2-1** | Auditoría de las lecturas frente al datasheet, decodificación corregida, configuración del cargador en solo lectura, histéresis de fin de carga y contador de eventos de carga | implementado · compilado · 168/168 en PC · **prueba física pendiente** |
| B2-2 | Decidir, con los registros reales, si se activa el ADC de VBAT y se desactiva el pin TS como hace el código oficial (decisión D1 de `ROADMAP.md`) | pendiente de T14 |
| B2-3 | Indicador de batería del tema (estados, colores, pantalla de batería) | pendiente |
| B2-4 | Animación de carga disparada por `charge_events` | pendiente |
| B2-5 | Avisos de batería baja o crítica | pendiente |

## B2-1. Auditoría frente al datasheet del AXP2101

Fuente: datasheet X-Powers del AXP2101, apartado 6.13.2. Se contrastó cada bit leído por el firmware de B1.

| Registro | B1 (según XPowersLib) | Datasheet | Acción |
|---|---|---|---|
| REG01 bit3 | "0 = USB conectado" (`isVbusIn`) | **VINDPM status**: la entrada está en gestión dinámica por caída de tensión | **Corregido.** USB presente = REG00 bit5 (VBUS good). VINDPM se informa aparte. Antes, un USB débil o muy cargado se habría mostrado como "Sin USB" |
| REG01 [7:5] | sentido de la corriente (3 bits) | [6:5]; bit7 reservado | Corregido (máscara de 2 bits; el valor 11 se trata como "otro") |
| REG34 | VBAT[12:8] (5 bits) | VBAT[13:8] (6 bits); [7:6] son de depuración | Alineado. Por debajo de 8,19 V el resultado es el mismo |
| REG30 / REG18 | — | Por defecto: ADC de VBAT = 1, ADC TS = 1, gauge = 1, cargador = 1 | Se informan como diagnóstico |
| Lectura del ADC | byte alto, bajo y alto otra vez | "leer primero los 6 bits altos" | Correcto, sin cambios |

**Configuración del cargador, solo lectura:**

| Registro | Contenido |
|---|---|
| 0x15 | VINDPM |
| 0x16 | límite de entrada |
| 0x18 | cargador, gauge, pila de respaldo |
| 0x30 | canales del ADC |
| 0x50 | función del pin TS (bit4 = 0: **el pin TS afecta al cargador**) |
| 0x61 | precarga |
| 0x62 | corriente de carga |
| 0x63 | terminación |
| 0x64 | tensión de carga |
| 0x68 | detección de batería |

- Se leen al arrancar y cada 60 s, y se registran **solo cuando cambian**.
- **Por qué importa:** el AXP2101 conserva estos registros mientras tenga batería. Si antes corrió el firmware de fábrica o el ejemplo oficial (que fija 400 mA, 4,2 V y desactiva el canal TS), esos valores siguen ahí aunque nuestro firmware no los toque. Hasta leerlos no sabemos con qué parámetros carga tu reloj.

**Estados y transiciones:**
- **Final de la tensión constante:** la corriente cae hacia cero y el sentido puede oscilar entre carga y reposo. Para no parpadear entre "Cargando" y "Externa sin carga", ese paso exige **3 muestras seguidas** (6 s). Los demás cambios siguen necesitando 2.
- **Al desenchufar:** la falta de VBUS es inequívoca, así que basta con 2 muestras.
- **`charge_events`:** cuenta cada inicio de carga confirmado. La animación de B2-4 lo usará para no perder ni repetir eventos aunque la interfaz lea la instantánea con retraso.
- **Sin cambios:** el filtrado del porcentaje (mediana de 3, no sube en descarga, se vuelve a tomar de la lectura actual al cambiar de fase) y los umbrales provisionales de batería baja (15 %, sale con 18 %) y crítica (5 %, sale con 8 %).

**Recursos:**
- Cada lectura mide su duración (`lectura I2C ... us`) y el margen mínimo de pila de la tarea (`pila libre minima`).
- Si el margen baja de 512 B, aparece un aviso en el registro.

## Comprobado en el PC

| Comprobación | Resultado |
|---|---|
| Pruebas de lógica pura (`make -C tests/host`, ASan/UBSan) | **168/168**: las 87 de B1 (2 corregidas según el datasheet) + 81 nuevas (VINDPM, bits reservados, VBAT de 14 bits, tablas de 0x15/0x16/0x61-0x64/0x50, transiciones con parpadeo, eventos de carga) |
| Compilación ESP-IDF 5.5.5 | **OK**, 0 avisos |
| RAM interna estática | +132 B (DIRAM de 86 395 a 86 527 B) |
| Tamaño de la app | 821 600 B (+2,9 KB) |
| Pila de la tarea (`-fstack-usage`) | Marco de `power_task` = 384 B. Pico estimado ~2,3 KB de 3 072 B; **margen real pendiente de medir en el reloj** |
| Tabla de particiones, `ota_data`, `flash_args` | idénticos a B1 (32 MB y los mismos offsets) |
| Interfaz | Las 4 vistas previas son idénticas byte a byte a las de B1 (no hay cambio visual) |
| Escrituras al AXP2101 | ninguna: solo `i2c_master_transmit_receive` con el puntero de registro |

## Pruebas en el reloj (pendientes)

Flashea con `idf.py -p PUERTO flash monitor` desde `Flex OS Ultra Watch/` y copia el registro serie de cada prueba.

| # | Prueba | Cómo | Esperado |
|---|---|---|---|
| T14 | Configuración real del cargador | arrancar y copiar las 3 líneas `AXP2101 config regs ...`, `cargador=...` y `pin TS: ...` | Valores decodificados. Interesa sobre todo `pin TS: AFECTA al cargador` con `canal ADC TS=1` (el código oficial avisa de carga anómala en ese caso), y el valor de `ICC` |
| T15 | Presencia de USB | conectar y desconectar el USB | `vbus=1` o `vbus=0`, coherente con el cable. La interfaz dice "USB conectado" o "Sin USB". Si aparece `vindpm=1`, anotar el cargador usado |
| T16 | Eventos de carga | sin USB → conectar → desconectar → conectar | Cada conexión con carga da **una** línea `transicion CONFIRMADA a 'cargando' (evento N)` con N = 1, 2... (sin duplicados) |
| T17 | Final de carga | dejarlo cargando hasta el final | El estado **no** alterna entre "Cargando" y "Externa sin carga". Termina en "Carga completa" (`etapa=4`) |
| T18 | Recursos | registro tras 30 min | `lectura I2C` con máximo < 5000 us; `pila libre minima` ≥ 512 B; ningún aviso de pila; sin reinicios |

Las pruebas T4-T8 de B1 (`B1-ARRANQUE.md` §5) siguen vigentes.
