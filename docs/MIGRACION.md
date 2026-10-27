# Mapa de migración — Flex OS Ultra (ESP32-P4, Arduino) → Flex OS Watch Ultra (ESP32-S3, ESP-IDF + LVGL)

Referencia: `AccountDev-Design/Flex-OS-Ecosistema` @ `claude/flexos-ota-system-blq9g9-2csacx` (`5dbc949`), solo lectura.
Destino: este repositorio, rama `ccr-b6cde864-7hvm3w`. Inventario de apps: [INVENTARIO-APPS.md](INVENTARIO-APPS.md).
Auditoría inicial: [AUDITORIA-FASE0.md](AUDITORIA-FASE0.md).

## 1. Diferencias que mandan

| | Flex OS Ultra (P4) | Watch Ultra (S3) | Consecuencia |
|---|---|---|---|
| Motor gráfico | propio (primitivas en `Gfx.h`, `Font.h`), sin LVGL | LVGL 9.5.0 | La interfaz se reimplementa en LVGL; se reutilizan tokens, geometría, algoritmos y lógica |
| Pantalla | 480×800, ~217 ppi, IPS | 410×502, ~315 ppi, AMOLED con esquinas redondeadas | Tamaño **físico** conservado: 72 px allí ≈ 104 px aquí. Negro puro = píxel apagado |
| PSRAM | 32 MB | 8 MB (octal) | Un solo fondo a la vez (402 KB), sin desenfoque por cuadro, recursos en flash |
| Radio | Wi-Fi por esp-hosted en un C6, **sin BLE** | Wi-Fi y BLE 5 nativos | El reloj puede usar el `GattServer.kt` de Flex Phone, que el P4 nunca pudo |
| Sensores | BNO085 (9 ejes, con magnetómetro) | QMI8658 (6 ejes, sin magnetómetro) | Sin rumbo de brújula: pantalla de requisito (como hace la referencia sin BNO085) |
| Marco | `setup()/loop()`, `String`, `Preferences`, `WiFi*`, `delay()` | `app_main`, FreeRTOS, NVS, `esp_wifi`, NimBLE | Nada de Arduino entra en el firmware |

## 2. Categorías del código de referencia y su destino

| Categoría | Ejemplos en la referencia | Destino en el reloj |
|---|---|---|
| Lógica pura (sin hardware) | calculadora `calcKey()` (AppsBasic.h:98), calendario, Flex Link (`FlexLink.kt` ↔ `FlexOS_FlexLink.cpp`), FlexAuth, codecs de Flex Phone, FBP/1 | **Transcrita a C** en módulos `*_logic.c` probados en el PC (`tests/host`) con los mismos vectores que la referencia |
| Componentes de interfaz | `drawLiquidGlassPanel`, `setRowCard`, cortina del panel, caja de apps | Componentes LVGL en `watch_theme`: motor de vidrio `flex_glass_*` ([LIQUID-GLASS.md](LIQUID-GLASS.md)), `watch_theme_list_row`... |
| Iconos y fondos | `drawAppIcon()` (Icons.h), `wall*()` (Wallpaper.h) — vectoriales/procedurales | **El mismo código de dibujo** ejecutado en el PC (`tools/icons`) o en el reloj (`flex_wallpapers.c`); ver §3 |
| Controladores | GT911, DSI, BNO085, ES8311 del P4 | BSP oficial de Waveshare (CO5300, FT3168) + controladores propios (AXP2101, PCF85063, QMI8658) |
| APIs de Arduino | `WiFi.begin`, `Preferences`, `String`, `millis`, `delay` | `esp_wifi` + `esp_netif`, NVS (`watch_settings`), `snprintf`, `esp_timer`, temporizadores de LVGL |
| Red | `WiFiClientSecure` con `setInsecure()` | `esp_http_client` / `esp-tls` **con validación de certificados** (bundle de ESP-IDF) |
| Almacenamiento | `FlexOS_FS` (LittleFS de Arduino) | `watch_storage` (LittleFS de joltwallet en `storage`, por encima de 16 MB) |
| Tareas | bucle único + tareas de red | tarea de LVGL única; red y BLE en sus tareas, entregan a la UI por buzón (`lv_async_call` con el bloqueo de LVGL) |

## 3. Identidad visual: componente por componente

