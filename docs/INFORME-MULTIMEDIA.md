# Informe final: audio, grabadora, reproductor, Galería universal, microSD y Liquid Glass

Rama `ccr-9b959676-ggdz3t`, 16 commits sobre `587442a` (desde `52a296b`). Detalle técnico en
`MULTIMEDIA.md`, `GALERIA.md` §13-§14 y `LIQUID-GLASS.md` §15.

**Cómo leer los estados**

| Estado | Significado |
|---|---|
| **Implementado y probado en hardware** | Funcionando en el Waveshare ESP32-S3-Touch-AMOLED-2.06. **Nada de este trabajo lo está todavía**: se hizo sin el reloj. |
| **Probado únicamente en host** | Pruebas de PC (ASan/UBSan; la Galería también TSan) y vista previa de LVGL. Mismo código que el firmware; además, el firmware **compila** para el S3 con ESP-IDF 5.5.5 sin avisos y sin marcos de pila de más de 3072 B. |
| **Pendiente de validación** | Necesita el reloj: lo que depende del hardware real (códecs, micrófonos, tarjeta, Wi-Fi, rendimiento). Las pruebas están en `MULTIMEDIA.md` §13 (T219-T252). |
| **No implementado** | No está hecho; se dice por qué. |

No se ha tocado el firmware del ESP32-P4/C6, sus repositorios, ramas ni su servidor. No se ha usado Arduino. No se
han añadido vibraciones. No se ha quitado ninguna función de Flex OS.

## 1. Diagnóstico y crashes

| Hallazgo | Causa | Corrección | Estado |
|---|---|---|---|
| La Galería reiniciaba el reloj "en ocasiones" al abrirse | Desbordamiento de pila: dos funciones con tablas de 8 KB en la pila de la tarea `watch_gal` (12 KB), medido con `-fstack-usage` | Tablas al montón; `check_frames.py` para todo el firmware; `gal_race.c` con TSan (3 carreras benignas corregidas) | Probado únicamente en host · pendiente T151 |
| Música solo mandaba al teléfono; micrófonos, altavoz y microSD sin usar | No había servicios | `watch_audio`, `watch_media` | ver §4-§6 |
| Fondo desenfocado a bloques en Reproduciendo | Redondeo a RGB565 en cada paso; escala no entera de LVGL distinta según la franja | Cadena en 8 bits + tramado; escala exacta x4 | Probado únicamente en host |
| Confirmaciones que caducaban al instante (descartar grabación, formatear y, ya existente, "Terminar" entrenamiento) | Se guardaba `lv_tick_get() \| 1`, que podía quedar 1 ms en el futuro: `lv_tick_elaps` lo leía como una vuelta | Nunca un instante futuro | Probado únicamente en host |

## 2. Pines (verificados en el BSP oficial, el esquema y el firmware de fábrica)

| Función | Pines |
|---|---|
| I2C (códecs, PMIC, IMU, RTC, táctil) | SDA GPIO15, SCL GPIO14 |
| I2S (`I2S_NUM_1`) | MCLK 16, BCLK 41, LRCK 45, DOUT 40, DIN 42 |
| Altavoz | ES8311 (I2C 0x18) + NS4150B, enable GPIO46 (pull-down 10 kΩ) |
| Micrófonos | ES7210 (I2C 0x40), TDM 4 ranuras: 0 MIC1, 1 MIC3 (eco), 2 MIC2, 3 MIC4 (no conectado) |
| microSD (SDMMC 1 bit) | CLK 2, CMD 1, D0 3, DAT3/CS 17 (pull-up); **sin pin de detección** |
| Pantalla / táctil (sin cambios) | QSPI CS 12, CLK 11, D0-D3 4-7, RST 8, TE 13 · FT3168 0x38, RST 9, INT 38 |

Estado: datos **verificados documentalmente**; su uso por el firmware nuevo **pendiente de validación** (T219, T226, T230).

## 3. Flash: 16 MB frente a 32 MB

