# Hoja de ruta — Flex OS Watch Ultra

```
PROGRESO GLOBAL: 51,7 %   [██████████░░░░░░░░░░]   48,3 % pendiente   (verificado en el reloj: 7,3 %)
```

El alcance ha crecido dos veces y ningún trabajo hecho se perdió. Cuando crece el alcance, el porcentaje baja porque
mide más trabajo. Detalle en el historial del final.

- **2026-10-16, prompt maestro de migración:** iconos y fondos originales, todas las apps del inventario y BLE como
  canal principal con el teléfono. El peso total pasó de 100 a 122.
- **2026-10-20, integración con Flex Phone:** la app Android entra en `FlexPhone/` y se añade el navegador del reloj
  por el teléfono. Fase FP, con lo que el peso total pasa a 130.
- **2026-10-20, experiencia Watch Ultra (F10):** gestos globales, cajón en panal, 5 esferas con selector, AOD y
  protección AMOLED, Flex Ultra Power Saving, rumbo relativo y correcciones de estabilidad (Wi-Fi, splash). El peso
  total pasa a 138 (`docs/F10-EXPERIENCIA.md`).
- **2026-10-21, estabilidad (F11):** causas raíz y correcciones del reinicio del editor de esferas (pila de LVGL), de la
  falta de memoria del navegador (LVGL en PSRAM), de un puntero nulo al cerrar el navegador, diagnóstico de memoria,
  cajón desde el borde inferior y redibujado solo de lo que cambia. Sin peso nuevo: corrigen tareas que ya contaban
  (`docs/F11-ESTABILIDAD.md`, pruebas físicas T142-T150).
- **2026-10-23, Galería compartida (FG):** la Galería entra en el reloj como cliente del Flex Web Server de Flex OS
  Ultra, de Flex Cloud (con la misma Flex Account) y del teléfono a través del P4, sin nube, cuenta, biblioteca ni
  servidor propios. El peso total pasa a 144. Resuelve D9 y D11 y da contenido a F8.1 y F8.2 (`docs/GALERIA.md`,
  pruebas físicas T151-T166).

- **Completado:**
  - Auditoría y plan (F0).
  - Proyecto ESP-IDF flasheado y arrancando en el reloj (F1.1).
  - Software de F2: lecturas del AXP2101, estados, indicador, animación de carga y avisos.
  - Software de F3: identidad, hora del RTC y esfera, navegación y arranque, panel rápido y Ajustes.
  - F2 y F3 están compilados y probados en el PC, incluidos ASan/UBSan y la prueba "mono".
  - Prueba física del 2026-10-15 (fotos y vídeo del usuario): pantalla, táctil, LVGL (50 FPS en reposo), flash de 32 MB y lecturas del AXP2101 sin errores. La carga se detecta en el reloj.
  - B4 (`docs/B4-CALIDAD.md`), con lo que se vio en esa prueba:
    - respuesta rápida de carga, sin tocar la pantalla;
    - animación de carga sin tirón;
    - negro AMOLED;
    - área segura con esquinas redondeadas;
    - verificador visual de todas las pantallas.
  - Software de F4 (`docs/F4-SERVICIOS.md`), compilado y probado en el PC:
    - ajustes persistentes;
    - poner en hora, zona horaria y 12/24 h;
    - cronómetro, temporizador, alarmas y lanzador de apps;
    - IMU y app Sensores;
    - apagado de pantalla;
    - LittleFS.
  - Identidad original (iconos, lanzador, fondos), apps Reloj, Calculadora, Calendario, Flex Compass y Device Care.
  - Wi-Fi (F5.1) y Flex Phone por Bluetooth LE (F5.2-F5.5): núcleo Flex Link v2 portado con vectores dorados,
    central NimBLE, notificaciones, Música y estado del teléfono (`docs/F5-CONECTIVIDAD.md`).
  - Integración con Flex Phone (fase FP, `docs/FLEX-PHONE-INTEGRACION.md`):
    - la app Android en `FlexPhone/`, con Flex Link v2 por BLE para el reloj y por Wi-Fi para el P4;
    - lista unificada de dispositivos con iconos de un único SVG;
    - el navegador del reloj usa Internet a través del teléfono (Browser Relay pedido por BLE), con la página
      actualizándose sola;
    - interoperabilidad probada entre el motor C del reloj y el motor Kotlin del teléfono.
  - Experiencia Watch Ultra (F10, `docs/F10-EXPERIENCIA.md`), compilada y probada en el PC: gestos de los cuatro
    bordes con apps suspendidas, cajón en panal, 5 esferas con selector y persistencia, panel rápido desde cualquier
    página, AOD, Pixel Shift, atenuación progresiva, Flex Ultra Power Saving, Device Care ampliada, rumbo relativo en
    Flex Compass, splash de 3 s, sin medidor de FPS y cinco causas de fallo del Wi-Fi corregidas.
  - Estabilidad (F11, `docs/F11-ESTABILIDAD.md`), compilada y probada en el PC: tres fallos fatales con su causa raíz
    y su prueba de regresión, LVGL fuera de la RAM interna, diagnóstico de memoria, cajón desde el borde inferior y
    0 px redibujados en reposo.
  - Galería compartida (FG, `docs/GALERIA.md`), compilada y probada en el PC y contra los servidores reales del P4 y
    de Flex Cloud: fotos del reloj, de Flex OS Ultra, de Flex Cloud y del teléfono por el P4, origen Automático con
    reglas explícitas, miniaturas y visor por flujo, transferencias reanudables con integridad y Flex Account del
    reloj (F8.1, F8.2).
