# Multimedia, almacenamiento y transferencia en Flex OS Watch Ultra

Estado de cada pieza, con una de estas marcas:

- **Probado en el reloj**: funcionando en un Waveshare ESP32-S3-Touch-AMOLED-2.06. *Ninguna pieza de este documento
  lo está todavía*: el trabajo se ha hecho sin el reloj delante.
- **Probado en el PC**: pruebas de host (`tests/host`, con ASan/UBSan y, la Galería, también con TSan) y vista
  previa de LVGL (`tools/sim`, con ASan). El código es el mismo que va en el reloj; lo que cambia es el hardware.
- **Compilado**: el firmware compila para el ESP32-S3 con ESP-IDF 5.5.5 sin avisos y sin funciones con marcos de
  pila de más de 3072 B (`tools/stack/check_frames.py`).
- **Pendiente de validación**: necesita el reloj (§13 dice qué probar y qué anotar).

## 1. Diagnóstico de partida

- **Galería que reiniciaba el reloj al abrirse** (corregido en `52a296b`). Causa raíz, medida con `-fstack-usage`
  del compilador Xtensa: `gal__origin_load` (8224 B) y `gal__origin_add` (8256 B) declaraban en la pila una tabla de
  64 orígenes, dentro de la tarea `watch_gal` de 12 KB y encima de una lectura de LittleFS. Según la profundidad de la
  lectura, el canario de la pila lo detectaba (pánico) o se corrompía el montón contiguo (fallo diferido): de ahí el
  "en ocasiones". Las tablas van ahora al montón del motor. Se añadieron `tools/stack/check_frames.py` (marcos de
  todo el firmware) y `tests/host/gal_race.c` (la Galería con dos hilos bajo ThreadSanitizer), que encontró y dejó
  corregidas tres carreras benignas. Detalle en `GALERIA.md` §13.
- **Microfonía, altavoz y microSD sin usar.** El firmware no tenía servicio de audio ni de microSD: los pines y
  códecs estaban en el BSP de Waveshare, pero ninguna app los usaba. Música solo era el mando del teléfono.
- **Flash 16 MB frente a 32 MB.** Resuelto en `FLASH-32MB.md`: el chip es un GD25Q256 (32 MB) y se declaran 32 MB
  sin la opción experimental; los 16 MB altos solo para datos (`storage`). La lectura por encima de 16 MB sigue
  **pendiente de validación** (T10). La memoria del reloj de este documento es la partición `storage`, que solo se
  monta si esa comprobación pasa (`watch_storage`).

## 2. Almacenamiento: dos volúmenes y un destino elegido (`watch_media`)

| Volumen | Raíz | Sistema | Cuándo está listo |
|---|---|---|---|
| Reloj (`WM_VOL_WATCH`) | `/storage` | LittleFS en la partición `storage` (16 MB, zona alta) | `watch_storage` montado (puerta de 32 MB pasada) |
| microSD (`WM_VOL_SD`) | `/sdcard` | FATFS (FAT12/16/32) por SDMMC de 1 bit | máquina de estados en `READY` (§3) |

- **Un solo dueño.** `watch_media` es el único que monta, desmonta y formatea; las apps piden y leen estado.
- **Destino elegido** ("Guardar en": Música, Galería › +, Almacenamiento › microSD; NVS `m_dest`). Lo nuevo
  (grabaciones, lo que llega del móvil, descargas de la Galería) va **solo** ahí. Si no está listo, la operación se
  detiene con el motivo ("Has elegido guardar en la microSD y no hay tarjeta…"): **nunca** se guarda en el otro
  volumen por detrás.
- **Biblioteca única** (`wm_store`): un catálogo `FlexMediaStore` de Flex OS Ultra por volumen (fotos, vídeos,
  música, grabaciones, dibujos), con miniaturas persistentes de 132 px en `/System/Media/th` de cada volumen. La
  leen Música, la grabadora, la Galería y el servidor web; la interfaz solo usa instantáneas, sin E/S.
- **Escrituras transaccionales.** Catálogo: temporal + `fsync` + renombrado, con la generación anterior en `.bak`.
  Lo recibido y lo descargado se escribe en temporales y se publica entero y comprobado; una grabación cortada se
  repara al montar y aparece como "Grabación recuperada" (§5). Nada se borra ni se formatea solo: borrar es una
  orden de la persona (en el reloj va a la papelera de Archivos; la microSD no tiene papelera y se pide doble toque).
- **Reservas**: el LittleFS nunca se llena por debajo de 512 KB (`FML_RESERVE_BYTES`); la grabadora deja 4 MB libres
  en la microSD.

Estado: **probado en el PC** (`test_media.c`: rutas seguras, textos, clasificación) y **compilado**.

## 3. microSD (`wm_sd`, `wm_sd_logic`)

- **Pines** (BSP de Waveshare y esquema): CLK GPIO2, CMD GPIO1, D0 GPIO3, **DAT3/CS GPIO17** (entrada con
  *pull-up*). **No hay pin de detección**: la tarjeta se detecta sondeando (CMD13 y sonda de inicio) cada 1,5 s con
  una pantalla de almacenamiento abierta y cada 10 s si no. SDMMC de 1 bit a 20 MHz, como el BSP.
- **Estados**: `ABSENT → DETECTING → MOUNTING → READY`, `BUSY` (formateando), `ERROR` (con motivo) y `REMOVING`.
  Dos fallos seguidos de CMD13 se toman como tarjeta retirada.
