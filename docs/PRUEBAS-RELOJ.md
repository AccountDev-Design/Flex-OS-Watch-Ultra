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
| T74-T77, T94-T99 | [F9-APPS.md](F9-APPS.md) (Calculadora, Calendario, Flex Compass, Device Care, Notas, Archivos, Paint) |
| T78-T83 | [F5-CONECTIVIDAD.md](F5-CONECTIVIDAD.md) (Wi-Fi) |
| T84-T93 | [F5-CONECTIVIDAD.md](F5-CONECTIVIDAD.md) §3.6 (Flex Phone por Bluetooth; T86-T93 con la app de `FlexPhone/`, [FLEX-PHONE-INTEGRACION.md](FLEX-PHONE-INTEGRACION.md)) |
| T104-T108 | [F6-NAVEGADOR.md](F6-NAVEGADOR.md) |
| T109-T115 | [F7-OTA.md](F7-OTA.md) (actualizaciones) |
| T116-T123 | [FLEX-PHONE-INTEGRACION.md](FLEX-PHONE-INTEGRACION.md) (dispositivos en Flex Phone y navegador por el teléfono) |
| **T124-T141** | [F10-EXPERIENCIA.md](F10-EXPERIENCIA.md) §9 (Wi-Fi, splash, gestos, cajón, esferas, AOD, ahorro, Device Care, rumbo relativo). **Empieza por T124-T125 (Wi-Fi)** |
| **T142-T150** | [F11-ESTABILIDAD.md](F11-ESTABILIDAD.md) §9 (editor de esferas, Wi-Fi, navegador y memoria, gesto del cajón, redibujado, volcado de fallos). **Empieza por T142 (el fallo del vídeo) y T149 si algo reinicia** |
| **T151-T166** | [GALERIA.md](GALERIA.md) §10 (Galería: fotos del reloj, Flex OS Ultra, Flex Cloud y el teléfono por el P4). **Empieza por T151-T152 (memoria y JPEG grandes)** |
| T185-T204 | [BIENESTAR.md](BIENESTAR.md) (app Bienestar: pasos, entrenamientos, funciones automáticas, gesto, memoria y batería). **Empieza por T185-T186 (arranque y sensor)** |
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
| 4 | T9 | FPS y % de CPU: el medidor ya no sale en la interfaz. Para medir, compila a propósito con `idf.py -DFLEX_PERF_MONITOR=1 build` y `CONFIG_LV_USE_PERF_MONITOR=y` (F10 §7.3) |

## 2. Interfaz (USB conectado)

| Paso | Pruebas | Qué hacer |
|---|---|---|
| 5 | T33 | Arrastra hacia abajo desde el borde superior de la esfera: despacio, rápido y un arrastre corto |
| 6 | T34, T35 | En el panel: el brillo de un extremo a otro; toca Wi-Fi y Bluetooth |
| 7 | T36, T31 | Cierra el panel (hacia arriba y con el asa). Ábrelo y toca Ajustes |
| 8 | T37, T38 | Ajustes > Batería: desplaza hasta el final y compara con el registro de `watch_power` |
| 9 | T39 | Ajustes > Diagnóstico |
| 10 | T40 (= T2 y T3) | Diagnóstico > Prueba de pantalla: marco azul en los 4 bordes; toca las 4 esquinas y el centro y anota las coordenadas |
| 11 | T32 | En cualquier página de Ajustes: el gesto desde el borde **derecho** hacia la izquierda (atrás, F10), corto y lento, largo y rápido; desplaza una lista en vertical |
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
| T52-T55 | Apps (desliza la esfera **hacia abajo**, F10): cronómetro, temporizador con aviso, alarma que suena y se conserva tras reiniciar |
| T56-T59 | Apagado de pantalla: atenuada y apagada sin tocar, botón lateral (anota "reposo N" en Diagnóstico; **no** lo mantengas 4 s), despertar con USB y temporizador, imagen correcta tras encender |
| T60-T62 | IMU: línea `QMI8658 ... configurado` en el registro; Device Care > Pruebas > Sensores (F10: ya no está en el cajón) plano (Z ≈ 1 g, punto verde) y hacia dónde va el punto al inclinar |
| T63-T66 | Ajustes > Almacenamiento: registro al arrancar (no formatea solo), Probar escritura ("Superada"), Formatear con dos toques, Probar archivo antes y después de reiniciar |