- **En curso:** pruebas físicas de F2-F4.
- **Falta:**
  - Pruebas físicas de F2-F4 sobre el firmware actual (T41-T66 y las T de F2/F3 aún sin resultado).
  - La calibración de umbrales (❓ D4).
  - Prueba de extremo a extremo de Flex Phone con el reloj y un teléfono reales (T84-T93, T116-T123). Antes hay que
    construir el APK de `FlexPhone/` en un equipo con Android Studio, porque este entorno no llega al Android Gradle
    Plugin.
  - La seguridad y las pruebas finales; el navegador, probarlo en el reloj con el servicio; el OTA, probarlo en el
    reloj y publicar la clave y el manifiesto (`docs/F7-OTA.md`).
- **Siguiente:** pruebas físicas de F11, la Galería y F10 (`docs/PRUEBAS-RELOJ.md`: primero T142-T150, después T151-T166 y T124-T141) y de F2-F4;
  después F5 (Wi-Fi y Flex Phone).

## Cómo se calcula

- Cada fase tiene un **peso fijo** desde la Fase 0 (suma 100). La ampliación del alcance del 2026-10-16 añadió 22 (total 122), la del 2026-10-20 (fase FP) otras 8 (total 130), F10 otras 8 (total 138) y la Galería (FG, 2026-10-23) otras 6 (total 144). El global es la suma de puntos dividida entre el peso total. Dentro de cada fase, las tareas reparten ese peso según su complejidad e importancia.
- Una tarea vale:
  - **0 %** si solo está planificada o escrita sin compilar;
  - **50 %** si compila y pasa las pruebas de PC;
  - **100 %** solo si pasa la prueba física en el reloj.
- **Repartos internos:**
  - F2 y F3 se fijaron en B1.
  - F1 y F4-F8 se fijan aquí (2026-10-15), antes de que ninguna de sus tareas tuviera puntos de hardware.
- Los pesos no se cambian para hacer subir el porcentaje. Cualquier cambio de estimación se explica en el historial del final.

Estados: ✅ verificado en el reloj · 🟡 compilado y probado en PC · 🔄 en curso · ⬜ pendiente · ❓ depende de una decisión pendiente

## Plan completo

### F0 · Auditoría y plan — 5 / 5
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| Auditoría de hardware, referencia Arduino, reutilización y plan | 5 | ✅ | 5 |

### F1 · Base ESP-IDF, pantalla y táctil — 9,5 / 10
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F1.1 Proyecto ESP-IDF 5.5.5 + BSP oficial; compilar, flashear y arrancar en el reloj | 2 | ✅ (confirmado por el usuario) | 2 |
| F1.2 Pantalla CO5300: imagen completa, desfase 0x16, brillo | 3 | ✅ (fotos del 2026-10-15 y confirmación del usuario) | 3 |
| F1.3 Táctil FT3168: coordenadas y ejes | 2 | ✅ (navegación y desplazamiento en el vídeo; confirmación del usuario) | 2 |
| F1.4 LVGL 9.5: buffers DMA medidos, FPS y memoria medidos | 2 | ✅ (Diagnóstico y medidor: 50 FPS en reposo, 22-32 al desplazar antes de B4) | 2 |
| F1.5 Estabilidad: 30 min sin reinicios | 1 | 🟡 falta T12 | 0,5 |

### F2 · Batería y energía — 7,5 / 15
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F2.1 Lectura del AXP2101 y estados (porcentaje, VBAT, USB, carga, completa, externa sin carga; filtrado, histéresis, errores). B1 + **B2-1** | 9 | 🔄🟡 faltan T4-T8 y T14-T18 | 4,5 |
| F2.2 Indicador de batería, animación de carga y avisos de batería baja (B2-3, B2-4, B2-5) | 6 | 🟡 faltan T19-T22 | 3 |

