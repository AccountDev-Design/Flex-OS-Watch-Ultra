# Flex OS Watch Ultra — Auditoría Fase 0 (solo lectura)

Fecha: 2026-10-15. Ningún archivo de firmware ni de referencia se ha modificado.
Este documento es la única salida de la auditoría.

## 0. Fuentes y aislamiento

| Rol | Fuente | Revisión |
|---|---|---|
| Destino | `AccountDev-Design/Flex-OS-Watch-Ultra` (estaba vacío: sin commits ni ramas) | rama `ccr-b6cde864-7hvm3w` |
| Hardware oficial | `waveshareteam/ESP32-S3-Touch-AMOLED-2.06` (Apache-2.0), esquema `Schematic/ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf` | `b099739` (2026-08-21) |
| BSP oficial | `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0 en `waveshareteam/Waveshare-ESP32-components` (Apache-2.0) | `23a6012` |
| Referencia Arduino | `accountdev-design/Flex-OS-Ecosistema`, rama `claude/flexos-ota-system-blq9g9-2csacx` | `5dbc949` |
| Solo consulta | rama `flexos-esp-idf` (migración P4 paralizada: 5dbc949 + 28 commits, ESP-IDF 5.5.5, LVGL 9.6.0) | `35387fc` |

- La rama `claude/flexos-ota-system-blq9g9` **ya no existe**. Comprobé la ascendencia de todas las ramas: las demás ramas Arduino son ancestros de `…-2csacx`, que es la punta de la línea Arduino.
- **`FlexOS_Ultra_S3.ino` NO es el reloj**: es otra placa ESP32-S3 N16R8 con TFT ST7796 de 480×320, táctil XPT2046 y cámara OV2640. No se usará como base.
- **Pendiente:** el firmware oficial que el usuario probó en su máquina no está en este entorno. Tampoco su versión local de ESP-IDF (el contenedor no tiene ESP-IDF ni el reloj conectado).

## 1. Hardware del reloj

Estados posibles: **V** = verificado, **P** = pendiente de verificación, **D** = discrepancia con la imagen de pines de marketing.

| Elemento | Dato | Estado | Fuente |
|---|---|---|---|
| SoC | ESP32-S3**R8** (8 MB de PSRAM **octal** en el encapsulado; los GPIO 33-37 quedan reservados) | V | esquema U2 |
| PSRAM | `CONFIG_SPIRAM_MODE_OCT`, 80 MHz | V | `02_lvgl_demo_v9/sdkconfig.defaults` |
| Flash | GD25Q256EYIGR = **32 MB** | V | esquema U3 |
| Tamaño de flash configurado | Los ejemplos oficiales configuran **16 MB** (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`) | D | sdkconfig.defaults (los 6 ejemplos) |
| Pantalla | AMOLED de 2,06", 410×502, **CO5300** por QSPI | V | README oficial; BSP `display.h:19-20` |
| Driver de pantalla | `waveshare/esp_lcd_sh8601` (modo QSPI). RGB565 big-endian (LVGL 9: `swap_bytes=true`) | V | BSP `.c:423-460`, `:542` |
| Pines de pantalla | CS 12, CLK 11, D0-D3 4/5/6/7, RST 8, TE 13. El BSP no usa TE | V | BSP `.h:46-54`; esquema |
| Desfase de columna | `esp_lcd_panel_set_gap(0x16, 0)`; CASET 0x16..0x1AF | V | BSP `.c:61`, `:456` |
| Actualización parcial | El área debe empezar en coordenada par y terminar en impar (*rounder*) | V | BSP `.c:390-404` |
| Brillo | DCS 0x51 (no hay GPIO de retroiluminación) | V | BSP `.c:60-64`, `:357`; `BSP_LCD_BACKLIGHT NC` |
| Alimentación de la pantalla | DSI_PWR_EN sale de un riel del AXP2101 | **P** (riel exacto) | esquema |
| Táctil | FT3168 en I2C 0x38, driver `esp_lcd_touch_ft5x06`. RST 9, INT 38 | V | README; BSP `.h:55-56` |
| Bus I2C | SDA 15, SCL 14. Driver `i2c_master` nuevo, con el handle compartido (`bsp_i2c_get_handle()`) | V | BSP `.h:35-36`, `.c:79-110` |
| PMIC | AXP2101 en 0x34 | V | XPowersLib `AXP2101Constants.h:3` |
| Rieles del AXP2101 | DCDC1 VCC3V3, ALDO1/ALDO2 3,3 V, ALDO3 VCC3V, ALDO4 1,8 V, BLDO2 2,8 V, CPUSLDO 1,2 V. VBAT2 carga la reserva del RTC | P (texto extraído del PDF; falta saber qué riel alimenta qué periférico) | esquema |
| Interrupción del AXP2101 | El ejemplo oficial no la usa (`CONFIG_PMU_INTERRUPT_PIN=-1`). Se leerá por sondeo | V (ejemplo) | `01_AXP2101/sdkconfig.defaults` |
| IMU | QMI8658C en 0x6B (`QMI8658_L_SLAVE_ADDRESS`), INT1 en GPIO21 | V | ejemplo Arduino 04; SensorLib; esquema |
| RTC | PCF85063 en 0x51, INT en GPIO39. VBACKUP/VCC-RTC salen del AXP2101 | V. **P**: si hay pila o supercondensador | ejemplo Arduino 03; esquema |
| Audio | ES8311 (0x18) para el altavoz, con PA en GPIO46; ES7210 (0x40) con 2 micrófonos. I2S: MCLK 16, SCLK 41, LRCK 45, DOUT 40, DIN 42 | V | BSP `.h:38-43`, `.c:261`, `:309` |
| microSD | Conectada a GPIO1 (CMD/MOSI), 2 (CLK), 3 (D0/MISO) y 17 (D3/CS). El BSP la usa en **SDMMC de 1 bit**; la imagen la presenta como SPI | V / D (las dos formas son válidas) | BSP `.h:59-61`, `.c:149-182`; esquema |
| Botones | BOOT en GPIO0. PWR: red `SYS_OUT` en GPIO10 + PWRON del AXP2101 | **P** (comportamiento exacto; el BSP declara `BSP_CAPS_BUTTONS 0`) | esquema |
| Motor de vibración | Está en el esquema (red MOTOR, probablemente GPIO18). No aparece en la imagen de pines | **P** (GPIO) / D | esquema |
| USB | USB nativo (GPIO19/20) | V | esquema |
| Radio | Wi-Fi 2,4 GHz + BLE 5 nativos (no hay coprocesador, a diferencia del P4/C6) | V | familia ESP32-S3 |
| Batería | Conector BAT del AXP2101 | **P**: capacidad y química exacta | — |
| Medida de la batería | Solo a través del AXP2101 (no se ha visto ningún divisor hacia el ADC del ESP32) | **P** (confirmar visualmente en el esquema) | esquema (texto) |