## 7. Identidad original: iconos, lanzador y fondos (`F3-IDENTIDAD-ORIGINAL.md`)

| Pruebas | Qué mirar |
|---|---|
| T67-T70 | Deslizar la esfera hacia abajo: iconos circulares nítidos y sin bordes raros (sin nombres desde F10); punto en Reloj con el cronómetro en marcha. En el registro, ningún `LoadStoreAlignment` |
| T71-T73 | Ajustes > Pantalla > Fondo: tiempo de generación en el registro, degradados sin escalones, hora legible sobre Aurora y Naturaleza |

## 8. Apps (`F9-APPS.md`)

| Pruebas | Qué mirar |
|---|---|
| T74-T77 | Calculadora (25 + 10, 5 / 0), Calendario (hoy y cambio de mes), Flex Compass (Pitch/Roll al inclinar, sin rumbo), Device Care (puntuación y semáforos) |
| T94-T99 | Con el almacenamiento montado: Notas (crear, escribir con ñ y tildes, reiniciar y que siga), tirones al autoguardar, papelera/restaurar/renombrar, explorador, Paint (trazos, deshacer, dibujo de Flex OS Ultra) |
| T100-T103 | Clima con Wi-Fi: buscar y añadir Madrid (comparar con open-meteo.com), "Actualizado hace N min" a los 30 min, sin Wi-Fi y tras reiniciar (caché marcada), clima y BLE a la vez. Copiar las líneas `watch_weather` |

## 9. Wi-Fi (`F5-CONECTIVIDAD.md`)

| Pruebas | Qué mirar |
|---|---|
| T78-T83 | Encender y listar redes, conectar con contraseña (y que reconecte tras reiniciar), contraseña mal, hora por Internet, señal perdida y reconexión, mosaico del panel. Copiar las líneas `watch_net` del registro |

## 10. Flex Phone por Bluetooth (`F5-CONECTIVIDAD.md` §3.6)

| Pruebas | Qué mirar |
|---|---|
| T84-T85 | Con Flex Phone instalada desde `FlexPhone/` de este repositorio: el reloj encuentra el teléfono, empareja por BLE y abre el canal (MTU 247). Con una app antigua, explica que el teléfono no contesta al saludo v2. Copiar las líneas `ble_link` y `watch_phone` (incluida la RAM interna del BLE) |
| T86-T93 | Con la app de `FlexPhone/`: emparejar con código, código equivocado, aviso con la pantalla apagada, responder, descartar, reconexión sola, sin Wi-Fi, Música |

## 11. Navegador (`F6-NAVEGADOR.md`)

| Pruebas | Qué mirar |
|---|---|
| T104-T108 | Con el servicio `server/` de Flex OS Ultra: sesión `wss://` con certificado público y credencial, desplazar y tocar (anotar la latencia), escribir en la página, certificado autofirmado rechazado, Wi-Fi que se va y vuelve sin cortar el BLE, abrir y cerrar 10 veces sin perder memoria. Copiar las líneas `watch_browser` |

## 12. Actualizaciones (`F7-OTA.md`)

Grabar **por USB** el firmware de esta versión: lleva el cargador de arranque con la reversión automática.

| Pruebas | Qué mirar |
|---|---|
| T109 | Ajustes > Actualizaciones > «Buscar actualización» con Wi-Fi: hoy «Archivo no encontrado» (el manifiesto no está publicado). Copiar las líneas `watch_ota` |
| T110-T115 | Con un firmware de prueba con clave y un servidor `https://` propio: instalar una versión mayor (tiempo y velocidad), tocar y usar el BLE durante la descarga, cancelar, Wi-Fi que se va, una imagen que falla antes de 60 s (vuelve a la anterior), batería baja y la pila mínima del registro |

## 13. Flex Phone ↔ reloj: dispositivos y navegador por el teléfono (`FLEX-PHONE-INTEGRACION.md`)

