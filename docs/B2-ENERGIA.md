# B2 — Batería y energía

B2 avanza en bloques pequeños. **Ningún bloque escribe registros del AXP2101.**

| Bloque | Contenido | Estado |
|---|---|---|
| **B2-1** | Auditoría de las lecturas frente al datasheet, decodificación corregida, configuración del cargador en solo lectura, histéresis de fin de carga y contador de eventos de carga | implementado · compilado · 168/168 en PC · **prueba física pendiente** |
| B2-2 | Decidir, con los registros reales, si se activa el ADC de VBAT y se desactiva el pin TS como hace el código oficial (decisión D1 de `ROADMAP.md`) | pendiente de T14 |
| B2-3 | Indicador de batería del tema: icono vectorial con 7 estados (normal, bajo, crítico, cargando, completa, externa, sin datos/sin batería) | implementado · compilado · pruebas en PC · prueba física pendiente |
| B2-4 | Animación de carga (anillo hasta el % real, ~2,3 s, se cierra con un toque) disparada una vez por cada inicio de carga confirmado que venga de un estado conocido; `ui_system` lee el servicio una sola vez para todas las pantallas | implementado · compilado · pruebas en PC y de ciclo de vida en la vista previa · prueba física pendiente |
| B2-5 | Avisos de batería baja o crítica: un aviso por episodio y clase, solo descargando, enfriamiento de 10 min; umbrales provisionales aislados en `battery_thresholds.h` | implementado · compilado · pruebas en PC y de ciclo de vida · prueba física pendiente |
| **B2-6** | Respuesta rápida al enchufar y desenchufar: sondeo ligero, confirmación con tiempo mínimo y notificación por eventos a la interfaz (corrige "tarda en reaccionar" y "no se ve hasta tocar") | implementado · compilado · pruebas en PC y en la vista previa · prueba física pendiente (T41) |

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

## B2-3 a B2-5. Indicador, animación y avisos

**Indicador** (`watch_theme/flex_battery_icon`):
- Es un único objeto LVGL dibujado con primitivas vectoriales: contorno, polo, relleno proporcional y glifo.
- Solo se invalida cuando cambia el tipo de vista o el porcentaje.

| Estado del servicio | Vista |
|---|---|
| descargando, nivel normal | relleno blanco |
| descargando, nivel bajo | relleno ámbar (`warn`) |
| descargando, nivel crítico | relleno y contorno rojos |
| cargando | relleno verde + rayo |
| carga completa | relleno verde + marca |
| externa sin carga | relleno azul claro + rayo atenuado |
| sin datos | solo contorno gris, **nunca un relleno inventado** |
| sin batería | contorno tachado |

**D3 (resuelta):** en "carga completa" se muestra el porcentaje real del gauge, no un 100 % forzado.

**Animación de inicio de carga** (`watch_ui/ui_charge_anim`):
- Es un anillo que sube hasta el % real. Entra en 220 ms, llena el anillo en 900 ms y sale a los 2 s (unos 2,3 s en total). Un toque la cierra.
- Se dispara **una vez** por cada `charge_events` nuevo, y solo si el estado previo confirmado era conocido (descarga, externa o completa). Nunca al arrancar ya cargando, ni tras un error de lectura.
- Vive en `lv_layer_top` y se borra al terminar.

**Avisos** (`watch_power/battery_alerts` + `watch_ui/ui_banner`):
- **Batería baja** al entrar en nivel bajo; **crítica** al entrar en nivel crítico.
- Solo descargando. Se rearman al volver a nivel normal (≥ 18 %), con 10 min de enfriamiento por clase.
- Tarjeta de vidrio superior: entra en 220 ms, se ve 4,2 s y sale en 180 ms (tiempos de la referencia).
- **Diferencia justificada:** el suelo de tinte del aviso es 235, no 150. La referencia usa 150 sobre un fondo desenfocado; sin desenfoque, la esfera se transparentaba (comprobado en la vista previa).

**Umbrales:** `battery_thresholds.h` con `BATT_THRESHOLDS_CALIBRATED 0`:

| Nivel | Entra | Sale |
|---|---|---|
| Bajo | 15 % | 18 % |
| Crítico | 5 % | 8 % |

Siguen siendo provisionales (D4) hasta medir una descarga real.

**`ui_system`:** un único temporizador (500 ms) lee el servicio y comparte la copia entre las pantallas y las capas globales.

### Comprobado en el PC (B2-3 a B2-5)

| Comprobación | Resultado |
|---|---|
| Pruebas de PC | **363/363**: estado → vista, colores, glifos, relleno monótono, compuerta de la animación, avisos (secuencias, USB, rearme, enfriamiento, vuelta del reloj de 32 bits) |
| Vista previa con LVGL 9.5 real (`tools/sim`) | El aviso bajo y el crítico aparecen y desaparecen solos. La animación aparece, se borra sola, no deja animaciones ni objetos en `lv_layer_top` y no se repite sin un evento nuevo |
| Compilación ESP-IDF 5.5.5 | OK, 0 avisos; DIRAM 86 775 B (+248 B desde B2-1); app 826 272 B; particiones idénticas |