- **Arrendamientos.** Quien abre un archivo en la microSD toma `wm_sd_lease()` y lo suelta al cerrar; `REMOVING`
  espera a que no quede ninguno (a los 10 s lo dice: "esperando a que se suelte"). El servidor web, el vídeo, la
  Galería y la grabadora los respetan y cortan lo suyo si la tarjeta deja de estar `READY`.
- **Capacidad.** Hasta 2 TB (SDXC: el CSD de 32 bits lo cubre) si está en **FAT32**. ESP-IDF 5.5.5 se compila sin
  exFAT ni LBA de 64 bits: una tarjeta exFAT (la mayoría de 64 GB o más vienen así) se reconoce, se dice
  "Formato exFAT…" y se ofrece formatear; **no se formatea sola**.
- **Formatear** en FAT32 (clúster de hasta 64 KB): pantalla propia, botón y **segundo toque** "Sí, borrar todo" en
  5 s, y un *ticket* de un solo uso del servicio (60 s). Sin ticket no hay formateo.
- **Expulsar** y **Volver a montar** en Almacenamiento › microSD, con el estado real, libre, capacidad y tipo.

Estado: **probado en el PC** (la máquina de estados con un "controlador simulado" que mete, saca, falla y cambia
de formato la tarjeta al azar, comprobando que nunca se desmonta con un arrendamiento vivo ni se formatea sin
pedirlo) y en el simulador (páginas). **Pendiente de validación** (T219-T225).

## 4. Audio: un solo dueño del I2S (`watch_audio`)

| Pieza | Dato (verificado en el BSP y el firmware de fábrica) |
|---|---|
| Bus I2C | SDA GPIO15, SCL GPIO14 (el *handle* compartido del BSP) |
| I2S | `I2S_NUM_1`: MCLK 16, BCLK 41, LRCK 45, DOUT 40, DIN 42 |
| Altavoz | ES8311 en 0x18 + amplificador NS4150B con enable en GPIO46 (pull-down de 10 kΩ) |
| Micrófonos | ES7210 en 0x40, TDM de 4 ranuras: 0 MIC1, 1 MIC3 (eco), 2 MIC2, 3 MIC4 (no conectado); ganancia 24 dB en MIC1 y MIC2 |
| Perfil de voz | 24 kHz, 16 bits, MCLK x256 (el de fábrica) |

- `watch_audio` es el único que abre el I2S y los códecs; el reproductor y la grabadora se lo piden. Si se pone a
  grabar con música sonando, la música se pausa (`preempted`) y no se reanuda sola.
- El amplificador (enable en GPIO46) lo maneja `esp_codec_dev` con la salida abierta; grabando se mantiene
  apagado (ni ruido ni realimentación). La salida se cierra sola tras 10 s sin sonar.

Estado: **compilado**; la lógica (WAV, cabeceras, tiempos, espacio) **probada en el PC**. Sonido y micrófono
**pendientes de validación** (T226-T232).

## 5. Grabadora (app nueva, `ui_recorder.c` + `wa_rec.c`)