Primero hay que construir e instalar la app: `cd FlexPhone && ./gradlew assembleDebug`, en un equipo con Android
Studio. Después, reloj y teléfono en la **misma red Wi-Fi**, y el reloj emparejado (T86).

| Prueba | Qué hacer | Qué mirar |
|---|---|---|
| T116 | Abrir Flex Phone en el teléfono con el reloj y el P4 emparejados | Una entrada por dispositivo, con su icono (reloj y Flex OS Ultra) y sin duplicados, aunque el P4 se vea por Wi-Fi y por su canal |
| T117 | Tocar el reloj en la lista | Su pantalla: Bluetooth, sesión, navegador. Tocar el P4: Wi-Fi, relay, nube. «Olvidar» pide confirmación |
| T118 | En el reloj: Flex Phone > «Web del teléfono» | Icono del teléfono, «Parado» y el botón. Pulsarlo: «Arrancando…» y luego «En marcha» con `ip:puerto` real. La notificación del relay aparece en el teléfono |
| T119 | Abrir el Navegador del reloj (origen Automático) e ir a una web | La sesión va al teléfono («Flex Phone · ip:puerto» en el panel). La página se ve y desplazar y tocar funcionan. Anotar la latencia |
| T120 | Abrir una web que cambie sola (un reloj en línea, un marcador) y no tocar nada durante 2 min | Se actualiza sola en el reloj, sin tirones ni cortes |
| T121 | Parar el relay desde el teléfono | El reloj dice «Servidor del navegador detenido en el teléfono» y no lo vuelve a encender solo. Con «Solo Flex Phone» no se conecta a otro servidor |
| T122 | Apagar el Wi-Fi del teléfono con el relay encendido | El motivo real aparece en el reloj. Al volver, la sesión se rehace sola |
| T123 | 30 min navegando por el teléfono y luego cerrar y abrir el navegador 10 veces | Sin reinicios. La PSRAM libre en `flex://about` (telemetría) vuelve al mismo valor. Copiar las líneas `watch_browser` y `watch_phone` |

## 14. Galería (`GALERIA.md` §10)

Hace falta un Flex OS Ultra en la **misma red Wi-Fi** con su Flex Web Server encendido («Conectar con el móvil»
enseña la dirección y el código). Para T160-T161, una Flex Account en la web de Flex Developer Studio.

| Pruebas | Qué mirar |
|---|---|
| T151-T152 | Abrir la Galería con fotos en `/Imagenes` (y un JPEG de 4000 × 3000): miniaturas y visor sin reinicio. Copiar las líneas `watch_gallery` (memoria al abrir) y anotar cuánto tarda la foto grande |
| T153-T155 | Emparejar con el P4 (primero un código malo), comparar con la Galería del P4, visor con deslizar, doble toque y arrastre |
| T156-T159 | Descarga con corte de red, subida al P4, papeleras y fotos del teléfono por el P4 |
| T160-T162 | Vincular la Flex Account (producción), fotos de Flex Cloud Web, subida del reloj, Automático apagando y encendiendo el P4 |
| T163-T166 | Sin Wi-Fi y vuelta, ir a la esfera durante una descarga, abrir y cerrar 10 veces (PSRAM en Device Care), HTTPS real. Copiar la línea «fin: … pila libre mínima» |

## 15. Rediseño 2026-10 (`REDISENO-2026-10.md`)

Compara cada pantalla con su referencia de `design/referencias/` y `design/faces/` y manda una foto del reloj. Marca
OK, FALLO o NO REALIZADA.

