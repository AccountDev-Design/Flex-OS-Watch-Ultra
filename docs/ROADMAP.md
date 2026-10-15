# Hoja de ruta — Flex OS Watch Ultra

```
PROGRESO GLOBAL: 21 %   [████░░░░░░░░░░░░░░░░]   (verificado en el reloj: 7 %)
```

- **Completado:** auditoría y plan (F0); proyecto ESP-IDF flasheado y arrancando en el reloj (F1.1). Software de F2 (lecturas, estados, indicador, animación y avisos) hecho y probado en el PC.
- **En curso:** F3 (interfaz, tema y navegación): B3-1 hecho; sigue B3-2 (hora y esfera). F2 está hecha en software y espera las pruebas físicas.
- **Falta:** las pruebas físicas de B1/B2-1, el resto de batería, la esfera, el panel rápido, los ajustes, los servicios, Flex Phone, el navegador, OTA, la seguridad y las pruebas finales.
- **Siguiente:** flashear B2-1 y enviar el registro serie de las pruebas T14-T18 (`docs/B2-ENERGIA.md`).

## Cómo se calcula

- Cada fase tiene un **peso fijo** desde la Fase 0, que suma 100. Dentro de cada fase, las tareas reparten ese peso según su complejidad e importancia.
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

### F1 · Base ESP-IDF, pantalla y táctil — 6 / 10
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F1.1 Proyecto ESP-IDF 5.5.5 + BSP oficial; compilar, flashear y arrancar en el reloj | 2 | ✅ (confirmado por el usuario) | 2 |
| F1.2 Pantalla CO5300: imagen completa, desfase 0x16, brillo | 3 | 🟡 falta T2 | 1,5 |
| F1.3 Táctil FT3168: coordenadas y ejes | 2 | 🟡 falta T3 | 1 |
| F1.4 LVGL 9.5: buffers DMA medidos, FPS y memoria medidos | 2 | 🟡 faltan T9 y T11 | 1 |
| F1.5 Estabilidad: 30 min sin reinicios | 1 | 🟡 falta T12 | 0,5 |

### F2 · Batería y energía — 7,5 / 15
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F2.1 Lectura del AXP2101 y estados (porcentaje, VBAT, USB, carga, completa, externa sin carga; filtrado, histéresis, errores). B1 + **B2-1** | 9 | 🔄🟡 faltan T4-T8 y T14-T18 | 4,5 |
| F2.2 Indicador de batería, animación de carga y avisos de batería baja (B2-3, B2-4, B2-5) | 6 | 🟡 faltan T19-T22 | 3 |

Dentro de F2 queda:
- **B2-2:** decisión sobre el ADC y el pin TS (❓ D1). Necesita los registros reales de T14.
- Calibración de los umbrales con la batería real (❓ D4).

### F3 · Interfaz, tema y navegación — 2,5 / 20
| Tarea | Peso | Estado | Puntos |
|---|---|---|---|
| F3.1 Identidad Flex OS Ultra: paleta, Outfit, iconos, radios, fondo Onyx, **Liquid Glass** base (tinte adaptativo, brillo, sombra, bordes). **B3-1:** 12 iconos vectoriales, estados pulsado/activo/no disponible, cabecera, lista y fila de Ajustes (`docs/B3-INTERFAZ.md`). Pendiente: fondo pre-desenfocado para fondos con detalle | 5 | 🟡 faltan T2, T13 y T23-T25 | 2,5 |
| F3.2 Esfera del reloj (hora real, fecha, batería). Depende de F4.2 | 5 | ⬜ | 0 |
| F3.3 Panel rápido | 5 | ⬜ | 0 |
| F3.4 Ajustes, navegación por gestos y **animaciones/transiciones** (apertura y cierre 210/190 ms, volver, arranque) | 5 | ⬜ | 0 |

### F4 · Servicios del sistema — 0 / 10
| Tarea | Peso | Estado |
|---|---|---|
| F4.1 Ajustes persistentes (NVS, escritor único) | 2 | ⬜ |
| F4.2 Hora: RTC PCF85063 + zona horaria (+ SNTP cuando haya Wi-Fi) | 2 | ⬜ |
| F4.3 Alarmas y funciones temporales (alcance concreto ❓ D15) | 2 | ⬜ |
| F4.4 IMU QMI8658 + información de batería y sensores | 1,5 | ⬜ |
| F4.5 Suspensión de pantalla, brillo, bloqueo por inactividad, consumo en reposo | 1,5 | ⬜ |
| F4.6 Almacenamiento LittleFS en `storage` (> 16 MB): prueba de escritura y montaje (`FLASH-32MB.md` §6) | 1 | ⬜ |

