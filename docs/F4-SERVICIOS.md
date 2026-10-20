# F4 — Servicios del sistema

Estado de cada bloque de F4 (pesos de `ROADMAP.md`). Las referencias son la auditoría de F4: cómo resuelve cada servicio el Flex OS Ultra de referencia y qué hardware tiene esta placa.

| Bloque | Contenido | Estado |
|---|---|---|
| F4.1 | Ajustes persistentes en NVS, escritor único | implementado · pruebas de PC · prueba física pendiente (T46, T47) |
| F4.2 | Poner en hora el RTC, zona horaria, 12/24 h | implementado · pruebas de PC · prueba física pendiente (T48-T51) |
| F4.3 | Cronómetro, temporizador, alarmas y lanzador de apps | implementado · pruebas de PC · prueba física pendiente (T52-T55) |
| F4.4 | IMU QMI8658 y app Sensores | implementado · pruebas de PC · prueba física pendiente (T60-T62) |
| F4.5 | Apagado de pantalla por inactividad, botón lateral | implementado · pruebas de PC · prueba física pendiente (T56-T59) |
| F4.6 | LittleFS en `storage` | implementado · compilado · prueba física pendiente (T63-T66) |

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
- Se abre **deslizando la esfera hacia la izquierda**. Se vuelve con el gesto desde el borde izquierdo o con la cabecera. **Actualizado en F10:** el cajón se abre hacia abajo y los gestos de borde cambian (`docs/F10-EXPERIENCIA.md` §2).
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

## F4.5. Apagado de pantalla e inactividad

La referencia no apaga la pantalla sola (`Touch.h:278`, `QuickPanel.h:262`). Este diseño es nuevo y reutiliza sus reglas: cualquier toque reinicia la cuenta y el toque que despierta no actúa (`Touch.h:292-300`).

**Política** (pura, con pruebas de PC: `ui_screen_logic.c`):
- **Encendida → atenuada → apagada.** El tiempo se elige en Ajustes > Pantalla > Apagado: 5, 10, 15, 30 o 60 s; 15 por defecto, guardado.
- **Atenuada:** los últimos 3 s (un tercio del tiempo si es corto), con un tercio del brillo del usuario. No cambia el brillo guardado.
- **Apagada:** brillo 0 y **DISPOFF** (0x28): los píxeles del AMOLED se apagan. El contenido queda en la RAM del panel.
  - Sin SLPIN (0x10): el driver del CO5300 no lo implementa y su temporización no está documentada.
  - Brillo 0 solo no basta: el panel seguiría barriendo.
- **Al apagarse vuelve a la esfera** y cierra el panel rápido. Es el "bloqueo por inactividad"; el bloqueo con código es D16 y no está acordado.
- **No se apaga** mientras suena un aviso, se ve la animación de carga o está abierta la prueba de pantalla.

**Se enciende con:**
- **Un toque.** No llega a la interfaz: lo recoge una capa transparente sobre todo, también durante la atenuación. El táctil sigue con alimentación y en modo interrupción; no se duerme, porque un error de lectura del táctil reinicia el reloj (`esp_lvgl_port_touch.c:127`).
- **El botón lateral (PWR → GPIO10).** Una pulsación corta apaga o enciende.
  - No se supone la polaridad: tras el arranque se aprende el nivel en reposo y se cuentan los cambios. Diagnóstico muestra "reposo N · ahora N".
  - Las pulsaciones de más de 2 s se ignoran: a los 4-6 s el AXP2101 apaga el reloj por hardware.
  - Se lee cada 100 ms: una pulsación de menos de 0,1 s puede perderse.
- **Una alarma o el temporizador.**
- **Enchufar el USB** (animación de carga) **o un aviso de batería.**

**Consumo en reposo.** Con la pantalla apagada:
- la esfera solo cambia una vez por minuto;
- el resto de la interfaz no dibuja nada (al apagarse vuelve a la esfera).

La CPU **no** entra en reposo ligero automático (`CONFIG_PM_ENABLE`). Exige medir en el reloj el consumo, el despertar del táctil (GPIO38) y el bus QSPI, y queda para F8.4. **No se ha medido el consumo.**