| # | Prueba | Resultado esperado |
|---|---|---|
| T167 | En la esfera, deslizar hacia abajo desde el centro | El cajón en panal baja siguiendo al dedo, con el fondo desenfocado. Deslizar hacia arriba vuelve a la esfera |
| T168 | En el cajón, arrastrar en diagonal y en círculos 30 s; soltar con impulso | Sin tirones ni saltos; la lente sigue al dedo; nunca dos iconos iguales a la vista; al parar, la celda se centra sin rebotes. Anotar si se nota lento |
| T169 | Tocar un icono; tocar entre dos iconos; arrastrar y soltar sobre un icono | Abre solo con el toque; entre iconos no abre nada; un arrastre no abre |
| T170 | Reloj: las cuatro pestañas | Como las referencias. Formato cambia 12/24 h y se mantiene tras reiniciar |
| T171 | Cronómetro 1 min con 3 parciales; temporizador de 00:00:10 | El cronómetro coincide con otro reloj; el temporizador avisa a los 10 s |
| T172 | Crear una alarma para dentro de 2 min y apagar otra con su interruptor | "Próxima" correcta; suena la activa, no la apagada |
| T173 | Ajustes > Almacenamiento | Memoria interna 32 MB, PSRAM y categorías creíbles (comparar con el explorador). "Optimizar" no borra nada (comprobar Notas y Galería después) |
| T174 | Elegir cada una de las 6 esferas nuevas | Fondos nítidos, textos enteros (nada cortado por las esquinas), batería y clima reales; "Tierra" sin clima no muestra nada inventado |
| T175 | Reiniciar con una esfera nueva elegida | Vuelve la misma esfera |
| T176 | Mantener pulsada la esfera sin moverse | La esfera se aleja hasta su tarjeta (zoom). Moviendo el dedo no se abre |
| T177 | En el selector, recorrer las 11 esferas despacio y deprisa | Las tarjetas aparecen con su vista previa sin congelar el desplazamiento. Copiar las líneas `selector:` del registro si las hay |
| T178 | Tocar la tarjeta centrada; tocar una vecina; salir por el borde derecho | Aplica con zoom y vuelve a la esfera; la vecina solo se centra; salir no cambia la esfera |
| T179 | Abrir y cerrar el selector 15 veces, alternando con el cajón | Sin reinicios. PSRAM libre en Device Care igual al principio y al final (anotar los dos valores) |
| T180 | Flex Compass: girar el reloj 90° sobre la mesa y volver | "90° REL" aprox. y vuelta a ~0°; la cuña crece al moverse; mantener pulsada la esfera fija la referencia |
| T181 | Flex Compass: inclinar el reloj | "INCLINACIÓN" y "ALABEO" cambian; los detalles dicen magnetómetro, barómetro y GPS "No presente" |
| T182 | Panel rápido: cada mosaico | Wi-Fi y Bluetooth se encienden y apagan (cian = encendido); AOD no se puede con el ahorro; Reloj abre la app; la batería abre Ajustes > Batería |
| T183 | Panel rápido: brillo de un extremo a otro | El brillo cambia en vivo, mínimo 10 %, y se mantiene tras reiniciar |
| T184 | 10 min de uso normal por todas las pantallas nuevas | Sin reinicios ni pantallas en blanco. Copiar el registro serie completo |

## 16. Bienestar (`BIENESTAR.md`)

Lleva el reloj en la muñeca de siempre, bien ajustado. Cuenta los pasos tú (en voz alta o con un contador) y anota
la cifra del reloj **antes y después** de cada prueba (app Bienestar, tarjeta Pasos). Marca OK, FALLO o NO
REALIZADA. Las cifras de T187-T192 sirven para ajustar los umbrales del detector: envíalas aunque salgan bien.