Dentro de F2 queda:
- **B2-2:** cerrada sin cambios (D1 resuelta). En el reloj la batería carga y VBAT se lee bien sin escribir el ADC ni el pin TS.
- Calibración de los umbrales con la batería real (❓ D4).
- F2.1 y F2.2 siguen al 50 %. B2-6 y B4-C2 cambiaron el muestreo y la animación después de la prueba, así que hay que repetirla (T41, T43). Los estados "completa" y "externa sin carga" no tienen resultado físico.

### F3 · Interfaz, tema y navegación — 12,5 / 25
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F3.1 Identidad Flex OS Ultra: paleta, Outfit, iconos, radios, fondo Onyx, **Liquid Glass** base (tinte adaptativo, brillo, sombra, bordes). **B3-1:** 12 iconos vectoriales, estados pulsado/activo/no disponible, cabecera, lista y fila de Ajustes (`docs/B3-INTERFAZ.md`). Pendiente: fondo pre-desenfocado para fondos con detalle | 5 | 🟡 faltan T2, T13 y T23-T25 | 2,5 |
| F3.2 Esfera del reloj: hora y fecha reales del RTC (servicio de solo lectura; poner en hora es F4.2), batería real, estados sin datos (B3-2) | 5 | 🟡 faltan T26-T29 | 2,5 |
| F3.3 Panel rápido: cortina que sigue al dedo, brillo real (mín. 10 %), batería real, acceso a Ajustes, Wi-Fi/Bluetooth marcados "Pendiente" (B3-4) | 5 | 🟡 faltan T33-T36 | 2,5 |
| F3.4 Ajustes (Pantalla, Batería, Diagnóstico, Acerca de), navegación por gestos y **animaciones/transiciones** (apertura y cierre 210/190 ms, volver, arranque) (B3-3, B3-5) | 5 | 🟡 faltan T30-T32 y T37-T40 | 2,5 |
| F3.5 **Iconos originales** de las apps (`drawAppIcon`, Icons.h) prerenderizados a su tamaño físico + **lanzador en rejilla** como la Caja de aplicaciones (`docs/F3-IDENTIDAD-ORIGINAL.md`) | 3 | 🟡 faltan T67-T70 | 1,5 |
| F3.6 **Fondos originales** (los 8 de Wallpaper.h) a 410×502, uno en PSRAM a la vez, selector en Ajustes | 2 | 🟡 faltan T71-T73 | 1 |

### F4 · Servicios del sistema — 5 / 10

Detalle y pruebas en `docs/F4-SERVICIOS.md`.
| Tarea | Peso | Estado |
|---|---|---|
| F4.1 Ajustes persistentes (NVS, escritor único): brillo, fondo, hora, zona, apagado, esfera, bloque de alarmas | 2 | 🟡 faltan T46-T47 |
| F4.2 Hora: poner en hora el RTC PCF85063 (UTC + marca, verificado), zona horaria, 12/24 h. SNTP llega con el Wi-Fi (F5) | 2 | 🟡 faltan T48-T51 |
| F4.3 Cronómetro, temporizador y alarmas (aviso visual), con lanzador de apps (D15) | 2 | 🟡 faltan T52-T55 |
| F4.4 IMU QMI8658 (controlador propio, mide solo con la app abierta) + app Sensores con nivel, movimiento y batería | 1,5 | 🟡 faltan T60-T62 |
| F4.5 Apagado de pantalla por inactividad (atenuada, DISPOFF, vuelta a la esfera), botón lateral, despertar por toque, alarma y USB. Sin reposo ligero de la CPU (F8.4, medir antes) | 1,5 | 🟡 faltan T56-T59 |
| F4.6 Almacenamiento LittleFS en `storage` (> 16 MB): puerta por la sonda de 32 MB, prueba de escritura del sector, formateo confirmado, montaje y archivo atómico (`FLASH-32MB.md` §6) | 1 | 🟡 faltan T63-T66 |

### F5 · Conectividad y Flex Phone — 7,5 / 17
| Tarea | Peso | Estado |
|---|---|---|
| F5.1 Wi-Fi STA: conexión, credenciales, reconexión, sin bloquear la interfaz, hora por SNTP (`docs/F5-CONECTIVIDAD.md`) | 3 | 🟡 faltan T78-T83 |
| F5.2 `flex_link`: cliente compatible con Flex Link v2 (descubrimiento, emparejamiento, sesión, reconexión) reutilizando los módulos puros (`components/flexlink`, `watch_phone`) | 4 | 🟡 faltan T84-T87, T91 (y la app Android v2) |
| F5.3 Notificaciones desde Flex Phone (recibir, listar, descartar, responder, aviso) | 3 | 🟡 faltan T88-T90 |
| F5.4 Otras funciones del protocolo (estado y capacidades del teléfono, multimedia, hacer sonar) | 2 | 🟡 falta T93 |
| F5.5 **Transporte BLE** con Flex Phone (canal principal): central NimBLE contra el `GattServer.kt` de la app (servicio `f1580001-…`), vínculo cifrado, reconexión, BLE independiente del Wi-Fi | 3 | 🟡 faltan T84-T85, T91-T92 |
| F5.6 Seguridad del enlace (v3 con ECDH + cifrado autenticado; requiere cambios en Flex Phone, en su repositorio) | 2 | ❓ D7, D8 |

