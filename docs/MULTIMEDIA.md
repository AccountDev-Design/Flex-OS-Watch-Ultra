# Multimedia, almacenamiento y transferencia en Flex OS Watch Ultra

Documento en construcción: cada sección dice qué está hecho y cómo se ha probado. Un apartado sin la marca
**probado en el reloj** solo se ha probado en el PC (pruebas de host o vista previa de LVGL).

## 14. Procedencia de la pila de medios (`components/flexmedia`)

La biblioteca de medios, el almacén, las miniaturas, el demultiplexor AVI/MJPEG, el reproductor WAV/IMA, el
codificador JPEG, el codificador QR, el analizador HTTP y el Flex Web Server **no se han reescrito**: son los de
Flex OS Ultra (ESP32-P4), copiados del repositorio de referencia **sin modificarlo**:

- repositorio `AccountDev-Design/Flex-OS-Ecosistema`;
- rama `claude/flexos-ota-system-blq9g9-2csacx` @ `5dbc949`;
- carpeta `FlexOS_Ultra/` (código) y `tests/host/` (pruebas).

Así el reloj habla exactamente el mismo protocolo que el P4 (código de 6 cifras, cookie `fxs`, `X-Flex: 1`,
comprobación de `Host`, limitador de intentos, subida por trozos con CRC-32) y la página web que abre el móvil es la
misma. No se ha creado un segundo mecanismo de transferencia.

Cada archivo portado empieza con una cabecera `PORT a Flex OS Watch Ultra de …` y sus cambios propios llevan la marca
`RELOJ:`, de modo que `grep -n "RELOJ:"` enseña todas las diferencias con el original.

### 14.1 Huellas SHA-256 de los originales

Código (`FlexOS_Ultra/`):

| Archivo | SHA-256 del original |
|---|---|
| `FlexOS_HttpShare.cpp` | `48c7c31ed069fa23187eae7bc68fce519786ffe737f81c5feb67e53f5bf3332d` |
| `FlexOS_JPEGEnc.cpp` | `d729bcdaaeb3d14c47c20b046ddcd7356701ff7939d3d0223d1ddb37b297d873` |
| `FlexOS_Media.cpp` | `1bd4a14992e46627fb612c91b2da803d980034fbd459364a9a0b624135cbb733` |
| `FlexOS_MediaLib.cpp` | `afd20baab062e17212a6e4249416ac61f5e1981ffe1812c86d75a6a97eec2609` |
| `FlexOS_MediaStore.cpp` | `9fad4bc4239d5e641c555b9f4c1d9a629a1f57e58f4f4f645ef4b27da4d712fb` |
| `FlexOS_MediaThumb.cpp` | `a5470ce3933346d41a4caf9d46248b00c047453abd1c7c51f32527b73ce0776f` |
| `FlexOS_MediaWeb.cpp` | `3defeb76b968af1883ea6b111030614e5f9d0514de40bb6bcb113dd8a050bef9` |
| `FlexOS_QR.cpp` | `da7a6bcc012e4f7b82e601cdb0583462a7ea2fc773425f6437781546083bc75e` |
| `FlexOS_HttpShare.h` | `f73061aaf71d692c34b887564bd14c3d6587d4247e3b7f86475663978424f357` |
| `FlexOS_JPEGEnc.h` | `f0e695bd57af466d79c5c9a2e7ea2f3410a8d045d653c1065f4d38105d8119bd` |
| `FlexOS_Media.h` | `d0816ab89903ecad4d895a4f3a40642edaea936cfdc9d9230b126df63ecabd5c` |
| `FlexOS_MediaLib.h` | `bd28d2a6abf7bb50d2f259fda3a7daa170d1c71ffbf6f397c86abfd18a594205` |
| `FlexOS_MediaStore.h` | `e3df9904e0a4ffb7e5d3c25cebbe57a9b24cb849a0333a86e59bf153825e85bb` |
| `FlexOS_MediaThumb.h` | `0d0da6018fe9ea56b9f13440fd69e666bcc02c131664c7460928cf96f8349060` |
| `FlexOS_MediaWeb.h` | `ca3b09cfc219e53339d4c6eac99d94e338dc8e9d2e70516af12c688734845190` |
| `FlexOS_QR.h` | `89804271598d9bfc2259ace5f5302d70da3a9d8c4043d9314df999141994c47b` |
| `FlexOS_StorageCore.h` | `cb50cfb6fe000f7b9024e2878698fbfd3dacd725da10cd4da23f452c698df5a3` |
| `webui/app.css` | `efe25ed3be2b2f7c6f3dff4179e6a28f20c8142dac60055cc7508c653b8e8783` |
| `webui/app.js` | `97011eeddb70e85464bd7a898f7de6cdce8ea86ddc23aae15d2c390813505429` |
| `webui/gen_header.py` | `8d35249e705414c590d4cb93763e2e12260e6cf57f2479f0442ae54b1ee1fa6c` |
| `webui/index.html` | `ae1da06622f0db26b70f2d05b6b5a9f3494071b7b6cabbc8efbc9be27f24d00a` |

