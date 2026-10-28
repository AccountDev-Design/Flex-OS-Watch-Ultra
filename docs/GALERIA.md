# Galería (Flex Gallery) en Flex OS Watch Ultra

Estado: **compilada con ESP-IDF 5.5.5 y probada en el PC. Todavía no se ha probado en el reloj.** Las pruebas
de PC son estas:

- el motor contra un ecosistema en memoria;
- el motor contra los servidores reales (el Flex Web Server de Flex OS Ultra compilado en el PC y el servidor Node de
  Flex Cloud);
- la interfaz en la vista previa de LVGL.

Las pruebas físicas son T151-T166 (§10).

La Galería del reloj **no tiene nube, cuenta, biblioteca ni servidor propios**. Enseña:

- las fotos del reloj (LittleFS);
- las de **Flex OS Ultra**, a través de su Flex Web Server, como cualquier navegador emparejado;
- las de **Flex Cloud**, con la misma Flex Account, API y cuota que Flex OS Ultra y Flex Cloud Web;
- las del **teléfono**, a través de la pasarela del P4.

## 1. Repositorios y ramas

| Repositorio | Rama / commit | Uso |
|---|---|---|
| `AccountDev-Design/Flex-OS-Watch-Ultra` | `ccr-922b787f-quazgs`. Contiene `ccr-b6cde864-7hvm3w` más F10 y F11, y se comprobó antes de empezar | **todo el código nuevo** |
| `AccountDev-Design/Flex-OS-Ecosistema` | `claude/flexos-ota-system-blq9g9-2csacx` @ `5dbc949`, carpeta `FlexOS_Ultra/` | solo lectura: contratos del P4. La prueba de extremo a extremo compila su servidor fuera del repositorio y comprueba que `git status` no cambia |
| `AccountDev-Design/Flex-Developer-Studio` | `claude/flexos-ota-system-blq9g9-2csacx` @ `8e17030`, carpeta `cloud/` | solo lectura: API de Flex Cloud y Flex Account. Su servidor Node se ejecuta en modo desarrollo en la prueba de extremo a extremo |

`flexos-esp-idf` (la migración pausada del P4) no se usa. No se ha modificado ningún repositorio de referencia.

## 2. Arquitectura encontrada en Flex OS Ultra

- **Galería del P4** (`FlexOS_Ultra_AppGallery.h`, `FlexOS_Ultra_MediaViewer.h`). Lee la biblioteca de medios
  (`FlexOS_MediaLib`: catálogo con revisión, miniaturas en `FlexOS_MediaThumb` y almacén en `FlexOS_MediaStore`). La
  edición está en `GalleryEdit.h` y `GalleryVideoEdit.h`.
- **Flex Web Server** (`FlexOS_MediaWeb.cpp`, puerto `FLEXWEB_PORT` 8080, `FlexOS_Ultra_WebServer.h`). Es el servidor
  que ya usan el móvil y el PC: hoja «Conectar con el móvil», código de 6 cifras y sesión con la cookie `fxs`. Sus
  defensas:
  - comprueba `Host` (ip:puerto);
  - pide `X-Flex: 1` en todo lo que cambia algo (CSRF);
  - deja 5 intentos de código;
  - lo protegido exige nivel de propietario.

  Registra Flex Storage y la papelera (`FlexOS_Ultra_WebServer.h:385-388`).
- **Flex Storage del teléfono.** La app Flex Phone sirve `CloudServer.kt`. El P4 lo expone en `/api/cloud` como
  pasarela. El teléfono guarda **un solo P4** emparejado: `session()` rechaza cualquier `p4Id` que no sea el suyo.
- **Flex Account** (`FlexOS_Account.cpp`). El dispositivo pide un código con `POST /api/devices/code`:
  - el cuerpo lleva `hardwareId`, `label`, `model`, `flexVersion` y `tokenHash` (solo el SHA-256 de una credencial
    aleatoria);
  - la respuesta es un sobre firmado ES256 con una clave anclada (`ACCOUNT_KEY_ID`);
  - la persona escribe el código en la web y el dispositivo pregunta cada 3 s durante 10 minutos;
  - la credencial se valida con `GET /api/cloud/me` (Bearer).