### F6 · Navegador (renderizado remoto) (`docs/F6-NAVEGADOR.md`) — 4 / 8
| Tarea | Peso | Estado |
|---|---|---|
| F6.1 Cliente FBP/1: WebSocket + JPEG (TJpgDec de LVGL; el S3 no tiene decodificador por hardware) | 4 | 🟡 falta T104-T105 |
| F6.2 Interfaz del navegador para 410×502 (barra, teclado, pestañas) | 3 | 🟡 faltan T105, T108 |
| F6.3 TLS con validación de certificado (paquete de CA de ESP-IDF) | 1 | 🟡 faltan T104, T106 |

Limitación conocida: sin servidor de renderizado o sin el móvil, no hay web. Nunca será un motor HTML local.

### F7 · Actualizaciones OTA (`docs/F7-OTA.md`) — 3,5 / 7
| Tarea | Peso | Estado |
|---|---|---|
| F7.1 OTA HTTPS (`esp_http_client` + `esp_ota_*`, escritura por partes) entre `ota_0` y `ota_1` + reversión automática | 3 | 🟡 faltan T109-T113, T115 |
| F7.2 Manifiesto propio (`flex_os_watch_ultra`) + firma de la imagen (ECDSA P-256 sobre producto, versión, tamaño y SHA-256) | 3 | 🟡 falta T110 (y publicar la clave y el manifiesto) |
| F7.3 Interfaz de actualización | 1 | 🟡 faltan T110, T112, T114 |

### F8 · Cuenta, nube, seguridad y cierre — 1,5 / 10
| Tarea | Peso | Estado |
|---|---|---|
| F8.1 Flex Account: vinculación por código de `FlexOS_Account.cpp` pasada a C, sobre ES256 verificado, TLS verificado, credencial aleatoria en la NVS como el P4 (`watch_gallery/gal_acct.c`, `docs/GALERIA.md` §6) | 2 | 🟡 falta T160 (servicio de producción) |
| F8.2 Flex Cloud: biblioteca, cuota, descargas y subidas reanudables de la misma cuenta (lo usa la Galería) | 1 | 🟡 faltan T161-T162 |
| F8.3 Protección de credenciales: cifrado de NVS y flash | 2 | ❓ D12 |
| F8.4 Consumo medido y optimización (pantalla, radio, sondeos) | 2 | ⬜ |
| F8.5 Pruebas finales: 24 h de estabilidad, memoria y regresión completa en el reloj | 3 | ⬜ |

### F9 · Apps de Flex OS Ultra (inventario: `docs/INVENTARIO-APPS.md`) — 7,5 / 15
| Tarea | Peso | Estado |
|---|---|---|
| F9.1 Reloj (agrupa Alarmas, Cronómetro y Temporizador con el icono original) | 1 | 🟡 falta T69 |
| F9.2 Calculadora (lógica de `calcKey` transcrita, rejilla 5×4) (`docs/F9-APPS.md`) | 1,5 | 🟡 falta T74 |
| F9.3 Calendario (vista de mes, hoy resaltado, cambio de mes) | 1 | 🟡 falta T75 |
| F9.4 Flex Compass (pantalla de requisito sin magnetómetro + cabeceo/alabeo reales) | 1 | 🟡 falta T76 |
| F9.5 Device Care (estado real, memoria, almacenamiento, pruebas) | 2 | 🟡 falta T77 |
| F9.6 Almacenamiento: explorador de LittleFS (`docs/F9-APPS.md`) | 1 | 🟡 faltan T96-T97 |
| F9.7 Notas (lista, editor con autoguardado y teclado en español sobre LittleFS) | 2 | 🟡 faltan T94-T95 |
| F9.8 Clima (Open-Meteo por Wi-Fi, estado sin conexión) (`docs/F9-APPS.md`) | 2 | 🟡 faltan T100-T103 |
| F9.9 Flex Phone (estado del enlace, emparejar, notificaciones) (`docs/F5-CONECTIVIDAD.md` §3.4) | 1,5 | 🟡 faltan T86-T90 |
| F9.10 Música (control de la música del teléfono) | 1 | 🟡 falta T93 |
| F9.11 Paint (lienzo reducido, formato FXP1 compatible) | 1 | 🟡 faltan T98-T99 |

