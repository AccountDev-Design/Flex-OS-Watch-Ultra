# F5 — Conectividad: Wi-Fi y Flex Phone

| Tarea | Contenido | Estado |
|---|---|---|
| F5.1 | Wi-Fi (estación): encendido bajo demanda, escaneo, contraseña, redes guardadas, reconexión, hora por SNTP | 🟡 compila, pasa las pruebas de PC y la vista previa; faltan T78-T83 |
| F5.2-F5.6 | Flex Link v2, notificaciones, BLE con Flex Phone, seguridad | ⬜ (especificación extraída de la referencia; ver §3) |

## 1. Wi-Fi (F5.1): componente `watch_net`

### 1.1 Arquitectura

- **Una sola tarea dueña del estado** (`watch_net`, prioridad 4, 4 KB de pila).
  - Las órdenes de la interfaz (encender, buscar, conectar, olvidar) y los eventos de ESP-IDF (Wi-Fi, IP y SNTP)
    llegan a su cola.
  - Los manejadores de eventos solo envían mensajes: nunca bloquean ni cambian el estado.
- **La interfaz lee una instantánea** (`watch_net_get`) desde su temporizador de LVGL. Ninguna tarea de red toca LVGL.
  Nada se queda congelado mientras conecta.
- **Lógica pura en `net_logic.c`**, probada en el PC: redes guardadas, espera entre reintentos, motivos de desconexión
  en palabras, barras de señal y orden de la lista.

### 1.2 Comportamiento

| Situación | Qué hace |
|---|---|
| Arranque | La radio sigue apagada salvo que el usuario la dejara encendida (NVS, espacio `wifi`, clave `on`) |
| Encender | `esp_wifi_start` → escaneo. Si hay una red guardada a la vista, conecta sola a la mejor |
| Red nueva | Se pide la contraseña. **Solo se guarda cuando la conexión funciona** (con IP) |
| Contraseña incorrecta (`AUTH_FAIL`, `4WAY_HANDSHAKE_TIMEOUT`, `HANDSHAKE_TIMEOUT`, `MIC_FAILURE`, `AUTH_EXPIRE`) | "Contraseña incorrecta" y **no se reintenta** |
| Señal perdida u otro fallo | Reintento con espera exponencial: 1, 2, 4, 8, 16, 32 s y después cada 60 s. La interfaz muestra la cuenta atrás |
| Con IP | Arranca SNTP (`pool.ntp.org`). Si la hora es válida (≥ 2025), pone el RTC en hora (`watch_time_set_utc`) |
| Apagar | `esp_wifi_stop`, SNTP parado, sin reintentos |
| Olvidar | Dos toques, como Formatear. Si era la red actual, se desconecta |

Las redes guardadas son como mucho 4, la más reciente primero. Se guardan en NVS dentro del bloque con versión y CRC
de `watch_settings` (`ws_blob_pack`): datos dañados no se usan.

> **Seguridad:** las contraseñas se guardan sin cifrar en la NVS. El cifrado de flash y NVS es la decisión pendiente
> D12 (F8.3). Hasta entonces, cualquiera con acceso físico y un cable puede leerlas.

### 1.3 Memoria (compilación ESP-IDF 5.5.5)

| | Antes | Con Wi-Fi |
|---|---|---|
| Imagen | 1,49 MB | 2,04 MB (ranura de 6 MB) |
| RAM interna estática (DIRAM) | 90,7 KB | 141,8 KB (+51 KB: pila Wi-Fi y lwIP); quedan 199,9 KB |