- **Flex Cloud** (`FlexOS_Cloud.cpp` en el P4; `cloud/src/http/app.js` en Flex Developer Studio):
  - `/api/cloud/health`, `/me` (cuenta y cuota) y `/files` (recientes, `kind=media`, cursor);
  - miniatura por archivo;
  - descarga con `Range`/`If-Range` y ETag = SHA-256;
  - subidas reanudables por partes con `X-Part-SHA256`;
  - papelera.

  La cuota es la del plan del servidor (`DEFAULT_PLANS.free` = 5 GiB en `cloud/src/config.js`). Flex Cloud Web sube
  sus fotos con una miniatura JPEG de 320 px.

`/v1/health` existe solo en el **servicio de navegación** (`server/src/server.js`, FBP/1), que es otro servicio. Ni el
P4 ni Flex Cloud lo tienen. Por eso la Galería comprueba la salud con lo que sí existe: `GET /api/hello` (P4),
`GET /api/cloud/health` + `/me` (Flex Cloud) y `GET /api/fs/overview` (el teléfono, por el P4).

## 3. Arquitectura en el reloj

```
ui_gallery.c (tarea de LVGL)          solo pide y lee instantáneas (gal_status, gal_list, gal_view_get...)
      │  cerrojo corto
watch_gallery.c (tarea "watch_gal")   esp_transport TCP (P4) y TLS verificado con el paquete de CA (Flex Cloud),
      │                               NVS "flexacct"/"flexgal", PSRAM, Wi-Fi de watch_net
gal_engine.c  ── fuentes, Automático, listas, cola de transferencias, miniaturas, visor
 ├─ gal_net.c    clientes del Flex Web Server, de Flex Cloud y de la pasarela del teléfono
 ├─ gal_acct.c   Flex Account (puerto a C de FlexOS_Account.cpp)
 ├─ gal_local.c  /Imagenes y /Paint (flexfs), miniaturas persistentes, origen.txt
 ├─ gal_jpeg.c   JPEG en flujo por bandas (TJpgDec de la ROM del S3 con escala 1/2..1/8)
 ├─ gal_http.c   HTTP/1.1 con keep-alive, chunked, Range y cuerpos por flujo
 └─ gal_json.c   lector JSON sin reservas (UTF-8 validado)
```

Todo menos `watch_gallery.c` es C portable. Se compila igual en el reloj, en las pruebas de PC y en la vista previa.
Ninguna tarea de red toca LVGL. La tarea nace al abrir la app y termina sola 2 s después de cerrarla, si no quedan
transferencias, y entonces suelta toda su PSRAM.

Archivos:

- `components/watch_gallery/`: el motor, la capa de ESP-IDF y `gal_rom.ld`, que da otro nombre al TJpgDec de la ROM
  porque LVGL enlaza el suyo con los mismos nombres.
- `components/watch_ui/ui_gallery.c`: la app.
- `ui_paint_thumb()`: miniaturas de dibujos.
- El icono original `IC_GALERIA` en `assets/icons/app_galeria.bin`. Los otros 15 iconos regenerados salen idénticos
  byte a byte.

## 4. El servidor web compartido de Flex OS Ultra

El reloj es **un cliente más** del Flex Web Server del P4, igual que el móvil o el PC. No abre ningún servidor:
`tests/host/gallery_checks.sh` comprueba que no hay `esp_http_server`, `listen`, `bind` ni `accept`.

| Petición | Para qué | Cabeceras |
|---|---|---|
| `GET /api/hello` | salud y estado de la sesión | `Host`, cookie `fxs` |
| `POST /api/pair` | código de 6 cifras → cookie `fxs` (solo en RAM) | `X-Flex: 1` |
| `GET /api/library?since=rev` | biblioteca (chunked); `same` si no cambió | cookie |
| `GET /api/thumb?id=&t=` | miniatura JPEG del P4 | cookie |
| `GET /api/file?id=` | original por flujo, reanudable con `Range` | cookie |
| `POST /api/upload` | subida del reloj al P4 con CRC32 | `X-Flex: 1` |
| `POST /api/local/delete` | a la papelera del P4 | `X-Flex: 1` |
| `GET /api/fs/overview` | ¿hay teléfono listo en Flex Storage? | cookie |
| `/api/cloud/...` | el Flex Cloud del teléfono, por la pasarela del P4 | cookie |