### Pruebas en el reloj (pendientes)

| # | Prueba | Cómo | Esperado |
|---|---|---|---|
| T19 | Indicador | mirar la barra de estado sin USB, con USB cargando y con USB al 100 % | El dibujo coincide con la tabla de vistas y con el texto de la tarjeta |
| T20 | Animación de carga | conectar el USB con la batería no llena; desconectar y volver a conectar; reiniciar con el USB conectado | Una animación por conexión (≤ 4 s tras conectar). **Ninguna** al arrancar ya conectado. Se cierra con un toque |
| T21 | Aviso de batería baja | usar el reloj sin USB hasta ≤ 15 % | Un solo aviso ámbar; no se repite mientras siga bajo; no aparece cargando |
| T22 | Aviso crítico (opcional) | seguir hasta ≤ 5 % | Un aviso rojo. **Precaución:** el PMIC puede apagar el reloj al vaciarse; no es necesario agotarla |

## B2-6. Respuesta rápida de carga

### Qué se observó en el reloj (2026-10-15)

En el vídeo de la prueba y según el usuario:
- al enchufar, el reloj tardaba en reaccionar;
- a veces el estado no aparecía hasta tocar la pantalla.

### Causa: latencias en cadena

| Paso | Latencia |
|---|---|
| Muestreo del servicio | cada 2 s |
| Confirmación | 2 muestras: 2 s más |
| `ui_system` | consultaba cada 0,5 s |
| Cada pantalla | consultaba `ui_system` cada 1 s |

En el peor caso eran unos 6 s. Si en ese intervalo se tocaba la pantalla, parecía que el toque provocaba la actualización.

### Solución (sin escribir en el AXP2101)

- **Sondeo ligero** (`power_sched.c`, puro):
  - cada 0,5 s se lee solo el registro 00h (VBUS y batería presentes);
  - la muestra completa pasa a ser cada 3 s y a 5 lecturas, porque el 00h, el identificador y los registros 18h y 30h ya no se leen en cada muestra.
  - En reposo el tráfico I2C **baja** de ~4,5 a ~3,7 lecturas por segundo (prueba de PC).
- **Modo rápido:** si el sondeo ve un cambio o hay un estado sin confirmar, hay muestras completas cada 200 ms durante 3 s.
- **Confirmación por tiempo además de por muestras** (`batt_hold_ms`):

  | Transición | Tiempo mínimo |
  |---|---|
  | Enchufar con carga, desenchufar | 150 ms |
  | Descarga a "externa sin carga" | 1,2 s, para dar tiempo a que arranque el cargador y no mostrar un estado intermedio |
  | Salir de "cargando" con el USB conectado | 4 s: la misma histéresis de B2-1 contra el parpadeo al final de la carga |
  | "Completa", "sin batería" y "sin datos" | 1 s |

  Con muestras cada 2 s, el comportamiento es idéntico al de antes: las pruebas anteriores pasan sin cambios.
- **Notificación:**
  - el servicio incrementa un contador atómico (`watch_power_generation`) cuando cambia algo visible;
  - `ui_system` lo mira cada 100 ms sin bloquear;
  - solo cuando cambia copia la instantánea y avisa **en el mismo ciclo** a las pantallas suscritas (`ui_power_listen`): esfera, panel rápido, Ajustes y Batería.
  - Ninguna pantalla sondea ya la batería por su cuenta.
- **Pila de la tarea:** de 3072 a 4096 B. El reloj mostró 656 B libres como mínimo; el umbral de aviso es 512 B.

### Comprobado en el PC

| Prueba | Resultado |
|---|---|
| Enchufar con carga inmediata | "Cargando" confirmado en ≤ 0,9 s |
| Cargador que tarda 0,8 s en arrancar | Confirmado ≤ 0,9 s después, sin pasar por "externa" |
| Batería llena | "Externa sin carga" |
| Fin de carga oscilando cada 0,6 s durante 6 s con muestras cada 200 ms | Sigue "cargando"; pasa a "externa" solo tras 4 s estables |
| Tráfico I2C en reposo | ≤ 4,5 lecturas/s |
| Vista previa, sin ningún toque | Un cambio llega a la esfera en 60 ms y la animación de carga empieza en ≤ 110 ms tras el evento |

### Prueba en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T41 | Respuesta al enchufar | con la esfera visible, conectar y desconectar el USB 3 veces **sin tocar la pantalla** | "Cargando" y la animación en ~1 s tras conectar; "Descargando" en ~1 s tras desconectar; nunca hace falta tocar la pantalla. En el registro serie: `transicion CONFIRMADA a 'cargando'` |
