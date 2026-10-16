# Próxima prueba en el reloj (F1-F4)

Esta guía ordena todas las pruebas físicas pendientes (T1-T66) en **una sola sesión**. El detalle de cada prueba está en su documento:

| Pruebas | Documento |
|---|---|
| T1-T13 | [B1-ARRANQUE.md](B1-ARRANQUE.md) |
| T14-T22 | [B2-ENERGIA.md](B2-ENERGIA.md) |
| T23-T40 | [B3-INTERFAZ.md](B3-INTERFAZ.md) |
| T41-T45 | [B4-CALIDAD.md](B4-CALIDAD.md) y [B2-ENERGIA.md](B2-ENERGIA.md) |
| T46-T66 | [F4-SERVICIOS.md](F4-SERVICIOS.md) |
| T67-T73 | [F3-IDENTIDAD-ORIGINAL.md](F3-IDENTIDAD-ORIGINAL.md) (iconos, lanzador, Reloj y fondos) |
| T74-T77 | [F9-APPS.md](F9-APPS.md) (Calculadora, Calendario, Flex Compass, Device Care) |
| T78-T83 | [F5-CONECTIVIDAD.md](F5-CONECTIVIDAD.md) (Wi-Fi) |
| Flash de 32 MB | [FLASH-32MB.md](FLASH-32MB.md) |

**Verificado en el reloj hasta ahora** (fotos y vídeo del 2026-10-15): pantalla, táctil, LVGL y la sonda de 32 MB (T10). Lo demás no. Compilar y pasar las pruebas de PC no prueba el reloj: cada resultado que me envíes es lo que convierte una tarea del 50 % al 100 %.

## 0. Preparación

1. Actualiza la rama `ccr-b6cde864-7hvm3w` (`git pull`).
2. Abre la terminal de ESP-IDF 5.5.5 y compila y flashea **desde la carpeta del proyecto**:

   ```bash
   cd "Flex OS Ultra Watch"
   idf.py build
   idf.py -p COM5 flash monitor      # tu puerto
   ```

   - **No** ejecutes `erase-flash` ni `update-dependencies`.
   - **Dependencia nueva del registro (F4.6):** `joltwallet/littlefs ==1.22.3`. La primera compilación la descarga y actualiza tu `dependencies.lock`: súbelo.
   - Los componentes nuevos (`watch_settings`, `watch_imu`, `watch_storage`) no necesitan nada más del registro.
3. Guarda **todo** el registro serie de la sesión en un archivo de texto. Es lo más útil que me puedes enviar.

## 1. Arranque (con el monitor abierto y USB conectado)

| Paso | Pruebas | Qué mirar |
|---|---|---|
| 1 | T1, T30 | Sin destello blanco. "Flex OS" aparece sobre negro y la esfera entra sola; sin reinicios en el registro |
| 2 | T26, T10, T11, T14 | Copia del registro: las líneas `watch_time: RTC …` (o `sin hora fiable`) y `registros 00h-0Ah`; las líneas `watch_diag` (flash y memoria) y las 3 de configuración del AXP2101 |
| 3 | T28 | La hora y la fecha de la esfera coinciden con la línea del RTC (o "--:--" y "Hora sin configurar"); el día de la semana es correcto |
| 4 | T9 | FPS y % de CPU del medidor de LVGL (abajo), en reposo y moviendo el dedo |

## 2. Interfaz (USB conectado)

| Paso | Pruebas | Qué hacer |
|---|---|---|
| 5 | T33 | Arrastra hacia abajo desde el borde superior de la esfera: despacio, rápido y un arrastre corto |
| 6 | T34, T35 | En el panel: el brillo de un extremo a otro; toca Wi-Fi y Bluetooth |
| 7 | T36, T31 | Cierra el panel (hacia arriba y con el asa). Ábrelo y toca Ajustes |
| 8 | T37, T38 | Ajustes > Batería: desplaza hasta el final y compara con el registro de `watch_power` |
| 9 | T39 | Ajustes > Diagnóstico |
| 10 | T40 (= T2 y T3) | Diagnóstico > Prueba de pantalla: marco azul en los 4 bordes; toca las 4 esquinas y el centro y anota las coordenadas |
| 11 | T32 | En cualquier página de Ajustes: el gesto desde el borde izquierdo, corto y lento, largo y rápido; desplaza una lista en vertical |
| 12 | T23-T25, T13 | Legibilidad de iconos y textos, respuesta al toque, filas "Pendiente" |

## 3. Batería (USB y sin USB)