El chip es un GD25Q256 (32 MB). Los ejemplos de Waveshare declaran 16 MB por prudencia; el firmware de fábrica usa
la zona alta. Se declaran **32 MB sin la opción experimental**: los 16 MB bajos para código (dos ranuras OTA de
6 MB) y los 16 MB altos solo para datos (`storage`, el LittleFS del reloj). ESP-IDF 5.5.5 usa comandos de 4 bytes
con este chip. Detalle en `FLASH-32MB.md`.

Estado: **diseñado y compilado**; la lectura por encima de 16 MB **pendiente de validación** (T10). Si falla, el
LittleFS no se monta y todo lo multimedia sigue funcionando con la microSD.

## 4. Almacenamiento y microSD

- **Dos volúmenes** (reloj y microSD) y un **destino elegido** ("Guardar en") que nunca cambia solo: si no está
  listo, grabar, recibir y descargar se detienen con el motivo. **Probado únicamente en host.**
- **Biblioteca única** por volumen (la de Flex OS Ultra), miniaturas persistentes, catálogo con escritura atómica y
  copia `.bak`; lo recibido se publica entero y comprobado; nada se borra solo. **Probado únicamente en host.**
- **microSD**: estados ABSENT / DETECTING / MOUNTING / READY / BUSY / ERROR / REMOVING, detección por sondeo,
  arrendamientos (no se desmonta con un archivo abierto), FAT32 hasta 2 TB, exFAT reconocida y explicada, formatear
  con doble confirmación y ticket de un solo uso, expulsar y volver a montar. **Probado únicamente en host** (con un
  controlador simulado aleatorio) · **pendiente de validación** T219-T225.
- **No implementado**: montar exFAT (ESP-IDF 5.5.5 se compila sin exFAT ni LBA de 64 bits).

## 5. ES7210 y ES8311

- `watch_audio` es el **único dueño** del I2S y de los dos códecs, con el perfil de fábrica (24 kHz, TDM de 4
  ranuras, ganancia 24 dB). La grabadora tiene prioridad: pausa la música y no la reanuda sola. El amplificador lo
  maneja `esp_codec_dev` con la salida abierta y se mantiene apagado grabando.
- Estado: **compilado**; lógica **probada únicamente en host**; sonido y micrófonos **pendientes de validación**
  (T226-T232).

## 6. Grabadora y reproductor

| Pieza | Qué hace | Estado |
|---|---|---|
| Grabadora (app nueva, icono propio) | WAV 16 bits a 24 kHz, mono en el reloj, mono o estéreo en la microSD ("Estéreo (microSD)" en Grabaciones); nivel en vivo, pausa, descartar con doble toque, lo que cabe; anillo de 128 KB; recuperación de grabaciones cortadas | Probado únicamente en host · pendiente T226-T229 |
| Música | Biblioteca local de los dos volúmenes, Grabaciones, "En el teléfono" (el mando de siempre), "Añadir desde el móvil", "Guardar en" | Probado únicamente en host |
| Reproduciendo | Portada, título, artista, formato, barra con salto, anterior/siguiente, volumen guardado, fondo desenfocado de la portada | Probado únicamente en host · pendiente T230-T233 |

## 7. Formatos admitidos

| Tipo | Se abre/reproduce | Se guarda (con el motivo de por qué no se abre) |
|---|---|---|
| Foto | JPEG baseline (TJpgDec de la ROM; 4:4:0 no) | PNG, GIF, WebP, HEIC, AVIF, JPEG progresivo |
| Vídeo | **AVI MJPEG, sin sonido** | MP4, MOV, WebM, MKV, AVI de otros códecs |
| Audio | WAV PCM 8/16 bits, WAV IMA ADPCM, **MP3** capa III (minimp3, 28/28 vectores ISO) | AAC, M4A, FLAC, OGG |
| Etiquetas | ID3v2 (título, artista, álbum) y portada JPEG | — |
| Dibujos | FXP de Paint | — |

La página del móvil convierte lo que el reloj no abre (fotos a 1024/1280/1600 px, vídeo a AVI MJPEG de 320/360/400 px,
audio a WAV) usando el perfil que el reloj le manda. Estado: **probado únicamente en host**.

