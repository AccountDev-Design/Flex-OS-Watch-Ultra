# F4 — Servicios del sistema

Estado de cada bloque de F4 (pesos de `ROADMAP.md`). Las referencias son la auditoría de F4: cómo resuelve cada servicio el Flex OS Ultra de referencia y qué hardware tiene esta placa.

| Bloque | Contenido | Estado |
|---|---|---|
| F4.1 | Ajustes persistentes en NVS, escritor único | implementado · pruebas de PC · prueba física pendiente (T46, T47) |
| F4.2 | Poner en hora el RTC, zona horaria, 12/24 h | pendiente |
| F4.3 | Cronómetro, temporizador y alarmas | pendiente |
| F4.4 | IMU QMI8658 y app de sensores | pendiente |
| F4.5 | Apagado de pantalla por inactividad | pendiente |
| F4.6 | LittleFS en `storage` | pendiente |

## F4.1. Ajustes persistentes

**Componente `watch_settings`.**
- Los ajustes viven en RAM.
  - Quien los lee recibe una copia (`watch_settings_get`).
  - Quien los cambia entrega la copia modificada (`watch_settings_update`). El cambio vale al momento.
- Solo la tarea `watch_settings` (prioridad 2) escribe en NVS:
  - Guarda 1,5 s después del último cambio y nunca más de 30 s después del primero, como la referencia (`AppFramework.h:1346`).
  - Solo escribe las claves que cambiaron.
  - Si falla, reintenta a los 10 s y lo cuenta en Diagnóstico.
- `watch_settings_flush()` guarda en el momento. Se usará antes de apagar la pantalla (F4.5).

**Lógica pura con pruebas de PC (`ws_logic.c`).**
- Valores por defecto y normalización: cada valor fuera de rango pasa al más cercano válido.
- El instante de guardado.
- Bloques binarios con magia, versión, longitud y CRC-32. Los usarán las alarmas; un bloque dañado nunca se usa.

**Ajustes guardados** (espacio `flexws` de la partición `nvs`; `partitions.csv` no cambia):

| Clave | Valor | Por defecto | Rango |
|---|---|---|---|
| `ver` | versión del esquema | 1 | — |
| `bright` | brillo | 70 % | 10-100 % |
| `scr_to` | apagado de pantalla (F4.5) | 15 s | 5, 10, 15, 30, 60 s |
| `h24` | formato de 24 h (F4.2, D18) | sí | — |
| `tz_min` | zona horaria (F4.2, D17) | −300 (Lima, UTC−5) | UTC−12 a UTC+14, en pasos de 15 min |
| `wall` | fondo | negro | negro, Onyx |
| `face` | estilo de esfera | 0 | 0-2 |
| `alarms` | bloque de alarmas (F4.3) | — | 256 B como máximo |

**En la interfaz.**
- **Brillo:** el control del panel rápido y de Ajustes > Pantalla guarda el valor al soltarlo. Arrastrarlo no escribe en cada paso; la vista previa lo comprueba: una sola escritura.
- **Arranque:** se leen los ajustes antes de iniciar la pantalla. Se aplican el brillo guardado y el fondo.
- **Ajustes > Pantalla > Fondo:** cambia entre negro AMOLED y Onyx, se guarda y la esfera se rehace al momento.
  - Si el toque llega en mitad de una animación de páginas, la esfera se rehace al terminar la animación. La prueba "mono" encontró ese caso: la esfera se quedaba con el fondo anterior.
- **Ajustes > Diagnóstico > Ajustes guardados:**
  - si se leyeron o son los valores por defecto;
  - si hay cambios pendientes;
  - las escrituras y los errores.
- **Sin NVS:** la página Pantalla dice que los cambios valen hasta reiniciar.

**Recuperación.** Si la partición `nvs` está llena o es de otra versión de NVS, se borra **solo esa partición** y se reinicia. Es la recuperación estándar de ESP-IDF y queda en el registro. No se toca ninguna otra partición ni se hace `erase-flash`.

**Iconos nuevos** (16, vectoriales, probados a 20, 32 y 64 px): fondo, esfera, zona horaria, alarma, temporizador, cronómetro, aplicaciones, sensores, apagado, almacenamiento y controles (más, menos, reproducir, pausa, detener, vuelta).

### Pruebas en el reloj

| # | Prueba | Cómo | Resultado esperado |
|---|---|---|---|
| T46 | Los ajustes sobreviven al reinicio | Pon el brillo en ~30 % y el fondo en Onyx; espera 3 s; pulsa RESET (o desconecta y vuelve a encender) | Arranca con el brillo ~30 % y el fondo Onyx. En el registro: `ajustes leidos (esquema 1): brillo 30 %` y antes `ajustes guardados en N ms` |
| T47 | Diagnóstico de NVS | Ajustes > Diagnóstico > Ajustes guardados | "leídos", escrituras ≥ 1 tras un cambio, errores 0. Anota cuántos ms tarda en guardar (registro) y si la pantalla da un tirón en ese momento |
