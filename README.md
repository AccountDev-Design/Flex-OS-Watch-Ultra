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

**Progreso global: 28,5 %**, del que un 7 % está verificado en el reloj.

- Referencia única: **[docs/ROADMAP.md](docs/ROADMAP.md)**. Ahí están el plan completo, los pesos, las decisiones pendientes y el historial.
- F2 (batería y energía) y F3 (interfaz, tema y navegación) están hechas en software: compilan y pasan las pruebas de PC y de la vista previa.
- Esperan las pruebas físicas en el reloj: [docs/B2-ENERGIA.md](docs/B2-ENERGIA.md) y [docs/B3-INTERFAZ.md](docs/B3-INTERFAZ.md).

## Estructura del repositorio

```
Flex OS Ultra Watch/   PROYECTO ESP-IDF DEL RELOJ (lo único que se compila y se flashea)
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
```

Los datos de la vista previa (batería, hora, memoria) son **escenarios
simulados**: sirven para probar la interfaz, no el hardware.

## Documentación

- [docs/ROADMAP.md](docs/ROADMAP.md): hoja de ruta y progreso global
- [docs/PRUEBAS-RELOJ.md](docs/PRUEBAS-RELOJ.md): **próxima prueba en el reloj**, todas las pruebas pendientes en orden
- [docs/B3-INTERFAZ.md](docs/B3-INTERFAZ.md): interfaz, tema, hora, navegación, panel rápido y Ajustes (B3)
- [docs/B2-ENERGIA.md](docs/B2-ENERGIA.md): batería y energía (B2)
- [docs/B1-ARRANQUE.md](docs/B1-ARRANQUE.md): contenido de B1, dependencias, pruebas físicas
- [docs/FLASH-32MB.md](docs/FLASH-32MB.md): capacidad, acceso y tabla de particiones de 32 MB
- [docs/AUDITORIA-FASE0.md](docs/AUDITORIA-FASE0.md): auditoría inicial