Pruebas (`tests/host/`, copiadas a `tests/host/flexmedia/`):

| Archivo | SHA-256 del original |
|---|---|
| `test_qr.cpp` | `239554796572ddde8eadad26793f6484c0ad0c57b6486ae4386213a2f68bf869` |
| `test_httpshare.cpp` | `ab2c5b37a3a22276f9f070597aa11ca81d4ee341a867c81c0c6f7ca84a0f36a0` |
| `test_media.cpp` | `230546a0fc933e589020c8b5dd94b1ed01e7258bfa020ace7c96288e39ba43cb` |
| `test_medialib.cpp` | `dcd1a03e1c92843e4c35df845bed8db4a3edb3cb883d5b2573d8a96c7271aa4a` |
| `test_jpegenc.cpp` | `40a19824d7058a4e5273f95a520b29044b4c729752f14fe3e8894ca4400aa4da` |
| `test_mediathumb.cpp` | `c14b8f7e2bd4772b760660699eea83a4dc57b87e0f76b7d4267120353069b07a` |
| `test_mediastore.cpp` | `e651b597fadf488a3e77e55f265dedc477d9f821fe77ce4de888242290a3d3fa` |
| `test_mediaweb.cpp` | `f2bceda992d5d59ae5f010febe1b86d67d652b9d59fceb824a0a2acc86520f6f` |

Las pruebas solo cambian en las rutas de `#include` y de las muestras (`fixtures/fm/`) y en cuatro expectativas,
marcadas `RELOJ:`, donde el reloj hace otra cosa a propósito (§14.2).

### 14.2 Qué cambia en el reloj y por qué

- **Decodificador JPEG.** El del P4 (`FlexOS_JPEG.cpp`) **no se porta**: su IDCT parece derivada de la del IJG sin
  atribución (`docs/AUDITORIA-FASE0.md` §4). En su lugar, `flex_jpeg_tjpgd.c` implementa el mismo contrato
  (`FlexOS_JPEG.h`, mismos nombres y códigos de error) sobre TJpgDec:
  - en el reloj, el de la **ROM** del ESP32-S3 (sin código ni Flash añadidos; escala 1/2, 1/4 y 1/8);
  - en el PC, el de LVGL.

  Dos diferencias con el decodificador del P4:
  - TJpgDec no admite el submuestreo 4:4:0: esa foto se rechaza limpia con `FLEXJPG_ERR_UNSUPPORTED` y la galería la
    marca como no compatible;
  - los JPEG MJPEG sin tabla Huffman (`DHT`), habituales en cámaras, reciben la tabla estándar (ITU-T T.81 anexo K)
    antes del `SOS`, como hacen los decodificadores de escritorio.
- **MP3.** El P4 no reproduce MP3 y el reloj sí (§5). Por eso:
  - `FML_F_MP3` es reproducible;
  - el servidor comprueba cada MP3 subido con `flex_mp3.c` (tramas MPEG reales, tres seguidas que concuerden) y
    rechaza con `422` uno sin audio, en vez de guardarlo como «original no reproducible».
- **Identidad del dispositivo.** `/api/hello` añade el nombre, el identificador público y el tipo del dispositivo,
  los destinos de almacenamiento y la caducidad del código. Toda petición que cambia algo puede llevar
  `X-Flex-Dev`; si no es la del reloj, se contesta `409` (`wrongDevice`) y no se escribe nada: una pestaña abierta
  para otro dispositivo Flex no puede subir aquí por error (§9).
- **Caducidad del código.** El código de 6 cifras caduca (`410`, `expired`); en el P4 vive mientras la hoja está
  abierta.
- **Carpetas.** El almacén conoce además `/Grabaciones` (grabadora) y `/DCIM` (fotos de cámaras en la microSD).
- **Límites.** `flexMlSetLimits()` deja al reloj fijar el tamaño máximo por tipo según el destino (§2).

### 14.3 Pruebas

```
make -C tests/host flexmedia LVGL_DIR=… IDF_PATH=…
```

Compila cada prueba con ASan y UBSan, igual que en la referencia. Resultado de la última ejecución (PC):

| Prueba | Comprobaciones | Fallos |
|---|---:|---:|
| `test_mp3` (nueva, del reloj) | 35 | 0 |
| `test_qr` | 171 | 0 |
| `test_httpshare` | 154 | 0 |
| `test_media` | 2456 | 0 |
| `test_medialib` | 19965 | 0 |
| `test_jpegenc` | 137 | 0 |
| `test_mediathumb` | 39 | 0 |
| `test_mediastore` | 169 | 0 |
| `test_mediaweb` | 186 | 0 |

El componente también compila para el ESP32-S3 con ESP-IDF 5.5.5, sin avisos. **Ninguna de estas piezas está todavía
conectada a la interfaz del reloj ni se ha probado en el reloj.**
