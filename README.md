# Flex OS Watch Ultra

Sistema propio para el reloj **Waveshare ESP32-S3-Touch-AMOLED-2.06** (ESP32-S3R8,
AMOLED de 410×502 con CO5300, táctil FT3168, PMIC AXP2101, flash GD25Q256 de 32 MB),
con **ESP-IDF 5.5.5** y **LVGL 9.5.0**, con la identidad visual de Flex OS Ultra.

## Estado

Bloque B1 "Arranque verificable": compilado y probado en el PC. **Pendiente de las pruebas físicas en el reloj.**

- Progreso y criterio de avance: [docs/PROGRESO.md](docs/PROGRESO.md)
- B1, compilación, flasheo y pruebas físicas: [docs/B1-ARRANQUE.md](docs/B1-ARRANQUE.md)
- Flash de 32 MB y particiones: [docs/FLASH-32MB.md](docs/FLASH-32MB.md)
- Auditoría inicial: [docs/AUDITORIA-FASE0.md](docs/AUDITORIA-FASE0.md)

## Compilar

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PUERTO flash monitor     # nunca erase-flash
```

Pruebas de PC: `make -C tests/host`. Vista previa de la interfaz: `tools/sim/` (ver su Makefile).

## Estructura

```
main/                 arranque (app_main.c) y dependencias del registro
components/
  watch_display/      CO5300 (BSP oficial) + LVGL 9
  watch_touch/        FT3168 (BSP oficial) -> LVGL
  watch_power/        AXP2101 solo lectura + modelo de batería puro
  watch_theme/        tokens Flex OS Ultra, fondo Onyx, vidrio, fuentes Outfit
  watch_diag/         memoria y sonda de flash de solo lectura
  watch_ui/           pantallas
tests/host/           pruebas de la lógica pura (ASan/UBSan)
tools/                fuentes, vista previa de PC, compilación sin registro
docs/                 auditoría, guías y progreso
```
