# Progreso de Flex OS Watch Ultra

## Criterio de avance (fijo desde la Fase 0)

- Los pesos por fase son los de `docs/AUDITORIA-FASE0.md` §7.
- Dentro de cada fase, un bloque cuenta el **50 %** cuando compila y pasa las pruebas de PC.
- Cuenta el **100 %** solo cuando pasa la prueba física en el reloj.
- Generar código que no compila o que no tiene pruebas no suma.

Para las fases que avanzan en varios bloques, el reparto interno queda fijado desde B1:

| Fase | Peso | Reparto interno |
|---|---|---|
| F0 Auditoría y plan | 5 | — |
| F1 Arranque (BSP, pantalla, táctil, LVGL 9, I2C) | 10 | — |
| F2 Batería | 15 | lectura y estados 9 · indicador y animación de carga 6 |
| F3 Tema y shell | 20 | tema y tokens 5 · esfera 5 · panel rápido 5 · ajustes y navegación 5 |
| F4 Servicios (NVS/KV, RTC+SNTP, IMU, suspensión, brillo, almacenamiento) | 10 | — |
| F5 Wi-Fi + flex_link | 15 | — |
| F6 Navegador remoto | 8 | — |
| F7 OTA | 7 | — |
| F8 Account/Cloud, consumo, estabilidad | 10 | — |

## Estado tras B1 (2026-10-15)

```
Global  [███░░░░░░░░░░░░░░░░░]  17 %   (verificado en hardware: 5 %)
```

| Fase | Bloque | Implementado | Compila | Pruebas de PC | Prueba física | Puntos |
|---|---|---|---|---|---|---|
| F0 | Auditoría, plan, versión ESP-IDF confirmada (5.5.5), discrepancia de LVGL resuelta | sí | — | — | — | **5 / 5** |
| F1 | BSP + CO5300 + FT3168 + LVGL 9.5 + I2C | sí | sí | vista previa en el PC | **pendiente** (T1-T3, T9, T11, T12) | 5 / 10 |
| F2 | Lectura AXP2101 en solo lectura y estados | sí | sí | 87/87 | **pendiente** (T4-T8) | 4,5 / 15 |
| F2 | Indicador y animación de carga | no (B2) | — | — | — | 0 |
| F3 | Tema y tokens (paleta, Onyx, vidrio, Outfit) | sí | sí | sí (tinte y fondo) | **pendiente** (T2, T13) | 2,5 / 20 |
| F3 | Esfera, panel rápido, ajustes | no | — | — | — | 0 |
| F4-F8 | — | no | — | — | — | 0 |

- **Total: 17 / 100.**
- Solo los 5 puntos de F0 no dependen del hardware.
- Los 12 restantes se quedan en la mitad de su valor hasta superar las pruebas T1-T13 de `docs/B1-ARRANQUE.md`. Si alguna falla, se descuenta.

**Flash de 32 MB:**
- Diseñada y compilada.
- Acceso por encima de 16 MB verificado solo en el código de ESP-IDF; **no** se cuenta como aprovechada hasta T10 y la prueba de escritura (docs/FLASH-32MB.md §6).

**Prueba en hardware informada por el usuario (2026-10-15):**
- B1 se compiló en su PC con el registro de componentes, se flasheó y la prueba inicial en el reloj fue satisfactoria.
- Confirma que la resolución del registro funciona y que el arranque es correcto.
- No cambia la puntuación hasta tener los registros de T1-T13.

**Reorganización (2026-10-15):**
- El proyecto ESP-IDF pasa a `Flex OS Ultra Watch/` sin cambiar ni un byte del firmware (47 archivos renombrados).
- Las imágenes compiladas solo difieren de las de B1 en la fecha y hora de compilación y en los hashes derivados.

**Bloqueos:**
- Pruebas físicas: requieren tu reloj.
- `dependencies.lock`: tu PC ya resolvió las dependencias contra el registro. Falta subir ese archivo a `Flex OS Ultra Watch/`, porque este entorno no accede a `components.espressif.com`.

**Siguiente bloque propuesto (B2):** decidir, con los registros de T8, si se activa el ADC de VBAT y se desactiva el pin TS como en el código oficial. Después, indicador de batería en el tema y animación de carga disparada por `charging_started`.
