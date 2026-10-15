/*
 * battery_thresholds — umbrales de batería baja y crítica.
 *
 * ESTADO: PROVISIONALES (decisión D4 de docs/ROADMAP.md). No se han calibrado
 * con la batería del reloj: son los valores habituales en relojes y móviles,
 * con histéresis para no oscilar. Cuando existan medidas reales (descarga
 * completa con registro de % y VBAT, pruebas T4/T14 en adelante) se sustituirán
 * aquí y BATT_THRESHOLDS_CALIBRATED pasará a 1. Nada más del código depende
 * de que sean provisionales salvo el texto que lo indica en la interfaz.
 */
#pragma once

#define BATT_THRESHOLDS_CALIBRATED 0

/* Entrar / salir, en % del fuel gauge. Empeorar es inmediato; mejorar exige
 * llegar al umbral de salida. */
#define BATT_LOW_ENTER_PCT      15
#define BATT_LOW_EXIT_PCT       18
#define BATT_CRITICAL_ENTER_PCT 5
#define BATT_CRITICAL_EXIT_PCT  8
