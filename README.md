# Flex OS Watch Ultra

Sistema propio para el reloj **Waveshare ESP32-S3-Touch-AMOLED-2.06** (ESP32-S3R8,
AMOLED de 410×502 con CO5300, táctil FT3168, PMIC AXP2101, flash GD25Q256 de 32 MB),
con **ESP-IDF 5.5.5** y **LVGL 9.5.0**, con la identidad visual de Flex OS Ultra.

## El firmware está en `Flex OS Ultra Watch/`

Esa carpeta es el proyecto ESP-IDF completo y autosuficiente. Compilar y flashear siempre **desde dentro**:

```bash
cd "Flex OS Ultra Watch"
idf.py set-target esp32s3     # solo la primera vez
idf.py build
idf.py -p PUERTO flash monitor     # nunca erase-flash
```

Detalles, archivos generados y avisos: [`Flex OS Ultra Watch/README.md`](Flex%20OS%20Ultra%20Watch/README.md).

## Estado

**Progreso global: 50,7 %** (49,3 % pendiente), del que un 7,6 % está verificado en el reloj. El alcance se amplió el 2026-10-16 (iconos y fondos originales, todas las apps del inventario, BLE) y el 2026-10-20 (integración con Flex Phone y experiencia Watch Ultra, F10): ver el historial de [docs/ROADMAP.md](docs/ROADMAP.md).

**Novedades de F10** ([docs/F10-EXPERIENCIA.md](docs/F10-EXPERIENCIA.md)), compiladas y probadas en el PC, pendientes
de probar en el reloj (T124-T141):

- Gestos: hacia abajo en la esfera, el cajón; desde el borde inferior, a la esfera sin cerrar la app; borde derecho,
  atrás; borde izquierdo, al cajón; borde superior, el panel rápido desde cualquier página.
- Cajón en panal con iconos circulares sin nombres.
- Cinco esferas y selector (mantener pulsada la esfera o Ajustes > Pantalla).
- AOD, Pixel Shift, atenuación progresiva y Flex Ultra Power Saving, en el panel rápido y en Device Care.
- Rumbo relativo en Flex Compass.
- Splash de 3 s, sin medidor de FPS y cinco causas de fallo del Wi-Fi corregidas.

- Referencia única: **[docs/ROADMAP.md](docs/ROADMAP.md)**. Ahí están el plan completo, los pesos, las decisiones pendientes y el historial.
- F2 (batería y energía), F3 (interfaz, tema y navegación) y F4 (servicios) están hechas en software: compilan y pasan las pruebas de PC y de la vista previa.
- F4 incluye:
  - ajustes guardados;
  - puesta en hora y zona horaria;
  - cronómetro, temporizador y alarmas;
  - sensores;
  - apagado de pantalla;
  - LittleFS.
- Esperan las pruebas físicas en el reloj: [docs/PRUEBAS-RELOJ.md](docs/PRUEBAS-RELOJ.md) (T1-T66).

## Estructura del repositorio

```
Flex OS Ultra Watch/   PROYECTO ESP-IDF DEL RELOJ (lo único que se compila y se flashea)
FlexPhone/             APP ANDROID Flex Phone: proyecto Gradle independiente (FlexPhone/README.md)
design/devices/        iconos de los dispositivos Flex: un SVG para Android y para el reloj
docs/                  auditoría, guías (B1, flash de 32 MB), progreso, vistas previas (img/)
tests/host/            pruebas de PC de la lógica pura del firmware: make -C tests/host
tools/sim/             vista previa de la interfaz con LVGL en el PC (no es firmware)
tools/offline/         compilación sin acceso al registro de Espressif (entornos con proxy)
```

Las herramientas de fuera usan el código de `Flex OS Ultra Watch/` sin copiarlo.

- **Pruebas de PC y vista previa:** lo enlazan mediante `build/fw`.
- **Compilación sin registro:** compila una copia en una carpeta de trabajo.

Ninguna escribe dentro de la carpeta del firmware.

## Pruebas de PC