## 8. Galería y "+"

- La barra **Todas / Reloj / (fuente) / Más opciones** se sustituye por **Fotos / Vídeos / +**. Cada pestaña junta
  reloj, microSD (marca "SD") y la fuente remota (marca de la fuente). **Probado únicamente en host.**
- **"+"**: Recibir con QR, Desde el navegador, Flex OS Ultra, Flex Cloud, Flex Phone, Origen, Guardar en,
  Transferencias y Música; nada se ha quitado, cambia de sitio. **Probado únicamente en host.**
- Lo local sale de la biblioteca (reloj + microSD) con miniaturas persistentes, lectura con arrendamiento, papelera
  (reloj) o borrado con doble toque (microSD) y descargas al destino elegido. **Probado únicamente en host**
  (409 comprobaciones del motor) · **pendiente** T234-T240.
- Vídeos AVI MJPEG del reloj o la microSD se reproducen al tocarlos. **Probado únicamente en host** · **pendiente**
  T241-T244.

## 9. Flujo QR, servidor y Flex Phone

1. Galería › + › Recibir con QR (o Música › Añadir desde el móvil): arranca la tarea `wm_web` (16 KB de pila en
   PSRAM) con **el mismo Flex Web Server de Flex OS Ultra** (no uno nuevo) en el puerto 8080.
2. El reloj enseña el QR `http://<IP>:8080/?k=<código>`, el código, su caducidad (10 min), la dirección y el destino.
   Escanear es emparejar.
3. La página convierte y sube por trozos; el reloj valida (foto decodificada entera, MP3 con tramas, AVI con su
   primer fotograma) y publica en el destino elegido; enseña el progreso.
4. Seguridad: código con caducidad e intentos limitados, cookie `HttpOnly`/`SameSite=Strict`, `X-Flex` (CSRF),
   `Host` (DNS rebinding), **identidad del aparato** (`X-Flex-Dev`: si la IP la tiene otro aparato Flex, 409 y no se
   escribe nada), "Código nuevo", "Desconectar móviles"; sin registrar códigos ni sesiones.
5. Vida: una conexión a la vez; se apaga al salir de la pantalla (terminando lo que está en marcha) o tras 15 min
   sin peticiones.

**Flex Phone**: en "+", la fuente Flex Phone es el Flex Cloud del teléfono **a través del P4** (lo de antes). Enviar
desde el móvil al reloj se hace con el QR (el navegador del móvil).

Estado: servidor **probado únicamente en host** (202 comprobaciones, 16 nuevas del reloj); pantalla con QR
**probada únicamente en host**; sockets, Wi-Fi y móvil reales **pendientes de validación** (T245-T252).
**No implementado**: transferencia directa desde la app Flex Phone de Android al reloj (Flex Storage empareja un solo
P4; usar el QR no lo necesita) y TLS en la red local (igual que el P4).

## 10. Liquid Glass y fantasmas

- Todas las pantallas nuevas usan los componentes del motor de vidrio.
- **Desenfoque real desde la fuente** en Reproduciendo: la portada desenfocada en un búfer propio (26 KB), en 8 bits
  con tramado, dibujada a escala exacta x4; nunca lee la pantalla.
- **Fantasmas**: la batería `rastros` (búferes parciales dobles, redondeo del CO5300, franjas de 54/40/16 líneas,
  dos fondos) cubre Reproduciendo, Desde el móvil y Vídeo: **940 cuadros por caso, 0 rastros**. Además, cada
  pantalla nueva se compara con su redibujado completo y no redibuja nada en reposo.

Estado: **probado únicamente en host** · **pendiente** T233 (y T205-T207 del vidrio general).

## 11. Compilación, pruebas y memoria