| Componente original | Archivo:línea | Cómo se reproduce en el reloj | Estado |
|---|---|---|---|
| Paleta oscura `kThemeDark` | Theme.h:114-141 | `flex_tokens.h` (valores transcritos) | 🟡 |
| Tema AMOLED (`LOOKS[3]`, acento #788CFF) | Wallpaper.h:507 | Tema por defecto | 🟡 |
| Fuente Outfit | rama IDF `tools/fonts/` (OFL) | `flex_outfit_18/22/28/40/clock_120` generadas con `lv_font_conv` | 🟡 |
| Liquid Glass nivel 50 (tinte 46-70, especular 26, sombra 30, borde 156/104) | Theme.h:404-464, 524-530 | Tinte adaptativo precalculado + borde de luz; sin desenfoque (coste). Sobre negro: opaco | 🟡 |
| Tarjeta de fila de Ajustes (icono de color, título, valor, chevron) | AppSettings.h:399 (`setRowCard`) | `watch_theme_list_row` | 🟡 |
| **Iconos de app (19, vectoriales, "son marca")** | Icons.h:65-305 (`drawAppIcon`) | Generador de PC con las **mismas figuras y colores**, a 72 px de diseño escalados a 104 px con antialiasing por supermuestreo; imágenes RGB565A8 en flash | 🟡 F3.5 |
| Caja de aplicaciones (rejilla 4×N, icono 72 px, etiqueta centrada, recorte con "...") | AppDrawer.h:328-346, 648-725 | Rejilla de 3 columnas, icono 104 px (mismo tamaño físico), etiqueta centrada ajustada | 🟡 F3.5 |
| **Fondos (8)**: Flex Original, Aurora, Nocturno, Halo, Onyx, Océano, Violeta, Naturaleza | Wallpaper.h:42-315 | Las mismas funciones (`wallDiag3`, `wallDisc`, `wallRadial`, LUT de distancia) en C puro; posiciones y radios escalados a 410×502; tramado 4×4 para el RGB565 | 🟡 F3.6 |
| Movimiento: apertura 210 ms, cierre 190 ms, ease-out cúbico; aviso 220/4200/180 ms; cortina 1:1 con umbral del 40 % | AppFramework.h:866-940, Home.h:278-320 | `flex_tokens.h` + `ui_nav.c` / `ui_qp.c` / `ui_banner.c` | 🟡 |
| Panel rápido de vidrio | QuickPanelGlass.h | `ui_qp.c` | 🟡 |
| Estados pulsado / activo / no disponible | Widgets.h:593, QuickPanelGlass.h:154 | `LV_STATE_PRESSED/CHECKED/DISABLED` en los estilos del tema | 🟡 |

Liquid Glass sin desenfoque en vivo: la referencia desenfoca el fondo por cada panel (`drawLiquidGlassPanel`) y aun así
lo apaga en las rejillas que se desplazan ("ICONOS PLANOS A PROPÓSITO", AppDrawer.h:651). En el reloj el material se
calcula una vez (tinte de la luma real del fondo que queda debajo) y se dibuja como relleno + borde. Con 8 MB de PSRAM y
LVGL por software no hay sitio para un desenfoque por cuadro.

## 4. Conectividad

| Pieza original | Archivo | En el reloj |
|---|---|---|
| Protocolo Flex Link v2 (trama de 18 B, CRC16-CCITT, fragmentos de 226 B, anti-repetición, reensamblado) | `protocol/FlexLink.kt`, `FlexOS_FlexLink.cpp` | 🟡 `components/flexlink/flex_link.c` (port de la referencia) con los vectores dorados de `FlexLinkTest.kt` |
| Autenticación FlexAuth (reto-respuesta) | `protocol/FlexAuth.kt`, `FlexOS_FlexAuth.cpp` | 🟡 `flex_auth.c`: el SHA-256/HMAC en C puro de la referencia (mismo código en PC y reloj), con los vectores de `FlexAuthTest.kt` y RFC 4231 |
| Transporte Wi-Fi (`WifiLinkServer.kt`) | Android | Topología misma LAN (D6) |
| **Transporte BLE (`GattServer.kt`)**: servicio `f1580001-…`, RX `f1580002` (escritura), TX `f1580003` (notificación), MTU 247, permisos cifrados (exige vínculo) | Android, "escrito y preparado, hoy no se usa" | 🟡 Central NimBLE en el reloj (`watch_phone/ble_link.c`). **Requiere en Flex Phone**: activar `GattServer` junto al servidor Wi-Fi y pasarlo al apretón de manos v2 (lo dice su propia cabecera): lista en `FLEX-PHONE-ANDROID.md` |
| Notificaciones (`FlexNotificationListener.kt`, T_NOTIF_*) | Android | Lista de notificaciones en el reloj, solo las que el usuario autoriza en el móvil (`AllowedApps.kt`) |
| Navegador FBP/1 (`server/`, `relay/`) | Node.js / Android | Cliente FBP/1 por Wi-Fi (F6) |

## 5. Memoria (8 MB de PSRAM)

| Recurso | Dónde | Tamaño | Regla |
|---|---|---|---|
| Buffers de pantalla | RAM interna DMA | 2 × N líneas (medido en el reloj) | `watch_display.c` |
| Fondo de pantalla activo | PSRAM | 410×502×2 = 402 KB | **uno solo**; se libera al volver a negro |
| Iconos de app | flash (rodata; con XIP en PSRAM ocupan su copia) | 104×104×3 ≈ 31,7 KB cada uno | sin escalado en tiempo de ejecución |
| Fuentes | flash | ~150 KB | solo los tamaños usados |

Las cifras medidas del reloj están en `B1-ARRANQUE.md` y `B4-CALIDAD.md`; las nuevas se añaden en cada bloque.
