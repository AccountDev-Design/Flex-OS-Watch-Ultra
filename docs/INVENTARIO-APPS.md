# Inventario de aplicaciones — Flex OS Ultra → Flex OS Watch Ultra

Referencia: `AccountDev-Design/Flex-OS-Ecosistema`, rama `claude/flexos-ota-system-blq9g9-2csacx` @ `5dbc949`
(la misma revisión auditada en F0). Todas las rutas de "Origen" son relativas a `FlexOS_Ultra/`.

El registro real de apps es `APP_REG[APP_N]` (`FlexOS_Ultra_AppFramework.h:739`, `APP_N = 19`) con los nombres de
`APP[APP_N][5]` (`FlexOS_Ultra_Session.h:345`) y los iconos de `drawAppIcon()` (`FlexOS_Ultra_Icons.h:65-305`).
No hay más apps nativas. Las apps instalables de Flex Store (`FlexOS_AppVM`, bytecode) se tratan aparte (fila 15).

**Estados:** ✅ verificado en el reloj · 🟡 implementado, compilado y probado en el PC · 🔄 en curso ·
⬜ pendiente · ⛔ excluido · ⚠️ adaptado por una limitación de hardware

## 1. Las 19 apps del registro

| # | App (id) | Icono original | Diseño y funciones reales en Flex OS Ultra | Hardware | Conectividad | En el reloj | Estado |
|---|---|---|---|---|---|---|---|
| 0 | **Reloj** (`IC_RELOJ`) | esfera blanca con agujas (Icons.h:105) | Pestañas Hora / Cronómetro / Temporizador. El temporizador es un marcador "próximamente" (AppFramework.h:600-630, AppChrono.h) | RTC | — | App **Reloj** que agrupa Alarmas, Cronómetro y Temporizador (los tres con motor real, F4.3: el reloj ya supera a la referencia) | 🟡 |
| 1 | Galería (`IC_GALERIA`) | flor de 8 pétalos (Icons.h:120) | Miniaturas, visor, edición de foto y vídeo (AppGallery.h, GalleryEdit.h, GalleryVideoEdit.h) | PSRAM 32 MB, cámara | — | Sin cámara ni microSD montada; la edición no cabe en 8 MB. Visor de imágenes de LittleFS posible más adelante | ⛔ (edición) · ⬜ (visor, ❓ D13) |
| 2 | Multimedia (`IC_MULTIMEDIA`) | triángulo de reproducción (Icons.h:133) | Reproductor de vídeo/audio de escritorio (AppMultimedia.h, MediaViewer.h) | decodificador de vídeo del P4 | — | Excluido en F0 (vídeo); el control de la música del móvil va en Música | ⛔ |
| 3 | **Almacenamiento** (`IC_ALMACEN`) | carpeta con nube (Icons.h:139) | Uso del almacenamiento, aviso de poco espacio, explorador (AppStorage.h, AppFiles.h) | flash | — | Ajustes > Almacenamiento (F4.6) + explorador de LittleFS | 🟡 (uso) · ⬜ (explorador) |
| 4 | Modo PC (`IC_MODOPC`) | monitor (Icons.h:150) | Samsung DeX / escritorio (DeX.h, DeXDraw.h, DeXInput.h) | HDMI/USB host | — | **Excluido por decisión del proyecto** | ⛔ |
| 5 | **Notas** (`IC_NOTAS`) | libreta con lápiz (Icons.h:157) | Lista de notas en /Notas, editor con teclado, sesión (AppNotes.h, Keyboard.h) | — | — | Lista y lectura en LittleFS; creación con teclado compacto | ⬜ |
| 6 | **Navegador** (`IC_NAV`) | globo terráqueo (Icons.h:165) | Navegador por renderizado remoto FBP/1 (BrowserApp.cpp, `server/`, relé del móvil `relay/`) | — | Wi-Fi | Cliente FBP/1 (WebSocket + JPEG). Sin servidor ni móvil no hay web. Nunca motor HTML local | ⬜ (F6) |
| 7 | **Flex Compass** (`IC_BRUJULA`) | rosa de los vientos (Icons.h:278) | Brújula con BNO085 (9 ejes). Sin BNO085 muestra la pantalla de requisito, nunca un rumbo falso (AppCompass.h) | magnetómetro | — | ⚠️ El QMI8658 del reloj no tiene magnetómetro: pantalla de requisito fiel a la referencia + cabeceo/alabeo reales del acelerómetro | ⬜ |
| 8 | Paint (`IC_PAINT`) | paleta de pintor (Icons.h:170) | Lienzo .fxp en /Paint (AppPaint.h) | táctil | — | Lienzo reducido posible (baja prioridad en un reloj) | ⬜ (baja) |
| 9 | Juegos (`IC_JUEGOS`) | mando (Icons.h:181) | Jumper, recreación de Geometry Dash (Jumper.h) | — | — | Excluido en F0 (riesgo de propiedad intelectual) | ⛔ |
| 10 | **Ajustes** (`IC_AJUSTES`) | engranaje gris (Icons.h:201) | 12 categorías (AppSettings.h:78): General, Pantalla, Sonido, Red e Internet, Dispositivos, Personalización, Seguridad, Batería, Almacenamiento, Desarrollador, Sistema, Acerca de | — | — | Pantalla, Fecha y hora, Batería, Diagnóstico, Almacenamiento, Acerca de con datos reales; Fondo de pantalla; Red (Wi-Fi) y Bluetooth al llegar F5 | 🟡 |
| 11 | **Calculadora** (`IC_CALC`) | teclado con columna naranja (Icons.h:212) | Rejilla 5×4, línea de operación "25 + 10", errores "Error", sesión (AppsBasic.h:30-337) | — | — | Lógica transcrita a C (`calc_logic.c`) + rejilla 5×4 adaptada a 410×502 | ⬜ |
| 12 | **Calendario** (`IC_CALEND`) | hoja con franja roja y "1" (Icons.h:221) | Vista de mes, hoy resaltado con círculo del acento (AppsBasic.h:343-409) | RTC | — | Vista de mes con navegación entre meses | ⬜ |
| 13 | Cámara (`IC_CAMARA`) | objetivo (Icons.h:296) | Cámara OV/MIPI (AppCamera.h) | cámara | — | El reloj no tiene cámara | ⛔ (sin hardware) |
| 14 | **Clima** (`IC_CLIMA`) | sol tras nube (Icons.h:226) | Datos reales de Open-Meteo, horas, días, ubicaciones, buscador (AppWeather.h, Weather.cpp) | — | Wi-Fi (HTTPS) | Misma fuente (Open-Meteo) por Wi-Fi; "sin conexión"/"datos de hh:mm" cuando no hay red | ⬜ (tras F5.1) |
| 15 | Flex Store (`IC_FLEXSTORE`) | bolsa (Icons.h:240) | Tienda de paquetes .flexpkg con firma y máquina virtual FlexVM (Store.cpp, AppVM.cpp, PkgCore.cpp) | flash | Wi-Fi | Evaluable cuando haya Wi-Fi y OTA: la VM es C++ puro. No prioritario | ⬜ (❓) |
| 16 | **Flex Phone** (`IC_FLEXPHONE`) | teléfono con ondas (Icons.h:253) | Emparejar, estado del enlace, notificaciones, buscar teléfono, multimedia (FlexPhone*.h/.cpp, android/) | — | BLE (reloj) / Wi-Fi | App de enlace por **BLE** (el reloj es el primer equipo con radio BLE: el `GattServer.kt` de Android estaba escrito para esto) | ⬜ (F5) |
| 17 | **Device Care** (`IC_DEVCARE`) | escudo con pulso (Icons.h:267) | Estado del dispositivo, salud, optimizar, historial, pruebas, detección de caídas (DeviceCare.h, DeviceTests.h, FallAlert.h) | IMU 9 ejes | — | Estado real (batería, memoria, almacenamiento, temperatura del chip), pruebas de pantalla/táctil/IMU. Caídas: ⚠️ recalibrar para 6 ejes (más adelante) | ⬜ |
| 18 | **Música** (`IC_MUSICA`) | dos corcheas (Icons.h:259) | Biblioteca local y reproductor (AppMusic.h, Audio.cpp) | I2S + códec | — | Control de la música del **teléfono** (T_MEDIA_STATE/T_MEDIA_CMD de Flex Link, `MediaBridge.kt`). Altavoz local ❓ D13 | ⬜ (F5) |