Los contratos se comprueban contra el servidor de verdad (`FlexOS_MediaWeb.cpp` compilado en el PC por
`tests/host/flexweb_host.cpp` de Flex-OS-Ecosistema). La prueba cubre:

- el código malo y los intentos;
- `Host` y CSRF rechazados;
- la biblioteca y la miniatura;
- el visor por flujo;
- una descarga cortada y reanudada;
- la subida con CRC y la papelera.

## 5. Origen de las fotos y conexión

En **Origen** se elige entre Automático, Flex OS Ultra, Flex Cloud y Flex Phone. Flex Phone solo aparece si el P4
dice que tiene un teléfono listo, o si ya estaba elegido. **PC** no aparece: el PC no sirve fotos, solo es otro
cliente del P4 o de la web. No se enseña ninguna opción que no funcione.

Reglas de `gal_pick_resolve` (`gal_core.c`):

- **Sin Wi-Fi:** ninguna fuente remota. Se ven las fotos del reloj y se dice.
- **Elegida a mano:** se usa esa, esté como esté. Se enseña su estado (sin configurar, comprobando, no responde, hay
  que volver a conectar) y **nunca se cambia a otra por detrás**.
- **Automático,** en este orden:
  1. Flex OS Ultra listo (red local, directo);
  2. Flex Cloud listo (Internet);
  3. Flex Phone listo (por el P4).

  La fuente en uso se conserva mientras siga lista. Una de más prioridad que vuelve solo la sustituye tras **10 s
  estable** (`GAL_AUTO_STABLE_MS`). Si ninguna está lista, se usa la que se está comprobando y, si no, la primera
  configurada, con su motivo. La interfaz dice siempre cuál se usa y por qué («Automático: Flex Cloud»).

Conexión:

- **Solo Wi-Fi.** Las fotos no van por BLE. El estado del Wi-Fi sale de `watch_net`.
- **Salud cada 30 s** con la fuente lista. El P4 se vuelve a leer cada 10 s con `?since=rev` (barato si no cambió) y
  Flex Cloud cada 60 s.
- **Plazos por petición:** 6 s para la salud y 10 s para conectar.
- **Fallos:** espera de 2, 4, 8, 16, 32 y 60 s. Una transferencia se da por fallida tras 6 fallos seguidos
  (`GAL_XFER_TRIES`) y se puede reintentar.
- **Conexiones:** keep-alive con una conexión por destino. En la prueba de extremo a extremo fueron 36 peticiones por
  8 conexiones.
- **Un corte del propio reloj** (sin Wi-Fi, abrir el visor) no cuenta como fallo de la fuente, que se recupera sola al
  volver la red. Esto se corrigió durante la vista previa: antes quedaba «No responde: Cancelado» con espera larga.
- **Nada se simula.** Sin servidor no hay lista, ni miniaturas, ni «hecho»: se dice por qué.

## 6. Flex Cloud: misma cuenta, misma biblioteca

- **Vinculación.** Es la de `FlexOS_Account.cpp` pasada a C (`gal_acct.c`): código, sobre ES256 verificado con la
  clave anclada de la referencia y validación con `/me`. Es la misma Flex Account de Flex OS Ultra y Flex Cloud Web:
  ni login, ni contraseña, ni otra cuenta.
  - El reloj se identifica como `model` `ESP32-S3`, `hardwareId` `S3-<MAC>` y `label` «Flex OS Watch Ultra», para que
    la cuenta lo distinga del P4.
  - Todo va por TLS **verificado** (el P4 usa `setInsecure()` para el código).