```bash
# Lógica pura (ASan + UBSan): batería, avisos, hora, RTC, gestos, textos...
make -C tests/host

# Vista previa de la interfaz con LVGL 9.5 real: renderiza las pantallas a
# PNG, simula el dedo y comprueba navegación, gestos, fugas, glifos y una
# prueba "mono" de 600 acciones aleatorias reproducibles.
make -C tools/sim LVGL_DIR=/ruta/a/lvgl-9.5.0
tools/sim/build/sim tools/sim/out && python3 tools/sim/to_png.py tools/sim/out

# La misma vista previa con ASan + UBSan, y otra semilla para la prueba mono
make -C tools/sim LVGL_DIR=/ruta/a/lvgl-9.5.0 SAN=1
SIM_SEED=7 tools/sim/build-san/sim tools/sim/out
# Solo una batería (tras el arranque): tema, energia, esfera, esferas, nav, panel,
# ajustes, hora, apps, pantalla, wifi, telefono, notas, clima, navegador,
# relay, ota, mono, visual, banco
SIM_ONLY=apps tools/sim/build/sim tools/sim/out
```

Los datos de la vista previa (batería, hora, memoria) son **escenarios
simulados**: sirven para probar la interfaz, no el hardware.

## Documentación

- [docs/ROADMAP.md](docs/ROADMAP.md): hoja de ruta y progreso global
- [docs/F10-EXPERIENCIA.md](docs/F10-EXPERIENCIA.md): gestos, cajón en panal, esferas, AOD, ahorro de energía, rumbo relativo y estabilidad del Wi-Fi
- [docs/FLEX-PHONE-INTEGRACION.md](docs/FLEX-PHONE-INTEGRACION.md): Flex Phone ↔ reloj: dispositivos, BLE v2 y navegador por el teléfono
- [docs/F6-NAVEGADOR.md](docs/F6-NAVEGADOR.md): navegador por renderizado remoto (FBP/1)
- [docs/F7-OTA.md](docs/F7-OTA.md): actualizaciones por Wi-Fi firmadas, con reversión automática
- [docs/MIGRACION.md](docs/MIGRACION.md): mapa de migración desde Flex OS Ultra (componente por componente)
- [docs/INVENTARIO-APPS.md](docs/INVENTARIO-APPS.md): inventario de las 19 apps de Flex OS Ultra y su estado en el reloj
- [docs/F3-IDENTIDAD-ORIGINAL.md](docs/F3-IDENTIDAD-ORIGINAL.md): iconos originales, lanzador, app Reloj y fondos (F3.5, F3.6, F9.1)
- [docs/F9-APPS.md](docs/F9-APPS.md): Calculadora, Calendario, Flex Compass y Device Care (F9.2-F9.5)
- [docs/F5-CONECTIVIDAD.md](docs/F5-CONECTIVIDAD.md): Wi-Fi (F5.1) y lo que sigue de Flex Phone y BLE
- [docs/PRUEBAS-RELOJ.md](docs/PRUEBAS-RELOJ.md): **próxima prueba en el reloj**, todas las pruebas pendientes en orden
- [docs/F4-SERVICIOS.md](docs/F4-SERVICIOS.md): ajustes, hora, apps de tiempo, sensores, apagado de pantalla y almacenamiento (F4)
- [docs/B4-CALIDAD.md](docs/B4-CALIDAD.md): respuesta de carga, rendimiento, negro AMOLED, esquinas e integridad visual (B4)
- [docs/B3-INTERFAZ.md](docs/B3-INTERFAZ.md): interfaz, tema, hora, navegación, panel rápido y Ajustes (B3)
- [docs/B2-ENERGIA.md](docs/B2-ENERGIA.md): batería y energía (B2)
- [docs/B1-ARRANQUE.md](docs/B1-ARRANQUE.md): contenido de B1, dependencias, pruebas físicas
- [docs/FLASH-32MB.md](docs/FLASH-32MB.md): capacidad, acceso y tabla de particiones de 32 MB
- [docs/AUDITORIA-FASE0.md](docs/AUDITORIA-FASE0.md): auditoría inicial