Apps propias del reloj sin equivalente directo en el registro (se conservan): **Sensores** (nivel y movimiento,
F4.4), que pasa a ser parte de Device Care / Flex Compass.

## 2. Superficies del sistema (no son apps del registro)

| Superficie | Origen | En el reloj | Estado |
|---|---|---|---|
| Esfera / pantalla de bloqueo | `Lock.h`, `Clock.h` | Esfera con hora del RTC, fecha y batería real | 🟡 |
| Escritorio (Inicio) y dock | `Home.h`, `HomeCfg.h` (rejilla 4×N, iconos de 72 px) | La esfera hace de Inicio; el escritorio de 4 columnas no cabe en 410 px | ⚠️ |
| Caja de aplicaciones | `AppDrawer.h` (4 columnas, iconos de 72 px, etiqueta debajo, buscador) | **Lanzador en rejilla de 3 columnas con los iconos originales** (mismo tamaño físico) | ⬜ (F3.5) |
| Panel rápido | `QuickPanel*.h` (22 mosaicos: Wi-Fi, BLE, brillo, ahorro, No molestar...) | Cortina con Wi-Fi, Bluetooth, batería, Ajustes y brillo | 🟡 (Wi-Fi/BT "Pendiente") |
| Notificaciones | `Notif.h`, `FlexPhone_Overlay.h` | Llegan con Flex Phone (F5.3) | ⬜ |
| Recientes | `AppSwitcher.h` | Pila de navegación + gesto volver | ⚠️ (no se replica la multitarea) |
| Teclado | `Keyboard.h` (QWERTY completo, 480 px) | Teclado compacto por páginas para 410 px | ⬜ |
| Fondos de pantalla | `Wallpaper.h` (8 fondos procedurales) | Los 8 fondos regenerados para 410×502 + negro AMOLED | ⬜ (F3.6) |
| Temas | `Wallpaper.h:504` (`LOOKS`, 8 temas) | Tema "AMOLED" por defecto (fondo negro, acento #788CFF) | 🟡 (solo AMOLED) |
| OTA | `FlexOS_OTA.cpp` | F7 | ⬜ |
| Antirrobo, cuenta, nube | `Theft*.h`, `FlexOS_Account`, `FlexOS_Cloud` | F8 (❓ D11) | ⬜ |
| Recuperación | `Recovery.h` | Modo seguro: pendiente de evaluar | ⬜ |

## 3. Excluidas y por qué

| Función | Motivo |
|---|---|
| **Modo PC / Samsung DeX** (`DeX.h`, `DeXDraw.h`, `DeXInput.h`, mosaico `QSID_DEX`) | Exclusión explícita del proyecto: no tiene sentido en un reloj. No se migra, no se reserva memoria ni se dibuja su icono |
| Cámara, Galería con edición, Multimedia de vídeo | Sin cámara; la edición de imagen y vídeo necesita la PSRAM de 32 MB del P4 |
| Juegos (Jumper) | Recrea Geometry Dash: riesgo de propiedad intelectual (F0) |
| Servidor web de subida, kiosco | Funciones de escritorio |
| Drivers del P4/C6 (BNO085, esp-hosted, DSI, GT911) | Hardware que el reloj no tiene |