**Pila oficial:**
- BSP v2.0.0: requiere `idf >=5.3`, `lvgl >=8,<10`, `esp_lvgl_port ^2`, `esp_codec_dev ~1.5`, `esp_lcd_sh8601` y `esp_lcd_touch_ft5x06`.
- El ejemplo LVGL 9 declara `lvgl 9.5.0`, pero su `dependencies.lock` está desactualizado (lvgl 9.2.0, idf 5.5.0, BSP 1.0.3). Es una incoherencia del repositorio oficial.
- XPowersLib y SensorLib: MIT (Lewis He).

## 2. Controladores oficiales que se conservan

1. **BSP oficial como componente gestionado** (sin copiarlo). Se usan sus funciones de bajo nivel:
   - `bsp_i2c_init` / `bsp_i2c_get_handle`
   - `bsp_display_new` (CO5300: comandos de inicialización, desfase 0x16)
   - `bsp_touch_new` (FT3168)
   - `bsp_display_brightness_set`
   - `bsp_sdcard_mount` y `bsp_audio_*`

   La configuración de LVGL (buffers, tareas) es propia, no la de `bsp_display_start`.
2. **XPowersLib (MIT) para el AXP2101**, con el mismo método de acceso a registros que `01_AXP2101/main/port_axp2101.cpp`, pero sobre el bus I2C compartido.
3. **SensorLib (MIT)** para el QMI8658 y el PCF85063, o drivers mínimos propios si SensorLib no encaja en ESP-IDF puro. Se decidirá en la Fase 4.

