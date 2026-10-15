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

- B1 "Arranque verificable": compilado, flasheado y con una prueba inicial correcta en el reloj (según el usuario).
- Faltan los registros de las pruebas T1-T13.
- Progreso y criterio de avance: [docs/PROGRESO.md](docs/PROGRESO.md)

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

## Documentación

- [docs/B1-ARRANQUE.md](docs/B1-ARRANQUE.md): contenido de B1, dependencias, pruebas físicas
- [docs/FLASH-32MB.md](docs/FLASH-32MB.md): capacidad, acceso y tabla de particiones de 32 MB
- [docs/PROGRESO.md](docs/PROGRESO.md): progreso
- [docs/AUDITORIA-FASE0.md](docs/AUDITORIA-FASE0.md): auditoría inicial
