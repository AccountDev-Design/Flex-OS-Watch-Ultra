/*
 * Clave pública de confianza de las actualizaciones (ECDSA P-256, SEC1 sin
 * comprimir, 65 B). Solo la PÚBLICA: la privada vive fuera del repositorio.
 *
 * Vacía a propósito: todavía no hay clave de publicación, así que el reloj
 * comprueba y enseña las novedades pero NO instala nada ("Este reloj no tiene
 * clave para verificar actualizaciones"). Para activarlo:
 *     tools/ota/ota_sign.py keygen --out ~/flexota/priv.pem
 *     tools/ota/ota_sign.py pubkey --key ~/flexota/priv.pem
 * y pegar aquí lo que imprime (sustituye las dos definiciones de abajo).
 */
#pragma once

#include <stdint.h>

#define OTA_TRUSTED_KEY_SET 0
static const uint8_t kOtaTrustedKey[65] = {0};