- **Credencial.** Son 32 bytes aleatorios generados al vincular. Solo sale su SHA-256 y, después, la cabecera Bearer
  hacia Flex Cloud. Se guarda en la NVS `flexacct`, con las mismas claves y el mismo orden que el P4: la marca
  `linked`, la última. Nunca está en el código, en el repositorio, en la pantalla ni en el registro, y se borra de la
  pila al usarla. Desvincular la borra.
- **Errores.** Un 401/403 sin código reconocido (la página de un proxy) **no** desvincula. `device_revoked` lleva a
  «vuelve a vincular».
- **Biblioteca.** Es la de la cuenta: `/files?kind=media`, paginada. Lo que sube el reloj aparece en la web con la
  misma huella SHA-256 (comprobado contra el servidor real).
- **Cuota.** La del servidor, leída de `/me`: 5 GiB en el plan `free` por defecto. El firmware no tiene ningún límite
  propio.
- **Descargas.** Por flujo a `/System/Galeria/tmp/*.part`, reanudables con `Range`/`If-Range`, con el SHA-256
  comprobado antes de pasar a `/Imagenes`. `origen.txt` apunta de qué fuente vino cada copia para enseñar su origen y
  no duplicarla. No es otra biblioteca: es la marca de las copias locales.
- **Subidas.** Por partes reanudables con `X-Part-SHA256`. Una subida cortada se reanuda donde estaba.

## 7. Seguridad

- No hay servidor en el reloj ni puertos abiertos.
- No hay credenciales en el firmware ni en el repositorio (`gallery_checks.sh`). Las únicas constantes largas son
  públicas: el alfabeto base64url y el keyId de la clave ES256 pública de la referencia.
- La sesión del P4 (cookie `fxs`) vive solo en RAM y caduca con el P4. Lo protegido del P4 ni se lista con nombre, ni
  se pide su miniatura, ni se abre, ni se descarga.
- El único servicio remoto escrito en el código es el de la referencia
  (`flex-developer-studio.ralvarezsantos980.chatgpt.site`). El P4 es la dirección que escribe la persona: IPv4 o
  nombre, comprobados.
- El registro dice memoria, pila y número de peticiones. Nunca dice credenciales, cookies ni rutas con datos de la
  cuenta.

## 8. Memoria (medida)

La memoria está en PSRAM y solo con la app abierta. Antes de reservar lo grande se comprueba que el mayor bloque
libre de PSRAM cabe con 64 KB de sobra y que queda 1 MB libre para el resto del sistema. Si no cabe, se dice y no se
intenta.

| Parte | Bytes | Cómo se midió |
|---|---|---|
| motor con las dos listas (200 + 200 elementos) | 180 400 en el S3 (180 512 en el PC) | `sizeof` con el compilador Xtensa |
| respuestas JSON | 163 840 | `GAL_JSON_MAX` |
| miniaturas (30 × 112 × 112 × 2) | 752 640 | `gal_engine_mem` |
| visor 4:3 encajado (410 × 308 × 2) | 252 560 | prueba de PC |
| visor de una foto de 1600 × 1200: encajada + 2x (820 × 615) | 252 560 + 1 008 600 | extremo a extremo |
| huellas de una subida (32 B por parte, ≤ 512 partes) | ≤ 16 384 | `GAL_UP_PARTS_MAX` |
| lista de la pestaña en la interfaz | 352 por elemento (≤ 141 KB) | `sizeof(gal_item_t)` |
| pila de la tarea (RAM interna) | 12 288 | `TASK_STACK`; el mínimo libre se registra al cerrar |

- **Originales.** Nunca están enteros en RAM. Se decodifican por bandas desde el flujo: una foto de 4000 × 3000 de
  2-3 MB se lee sin guardarla. Las miniaturas no necesitan el original en memoria.
- **Tamaño del firmware.** La imagen pasa de 2 541 511 B a 2 655 823 B (+114 312 B, de los que 25 392 son el icono).
  DIRAM pasa de 170 187 B a 170 403 B (+216 B).
- **Cierre.** Al cerrar la app el motor se libera entero. La vista previa comprueba 0 B y 0 bloques del motor vivos,
  los mismos bloques de LVGL y los mismos temporizadores.

## 9. Interfaz