| Comprobación | Resultado |
|---|---|
| Firmware ESP32-S3, ESP-IDF 5.5.5 | Compila, **0 avisos**; imagen 3 926 720 B en ranura de 6 MB (37 % libre); DIRAM 52,9 % |
| Marcos de pila | Ninguno pasa de 3072 B (2603 medidas; `flexWebServeConn` admitida en `wm_web` de 16 KB) |
| Pruebas de host | 1 000 025 comprobaciones, 0 fallos (lógica del reloj) + Galería 409 + flexmedia 57 711 + Galería con dos hilos (TSan) |
| Vista previa (LVGL, ASan) | Todas las baterías, 0 fallos (incluidas `medios`, `galeria`, `rastros`, `reposo`, `mono`) |

Memoria (PSRAM, solo mientras se usa): catálogo 126 KB por volumen; grabadora 144 KB; portada 80 KB + fondo 26 KB;
QR ≤ 31 KB; servidor ~38 KB; vídeo 2 lienzos + 144 KB (400 x 225: ~500 KB). `wm_alloc` nunca deja menos de 768 KB
libres. Detalle en `MULTIMEDIA.md` §11.

## 12. Archivos modificados (175 archivos, 16 commits)

| Zona | Archivos | Qué |
|---|---:|---|
| `components/flexmedia` | 30 nuevos | Pila de medios y Flex Web Server de Flex OS Ultra (huellas SHA-256 en `MULTIMEDIA.md` §14), MP3, ID3, web del móvil |
| `components/minimp3` | 4 nuevos | Decodificador MP3 (CC0) |
| `components/watch_media` | 19 nuevos | Volúmenes, microSD, biblioteca, servidor web (`wm_web`), vídeo (`wm_video`) |
| `components/watch_audio` | 9 nuevos | Códecs, reproductor, grabadora |
| `components/watch_gallery` | 12 (3 nuevos) | E/S local opcional del motor, `gal_wm*`, fallo de pila |
| `components/watch_ui` | 15 (5 nuevos) | Grabadora, Música, Reproduciendo, microSD, Guardar en, Desde el móvil, Vídeo, Galería |
| `components/watch_theme` | 9 | Iconos nuevos, icono de la Grabadora, desenfoque desde la fuente |
| `components/watch_settings` | 3 | Destino, volumen y calidad de grabación |
| `main`, `sdkconfig.defaults`, `CMakeLists.txt`, iconos | 7 | Arranque de los servicios, FATFS, medida de pilas |
| `tests/host` | 49 (42 nuevos) | Pruebas y muestras |
| `tools/sim`, `tools/stack` | 11 | Vista previa y marcos de pila |
| `docs` | 7 (2 nuevos) | Este informe, `MULTIMEDIA.md`, `GALERIA.md`, `LIQUID-GLASS.md`, `ROADMAP.md`, `PRUEBAS-RELOJ.md`, `INVENTARIO-APPS.md` |

## 13. Limitaciones y pruebas físicas

**Limitaciones**

- Nada probado aún en el reloj.
- Vídeo sin sonido y solo AVI MJPEG.
- exFAT no se monta: hay que formatear en FAT32 (borra la tarjeta; se pide dos veces).
- HTTP sin TLS en la red local (como el P4): cualquiera en la misma Wi-Fi que vea el QR puede emparejarse mientras
  el código no caduque.
- La Galería enseña hasta 200 elementos locales; las descargas pasan por el LittleFS antes de llegar a la microSD.
- Sin contenido protegido desde la web del reloj (no hay PIN de sistema).
- Límites por archivo recibido: 6 MB por foto, 8 MB por vídeo o audio.

**Pruebas físicas, en este orden** (detalle y qué anotar en `MULTIMEDIA.md` §13):

1. T10 (Flash por encima de 16 MB) y T151 (abrir y cerrar la Galería 20 veces).
2. T219-T225: microSD (meter, expulsar, sacar en caliente, exFAT, formatear, 128 GB+, destino ausente).
3. T226-T229: grabadora (micrófonos, 30 min, corte de alimentación, con música).
4. T230-T233: altavoz y Reproduciendo.
5. T234-T244: Galería con la microSD y vídeo.
6. T245-T252: recibir desde el móvil (QR, caducidad, cambio de IP, dos aparatos Flex, desconectar, apagado solo).

Si algo reinicia: copiar el registro completo del monitor (T149) y el volcado de memoria.