| # | Prueba | Resultado esperado |
|---|---|---|
| T185 | Arrancar con el monitor abierto | Líneas `watch_act`: `servicio de actividad: motor … B (RAM interna), caché … B (PSRAM), anillo 64 muestras` y `en marcha: IMU contando…`. Bienestar está en el cajón con su icono de tres anillos. Copiar las líneas |
| T186 | Reloj sobre la mesa 2 min; Bienestar > Ajustes > Diagnóstico del sensor | 0 pasos nuevos. "Muestras" sube unas 49 por segundo (unas 25 tras 30 s quieto, "Lectura" 40 ms); "Perdidas" e "Inválidas" en 0 o casi. Anotar todos los valores |
| T187 | Caminar 100 pasos a ritmo normal, tres veces | Cada vez 100 ± 5. Anotar las tres cifras |
| T188 | 100 pasos despacio (paseo) y 100 deprisa | Anotar las dos cifras (se espera ± 10 % despacio) |
| T189 | 100 pasos con esa mano en el bolsillo y 100 empujando un carro o con la mano apoyada | Anotar: se esperan pasos de menos (límite conocido) |
| T190 | Entrenar > Correr al aire libre; correr 200 pasos contados | 200 ± 10; la sesión dice pasos, distancia (est.) y ritmo. Anotar |
| T191 | 10 min sentado escribiendo o con el móvil | Anotar cuántos pasos suben (ideal 0) |
| T192 | 20 gestos sueltos (mirar la hora, rascarse, aplaudir 5 veces, golpear la mesa), cepillarse los dientes 30 s y 10 min en coche o autobús | Anotar los pasos que suben en cada parte (ideal 0; cepillarse puede contar: límite conocido) |
| T193 | Caminar al aire libre: caminar 1 min, parar de pie 40 s, volver a caminar | ~20 s después de parar, "Pausa automática" (sin vibración: solo en pantalla); al caminar, "En curso" en unos 3 s. El tiempo activo no incluye la espera |
| T194 | Pausar a mano y esperar 5 min sin moverse | No se reanuda al caminar un poco; a los 5 min termina solo con el resumen. La pausa final no cuenta en el tiempo total |
| T195 | Ajustes: apagar la pausa automática y repetir T193; luego la reanudación; luego el fin (T194). Reiniciar | La función apagada no actúa y las otras sí. Tras reiniciar siguen como se dejaron |
| T196 | Pantalla apagada y la app cerrada: caminar 200 pasos | Al encender, los 200 pasos están en Bienestar |
| T197 | A mitad de un entrenamiento, reiniciar el reloj (en el monitor, Ctrl+T y Ctrl+R; o apagarlo manteniendo el botón y volver a encenderlo) | Vuelve en pausa con sus tiempos; "Reanudar" sigue. El día conserva los pasos (como mucho faltan los de los últimos 5 min). Copiar la línea `estado restaurado` |
| T198 | Llevar el reloj puesto al pasar la medianoche (sin cambiar la hora) | Al día siguiente: el anillo de pasos empieza en 0 y el historial tiene ayer una sola vez. Copiar la línea `día cerrado`. **No** cambies la fecha a mano para esta prueba |
| T199 | En la esfera: desde el borde derecho hacia la izquierda; desde el centro; dedo quieto en el borde y luego arrastrar; en una app, el borde derecho; panel rápido y cajón | Solo el primero abre las tarjetas, siguiendo al dedo. En una app el borde derecho sigue siendo "atrás". Panel y cajón como antes. Con un entrenamiento en marcha, un roce corto no abre |
| T200 | Abrir y cerrar Bienestar 20 veces (app, tarjetas, selector, historial), con un entrenamiento en marcha | Sin reinicios ni tirones. RAM interna y PSRAM libres en Device Care iguales al principio y al final (anotar los cuatro valores) |
| T201 | Entrenamiento de 1 h caminando, con la pantalla apagada casi todo el rato | Sin reinicios. Anotar en Diagnóstico: "Pila libre (mín.)" (esperado > 1000 B), "Perdidas" y "Guardados / errores" (errores 0) |
| T202 | Ajustes > Calibrar distancia en una pista de 400 m (o un recorrido medido), caminando; luego repetir los 400 m | Anotar el error antes de calibrar y la zancada nueva; en la repetición la distancia (est.) da 400 m ± 5 % |
| T203 | Terminar un entrenamiento corto, mirar el resumen y reiniciar como en T197 | El resumen se ve igual tras reiniciar y el entrenamiento está **una sola vez** en el historial. (Sin LittleFS solo se probó en el PC: en el reloj no se puede provocar sin formatear, y no se debe formatear)
| T204 | Batería: 2 h con "Contar pasos" encendido y 2 h apagado, pantalla apagada, uso parecido | Anotar el % gastado en cada caso (cuesta energía tener el sensor siempre encendido) |

## Qué enviarme

- El registro serie completo de la sesión.
- Las fotos de los pasos 3, 10 y 14.
- Las coordenadas de las esquinas (paso 10).
- Las cifras del medidor (paso 4).
- Cualquier cosa que se vea mal, se corte, parpadee o no responda.

Con eso se cierran las decisiones D1 (ADC y pin TS) y D17 (zona horaria del RTC), y las tareas de F1-F3 pasan de "PC" a "verificado".
