# F4 — Servicios del sistema

Estado de cada bloque de F4 (pesos de `ROADMAP.md`). Las referencias son la auditoría de F4: cómo resuelve cada servicio el Flex OS Ultra de referencia y qué hardware tiene esta placa.

| Bloque | Contenido | Estado |
|---|---|---|
| F4.1 | Ajustes persistentes en NVS, escritor único | implementado · pruebas de PC · prueba física pendiente (T46, T47) |
| F4.2 | Poner en hora el RTC, zona horaria, 12/24 h | implementado · pruebas de PC · prueba física pendiente (T48-T51) |
| F4.3 | Cronómetro, temporizador, alarmas y lanzador de apps | implementado · pruebas de PC · prueba física pendiente (T52-T55) |
| F4.4 | IMU QMI8658 y app de sensores | pendiente |
| F4.5 | Apagado de pantalla por inactividad | pendiente |
| F4.6 | LittleFS en `storage` | pendiente |

## F4.1. Ajustes persistentes

**Componente `watch_settings`.**
- Los ajustes viven en RAM.
  - Quien los lee recibe una copia (`watch_settings_get`).
  - Quien los cambia entrega la copia modificada (`watch_settings_update`). El cambio vale al momento.
- Solo la tarea `watch_settings` (prioridad 2) escribe en NVS:
  - Guarda 1,5 s después del último cambio y nunca más de 30 s después del primero, como la referencia (`AppFramework.h:1346`).
  - Solo escribe las claves que cambiaron.
  - Si falla, reintenta a los 10 s y lo cuenta en Diagnóstico.
- `watch_settings_flush()` guarda en el momento. Se usará antes de apagar la pantalla (F4.5).

**Lógica pura con pruebas de PC (`ws_logic.c`).**
- Valores por defecto y normalización: cada valor fuera de rango pasa al más cercano válido.
- El instante de guardado.
- Bloques binarios con magia, versión, longitud y CRC-32. Los usarán las alarmas; un bloque dañado nunca se usa.

**Ajustes guardados** (espacio `flexws` de la partición `nvs`; `partitions.csv` no cambia):

| Clave | Valor | Por defecto | Rango |
|---|---|---|---|
| `ver` | versión del esquema | 1 | — |
| `bright` | brillo | 70 % | 10-100 % |
| `scr_to` | apagado de pantalla (F4.5) | 15 s | 5, 10, 15, 30, 60 s |
| `h24` | formato de 24 h (F4.2, D18) | sí | — |
| `tz_min` | zona horaria (F4.2, D17) | −300 (Lima, UTC−5) | UTC−12 a UTC+14, en pasos de 15 min |
| `wall` | fondo | negro | negro, Onyx |
| `face` | estilo de esfera | 0 | 0-2 |
| `alarms` | bloque de alarmas (F4.3) | — | 256 B como máximo |

**En la interfaz.**
- **Brillo:** el control del panel rápido y de Ajustes > Pantalla guarda el valor al soltarlo. Arrastrarlo no escribe en cada paso; la vista previa lo comprueba: una sola escritura.
- **Arranque:** se leen los ajustes antes de iniciar la pantalla. Se aplican el brillo guardado y el fondo.
- **Ajustes > Pantalla > Fondo:** cambia entre negro AMOLED y Onyx, se guarda y la esfera se rehace al momento.
  - Si el toque llega en mitad de una animación de páginas, la esfera se rehace al terminar la animación. La prueba "mono" encontró ese caso: la esfera se quedaba con el fondo anterior.
- **Ajustes > Diagnóstico > Ajustes guardados:**
  - si se leyeron o son los valores por defecto;
  - si hay cambios pendientes;
  - las escrituras y los errores.
- **Sin NVS:** la página Pantalla dice que los cambios valen hasta reiniciar.

**Recuperación.** Si la partición `nvs` está llena o es de otra versión de NVS, se borra **solo esa partición** y se reinicia. Es la recuperación estándar de ESP-IDF y queda en el registro. No se toca ninguna otra partición ni se hace `erase-flash`.

**Iconos nuevos** (16, vectoriales, probados a 20, 32 y 64 px): fondo, esfera, zona horaria, alarma, temporizador, cronómetro, aplicaciones, sensores, apagado, almacenamiento y controles (más, menos, reproducir, pausa, detener, vuelta).

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T46 | Los ajustes sobreviven al reinicio | Pon el brillo en ~30 % y el fondo en Onyx; espera 3 s; pulsa RESET (o desconecta y vuelve a encender) | Arranca con el brillo ~30 % y el fondo Onyx. En el registro: `ajustes leidos (esquema 1): brillo 30 %` y antes `ajustes guardados en N ms` |
| T47 | Diagnóstico de NVS | Ajustes > Diagnóstico > Ajustes guardados | "leídos", escrituras ≥ 1 tras un cambio, errores 0. Anota cuántos ms tarda en guardar (registro) y si la pantalla da un tirón en ese momento |