| Paso | Pruebas | Qué hacer |
|---|---|---|
| 13 | T15, T16, T20, T5 | Desconecta y conecta el USB tres veces (con la batería no llena): una animación de carga por conexión, ninguna al arrancar ya conectado |
| 14 | T4, T8, T19 | Sin USB: foto de la esfera y de Ajustes > Batería (el monitor se corta sin USB) |
| 15 | T27, T29 | Tras 15 min, compara la hora con un reloj de referencia; mira un cambio de minuto |
| 16 | T12, T18 | Déjalo 30 min encendido; luego copia las líneas `lectura I2C … pila libre minima` |

## 4. Pruebas largas (cuando puedas)

| Pruebas | Qué hacer |
|---|---|
| T6, T7, T17 | Carga completa: no debe alternar entre "Cargando" y "Externa sin carga" |
| T21 (y T22 opcional) | Aviso de batería baja usando el reloj sin USB hasta el 15 %. **No** es necesario agotar la batería |

## 5. Correcciones B4 (T41-T45, `B4-CALIDAD.md` y `B2-ENERGIA.md`)

| Pruebas | Qué hacer |
|---|---|
| T41 | Con la esfera visible y sin tocarla, conecta y desconecta el USB: el estado y la animación deben cambiar solos en ~1 s |
| T42 | Con el medidor de LVGL: desplaza Ajustes > Batería y baja/sube el panel rápido despacio; anota FPS y ms |
| T43 | Animación de carga sin destello ni tirón; fondos en negro puro |
| T44 | Prueba de pantalla: primer arco de esquina que se ve entero (104/112/120/128/136) |
| T45 | Nada cortado en las esquinas: panel rápido, páginas de Ajustes, aviso |

## 6. Servicios F4 (`F4-SERVICIOS.md`)

| Pruebas | Qué hacer |
|---|---|
| T46 | Cambia brillo y fondo, espera 3 s y reinicia: deben conservarse |
| T47 | Diagnóstico > Ajustes guardados: "leídos", escrituras y 0 errores |
| T48 | Ajustes > Fecha y hora > Poner en hora: "Hora guardada y comprobada" (con esta versión el reloj pide hora una vez) |
| T49 | Tras RESET y tras apagar y encender, la hora se conserva |
| T50 | Diferencia con el móvil a los 15 min y a las 24 h |
| T51 | Cambiar zona y formato 12/24 h: la hora se ve bien y el RTC no cambia |
| T52-T55 | Apps (desliza la esfera a la izquierda): cronómetro, temporizador con aviso, alarma que suena y se conserva tras reiniciar |
| T56-T59 | Apagado de pantalla: atenuada y apagada sin tocar, botón lateral (anota "reposo N" en Diagnóstico; **no** lo mantengas 4 s), despertar con USB y temporizador, imagen correcta tras encender |
| T60-T62 | IMU: línea `QMI8658 ... configurado` en el registro; Aplicaciones > Sensores plano (Z ≈ 1 g, punto verde) y hacia dónde va el punto al inclinar |
| T63-T66 | Ajustes > Almacenamiento: registro al arrancar (no formatea solo), Probar escritura ("Superada"), Formatear con dos toques, Probar archivo antes y después de reiniciar |

## 7. Identidad original: iconos, lanzador y fondos (`F3-IDENTIDAD-ORIGINAL.md`)

| Pruebas | Qué mirar |
|---|---|
| T67-T70 | Deslizar la esfera a la izquierda: iconos nítidos y sin bordes raros; "Almacenamiento" entero; punto en Reloj con el cronómetro en marcha; FPS al desplazar. En el registro, ningún `LoadStoreAlignment` |
| T71-T73 | Ajustes > Pantalla > Fondo: tiempo de generación en el registro, degradados sin escalones, hora legible sobre Aurora y Naturaleza |

## 8. Apps (`F9-APPS.md`)

| Pruebas | Qué mirar |
|---|---|
| T74-T77 | Calculadora (25 + 10, 5 / 0), Calendario (hoy y cambio de mes), Flex Compass (Pitch/Roll al inclinar, sin rumbo), Device Care (puntuación y semáforos) |

## 9. Wi-Fi (`F5-CONECTIVIDAD.md`)

| Pruebas | Qué mirar |
|---|---|
| T78-T83 | Encender y listar redes, conectar con contraseña (y que reconecte tras reiniciar), contraseña mal, hora por Internet, señal perdida y reconexión, mosaico del panel. Copiar las líneas `watch_net` del registro |

## Qué enviarme

- El registro serie completo de la sesión.
- Las fotos de los pasos 3, 10 y 14.
- Las coordenadas de las esquinas (paso 10).
- Las cifras del medidor (paso 4).
- Cualquier cosa que se vea mal, se corte, parpadee o no responda.

Con eso se cierran las decisiones D1 (ADC y pin TS) y D17 (zona horaria del RTC), y las tareas de F1-F3 pasan de "PC" a "verificado".
