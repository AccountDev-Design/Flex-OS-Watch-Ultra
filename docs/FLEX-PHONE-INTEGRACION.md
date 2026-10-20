# Integración Flex Phone ↔ Flex OS Watch Ultra

La app Android **Flex Phone** vive ahora en este repositorio, en [`FlexPhone/`](../FlexPhone). Es un proyecto Gradle
independiente del firmware. Sirve a la vez:

- a **Flex OS Ultra** (ESP32-P4/C6) por Wi-Fi, como siempre;
- al **reloj** (ESP32-S3) por Bluetooth LE.

El reloj usa el teléfono para cuatro cosas: notificaciones, música, el estado del teléfono y **navegar por Internet a
través del teléfono** (Browser Relay).

Reglas que se han respetado:

- El firmware del P4/C6 y su `server/` **no se han tocado**. Se reutilizan sus protocolos tal cual (Flex Link v2 y FBP/1).
- El repositorio Flex-OS-Ecosistema tampoco se ha tocado. `FlexPhone/` se importó **sin cambios** de su commit
  `5dbc949` (commit `d504090`). Todo lo demás está encima, en commits aparte.

## 1. Qué hace cada parte

```
 Flex Phone (Android)                                   Flex OS Watch Ultra (ESP32-S3)
 ───────────────────                                    ──────────────────────────────
 FlexLinkService                                        watch_phone (NimBLE central)
  ├─ WifiLinkServer  TCP 47820 ── Flex OS Ultra (P4)     │
  └─ GattServer      BLE f1580001-… ◄── Flex Link v2 ───┘  RELAY_START / RELAY_INFO
        │                                                     │
 BrowserRelayService ws://ip:puerto/v1/session ◄── FBP/1 ── watch_browser (br_backend + br_net)
   (renderiza con el WebView del teléfono)       Wi-Fi local   imágenes JPEG empujadas sin pedirlas
```

| Pieza | Dónde | Qué hace |
|---|---|---|
| Motor v2 sin transporte | `FlexPhone/protocol/…/PhoneLinkPeer.kt` | Lado teléfono de Flex Link v2. Gestiona el saludo, el emparejamiento por código y el reto de sesión mutuo. También PING/PONG, los ACK sin duplicados, el reensamblado y los plazos. Lo usa el servidor BLE; el de Wi-Fi conserva su motor |
| Registro de dispositivos | `FlexPhone/protocol/…/Devices.kt` | Une en **una sola entrada** el mismo dispositivo visto por varios caminos: vínculo guardado, descubrimiento Wi-Fi, canal BLE y canal Wi-Fi. La clave es el `flexosId`. Un canal que aún no se ha presentado se asocia por dirección BLE o IP |
| Vínculos por dispositivo | `FlexPhone/app/…/storage/BondStore.kt` | Hasta 8 vínculos, con nombre, modelo y dirección BLE. Migra el vínculo único anterior. Cada vínculo tiene su credencial de relay |
| Servidor BLE v2 | `FlexPhone/app/…/link/GattServer.kt` | Periférico GATT con los mismos UUID. Exige MTU ≥ 247 y la suscripción al CCCD. Tiene una notificación en vuelo, cola de 64 tramas y hasta 3 conexiones |
| Servicio | `FlexPhone/app/…/link/FlexLinkService.kt` | Wi-Fi y BLE a la vez. Envía a todas las sesiones y responde por el canal de origen. Al abrir sesión BLE manda CAPS, PHONE_STATE, el relay, la música y las notificaciones activas |
| Lista y pantalla por dispositivo | `FlexPhone/app/…/ui/screens/Devices.kt` | Portada «Dispositivos» con icono, estado y canal. Cada modelo tiene su pantalla: el reloj muestra Bluetooth y el navegador; Flex OS Ultra muestra Wi-Fi, relay, nube y conexión. «Olvidar» pide confirmación |
| Fuente del navegador del reloj | `watch_browser/br_backend.c` | Lógica pura que elige teléfono, nube o PC. Nunca inventa una dirección y da el motivo real cuando no hay fuente |
| Relay en el reloj | `watch_phone` (`PM_RELAY`), `watch_browser.c` | Pide el relay por BLE solo con el navegador abierto, como mucho cada 15 s. Rehace la sesión cuando el relay aparece, cambia de dirección o se va |
| Interfaz del reloj | `ui_browser.c`, `ui_flexphone.c` | Origen de la web en `flex://settings` (Automático, Flex Phone, Nube, PC). Página «Web del teléfono» con icono, estado, dirección y botón |

## 2. Los dispositivos en el teléfono

Al abrir Flex Phone, la portada lista los dispositivos Flex: los emparejados y los que se ven ahora.

