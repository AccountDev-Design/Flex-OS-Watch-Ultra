# Flex OS Ultra Watch — proyecto ESP-IDF del reloj

Firmware para el **Waveshare ESP32-S3-Touch-AMOLED-2.06** con ESP-IDF **5.5.5** y LVGL **9.5.0**.
Esta carpeta es el proyecto completo: se compila y se flashea **desde aquí**.
La documentación, las pruebas de PC y las herramientas de desarrollo están fuera, en la raíz del repositorio.

## Compilar y flashear

Abre la terminal de ESP-IDF 5.5.5 (o "ESP-IDF Terminal" en VS Code) y ejecuta:

```bash
cd "Flex OS Ultra Watch"          # comillas obligatorias: el nombre tiene espacios
idf.py set-target esp32s3          # solo la primera vez (crea sdkconfig desde sdkconfig.defaults)
idf.py build
idf.py -p COM5 flash monitor       # Windows: COMx · Linux: /dev/ttyACM0 · macOS: /dev/cu.usbmodem*
```

En VS Code, abre **esta carpeta** como espacio de trabajo (no la raíz del repositorio). Después usa Set Target `esp32s3`, Build, Flash y Monitor.

### Normas

- **Nunca** ejecutes `idf.py erase-flash`.
- No cambies los offsets de flasheo: `idf.py flash` los toma de `build/flash_args`.
- Si no entra en modo de descarga, mantén **BOOT** (GPIO0) al conectar o reiniciar.
- Sube al repositorio el `dependencies.lock` que genera la primera compilación: fija las versiones exactas de las dependencias.

> **Aviso sobre los espacios en la ruta.** La documentación oficial de ESP-IDF 5.5.5 dice que el sistema de compilación no admite espacios en la ruta del proyecto (`docs/en/get-started/windows-start-project.rst:16`). Este proyecto se ha compilado y flasheado sin errores en una ruta con espacios (Linux), pero **no está comprobado en Windows ni con la extensión de VS Code**.
>
> Si aparece un error de rutas, renombra la carpeta sin espacios (por ejemplo `git mv "Flex OS Ultra Watch" Flex-OS-Ultra-Watch`) y avísame. El proyecto no depende del nombre de la carpeta.

## Qué produce la compilación (`build/`)

| Archivo | Qué es | Offset |
|---|---|---|
| `bootloader/bootloader.bin` | bootloader de segunda etapa | 0x0 |
| `partition_table/partition-table.bin` | tabla de particiones de 32 MB (`partitions.csv`) | 0x8000 |
| `ota_data_initial.bin` | `otadata` inicial: arranca en `ota_0` | 0xE000 |
| `flex_os_watch_ultra.bin` | **la aplicación** (el firmware de Flex OS Ultra Watch) | 0x10000 (`ota_0`) |
| `flash_args` | lista exacta de archivos y offsets que usa `idf.py flash` | — |
| `flex_os_watch_ultra.elf` / `.map` | símbolos para depuración (no se graba) | — |

**No confundir:**
- **`flex_os_watch_ultra.bin`** es **solo la app**. Grabarlo en 0x0 dejaría el reloj sin arrancar.
- **Imagen combinada:** `idf.py merge-bin` genera un único archivo para 0x0 con las cuatro piezas. Este proyecto no lo necesita.
- **Firmware de fábrica de Waveshare** (`ESP32-S3-Touch-AMOLED-2.06-xiaozhi-251104.bin`, 28 MB, repositorio oficial): es otro firmware completo con su propia tabla de particiones. **No forma parte de este proyecto** y no debe mezclarse con estas imágenes.

En esta carpeta no hay ningún `.bin` versionado: todas las imágenes salen de `build/` en cada compilación.

## Contenido

```
CMakeLists.txt          proyecto (nombre: flex_os_watch_ultra)
version.txt             versión de la app (0.1.0-b1)
sdkconfig.defaults      configuración: ESP32-S3R8, PSRAM octal, flash de 32 MB, LVGL 9.5
partitions.csv          particiones de 32 MB (ver ../docs/FLASH-32MB.md)
main/                   app_main.c + idf_component.yml (BSP oficial 2.0.0, LVGL 9.5.0)
components/             watch_display, watch_touch, watch_power, watch_theme, watch_diag, watch_ui
assets/fonts/           Outfit-Regular.ttf + OFL.txt (origen de las fuentes LVGL)
tools/gen_fonts.sh      regenera components/watch_theme/fonts/ a partir de assets/fonts/
THIRD_PARTY_NOTICES.md  licencias
```

Se generan aquí, sin versionar: `build/`, `managed_components/`, `sdkconfig` y `sdkconfig.old`.