- WAV PCM de 16 bits a 24 kHz: **mono** en el reloj; mono o estéreo en la microSD (interruptor "Estéreo
  (microSD)" en Grabaciones).
- Medidor de nivel en vivo, tiempo, pausa/reanudar, descartar con doble toque, chip con el destino y lo que cabe
  ("Reloj · quedan 45 min"), aviso de saturación y de muestras perdidas ("el disco va lento").
- Captura a un anillo de 128 KB en PSRAM (2,7 s en mono) y escritura a bloques de 16 KB en otra tarea: una escritura
  lenta no corta la captura.
- **Sin pérdidas al cortar**: en la microSD la cabecera se reescribe cada 5 s; en el LittleFS (donde reescribir el
  principio de un archivo cuesta copiarlo entero) la cabecera lleva tamaño "en curso" y se fija al terminar. Al
  montar, lo que quedó a medias se repara y se publica como "Grabación recuperada"; nunca se borra nada con datos.
- Grabaciones en `/Grabaciones` del destino; se escuchan desde la grabadora o Música › Grabaciones.

Estado: **probado en el PC** (lógica y pantallas) y **compilado**. **Pendiente de validación** (T226-T229).

## 6. Música y reproductor (`ui_player.c` + `wa_player.c`)

- **Formatos**: WAV PCM de 8/16 bits e IMA ADPCM (el `flexAs` del P4) y **MP3** MPEG-1/2/2.5 capa III (minimp3, CC0;
  28/28 vectores de conformidad ISO idénticos bit a bit). Cualquier frecuencia: el I2S se reconfigura por pista.
- **Etiquetas**: ID3v2 (título, artista, álbum) y la portada JPEG (APIC), reducida a 200 px.
- **Música**: biblioteca local de los dos volúmenes (Canciones, Grabaciones), "En el teléfono" (el mando de lo que
  suena en el móvil, que ya existía y no se ha quitado), "Añadir desde el móvil" y "Guardar en".
- **Reproduciendo**: portada, título, artista, formato, barra con salto, anterior/reproducir/siguiente y volumen
  (guardado, NVS `m_vol`). Fondo de vidrio **desenfocado desde la portada** (§10).

Estado: **probado en el PC** (decodificación, etiquetas, pantallas, sin rastros) y **compilado**. Sonido **pendiente
de validación** (T230-T233).

## 7. Galería: Fotos / Vídeos / +

Detalle en `GALERIA.md` §13-§14. En resumen:

- La barra "Todas / Reloj / (fuente) / Más opciones" se sustituye por **Fotos**, **Vídeos** y **+**. Cada pestaña
  junta lo del reloj, lo de la **microSD** y lo de la fuente remota elegida (Flex OS Ultra, Flex Cloud o el Flex
  Cloud del teléfono por el P4), sin duplicar lo descargado. Marcas: nada en lo del reloj, "SD" en la microSD y la
  fuente en lo remoto.
- **+** reúne lo de antes y lo nuevo: Recibir con QR, Desde el navegador, Flex OS Ultra, Flex Cloud, Flex Phone,
  Origen, Guardar en, Transferencias y Música. **No se ha quitado ninguna función**: cambian de sitio.
- Lo local sale de la biblioteca única (§2) por una interfaz del motor (`gal_local_io_t`): miniaturas persistentes
  de la biblioteca (también de vídeos y dibujos), lectura con arrendamiento, papelera en el reloj y borrado con doble
  toque en la microSD, y **descargas al destino elegido** (en la microSD, copia verificada antes de borrar el
  temporal).
- Visor de fotos: el de siempre (encajada y 2x, deslizando), ahora también para las de la microSD.

Estado: **probado en el PC** (409 comprobaciones del motor, TSan, recorrido completo en el simulador) y **compilado**.
**Pendiente de validación** (T234-T240).

## 8. Vídeo AVI MJPEG (`wm_video.c` + `ui_video.c`)

- Demultiplexor AVI de Flex OS Ultra (`flexAvi*`); cada fotograma por TJpgDec (ROM) a RGB565 al tamaño de la
  pantalla (escala 1/2, 1/4 o 1/8 solo si no cabe en 410 px). Lectura por trozos.
- **Ritmo** por el reloj del sistema: si la decodificación va tarde, se **saltan** fotogramas (8 bytes de cabecera,
  sin decodificar); nunca a cámara lenta.
- **Memoria**: dos lienzos del tamaño del fotograma, una zona fija de 48 KB para TJpgDec y el búfer del fotograma
  comprimido (96 KB, crece hasta 1 MB si hace falta): nada por fotograma, todo se suelta al cerrar.
- **Sin sonido**: la página del móvil crea AVI MJPEG solo de imagen y las pistas de audio de otros AVI se saltan.
- Pantalla: fotograma sobre negro, mandos de vidrio (reproducir/pausa, barra para ir a un punto, tiempo) que se
  ocultan solos a los 3 s; al ir a la esfera se pausa. Lo que no es MJPEG explica qué hacer.
- La página del móvil convierte a 320/360/400 px (lado largo) a 10/12/15 fps: siempre 1:1 en el reloj.

Estado: **probado en el PC** (ritmo, escala, búsqueda; pantalla con fotogramas sintéticos) y **compilado**.
Rendimiento real (fps, saltos) **pendiente de validación** (T241-T244).

## 9. Recibir desde el móvil: QR y Flex Web Server (`wm_web.c` + `ui_xfer.c`)

### 9.1 El mismo mecanismo que Flex OS Ultra

Es el Flex Web Server del P4 (`FlexOS_MediaWeb`, §14), con la misma página web y el mismo protocolo. No hay un
segundo mecanismo. El firmware del P4/C6 y su servidor **no se han tocado**.

### 9.2 Flujo

1. Galería › + › Recibir con QR (o Música › Añadir desde el móvil). El servidor arranca.
2. El reloj enseña el QR (`http://<IP>:8080/?k=<código>`), el código en grande ("482 913"), cuánto le queda, la
   dirección, el destino y los móviles conectados. **Escanear es emparejar.**
3. La página del móvil convierte (fotos a 1024/1280/1600 px, vídeo a AVI MJPEG, audio a WAV) y sube por trozos.
   El reloj enseña el progreso ("Recibiendo · 45 %", "Comprobando que se puede abrir…", "Guardado en Reloj").
4. Al salir de la pantalla, el servidor termina lo que esté en marcha y se apaga.

### 9.3 Seguridad y sesiones

- Código de 6 cifras **con caducidad de 10 min** (`410` si caducó, sin gastar intentos); "Código nuevo" lo cambia
  sin cerrar las sesiones; una IP nueva cambia el código y cierra todas. "Desconectar móviles" las cierra.
- 5 intentos fallidos y espera creciente; cookie de sesión `HttpOnly` + `SameSite=Strict`; `X-Flex` obligatorio en
  todo lo que cambia algo (CSRF); `Host` debe ser la IP del reloj (DNS rebinding); 6 sesiones como mucho, 30 min
  sin uso.
- **Identidad del aparato**: `/api/hello` dice nombre ("Flex OS Watch Ultra"), identidad (`S3-<MAC>`), tipo, destino
  y segundos del código. La página la devuelve en `X-Flex-Dev`; si la IP la tiene ahora otro aparato Flex (o la
  pestaña es de otro), el servidor contesta `409` y **no escribe nada**, y la página pide volver a escanear.
- El código y las sesiones **no se registran** (`ESP_LOG`): solo contadores. El código no se queda en la PSRAM al
  apagar.
- Límite conocido: HTTP sin TLS en la red local, igual que el P4 (§12).

### 9.4 Límites y vida del servidor

- Una conexión a la vez (las demás esperan; una inactiva cede el sitio), puerto 8080, sockets de lwIP.
- Tarea `wm_web` con 16 KB de pila en PSRAM (`flexWebServeConn` usa ~5 KB; admitido en `check_frames.py`), búferes
  de 4 + 16 KB solo con el servidor encendido.
- Se apaga al salir de la pantalla (cuando acaba lo que está en marcha) o tras 15 min sin peticiones.
- Tamaño por archivo: 6 MB por foto, 8 MB por vídeo o audio (los del P4). Lo recibido se valida antes de publicarlo
  (foto decodificada entera, MP3 con tramas reales, AVI con su primer fotograma).
- Disco y catálogo **del destino elegido por conexión**; si no está listo, no se aceptan subidas y se dice por qué.
  Con la microSD, si se expulsa o se saca, la subida en curso se corta y sus temporales se limpian.

Estado: **probado en el PC** (202 comprobaciones del servidor, 16 de ellas de lo del reloj; la lógica del servicio;
la pantalla con QR nítido, caducado, sin Wi-Fi, destino ausente y progreso) y **compilado**. Sockets, Wi-Fi y móvil
reales **pendientes de validación** (T245-T252).

## 10. Liquid Glass en lo nuevo

- Todas las pantallas nuevas usan los componentes del motor de vidrio (`flex_glass_*`): paneles, botones, filas,
  chips y deslizadores.
- **Desenfoque real desde la fuente** (`LIQUID-GLASS.md` §15): Reproduciendo desenfoca **su portada** una vez por
  cambio, en un búfer propio de 1/4 (104 x 127, 26 KB), en 8 bits con tramado, y lo dibuja a escala exacta x4. No
  lee la pantalla, así que no puede dejar costuras, parches ni rastros.
- **Sin fantasmas**: la batería `rastros` (búferes parciales dobles, redondeo del CO5300, franjas de 54/40/16 líneas,
  fondo Aurora y esfera fotográfica) cubre ahora Reproduciendo (volumen arrastrado y página que se va tras el dedo),
  Desde el móvil (desplazándose) y Vídeo: 940 cuadros por caso, **0 con rastros**.

## 11. Memoria (PSRAM salvo donde se dice)

| Qué | Cuánto | Cuándo |
|---|---:|---|
| Catálogo de la biblioteca | 384 x 336 B = 126 KB por volumen | con el volumen montado |
| Lista local de la Galería | 126 KB | solo mientras se rehace la lista |
| Motor de la Galería + miniaturas + JSON | ~180 KB + hasta 30 x 24,5 KB (una a una) + 160 KB si cabe | con la Galería abierta |
| Pila de la tarea de la Galería / del navegador | 12 KB / 8 KB (+ 7 KB al probar el servidor) | con la app abierta |
| Grabadora: anillo + bloque | 128 KB + 16 KB | grabando |
| Reproductor | 16 KB de entrada MP3 + trama + pila de 8 KB (RAM interna si sobra; si no, PSRAM) | sonando |
| Portada en Reproduciendo | 200 x 200 x 2 = 80 KB | con la pantalla abierta |
| Fondo desenfocado | 26 KB (+ ~40 KB solo durante el cálculo) | con la pantalla abierta |
| QR | hasta 176 x 176 = 31 KB (L8) | con la pantalla abierta |
| Servidor web | ~2 KB de estado + 4 KB + 16 KB + pila de 16 KB | con la pantalla abierta o terminando |
| Vídeo | 2 x ancho x alto x 2 (400 x 225: 352 KB) + 48 KB + 96 KB | reproduciendo |

`wm_alloc` nunca deja menos de 768 KB de PSRAM libres: si no cabe, la operación lo dice en vez de reservar.

**Firmware** (ESP-IDF 5.5.5, `idf.py size`): imagen de 3 931 255 B en una ranura OTA de 6 MB (37 % libre); DIRAM
180 939 de 341 760 B (52,9 %). La web del móvil va dentro (276 KB).

### 11.1 "Sin memoria" con la PSRAM casi vacía: causa y arreglo

El ESP32-S3 tiene **dos memorias**: la RAM interna (~300 KB, la del Wi-Fi, el BLE, lwIP, la pantalla y las pilas
de las tareas del sistema) y la PSRAM (8 MB). La barra "PSRAM" de Almacenamiento solo mide la segunda. Lo que
fallaba no era la PSRAM:

1. **Pilas de las apps en RAM interna.** La Galería (12 KB), el navegador (8 KB + 7 KB al probar el servidor), el
   reproductor (8 KB) y la captura de la grabadora (3 KB) creaban su tarea con `xTaskCreate`, que reserva la
   pila en RAM interna. Con el Wi-Fi activo, cuando no quedaba un bloque de 12 KB ahí, la Galería decía "Sin
   memoria" aunque la PSRAM estuviera al 24 %. Ahora todas pasan por `watch_task_spawn()` (`watch_diag`):
   la pila va a PSRAM (posible porque el código se ejecuta desde la PSRAM, `CONFIG_SPIRAM_XIP_FROM_PSRAM`) o a
   la RAM interna solo si después de ponerla quedan 40 KB (`MEM_INTERNAL_KEEP`); si la primera falla, prueba la
   otra. El TCB (~350 B) sigue en RAM interna, como exige FreeRTOS.
2. **Fuga en la grabadora.** La tarea de escritura se creaba con `...WithCaps` y terminaba con
   `vTaskDelete(NULL)`: ESP-IDF no suelta así ni su TCB (RAM interna) ni su pila, y **cada grabación perdía
   ~0,35 KB de RAM interna y 6 KB de PSRAM** hasta reiniciar. Ahora toda tarea creada así termina con
   `watch_task_exit()` (`vTaskDeleteWithCaps`).
3. **Reservas grandes de una pieza en la Galería.** Abrir pedía 30 miniaturas en un bloque de 735 KB y 160 KB de
   JSON, con 1 MB de margen; si no cabía, no abría. Ahora abrir no reserva nada grande: cada miniatura pide sus
   24,5 KB al hacerse (si no hay, toma el lienzo de la más vieja que no se esté pintando, y si tampoco, la celda
   se queda sin imagen y se reintenta a los 5 s), el JSON se pide si cabe y se reintenta en cada vuelta (sin él
   se ve lo del reloj y la microSD; Flex OS Ultra/Cloud esperan), la lista local se reintenta cada 3 s si su
   copia no cabe (antes se rendía y quedaba vacía) y el visor suelta las miniaturas si la foto no cabe.
   El margen de PSRAM de la Galería pasa de 1 MB a 384 KB.
4. **Mensajes honestos.** Nada de "cerrar otra app libera memoria" (no hay otras abiertas a la vista): la
   Galería, el navegador, el reproductor, la grabadora y el vídeo dicen qué memoria falta y cuánta hay
   (`mem_why_text`, p. ej. "Faltan 12 KB de RAM interna (hay 9 KB). PSRAM libre: 6,0 MB.") y se reintentan
   solos donde tiene sentido.

Cómo comprobarlo en el reloj (T253-T256): el monitor escribe `watch_diag: tarea watch_gal: pila de 12288 B en
PSRAM` al abrir cada app, y `[mem]` con la RAM interna libre al abrir y cerrar.

### 11.2 Liberar espacio (borrar multimedia)

Almacenamiento › **Liberar espacio** (y microSD › Uso › Liberar espacio) enseña, por volumen y con su espacio
libre, los 20 archivos multimedia más grandes (música, grabaciones, vídeos, fotos y dibujos) de mayor a menor.
Un toque marca la fila ("Toca para borrar", 5 s); el segundo toque la borra **del todo** con `wm_lib_delete`
(el espacio vuelve en el momento; no pasa por la Papelera) y un aviso dice cuánto se liberó. Nunca se borra
nada solo ni por falta de espacio; lo protegido y lo que aún está entrando no se ofrece; la canción que suena
no se borra ("Párala para borrarla"); un archivo abierto en la microSD no se puede borrar (FatFs lo bloquea) y
se dice. La copia del catálogo para elegir los más grandes (~126 KB de PSRAM) solo vive mientras se recorre.

## 12. Limitaciones conocidas

- **Nada probado aún en el reloj** (§13).
- **Vídeo sin sonido**; solo AVI MJPEG (lo demás se convierte en el móvil).
- **exFAT no se monta** (ESP-IDF 5.5.5 sin exFAT): hay que formatear en FAT32 (borra la tarjeta; se pide dos veces).
- **HTTP sin TLS** en la red local (como el P4): cualquiera en la misma Wi-Fi que vea el QR o el código puede
  emparejarse mientras no caduque. Mitigado con caducidad, límite de intentos y "Desconectar móviles".
- La lista local de la Galería enseña como mucho 200 elementos (los más recientes).
- Las descargas de la Galería pasan primero por el LittleFS (hace falta ese sitio aunque el destino sea la microSD).
- Sin contenido protegido desde la web del reloj: no hay PIN de sistema con el que comprobar al propietario.
- Lo que llega del móvil se limita a 6 MB por foto y 8 MB por vídeo o audio también en la microSD.
- Los catálogos de la biblioteca (126 KB por volumen) siguen cargados mientras el volumen está montado; con la
  PSRAM es asumible y no se han pasado a carga bajo demanda.
- "Liberar espacio" enseña los 20 más grandes de cada volumen; lo demás se borra desde cada app o Archivos.
- La causa de "Sin memoria" en §11.1 está deducida del código y de la configuración (no hay un registro del
  reloj que lo confirme): T253-T256 lo comprueban.

## 13. Pruebas físicas pendientes (T219-T260)

| Prueba | Qué hacer | Qué anotar / esperado |
|---|---|---|
| T219 | Arrancar sin microSD; meter una FAT32 (32 GB); abrir Almacenamiento › microSD | "Lista · X libres de Y" en < 3 s con la página abierta (< 12 s sin ella). Copiar las líneas `wm_sd` del monitor |
| T220 | Con la microSD lista, Expulsar; sacarla; meterla y "Volver a montar" | "Expulsada: ya puedes sacarla"; al volver, lista sin reiniciar |
| T221 | Sacar la tarjeta sin expulsar, en reposo y grabando en ella | Sin reinicios ni cuelgues; "Sin tarjeta"; la grabación se para con su motivo y al volver a meterla aparece "Grabación recuperada" |
| T222 | Meter una tarjeta exFAT (64 GB o más, tal cual de fábrica) | "Formato exFAT…"; no se toca nada; se ofrece formatear |
| T223 | Formatear en FAT32: tocar "Formatear" una vez y esperar 6 s; luego dos veces | El primer toque solo no formatea; el doble sí. Anotar el tiempo y la capacidad que queda |
| T224 | Tarjeta de 128 GB o más en FAT32 (formateada en el reloj o en el PC) | Capacidad correcta (64 bits); leer y escribir |
| T225 | Elegir "Guardar en: microSD" y sacar la tarjeta; intentar grabar, recibir desde el móvil y descargar en la Galería | Las tres se niegan con el mismo motivo; **nada** aparece en el reloj |
| T226 | Grabar 1 min en el reloj y 1 min en la microSD (mono y estéreo) | El medidor se mueve con la voz; los WAV suenan bien en el PC (24 kHz). Anotar "perdidos" (esperado 0) |
| T227 | Grabar con la pantalla apagada 30 min en la microSD | Sin cortes; tamaño ≈ 2,9 MB por minuto en mono (24 000 x 2 B/s) |
| T228 | Grabar y quitar la alimentación (o reiniciar) a mitad | Al arrancar, "Grabación recuperada" se escucha hasta el corte (±5 s en la microSD) |
| T229 | Grabar con música sonando | La música se pausa; al terminar no vuelve sola |
| T230 | Reproducir un MP3 de 128 y de 320 kbps con portada, y un WAV de 44,1 kHz | Suenan sin cortes; portada, título y artista; el volumen se guarda al reiniciar |
| T231 | Reproducir mientras se navega por la Galería y el cajón | Sin cortes al animar |
| T232 | Volumen al 100 % con un MP3 fuerte | Sin distorsión audible del amplificador; anotar si se oye un chasquido al empezar/terminar |
| T233 | Reproduciendo: mirar el fondo desenfocado | Suave, sin bloques ni franjas visibles; anotar si se ve el grano del tramado |
| T234 | Galería con fotos en el reloj y en la microSD | Las dos en Fotos; las de la microSD con "SD"; miniaturas sin esperar mucho tras la primera vez |
| T235 | Abrir una foto de la microSD y deslizar entre fotos de los dos volúmenes | Visor correcto, 2x funciona |
| T236 | Borrar una foto de la microSD (doble toque) y otra del reloj | La de la microSD desaparece; la del reloj va a Archivos › Papelera |
| T237 | Con "Guardar en: microSD", descargar una foto de Flex OS Ultra | "Guardada en la microSD"; aparece con "SD"; no queda copia en el reloj |
| T238 | "+": tocar cada fila | Cada una abre lo que dice; Flex OS Ultra / Flex Cloud piden emparejar o vincular si falta |
| T239 | Galería abierta 10 min usándola (fotos de los dos volúmenes, vídeos, +) | Sin reinicios; RAM interna y PSRAM libres parecidas al principio y al final (Device Care) |
| T240 | Expulsar la microSD con la Galería abierta | Las fotos "SD" desaparecen sin fallos; la expulsión no se queda esperando |
| T241 | Subir un vídeo desde el móvil (perfil Recomendado) y reproducirlo | Fluido. Al cerrar, copiar del monitor la línea `vídeo cerrado: N enseñados, M saltados` (pocos saltados) |
| T242 | Vídeo largo (5 min, perfil Alta calidad) desde la microSD | Sin reinicios; ritmo correcto (5 min de reloj) |
| T243 | Tocar la barra para ir a un punto; pausar; ir a la esfera y volver | Salta al punto; en pausa no avanza; a la esfera se pausa |
| T244 | Expulsar la microSD reproduciendo un vídeo de ella | Se detiene con "Se ha retirado la microSD"; la expulsión termina |
| T245 | Galería › + › Recibir con QR con Wi-Fi; escanear con la cámara del móvil | La página abre emparejada; el reloj cuenta 1 móvil |
| T246 | Subir 5 fotos y 1 vídeo desde el móvil | Progreso en el reloj; aparecen en la Galería del destino elegido |
| T247 | Esperar 10 min sin emparejar y escanear el QR viejo | "El código ha caducado…"; "Código nuevo" vuelve a funcionar |
| T248 | Con la página abierta en el móvil, cambiar el reloj de Wi-Fi o reiniciar el router (otra IP) | El reloj enseña un QR nuevo; la página vieja pide volver a escanear y no sube nada |
| T249 | Dos aparatos Flex en la misma red; abrir en el móvil la página del otro y subir | La página del otro no escribe en este (409) |
| T250 | "Desconectar móviles" | La página del móvil pide el código otra vez |
| T251 | Salir de la pantalla del QR con una subida en marcha | La subida termina; luego el servidor se apaga (monitor: `servidor apagado`) |
| T252 | Pantalla del QR abierta 20 min sin usar | El servidor se apaga a los 15 min sin peticiones |
| T253 | Con Wi-Fi, abrir Galería, Navegador, Música y Grabadora, una tras otra; mirar el monitor | Abren todas. Copiar las líneas `watch_diag: tarea … pila de … en PSRAM/RAM interna` y las `[mem]` |
| T254 | Repetir T253 10 veces seguidas (abrir y cerrar cada app) | Nunca "Sin memoria"; la RAM interna libre (`[mem]`) vuelve al mismo valor ±2 KB |
| T255 | Grabar 10 grabaciones cortas seguidas | Device Care / `[mem]`: la RAM interna y la PSRAM no bajan con cada grabación (antes, fuga) |
| T256 | Galería con 60 fotos: bajar y subir rápido por la cuadrícula; abrir una foto grande | Miniaturas aparecen; el visor abre; ninguna tarjeta "Sin memoria" |
| T257 | Almacenamiento › Liberar espacio | Los archivos más grandes del reloj y de la microSD, con su tamaño, de mayor a menor |
| T258 | Tocar uno una vez y esperar 6 s; luego tocarlo dos veces | El primer toque no borra y caduca; el doble borra; el espacio libre sube lo que pesaba |
| T259 | Con una canción sonando, intentar borrarla desde Liberar espacio | "Está sonando · Párala para borrarla"; no se borra |
| T260 | Borrar un vídeo de la microSD y comprobar en el PC | El archivo ya no está; nada más ha cambiado en la tarjeta |

## 14. Procedencia de la pila de medios (`components/flexmedia`)

La biblioteca de medios, el almacén, las miniaturas, el demultiplexor AVI/MJPEG, el reproductor WAV/IMA, el
codificador JPEG, el codificador QR, el analizador HTTP y el Flex Web Server **no se han reescrito**: son los de
Flex OS Ultra (ESP32-P4), copiados del repositorio de referencia **sin modificarlo**:

- repositorio `AccountDev-Design/Flex-OS-Ecosistema`;
- rama `claude/flexos-ota-system-blq9g9-2csacx` @ `5dbc949`;
- carpeta `FlexOS_Ultra/` (código) y `tests/host/` (pruebas).

Así el reloj habla exactamente el mismo protocolo que el P4 (código de 6 cifras, cookie `fxs`, `X-Flex: 1`,
comprobación de `Host`, limitador de intentos, subida por trozos con CRC-32) y la página web que abre el móvil es la
misma. No se ha creado un segundo mecanismo de transferencia.

Cada archivo portado empieza con una cabecera `PORT a Flex OS Watch Ultra de …` y sus cambios propios llevan la marca
`RELOJ:`, de modo que `grep -n "RELOJ:"` enseña todas las diferencias con el original.

### 14.1 Huellas SHA-256 de los originales

Código (`FlexOS_Ultra/`):

| Archivo | SHA-256 del original |
|---|---|
| `FlexOS_HttpShare.cpp` | `48c7c31ed069fa23187eae7bc68fce519786ffe737f81c5feb67e53f5bf3332d` |
| `FlexOS_JPEGEnc.cpp` | `d729bcdaaeb3d14c47c20b046ddcd7356701ff7939d3d0223d1ddb37b297d873` |
| `FlexOS_Media.cpp` | `1bd4a14992e46627fb612c91b2da803d980034fbd459364a9a0b624135cbb733` |
| `FlexOS_MediaLib.cpp` | `afd20baab062e17212a6e4249416ac61f5e1981ffe1812c86d75a6a97eec2609` |
| `FlexOS_MediaStore.cpp` | `9fad4bc4239d5e641c555b9f4c1d9a629a1f57e58f4f4f645ef4b27da4d712fb` |
| `FlexOS_MediaThumb.cpp` | `a5470ce3933346d41a4caf9d46248b00c047453abd1c7c51f32527b73ce0776f` |
| `FlexOS_MediaWeb.cpp` | `3defeb76b968af1883ea6b111030614e5f9d0514de40bb6bcb113dd8a050bef9` |
| `FlexOS_QR.cpp` | `da7a6bcc012e4f7b82e601cdb0583462a7ea2fc773425f6437781546083bc75e` |
| `FlexOS_HttpShare.h` | `f73061aaf71d692c34b887564bd14c3d6587d4247e3b7f86475663978424f357` |
| `FlexOS_JPEGEnc.h` | `f0e695bd57af466d79c5c9a2e7ea2f3410a8d045d653c1065f4d38105d8119bd` |
| `FlexOS_Media.h` | `d0816ab89903ecad4d895a4f3a40642edaea936cfdc9d9230b126df63ecabd5c` |
| `FlexOS_MediaLib.h` | `bd28d2a6abf7bb50d2f259fda3a7daa170d1c71ffbf6f397c86abfd18a594205` |
| `FlexOS_MediaStore.h` | `e3df9904e0a4ffb7e5d3c25cebbe57a9b24cb849a0333a86e59bf153825e85bb` |
| `FlexOS_MediaThumb.h` | `0d0da6018fe9ea56b9f13440fd69e666bcc02c131664c7460928cf96f8349060` |
| `FlexOS_MediaWeb.h` | `ca3b09cfc219e53339d4c6eac99d94e338dc8e9d2e70516af12c688734845190` |
| `FlexOS_QR.h` | `89804271598d9bfc2259ace5f5302d70da3a9d8c4043d9314df999141994c47b` |
| `FlexOS_StorageCore.h` | `cb50cfb6fe000f7b9024e2878698fbfd3dacd725da10cd4da23f452c698df5a3` |
| `webui/app.css` | `efe25ed3be2b2f7c6f3dff4179e6a28f20c8142dac60055cc7508c653b8e8783` |
| `webui/app.js` | `97011eeddb70e85464bd7a898f7de6cdce8ea86ddc23aae15d2c390813505429` |
| `webui/gen_header.py` | `8d35249e705414c590d4cb93763e2e12260e6cf57f2479f0442ae54b1ee1fa6c` |
| `webui/index.html` | `ae1da06622f0db26b70f2d05b6b5a9f3494071b7b6cabbc8efbc9be27f24d00a` |

Pruebas (`tests/host/`, copiadas a `tests/host/flexmedia/`):

| Archivo | SHA-256 del original |
|---|---|
| `test_qr.cpp` | `239554796572ddde8eadad26793f6484c0ad0c57b6486ae4386213a2f68bf869` |
| `test_httpshare.cpp` | `ab2c5b37a3a22276f9f070597aa11ca81d4ee341a867c81c0c6f7ca84a0f36a0` |
| `test_media.cpp` | `230546a0fc933e589020c8b5dd94b1ed01e7258bfa020ace7c96288e39ba43cb` |
| `test_medialib.cpp` | `dcd1a03e1c92843e4c35df845bed8db4a3edb3cb883d5b2573d8a96c7271aa4a` |
| `test_jpegenc.cpp` | `40a19824d7058a4e5273f95a520b29044b4c729752f14fe3e8894ca4400aa4da` |
| `test_mediathumb.cpp` | `c14b8f7e2bd4772b760660699eea83a4dc57b87e0f76b7d4267120353069b07a` |
| `test_mediastore.cpp` | `e651b597fadf488a3e77e55f265dedc477d9f821fe77ce4de888242290a3d3fa` |
| `test_mediaweb.cpp` | `f2bceda992d5d59ae5f010febe1b86d67d652b9d59fceb824a0a2acc86520f6f` |

Las pruebas solo cambian en las rutas de `#include` y de las muestras (`fixtures/fm/`) y en cuatro expectativas,
marcadas `RELOJ:`, donde el reloj hace otra cosa a propósito (§14.2).

### 14.2 Qué cambia en el reloj y por qué

- **Decodificador JPEG.** El del P4 (`FlexOS_JPEG.cpp`) **no se porta**: su IDCT parece derivada de la del IJG sin
  atribución (`docs/AUDITORIA-FASE0.md` §4). En su lugar, `flex_jpeg_tjpgd.c` implementa el mismo contrato
  (`FlexOS_JPEG.h`, mismos nombres y códigos de error) sobre TJpgDec:
  - en el reloj, el de la **ROM** del ESP32-S3 (sin código ni Flash añadidos; escala 1/2, 1/4 y 1/8);
  - en el PC, el de LVGL.

  Dos diferencias con el decodificador del P4:
  - TJpgDec no admite el submuestreo 4:4:0: esa foto se rechaza limpia con `FLEXJPG_ERR_UNSUPPORTED` y la galería la
    marca como no compatible;
  - los JPEG MJPEG sin tabla Huffman (`DHT`), habituales en cámaras, reciben la tabla estándar (ITU-T T.81 anexo K)
    antes del `SOS`, como hacen los decodificadores de escritorio.
- **MP3.** El P4 no reproduce MP3 y el reloj sí (§5). Por eso:
  - `FML_F_MP3` es reproducible;
  - el servidor comprueba cada MP3 subido con `flex_mp3.c` (tramas MPEG reales, tres seguidas que concuerden) y
    rechaza con `422` uno sin audio, en vez de guardarlo como «original no reproducible».
- **Identidad del dispositivo.** `/api/hello` añade el nombre, el identificador público y el tipo del dispositivo,
  los destinos de almacenamiento y la caducidad del código. Toda petición que cambia algo puede llevar
  `X-Flex-Dev`; si no es la del reloj, se contesta `409` (`wrongDevice`) y no se escribe nada: una pestaña abierta
  para otro dispositivo Flex no puede subir aquí por error (§9).
- **Caducidad del código.** El código de 6 cifras caduca (`410`, `expired`); en el P4 vive mientras la hoja está
  abierta.
- **Carpetas.** El almacén conoce además `/Grabaciones` (grabadora) y `/DCIM` (fotos de cámaras en la microSD).
- **Límites.** `flexMlSetLimits()` permite cambiar el tamaño máximo por tipo, pero el reloj **no lo llama**: usa los
  del P4 (6 MB por foto, 8 MB por vídeo o audio, `FML_LIMIT_*`), también hacia la microSD (§9.4).

### 14.3 Pruebas

```
make -C tests/host flexmedia LVGL_DIR=… IDF_PATH=…
```

Compila cada prueba con ASan y UBSan, igual que en la referencia. Resultado de la última ejecución (PC):

| Prueba | Comprobaciones | Fallos |
|---|---:|---:|
| `test_mp3` (nueva, del reloj) | 36 | 0 |
| `test_id3` (nueva, del reloj) | 34 375 | 0 |
| `test_qr` | 171 | 0 |
| `test_httpshare` | 154 | 0 |
| `test_media` | 2 456 | 0 |
| `test_medialib` | 19 965 | 0 |
| `test_jpegenc` | 137 | 0 |
| `test_mediathumb` | 39 | 0 |
| `test_mediastore` | 176 | 0 |
| `test_mediaweb` (16 del reloj: identidad, caducidad, destino) | 202 | 0 |
| `webui_media_check.js` (Node: la página aplica el perfil del reloj) | 1 | 0 |

El componente compila para el ESP32-S3 con ESP-IDF 5.5.5, sin avisos, y está conectado a la interfaz (§2-§9).
Nada de esto se ha probado todavía en el reloj (§13).