## 3. Método de batería (código oficial + XPowersLib)

| Dato | Registro y bits | Notas |
|---|---|---|
| Batería presente | 0x00 bit 3 | `isBatteryConnect` |
| VBUS correcto | 0x00 bit 5 | `isVbusGood` |
| VBUS conectado | 0x01 bit 3 == 0 **y** VBUS correcto | `isVbusIn` |
| Sentido de la corriente | 0x01 bits [7:5]: 0 reposo, 1 cargando, 2 descargando | `isStandby` / `isCharging` / `isDischarge` |
| Estado del cargador | 0x01 bits [2:0]: 0 tri, 1 precarga, 2 CC, 3 CV, **4 terminada**, 5 sin carga | `getChargerStatus` |
| Porcentaje | 0xA4: *fuel gauge* interno del AXP2101. Solo es válido con batería presente | `getBatteryPercent` |
| Tensión VBAT | 0x34-0x35, 13 bits en mV. Requiere tener el ADC habilitado (0x30 bit 0) | `getBattVoltage` |

**Escrituras que hace el ejemplo oficial:**
- Habilitar los canales del ADC (0x30).
- **Desactivar la medida del pin TS** (0x30 bit 1). El comentario oficial dice que, sin NTC, la carga es anómala si no se hace.
- Cargador: precarga 50 mA, corriente constante 400 mA, terminación 25 mA y 4,2 V.

**Política del reloj:**
- El Bloque 1 solo lee.
- Después se aplican únicamente los ajustes del ADC y del pin TS.
- **No se escriben corrientes ni tensiones de carga** mientras no se confirme la capacidad de la batería. Se conservan los valores OTP.

## 4. Reutilización de Flex OS Ultra (5dbc949)

El P4 Arduino **no usa LVGL**: tiene un motor gráfico propio. Toda la interfaz se reimplementa, y lo que se reutiliza son los tokens, las fórmulas y la lógica.

