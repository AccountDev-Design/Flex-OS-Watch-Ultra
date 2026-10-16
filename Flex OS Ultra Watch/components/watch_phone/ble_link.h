/*
 * ble_link — transporte Flex Link sobre Bluetooth LE con el reloj de
 * central (NimBLE) y el GattServer.kt de Flex Phone de periférico.
 * Privado de watch_phone.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "flex_phone_transport.h"

/**
 * El transporte (siempre el mismo objeto). `wake` se llama desde la tarea
 * de NimBLE cuando llega una trama o cambia el canal; no puede bloquear.
 * La pila Bluetooth no se inicia hasta el primer start(): si el usuario no
 * enciende Flex Phone, no cuesta ni RAM ni radio.
 */
FlexPhoneTransport *ble_link_transport(void (*wake)(void));
/** ¿Hay tramas recibidas esperando? */
bool ble_link_rx_pending(void);
/** Qué hace el canal, en palabras (copia segura). */
void ble_link_text(char *out, size_t n);
/** RSSI de la conexión o del último anuncio visto (0 = desconocido). */
int8_t ble_link_rssi(void);
/** Borra todos los emparejamientos Bluetooth guardados (olvidar teléfono). */
void ble_link_forget(void);