- **Rejilla.**
  - 3 columnas de 112 px y pestañas Todas / Reloj / la fuente remota con su punto de estado.
  - Encima, la línea de origen, que lleva a Origen al tocarla.
  - Es **virtual**: 15 celdas fijas que se recolocan al desplazar, así los objetos y las miniaturas fijadas no crecen
    con el número de fotos.
  - Marcas de origen (Reloj, Ultra, Cloud, Phone), ✓ «ya en el reloj» y ▶ para vídeo.
  - Lo que no tiene miniatura dice por qué: PNG, Protegido, Sin miniatura.
  - Estados vacío, cargando, error con «Reintentar», sin Wi-Fi y sin almacenamiento.
  - Al llegar al final se pide la página siguiente.
- **Visor.**
  - Pantalla completa sobre negro.
  - Deslizar para la siguiente o la anterior, doble toque para el 2x y arrastrar para moverse por él.
  - Un toque enseña u oculta las capas opacas (nombre arriba; origen y medidas reales abajo, que llevan a
    Información).
  - Carga con porcentaje.
  - Los vídeos, PNG o protegidos dicen por qué no se ven. Los dibujos se abren en Paint.
- **Información.**
  - Tamaño, medidas, fecha, formato y estado.
  - Solo las acciones posibles ahora (`gal_can`): Descargar al reloj, Subir a Flex OS Ultra / Flex Cloud /
    Flex Phone, Mover a la papelera (con un segundo toque), y el motivo de lo que no se puede.
- **Origen.**
  - Las opciones con su estado.
  - Conectar y Olvidar Flex OS Ultra: dirección y código con un teclado numérico.
  - Flex Account: código grande para la web, estado, cuota real y Desvincular (con un segundo toque).
- **Transferencias.**
  - En la cabecera, el número y el porcentaje, o el aviso de fallo.
  - La lista con Cancelar, Reintentar y Quitar las terminadas.

Estilo Liquid Glass del sistema: tarjetas y mosaicos opacos sobre negro y las fuentes Outfit, sin desenfoques ni
capas translúcidas a pantalla completa. Las únicas animaciones son las de navegación del sistema. El verificador
visual de la vista previa no encuentra solapes, recortes, textos truncados ni contenido en las esquinas en ninguna de
las pantallas. El redibujado en reposo es 0 px en 5 s.

## 10. Pruebas

### Resultados (compilación y PC)

| Prueba | Resultado |
|---|---|
| Firmware con ESP-IDF 5.5.5 (`tools/offline/build_offline.sh build`) | compila sin avisos; `idf.py size`: imagen 2 655 823 B, DIRAM 170 403 B; el TJpgDec de la ROM queda en `0x40000858`/`0x40000864` (`esp32s3.rom.ld`) y el de LVGL en flash |
| `make -C tests/host` (ASan/UBSan) | 3019 comprobaciones del firmware + **344 de la Galería**, 0 fallos |
| `tests/host/gallery_checks.sh` | sin servidor propio, solo el servicio de la referencia, sin credenciales en el código: OK |
| `make -C tests/host gallery-e2e` contra el **Flex Web Server real** (5dbc949) y el **servidor real de Flex Cloud** (8e17030, modo desarrollo) | **40 comprobaciones, 0 fallos** |
| Vista previa `tools/sim` completa (todas las baterías, más «galeria», «reposo» y «mono» con la Galería) | TODO OK, 0 fallos |
| Vista previa con ASan/UBSan (`SAN=1`): batería «galeria» | OK, 0 fallos, sin avisos de los sanitizadores |

Lo que cubren, por prueba pedida:

1. **Selección de origen.** Automático, explícita, estabilidad de 10 s, sin teléfono no se ofrece y PC no aparece.
   Pruebas de PC y vista previa.
2. **Desconexión y recuperación.** Sin Wi-Fi, P4 apagado y vuelta sola. Pruebas de PC y vista previa.
3. **Autenticación.** Código malo, `Host` o CSRF rechazados, sesión caducada, credencial revocada → volver a vincular.
   Pruebas de PC y extremo a extremo.