## F4.2. Hora: poner en hora, zona horaria y 12/24 h

**Decisiones cerradas:**
- **D17:** el RTC guarda **UTC**. La zona es un desfase fijo en minutos, de UTC-12 a UTC+14, guardado en los ajustes. Por defecto es Lima, UTC-5, sin horario de verano, como la referencia (`Clock.h:46-56`). Cambiar la zona nunca escribe el RTC.
- **D18:** 24 h por defecto. Con 12 h:
  - la esfera grande muestra "9:41" y un "PM" pequeño al lado;
  - el panel rápido muestra "9:41 PM" (referencia, `Clock.h:179-196`). Si con eso no cabe la fecha, la oculta.

**Puesta en hora** (`watch_time`, en su propia tarea, la única que habla con el RTC):
1. Lee Control_1 y deja en el registro serie los registros de antes.
2. Pone STOP conservando CIE y CAP_SEL de Control_1. Nunca usa el patrón de reinicio 58h.
3. Escribe en **una** transacción 03h-0Ah: la marca `0x5A` en RAM_byte y la hora, con OS = 0. La hora es la pedida más lo que ha pasado desde la petición.
4. Quita STOP.
5. Relee y exige lo escrito, la marca y Control_1 correcto.
6. 1,1 s después comprueba que el reloj **avanza**.
7. Solo entonces da la hora por buena: ajusta la fase del segundo y el reloj del sistema (`settimeofday`, en UTC).

**Si algo falla:**
- **Error de I2C o de verificación:** el RTC queda detenido. Tras un reinicio se verá "sin configurar", nunca una hora mal escrita. La hora pedida sigue en RAM y se reintenta cada 30 s.
- **El reloj no avanza** (cristal parado): no se reintenta. Se sigue con la hora en RAM y Diagnóstico lo dice.

**Hora válida:** solo cuando el RTC lleva la marca de Flex OS. La hora del demo de fábrica (escrita en una zona desconocida) o un RTC con OS = 1 cuentan como **sin configurar**. Por eso, al flashear esta versión, el reloj pide poner en hora una vez.

**Codificación pura con pruebas de PC.**
- `pcf85063_encode`/`_utc` y `pcf85063_ctrl1_for_set`, en `rtc_pcf85063.c`.
- Casos E1-E9 de la auditoría:
  - un día entero, segundo a segundo, en 24 h y 12 h;
  - todos los días de 2000 a 2099, ida y vuelta con el decodificador.
- Zona (`wt_utc_to_local`/`wt_local_to_utc`, todos los desfases de 15 min) y formatos de 12 h.

**Interfaz** (Ajustes > Fecha y hora):
- **Poner en hora:** rodillos de hora, minuto, día, mes y año, en hora local.
  - Los días se ajustan al mes: febrero de 2027 tiene 28.
  - Al guardar se ve el resultado real del servicio ("Hora guardada y comprobada", "El RTC no responde (reintenta)", "El RTC no avanza"), con "Reintentar" si falla.
  - Si el reloj no tiene hora, los rodillos empiezan en la fecha del firmware.
- **Formato 24 h:** sí o no.
- **Zona horaria:** lista de 38 desfases con ciudades de ejemplo. La zona actual sale marcada y visible al abrir.
- **Por Internet:** "Pendiente" (SNTP con Wi-Fi, F5).
- **Fecha, estado del RTC y última puesta en hora.**
- **Diagnóstico:** contenido del RTC ("UTC de Flex OS" o "sin marca", más la zona) y puestas en hora con sus errores.

**Vista previa:** una batería nueva ("fecha y hora F4.2") recorre el formato de 12 h en la esfera, el panel y Ajustes, la zona, la puesta en hora con error y reintento y el UTC pedido. También hace el control de integridad visual de las páginas nuevas.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T48 | Poner en hora | Ajustes > Fecha y hora > Poner en hora: pon la hora de tu móvil y pulsa Guardar cuando cambie el minuto | "Hora guardada y comprobada" y vuelve sola. La esfera muestra la hora. En el registro: `antes de poner en hora`, `tras poner en hora` y `RTC puesto en hora y verificado`. En Diagnóstico, Contenido: "UTC de Flex OS · UTC-05:00" |
| T49 | Se conserva | Pulsa RESET. Luego apágalo y enciéndelo | La hora sigue bien tras ambos (la mantiene el RTC). Si tras apagar sale "Hora sin configurar", envíame la línea `registros 00h-0Ah` |
| T50 | Precisión | Compara con el móvil a los 15 min y a las 24 h (amplía T27) | Anota la diferencia en segundos: sirve para decidir CAP_SEL |
| T51 | Zona y 12 h | Cambia la zona (p. ej. UTC+01:00) y vuelve a UTC-05:00. Activa y desactiva "Formato 24 h" | La hora cambia con la zona y vuelve; los registros del RTC en Diagnóstico solo cambian en los segundos. En 12 h: "9:41" con "PM" en la esfera y "9:41 PM" en el panel |

