# Cambios que necesita la app Flex Phone (Android) para servir al reloj por BLE

El reloj ya implementa su lado de Flex Link v2 sobre Bluetooth LE (`docs/F5-CONECTIVIDAD.md` §3). La app Android de
hoy **no** lo sirve: `FlexLinkService` solo arranca `WifiLinkServer` (TCP 47820) y `GattServer.kt` está escrito pero
apagado y sin el apretón de manos v2 (`GattServer.kt:20-47`). Este proyecto no modifica el repositorio de Flex Phone;
esta es la lista de lo que hay que cambiar allí, con las citas de la extracción (`flexphone-messages.md` §7).

Rutas relativas a `android/FlexPhone/app/src/main/kotlin/com/flexos/flexphone/` del repositorio Flex-OS-Ecosistema.

## 1. Integración en el servicio (`link/FlexLinkService.kt`)

1. **Arrancar `GattServer`** además de (o en vez de) `WifiLinkServer`, y no exigir `device.isOnWifi()` para el
   transporte BLE (`FlexLinkService.kt:147-180`).
2. **`state.sender`** debe apuntar a `gatt.sendMessage` cuando la sesión es BLE (`:190`), y el bucle necesita un
   `hasSession()` del servidor GATT (`:379`).
3. **Eventos**: `GattServer` debe emitir los mismos `LinkEvent` que Wi-Fi (`ChannelOpened`, `SessionOpen`,
   `SessionClosed`, `PairingRequested`, `PairingFailed`, `ProofResent`, `ProofNotSent`, `Error`) para que al abrir
   sesión se envíen CAPS y PHONE_STATE (`:300-311`).
4. Llamar a `GattServer.tick()` desde el bucle de 5 s (caducidad del reensamblador, `GattServer.kt:377-380`).
5. Conceder `Caps.BLE` cuando el transporte BLE esté activo (`device/DeviceAdapter.kt:187-189`).
6. **Pedir los permisos** `BLUETOOTH_CONNECT` y `BLUETOOTH_ADVERTISE` (API ≥ 31) antes de arrancar
   (`AndroidManifest.xml:22-47` los declara pero no se piden).

## 2. Apretón de manos v2 dentro de `GattServer` (portar de `WifiLinkServer.kt`)

7. Constructor con `phoneId`, `phoneName`, `bondKey`, `onPaired`, `onEvent` (`WifiLinkServer.kt:64-83`).
8. `T_HELLO` → `T_WELCOME {u8 2, str phoneId, str phoneName}` con sesión 0; `T_ERR E_VERSION` si la versión < 2.
9. Emparejamiento: `T_PAIR_CODE` → `PairingSession`; `submitPairingCode` → `T_PAIR_CONFIRM` (sesión 0); manejo de
   `T_ERR` del reloj (`E_AUTH` = código equivocado, `E_TIMEOUT` = caducado o cancelado).
10. Sesión: `T_AUTH_CHALLENGE` → `T_AUTH_RESPONSE`; verificar `T_AUTH_OK` y SOLO entonces guardar el vínculo.
11. Control de acceso: descartar todo lo que no sea del apretón de manos mientras no haya autenticación.
12. **Sesión**: la fija el reloj (carga de `AUTH_CHALLENGE` o cabecera de `AUTH_OK` tras emparejar). Hoy `GattServer`
    se inventa una al conectar (`GattServer.kt:282`) y el reloj descartaría todo con ella.
13. `T_PING` → `T_PONG` con la sesión de la trama, con o sin sesión. El reloj da el enlace por muerto a los 30 s sin
    recibir nada y late cada 8 s.
14. `T_ACK {u16 packet}` para `REPLY_REQ`, `ACTION_REQ`, `FIND_START`, `FIND_STOP` (y relay), entregando solo la
    primera vez.
15. `T_BYE` entrante: cerrar la conexión.
16. Una sola sesión: el hueco solo cambia de dueño al autenticar.

## 3. Transporte GATT

17. UUIDs sin cambios: servicio `f1580001-4c65-4f53-8f6c-6578f1584c4b`, RX `f1580002-…` (el reloj escribe), TX
    `f1580003-…` (el reloj se suscribe). Anunciar el UUID del servicio (como hoy).
18. Permisos `*_ENCRYPTED`: el reloj empareja a nivel BLE (Just Works con vínculo) antes de suscribirse. Se pueden
    mantener; la decisión es D7/D8.
19. **MTU ≥ 247**: `FlexLink.fragment()` trocea siempre a 226 B (`FlexLink.kt:292-299`). No enviar hasta que
    `onMtuChanged` haya dado ≥ 247; el reloj ya lo pide y, si no lo consigue, corta y lo explica.
20. Una escritura del reloj = una trama completa (sin escrituras largas), y una notificación = una trama.
21. Guardar el estado del CCCD y no notificar antes de que el reloj se suscriba (el reloj se suscribe antes del HELLO).
22. Mantener el reensamblado y la anti-repetición por conexión que `GattServer` ya tiene.

## 4. Opcional (mejora la experiencia en el reloj)

- Al abrir sesión, reenviar las notificaciones activas (`activeNotifications`) y el estado multimedia: hoy solo se
  envían las nuevas (`flexphone-messages.md` §4.5-4.6). El reloj conserva su historial entre cortes, pero lo que llegó
  mientras estaba desconectado no lo sabe.
- Enviar `T_MEDIA_STATE` con título vacío cuando no haya reproductor (hoy no se envía nada).

## 5. Cómo probarlo

Con estos cambios, las pruebas T84-T93 de `docs/F5-CONECTIVIDAD.md` §3.6 dan el resultado de extremo a extremo. Los
vectores de `tests/host/test_flexlink.c` (dorados de `FlexLinkTest.kt` y `FlexAuthTest.kt`) valen para comprobar que
el lado Android sigue hablando el mismo protocolo byte a byte.