- **Sin duplicados.** Si un reloj aparece por su vínculo y por su canal BLE a la vez, sale una sola entrada. Lo mismo
  vale para un P4 visto por descubrimiento Wi-Fi y por su canal TCP. Lo comprueba `DeviceRegistryTest`.
- **Modelo.** Llega en el `HELLO`, con dos campos opcionales al final: nombre y modelo (`FLP_MODEL_WATCH_ULTRA = 2`).
  Un lector antiguo los ignora. Sin ellos, el modelo se deduce del camino (Wi-Fi → P4, BLE → reloj) y se marca como
  deducido.
- **Orden:** primero los conectados, luego los que enseñan un código, luego los emparejados.
- Al tocar uno se abre **su** pantalla. El modelo decide qué secciones tiene.

### Iconos: un SVG, cada plataforma con lo suyo

Los dibujos están en `design/devices/{watch_ultra, ultra_p4, phone, unknown}.svg` (viewBox 96). `tools/devicons/gen_devicons.py` los convierte:

- **Android:** `VectorDrawable` (`FlexPhone/app/src/main/res/drawable/ic_dev_*.xml`). Android lo pinta en vectorial a
  cualquier densidad.
- **Reloj:** RGB565A8 prerrasterizado con Chromium al tamaño en que se usa (56×56, 9408 B en flash, alineado a 4)
  (`watch_theme/flex_device_icons.c`). El S3 no tiene motor SVG. Rasterizar en el reloj gastaría RAM interna y CPU en
  cada dibujo, para un icono que nunca cambia.
- `tools/devicons/verify_devicons.py` rasteriza el VectorDrawable y el SVG y los compara: **0,00 %** de píxeles
  distintos en los cuatro iconos.

## 3. Navegar desde el reloj por el teléfono (Browser Relay)

### El flujo

1. Se abre el Navegador en el reloj. El origen está en **Automático** (o en **Flex Phone**) y hay un teléfono
   vinculado. La sesión se abre aunque el relay aún no esté.
2. La tarea del navegador ve el relay parado y pide `RELAY_START` por BLE, con acuse. El teléfono lo recibe **una
   vez**.
3. `BrowserRelayService` se levanta y el teléfono contesta `RELAY_INFO {ip, puerto, versión, flags, caps, error}`.
4. `br_backend` construye `ws://ip:puerto/v1/session`. Es la misma ruta que usa el P4. Va sin TLS porque es la red
   local, y solo para esta fuente se permite `ws://`.
5. La sesión FBP/1 manda en el HELLO la **credencial derivada del vínculo**: los 32 primeros hex de
   HMAC-SHA256(clave, `"flexphone-relay-v2"`).
   - El teléfono deriva la suya con su copia de la clave. **No viaja nunca.**
   - El relay solo acepta las credenciales de los vínculos guardados.
6. El relay **empuja** imágenes sin que se pidan (2-12 por segundo) y el reloj las pinta según llegan. La página se
   actualiza sola, sin tocar la pantalla.

Requisito físico: **reloj y teléfono en la misma red Wi-Fi** (o el reloj conectado al punto de acceso del teléfono).
El BLE solo lleva el control. Las imágenes van por Wi-Fi.

### Qué pasa en cada caso (todo probado en la vista previa)

| Caso | Qué hace el reloj |
|---|---|
| Navegador cerrado | No pide nada. No se levanta ningún servidor en el teléfono |
| Relay arrancando | «Pidiendo al teléfono su servidor del navegador…» |
| Relay con error (p. ej. sin Wi-Fi en el teléfono) | Enseña el motivo que manda el teléfono. Reintenta como mucho cada 15 s |
| El teléfono no contesta a `RELAY_START` (app antigua) | A los 15 s: «el teléfono no contestó a la petición» |
| Parado por el usuario en el teléfono | «Servidor del navegador detenido en el teléfono». **No lo vuelve a levantar solo** |
| Suspendido por Android | «Android suspendió el servidor de Flex Phone» |
| Origen «Solo Flex Phone» sin relay | Sin servicio y con el motivo. **Nunca cae al PC por detrás** |
| Origen «Automático» sin relay | Usa la nube o el PC de Ajustes. Cuando vuelve el relay, vuelve al teléfono y pide de nuevo la página abierta |
| Cambia la dirección del teléfono | Sesión nueva con la dirección nueva |
| Se pierde la sesión BLE | La petición pendiente se anula. Al reconectar, el teléfono repite el estado de su relay (`reannounceRelay`) |

### Memoria y estabilidad (8 MB de PSRAM)