### F5 · Conectividad y Flex Phone — 0 / 15
| Tarea | Peso | Estado |
|---|---|---|
| F5.1 Wi-Fi STA: conexión, credenciales, reconexión, sin bloquear la interfaz | 3 | ⬜ |
| F5.2 `flex_link`: cliente compatible con Flex Link v2 (descubrimiento, emparejamiento, sesión, reconexión) reutilizando los módulos puros | 4 | ⬜ |
| F5.3 Notificaciones desde Flex Phone (recibir, listar, descartar) | 3 | ⬜ |
| F5.4 Otras funciones del protocolo (batería del móvil, multimedia, buscar teléfono...) | 2 | ❓ D10 |
| F5.5 Segunda topología: punto de acceso del reloj o BLE | 1 | ❓ D5, D6 |
| F5.6 Seguridad del enlace (v3 con ECDH + cifrado autenticado; requiere cambios en Flex Phone, en su repositorio) | 2 | ❓ D7, D8 |

### F6 · Navegador (renderizado remoto) — 0 / 8
| Tarea | Peso | Estado |
|---|---|---|
| F6.1 Cliente FBP/1: WebSocket + JPEG (`esp_jpeg`) | 4 | ⬜ |
| F6.2 Interfaz del navegador para 410×502 (barra, teclado, pestañas) | 3 | ⬜ |
| F6.3 TLS con validación de certificado | 1 | ⬜ |

Limitación conocida: sin servidor de renderizado o sin el móvil, no hay web. Nunca será un motor HTML local.

### F7 · Actualizaciones OTA — 0 / 7
| Tarea | Peso | Estado |
|---|---|---|
| F7.1 OTA HTTPS (`esp_https_ota`) entre `ota_0` y `ota_1` + reversión automática | 3 | ⬜ |
| F7.2 Manifiesto propio (`flex_os_watch_ultra`) + firma de la imagen | 3 | ⬜ |
| F7.3 Interfaz de actualización | 1 | ⬜ |

### F8 · Cuenta, nube, seguridad y cierre — 0 / 10
| Tarea | Peso | Estado |
|---|---|---|
| F8.1 Flex Account, solo si se adapta con seguridad | 2 | ❓ D11 |
| F8.2 Flex Cloud, solo si es útil | 1 | ❓ D11 |
| F8.3 Protección de credenciales: cifrado de NVS y flash | 2 | ❓ D12 |
| F8.4 Consumo medido y optimización (pantalla, radio, sondeos) | 2 | ⬜ |
| F8.5 Pruebas finales: 24 h de estabilidad, memoria y regresión completa en el reloj | 3 | ⬜ |

### Acordado sin peso asignado

Si se decide incluirlas, se les dará peso redistribuyendo dentro de F4 u F8, y quedará anotado en el historial.

- Audio: altavoz ES8311 y micrófonos ES7210 (❓ D13).
- Motor de vibración, con su GPIO pendiente de verificar (❓ D13).
- microSD (❓ D13).

### Excluido

- Samsung DeX / Flex DeX y modo PC.
- Cámara.
- Galería y edición de imágenes o vídeo.
- Multimedia de escritorio.
- Servidor web de subida.
- Kiosco.
- Jumper (recrea Geometry Dash).
- Drivers del P4/C6 (BNO085, esp-hosted).
- `FlexOS_Ultra_S3.ino` y `FlexOS_Pro.ino`.

## Decisiones pendientes

| # | Decisión | Bloquea |
|---|---|---|
| D1 | ¿Activar el ADC de VBAT y desactivar el pin TS como hace el código oficial? Se decide con los registros de T14 | B2-2 |
| D2 | Parámetros de carga: no se escriben mientras no se conozca la capacidad de la batería | — |
| D3 | ✅ Resuelta: en "carga completa" se muestra el valor real del gauge | — |
| D4 | Calibrar los umbrales de batería baja y crítica (hoy provisionales: 15 % y 5 %, aislados en `battery_thresholds.h`) | calibración |
| D5 | Transporte con Flex Phone: Wi-Fi, BLE o ambos | F5.5 |
| D6 | Topología prioritaria: misma red, hotspot del móvil o punto de acceso del reloj | F5.5 |
| D7 | Protocolo: Flex Link v2 (en claro) o v3 cifrado | F5.6 |
| D8 | Cambios en Flex Phone (Android): varios dispositivos y tipo "reloj" | F5.6 |
| D9 | ¿Comunicación directa reloj ↔ Flex OS Ultra (P4)? | F5 |
| D10 | Funciones de Flex Phone en el reloj más allá de las notificaciones | F5.4 |
| D11 | Flex Account / Flex Cloud en el reloj (modelo, servidor) | F8.1, F8.2 |
| D12 | Cifrado de flash y NVS desde el principio | F8.3 |
| D13 | Uso de audio, motor y microSD | sin peso |
| D14 | Nombre "Liquid Glass" (denominación de terceros) en el producto | F3 |
| D15 | Alcance de "funciones temporales" (cronómetro, temporizador...) | F4.3 |
| D16 | ¿Bloqueo con código? (no acordado) | — |

## Verificaciones pendientes en el reloj

- **B1:** T1-T13 (`docs/B1-ARRANQUE.md` §5). T1 (arranque) ya está confirmado.
- **B2:** T14-T22 (`docs/B2-ENERGIA.md`).
- **B3:** T23 en adelante (`docs/B3-INTERFAZ.md`).
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
| 2026-10-15 | **21 %** | 0 | B3-1 (iconos y componentes del tema) amplía F3.1, que ya contaba al 50 % |