4. **Enumeración y miniaturas.** Del P4, de Flex Cloud Web (JPEG de 320 px) y del reloj. Todas.
5. **Transferencias.** Cortadas y reanudadas (`Range`), integridad (SHA-256 de Flex Cloud, CRC32 hacia el P4) y
   reintentos limitados. PC y extremo a extremo.
6. **Errores HTTP y respuestas incompletas.** Cortes en cabeceras y cuerpo, chunked, 503, JSON inválido y UTF-8 malo.
   Pruebas de PC.
7. **Memoria acotada y liberación.** Límites de PSRAM simulados, sin fugas y motor liberado al cerrar. PC y vista
   previa.
8. **Navegación sin solapes.** Verificador visual en cada pantalla de la batería «galeria».
9. **Compatibilidad.** Contra el servidor real del P4 y el de Flex Cloud.
10. **Sin servidores ni bibliotecas duplicados.** `gallery_checks.sh` y revisión del diseño (§4 y §6).

Comandos:

```bash
make -C tests/host LVGL_DIR=/ruta/lvgl-9.5.0 IDF_PATH=/ruta/esp-idf
REF_DIR=/ruta/Flex-OS-Ecosistema CLOUD_DIR=/ruta/flex-developer-studio/cloud \
  LVGL_DIR=... IDF_PATH=... make -C tests/host gallery-e2e
make -C tools/sim LVGL_DIR=... && SIM_ONLY=galeria tools/sim/build/sim tools/sim/out
```

### Pendiente de probar en el reloj (T151-T166)

| # | Qué hacer | Qué debe pasar |
|---|---|---|
| T151 | Abrir la Galería con fotos en `/Imagenes` y un dibujo en `/Paint` | Miniaturas y dibujo. El registro `watch_gallery` dice la memoria al abrir; anotar las cifras |
| T152 | Copiar al reloj un JPEG de 4000 × 3000 y abrirlo | Miniatura y visor sin reinicio. **Es la primera vez que corre el TJpgDec de la ROM**; anotar el tiempo |
| T153 | Origen > Conectar con la dirección y el código de «Conectar con el móvil» del P4; primero un código malo | «Código incorrecto. Quedan N intentos»; con el bueno, «Conectado» y la pestaña Flex OS Ultra |
| T154 | Comparar la pestaña Flex OS Ultra con la Galería del P4 | Mismas fotos; lo protegido sale como «Protegido» y el vídeo con ▶ |
| T155 | Visor: deslizar, doble toque, arrastrar el 2x, tocar | Siguiente/anterior, 2x, se mueve sin dejar ver el fondo y la capa se oculta |
| T156 | Descargar una foto del P4 y apagar el router a mitad | Se reanuda al volver la red; la foto se abre y dice «copia de Flex OS Ultra» |
| T157 | Subir una foto del reloj al P4 (≤ 6 MB) | Aparece en la Galería del P4 |
| T158 | Mover a la papelera una del P4 y otra del reloj | En la papelera del P4 y en `/Papelera` |
| T159 | Con el teléfono emparejado en Flex Storage del P4 | Aparece «Flex Phone» en Origen y sus fotos |
| T160 | Flex Account > Vincular contra el servicio de **producción**; aprobar el código en la web | «Conectada», la cuenta y la cuota reales. Comprueba que el servicio acepta `model` ESP32-S3 |
| T161 | Subir una foto desde Flex Cloud Web; en el reloj, descargarla; subir una del reloj | Se ve con miniatura y se descarga (SHA-256 bien). La del reloj aparece en la web **sin miniatura** (§11) |
| T162 | Automático con el P4 y la cuenta; apagar el P4 y volver a encenderlo | Usa Flex OS Ultra; pasa a Flex Cloud; vuelve al P4 tras 10 s estable |
| T163 | Apagar el Wi-Fi del reloj con la Galería abierta y volver a encenderlo | «Sin Wi-Fi»; las del reloj se siguen viendo; se recupera sola |
| T164 | Ir a la esfera durante una descarga y volver | La descarga termina; al volver, miniaturas otra vez. Al cerrar, el registro «fin: ... pila libre mínima» |
| T165 | Abrir y cerrar la Galería 10 veces, también con el Navegador suspendido | Sin reinicios; Device Care: la PSRAM libre vuelve al valor del principio |
| T166 | Flex Cloud por HTTPS real | Sin errores de certificado; el registro no dice «TLS aplazado» con la memoria normal |