## F4.3. Apps de tiempo y lanzador

**Alcance (cierra D15):** cronómetro, temporizador y alarmas.

**Avisos solo visuales (D13).** El motor necesita escribir en el AXP2101 (ALDO3), y este firmware no escribe ese chip; además no se sabe si la placa lleva motor. El altavoz necesita ES8311/I2S. Las páginas lo dicen.

**Lanzador.**
- Se abre **deslizando la esfera hacia la izquierda**. Se vuelve con el gesto desde el borde izquierdo o con la cabecera.
- Mosaicos de vidrio con el estado real de cada app: cronómetro en marcha, tiempo restante del temporizador y próxima alarma.
- Mosaicos de Sensores (F4.4, "Pendiente" en esta versión) y Ajustes.

**Cronómetro** (como la referencia, `AppChrono.h:107-209`):
- Minutos y segundos con la fuente grande del reloj y las centésimas debajo.
- Tabla con las 3 vueltas más recientes (número, parcial y total). La mejor sale en verde y la peor en rojo, desde 3 vueltas y si no son todas iguales.
- Guarda hasta 20 vueltas; al llenarse descarta la más antigua sin renumerar.
- Botones "Vuelta"/"Reiniciar" e "Iniciar"/"Pausa"/"Seguir".
- Sigue contando fuera de su página.
- Solo redibuja deprisa (30 Hz) las centésimas, y solo en marcha.

**Temporizador.**
- Rodillos de minutos (0-99) y segundos, y atajos de 1, 3, 5, 10 y 15 min.
- En marcha muestra el tiempo restante en grande, una barra de progreso, "Pausa"/"Seguir" y "Cancelar".
- Cuenta por instante de fin (`esp_timer`): no se desvía aunque la interfaz se retrase.
- Al llegar a 0 sale el aviso **"Tiempo agotado"** en cualquier pantalla, con "+1 min" y "Detener".

**Alarmas.**
- Hasta 5, en hora local, con días de la semana (L M X J V S D) o una sola vez.
- Se pueden activar, desactivar, editar y eliminar. La página muestra la próxima ("hoy 7:00", "mañana 7:30", "lun 7:30").
- Se guardan en un bloque con versión y CRC de los ajustes persistentes (F4.1).
- Las comprueba el tick de 100 ms del sistema, una vez por minuto local. Usa el día de la semana **local**, no el del RTC, que es UTC.
- Una alarma "una vez" se desactiva al sonar.
- "Posponer" = 5 min.
- El aviso se cierra solo a los 3 min.
- Si el reloj estaba apagado a esa hora, no suena después.

**Pruebas.**
- **PC:** lógica pura (`ui_clock_logic.c`, sin LVGL):
  - cronómetro: vueltas, desbordamiento, mejor y peor, formato;
  - temporizador: pausa, fin una sola vez, +1 min, tope de 99:59, redondeo;
  - alarmas: coincidencia, próxima entre días y semanas, textos, empaquetado y datos dañados.
- **Vista previa:** batería "apps de tiempo F4.3".
  - Deslizar para abrir.
  - Cronómetro completo.
  - Temporizador con aviso en otra pantalla, +1 min y detener.
  - Alarma creada, guardada, sonando a su hora, desactivada, pospuesta, editada y eliminada.
  - Integridad visual y sin fugas.
  - La prueba "mono" ahora también desliza a la izquierda y abre las apps.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T52 | Lanzador | Desliza la esfera hacia la izquierda | Se abre "Aplicaciones" sin tirón; vuelves con el gesto desde el borde izquierdo |
| T53 | Cronómetro | Inicia, marca 3 vueltas, pausa, sal de la app 1 min y vuelve | Sigue contando fuera de la app; la mejor vuelta en verde y la peor en rojo; anota FPS del medidor con el cronómetro en marcha |
| T54 | Temporizador | Atajo "1", Iniciar, sal a la esfera | Al minuto, "Tiempo agotado" sobre la esfera; "+1 min" vuelve a contar; "Detener" lo cierra |
| T55 | Alarma | Crea una alarma para dentro de 2 min, una vez, y espera en la esfera | Suena a su hora (aviso en pantalla, sin sonido); queda "Desactivada"; tras reiniciar el reloj, la alarma sigue en la lista |