### FP · Integración Flex Phone ↔ reloj (`docs/FLEX-PHONE-INTEGRACION.md`) — 4 / 8
| Tarea | Peso | Estado |
|---|---|---|
| FP.1 Android: motor v2 sin transporte (`PhoneLinkPeer`), `GattServer` v2, servicio Wi-Fi + BLE a la vez, vínculos por dispositivo y credencial de relay por vínculo | 3 | 🟡 faltan T84-T93 y construir el APK (el código compila contra el framework real con `:typecheck`) |
| FP.2 Android: lista de dispositivos sin duplicados, icono de un único SVG (VectorDrawable en Android, RGB565A8 en el reloj) y pantalla por modelo | 2 | 🟡 faltan T116-T117 |
| FP.3 Reloj: navegador por el Browser Relay del teléfono (`br_backend`, `RELAY_START` por BLE, origen en `flex://settings`, página «Web del teléfono», refresco continuo sin tocar) | 3 | 🟡 faltan T118-T123 |

### F10 · Experiencia Watch Ultra (`docs/F10-EXPERIENCIA.md`) — 4 / 8
| Tarea | Peso | Estado |
|---|---|---|
| F10.1 Gestos globales (cuatro bordes, apps suspendidas, cambios sin guardar), cajón en panal y panel rápido desde cualquier página | 2 | 🟡 faltan T130-T132 |
| F10.2 Esferas (digital, analógica, modular, Liquid Glass, Parallax), selector con vistas previas reales y persistencia | 2 | 🟡 faltan T133-T135 |
| F10.3 AOD, Pixel Shift y atenuación progresiva en `ui_screen`, configurables en Ajustes y Device Care | 1,5 | 🟡 faltan T136-T137 |
| F10.4 Flex Ultra Power Saving (panel rápido y Device Care) con restauración del estado real | 1,5 | 🟡 faltan T138-T140 |
| F10.5 Flex Compass: rumbo relativo por giroscopio con incertidumbre (sin Norte) | 1 | 🟡 falta T141 |

Fuera del peso (correcciones de tareas que ya contaban): Wi-Fi (F5.1, T124-T128), splash (F3.4, T129), medidor de FPS
retirado.

### FG · Galería compartida con Flex OS Ultra y Flex Cloud (`docs/GALERIA.md`) — 3 / 6
| Tarea | Peso | Estado |
|---|---|---|
| FG.1 Fuentes y conexión: cliente del Flex Web Server del P4 (código, sesión, biblioteca), pasarela al teléfono, Flex Cloud; Automático con reglas y estabilidad, salud, esperas y recuperación | 2 | 🟡 faltan T153-T154, T159, T162-T163 |
| FG.2 Miniaturas y visor: JPEG por flujo y por bandas (TJpgDec de la ROM con escala), miniaturas fijadas, 2x, memoria acotada | 1,5 | 🟡 faltan T151-T152, T155, T165 |
| FG.3 Transferencias: descargas y subidas reanudables con integridad (SHA-256, CRC32), papeleras, cola con reintentos limitados | 1,5 | 🟡 faltan T156-T158, T161, T164 |
| FG.4 App Galería: rejilla virtual, pestañas, origen, visor con gestos, información, emparejamiento, cuenta, transferencias e icono original | 1 | 🟡 faltan T151, T153, T155 |

### Acordado sin peso asignado

Si se decide incluirlas, se les dará peso redistribuyendo dentro de F4 u F8, y quedará anotado en el historial.

- Audio: altavoz ES8311 y micrófonos ES7210 (❓ D13).
- Motor de vibración, con su GPIO pendiente de verificar (❓ D13).
- microSD (❓ D13).

### Excluido

- Samsung DeX / Flex DeX y modo PC.
- Cámara.
- Edición de imágenes o vídeo (la Galería entra sin edición: FG).
- Multimedia de escritorio.
- Servidor web de subida.
- Kiosco.
- Jumper (recrea Geometry Dash).
- Drivers del P4/C6 (BNO085, esp-hosted).
- `FlexOS_Ultra_S3.ino` y `FlexOS_Pro.ino`.

## Decisiones pendientes