- Nada nuevo reserva memoria en tiempo de ejecución.
  - Elegir la fuente usa unos 1,1 KB de pila: la vista del teléfono (108 B), el resultado (360 B) y una copia de los
    ajustes (586 B). Corre en la tarea del navegador, que tiene 8 KB de pila.
  - La instantánea del teléfono es estática (`s_ps`, 636 B en `.bss`), protegida con un mutex.
  - La sesión (`br_net_t`, 11,7 KB) y el búfer de recepción (135 KB) siguen en PSRAM y solo con la app abierta, como
    en F6.
- Una sola ranura de FRAME con contrapresión, como en F6. Si el reloj va más lento que el relay, deja de leer del
  socket hasta pintar lo que tiene. En el reloj no se acumulan imágenes y su memoria no crece.
- Las credenciales se borran de la pila tras usarlas (`memset`). No aparecen nunca en el registro ni en pantalla.
- **Prueba larga en el PC** (`test_br_backend.c`): 2 h de reloj virtual contra un relay que empuja. El resultado fue
  55 803 imágenes empujadas y 55 804 pintadas (7,75 por segundo), 479 PING y ningún corte. ASan y LSan no dan fugas.
- **Prueba larga en la vista previa** (suite `relay`): 10 min con la página viva. Se pintaron 4000 imágenes. La
  memoria de LVGL pasó de 53 608 B a 53 608 B, con 934 bloques antes y después. Ningún temporizador de más.

## 4. Pruebas que se han ejecutado (2026-10-20)

| Prueba | Resultado |
|---|---|
| `make -C tests/host` (ASan + UBSan + LSan) | **2756 comprobaciones, 0 fallos**. Incluye `test_br_backend.c` (resolutor, sesión al relay, credencial mala, prueba de 2 h) |
| Vista previa completa (`tools/sim/build/sim`) | **TODO OK**. La suite `relay` cubre extremo a extremo con el `phone_core` y el `br_net` del firmware |
| Vista previa con ASan + UBSan, semillas 2, 9 y 13 | **TODO OK** en las tres, sin informes del sanitizador |
| Firmware (`idf.py build`, ESP-IDF 5.5.5) | Compila sin avisos. Imagen de 2 631 519 B. DIRAM al 49,1 % |
| Kotlin `:protocol:test :storage:test` | **244 pruebas, 0 fallos** |
| Interoperabilidad Kotlin ↔ C | `WatchPeerInteropTest` arranca el **motor C real del reloj** (`tests/host/build/flexlink_watch_peer`) frente al motor Kotlin del teléfono. Prueba emparejar, la sesión, las notificaciones, la credencial del relay (la misma en los dos lados), una respuesta larga con acuse entregada una vez, `RELAY_START` entregado una vez, `RELAY_INFO` y «parado por el usuario», 2 min de latido, reconexión sin código y código equivocado |
| App Android (`-PflexTypecheck :typecheck:compileKotlin`) | Compila contra el `android.jar` real (Robolectric android-all) y Compose |
| Iconos | VectorDrawable frente a SVG: 0,00 % de diferencia |

Para repetirlo:

```bash
make -C tests/host && make -C tests/host peer
make -C tools/sim LVGL_DIR=… && SIM_ONLY=relay tools/sim/build/sim tools/sim/out
cd FlexPhone && ./gradlew :protocol:test :storage:test     # la prueba de interoperabilidad usa el binario de arriba
```

## 5. Límites, dichos claramente

- **No se ha probado en el reloj ni en un teléfono reales.** Este entorno no tiene acceso a ninguno de los dos. Todo lo
  anterior son pruebas de PC y de la vista previa. Las pruebas físicas están en
  [PRUEBAS-RELOJ.md](PRUEBAS-RELOJ.md) §13 (T116-T123).
- **El APK no se ha construido aquí.** Este entorno no llega a `dl.google.com`, que da 403, así que no hay Android
  Gradle Plugin. El código de la app se ha compilado contra el framework real con el módulo `typecheck`, pero los
  recursos y el manifiesto (AAPT) no se han procesado. Para construirlo: `cd FlexPhone && ./gradlew assembleDebug` en
  un equipo con Android Studio ([`FlexPhone/README.md`](../FlexPhone/README.md)).
- El relay va **sin TLS** por la red local. Es lo mismo que hace el P4 con el teléfono.
  - La credencial autentica, pero no cifra.
  - Alguien en la misma red podría ver las imágenes.
  - El cifrado de aplicación es F5.6 (D7).
- La primera conexión BLE usa Just Works. No protege frente a alguien presente durante ese primer emparejamiento.
  Después, Flex Link v2 autentica las dos partes con el código de 6 cifras.