| Módulo | Veredicto | Motivo |
|---|---|---|
| Paleta `FlexTheme` oscura y clara (`FlexOS_Ultra_Theme.h:113-174`), temas `LOOKS` (`Wallpaper.h:504-513`) | **Transcribir** | Valores exactos. Por defecto, tema AMOLED (negro, acento #788CFF) |
| Material Liquid Glass (`Theme.h:404-464`: nivel 50 = R6, tinte 46/70, especular 26, sombra 30, borde 156/104) | **Adaptar** | En el S3: fondo desenfocado **una sola vez** + tinte, degradado y borde por capas (patrón de `flex_glass.c` de la rama IDF). Sin desenfoque por cuadro. Se documentará como "transparencia sobre fondo pre-desenfocado", no como desenfoque en vivo |
| Radios, movimientos y gestos (apertura de app 210/190 ms, ease-out cúbico; cortina 1:1 con umbral del 40 %; aviso 220/4200/180 ms) | **Transcribir y escalar** | Factor 410/480 ≈ 0,854 y rediseño para pantalla vertical pequeña |
| Fuente Outfit (SIL OFL 1.1, TTF y licencia en la rama IDF `tools/fonts/`) | **Reutilizar** con aviso OFL | Regenerar con `lv_font_conv` para los tamaños del reloj. Las fuentes IDF generadas incluyen glifos FontAwesome y DejaVu cuya licencia no está documentada: no copiarlas, regenerarlas |
| Iconos vectoriales (`Icons.h`, `QuickPanel.h`) | **Reimplementar** en LVGL | No hay mapas de bits. **No existe icono de alarma ni animación de carga**: son diseño nuevo |
| `FlexOS_FlexLink`, `FlexAuth`, `FlexPhone_Transport`, `FlexPhone_Discovery.h`, `FlexPhone` (modelo y codecs) | **Tal cual** (C++ puro, probado en PC; ya compila en ESP-IDF 5.5.5 en la rama IDF) | Copia con hash de procedencia |
| `FlexPhone_Link.cpp` | Adaptar | Máquina de estados pura. Textos y caminos del P4/C6 |
| `FlexPhone_WiFi.h`, `_Bridge.h` | Reescribir | Arduino `WiFi*`/`Preferences` → sockets lwIP, `esp_netif`, NVS |
| `FlexOS_Browser.cpp` (cliente FBP/1, perfil S3 ya definido) | Tal cual | Puro |
| `FlexOS_BrowserApp.cpp` | Adaptar | `esp_websocket_client` + esp-tls **con validación** (hoy usa `setInsecure`) |
| `FlexOS_JPEG.cpp` | **No reutilizar** | La IDCT parece derivada de IJG (`jidctint.c`) sin atribución. Usar `esp_jpeg` de Espressif |
| `FlexOS_Mem` (histéresis asimétrica), patrón anti-spam de avisos | Adaptar | Umbrales fijados para 32 MB de PSRAM; parametrizar para 8 MB |
| `FlexOS_FS` (escritura atómica), `Passcode`, `FallDetect` | Adaptar | Pasar a VFS/NVS. Las caídas se recalibran para la muñeca con IMU de 6 ejes |
| `FlexOS_OTA` | Solo referencia | Hoy solo MD5, firma no implementada y `ota/manifest.json` **inválido**. En un S3 elegiría la clave `flex_os_ultra_s3` (otra placa) → el reloj necesita la clave **`flex_os_watch_ultra`** |
| `FlexOS_Account` / `CloudCore` | Posponer o adaptar | Servidor externo `…chatgpt.site` sin código en el repo; token en claro en NVS |
| Patrones de la rama IDF (consulta): `flex_inbox` (buzón de UI), `flex_bus`, I2C con recuperación, KV con escritor único, modo seguro, simulador LVGL de PC | Referencia de diseño | Específico del P4: DSI/DPI, GT911, 480×800 fijos |
| **Batería** | **Nada que reutilizar** | El P4 pinta un 82 % fijo (`Icons.h:342` y todas sus llamadas) |
| DeX/Modo PC, cámara, galería y edición, multimedia, servidor web, Jumper (recrea Geometry Dash: riesgo de PI), kiosco, BNO085, C6/esp-hosted, `FlexOS_Ultra_S3.ino`, `FlexOS_Pro.ino` | **Excluir** | — |

**Licencias:**
- El repositorio de referencia **no tiene LICENSE**.
- "DeX", "One UI" y "Liquid Glass" son denominaciones de terceros. Conviene renombrar en el producto (por ejemplo, "Flex Glass").

## 5. Plan mínimo

**Batería y carga (`watch_power`):**
1. Lectura en una tarea propia: cada 2 s con la pantalla encendida, cada 30 s con ella apagada.
2. Filtrado: mediana de 3 + EMA. En descarga, el porcentaje no sube; con lectura inválida se muestra "--".
3. Estados: SIN_DATOS, DESCARGA, BAJA (≤15 %, sale con ≥18 %), CRÍTICA (≤5 %), CARGANDO (sentido = 1 y estado del cargador 1-3, confirmado en 2 lecturas), CARGA_COMPLETA (estado del cargador = 4), EXTERNA_SIN_CARGA (VBUS correcto y no carga).
4. Los umbrales son provisionales hasta medir el reloj real.
5. La animación de carga se dispara **solo** con la transición confirmada a CARGANDO, nunca solo con VBUS.
6. La lógica pura (máquina de estados y filtros) se prueba en el PC.

**Interfaz LVGL 9:**
- Dos buffers parciales en RAM interna con DMA (410×N líneas, N medido), con el *rounder* par/impar del BSP.
- LVGL solo en la tarea de UI; el resto publica mediante un buzón.
- Tema = tokens Flex de la sección 4.
- Pantallas iniciales: esfera, barra de estado con batería real, panel rápido, ajustes y batería.

**Wi-Fi y Flex Phone:**
- **Primero**: topología A (misma LAN), cliente compatible con Flex Link v2 reutilizando los módulos puros, con transporte lwIP propio.
- Limitaciones verificadas en la app Android: un solo vínculo y una sesión; no hay tipo de dispositivo; el hotspot está bloqueado; Flex Link v2 va **sin cifrado**.
- Un v3 con ECDH + AEAD y la topología B (SoftAP del reloj) requieren cambios en Flex Phone, en su propio repositorio.

## 6. Riesgos y datos sin verificar

1. **Flash de 32 MB**: los ejemplos oficiales usan 16 MB. *Actualizado en B1:* ESP-IDF 5.5.5 admite datos por encima de 16 MB en el GD25Q256 (solo la ejecución por encima de 16 MB es experimental), así que se declaran 32 MB. Ver `docs/FLASH-32MB.md`.
2. **Batería**: falta la capacidad, saber si hay NTC (el código oficial indica que no) y la precisión del *fuel gauge* sin calibrar.
3. **Rieles**: qué riel alimenta la pantalla (DSI_PWR_EN), el GPIO del motor, el comportamiento del PWR/GPIO10 y si hay reserva del RTC.
4. **Versiones**: la ESP-IDF y la LVGL locales del usuario son desconocidas; el repositorio oficial es incoherente (9.5.0 frente a 9.2.0 en el lock).
5. **Memoria**: 8 MB de PSRAM; un cuadro completo ocupa 402 KiB. No hay FPS ni consumo medidos.
6. **Seguridad**: Flex Link v2 va en claro (fuerza bruta del código offline, MITM por relevo); NVS sin cifrar; TLS sin validar en el navegador y la Store de referencia.
7. **Navegador**: renderizado remoto (Chromium en el servidor o WebView del móvil). Sin servidor no hay web. Nunca será un motor HTML local.
8. **Wi-Fi**: los problemas del P4 vienen de esp-hosted/SDIO hacia el C6 y no se heredan, pero Flex Link nunca ha corrido sobre radio nativa.

## 7. Progreso (ponderación fija desde hoy)

Cada fase cuenta el **50 % al compilar y pasar las pruebas de PC**, y el **100 % solo con la prueba física en el reloj**.

| Fase | Peso | Estado |
|---|---|---|
| F0 Auditoría y plan | 5 | 4/5 (faltan la versión local de ESP-IDF y el firmware probado) |
| F1 Arranque: BSP, pantalla, táctil, LVGL 9, I2C | 10 | 0 |
| F2 Batería, estados y animación de carga | 15 | 0 |
| F3 Tema Flex y shell (esfera, barra, panel rápido, ajustes) | 20 | 0 |
| F4 Servicios: KV/NVS, RTC+SNTP, IMU, suspensión y brillo | 10 | 0 |
| F5 Wi-Fi + `flex_link` (Flex Phone v2, notificaciones) | 15 | 0 |
| F6 Navegador remoto (FBP/1) | 8 | 0 |
| F7 OTA firmado + rollback | 7 | 0 |
| F8 Account/Cloud opcional, consumo y estabilidad | 10 | 0 |
| **Global** | 100 | **4 %** (estado vigente en `docs/PROGRESO.md`) |

## 8. Próximo bloque: B1 "Arranque verificable"

Archivos que se crean (todos nuevos, en este repositorio):

- `CMakeLists.txt`, `sdkconfig.defaults` (esp32s3, PSRAM octal, flash de 16 MB), `partitions.csv`
- `main/CMakeLists.txt`, `main/idf_component.yml` (BSP `^2.0.0`, `lvgl` fijado a la versión que confirme el usuario, `esp_lvgl_port ^2`, XPowersLib), `main/app_main.c`
- `components/watch_display/`: LVGL 9 sobre `bsp_display_new`, con buffers parciales, *rounder* y tarea de UI
- `components/watch_touch/`: `bsp_touch_new` → *indev* de LVGL
- `components/watch_power/`: AXP2101 **solo lectura**, registro por serie + lógica pura con pruebas de PC
- `tests/host/`: pruebas de la lógica de batería
- `THIRD_PARTY_NOTICES.md`

Criterio de aceptación en el reloj:
- La pantalla se ve sin desfase.
- El táctil devuelve coordenadas correctas.
- El serie muestra VBAT, porcentaje, VBUS y estado del cargador **coherentes** con y sin cable.
- Se registran la RAM y la PSRAM libres.