| # | Decisión | Bloquea |
|---|---|---|
| D1 | ✅ Resuelta (2026-10-15): no se escribe nada. En el reloj la batería carga y VBAT se lee bien con la configuración de fábrica | — |
| D2 | Parámetros de carga: no se escriben mientras no se conozca la capacidad de la batería | — |
| D3 | ✅ Resuelta: en "carga completa" se muestra el valor real del gauge | — |
| D4 | Calibrar los umbrales de batería baja y crítica (hoy provisionales: 15 % y 5 %, aislados en `battery_thresholds.h`) | calibración |
| D5 | ✅ Resuelta (prompt maestro, 2026-10-16): BLE con el teléfono; Wi-Fi solo para Internet, encendido cuando una función lo necesita | — |
| D6 | Topología prioritaria: misma red, hotspot del móvil o punto de acceso del reloj. El navegador por el teléfono necesita misma red o hotspot del móvil (el control va por BLE; las imágenes, por Wi-Fi) | F5.5 |
| D7 | Protocolo: Flex Link v2 (en claro) o v3 cifrado | F5.6 |
| D8 | ✅ Resuelta (2026-10-20): la app vive en `FlexPhone/` de este repositorio, con varios dispositivos y el modelo «reloj» en el `HELLO` | — |
| D9 | ✅ Resuelta (2026-10-23, Galería): el reloj es un cliente más del Flex Web Server del P4 en la red local (código de 6 cifras y sesión, como el móvil o el PC). No se cambia nada en el P4 | — |
| D10 | ~~Funciones de Flex Phone en el reloj más allá de las notificaciones~~ Resuelta por el prompt maestro (2026-10-16): todo lo que el protocolo y la app ofrecen (estado, capacidades, multimedia, hacer sonar, respuestas) | F5.4 |
| D11 | ✅ Resuelta (2026-10-23, Galería): la misma Flex Account (vinculación por código como el P4, `model` ESP32-S3) y la API de Flex Cloud de Flex Developer Studio, sin cuenta ni nube propias | — |
| D12 | Cifrado de flash y NVS desde el principio | F8.3 |
| D13 | Uso de audio, motor y microSD | sin peso |
| D14 | Nombre "Liquid Glass" (denominación de terceros) en el producto | F3 |
| D15 | ✅ Resuelta (F4.3): cronómetro, temporizador y alarmas | — |
| D16 | ¿Bloqueo con código? (no acordado) | — |
| D17 | ✅ Resuelta (F4.2): el RTC guarda UTC con una marca de Flex OS; la zona es un desfase fijo elegible (por defecto Lima, UTC-5) | — |
| D18 | ✅ Resuelta (F4.2): 24 h por defecto; 12 h opcional en Ajustes > Fecha y hora | — |

## Verificaciones pendientes en el reloj

- **B1:** T1-T13 (`docs/B1-ARRANQUE.md` §5). T1 (arranque) ya está confirmado.
- **B2:** T14-T22 (`docs/B2-ENERGIA.md`).
- **B3:** T23-T40 (`docs/B3-INTERFAZ.md`).
- **Flash de 32 MB:** T10 y la prueba de escritura (`docs/FLASH-32MB.md` §6).
- **Ruta con espacios** `Flex OS Ultra Watch/`: confirmar que compila en Windows y con la extensión de VS Code.
- **`dependencies.lock`:** subir el que generó tu PC.

## Historial de la estimación