**Diagnóstico:** veces que se apagó y se encendió, y nivel del botón lateral.

**Vista previa:** batería "apagado de pantalla F4.5".
- Atenuada a los 12 s sin tocar el brillo del usuario.
- El toque que la saca no abre la fila.
- Apagada a los 15 s, de vuelta a la esfera.
- El toque que la enciende no abre las apps.
- Botón con pulsación corta y larga.
- La encienden el aviso del temporizador y el USB.
- La prueba de pantalla la mantiene encendida.
- Apagado a 5 s desde Ajustes.
- Sin fugas.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T56 | Apagado | Deja el reloj en Ajustes sin tocarlo | A los 12 s se atenúa; a los 15 s se apaga del todo (negro, sin brillo residual en una habitación oscura). Al tocarlo se enciende en la esfera y el toque no abre nada |
| T57 | Botón lateral | Ajustes > Diagnóstico > Pantalla y botón lateral: anota "reposo N". Luego pulsa el botón PWR brevemente dos veces | El reposo se aprende (0 o 1) y "ahora" cambia al pulsar. La 1.ª pulsación apaga y la 2.ª enciende. **No** mantengas el botón 4 s: apaga el reloj |
| T58 | Despertar | Con la pantalla apagada: conecta el USB; y en otra prueba, espera a que suene un temporizador de 1 min | Se enciende sola en ambos casos |
| T59 | Imagen tras encender | Apaga y enciende 10 veces | La imagen vuelve completa, sin basura ni franjas, y el táctil responde al momento. En el registro: `pantalla apagada (DISPOFF)` / `pantalla encendida`, sin errores |

## F4.4. IMU QMI8658 y app Sensores

La referencia usa un BNO085: no hay nada que portar. El BSP declara `BSP_CAPS_IMU 0` y el controlador de Waveshare tiene errores:
- su "reinicio" pone el modo SPI de 3 hilos;
- los rangos pisan la frecuencia;
- lee la temperatura y el contador en transacciones separadas.

Por eso hay un controlador mínimo propio, según la hoja de datos (componente `watch_imu`).

**Arranque.**
- Sondeo de 0x6B e identidad 0x05. **Si no responde o es otro chip, no se le escribe nada**: la app dice "No disponible".
- Si es el chip:
  1. Reinicio por software (0x60 = 0xB0) y espera a 0x4D = 0x80.
  2. Configuración registro a registro: ±4 g y ±512 °/s, 56 Hz los dos, autoincremento, sin interrupciones.
  3. Relectura de la configuración.
  4. **Reposo** (oscilador apagado, ~6 µA según la hoja de datos).

**Medir solo cuando hace falta.** Los sensores se encienden al abrir Sensores (unos 250 ms para que el giroscopio se estabilice) y se apagan al salir. Mientras tanto, una ráfaga de 19 bytes cada 20 ms en la tarea del IMU: la interfaz nunca hace I2C. Se cuentan las muestras perdidas con el contador de 24 bits del chip.

**Lógica pura con pruebas de PC (`qmi8658.c`).**
- Decodificación, con temperatura con signo bajo 0 °C (la librería de Arduino falla ahí).
- Contador de muestras con vuelta.
- Unidades e inclinación.
- La tabla de configuración: CTRL1 primero; sensores apagados antes de tocar rangos; nunca el bit SIM ni registros reservados; misma frecuencia en los dos sensores.

**App Sensores** (en Aplicaciones):
- **Nivel de burbuja:** el punto se pone verde con menos de 2°. Se muestra la inclinación.
- Aceleración X/Y/Z en g, giro X/Y/Z en °/s y temperatura del chip.
- Batería: tensión, nivel y estado.
- Estado del IMU: muestras, perdidas y errores de I2C.

**Pendiente de comprobar en el reloj:** la correspondencia entre los ejes del chip y la esfera, y que la variante C se comporta como la A de la hoja de datos.

