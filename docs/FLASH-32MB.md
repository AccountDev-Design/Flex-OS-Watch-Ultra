# Flash de 32 MB — capacidad, acceso y tabla de particiones

Estado: **diseñado y compilado. El acceso por encima de 16 MB NO está verificado
físicamente.** Hasta completar las comprobaciones de la sección 6 no se considera
aprovechado.

## 1. Capacidad física

| Fuente | Dato | Tipo de evidencia |
|---|---|---|
| Esquema oficial V1.0, U3 | GD25Q256EYIGR = 256 Mbit = **32 MB**, QSPI | documental |
| Firmware de fábrica de Waveshare `FirmWare/ESP32-S3-Touch-AMOLED-2.06-xiaozhi-251104.bin` (repositorio oficial @ b099739) | Imagen completa de 28 MB grabada desde 0x0. La cabecera declara **32 MB** y la tabla tiene particiones por encima de 16 MB: `ota_1` en 0x1000000 y `storage` (6 MB) en 0x1600000 | documental: el fabricante usa la zona alta en esta placa |
| Ejemplos ESP-IDF oficiales | Declaran solo 16 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`) | configuración conservadora, no un límite físico |
| **Tu reloj** | Identificador JEDEC y tamaño detectados por `watch_diag` | **PENDIENTE** (prueba T10) |

El identificador esperado es `0xC84019`: fabricante GigaDevice (0xC8), tipo 0x40 y capacidad 0x19, que equivale a 2^25 B = 32 MB.

## 2. Qué permite ESP-IDF 5.5.5 (comprobado en su código)

- **Documentación:** `docs/en/api-reference/peripherals/spi_flash/spi_flash_optional_feature.rst:143-179`.
  - El GD25Q256 figura en la lista de chips con direccionamiento de 32 bits.
  - En el ESP32-S3, la zona por encima de 16 MB sirve por defecto **para datos** (por ejemplo, un sistema de archivos).
  - Ejecutar código o mapear por caché por encima de 16 MB requiere `CONFIG_IDF_EXPERIMENTAL_FEATURES` + `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH`, que es **experimental**.
- **Driver GigaDevice** (`components/spi_flash/spi_flash_chip_gd.c:30-36`): cuando el byte de capacidad es ≥ 0x19, activa `SPI_FLASH_CHIP_CAP_32MB_SUPPORT`. Las lecturas y escrituras por encima de 16 MB usan entonces comandos de 4 bytes en cada operación, sin cambiar el modo global del chip.
- **Comprobación de tamaño** (`components/spi_flash/esp_flash_api.c:363`): sin esa capacidad, ESP-IDF recorta el tamaño a 16 MB y avisa con un *warning*.
- **Bootloader** (`components/bootloader_support/bootloader_flash/src/bootloader_flash.c:656`): solo pasa la caché a 32 bits con la opción experimental. Sin ella, la caché sigue con 24 bits y el código de los primeros 16 MB funciona igual que con una flash de 16 MB.

**Decisión:** se declaran 32 MB y **no** se activa la opción experimental.

## 3. Capacidad accesible en este diseño

| Zona | Acceso | Uso |
|---|---|---|
| 0x0000000 – 0x0FFFFFF (16 MB) | caché (ejecución y mmap) + `esp_partition` | bootloader, apps OTA, volcado de memoria, recursos mapeables |
| 0x1000000 – 0x1FFFFFF (16 MB) | **solo** `esp_partition_read/write/erase` (SPI1, comandos de 4 bytes) | datos de usuario (LittleFS), nunca código |

## 4. Tabla de particiones (`Flex OS Ultra Watch/partitions.csv`)

| Nombre | Desplazamiento | Tamaño | Justificación |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | Ajustes clave-valor: Wi-Fi, vínculo con Flex Phone, preferencias. Son 5 páginas, y NVS exige un mínimo de 3. Es el mismo tamaño que usa Flex OS Ultra (`flexos_ultra_usb.csv:38`). Los datos grandes van a `storage` |
| otadata | 0xE000 | 8 KB | Obligatoria para OTA (2 sectores) |
| ota_0 | 0x10000 | 6 MB | Imagen de B1: 0,82 MB; firmware completo del P4: 1,3 MB; estimación del reloj completo (Wi-Fi, BLE, TLS, navegador remoto, fuentes): 2,5–4 MB. 6 MB da al menos 1,5 veces de margen sobre la estimación alta y deja las dos ranuras por debajo de 16 MB. Es el mismo tamaño de ranura OTA que el firmware de fábrica |
| ota_1 | 0x610000 | 6 MB | Segunda ranura OTA. Debe estar por debajo de 16 MB para poder arrancar sin la opción experimental (el firmware de fábrica la pone en 0x1000000) |
| coredump | 0xC10000 | 64 KB | Volcado ELF tras un fallo. Se confirmará el tamaño real con el primer volcado |
| assets | 0xC20000 | 3,875 MB | Recursos de solo lectura que LVGL puede leer por mmap (fondos, fuentes extra), por eso por debajo de 16 MB. Hasta 0xFFFFFF. **Sin uso en B1** |
| storage | 0x1000000 | 16 MB | LittleFS para datos (historial de notificaciones, ficheros de ajustes, caché del navegador, registros). Solo se accede con `esp_partition`. **No se monta en B1** |

**Validación en compilación:**
- `gen_esp32part.py` acepta la tabla con 32 MB.
- Con 16 MB la **rechaza** ("does not fit in configured flash size 16MB"). Así no puede mezclarse por error esta tabla con una configuración de 16 MB.

## 5. Compatibilidad y datos existentes

- **Lo que escribe el flasheo:** `idf.py flash` graba solo el bootloader (0x0), la tabla (0x8000), `otadata` (0xE000) y la app (0x10000). esptool borra únicamente los sectores que escribe.
- **Sin `erase-flash`:** no se usa ni se recomienda.
- **Datos de fábrica:** si tu reloj tuvo el firmware de fábrica, la zona de 0x1000000 en adelante conserva su `ota_1` y su `storage`. **B1 no lee esa zona más que en 3 ventanas de 4 KB, y no la escribe.**
  - Cuando una fase posterior formatee `storage`, esos datos de fábrica se perderán.
  - Se pueden recuperar volviendo a grabar la imagen de fábrica de 28 MB del repositorio oficial. Eso sustituye todo el firmware.
  - Ese formateo será una acción explícita y se avisará antes.
- **Arranque y OTA:**
  - El bootloader y las apps viven en la zona de 24 bits.
  - El mecanismo OTA estándar (`otadata` + `ota_0`/`ota_1`) no cambia.
  - Una imagen OTA solo puede escribirse en la ranura libre por debajo de 16 MB.

## 6. Comprobaciones pendientes antes de declarar los 32 MB aprovechados

1. **T10 (B1, solo lectura):** el registro debe mostrar `JEDEC 0xC84019, fisica 32768 KB, declarada 32768 KB`.
2. **T10 (B1, solo lectura):** el veredicto de la sonda debe ser `lectura >16 MB distinta (sin aliasing)`.
   - La sonda compara 4 KB del bootloader, de la tabla y de la cabecera de la app con las mismas posiciones 16 MB más arriba.
   - Si todo coincide, las direcciones "dan la vuelta" y **no se debe escribir** por encima de 16 MB.
3. **Prueba de escritura** (fase de almacenamiento, no en B1): borrar, escribir y releer un sector de `storage` con patrón propio, y comprobar que la zona baja no cambió (la CRC de la app sigue intacta). Solo si 1 y 2 pasan.
4. **LittleFS montado** en `storage`, con reinicios y cortes de alimentación durante la escritura.

Hasta entonces la capacidad comprobada en hardware es **ninguna** (pendiente de T10). La capacidad diseñada es 16 MB ejecutables/mapeables + 16 MB de datos.