| Fecha | Global | Cambio | Motivo |
|---|---|---|---|
| 2026-10-15 | 4 % | inicial | Auditoría (F0 4/5) |
| 2026-10-15 | 17 % | +13 | F0 completa (+1); B1 compilado y con pruebas de PC (F1 5, F2 4,5, F3 2,5) |
| 2026-10-15 | 17 % | 0 | Reorganización a `Flex OS Ultra Watch/` (sin cambio funcional) |
| 2026-10-15 | 18 % | +1 | Se fija el reparto interno de F1. F1.1 (flasheo y arranque) queda verificada en el reloj según el usuario: pasa de 50 % a 100 %. B2-1 no suma: mejora una tarea que ya contaba al 50 % |
| 2026-10-15 | 21 % | +3 | F2.2 (indicador, animación de carga y avisos) compila y pasa las pruebas de PC y de la vista previa: 50 % de 6 |
| 2026-10-15 | 21 % | 0 | B3-1 (iconos y componentes del tema) amplía F3.1, que ya contaba al 50 % |
| 2026-10-15 | 23,5 % | +2,5 | F3.2 (servicio de hora de solo lectura y esfera) compila y pasa las pruebas de PC y de la vista previa: 50 % de 5 |
| 2026-10-15 | 23,5 % | 0 | B3-3 (navegación y arranque) es parte de F3.4, que no suma hasta tener también los Ajustes (B3-5) |
| 2026-10-15 | 26 % | +2,5 | F3.3 (panel rápido) compila y pasa las pruebas de PC y de la vista previa: 50 % de 5 |
| 2026-10-15 | 28,5 % | +2,5 | F3.4 (navegación, arranque y páginas de Ajustes) compila y pasa las pruebas de PC y de la vista previa: 50 % de 5 |
| 2026-10-15 | 28,5 % | 0 | B3-6 (integración y robustez en el PC) no añade tareas: las de F2 y F3 solo suben con las pruebas físicas |
| 2026-10-15 | 32 % | +3,5 | Prueba física del usuario (fotos y vídeo): F1.2, F1.3 y F1.4 pasan de 50 % a 100 %. F1.5 no tiene prueba larga. B4 (C1-C4) corrige lo visto en esa prueba y no suma: mejora tareas que ya contaban |
| 2026-10-16 | 33 % | +1 | F4.1 (ajustes persistentes en NVS) compila y pasa las pruebas de PC y de la vista previa: 50 % de 2 |
| 2026-10-16 | 34 % | +1 | F4.2 (poner en hora, zona horaria y 12/24 h) compila y pasa las pruebas de PC y de la vista previa: 50 % de 2 |
| 2026-10-16 | 35 % | +1 | F4.3 (cronómetro, temporizador, alarmas y lanzador) compila y pasa las pruebas de PC y de la vista previa: 50 % de 2 |
| 2026-10-16 | 35,75 % | +0,75 | F4.5 (apagado de pantalla y botón lateral) compila y pasa las pruebas de PC y de la vista previa: 50 % de 1,5 |
| 2026-10-16 | 36,5 % | +0,75 | F4.4 (IMU QMI8658 y app Sensores) compila y pasa las pruebas de PC y de la vista previa: 50 % de 1,5 |
| 2026-10-16 | **37 %** | +0,5 | F4.6 (LittleFS en storage) compila y pasa las pruebas de PC y de la vista previa: 50 % de 1. Todas las tareas de F4 están implementadas y probadas en el PC; falta la prueba física |
| 2026-10-16 | **30,3 %** | −6,7 | **Ampliación del alcance** (prompt maestro de migración): F3 sube de 20 a 25 (F3.5 iconos originales y lanzador, F3.6 fondos originales), F5 de 15 a 17 (F5.5 pasa a ser el transporte BLE, canal principal: 1 → 3) y se añade F9 (apps del inventario, 15). Peso total 122; puntos sin cambios (37). Global = 37 / 122. Verificado en el reloj = 10,5 / 122 = 8,6 % |
| 2026-10-16 | **32,8 %** | +2,5 | F3.5 (iconos originales y lanzador, 50 % de 3), F3.6 (fondos originales y selector, 50 % de 2) y F9.1 (app Reloj, 50 % de 1) compilan y pasan las pruebas de PC y de la vista previa. Puntos 40 / 122 |
| 2026-10-16 | **35,0 %** | +2,25 | F9.2 Calculadora (50 % de 1,5), F9.3 Calendario (50 % de 1), F9.4 Flex Compass (50 % de 1) y F9.5 Device Care (50 % de 2) compilan y pasan las pruebas de PC y de la vista previa. Puntos 42,75 / 122 |
| 2026-10-16 | **36,3 %** | +1,5 | F5.1 Wi-Fi (watch_net: servicio de una tarea, redes guardadas con CRC, reintentos, SNTP al RTC, Ajustes > Wi-Fi con teclado propio y mosaico del panel) compila y pasa las pruebas de PC y de la vista previa: 50 % de 3. Puntos 44,25 / 122 |
| 2026-10-16 | **42,2 %** | +5,9 | Flex Phone por BLE: núcleo Flex Link v2 portado de la referencia (`components/flexlink`, 228 comprobaciones con vectores dorados y apretón de manos contra un teléfono simulado), servicio `watch_phone` con central NimBLE, app Flex Phone, notificaciones con aviso y respuestas, Música y mosaico Bluetooth. Compilan y pasan las pruebas de PC y de la vista previa: F5.2 (50 % de 4), F5.3 (50 % de 3), F5.4 (50 % de 2, D10 resuelta), F5.5 (50 % de 3), F9.9 (50 % de 1,5) y F9.10 (50 % de 1). Sin prueba de extremo a extremo hasta que la app Android sirva v2 por BLE. Puntos 51,5 / 122 |
| 2026-10-16 | **43,4 %** | +1,2 | F9.6 explorador de archivos (50 % de 1) y F9.7 Notas (50 % de 2): capa `watch_files` con la semántica de `FlexOS_FS` (66 comprobaciones sobre una carpeta real), teclado compartido en español, suite "notas" de la vista previa. Puntos 53 / 122 |
| 2026-10-16 | **43,9 %** | +0,5 | F9.11 Paint (50 % de 1): formato FXP1 de la referencia byte a byte (`watch_files/fxp.c`, 23 comprobaciones), lienzo con trazos guardados al levantar el dedo, deshacer real y galería con miniaturas reproducidas del archivo. Puntos 53,5 / 122 |
| 2026-10-17 | **44,7 %** | +1 | F9.8 Clima (50 % de 2): motor de `FlexOS_Weather` portado a C puro (`watch_weather`: parser, cadencia, ubicaciones; 225 comprobaciones con las respuestas reales de Open-Meteo de la referencia), servicio con `esp_http_client` sobre TLS verificado (paquete de CA, mbedTLS en PSRAM), NVS `flexwx` y app Clima con iconos procedurales; suite "clima" de la vista previa. Puntos 54,5 / 122 |
| 2026-10-20 | **48,0 %** | +4 | F6 Navegador (50 % de 8): núcleo de `FlexOS_Browser` pasado a C con el perfil S3 (las comprobaciones de `test_browser.cpp` sin tocar un vector + vectores dorados), sesión FBP/1 con WebSocket propio probada contra un servicio a nivel de bytes, TLS verificado con el paquete de CA (la referencia no verificaba), JPEG con el TJpgDec de LVGL, app con omnibox anti-SSRF, páginas `flex://`, pestañas, historial, favoritos y teclado de página; suite "navegador". Puntos 58,5 / 122 |
| 2026-10-20 | **50,8 %** | +3,5 | F7 OTA (50 % de 7): motor de `FlexOS_OTA` en C puro (`watch_ota/ota_core.c`: versiones con SemVer, manifiesto con JSON leído de verdad, instalación paso a paso sobre un transporte abstracto; 492 comprobaciones con los vectores de la especificación y el motor entero contra un servidor y una flash falsos), firma ECDSA P-256 obligatoria con el mbedTLS de IDF (la referencia no verificaba), comprobación de producto, chip y versión de la imagen, reversión automática con confirmación a los 60 s, reglas de batería, Ajustes > Actualizaciones, pantalla de instalación y avisos; `tools/ota/ota_sign.py` comprobado con la imagen real; suite "ota". Sin clave ni manifiesto publicados todavía: el reloj comprueba pero no instala. Puntos 62 / 122 |
| 2026-10-20 | **50,8 %** | 0 (peso +8, puntos +4) | **Ampliación del alcance**: fase FP (integración con Flex Phone, peso 8). La app Android entra en `FlexPhone/`, importada sin cambios y modificada encima. Las tres tareas compilan y pasan las pruebas de PC, así que cuentan al 50 %: FP.1 (50 % de 3), FP.2 (50 % de 2) y FP.3 (50 % de 3). Pruebas que respaldan ese 50 %: 2756 comprobaciones de PC con ASan, vista previa completa y con sanitizadores (semillas 2, 9 y 13), 244 pruebas Kotlin, interoperabilidad con el motor C del reloj y el firmware compilado. El APK no se construyó aquí. Además se corrigió un fallo de navegación anterior (tocar un aviso con el panel rápido abierto dejaba la página debajo del panel). Puntos 66 / 130. Verificado en el reloj: 10,5 / 130 = 8,1 % |
| 2026-10-20 | **50,7 %** | −0,1 (peso +8, puntos +4) | **Ampliación del alcance**: fase F10 (experiencia Watch Ultra, peso 8). Las cinco tareas compilan con ESP-IDF 5.5.5 y pasan las pruebas de PC (3019 comprobaciones con ASan/UBSan) y la vista previa completa, también con sanitizadores (semillas 2, 9 y 13) y la batería nueva `esferas`: 50 % de 8. Además, sin peso: cinco causas de fallo del Wi-Fi corregidas en `watch_net` (sin reproducir en el reloj), splash de 3 s como mínimo y medidor de FPS fuera de la interfaz. Puntos 70 / 138. Verificado en el reloj: 10,5 / 138 = 7,6 % |
| 2026-10-23 | **51,7 %** | +1,0 (peso +6, puntos +4,5) | **Ampliación del alcance**: fase FG (Galería compartida, peso 6), a petición del usuario. Sus cuatro tareas compilan con ESP-IDF 5.5.5 y pasan las pruebas de PC: 344 comprobaciones de la Galería además de las 3019 del firmware, con ASan/UBSan; 40 de extremo a extremo contra el Flex Web Server real del P4 y el servidor real de Flex Cloud; vista previa completa con la batería nueva «galeria», también con sanitizadores. Cuentan al 50 % de 6. Además F8.1 (50 % de 2) y F8.2 (50 % de 1), que la Galería implementa (D11 resuelta). Puntos 74,5 / 144. Verificado en el reloj: 10,5 / 144 = 7,3 % |