Con `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, los búferes dinámicos de Wi-Fi y lwIP van a la PSRAM cuando se puede. La
RAM interna queda para los búferes DMA de la pantalla y, después, el BLE. El consumo real con el Wi-Fi encendido se
medirá en el reloj (T81, Device Care > Memoria).

### 1.4 Interfaz

- **Ajustes > Wi-Fi** (fila arriba del todo, con la red o el estado):
  - interruptor, estado real, IP y hora sincronizada;
  - "Buscar redes";
  - redes del escaneo (guardadas primero y después por señal; el color del icono indica la señal);
  - redes guardadas, con "Olvidar".
- **Contraseña:**
  - campo con "Ver/Ocultar" y teclado propio en 4 páginas (minúsculas, mayúsculas de una letra, números y símbolos):
    todo el ASCII imprimible;
  - teclas de texto ("DEL", "OK"), porque los símbolos de LVGL no existen en Outfit;
  - el cursor no parpadea y cada carácter se oculta al momento: ninguna animación sin fin redibuja la pantalla.
- **Panel rápido:** el mosaico Wi-Fi deja de estar "Pendiente".
  - Tocarlo enciende o apaga la radio (`QSID_WIFI` es `QT_TOGGLE` en la referencia).
  - Responde en el mismo toque y debajo muestra la red o el estado.

![Wi-Fi](img/f5_wifi.png) ![Contraseña](img/f5_wifi_contrasena.png)

### 1.5 Pruebas

**En el PC:**

- `tests/host` (1116 comprobaciones con ASan y UBSan):
  - redes guardadas: orden, actualizar, lleno, borrar, ida y vuelta por el bloque con CRC, y datos dañados o truncados;
  - espera entre reintentos;
  - motivos con los códigos reales de ESP-IDF 5.5.5;
  - barras de señal, y escaneo sin ocultas ni repetidas con las guardadas primero.
- **Vista previa** (`SIM_ONLY=wifi`, servicio simulado en `tools/sim/sim_net.c`):
  - Ajustes muestra "Apagado"; encender, buscar y listar;
  - contraseña incorrecta, y la buena escrita con el teclado (DEL, cambio de página, Ver/Ocultar);
  - "Conectado a Casa" con IP, la red guardada y la hora sincronizada;
  - señal perdida con cuenta atrás y reconexión;
  - olvidar con dos toques y apagar;
  - mosaico del panel rápido (respuesta inmediata y estado real);
  - sin fugas ni temporizadores sueltos; prueba "mono" con las páginas de Wi-Fi y ASan/UBSan con 3 semillas.
- **Firmware completo:** compila sin avisos.

**En el reloj (pendientes):**

| # | Prueba | Resultado esperado |
|---|---|---|
| T78 | Ajustes > Wi-Fi > encender | Lista las redes de alrededor en 2-3 s; el registro dice `Wi-Fi encendido` |
| T79 | Conectar a tu red con contraseña | "Conectado a …", IP de tu router; reinicia el reloj: con el Wi-Fi encendido vuelve a conectar sola |
| T80 | Contraseña mal escrita | "Contraseña incorrecta" sin reintentos en el registro |
| T81 | Hora por Internet | Con la hora del reloj desajustada: al conectar, "Hora: Sincronizada" y la esfera corrige la hora. Device Care > Memoria con el Wi-Fi encendido (anotar) |
| T82 | Alejarse del router o apagarlo | "Señal perdida · N s" y reconexión sola al volver; la interfaz sigue respondiendo |
| T83 | Panel rápido | El mosaico enciende y apaga; el consumo (horas de batería) con el Wi-Fi apagado no cambia respecto a antes |

## 2. Independencia del Wi-Fi y el BLE

- El Wi-Fi solo sirve para Internet (navegador, clima, hora) y está apagado si no se usa.
- El enlace con el teléfono irá por BLE (F5.5, decisión D5). Perder Internet no corta el BLE y viceversa: son servicios
  distintos, sin estado compartido.
- En el ESP32-S3 los dos comparten la antena con la coexistencia de ESP-IDF (activa por defecto cuando están los dos).

## 3. Lo que sigue (F5.2-F5.6)

La especificación de Flex Link v2 y de los mensajes de Flex Phone se extrajo de la referencia con vectores dorados:

- trama de 18 B con CRC16-CCITT, fragmentos de 226 B, anti-repetición y reintentos;
- apretón de manos HELLO/WELCOME/AUTH/CAPS y emparejamiento con código;
- notificaciones, estado del teléfono y multimedia.

El servidor GATT de Android (`GattServer.kt`) ya existe pero está apagado, porque el P4 no tiene BLE. Para servir al
reloj necesita los cambios que lista la especificación: activarlo junto al servidor Wi-Fi, hacer el apretón de manos
v2 y negociar un MTU ≥ 247. Esos cambios son del repositorio de Flex Phone; aquí se implementa el lado del reloj.