## 11. Limitaciones conocidas

- **Teléfono directo:** no es posible. Flex Storage empareja un solo P4 (`CloudServer.kt`, `session()`), así que el
  reloj llega al teléfono solo a través del P4.
- **Miniaturas de lo que sube el reloj:** el reloj no tiene codificador JPEG, así que sus subidas a Flex Cloud llegan
  sin miniatura. Flex Cloud tiene `PUT /files/:id/thumbnail`, pero la web no genera las que faltan. En el P4 la
  miniatura la hace el propio P4.
- **Integridad de las descargas del P4:** se comprueba por tamaño y marca JPEG. El P4 no publica una huella de sus
  archivos; las de Flex Cloud sí se comprueban por SHA-256.
- **Lo que no se ve:** PNG, GIF, WebP, HEIC y JPEG progresivo (TJpgDec solo hace baseline). Se dice por qué y se
  pueden descargar o subir igual. Vídeos: solo los AVI MJPEG del reloj o de la microSD se reproducen (§14); los
  remotos y los de otros formatos dicen cómo verlos.
- **Sin probar en el reloj:**
  - el TJpgDec de la ROM con escala, que el PC no puede ejecutar (el PC usa el de LVGL, sin escala);
  - TLS con certificados reales;
  - el servicio de Flex Account de producción, que no está en ningún repositorio;
  - si ese servicio acepta `model` ESP32-S3 y `hardwareId` S3-….
- **Vincular en la vista previa:** no se ve la pantalla del código de vinculación, porque la firma ES256 la da el
  servicio. El flujo sí está probado en el PC, con firmas reales hechas con una clave de pruebas.
- **Credencial en la NVS sin cifrar:** igual que en el P4. El cifrado de NVS es F8.3 (❓ D12).

## 12. Cambios propuestos en otros repositorios (no hechos)

1. **Flex Cloud Web** (Flex-Developer-Studio): generar la miniatura de los archivos que llegan sin ella (las subidas
   del reloj), en el navegador al listarlos o en el servidor.
2. **Flex OS Ultra** (`FlexOS_MediaWeb.cpp`): publicar el SHA-256 del original (ETag o `X-Content-SHA256`) en
   `/api/file`, para comprobar las descargas del reloj como las de Flex Cloud. Es compatible hacia atrás.
3. **Servicio de Flex Account de producción:** confirmar que acepta `model` `ESP32-S3` y el `label` del reloj. Si
   filtra por modelo, añadirlo.
4. **Flex Phone** (`CloudServer.kt`): solo si se quisiera el teléfono sin el P4, admitir varios dispositivos
   emparejados. Hoy no hace falta.

## 13. Fallo corregido: reinicio al abrir la Galería

- **Síntoma:** el reloj se reiniciaba "en ocasiones" al abrir la Galería.
- **Causa raíz** (medida con `-fstack-usage` del compilador Xtensa, `52a296b`): `gal__origin_load` (8224 B) y
  `gal__origin_add` (8256 B) declaraban en la pila una tabla de 64 `gal_origin_t`. `gal__origin_load` corre en cada
  apertura, dentro de la tarea `watch_gal` de 12 KB, debajo de `gal_engine_step` (1600 B) y encima de una lectura de
  LittleFS: la pila se desbordaba. Según la profundidad de LittleFS y las interrupciones, el canario lo detectaba
  (pánico) o se corrompía el montón contiguo (fallo diferido). Las pruebas de PC no lo veían: un hilo de PC tiene
  8 MB de pila.
- **Corrección:** las dos tablas van al montón del motor (PSRAM); sin memoria se omiten las marcas de origen, sin
  fallar.