**Fuera de alcance:** podómetro (comandos CTRL9), brújula, caídas y despertar al levantar la muñeca.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T60 | Detección | Arranca con el monitor serie | `QMI8658 (id 0x05, revision 0x..) configurado`. Si sale "no responde", envíame el registro |
| T61 | Ejes | Aplicaciones > Sensores con el reloj **plano** sobre la mesa; luego inclínalo hacia la derecha y hacia ti | Plano: Z ≈ 1,00 g (o −1,00), X e Y ≈ 0, el punto verde en el centro. Dime hacia dónde se mueve el punto al inclinar: así se ajustan los ejes |
| T62 | Giro y reposo | Gira el reloj sobre la mesa; luego sal de la app | Giro Z cambia de signo según el sentido; "Perdidas" casi 0 y "Errores I2C" 0. En el registro, al salir: `en reposo` |

## F4.6. LittleFS en `storage` (16 MB por encima de 16 MB)

**Dependencia nueva:** `joltwallet/littlefs ==1.22.3`, con el núcleo littlefs 2.11.3.
- Es la versión más reciente con más de 14 días, de julio de 2026.
- Su CI no compila ESP-IDF 5.5; aquí compila con 5.5.5 para esp32s3, sin avisos.
- **En tu PC** la descarga el gestor de componentes. Sube el `dependencies.lock` que se genere.
- **En el entorno sin registro** se usa una copia local de la misma etiqueta (`tools/offline/build_offline.sh`).
- **Opciones** (`sdkconfig.defaults`):
  - sin mapear la partición en caché: la caché solo direcciona 24 bits;
  - "lookahead" de 512 B, que cubre los 4096 bloques;
  - búferes en RAM interna.
- **No** se usa `littlefs_create_partition_image`: instalaría paquetes de Python al compilar.
- **Particiones sin cambios.**

**Puerta de escritura** (`stg_logic.c`, pura, con pruebas de PC). Solo se monta o se escribe si se cumple todo:
- chip GD25Q256 (JEDEC 0xC84019);
- 32 MB declarados y leídos;
- partición dentro del chip y alineada;
- veredicto **"sin aliasing"** de la sonda de B1. Tu foto del 2026-10-15 lo mostró.

Si no, la página dice por qué y no ofrece ninguna acción.

**Orden de uso** (Ajustes > Almacenamiento; todo lo que escribe es a petición):
1. **Al arrancar** solo se intenta **montar**, sin formatear a escondidas. Con los datos de fábrica en esa zona falla: "Sin formato".
2. **Probar escritura:**
   - borra, escribe con un patrón propio y relee el **último** sector de `storage`;
   - comprueba que ni el bootloader (0x0) ni la posición 16 MB más abajo cambiaron;
   - deja el sector borrado.
   - Se elige el último sector a propósito: si las direcciones dieran la vuelta, caería al final de `assets`, nunca en el bootloader.
3. **Formatear:** solo tras superar la prueba, con **dos toques**. Borra los datos de fábrica de los 16 MB altos; la imagen de fábrica se puede volver a grabar (`FLASH-32MB.md` §5). Después monta.
4. **Probar archivo:** escritura atómica (temporal, `fsync` y `rename`) de `/storage/flexos/prueba.txt` con un contador. Tras reiniciar, el contador sigue: se ha conservado.

**Pruebas.**
- **PC:** la puerta (cada condición la cierra por separado), el sector de prueba, el patrón y la CRC.
- **Vista previa:** la página bloqueada; luego el orden prueba → formatear (no formatea con un solo toque) → archivo; integridad visual.
- **Escritura real en la flash:** solo se puede comprobar en el reloj.

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T63 | Arranque | Monitor serie al arrancar | `storage sin LittleFS ... no se formatea solo` (datos de fábrica) o `LittleFS montado`. Nunca un formateo sin pedirlo |
| T64 | Prueba de escritura | Ajustes > Almacenamiento > Probar escritura | "Prueba del sector: Superada". En el registro: `prueba del sector 0x01fff000: Prueba superada`. Si dijera "LA ZONA BAJA CAMBIÓ", **no formatees** y envíame el registro |
| T65 | Formatear | Formatear, dos toques | "Montado (LittleFS)" y "Espacio: … de 16,0 MB". El reloj sigue arrancando normal tras reiniciar |
| T66 | Archivo | Probar archivo dos veces, reinicia y otra vez | El número sigue creciendo tras el reinicio (n.º 3): el archivo se conservó |