- **Para que no vuelva:** `tools/stack/check_frames.py` revisa los marcos de todo el firmware (ninguna función pasa
  de 3072 B salvo `flexWebServeConn`, admitida porque solo corre en `wm_web` con 16 KB) y `make race` ejecuta la
  Galería con dos hilos bajo ThreadSanitizer (encontró tres carreras benignas, corregidas: progreso del visor en
  32 bits atómicos, `abort_job` atómico y estado de las miniaturas leído con el cerrojo).
- Estado: **probado en el PC** y **compilado**; en el reloj, T151 (abrir y cerrar la Galería 20 veces).

## 14. Rediseño: Fotos / Vídeos / + y la microSD

### 14.1 Barra y "+"

- Se quita "Todas / Reloj / (fuente) / Más opciones" y queda **Fotos** (fotos y dibujos), **Vídeos** y **+**. La
  pestaña elegida va tintada. Cada pestaña junta lo del reloj, lo de la microSD y lo de la fuente remota elegida;
  lo remoto ya descargado sale una sola vez (la copia).
- Marcas en las celdas: nada en lo del reloj, **"SD"** en lo de la microSD y la fuente en lo remoto.
- **"+" ("Añadir y fuentes")**: Recibir con QR y Desde el navegador (el Flex Web Server del reloj, `MULTIMEDIA.md`
  §9), Flex OS Ultra, Flex Cloud, Flex Phone (piden emparejar o vincular si falta, o eligen la fuente), Origen,
  Guardar en, Transferencias y Música. **No se ha quitado nada**: lo que estaba en la barra y en "Más opciones"
  está aquí.
- La fila de origen (estado de la fuente remota) sigue encima de la rejilla.

### 14.2 Lo local: la biblioteca del reloj y de la microSD

- El motor tiene una E/S local opcional (`gal_local_io_t` en `gal_engine.h`). Sin ella, recorre `/Imagenes` y
  `/Paint` como antes (todas las pruebas de siempre lo siguen comprobando). En el reloj, `gal_wm.c` la implementa
  sobre la biblioteca única de `watch_media`:
  - **lista**: fotos, vídeos y dibujos de los dos volúmenes (instantánea, sin E/S), del más reciente al más antiguo,
    hasta 200; se rehace sola cuando cambia la biblioteca o entra/sale la tarjeta;
  - **miniaturas**: las persistentes de la biblioteca (JPEG de 132 px, también de vídeos y dibujos), reducidas a
    112 px; un JPEG del reloj sin ella se decodifica como siempre, uno de la microSD espera a la de la biblioteca (no
    se llena el LittleFS con miniaturas de la tarjeta);
  - **leer** (visor, subidas): con el arrendamiento de la microSD, que se suelta siempre; si la tarjeta no está, la
    operación no empieza;
  - **borrar**: en el reloj a la papelera de Archivos; en la microSD, borrado (no hay papelera) con el doble toque;
  - **descargar**: al **destino elegido**; si no está listo, se dice el motivo y no se guarda nada en otro sitio; en
    la microSD, copia verificada (tamaño) antes de borrar el temporal del LittleFS.
- Información dice dónde está ("En la microSD (/DCIM/x.jpg)") y "Descargar a la microSD" si ese es el destino.

### 14.3 Vídeos

Un AVI MJPEG del reloj o de la microSD se reproduce al tocarlo (`MULTIMEDIA.md` §8); en Información, "Reproducir".
Lo que no es MJPEG, y los vídeos remotos, explican cómo verlos (enviarlos desde el móvil, que los convierte).

### 14.4 Pruebas

- Host: 409 comprobaciones (47 nuevas de la E/S local: dos volúmenes, arrendamientos siempre equilibrados, microSD
  retirada a mitad, borrar, subir desde la microSD, descargar con y sin destino listo; 18 de la traducción registro →
  elemento) y la batería de dos hilos con TSan.
- Simulador: la barra nueva, "+" con todas sus filas, Vídeos vacío y con un vídeo de Flex OS Ultra, el visor sobre
  una foto remota, la descarga y que después ya no se ofrezca otra vez.
- En el reloj: `MULTIMEDIA.md` §13, T234-T244.

